from __future__ import annotations

import hashlib
import json
import math
import os
import random
import shutil
import string
import time
from pathlib import Path
from typing import Any, Iterable

import numpy as np
try:
    import yaml
except ModuleNotFoundError:
    yaml = None


DEFAULT_ACTIONS = [
    {"name": "rewrite", "abc_cmd": "rewrite"},
    {"name": "rewrite_z", "abc_cmd": "rewrite -z"},
    {"name": "balance", "abc_cmd": "balance"},
    {"name": "refactor", "abc_cmd": "refactor"},
    {"name": "refactor_z", "abc_cmd": "refactor -z"},
    {"name": "resub", "abc_cmd": "resub"},
    {"name": "resub_z", "abc_cmd": "resub -z"},
]


def ensure_dir(path: str | Path) -> Path:
    path_obj = Path(path)
    path_obj.mkdir(parents=True, exist_ok=True)
    return path_obj


def read_text(path: str | Path) -> str:
    return Path(path).read_text(encoding="utf-8")


def write_text(path: str | Path, content: str) -> None:
    path_obj = Path(path)
    ensure_dir(path_obj.parent)
    path_obj.write_text(content, encoding="utf-8")


def read_json(path: str | Path, default: Any | None = None) -> Any:
    path_obj = Path(path)
    if not path_obj.exists():
        return {} if default is None else default
    try:
        with path_obj.open("r", encoding="utf-8") as handle:
            return json.load(handle)
    except json.JSONDecodeError:
        return {} if default is None else default


def write_json(path: str | Path, data: Any) -> None:
    path_obj = Path(path)
    ensure_dir(path_obj.parent)
    with path_obj.open("w", encoding="utf-8") as handle:
        json.dump(data, handle, indent=2, sort_keys=True)


def append_jsonl(path: str | Path, record: dict[str, Any]) -> None:
    path_obj = Path(path)
    ensure_dir(path_obj.parent)
    with path_obj.open("a", encoding="utf-8") as handle:
        handle.write(json.dumps(record))
        handle.write("\n")


def read_jsonl(path: str | Path) -> list[dict[str, Any]]:
    path_obj = Path(path)
    if not path_obj.exists():
        return []
    records = []
    with path_obj.open("r", encoding="utf-8") as handle:
        for line in handle:
            line = line.strip()
            if line:
                records.append(json.loads(line))
    return records


def sha1_bytes(payload: bytes) -> str:
    return hashlib.sha1(payload).hexdigest()


def sha1_text(text: str) -> str:
    return sha1_bytes(text.encode("utf-8"))


def sha1_file(path: str | Path) -> str:
    return sha1_bytes(Path(path).read_bytes())


def sanitize_token(text: str) -> str:
    keep = string.ascii_letters + string.digits + "._-"
    return "".join(ch if ch in keep else "_" for ch in text)


def utc_timestamp() -> str:
    return time.strftime("%Y%m%d_%H%M%S", time.gmtime())


def _resolve_project_path(project_root: Path, value: str | Path) -> str:
    path = Path(value)
    if not path.is_absolute():
        path = (project_root / path).resolve()
    return str(path)


def load_config(config_path: str | Path) -> dict[str, Any]:
    config_path = Path(config_path).resolve()
    with config_path.open("r", encoding="utf-8") as handle:
        if config_path.suffix.lower() == ".json":
            config = json.load(handle)
        else:
            if yaml is None:
                raise ModuleNotFoundError("PyYAML is not installed; use a JSON config or install yaml.")
            config = yaml.safe_load(handle)
    project_root = config_path.parent.parent.resolve()
    config["_project_root"] = str(project_root)
    config["_config_path"] = str(config_path)
    for key in ["benchmark_root", "work_dir", "data_dir", "result_dir", "hpwl_flow_template", "models_dir"]:
        if key in config:
            config[key] = _resolve_project_path(project_root, config[key])
    if "abc_binary" in config:
        config["abc_binary"] = _resolve_project_path(project_root, config["abc_binary"])
    if "mapping_lib" in config:
        config["mapping_lib"] = _resolve_project_path(project_root, config["mapping_lib"])
    if "actions" not in config:
        config["actions"] = list(DEFAULT_ACTIONS)
    return config


def ensure_project_dirs(config: dict[str, Any]) -> None:
    ensure_dir(config["work_dir"])
    ensure_dir(config["data_dir"])
    ensure_dir(Path(config["data_dir"]) / "offline_d2")
    ensure_dir(Path(config["data_dir"]) / "online")
    ensure_dir(Path(config["data_dir"]) / "processed")
    ensure_dir(config["result_dir"])
    ensure_dir(Path(config["result_dir"]) / "logs")
    ensure_dir(Path(config["result_dir"]) / "tables")
    ensure_dir(Path(config["result_dir"]) / "sequences")
    ensure_dir(models_dir(config))


def configure_runtime_env(config: dict[str, Any]) -> None:
    os.environ["RL_LOGIC_SEQ_PROJECT_ROOT"] = config["_project_root"]
    os.environ["RL_LOGIC_SEQ_WORK_DIR"] = config["work_dir"]
    os.environ["RL_LOGIC_SEQ_RESULTS_LOG_DIR"] = str(Path(config["result_dir"]) / "logs")


def set_seed(seed: int) -> None:
    random.seed(seed)
    np.random.seed(seed)
    try:
        import torch

        torch.manual_seed(seed)
    except ModuleNotFoundError:
        pass


def get_actions(config: dict[str, Any]) -> list[dict[str, str]]:
    actions = config.get("actions", DEFAULT_ACTIONS)
    if not actions:
        raise ValueError("Action set is empty.")
    return actions


def num_actions(config: dict[str, Any]) -> int:
    return len(get_actions(config))


