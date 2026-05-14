from __future__ import annotations

import json
import os
import statistics
from collections import defaultdict
from pathlib import Path

import numpy as np

from .abc_runner import export_strashed_blif, get_basic_abc_stats
from .utils import ensure_dir, sanitize_token, sha1_file


def _feature_work_dir() -> Path:
    base = os.environ.get("RL_LOGIC_SEQ_WORK_DIR", "work")
    return ensure_dir(Path(base) / "feature_extract")


def _join_blif_continuations(lines: list[str]) -> list[str]:
    merged: list[str] = []
    current = ""
    for raw in lines:
        line = raw.rstrip()
        if not line or line.lstrip().startswith("#"):
            continue
        current = f"{current} {line.lstrip()}".strip() if current else line
        if current.endswith("\\"):
            current = current[:-1].rstrip()
            continue
        merged.append(current.strip())
        current = ""
    if current:
        merged.append(current.strip())
    return merged


def _parse_strashed_blif(blif_path: str | Path) -> dict[str, float]:
    lines = _join_blif_continuations(Path(blif_path).read_text(encoding="utf-8").splitlines())
    pis: list[str] = []
    pos: list[str] = []
    names_blocks: list[tuple[list[str], str]] = []
    idx = 0
    while idx < len(lines):
        line = lines[idx]
        if line.startswith(".inputs"):
            pis.extend(line.split()[1:])
        elif line.startswith(".outputs"):
            pos.extend(line.split()[1:])
        elif line.startswith(".names"):
            tokens = line.split()[1:]
            if not tokens:
                raise RuntimeError(f"Malformed .names line in {blif_path}: {line}")
            fanins = tokens[:-1]
            output = tokens[-1]
            names_blocks.append((fanins, output))
            idx += 1
            while idx < len(lines) and not lines[idx].startswith("."):
                idx += 1
            continue
        idx += 1

    fanout = defaultdict(int)
    levels: dict[str, int] = {name: 0 for name in pis}
    node_kinds: dict[str, str] = {}
    for fanins, output in names_blocks:
        if len(fanins) == 2:
            node_kinds[output] = "and"
        elif len(fanins) <= 1:
            node_kinds[output] = "buf"
        else:
            node_kinds[output] = "other"
        for fanin in fanins:
            fanout[fanin] += 1
        if not fanins:
            levels[output] = 0
        else:
            levels[output] = 1 + max(levels.get(fanin, 0) for fanin in fanins)

    for po in pos:
        fanout[po] += 1

    width_by_level = defaultdict(int)
    for name, kind in node_kinds.items():
        if kind == "and":
            width_by_level[levels[name]] += 1

    pi_fanouts = [fanout.get(name, 0) for name in pis]
    and_nodes = [name for name, kind in node_kinds.items() if kind == "and"]
    and_fanouts = [fanout.get(name, 0) for name in and_nodes]
    all_fanouts = pi_fanouts + and_fanouts

    def _mean(values: list[int]) -> float:
        return float(sum(values) / len(values)) if values else 0.0

    def _std(values: list[int]) -> float:
        if len(values) <= 1:
            return 0.0
        return float(statistics.pstdev(values))

    return {
        "width": float(max(width_by_level.values()) if width_by_level else 0),
        "avg_pi_fanout": _mean(pi_fanouts),
        "std_pi_fanout": _std(pi_fanouts),
        "avg_and_fanout": _mean(and_fanouts),
        "std_and_fanout": _std(and_fanouts),
        "max_fanout": float(max(all_fanouts) if all_fanouts else 0.0),
        "parsed_num_and_nodes": float(len(and_nodes)),
        "parsed_aig_level": float(max((levels[name] for name in and_nodes), default=0)),
    }


