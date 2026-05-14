from __future__ import annotations

import argparse
import random
import subprocess
import sys
from pathlib import Path

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from .abc_runner import apply_action_to_temp_state
from .dataset import BranchDataset, OnlineDataset
from .features import FeatureNormalizer, extract_aig_graph_stats, extract_features
from .hpwl_eval import evaluate_hpwl
from .models import PolicyModel, QModel
from .training_helpers import (
    clone_state_dict,
    cycle_loader,
    device,
    loss_summary,
    make_stage_logger,
    model_hparams,
    regression_loss,
    should_log_epoch,
    use_normalized_targets,
)
from .utils import (
    benchmark_path,
    checkpoint_dir_for_budget,
    configure_runtime_env,
    ensure_project_dirs,
    get_actions,
    get_hpwl_counter_value,
    load_config,
    make_state_id,
    make_work_subdir,
    models_dir,
    num_actions,
    online_checkpoint_budgets,
    online_train_circuit_schedule,
    set_seed,
    write_json,
)


def _v_from_policy(q_model: QModel, policy: PolicyModel, x: torch.Tensor) -> torch.Tensor:
    probs = F.softmax(policy(x), dim=-1)
    q_all = q_model.all_actions(x)
    return torch.sum(probs * q_all, dim=-1)


def _q_ctor_args(config: dict) -> tuple:
    hp = model_hparams(config)
    return (hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])


def _policy_ctor_args(config: dict) -> tuple:
    hp = model_hparams(config)
    return (hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])


def _train_awac(
    config: dict,
    offline_ds: BranchDataset,
    q_model: QModel,
    policy: PolicyModel,
    train_device: torch.device,
    epochs: int,
    log_fn=None,
    log_prefix: str = "awac",
    online_ds: OnlineDataset | None = None,
) -> tuple[QModel, PolicyModel, dict[str, list[float]]]:
    q_model = q_model.to(train_device)
    policy = policy.to(train_device)
    target_q = QModel(*_q_ctor_args(config))
    target_q.load_state_dict(q_model.state_dict())
    target_q = target_q.to(train_device)
    q_opt = torch.optim.Adam(q_model.parameters(), lr=float(config["offline"]["lr"]))
    policy_opt = torch.optim.Adam(policy.parameters(), lr=float(config["offline"]["lr"]))
    loader = DataLoader(offline_ds, batch_size=int(config["offline"]["batch_size"]), shuffle=True)
    online_loader = None
    online_iter = None
    if online_ds is not None and len(online_ds):
        online_loader = DataLoader(online_ds, batch_size=max(1, min(len(online_ds), int(config["offline"]["batch_size"]))), shuffle=True)
        online_iter = cycle_loader(online_loader)
    awac_lambda = float(config.get("awac", {}).get("lambda", 1.0))
    online_weight = float(config.get("online", {}).get("lambda_online", 1.0))
    best_metric = float("inf")
    best_q = clone_state_dict(q_model)
    best_policy = clone_state_dict(policy)
    q_losses: list[float] = []
    actor_losses: list[float] = []
    total_losses: list[float] = []
    gamma = float(config["gamma"])

    for epoch_idx in range(1, epochs + 1):
        q_running = 0.0
        actor_running = 0.0
        num_batches = 0
        for offline_batch in loader:
            x = offline_batch["x"].to(train_device)
            a = offline_batch["action_id"].to(train_device)
            r = offline_batch["reward"].to(train_device)
            x_next = offline_batch["x_next"].to(train_device)
            online_batch = next(online_iter) if online_iter is not None else None

            with torch.no_grad():
                v_next = _v_from_policy(target_q, policy, x_next)
                target = r + gamma * v_next
            q_loss = regression_loss(config, q_model(x, a), target)
            if online_batch is not None:
                x_on = online_batch["x"].to(train_device)
                a_on = online_batch["action_id"].to(train_device)
                r_on = online_batch["reward"].to(train_device)
                x_next_on = online_batch["x_next"].to(train_device)
                with torch.no_grad():
                    v_next_on = _v_from_policy(target_q, policy, x_next_on)
                    target_on = r_on + gamma * v_next_on
                q_loss = q_loss + online_weight * regression_loss(config, q_model(x_on, a_on), target_on)
            q_opt.zero_grad()
            q_loss.backward()
            q_opt.step()

            with torch.no_grad():
                q_data = q_model(x, a)
                v = _v_from_policy(q_model, policy, x)
                weights = torch.exp((q_data - v) / awac_lambda).clamp(max=100.0)
            log_probs = F.log_softmax(policy(x), dim=-1)
            bc_logp = log_probs.gather(1, a.unsqueeze(-1)).squeeze(-1)
            actor_loss = -(weights * bc_logp).mean()
            if online_batch is not None:
                with torch.no_grad():
                    q_data_on = q_model(x_on, a_on)
                    v_on = _v_from_policy(q_model, policy, x_on)
                    weights_on = torch.exp((q_data_on - v_on) / awac_lambda).clamp(max=100.0)
                log_probs_on = F.log_softmax(policy(x_on), dim=-1)
                bc_logp_on = log_probs_on.gather(1, a_on.unsqueeze(-1)).squeeze(-1)
                actor_loss = actor_loss + online_weight * (-(weights_on * bc_logp_on).mean())
            policy_opt.zero_grad()
            actor_loss.backward()
            policy_opt.step()

            q_running += float(q_loss.item())
            actor_running += float(actor_loss.item())
            num_batches += 1

        target_q.load_state_dict(q_model.state_dict())
        q_epoch = q_running / max(num_batches, 1)
        actor_epoch = actor_running / max(num_batches, 1)
        total_epoch = q_epoch + actor_epoch
        q_losses.append(q_epoch)
        actor_losses.append(actor_epoch)
        total_losses.append(total_epoch)
        if total_epoch <= best_metric:
            best_metric = total_epoch
            best_q = clone_state_dict(q_model)
            best_policy = clone_state_dict(policy)
        if log_fn is not None and should_log_epoch(epoch_idx, epochs):
            log_fn(f"[{log_prefix}] epoch {epoch_idx}/{epochs} q_loss={q_epoch:.6f} actor_loss={actor_epoch:.6f} total={total_epoch:.6f}")

    q_model.load_state_dict(best_q)
    policy.load_state_dict(best_policy)
    q_model = q_model.cpu()
    policy = policy.cpu()
    q_model.eval()
    policy.eval()
    curves = {
        "q_loss": q_losses,
        "actor_loss": actor_losses,
        "total_loss": total_losses,
    }
    return q_model, policy, curves


