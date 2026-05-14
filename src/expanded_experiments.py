from __future__ import annotations

import argparse
import copy
import csv
import subprocess
import sys
from pathlib import Path
from typing import Iterable

from .utils import (
    ensure_dir,
    experiment_budgets,
    experiment_seeds,
    geometric_mean,
    load_config,
    read_json,
    write_json,
)


METHOD_RUNNERS = {
    "gso2o": {
        "offline": ["-m", "src.train_gso2o"],
        "online": ["-m", "src.online_gso2o"],
    },
    "awac": {
        "offline": ["-m", "src.baselines_awac", "--stage", "offline"],
        "online": ["-m", "src.baselines_awac", "--stage", "online"],
    },
    "iql": {
        "offline": ["-m", "src.baselines_iql", "--stage", "offline"],
        "online": ["-m", "src.baselines_iql", "--stage", "online"],
    },
    "cql": {
        "offline": ["-m", "src.baselines_cql", "--stage", "offline"],
        "online": ["-m", "src.baselines_cql", "--stage", "online"],
    },
}


def _method_label(method: str, budget: int) -> str:
    return f"{method}_budget{int(budget):03d}"


def _seed_tag(seed: int) -> str:
    return f"seed_{int(seed):03d}"


def _runtime_config_path(base_config: dict, seed: int) -> Path:
    return ensure_dir(Path(base_config["work_dir"]) / "generated_configs") / f"{Path(base_config['_config_path']).stem}_{_seed_tag(seed)}.json"


def _runtime_config(base_config: dict, seed: int) -> tuple[dict, Path]:
    config = copy.deepcopy(base_config)
    seed_tag = _seed_tag(seed)
    config["seed"] = int(seed)
    config["work_dir"] = str(Path(base_config["work_dir"]) / seed_tag)
    config["result_dir"] = str(Path(base_config["result_dir"]) / seed_tag)
    config["models_dir"] = str(Path(base_config["models_dir"]) / seed_tag)
    config_path = _runtime_config_path(base_config, seed)
    write_json(config_path, config)
    return config, config_path


def _run_python(project_root: Path, args: list[str]) -> None:
    subprocess.run([sys.executable, *args], cwd=project_root, check=True)


def _collect_if_needed(project_root: Path, config_path: Path, config: dict, force: bool) -> None:
    processed_dir = Path(config["data_dir"]) / "processed"
    required = [
        processed_dir / "d2_train.pt",
        processed_dir / "greedy_trajs.pt",
        processed_dir / "feature_normalizer.json",
    ]
    if not force and all(path.exists() for path in required):
        return
    _run_python(project_root, ["-m", "src.collect_d2", "--config", str(config_path)])


def _run_method_stage(project_root: Path, config_path: Path, method: str, stage: str) -> None:
    if method not in METHOD_RUNNERS:
        raise ValueError(f"Unsupported method: {method}")
    _run_python(project_root, [*METHOD_RUNNERS[method][stage], "--config", str(config_path)])


def _evaluate_budget(project_root: Path, config_path: Path, method: str, budget: int, checkpoint_dir: Path | None = None) -> None:
    args = [
        "-m",
        "src.evaluate",
        "--config",
        str(config_path),
        "--method",
        method,
        "--split",
        "test",
        "--budget",
        str(int(budget)),
        "--method-label",
        _method_label(method, budget),
    ]
    if checkpoint_dir is not None:
        args.extend(["--checkpoint-dir", str(checkpoint_dir)])
    _run_python(project_root, args)


def _iter_methods(base_config: dict, methods_arg: str | None) -> list[str]:
    if methods_arg:
        return [part.strip() for part in methods_arg.split(",") if part.strip()]
    return list(base_config.get("experiment", {}).get("methods", METHOD_RUNNERS.keys()))


def _iter_budgets(base_config: dict, budgets_arg: str | None) -> list[int]:
    if budgets_arg:
        return sorted({int(part.strip()) for part in budgets_arg.split(",") if part.strip()})
    return experiment_budgets(base_config)


def _iter_seeds(base_config: dict, seeds_arg: str | None) -> list[int]:
    if seeds_arg:
        return [int(part.strip()) for part in seeds_arg.split(",") if part.strip()]
    return experiment_seeds(base_config)