def extract_aig_graph_stats(design_path: str, abc_binary: str = "abc") -> dict:
    """
    Return:
        num_pis
        num_pos
        num_and_nodes
        aig_level
        width
        avg_pi_fanout
        std_pi_fanout
        avg_and_fanout
        std_and_fanout
        max_fanout

    Implementation notes:
        - Use ABC print_stats for num_pis, num_pos, num_and_nodes, aig_level.
        - For width and fanout statistics, export a normalized BLIF and parse graph structure in Python.
        - If graph parsing fails, raise a clear error and save the design/log.
    """
    basic = get_basic_abc_stats(design_path, abc_binary=abc_binary)
    work_dir = _feature_work_dir()
    tmp_name = f"{sanitize_token(Path(design_path).stem)}_{sha1_file(design_path)[:10]}.blif"
    strashed_path = work_dir / tmp_name
    try:
        export_strashed_blif(design_path, str(strashed_path), abc_binary=abc_binary)
        parsed = _parse_strashed_blif(strashed_path)
    except Exception as exc:
        failure_dir = ensure_dir(work_dir / "failures")
        snapshot = failure_dir / Path(design_path).name
        snapshot.write_bytes(Path(design_path).read_bytes())
        raise RuntimeError(f"Failed to extract graph stats for {design_path}: {exc}") from exc
    result = dict(basic)
    result.update(
        {
            "width": int(parsed["width"]),
            "avg_pi_fanout": float(parsed["avg_pi_fanout"]),
            "std_pi_fanout": float(parsed["std_pi_fanout"]),
            "avg_and_fanout": float(parsed["avg_and_fanout"]),
            "std_and_fanout": float(parsed["std_and_fanout"]),
            "max_fanout": float(parsed["max_fanout"]),
        }
    )
    return result


def extract_features(design_path: str, t: int, H: int, initial_stats: dict, abc_binary: str = "abc") -> np.ndarray:
    """
    Return 15-dimensional unnormalized feature vector.
    """
    stats = extract_aig_graph_stats(design_path, abc_binary=abc_binary)
    return np.array(
        [
            stats["num_pis"],
            stats["num_pos"],
            stats["num_and_nodes"],
            stats["aig_level"],
            stats["width"],
            stats["avg_pi_fanout"],
            stats["std_pi_fanout"],
            stats["avg_and_fanout"],
            stats["std_and_fanout"],
            stats["max_fanout"],
            stats["num_and_nodes"] / max(initial_stats["num_and_nodes"], 1),
            stats["aig_level"] / max(initial_stats["aig_level"], 1),
            stats["width"] / max(initial_stats["width"], 1),
            t / H,
            (H - t) / H,
        ],
        dtype=np.float32,
    )


class FeatureNormalizer:
    def __init__(self) -> None:
        self.mean: np.ndarray | None = None
        self.std: np.ndarray | None = None

    def fit(self, X: np.ndarray) -> None:
        X = np.asarray(X, dtype=np.float32)
        if X.ndim != 2:
            raise ValueError(f"Expected 2D array for fit(), got shape {X.shape}.")
        self.mean = X.mean(axis=0)
        self.std = np.maximum(X.std(axis=0), 1e-6)

    def transform(self, X: np.ndarray) -> np.ndarray:
        if self.mean is None or self.std is None:
            raise RuntimeError("FeatureNormalizer must be fit before transform().")
        X = np.asarray(X, dtype=np.float32)
        return (X - self.mean) / self.std

    def save(self, path: str) -> None:
        if self.mean is None or self.std is None:
            raise RuntimeError("FeatureNormalizer must be fit before save().")
        path_obj = Path(path)
        ensure_dir(path_obj.parent)
        payload = {"mean": self.mean.tolist(), "std": self.std.tolist()}
        path_obj.write_text(json.dumps(payload, indent=2), encoding="utf-8")

    @classmethod
    def load(cls, path: str) -> "FeatureNormalizer":
        payload = json.loads(Path(path).read_text(encoding="utf-8"))
        normalizer = cls()
        normalizer.mean = np.asarray(payload["mean"], dtype=np.float32)
        normalizer.std = np.asarray(payload["std"], dtype=np.float32)
        return normalizer
