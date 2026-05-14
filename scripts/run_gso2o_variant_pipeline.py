from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path


def _run(cmd: list[str]) -> None:
    subprocess.run(cmd, check=True)


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--method-prefix", required=True)
    parser.add_argument("--budgets", nargs="+", type=int, default=[0, 32, 64, 96])
    args = parser.parse_args()

    config_path = Path(args.config).resolve()
    config = json.loads(config_path.read_text(encoding="utf-8"))
    models_dir = Path(config["models_dir"])

    _run([sys.executable, "-m", "src.online_gso2o", "--config", str(config_path)])

    for budget in args.budgets:
        method_label = f"{args.method_prefix}_budget{budget:03d}"
        cmd = [
            sys.executable,
            "-m",
            "src.evaluate",
            "--config",
            str(config_path),
            "--method",
            "gso2o",
            "--split",
            "test",
            "--method-label",
            method_label,
        ]
        if budget > 0:
            checkpoint_dir = models_dir / f"budget_{budget:04d}"
            cmd.extend(["--checkpoint-dir", str(checkpoint_dir), "--budget", str(budget)])
        else:
            cmd.extend(["--budget", "0"])
        _run(cmd)


if __name__ == "__main__":
    main()