def experiment_seeds(config: dict[str, Any]) -> list[int]:
    seeds = config.get("experiment", {}).get("seeds")
    if seeds is None:
        return [int(config.get("seed", 0))]
    return [int(seed) for seed in seeds]


def experiment_budgets(config: dict[str, Any]) -> list[int]:
    budgets = config.get("experiment", {}).get("budgets")
    if budgets is None:
        episodes = int(config.get("online", {}).get("episodes", 0))
        budgets = [0, episodes] if episodes > 0 else [0]
    unique = {int(budget) for budget in budgets}
    if bool(config.get("experiment", {}).get("include_train_round_budgets", False)):
        round_size = max(len(config.get("train_circuits", [])), 1)
        total_episodes = int(config.get("online", {}).get("episodes", 0))
        unique.update(range(round_size, total_episodes + 1, round_size))
    unique = sorted(unique)
    return unique


def online_checkpoint_budgets(config: dict[str, Any]) -> list[int]:
    total_episodes = int(config.get("online", {}).get("episodes", 0))
    return [budget for budget in experiment_budgets(config) if 0 < budget <= total_episodes]


def online_train_circuit_schedule(config: dict[str, Any], total_episodes: int) -> list[str]:
    circuits = list(config.get("train_circuits", []))
    if not circuits:
        return []
    schedule: list[str] = []
    while len(schedule) < total_episodes:
        round_circuits = circuits.copy()
        random.shuffle(round_circuits)
        schedule.extend(round_circuits)
    return schedule[:total_episodes]


def benchmark_path(config: dict[str, Any], circuit: str) -> Path:
    return Path(config["benchmark_root"]) / f"{circuit.lower()}.blif"


def make_work_subdir(config: dict[str, Any], stage: str, circuit: str) -> Path:
    return ensure_dir(Path(config["work_dir"]) / sanitize_token(stage) / sanitize_token(circuit))


def make_state_id(circuit: str, t: int, suffix: str, parent_state_id: str | None = None) -> str:
    state_id = f"{circuit}_t{t}_{sanitize_token(suffix)}"
    if parent_state_id:
        state_id = f"{state_id}_{sha1_text(parent_state_id)[:8]}"
    return state_id


def copy_design_to_work(src: str | Path, dst: str | Path) -> None:
    dst_path = Path(dst)
    ensure_dir(dst_path.parent)
    shutil.copyfile(src, dst_path)


def linear_schedule(step: int, start: float, end: float, total_steps: int) -> float:
    if total_steps <= 1:
        return end
    alpha = min(max(step, 0), total_steps - 1) / float(total_steps - 1)
    return start + alpha * (end - start)


def monte_carlo_returns(rewards: Iterable[float], gamma: float) -> list[float]:
    rewards = list(rewards)
    returns = [0.0 for _ in rewards]
    running = 0.0
    for idx in range(len(rewards) - 1, -1, -1):
        running = rewards[idx] + gamma * running
        returns[idx] = running
    return returns


def geometric_mean(values: Iterable[float], eps: float = 1e-12) -> float:
    values = [max(float(v), eps) for v in values]
    if not values:
        return float("nan")
    return math.exp(sum(math.log(v) for v in values) / len(values))


def hpwl_cache_path(config: dict[str, Any]) -> Path:
    return Path(config["data_dir"]) / "hpwl_cache.json"


def hpwl_count_path(config: dict[str, Any]) -> Path:
    return Path(config["result_dir"]) / "logs" / "hpwl_call_counts.json"


def increment_hpwl_counter(config: dict[str, Any], circuit: str, state_id: str, cache_hit: bool) -> None:
    phase = config.get("_hpwl_phase", "unspecified")
    counts = read_json(hpwl_count_path(config), default={"totals": {}, "per_circuit": {}, "calls": []})
    counts["totals"].setdefault(phase, 0)
    counts["totals"][phase] += 1
    counts["per_circuit"].setdefault(circuit, {})
    counts["per_circuit"][circuit].setdefault(phase, 0)
    counts["per_circuit"][circuit][phase] += 1
    counts["calls"].append(
        {
            "phase": phase,
            "circuit": circuit,
            "state_id": state_id,
            "cache_hit": bool(cache_hit),
            "timestamp": utc_timestamp(),
        }
    )
    write_json(hpwl_count_path(config), counts)


def get_hpwl_counter_value(config: dict[str, Any], phase: str, circuit: str | None = None) -> int:
    counts = read_json(hpwl_count_path(config), default={"totals": {}, "per_circuit": {}})
    if circuit is None:
        return int(counts.get("totals", {}).get(phase, 0))
    return int(counts.get("per_circuit", {}).get(circuit, {}).get(phase, 0))


def models_dir(config: dict[str, Any]) -> Path:
    if "models_dir" in config:
        return Path(config["models_dir"])
    return Path(config["_project_root"]) / "models" / "checkpoints"


def checkpoint_dir_for_budget(config: dict[str, Any], budget: int, checkpoint_root: str | Path | None = None) -> Path:
    base_dir = Path(checkpoint_root) if checkpoint_root is not None else models_dir(config)
    return ensure_dir(base_dir / f"budget_{int(budget):04d}")


def save_sequence_result(config: dict[str, Any], method: str, split: str, circuit: str, result: dict[str, Any]) -> Path:
    out_path = Path(config["result_dir"]) / "sequences" / method / split / f"{circuit}.json"
    write_json(out_path, result)
    return out_path


def load_sequence_result(config: dict[str, Any], method: str, split: str, circuit: str) -> dict[str, Any] | None:
    path = Path(config["result_dir"]) / "sequences" / method / split / f"{circuit}.json"
    if not path.exists():
        return None
    return read_json(path)
