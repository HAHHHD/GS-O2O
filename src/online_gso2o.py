from __future__ import annotations

import argparse
from pathlib import Path
from typing import Callable

import numpy as np
import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from .abc_runner import apply_action_to_temp_state
from .dataset import BranchDataset, GreedyReturnDataset, OnlineDataset
from .features import FeatureNormalizer, extract_aig_graph_stats, extract_features
from .gso2o_policy import gso2o_mode_metadata, refine_cfg, select_action_gso2o
from .hpwl_eval import evaluate_hpwl
from .models import QModel, RewardModel, ValueModel
from .utils import (
    append_jsonl,
    benchmark_path,
    configure_runtime_env,
    ensure_dir,
    ensure_project_dirs,
    get_actions,
    get_hpwl_counter_value,
    linear_schedule,
    load_config,
    checkpoint_dir_for_budget,
    make_state_id,
    make_work_subdir,
    models_dir,
    monte_carlo_returns,
    num_actions,
    online_checkpoint_budgets,
    online_train_circuit_schedule,
    set_seed,
    utc_timestamp,
    write_json,
)


def _device() -> torch.device:
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def _model_hparams(config: dict) -> dict:
    model_cfg = config["model"]
    return {
        "feature_dim": int(model_cfg["feature_dim"]),
        "hidden_dim": int(model_cfg["hidden_dim"]),
        "num_layers": int(model_cfg.get("num_layers", 2)),
        "activation": str(model_cfg.get("activation", "leaky_relu")),
    }


def _use_normalized_targets(config: dict) -> bool:
    return bool(config.get("offline", {}).get("use_normalized_targets", True))


def _regression_loss(config: dict, pred: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    loss_name = str(config.get("offline", {}).get("loss", "huber")).lower()
    if loss_name == "mse":
        return torch.mean((pred - target) ** 2)
    delta = float(config.get("offline", {}).get("huber_delta", 1.0))
    return F.smooth_l1_loss(pred, target, beta=delta)


def _clone_state_dict(model: torch.nn.Module) -> dict[str, torch.Tensor]:
    return {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}


def _cql_penalty(model: QModel, x: torch.Tensor, action_id: torch.Tensor) -> torch.Tensor:
    q_all = model.all_actions(x)
    q_data = q_all.gather(1, action_id.view(-1, 1)).squeeze(1)
    return torch.logsumexp(q_all, dim=-1).mean() - q_data.mean()


def _make_logger(config: dict) -> tuple[str, Path, Path, Callable[[str], None]]:
    run_id = utc_timestamp()
    log_dir = Path(config["result_dir"]) / "logs"
    log_path = log_dir / f"gso2o_online_{run_id}.log"
    latest_log_path = log_dir / "gso2o_online_latest.log"
    latest_log_path.write_text("", encoding="utf-8")

    def log_fn(message: str) -> None:
        print(message, flush=True)
        for target in (log_path, latest_log_path):
            with target.open("a", encoding="utf-8") as handle:
                handle.write(message)
                handle.write("\n")

    return run_id, log_path, latest_log_path, log_fn


def _load_models(config: dict) -> tuple[RewardModel, ValueModel | None, list[QModel]]:
    hp = _model_hparams(config)
    ckpt_dir = models_dir(config)
    reward_model = RewardModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])
    reward_model.load_state_dict(torch.load(ckpt_dir / "reward.pt", map_location="cpu"))
    reward_model.eval()
    if bool(config.get("ablation", {}).get("immediate_reward_only", False)):
        return reward_model, None, []
    value_model = ValueModel(hp["feature_dim"], hp["hidden_dim"], hp["num_layers"], hp["activation"])
    value_model.load_state_dict(torch.load(ckpt_dir / "value.pt", map_location="cpu"))
    value_model.eval()
    q_ensemble = []
    for idx in range(int(config["model"]["ensemble_size"])):
        q_model = QModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])
        q_model.load_state_dict(torch.load(ckpt_dir / f"q_ensemble_{idx}.pt", map_location="cpu"))
        q_model.eval()
        q_ensemble.append(q_model)
    return reward_model, value_model, q_ensemble