def _load_eval_result(result_dir: Path, method_label: str, circuit: str) -> dict:
    path = result_dir / "sequences" / method_label / "test" / f"{circuit}.json"
    if not path.exists():
        raise FileNotFoundError(f"Missing evaluation result: {path}")
    return read_json(path)


def _write_rows(path: Path, fieldnames: list[str], rows: Iterable[dict]) -> None:
    ensure_dir(path.parent)
    with path.open("w", encoding="utf-8", newline="") as handle:
        writer = csv.DictWriter(handle, fieldnames=fieldnames)
        writer.writeheader()
        for row in rows:
            writer.writerow(row)


def _aggregate_results(base_config: dict, seeds: list[int], methods: list[str], budgets: list[int]) -> None:
    result_root = Path(base_config["result_dir"])
    circuits = list(base_config["test_circuits"])
    per_circuit_rows: list[dict] = []
    per_seed_rows: list[dict] = []
    for seed in seeds:
        seed_result_dir = result_root / _seed_tag(seed)
        offline_lookup: dict[str, float] = {}
        for method in methods:
            for budget in budgets:
                label = _method_label(method, budget)
                ratio_initial = []
                ratio_greedy = []
                final_hpwl_values = []
                for circuit in circuits:
                    result = _load_eval_result(seed_result_dir, label, circuit)
                    greedy = _load_eval_result(seed_result_dir, "greedy", circuit)
                    initial_hpwl = float(result["initial_hpwl"])
                    final_hpwl = float(result["final_hpwl"])
                    greedy_hpwl = float(greedy["final_hpwl"])
                    row = {
                        "seed": int(seed),
                        "method": method,
                        "budget": int(budget),
                        "circuit": circuit,
                        "initial_hpwl": initial_hpwl,
                        "greedy_hpwl": greedy_hpwl,
                        "final_hpwl": final_hpwl,
                        "hpwl_ratio_vs_initial": final_hpwl / max(initial_hpwl, 1e-12),
                        "hpwl_ratio_vs_greedy": final_hpwl / max(greedy_hpwl, 1e-12),
                        "hpwl_improvement_vs_greedy": (greedy_hpwl - final_hpwl) / max(greedy_hpwl, 1e-12),
                        "sequence": " ".join(result["sequence"]),
                    }
                    per_circuit_rows.append(row)
                    ratio_initial.append(row["hpwl_ratio_vs_initial"])
                    ratio_greedy.append(row["hpwl_ratio_vs_greedy"])
                    final_hpwl_values.append(final_hpwl)
                geo_initial = geometric_mean(ratio_initial)
                geo_greedy = geometric_mean(ratio_greedy)
                key = f"{seed}:{method}"
                if budget == 0:
                    offline_lookup[key] = geo_initial
                baseline_ratio = offline_lookup[key]
                per_seed_rows.append(
                    {
                        "seed": int(seed),
                        "method": method,
                        "budget": int(budget),
                        "geomean_final_hpwl": geometric_mean(final_hpwl_values),
                        "geomean_ratio_vs_initial": geo_initial,
                        "geomean_ratio_vs_greedy": geo_greedy,
                        "geomean_improvement_vs_greedy": 1.0 - geo_greedy,
                        "relative_gain_vs_budget0": (baseline_ratio - geo_initial) / max(baseline_ratio, 1e-12),
                    }
                )

    aggregate_rows: list[dict] = []
    for method in methods:
        for budget in budgets:
            rows = [row for row in per_seed_rows if row["method"] == method and row["budget"] == budget]
            if not rows:
                continue

            def _mean(metric: str) -> float:
                return sum(float(row[metric]) for row in rows) / len(rows)

            def _std(metric: str) -> float:
                mean = _mean(metric)
                return (sum((float(row[metric]) - mean) ** 2 for row in rows) / len(rows)) ** 0.5

            aggregate_rows.append(
                {
                    "method": method,
                    "budget": int(budget),
                    "num_seeds": len(rows),
                    "mean_geomean_final_hpwl": _mean("geomean_final_hpwl"),
                    "std_geomean_final_hpwl": _std("geomean_final_hpwl"),
                    "mean_geomean_ratio_vs_initial": _mean("geomean_ratio_vs_initial"),
                    "std_geomean_ratio_vs_initial": _std("geomean_ratio_vs_initial"),
                    "mean_geomean_ratio_vs_greedy": _mean("geomean_ratio_vs_greedy"),
                    "std_geomean_ratio_vs_greedy": _std("geomean_ratio_vs_greedy"),
                    "mean_geomean_improvement_vs_greedy": _mean("geomean_improvement_vs_greedy"),
                    "std_geomean_improvement_vs_greedy": _std("geomean_improvement_vs_greedy"),
                    "mean_relative_gain_vs_budget0": _mean("relative_gain_vs_budget0"),
                    "std_relative_gain_vs_budget0": _std("relative_gain_vs_budget0"),
                }
            )

    tables_dir = ensure_dir(result_root / "tables")
    _write_rows(
        tables_dir / "expanded_test_per_circuit.csv",
        [
            "seed",
            "method",
            "budget",
            "circuit",
            "initial_hpwl",
            "greedy_hpwl",
            "final_hpwl",
            "hpwl_ratio_vs_initial",
            "hpwl_ratio_vs_greedy",
            "hpwl_improvement_vs_greedy",
            "sequence",
        ],
        per_circuit_rows,
    )
    _write_rows(
        tables_dir / "expanded_test_per_seed_geomean.csv",
        [
            "seed",
            "method",
            "budget",
            "geomean_final_hpwl",
            "geomean_ratio_vs_initial",
            "geomean_ratio_vs_greedy",
            "geomean_improvement_vs_greedy",
            "relative_gain_vs_budget0",
        ],
        per_seed_rows,
    )
    _write_rows(
        tables_dir / "expanded_test_aggregate.csv",
        [
            "method",
            "budget",
            "num_seeds",
            "mean_geomean_final_hpwl",
            "std_geomean_final_hpwl",
            "mean_geomean_ratio_vs_initial",
            "std_geomean_ratio_vs_initial",
            "mean_geomean_ratio_vs_greedy",
            "std_geomean_ratio_vs_greedy",
            "mean_geomean_improvement_vs_greedy",
            "std_geomean_improvement_vs_greedy",
            "mean_relative_gain_vs_budget0",
            "std_relative_gain_vs_budget0",
        ],
        aggregate_rows,
    )


