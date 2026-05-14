from __future__ import annotations

import argparse
from pathlib import Path
from typing import Callable

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset

from .dataset import BranchDataset, GreedyReturnDataset
from .gso2o_policy import gso2o_mode_metadata
from .models import QModel, RewardModel, ValueModel
from .utils import configure_runtime_env, ensure_project_dirs, load_config, models_dir, num_actions, set_seed, utc_timestamp, write_json


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


def _should_log_epoch(epoch_idx: int, total_epochs: int) -> bool:
    if epoch_idx in {1, total_epochs}:
        return True
    interval = max(total_epochs // 10, 1)
    return epoch_idx % interval == 0


def _make_logger(config: dict) -> tuple[str, Path, Path, Callable[[str], None]]:
    run_id = utc_timestamp()
    log_dir = Path(config["result_dir"]) / "logs"
    log_path = log_dir / f"gso2o_offline_{run_id}.log"
    latest_log_path = log_dir / "gso2o_offline_latest.log"
    latest_log_path.write_text("", encoding="utf-8")

    def log_fn(message: str) -> None:
        print(message, flush=True)
        for target in (log_path, latest_log_path):
            with target.open("a", encoding="utf-8") as handle:
                handle.write(message)
                handle.write("\n")

    return run_id, log_path, latest_log_path, log_fn


def _loss_summary(losses: list[float]) -> dict[str, float]:
    return {
        "initial": float(losses[0]),
        "final": float(losses[-1]),
        "best": float(min(losses)),
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


def _hybrid_cfg(config: dict) -> dict:
    return dict(config.get("gso2o_hybrid", {}))


def _cql_penalty(model: QModel, x: torch.Tensor, action_id: torch.Tensor) -> torch.Tensor:
    q_all = model.all_actions(x)
    q_data = q_all.gather(1, action_id.view(-1, 1)).squeeze(1)
    return torch.logsumexp(q_all, dim=-1).mean() - q_data.mean()


def _train_reward_model(
    config: dict,
    branch_dataset: BranchDataset,
    device: torch.device,
    log_fn: Callable[[str], None],
) -> tuple[RewardModel, list[float]]:
    hp = _model_hparams(config)
    model = RewardModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"]).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=float(config["offline"]["lr"]))
    loader = DataLoader(branch_dataset, batch_size=int(config["offline"]["batch_size"]), shuffle=True)
    total_epochs = int(config["offline"]["reward_epochs"])
    losses: list[float] = []
    best_loss = float("inf")
    best_state = _clone_state_dict(model)
    for epoch_idx in range(1, total_epochs + 1):
        running_loss = 0.0
        num_batches = 0
        for batch in loader:
            x = batch["x"].to(device)
            action_id = batch["action_id"].to(device)
            reward = batch["reward"].to(device)
            pred = model(x, action_id)
            loss = _regression_loss(config, pred, reward)
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
        if _should_log_epoch(epoch_idx, total_epochs):
            log_fn(f"[reward] epoch {epoch_idx}/{total_epochs} loss={epoch_loss:.6f}")
    model.load_state_dict(best_state)
    return model.cpu(), losses


def _train_value_model(
    config: dict,
    greedy_dataset: GreedyReturnDataset,
    device: torch.device,
    log_fn: Callable[[str], None],
) -> tuple[ValueModel, list[float]]:
    hp = _model_hparams(config)
    model = ValueModel(hp["feature_dim"], hp["hidden_dim"], hp["num_layers"], hp["activation"]).to(device)
    optimizer = torch.optim.Adam(model.parameters(), lr=float(config["offline"]["lr"]))
    loader = DataLoader(greedy_dataset, batch_size=int(config["offline"]["batch_size"]), shuffle=True)
    total_epochs = int(config["offline"]["value_epochs"])
    losses: list[float] = []
    best_loss = float("inf")
    best_state = _clone_state_dict(model)
    for epoch_idx in range(1, total_epochs + 1):
        running_loss = 0.0
        num_batches = 0
        for batch in loader:
            x = batch["x"].to(device)
            target = batch["return"].to(device)
            pred = model(x)
            loss = _regression_loss(config, pred, target)
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
        if _should_log_epoch(epoch_idx, total_epochs):
            log_fn(f"[value] epoch {epoch_idx}/{total_epochs} loss={epoch_loss:.6f}")
    model.load_state_dict(best_state)
    return model.cpu(), losses


def _bootstrap_dataset(branch_dataset: BranchDataset) -> dict[str, torch.Tensor]:
    count = len(branch_dataset)
    indices = torch.randint(0, count, size=(count,))
    return {
        "x": branch_dataset.x[indices],
        "action_id": branch_dataset.action_id[indices],
        "reward": branch_dataset.reward[indices],
        "x_next": branch_dataset.x_next[indices],
    }


def _train_q_ensemble(
    config: dict,
    branch_dataset: BranchDataset,
    value_model: ValueModel,
    device: torch.device,
    log_fn: Callable[[str], None],
) -> tuple[list[QModel], list[list[float]]]:
    hp = _model_hparams(config)
    batch_size = int(config["offline"]["batch_size"])
    gamma = float(config["gamma"])
    total_epochs = int(config["offline"]["q_epochs"])
    q_alpha = float(_hybrid_cfg(config).get("offline_cql_alpha", 0.0))
    ensemble: list[QModel] = []
    ensemble_losses: list[list[float]] = []
    value_model = value_model.to("cpu")
    value_model.eval()
    for ensemble_idx in range(int(config["model"]["ensemble_size"])):
        sample = _bootstrap_dataset(branch_dataset)
        with torch.no_grad():
            targets = sample["reward"] + gamma * value_model(sample["x_next"]).cpu()
        dataset = TensorDataset(sample["x"], sample["action_id"], targets)
        loader = DataLoader(dataset, batch_size=batch_size, shuffle=True)
        model = QModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"]).to(device)
        optimizer = torch.optim.Adam(model.parameters(), lr=float(config["offline"]["lr"]))
        losses: list[float] = []
        best_loss = float("inf")
        best_state = _clone_state_dict(model)
        for epoch_idx in range(1, total_epochs + 1):
            running_loss = 0.0
            num_batches = 0
            for x, action_id, target in loader:
                x = x.to(device)
                action_id = action_id.to(device)
                target = target.to(device)
                pred = model(x, action_id)
                loss = _regression_loss(config, pred, target)
                if q_alpha > 0.0:
                    loss = loss + q_alpha * _cql_penalty(model, x, action_id)
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
            if _should_log_epoch(epoch_idx, total_epochs):
                log_fn(f"[q_ensemble_{ensemble_idx}] epoch {epoch_idx}/{total_epochs} loss={epoch_loss:.6f}")
        model.load_state_dict(best_state)
        ensemble.append(model.cpu())
        ensemble_losses.append(losses)
    return ensemble, ensemble_losses


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args()

    config = load_config(args.config)
    ensure_project_dirs(config)
    configure_runtime_env(config)
    set_seed(int(config.get("seed", 0)))
    device = _device()
    ckpt_dir = models_dir(config)
    run_id, log_path, latest_log_path, log_fn = _make_logger(config)
    use_normalized_targets = _use_normalized_targets(config)

    branch_dataset = BranchDataset(
        torch.load(Path(config["data_dir"]) / "processed" / "d2_train.pt", map_location="cpu"),
        greedy_only=bool(config.get("ablation", {}).get("d1_greedy_only", False)),
        normalized_reward=use_normalized_targets,
    )
    greedy_dataset = GreedyReturnDataset(
        torch.load(Path(config["data_dir"]) / "processed" / "greedy_trajs.pt", map_location="cpu"),
        normalized_return=use_normalized_targets,
    )

    log_fn(f"Starting GS-O2O offline training run_id={run_id}")
    log_fn(f"Config path: {Path(args.config).resolve()}")
    log_fn(f"Device: {device}")
    log_fn(f"Checkpoint dir: {ckpt_dir}")
    log_fn(f"Branch records: {len(branch_dataset)}")
    log_fn(f"Greedy return records: {len(greedy_dataset)}")
    log_fn(f"Use normalized targets: {use_normalized_targets}")
    log_fn(f"Regression loss: {config.get('offline', {}).get('loss', 'huber')}")

    reward_model, reward_losses = _train_reward_model(config, branch_dataset, device, log_fn)
    reward_ckpt = ckpt_dir / "reward.pt"
    torch.save(reward_model.state_dict(), reward_ckpt)
    log_fn(f"Saved checkpoint: {reward_ckpt}")

    summary_path = Path(config["result_dir"]) / "logs" / f"gso2o_offline_{run_id}.json"
    latest_summary_path = Path(config["result_dir"]) / "logs" / "gso2o_offline_latest.json"

    if bool(config.get("ablation", {}).get("immediate_reward_only", False)):
        mode_path = ckpt_dir / "gso2o_mode.json"
        write_json(mode_path, gso2o_mode_metadata(config))
        summary = {
            "run_id": run_id,
            "config_path": str(Path(args.config).resolve()),
            "device": str(device),
            "branch_records": len(branch_dataset),
            "greedy_return_records": len(greedy_dataset),
            "immediate_reward_only": True,
            "use_normalized_targets": use_normalized_targets,
            "regression_loss": str(config.get("offline", {}).get("loss", "huber")),
            "checkpoints": {
                "reward": str(reward_ckpt),
                "mode": str(mode_path),
            },
            "losses": {
                "reward": reward_losses,
            },
            "loss_summary": {
                "reward": _loss_summary(reward_losses),
            },
            "log_path": str(log_path),
            "latest_log_path": str(latest_log_path),
        }
        write_json(summary_path, summary)
        write_json(latest_summary_path, summary)
        log_fn("Immediate-reward-only ablation enabled; skipping value/Q training.")
        log_fn(f"Saved summary: {summary_path}")
        return

    value_model, value_losses = _train_value_model(config, greedy_dataset, device, log_fn)
    value_ckpt = ckpt_dir / "value.pt"
    torch.save(value_model.state_dict(), value_ckpt)
    log_fn(f"Saved checkpoint: {value_ckpt}")

    q_ensemble, q_ensemble_losses = _train_q_ensemble(config, branch_dataset, value_model, device, log_fn)
    q_ckpts: list[str] = []
    for idx, model in enumerate(q_ensemble):
        q_ckpt = ckpt_dir / f"q_ensemble_{idx}.pt"
        torch.save(model.state_dict(), q_ckpt)
        q_ckpts.append(str(q_ckpt))
        log_fn(f"Saved checkpoint: {q_ckpt}")

    mode_path = ckpt_dir / "gso2o_mode.json"
    write_json(mode_path, gso2o_mode_metadata(config))
    summary = {
        "run_id": run_id,
        "config_path": str(Path(args.config).resolve()),
        "device": str(device),
        "branch_records": len(branch_dataset),
        "greedy_return_records": len(greedy_dataset),
        "immediate_reward_only": False,
        "use_normalized_targets": use_normalized_targets,
        "regression_loss": str(config.get("offline", {}).get("loss", "huber")),
        "checkpoints": {
            "reward": str(reward_ckpt),
            "value": str(value_ckpt),
            "q_ensemble": q_ckpts,
            "mode": str(mode_path),
        },
        "losses": {
            "reward": reward_losses,
            "value": value_losses,
            "q_ensemble": q_ensemble_losses,
        },
        "loss_summary": {
            "reward": _loss_summary(reward_losses),
            "value": _loss_summary(value_losses),
            "q_ensemble": [_loss_summary(losses) for losses in q_ensemble_losses],
        },
        "log_path": str(log_path),
        "latest_log_path": str(latest_log_path),
    }
    write_json(summary_path, summary)
    write_json(latest_summary_path, summary)
    log_fn(f"Saved summary: {summary_path}")
    log_fn("GS-O2O offline training completed.")


if __name__ == "__main__":
    main()