def _cycle_loader(loader: DataLoader):
    while True:
        for batch in loader:
            yield batch


def _loss_summary(losses: list[float]) -> dict[str, float]:
    return {
        "initial": float(losses[0]),
        "final": float(losses[-1]),
        "best": float(min(losses)),
    }


def _save_budget_checkpoint(config: dict, budget: int, reward_model: RewardModel, value_model: ValueModel | None, q_ensemble: list[QModel]) -> None:
    budget_dir = checkpoint_dir_for_budget(config, budget)
    torch.save(reward_model.state_dict(), budget_dir / "reward_online.pt")
    if value_model is not None:
        torch.save(value_model.state_dict(), budget_dir / "value_online.pt")
        for idx, model in enumerate(q_ensemble):
            torch.save(model.state_dict(), budget_dir / f"q_ensemble_online_{idx}.pt")
    write_json(budget_dir / "gso2o_mode.json", gso2o_mode_metadata(config))


def _update_reward_model(
    config: dict,
    reward_model: RewardModel,
    offline_ds: BranchDataset,
    online_ds: OnlineDataset,
    device: torch.device,
) -> tuple[RewardModel, dict[str, float]]:
    hybrid = dict(config.get("gso2o_hybrid", {}))
    reward_model = reward_model.to(device)
    offline_state = {name: param.detach().clone() for name, param in reward_model.state_dict().items()}
    lr_scale = float(hybrid.get("reward_lr_scale", 1.0))
    optimizer = torch.optim.Adam(reward_model.parameters(), lr=float(config["offline"]["lr"]) * lr_scale)
    offline_loader = DataLoader(offline_ds, batch_size=int(config["offline"]["batch_size"]), shuffle=True)
    online_loader = DataLoader(online_ds, batch_size=max(1, min(len(online_ds), int(config["offline"]["batch_size"]))), shuffle=True) if len(online_ds) else None
    online_iter = _cycle_loader(online_loader) if online_loader is not None else None
    losses: list[float] = []
    best_loss = float("inf")
    best_state = _clone_state_dict(reward_model)
    anchor_weight = float(hybrid.get("reward_anchor_weight", 0.0))
    for _ in range(int(config["online"]["update_epochs_per_episode"])):
        running_loss = 0.0
        num_batches = 0
        for offline_batch in offline_loader:
            x_off = offline_batch["x"].to(device)
            a_off = offline_batch["action_id"].to(device)
            r_off = offline_batch["reward"].to(device)
            loss = _regression_loss(config, reward_model(x_off, a_off), r_off)
            if online_iter is not None:
                online_batch = next(online_iter)
                x_on = online_batch["x"].to(device)
                a_on = online_batch["action_id"].to(device)
                r_on = online_batch["reward"].to(device)
                loss = loss + float(config["online"]["lambda_online"]) * _regression_loss(config, reward_model(x_on, a_on), r_on)
            if anchor_weight > 0.0:
                anchor = torch.zeros((), device=device)
                for name, param in reward_model.state_dict().items():
                    anchor = anchor + torch.sum((param - offline_state[name].to(device)) ** 2)
                loss = loss + anchor_weight * anchor
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            running_loss += float(loss.item())
            num_batches += 1
        epoch_loss = running_loss / max(num_batches, 1)
        losses.append(epoch_loss)
        if epoch_loss <= best_loss:
            best_loss = epoch_loss
            best_state = _clone_state_dict(reward_model)
    reward_model.load_state_dict(best_state)
    reward_model = reward_model.cpu()
    reward_model.eval()
    return reward_model, _loss_summary(losses)