def _run_method_serial(project_root: Path, runtime_config: dict, runtime_config_path: Path, method: str, budgets: list[int], skip_offline: bool, skip_online: bool, skip_eval: bool) -> None:
    if not skip_offline:
        _run_method_stage(project_root, runtime_config_path, method, "offline")
    if not skip_eval and 0 in budgets:
        _evaluate_budget(project_root, runtime_config_path, method, 0)
    if not skip_online:
        _run_method_stage(project_root, runtime_config_path, method, "online")
    if not skip_eval:
        for budget in budgets:
            if budget == 0:
                continue
            checkpoint_dir = Path(runtime_config["models_dir"]) / f"budget_{int(budget):04d}"
            _evaluate_budget(project_root, runtime_config_path, method, budget, checkpoint_dir=checkpoint_dir)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--methods")
    parser.add_argument("--seeds")
    parser.add_argument("--budgets")
    parser.add_argument("--force-recollect", action="store_true")
    parser.add_argument("--skip-collect", action="store_true")
    parser.add_argument("--skip-offline", action="store_true")
    parser.add_argument("--skip-online", action="store_true")
    parser.add_argument("--skip-eval", action="store_true")
    parser.add_argument("--skip-aggregate", action="store_true")
    args = parser.parse_args()

    base_config = load_config(args.config)
    project_root = Path(base_config["_project_root"])
    methods = _iter_methods(base_config, args.methods)
    seeds = _iter_seeds(base_config, args.seeds)
    budgets = _iter_budgets(base_config, args.budgets)

    if not args.skip_collect:
        _collect_if_needed(project_root, Path(base_config["_config_path"]), base_config, args.force_recollect)

    for seed in seeds:
        runtime_config, runtime_config_path = _runtime_config(base_config, seed)
        for method in methods:
            _run_method_serial(
                project_root,
                runtime_config,
                runtime_config_path,
                method,
                budgets,
                args.skip_offline,
                args.skip_online,
                args.skip_eval,
            )

    if not args.skip_aggregate:
        _aggregate_results(base_config, seeds, methods, budgets)


if __name__ == "__main__":
    main()
