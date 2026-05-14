from __future__ import annotations

import argparse
import csv
from pathlib import Path
from typing import Callable

import numpy as np
import torch

from .abc_runner import apply_action_to_temp_state
from .features import FeatureNormalizer, extract_aig_graph_stats, extract_features
from .gso2o_policy import select_action_gso2o
from .hpwl_eval import evaluate_hpwl
from .models import PolicyModel, QModel, RewardModel
from .utils import (
    benchmark_path,
    configure_runtime_env,
    ensure_project_dirs,
    geometric_mean,
    get_actions,
    get_hpwl_counter_value,
    load_config,
    load_sequence_result,
    make_state_id,
    make_work_subdir,
    models_dir,
    num_actions,
    read_json,
    save_sequence_result,
    set_seed,
)




def _checkpoint_dir(config: dict) -> Path:
    if config.get("_checkpoint_dir"):
        return Path(config["_checkpoint_dir"])
    return models_dir(config)


def _prefer_online_checkpoints(config: dict) -> bool:
    budget = config.get("_checkpoint_budget")
    if budget is None:
        return True
    return int(budget) > 0


def _model_hparams(config: dict) -> dict:
    model_cfg = config["model"]
    return {
        "feature_dim": int(model_cfg["feature_dim"]),
        "hidden_dim": int(model_cfg["hidden_dim"]),
        "num_layers": int(model_cfg.get("num_layers", 2)),
        "activation": str(model_cfg.get("activation", "leaky_relu")),
    }


def _load_gso2o_selector(config: dict) -> Callable[[np.ndarray, dict | None], int]:
    hp = _model_hparams(config)
    ckpt_dir = _checkpoint_dir(config)
    online = _prefer_online_checkpoints(config) and (ckpt_dir / "reward_online.pt").exists()
    reward_name = "reward_online.pt" if online else "reward.pt"
    action_count = num_actions(config)
    reward_model = RewardModel(hp["feature_dim"], action_count, hp["hidden_dim"], hp["num_layers"], hp["activation"])
    reward_model.load_state_dict(torch.load(ckpt_dir / reward_name, map_location="cpu"))
    reward_model.eval()
    mode = read_json(ckpt_dir / "gso2o_mode.json", default={"immediate_reward_only": bool(config.get("ablation", {}).get("immediate_reward_only", False))})
    if bool(mode.get("immediate_reward_only", False)):
        def _select(x_norm: np.ndarray, context: dict | None = None) -> int:
            x = torch.as_tensor(x_norm, dtype=torch.float32).unsqueeze(0).repeat(action_count, 1)
            action_ids = torch.arange(action_count, dtype=torch.long)
            with torch.no_grad():
                pred = reward_model(x, action_ids)
            return int(torch.argmax(pred).item())
        return _select

    prefix = "q_ensemble_online_" if (_prefer_online_checkpoints(config) and (ckpt_dir / "q_ensemble_online_0.pt").exists()) else "q_ensemble_"
    q_ensemble = []
    for idx in range(int(config["model"]["ensemble_size"])):
        model = QModel(hp["feature_dim"], action_count, hp["hidden_dim"], hp["num_layers"], hp["activation"])
        model.load_state_dict(torch.load(ckpt_dir / f"{prefix}{idx}.pt", map_location="cpu"))
        model.eval()
        q_ensemble.append(model)
    mode_config = dict(config)
    mode_config["gso2o_hybrid"] = {
        "enabled": bool(mode.get("hybrid_selector", False)),
        "selector_topk_q": int(mode.get("topk_q", 0)),
        "reward_regret_penalty": float(mode.get("reward_regret_penalty", 0.0)),
    }
    if "gso2o_refine" in mode:
        mode_config["gso2o_refine"] = dict(mode.get("gso2o_refine", {}))

    def _select(x_norm: np.ndarray, context: dict | None = None) -> int:
        return select_action_gso2o(
            mode_config,
            x_norm,
            reward_model,
            q_ensemble,
            epsilon=0.0,
            beta=float(config.get("online", {}).get("beta_end", 0.0)),
            rho=float(config.get("online", {}).get("rho", 0.0)),
            ablation=config.get("ablation", {}),
            context=context,
        )

    return _select