def _update_value_model(
    config: dict,
    value_model: ValueModel,
    offline_ds: GreedyReturnDataset,
    online_ds: OnlineDataset,
    device: torch.device,
) -> tuple[ValueModel, dict[str, float]]:
    value_model = value_model.to(device)
    optimizer = torch.optim.Adam(value_model.parameters(), lr=float(config["offline"]["lr"]))
    offline_loader = DataLoader(offline_ds, batch_size=int(config["offline"]["batch_size"]), shuffle=True)
    online_loader = DataLoader(online_ds, batch_size=max(1, min(len(online_ds), int(config["offline"]["batch_size"]))), shuffle=True) if len(online_ds) else None
    online_iter = _cycle_loader(online_loader) if online_loader is not None else None
    losses: list[float] = []
    best_loss = float("inf")
    best_state = _clone_state_dict(value_model)
    for _ in range(int(config["online"]["update_epochs_per_episode"])):
        running_loss = 0.0
        num_batches = 0
        for offline_batch in offline_loader:
            x_off = offline_batch["x"].to(device)
            ret_off = offline_batch["return"].to(device)
            loss = _regression_loss(config, value_model(x_off), ret_off)
            if online_iter is not None:
                online_batch = next(online_iter)
                x_on = online_batch["x"].to(device)
                ret_on = online_batch["return"].to(device)
                loss = loss + float(config["online"]["lambda_online"]) * _regression_loss(config, value_model(x_on), ret_on)
            optimizer.zero_grad()
            loss.backward()
            optimizer.step()
            running_loss += float(loss.item())
            num_batches += 1
        epoch_loss = running_loss / max(num_batches, 1)
        losses.append(epoch_loss)
        if epoch_loss <= best_loss:
            best_loss = epoch_loss
            best_state = _clone_state_dict(value_model)
    value_model.load_state_dict(best_state)
    value_model = value_model.cpu()
    value_model.eval()
    return value_model, _loss_summary(losses)