def _select_action(policy: PolicyModel, x_norm, stochastic: bool) -> int:
    x = torch.as_tensor(x_norm, dtype=torch.float32).unsqueeze(0)
    with torch.no_grad():
        probs = F.softmax(policy(x), dim=-1).squeeze(0)
    if stochastic:
        return int(torch.distributions.Categorical(probs=probs).sample().item())
    return int(torch.argmax(probs).item())


def _collect_online_episode(config: dict, normalizer: FeatureNormalizer, policy: PolicyModel, episode: int, circuit: str) -> tuple[list[dict], dict[str, object]]:
    actions = get_actions(config)
    stage_dir = make_work_subdir(config, "awac_online", circuit)
    state_path = str(benchmark_path(config, circuit))
    current_state_id = make_state_id(circuit, 0, f"awac_episode{episode}_init")
    initial_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
    hpwl_initial = evaluate_hpwl(state_path, circuit, current_state_id, config)
    hpwl_current = hpwl_initial
    records = []
    sequence: list[str] = []
    for t in range(int(config["horizon"])):
        x = extract_features(state_path, t, int(config["horizon"]), initial_stats, abc_binary=config["abc_binary"])
        x_norm = normalizer.transform(x)
        action_id = _select_action(policy, x_norm, stochastic=True)
        action = actions[action_id]
        sequence.append(action["name"])
        next_state_id = make_state_id(circuit, t + 1, f"awac_episode{episode}_{action['name']}", current_state_id)
        next_state_path = apply_action_to_temp_state(state_path, stage_dir, next_state_id, action["abc_cmd"], abc_binary=config["abc_binary"])
        hpwl_next = evaluate_hpwl(next_state_path, circuit, next_state_id, config)
        reward = (hpwl_current - hpwl_next) / max(hpwl_initial, 1e-12)
        x_next = extract_features(next_state_path, t + 1, int(config["horizon"]), initial_stats, abc_binary=config["abc_binary"])
        records.append({
            "x": x_norm.tolist(),
            "action_id": action_id,
            "reward": float(reward),
            "x_next": normalizer.transform(x_next).tolist(),
            "return": 0.0,
            "circuit": circuit,
            "t": t,
        })
        state_path = next_state_path
        current_state_id = next_state_id
        hpwl_current = hpwl_next
    episode_summary = {
        "circuit": circuit,
        "sequence": sequence,
        "initial_hpwl": float(hpwl_initial),
        "final_hpwl": float(hpwl_current),
        "hpwl_ratio_vs_initial": float(hpwl_current / max(hpwl_initial, 1e-12)),
        "episode_return": float(sum(row["reward"] for row in records)),
    }
    return records, episode_summary