def _load_policy_selector(config: dict, prefix: str) -> Callable[[np.ndarray, dict | None], int]:
    hp = _model_hparams(config)
    ckpt_dir = _checkpoint_dir(config)
    use_online = _prefer_online_checkpoints(config) and (ckpt_dir / f"{prefix}_policy_online.pt").exists()
    name = f"{prefix}_policy_online.pt" if use_online else f"{prefix}_policy.pt"
    model = PolicyModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])
    model.load_state_dict(torch.load(ckpt_dir / name, map_location="cpu"))
    model.eval()

    def _select(x_norm: np.ndarray, context: dict | None = None) -> int:
        x = torch.as_tensor(x_norm, dtype=torch.float32).unsqueeze(0)
        with torch.no_grad():
            logits = model(x).squeeze(0)
        return int(torch.argmax(logits).item())

    return _select


def _load_q_selector(config: dict, prefix: str) -> Callable[[np.ndarray, dict | None], int]:
    hp = _model_hparams(config)
    ckpt_dir = _checkpoint_dir(config)
    use_online = _prefer_online_checkpoints(config) and (ckpt_dir / f"{prefix}_q_online.pt").exists()
    name = f"{prefix}_q_online.pt" if use_online else f"{prefix}_q.pt"
    model = QModel(hp["feature_dim"], num_actions(config), hp["hidden_dim"], hp["num_layers"], hp["activation"])
    model.load_state_dict(torch.load(ckpt_dir / name, map_location="cpu"))
    model.eval()

    def _select(x_norm: np.ndarray, context: dict | None = None) -> int:
        x = torch.as_tensor(x_norm, dtype=torch.float32).unsqueeze(0)
        with torch.no_grad():
            q_values = model.all_actions(x).squeeze(0)
        return int(torch.argmax(q_values).item())

    return _select


def _selector_for_method(config: dict, method: str) -> Callable[[np.ndarray, dict | None], int]:
    if method == "gso2o":
        return _load_gso2o_selector(config)
    if method == "awac":
        return _load_policy_selector(config, "awac")
    if method == "iql":
        return _load_policy_selector(config, "iql")
    if method == "cql":
        return _load_q_selector(config, "cql")
    raise ValueError(f"Unsupported method: {method}")


def _rollout_policy(config: dict, method: str, split: str, circuit: str, select_action: Callable[[np.ndarray, dict | None], int]) -> dict:
    normalizer = FeatureNormalizer.load(Path(config["data_dir"]) / "processed" / "feature_normalizer.json")
    actions = get_actions(config)
    stage_dir = make_work_subdir(config, f"eval_{method}_{split}", circuit)
    state_path = str(benchmark_path(config, circuit))
    current_state_id = make_state_id(circuit, 0, f"{method}_{split}_init")
    initial_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
    hpwl_initial = evaluate_hpwl(state_path, circuit, current_state_id, config)
    sequence = []
    action_history: list[int] = []

    for t in range(int(config["horizon"])):
        x = extract_features(state_path, t, int(config["horizon"]), initial_stats, abc_binary=config["abc_binary"])
        x_norm = normalizer.transform(x)
        checkpoint_budget = int(config.get("_checkpoint_budget", 0))
        action_id = select_action(
            x_norm,
            {
                "episode": max(checkpoint_budget - 1, 0),
                "total_episodes": max(int(config.get("online", {}).get("episodes", checkpoint_budget or 1)), 1),
                "t": t,
                "horizon": int(config["horizon"]),
                "action_history": action_history,
            },
        )
        action = actions[action_id]
        sequence.append(action["name"])
        action_history.append(action_id)
        next_state_id = make_state_id(circuit, t + 1, f"{method}_{action['name']}", current_state_id)
        state_path = apply_action_to_temp_state(state_path, stage_dir, next_state_id, action["abc_cmd"], abc_binary=config["abc_binary"])
        current_state_id = next_state_id

    hpwl_final = evaluate_hpwl(state_path, circuit, current_state_id, config)
    final_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
    return {
        "circuit": circuit,
        "method": method,
        "sequence": sequence,
        "initial_hpwl": float(hpwl_initial),
        "final_hpwl": float(hpwl_final),
        "final_aig_nodes": int(final_stats["num_and_nodes"]),
        "final_aig_level": int(final_stats["aig_level"]),
    }