def _update_q_ensemble(
    config: dict,
    q_ensemble: list[QModel],
    value_model: ValueModel,
    offline_ds: BranchDataset,
    online_ds: OnlineDataset,
    device: torch.device,
    q_anchor_refs: list[QModel] | None = None,
) -> tuple[list[QModel], list[dict[str, float]]]:
    hybrid = dict(config.get("gso2o_hybrid", {}))
    gamma = float(config["gamma"])
    batch_size = int(config["offline"]["batch_size"])
    offline_alpha = float(hybrid.get("online_cql_alpha_offline", hybrid.get("offline_cql_alpha", 0.0)))
    online_alpha = float(hybrid.get("online_cql_alpha_online", 0.0))
    q_anchor_weight = float(hybrid.get("q_anchor_weight", 0.0))
    value_model = value_model.to(device)
    value_model.eval()
    updated = []
    summaries = []
    for model_idx, model in enumerate(q_ensemble):
        model = model.to(device)
        ref_model = None
        if q_anchor_refs is not None and model_idx < len(q_anchor_refs):
            ref_model = q_anchor_refs[model_idx].to(device)
            ref_model.eval()
        optimizer = torch.optim.Adam(model.parameters(), lr=float(config["offline"]["lr"]))
        offline_loader = DataLoader(offline_ds, batch_size=batch_size, shuffle=True)
        online_loader = DataLoader(online_ds, batch_size=max(1, min(len(online_ds), batch_size)), shuffle=True) if len(online_ds) else None
        online_iter = _cycle_loader(online_loader) if online_loader is not None else None
        losses: list[float] = []
        best_loss = float("inf")
        best_state = _clone_state_dict(model)
        for _ in range(int(config["online"]["update_epochs_per_episode"])):
            running_loss = 0.0
            num_batches = 0
            for offline_batch in offline_loader:
                x_off = offline_batch["x"].to(device)
                a_off = offline_batch["action_id"].to(device)
                r_off = offline_batch["reward"].to(device)
                x_next_off = offline_batch["x_next"].to(device)
                with torch.no_grad():
                    target_off = r_off + gamma * value_model(x_next_off)
                loss = _regression_loss(config, model(x_off, a_off), target_off)
                if offline_alpha > 0.0:
                    loss = loss + offline_alpha * _cql_penalty(model, x_off, a_off)
                if ref_model is not None and q_anchor_weight > 0.0:
                    with torch.no_grad():
                        ref_q = ref_model.all_actions(x_off)
                    loss = loss + q_anchor_weight * torch.mean((model.all_actions(x_off) - ref_q) ** 2)
                if online_iter is not None:
                    online_batch = next(online_iter)
                    x_on = online_batch["x"].to(device)
                    a_on = online_batch["action_id"].to(device)
                    ret_on = online_batch["return"].to(device)
                    loss = loss + float(config["online"]["lambda_online"]) * _regression_loss(config, model(x_on, a_on), ret_on)
                    if online_alpha > 0.0:
                        loss = loss + float(config["online"]["lambda_online"]) * online_alpha * _cql_penalty(model, x_on, a_on)
                optimizer.zero_grad()
                loss.backward()
                optimizer.step()
                running_loss += float(loss.item())
                num_batches += 1
            epoch_loss = running_loss / max(num_batches, 1)
            losses.append(epoch_loss)
            if epoch_loss <= best_loss:
                best_loss = epoch_loss
                best_state = _clone_state_dict(model)
        model.load_state_dict(best_state)
        model = model.cpu()
        model.eval()
        updated.append(model)
        summaries.append(_loss_summary(losses))
        if ref_model is not None:
            ref_model.cpu()
    return updated, summaries


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args()

    config = load_config(args.config)
    ensure_project_dirs(config)
    configure_runtime_env(config)
    set_seed(int(config.get("seed", 0)))
    config["_hpwl_phase"] = "online"
    device = _device()
    use_normalized_targets = _use_normalized_targets(config)
    run_id, log_path, latest_log_path, log_fn = _make_logger(config)

    reward_model, value_model, q_ensemble = _load_models(config)
    q_anchor_refs = None
    if float(config.get("gso2o_hybrid", {}).get("q_anchor_weight", 0.0)) > 0.0:
        hp = _model_hparams(config)
        q_anchor_refs = []
        for model in q_ensemble:
            ref_model = QModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])
            ref_model.load_state_dict(_clone_state_dict(model))
            ref_model.eval()
            q_anchor_refs.append(ref_model)
    normalizer = FeatureNormalizer.load(Path(config["data_dir"]) / "processed" / "feature_normalizer.json")
    offline_branch = BranchDataset(
        torch.load(Path(config["data_dir"]) / "processed" / "d2_train.pt", map_location="cpu"),
        greedy_only=bool(config.get("ablation", {}).get("d1_greedy_only", False)),
        normalized_reward=use_normalized_targets,
    )
    offline_greedy = GreedyReturnDataset(
        torch.load(Path(config["data_dir"]) / "processed" / "greedy_trajs.pt", map_location="cpu"),
        normalized_return=use_normalized_targets,
    )
    actions = get_actions(config)
    online_buffer: list[dict] = []
    online_jsonl = Path(config["data_dir"]) / "online" / "online_buffer.jsonl"
    if online_jsonl.exists():
        online_jsonl.unlink()

    total_episodes = int(config["online"]["episodes"])
    episode_circuits = online_train_circuit_schedule(config, total_episodes)
    sequence_dir = ensure_dir(Path(config["result_dir"]) / "sequences" / "gso2o_online_train" / run_id)
    summary_path = Path(config["data_dir"]) / "online" / "summary.json"
    run_summary_path = Path(config["result_dir"]) / "logs" / f"gso2o_online_{run_id}.json"
    latest_summary_path = Path(config["result_dir"]) / "logs" / "gso2o_online_latest.json"
    hpwl_start = get_hpwl_counter_value(config, "online")
    budget_checkpoints = set(online_checkpoint_budgets(config))
    episode_summaries: list[dict[str, object]] = []

    log_fn(f"Starting GS-O2O online training run_id={run_id}")
    log_fn(f"Config path: {Path(args.config).resolve()}")
    log_fn(f"Device: {device}")
    base_update_reward_model = bool(config.get("online", {}).get("update_reward_model", True))
    log_fn(f"Episodes: {total_episodes}")
    log_fn(f"Update epochs per episode: {int(config['online']['update_epochs_per_episode'])}")
    log_fn(f"Use normalized targets: {use_normalized_targets}")
    log_fn(f"Regression loss: {config.get('offline', {}).get('loss', 'huber')}")
    log_fn(f"Update reward model online: {base_update_reward_model}")
    log_fn(f"Hybrid config: {dict(config.get('gso2o_hybrid', {}))}")
    log_fn(f"Refine config: {refine_cfg(config)}")

    for episode in range(total_episodes):
        epsilon = linear_schedule(episode, float(config["online"]["epsilon_start"]), float(config["online"]["epsilon_end"]), total_episodes)
        beta = linear_schedule(episode, float(config["online"]["beta_start"]), float(config["online"]["beta_end"]), total_episodes)
        circuit = episode_circuits[episode]
        stage_dir = make_work_subdir(config, "online", circuit)
        state_path = str(benchmark_path(config, circuit))
        current_state_id = make_state_id(circuit, 0, f"episode{episode}_init")
        initial_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
        hpwl_before_episode = get_hpwl_counter_value(config, "online")
        hpwl_initial = evaluate_hpwl(state_path, circuit, current_state_id, config)
        hpwl_current = hpwl_initial
        episode_records = []
        sequence = []
        action_history: list[int] = []

        for t in range(int(config["horizon"])):
            x = extract_features(state_path, t, int(config["horizon"]), initial_stats, abc_binary=config["abc_binary"])
            x_norm = normalizer.transform(x)
            select_context = {
                "episode": episode,
                "total_episodes": total_episodes,
                "t": t,
                "horizon": int(config["horizon"]),
                "action_history": action_history,
            }
            action_id = select_action_gso2o(
                config,
                x_norm,
                reward_model,
                q_ensemble,
                epsilon,
                beta,
                float(config["online"]["rho"]),
                config.get("ablation", {}),
                context=select_context,
            )
            action = actions[action_id]
            sequence.append(action["name"])
            action_history.append(action_id)
            next_state_id = make_state_id(circuit, t + 1, f"episode{episode}_{action['name']}", current_state_id)
            next_state_path = apply_action_to_temp_state(state_path, stage_dir, next_state_id, action["abc_cmd"], abc_binary=config["abc_binary"])
            hpwl_next = evaluate_hpwl(next_state_path, circuit, next_state_id, config)
            reward = (hpwl_current - hpwl_next) / max(hpwl_initial, 1e-12)
            x_next = extract_features(next_state_path, t + 1, int(config["horizon"]), initial_stats, abc_binary=config["abc_binary"])
            x_next_norm = normalizer.transform(x_next)
            episode_records.append({
                "circuit": circuit,
                "t": t,
                "x": x_norm.tolist(),
                "action_id": action_id,
                "reward": float(reward),
                "x_next": x_next_norm.tolist(),
            })
            state_path = next_state_path
            current_state_id = next_state_id
            hpwl_current = hpwl_next

        returns = monte_carlo_returns([row["reward"] for row in episode_records], float(config["gamma"]))
        for row, ret in zip(episode_records, returns):
            row["return"] = float(ret)
            online_buffer.append(row)
            append_jsonl(online_jsonl, row)

        online_dataset = OnlineDataset(online_buffer)
        freeze_after = refine_cfg(config).get("freeze_reward_after_episode")
        update_reward_model = base_update_reward_model and not (
            freeze_after is not None and episode >= int(freeze_after)
        )
        if update_reward_model:
            reward_model, reward_update = _update_reward_model(config, reward_model, offline_branch, online_dataset, device)
        else:
            reward_update = {"skipped": True, "frozen_after_episode": freeze_after}
        value_update = None
        q_updates = []
        if not bool(config.get("ablation", {}).get("immediate_reward_only", False)):
            assert value_model is not None
            value_model, value_update = _update_value_model(config, value_model, offline_greedy, online_dataset, device)
            q_ensemble, q_updates = _update_q_ensemble(
                config,
                q_ensemble,
                value_model,
                offline_branch,
                online_dataset,
                device,
                q_anchor_refs=q_anchor_refs,
            )

        hpwl_after_episode = get_hpwl_counter_value(config, "online")
        episode_summary = {
            "episode": episode,
            "circuit": circuit,
            "epsilon": float(epsilon),
            "beta": float(beta),
            "sequence": sequence,
            "initial_hpwl": float(hpwl_initial),
            "final_hpwl": float(hpwl_current),
            "hpwl_ratio_vs_initial": float(hpwl_current / max(hpwl_initial, 1e-12)),
            "episode_return": float(sum(row["reward"] for row in episode_records)),
            "online_buffer_size": len(online_buffer),
            "hpwl_calls_this_episode": int(hpwl_after_episode - hpwl_before_episode),
            "reward_update": reward_update,
            "value_update": value_update,
            "q_updates": q_updates,
        }
        episode_summaries.append(episode_summary)
        write_json(sequence_dir / f"episode_{episode:03d}.json", episode_summary)
        if (episode + 1) in budget_checkpoints:
            _save_budget_checkpoint(config, episode + 1, reward_model, value_model, q_ensemble)
            log_fn(f"Saved budget checkpoint for episode {episode + 1} at {checkpoint_dir_for_budget(config, episode + 1)}")
        log_fn(
            f"[episode {episode + 1}/{total_episodes}] circuit={circuit} epsilon={epsilon:.4f} beta={beta:.4f} "
            f"initial_hpwl={hpwl_initial:.4f} final_hpwl={hpwl_current:.4f} return={episode_summary['episode_return']:.6f} "
            f"hpwl_calls={episode_summary['hpwl_calls_this_episode']} sequence={' '.join(sequence)}"
        )

    ckpt_dir = models_dir(config)
    torch.save(reward_model.state_dict(), ckpt_dir / "reward_online.pt")
    if value_model is not None:
        torch.save(value_model.state_dict(), ckpt_dir / "value_online.pt")
        for idx, model in enumerate(q_ensemble):
            torch.save(model.state_dict(), ckpt_dir / f"q_ensemble_online_{idx}.pt")
    write_json(ckpt_dir / "gso2o_mode.json", gso2o_mode_metadata(config))
    torch.save(OnlineDataset(online_buffer).__dict__, Path(config["data_dir"]) / "online" / "online_buffer.pt")

    summary = {
        "run_id": run_id,
        "config_path": str(Path(args.config).resolve()),
        "device": str(device),
        "episodes": total_episodes,
        "transitions": len(online_buffer),
        "use_normalized_targets": use_normalized_targets,
        "regression_loss": str(config.get("offline", {}).get("loss", "huber")),
        "hpwl_calls_this_run": int(get_hpwl_counter_value(config, "online") - hpwl_start),
        "sequence_dir": str(sequence_dir),
        "log_path": str(log_path),
        "latest_log_path": str(latest_log_path),
        "episode_summaries": episode_summaries,
    }
    write_json(summary_path, summary)
    write_json(run_summary_path, summary)
    write_json(latest_summary_path, summary)
    log_fn(f"Saved summary: {run_summary_path}")
    log_fn("GS-O2O online training completed.")


if __name__ == "__main__":
    main()