def _save_online_checkpoint(config: dict, budget: int, q_model: QModel, policy: PolicyModel) -> None:
    budget_dir = checkpoint_dir_for_budget(config, budget)
    torch.save(q_model.state_dict(), budget_dir / "awac_q_online.pt")
    torch.save(policy.state_dict(), budget_dir / "awac_policy_online.pt")


def _load_offline_dataset(config: dict) -> BranchDataset:
    return BranchDataset(
        torch.load(Path(config["data_dir"]) / "processed" / "d2_train.pt", map_location="cpu"),
        normalized_reward=use_normalized_targets(config),
    )


def _load_normalizer(config: dict) -> FeatureNormalizer:
    return FeatureNormalizer.load(Path(config["data_dir"]) / "processed" / "feature_normalizer.json")


def _train_offline(config: dict, train_device: torch.device) -> tuple[BranchDataset, QModel, PolicyModel, dict]:
    offline_ds = _load_offline_dataset(config)
    q_model = QModel(*_q_ctor_args(config))
    policy = PolicyModel(*_policy_ctor_args(config))
    run_id, log_path, latest_log_path, log_fn = make_stage_logger(config, "awac_offline")
    log_fn(f"Starting AWAC offline training run_id={run_id}")
    log_fn(f"Config path: {Path(config['_config_path']).resolve()}")
    log_fn(f"Device: {train_device}")
    log_fn(f"Branch records: {len(offline_ds)}")
    log_fn(f"Use normalized targets: {use_normalized_targets(config)}")
    q_model, policy, curves = _train_awac(config, offline_ds, q_model, policy, train_device, int(config["offline"]["q_epochs"]), log_fn=log_fn, log_prefix="awac_offline")
    ckpt_dir = models_dir(config)
    q_path = ckpt_dir / "awac_q.pt"
    policy_path = ckpt_dir / "awac_policy.pt"
    torch.save(q_model.state_dict(), q_path)
    torch.save(policy.state_dict(), policy_path)
    summary = {
        "run_id": run_id,
        "config_path": str(Path(config["_config_path"]).resolve()),
        "device": str(train_device),
        "branch_records": len(offline_ds),
        "use_normalized_targets": use_normalized_targets(config),
        "checkpoints": {
            "q": str(q_path),
            "policy": str(policy_path),
        },
        "curves": curves,
        "curve_summary": {name: loss_summary(values) for name, values in curves.items()},
        "log_path": str(log_path),
        "latest_log_path": str(latest_log_path),
    }
    run_summary_path = Path(config["result_dir"]) / "logs" / f"awac_offline_{run_id}.json"
    latest_summary_path = Path(config["result_dir"]) / "logs" / "awac_offline_latest.json"
    write_json(run_summary_path, summary)
    write_json(latest_summary_path, summary)
    log_fn(f"Saved summary: {run_summary_path}")
    return offline_ds, q_model, policy, summary


def _load_offline_models(config: dict) -> tuple[BranchDataset, QModel, PolicyModel]:
    offline_ds = _load_offline_dataset(config)
    q_model = QModel(*_q_ctor_args(config))
    policy = PolicyModel(*_policy_ctor_args(config))
    ckpt_dir = models_dir(config)
    q_model.load_state_dict(torch.load(ckpt_dir / "awac_q.pt", map_location="cpu"))
    policy.load_state_dict(torch.load(ckpt_dir / "awac_policy.pt", map_location="cpu"))
    q_model.eval()
    policy.eval()
    return offline_ds, q_model, policy


