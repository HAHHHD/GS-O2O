from __future__ import annotations

import argparse
from pathlib import Path

import numpy as np
import torch

from .abc_runner import apply_action_to_temp_state
from .features import FeatureNormalizer, extract_aig_graph_stats, extract_features
from .hpwl_eval import evaluate_hpwl
from .utils import (
    append_jsonl,
    benchmark_path,
    configure_runtime_env,
    ensure_project_dirs,
    get_actions,
    load_config,
    make_state_id,
    make_work_subdir,
    monte_carlo_returns,
    read_json,
    read_jsonl,
    set_seed,
    write_json,
)


def _expected_branch_records(config: dict) -> int:
    return int(config["horizon"]) * len(get_actions(config))


def _circuit_outputs_complete(config: dict, circuit: str) -> bool:
    output_jsonl = Path(config["data_dir"]) / "offline_d2" / f"{circuit}.jsonl"
    greedy_path = Path(config["data_dir"]) / "offline_d2" / f"{circuit}_greedy_traj.json"
    if not output_jsonl.exists() or not greedy_path.exists():
        return False
    line_count = sum(1 for _ in output_jsonl.open("r", encoding="utf-8"))
    if line_count != _expected_branch_records(config):
        return False
    greedy_traj = read_json(greedy_path, default=[])
    return len(greedy_traj) == int(config["horizon"])


def _collect_for_circuit(config: dict, circuit: str) -> None:
    actions = get_actions(config)
    horizon = int(config["horizon"])
    stage_dir = make_work_subdir(config, "offline_d2", circuit)
    output_jsonl = Path(config["data_dir"]) / "offline_d2" / f"{circuit}.jsonl"
    greedy_path = Path(config["data_dir"]) / "offline_d2" / f"{circuit}_greedy_traj.json"
    if _circuit_outputs_complete(config, circuit):
        print(f"[collect_d2] skip complete circuit={circuit}", flush=True)
        return
    if output_jsonl.exists():
        output_jsonl.unlink()
    if greedy_path.exists():
        greedy_path.unlink()
    print(f"[collect_d2] start circuit={circuit}", flush=True)
    design_path = benchmark_path(config, circuit)
    current_state_id = make_state_id(circuit, 0, "init")
    initial_stats = extract_aig_graph_stats(str(design_path), abc_binary=config["abc_binary"])
    hpwl_initial = evaluate_hpwl(str(design_path), circuit, current_state_id, config)
    hpwl_current = hpwl_initial
    greedy_traj: list[dict] = []
    current_design = str(design_path)

    for t in range(horizon):
        x = extract_features(current_design, t, horizon, initial_stats, abc_binary=config["abc_binary"])
        branches: list[dict] = []
        for action_id, action in enumerate(actions):
            next_state_id = make_state_id(circuit, t + 1, action["name"], current_state_id)
            next_design = apply_action_to_temp_state(
                current_design,
                stage_dir,
                next_state_id,
                action["abc_cmd"],
                abc_binary=config["abc_binary"],
            )
            hpwl_next = evaluate_hpwl(next_design, circuit, next_state_id, config)
            hpwl_reduction = hpwl_current - hpwl_next
            reward_normalized = hpwl_reduction / max(hpwl_initial, 1e-12)
            x_next = extract_features(next_design, t + 1, horizon, initial_stats, abc_binary=config["abc_binary"])
            record = {
                "circuit": circuit,
                "t": t,
                "H": horizon,
                "state_id": current_state_id,
                "next_state_id": next_state_id,
                "action_id": action_id,
                "action_name": action["name"],
                "reward": float(hpwl_reduction),
                "reward_normalized": float(reward_normalized),
                "hpwl_reduction": float(hpwl_reduction),
                "hpwl_s": float(hpwl_current),
                "hpwl_next": float(hpwl_next),
                "hpwl_initial": float(hpwl_initial),
                "state_path": current_design,
                "next_state_path": next_design,
                "x": x.tolist(),
                "x_next": x_next.tolist(),
            }
            branches.append(record)

        greedy_record = max(branches, key=lambda row: row["hpwl_reduction"])
        greedy_action_id = int(greedy_record["action_id"])
        for record in branches:
            record["greedy_action_id"] = greedy_action_id
            record["is_greedy"] = record["action_id"] == greedy_action_id
            append_jsonl(output_jsonl, record)
        greedy_traj.append(greedy_record)
        current_state_id = greedy_record["next_state_id"]
        current_design = greedy_record["next_state_path"]
        hpwl_current = greedy_record["hpwl_next"]

    write_json(greedy_path, greedy_traj)
    print(f"[collect_d2] done circuit={circuit}", flush=True)