def evaluate_greedy_reference(config: dict, split: str) -> None:
    actions = get_actions(config)
    circuits = config["train_circuits"] if split == "train" else config["test_circuits"]
    old_phase = config.get("_hpwl_phase")
    config["_hpwl_phase"] = "greedy"
    for circuit in circuits:
        stage_dir = make_work_subdir(config, f"greedy_{split}", circuit)
        state_path = str(benchmark_path(config, circuit))
        current_state_id = make_state_id(circuit, 0, f"greedy_{split}_init")
        initial_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
        hpwl_initial = evaluate_hpwl(state_path, circuit, current_state_id, config)
        hpwl_current = hpwl_initial
        sequence = []
        for t in range(int(config["horizon"])):
            branches = []
            for action_id, action in enumerate(actions):
                next_state_id = make_state_id(circuit, t + 1, f"greedy_{action['name']}", current_state_id)
                next_state_path = apply_action_to_temp_state(state_path, stage_dir, next_state_id, action["abc_cmd"], abc_binary=config["abc_binary"])
                hpwl_next = evaluate_hpwl(next_state_path, circuit, next_state_id, config)
                reward = (hpwl_current - hpwl_next) / max(hpwl_initial, 1e-12)
                branches.append((reward, action_id, action, next_state_id, next_state_path, hpwl_next))
            _, best_action_id, best_action, best_state_id, best_state_path, best_hpwl = max(branches, key=lambda row: row[0])
            sequence.append(best_action["name"])
            state_path = best_state_path
            current_state_id = best_state_id
            hpwl_current = best_hpwl
        final_stats = extract_aig_graph_stats(state_path, abc_binary=config["abc_binary"])
        result = {
            "circuit": circuit,
            "method": "greedy",
            "sequence": sequence,
            "initial_hpwl": float(hpwl_initial),
            "final_hpwl": float(hpwl_current),
            "final_aig_nodes": int(final_stats["num_and_nodes"]),
            "final_aig_level": int(final_stats["aig_level"]),
        }
        save_sequence_result(config, "greedy", split, circuit, result)
    if old_phase is not None:
        config["_hpwl_phase"] = old_phase