def _run_online(config: dict, train_device: torch.device, offline_ds: BranchDataset, q_model: QModel, policy: PolicyModel) -> tuple[QModel, PolicyModel, dict]:
    normalizer = _load_normalizer(config)
    online_buffer: list[dict] = []
    budget_checkpoints = set(online_checkpoint_budgets(config))
    total_episodes = int(config["online"]["episodes"])
    episode_circuits = online_train_circuit_schedule(config, total_episodes)
    run_id, log_path, latest_log_path, log_fn = make_stage_logger(config, "awac_online")
    sequence_dir = Path(config["result_dir"]) / "sequences" / "awac_online_train" / run_id
    sequence_dir.mkdir(parents=True, exist_ok=True)
    hpwl_start = get_hpwl_counter_value(config, "online")
    episode_summaries: list[dict[str, object]] = []
    log_fn(f"Starting AWAC online training run_id={run_id}")
    log_fn(f"Config path: {Path(config['_config_path']).resolve()}")
    log_fn(f"Device: {train_device}")
    log_fn(f"Episodes: {total_episodes}")
    log_fn(f"Update epochs per episode: {int(config['online']['update_epochs_per_episode'])}")
    log_fn(f"Online weight: {float(config['online'].get('lambda_online', 1.0))}")

    for episode in range(total_episodes):
        hpwl_before_episode = get_hpwl_counter_value(config, "online")
        episode_records, episode_summary = _collect_online_episode(config, normalizer, policy, episode, episode_circuits[episode])
        online_buffer.extend(episode_records)
        online_ds = OnlineDataset(online_buffer)
        q_model, policy, update_curves = _train_awac(
            config,
            offline_ds,
            q_model,
            policy,
            train_device,
            int(config["online"]["update_epochs_per_episode"]),
            online_ds=online_ds,
        )
        hpwl_after_episode = get_hpwl_counter_value(config, "online")
        episode_summary.update(
            {
                "episode": episode,
                "online_buffer_size": len(online_buffer),
                "hpwl_calls_this_episode": int(hpwl_after_episode - hpwl_before_episode),
                "update_summary": {name: loss_summary(values) for name, values in update_curves.items()},
            }
        )
        episode_summaries.append(episode_summary)
        write_json(sequence_dir / f"episode_{episode:03d}.json", episode_summary)
        if (episode + 1) in budget_checkpoints:
            _save_online_checkpoint(config, episode + 1, q_model, policy)
            log_fn(f"Saved budget checkpoint for episode {episode + 1} at {checkpoint_dir_for_budget(config, episode + 1)}")
        log_fn(
            f"[episode {episode + 1}/{total_episodes}] circuit={episode_summary['circuit']} "
            f"initial_hpwl={episode_summary['initial_hpwl']:.4f} final_hpwl={episode_summary['final_hpwl']:.4f} "
            f"return={episode_summary['episode_return']:.6f} hpwl_calls={episode_summary['hpwl_calls_this_episode']} "
            f"sequence={' '.join(episode_summary['sequence'])}"
        )

    ckpt_dir = models_dir(config)
    q_path = ckpt_dir / "awac_q_online.pt"
    policy_path = ckpt_dir / "awac_policy_online.pt"
    torch.save(q_model.state_dict(), q_path)
    torch.save(policy.state_dict(), policy_path)
    summary = {
        "run_id": run_id,
        "config_path": str(Path(config["_config_path"]).resolve()),
        "device": str(train_device),
        "episodes": int(config["online"]["episodes"]),
        "transitions": len(online_buffer),
        "hpwl_calls_this_run": int(get_hpwl_counter_value(config, "online") - hpwl_start),
        "sequence_dir": str(sequence_dir),
        "checkpoints": {
            "q": str(q_path),
            "policy": str(policy_path),
        },
        "episode_summaries": episode_summaries,
        "log_path": str(log_path),
        "latest_log_path": str(latest_log_path),
    }
    run_summary_path = Path(config["result_dir"]) / "logs" / f"awac_online_{run_id}.json"
    latest_summary_path = Path(config["result_dir"]) / "logs" / "awac_online_latest.json"
    write_json(run_summary_path, summary)
    write_json(latest_summary_path, summary)
    log_fn(f"Saved summary: {run_summary_path}")
    return q_model, policy, summary


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--stage", choices=["offline", "online", "full"], default="full")
    args = parser.parse_args()

    config = load_config(args.config)
    ensure_project_dirs(config)
    configure_runtime_env(config)
    set_seed(int(config.get("seed", 0)))
    train_device = device()

    offline_ds: BranchDataset
    q_model: QModel
    policy: PolicyModel
    if args.stage in {"offline", "full"}:
        offline_ds, q_model, policy, _ = _train_offline(config, train_device)
    else:
        offline_ds, q_model, policy = _load_offline_models(config)

    if args.stage == "offline":
        return

    config["_hpwl_phase"] = "online"
    _run_online(config, train_device, offline_ds, q_model, policy)

    if args.stage == "full":
        subprocess.run([sys.executable, "-m", "src.evaluate", "--config", args.config, "--method", "awac", "--split", "test"], check=False)


if __name__ == "__main__":
    main()
