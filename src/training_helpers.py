from __future__ import annotations

from pathlib import Path
from typing import Callable

import torch
import torch.nn.functional as F
from torch.utils.data import DataLoader

from .utils import utc_timestamp


def device() -> torch.device:
    return torch.device("cuda" if torch.cuda.is_available() else "cpu")


def model_hparams(config: dict) -> dict:
    model_cfg = config["model"]
    return {
        "feature_dim": int(model_cfg["feature_dim"]),
        "hidden_dim": int(model_cfg["hidden_dim"]),
        "num_layers": int(model_cfg.get("num_layers", 2)),
        "activation": str(model_cfg.get("activation", "leaky_relu")),
    }


def use_normalized_targets(config: dict) -> bool:
    return bool(config.get("offline", {}).get("use_normalized_targets", True))


def regression_loss(config: dict, pred: torch.Tensor, target: torch.Tensor) -> torch.Tensor:
    loss_name = str(config.get("offline", {}).get("loss", "huber")).lower()
    if loss_name == "mse":
        return torch.mean((pred - target) ** 2)
    delta = float(config.get("offline", {}).get("huber_delta", 1.0))
    return F.smooth_l1_loss(pred, target, beta=delta)


def clone_state_dict(model: torch.nn.Module) -> dict[str, torch.Tensor]:
    return {key: value.detach().cpu().clone() for key, value in model.state_dict().items()}


def should_log_epoch(epoch_idx: int, total_epochs: int) -> bool:
    if epoch_idx in {1, total_epochs}:
        return True
    interval = max(total_epochs // 10, 1)
    return epoch_idx % interval == 0


def make_stage_logger(config: dict, prefix: str) -> tuple[str, Path, Path, Callable[[str], None]]:
    run_id = utc_timestamp()
    log_dir = Path(config["result_dir"]) / "logs"
    log_path = log_dir / f"{prefix}_{run_id}.log"
    latest_log_path = log_dir / f"{prefix}_latest.log"
    latest_log_path.write_text("", encoding="utf-8")

    def log_fn(message: str) -> None:
        print(message, flush=True)
        for target in (log_path, latest_log_path):
            with target.open("a", encoding="utf-8") as handle:
                handle.write(message)
                handle.write("\n")

    return run_id, log_path, latest_log_path, log_fn


def loss_summary(losses: list[float]) -> dict[str, float]:
    return {
        "initial": float(losses[0]),
        "final": float(losses[-1]),
        "best": float(min(losses)),
    }


def cycle_loader(loader: DataLoader):
    while True:
        for batch in loader:
            yield batch
