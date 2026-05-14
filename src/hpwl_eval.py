from __future__ import annotations

import re
from pathlib import Path

from .abc_runner import run_abc_script
from .utils import (
    ensure_dir,
    hpwl_cache_path,
    increment_hpwl_counter,
    read_json,
    read_text,
    sha1_bytes,
    utc_timestamp,
    write_json,
    write_text,
)


def _render_hpwl_script(template_text: str, design_path: str, config: dict) -> str:
    return template_text.format(
        design_path=design_path,
        mapping_lib=config["mapping_lib"],
    )


def _parse_hpwl_from_output(output: str) -> float | None:
    patterns = [
        re.compile(r"Mode\s*:\s*Final\s*\n\s*HPWL\s*:\s*([0-9eE+\-.]+)"),
        re.compile(r"HPWL\s*=\s*([0-9eE+\-.]+)"),
        re.compile(r"globalHpwl\s*=\s*([0-9eE+\-.]+)"),
    ]
    for pattern in patterns:
        matches = pattern.findall(output)
        if matches:
            return float(matches[-1])
    return None


def evaluate_hpwl(design_path: str, circuit_name: str, state_id: str, config: dict) -> float:
    design_bytes = Path(design_path).read_bytes()
    template_path = Path(config["hpwl_flow_template"])
    template_text = read_text(template_path)
    script = _render_hpwl_script(template_text, design_path=design_path, config=config)
    cache_key = sha1_bytes(design_bytes + script.encode("utf-8"))
    cache = read_json(hpwl_cache_path(config), default={})
    if cache_key in cache:
        increment_hpwl_counter(config, circuit_name, state_id, cache_hit=True)
        return float(cache[cache_key]["hpwl"])

    output = run_abc_script(script, abc_binary=config["abc_binary"], timeout_s=600)
    hpwl = _parse_hpwl_from_output(output)
    log_dir = ensure_dir(Path(config["result_dir"]) / "logs" / "hpwl")
    stem = f"{utc_timestamp()}_{circuit_name}_{state_id}"
    write_text(log_dir / f"{stem}.log", output)
    if hpwl is None:
        failure_dir = ensure_dir(Path(config["result_dir"]) / "logs" / "hpwl_failures")
        write_text(failure_dir / f"{stem}.abc", script)
        write_text(failure_dir / f"{stem}.log", output)
        raise RuntimeError(f"Failed to parse HPWL for circuit={circuit_name}, state_id={state_id}.")
    cache[cache_key] = {
        "circuit": circuit_name,
        "state_id": state_id,
        "hpwl": float(hpwl),
        "design_path": str(Path(design_path).resolve()),
        "template_path": str(template_path.resolve()),
        "mapping_lib": str(Path(config["mapping_lib"]).resolve()),
    }
    write_json(hpwl_cache_path(config), cache)
    increment_hpwl_counter(config, circuit_name, state_id, cache_hit=False)
    return float(hpwl)