def _build_processed_tensors(config: dict) -> None:
    offline_dir = Path(config["data_dir"]) / "offline_d2"
    all_records = []
    for circuit in config["train_circuits"]:
        all_records.extend(read_jsonl(offline_dir / f"{circuit}.jsonl"))
    if not all_records:
        raise RuntimeError("No offline D2 records were collected.")

    feature_rows = []
    for record in all_records:
        feature_rows.append(np.asarray(record["x"], dtype=np.float32))
        feature_rows.append(np.asarray(record["x_next"], dtype=np.float32))
    normalizer = FeatureNormalizer()
    normalizer.fit(np.stack(feature_rows, axis=0))
    normalizer.save(Path(config["data_dir"]) / "processed" / "feature_normalizer.json")

    x = torch.as_tensor(np.stack([normalizer.transform(np.asarray(r["x"], dtype=np.float32)) for r in all_records]), dtype=torch.float32)
    x_next = torch.as_tensor(np.stack([normalizer.transform(np.asarray(r["x_next"], dtype=np.float32)) for r in all_records]), dtype=torch.float32)
    payload = {
        "x": x,
        "action_id": torch.as_tensor([r["action_id"] for r in all_records], dtype=torch.long),
        "reward": torch.as_tensor([r["reward"] for r in all_records], dtype=torch.float32),
        "reward_normalized": torch.as_tensor([r["reward_normalized"] for r in all_records], dtype=torch.float32),
        "x_next": x_next,
        "greedy_action_id": torch.as_tensor([r["greedy_action_id"] for r in all_records], dtype=torch.long),
        "is_greedy": torch.as_tensor([r["is_greedy"] for r in all_records], dtype=torch.bool),
        "t": torch.as_tensor([r["t"] for r in all_records], dtype=torch.long),
        "circuit": [r["circuit"] for r in all_records],
    }
    torch.save(payload, Path(config["data_dir"]) / "processed" / "d2_train.pt")

    greedy_x = []
    greedy_returns = []
    greedy_returns_normalized = []
    greedy_t = []
    greedy_circuit = []
    for circuit in config["train_circuits"]:
        trajectory = read_jsonl(offline_dir / f"{circuit}.jsonl")
        greedy_only = [row for row in trajectory if row["is_greedy"]]
        returns = monte_carlo_returns([row["reward"] for row in greedy_only], float(config["gamma"]))
        returns_normalized = monte_carlo_returns([row["reward_normalized"] for row in greedy_only], float(config["gamma"]))
        for row, ret, ret_norm in zip(greedy_only, returns, returns_normalized):
            greedy_x.append(normalizer.transform(np.asarray(row["x"], dtype=np.float32)))
            greedy_returns.append(ret)
            greedy_returns_normalized.append(ret_norm)
            greedy_t.append(row["t"])
            greedy_circuit.append(circuit)
    greedy_payload = {
        "x": torch.as_tensor(np.stack(greedy_x), dtype=torch.float32),
        "return": torch.as_tensor(greedy_returns, dtype=torch.float32),
        "return_normalized": torch.as_tensor(greedy_returns_normalized, dtype=torch.float32),
        "t": torch.as_tensor(greedy_t, dtype=torch.long),
        "circuit": greedy_circuit,
    }
    torch.save(greedy_payload, Path(config["data_dir"]) / "processed" / "greedy_trajs.pt")


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    args = parser.parse_args()

    config = load_config(args.config)
    ensure_project_dirs(config)
    configure_runtime_env(config)
    set_seed(int(config.get("seed", 0)))
    config["_hpwl_phase"] = "offline"

    for circuit in config["train_circuits"]:
        _collect_for_circuit(config, circuit)
    _build_processed_tensors(config)


if __name__ == "__main__":
    main()
