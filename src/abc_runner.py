from __future__ import annotations

import os
import re
import subprocess
import time
from pathlib import Path

from .utils import ensure_dir, sanitize_token, utc_timestamp, write_text


def _results_log_dir() -> Path:
    base = os.environ.get("RL_LOGIC_SEQ_RESULTS_LOG_DIR", "results/logs")
    return ensure_dir(base)




def _wrap_with_abc_rc(script: str, abc_binary: str) -> str:
    abc_rc = Path(abc_binary).resolve().with_name("abc.rc")
    if abc_rc.exists() and f"source {abc_rc}" not in script:
        return f"source {abc_rc}\n{script}"
    return script


def run_abc_script(script: str, abc_binary: str = "abc", timeout_s: int = 300) -> str:
    """
    Run ABC with the provided script string.
    Return combined stdout/stderr.
    Raise RuntimeError if ABC exits with nonzero status or times out.
    Save failed scripts/logs under results/logs/.
    """
    run_dir = ensure_dir(_results_log_dir() / "abc_runs")
    script_id = sanitize_token(f"{utc_timestamp()}_{time.time_ns()}")
    script_path = run_dir / f"{script_id}.abc"
    log_path = run_dir / f"{script_id}.log"
    script = _wrap_with_abc_rc(script, abc_binary)
    write_text(script_path, script)
    try:
        completed = subprocess.run(
            [abc_binary, "-f", str(script_path)],
            capture_output=True,
            text=True,
            timeout=timeout_s,
            check=False,
        )
    except subprocess.TimeoutExpired as exc:
        combined = (exc.stdout or "") + (exc.stderr or "")
        write_text(log_path, combined)
        failure_dir = ensure_dir(_results_log_dir() / "abc_failures")
        write_text(failure_dir / f"{script_id}.abc", script)
        write_text(failure_dir / f"{script_id}.log", combined)
        raise RuntimeError(f"ABC timed out after {timeout_s}s.") from exc
    combined = (completed.stdout or "") + (completed.stderr or "")
    write_text(log_path, combined)
    if completed.returncode != 0:
        failure_dir = ensure_dir(_results_log_dir() / "abc_failures")
        write_text(failure_dir / f"{script_id}.abc", script)
        write_text(failure_dir / f"{script_id}.log", combined)
        raise RuntimeError(f"ABC exited with code {completed.returncode}.")
    return combined


def apply_action(input_design: str, output_design: str, action_cmd: str, abc_binary: str = "abc") -> None:
    """
    Apply one ABC operator to input_design and write output_design.

    ABC script:
        read_blif {input_design}
        strash
        {action_cmd}
        write_blif {output_design}
    """
    output_path = Path(output_design)
    ensure_dir(output_path.parent)
    script = "\n".join(
        [
            f"read_blif {input_design}",
            "strash",
            action_cmd,
            f"write_blif {output_design}",
        ]
    )
    run_abc_script(script, abc_binary=abc_binary)
    if not output_path.exists():
        raise RuntimeError(f"ABC did not create expected output design: {output_design}")


def apply_action_to_temp_state(
    input_design: str,
    output_dir: str | Path,
    output_stem: str,
    action_cmd: str,
    abc_binary: str = "abc",
) -> str:
    output_path = Path(output_dir) / f"{sanitize_token(output_stem)}.blif"
    apply_action(str(input_design), str(output_path), action_cmd, abc_binary=abc_binary)
    return str(output_path)


def get_basic_abc_stats(design_path: str, abc_binary: str = "abc") -> dict:
    """
    Run ABC print_stats after strash.

    Return at least:
        num_pis
        num_pos
        num_and_nodes
        aig_level

    Suggested ABC script:
        read_blif {design_path}
        strash
        print_stats
    """
    script = "\n".join([f"read_blif {design_path}", "strash", "print_stats"])
    output = run_abc_script(script, abc_binary=abc_binary)
    ansi = re.compile(r"\x1b\[[0-9;]*m")
    clean_output = ansi.sub("", output)
    pattern = re.compile(
        r"i/o\s*=\s*(?P<num_pis>\d+)\s*/\s*(?P<num_pos>\d+)\s+lat\s*=\s*\d+\s+and\s*=\s*(?P<num_and_nodes>\d+)\s+lev\s*=\s*(?P<aig_level>\d+)"
    )
    matches = list(pattern.finditer(clean_output))
    if not matches:
        raise RuntimeError(f"Failed to parse ABC print_stats output for {design_path}.")
    match = matches[-1]
    return {
        "num_pis": int(match.group("num_pis")),
        "num_pos": int(match.group("num_pos")),
        "num_and_nodes": int(match.group("num_and_nodes")),
        "aig_level": int(match.group("aig_level")),
    }


def export_strashed_blif(design_path: str, output_path: str, abc_binary: str = "abc") -> None:
    ensure_dir(Path(output_path).parent)
    script = "\n".join([f"read_blif {design_path}", "strash", f"write_blif {output_path}"])
    run_abc_script(script, abc_binary=abc_binary)
    if not Path(output_path).exists():
        raise RuntimeError(f"Failed to export strashed BLIF for {design_path}.")