def _update_summary_table(config: dict, method_label: str, split: str) -> None:
    circuits = config["train_circuits"] if split == "train" else config["test_circuits"]
    summary_path = Path(config["result_dir"]) / "tables" / "test_summary.csv"
    existing = []
    if summary_path.exists():
        with summary_path.open("r", encoding="utf-8") as handle:
            existing = list(csv.DictReader(handle))
    existing = [row for row in existing if not (row["method"] == method_label and row["circuit"] in set(circuits + ["geomean"]))]

    rows = []
    for circuit in circuits:
        result = load_sequence_result(config, method_label, split, circuit)
        greedy = load_sequence_result(config, "greedy", split, circuit)
        if result is None or greedy is None:
            continue
        initial_hpwl = float(result["initial_hpwl"])
        greedy_hpwl = float(greedy["final_hpwl"])
        final_hpwl = float(result["final_hpwl"])
        row = {
            "circuit": circuit,
            "method": method_label,
            "initial_hpwl": initial_hpwl,
            "greedy_hpwl": greedy_hpwl,
            "final_hpwl": final_hpwl,
            "hpwl_ratio_vs_initial": final_hpwl / max(initial_hpwl, 1e-12),
            "hpwl_ratio_vs_greedy": final_hpwl / max(greedy_hpwl, 1e-12),
            "hpwl_improvement_vs_greedy": (greedy_hpwl - final_hpwl) / max(greedy_hpwl, 1e-12),
            "final_aig_nodes": int(result["final_aig_nodes"]),
            "final_aig_level": int(result["final_aig_level"]),
            "sequence": " ".join(result["sequence"]),
            "offline_hpwl_calls": get_hpwl_counter_value(config, "offline"),
            "online_hpwl_calls": get_hpwl_counter_value(config, "online"),
            "test_hpwl_calls": get_hpwl_counter_value(config, "test"),
        }
        rows.append(row)

    if rows:
        geo_ratio_initial = geometric_mean([row["hpwl_ratio_vs_initial"] for row in rows])
        geo_ratio_greedy = geometric_mean([row["hpwl_ratio_vs_greedy"] for row in rows])
        rows.append(
            {
                "circuit": "geomean",
                "method": method_label,
                "initial_hpwl": geometric_mean([row["initial_hpwl"] for row in rows]),
                "greedy_hpwl": geometric_mean([row["greedy_hpwl"] for row in rows]),
                "final_hpwl": geometric_mean([row["final_hpwl"] for row in rows]),
                "hpwl_ratio_vs_initial": geo_ratio_initial,
                "hpwl_ratio_vs_greedy": geo_ratio_greedy,
                "hpwl_improvement_vs_greedy": 1.0 - geo_ratio_greedy,
                "final_aig_nodes": int(round(sum(row["final_aig_nodes"] for row in rows) / len(rows))),
                "final_aig_level": int(round(sum(row["final_aig_level"] for row in rows) / len(rows))),
                "sequence": "",
                "offline_hpwl_calls": get_hpwl_counter_value(config, "offline"),
                "online_hpwl_calls": get_hpwl_counter_value(config, "online"),
                "test_hpwl_calls": get_hpwl_counter_value(config, "test"),
            }
        )

    fieldnames = [
        "circuit",
        "method",
        "initial_hpwl",
        "greedy_hpwl",
        "final_hpwl",
        "hpwl_ratio_vs_initial",
        "hpwl_ratio_vs_greedy",
        "hpwl_improvement_vs_greedy",
        "final_aig_nodes",
        "final_aig_level",
        "sequence",
        "offline_hpwl_calls",
        "online_hpwl_calls",
        "test_hpwl_calls",
    ]
    with summary_path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in existing + rows:
            writer.writerow(row)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--method", required=True)
    parser.add_argument("--split", choices=["train", "test"], default="test")
    parser.add_argument("--checkpoint-dir")
    parser.add_argument("--budget", type=int)
    parser.add_argument("--method-label")
    args = parser.parse_args()

    config = load_config(args.config)
    if args.checkpoint_dir:
        config["_checkpoint_dir"] = str(Path(args.checkpoint_dir).resolve())
    if args.budget is not None:
        config["_checkpoint_budget"] = int(args.budget)
    ensure_project_dirs(config)
    configure_runtime_env(config)
    set_seed(int(config.get("seed", 0)))

    if args.method == "greedy":
        evaluate_greedy_reference(config, args.split)
        _update_summary_table(config, "greedy", args.split)
        return

    if any(load_sequence_result(config, "greedy", args.split, circuit) is None for circuit in (config["train_circuits"] if args.split == "train" else config["test_circuits"])):
        evaluate_greedy_reference(config, args.split)

    config["_hpwl_phase"] = "test" if args.split == "test" else "train_eval"
    circuits = config["train_circuits"] if args.split == "train" else config["test_circuits"]
    selector = _selector_for_method(config, args.method)
    method_label = args.method_label or args.method
    for circuit in circuits:
        result = _rollout_policy(config, args.method, args.split, circuit, selector)
        result["method"] = method_label
        if args.budget is not None:
            result["budget"] = int(args.budget)
        save_sequence_result(config, method_label, args.split, circuit, result)
    _update_summary_table(config, method_label, args.split)


if __name__ == "__main__":
    main()
