from __future__ import annotations

import os
import shutil
import subprocess
import sys
from pathlib import Path
from typing import Iterable

LOCAL_PROJECT_ROOT = Path(__file__).resolve().parents[1]
LOCAL_MODAL_ABC_SOURCE_ROOT = LOCAL_PROJECT_ROOT / "vendor" / "modal_abc_src"

REMOTE_SNAPSHOT_ROOT = Path("/root/project_snapshot/rl_logic_seq")
REMOTE_STATE_ROOT = Path("/root/modal_state")
REMOTE_WORKSPACE = REMOTE_STATE_ROOT / "rl_logic_seq"
REMOTE_ABC_SOURCE_ROOT = Path("/root/modal_vendor/modal_abc_src")
REMOTE_ABC_BUILD_ROOT = Path("/root/modal_vendor/modal_abc_build")
REMOTE_ABC_BINARY = REMOTE_ABC_BUILD_ROOT / "abc"
REMOTE_MAPPING_LIB = REMOTE_ABC_SOURCE_ROOT / "asap7_clean.lib"

SYNC_PATHS = [
    "README.md",
    "pyproject.toml",
    "benchmarks",
    "configs",
    "scripts",
    "src",
    "data/offline_d2",
    "data/processed",
    "data/hpwl_cache.json",
]
PERSISTENT_DIRS = [
    "data/online",
    "models/checkpoints",
    "results/logs",
    "results/sequences",
    "results/tables",
    "work",
]
MODAL_ABC_APT_PACKAGES = [
    "build-essential",
    "cmake",
    "ninja-build",
    "bison",
    "flex",
    "tcl-dev",
    "tk-dev",
    "zlib1g-dev",
    "libx11-dev",
    "libjpeg-dev",
    "libboost-dev",
    "swig",
    "libreadline-dev",
    "libncurses-dev",
    "pkg-config",
]
MODAL_PROJECT_IGNORE = [
    ".git",
    ".git/**",
    ".venv",
    ".venv/**",
    ".uv-cache",
    ".uv-cache/**",
    "vendor/modal_abc_src",
    "vendor/modal_abc_src/**",
    "work",
    "work/**",
    "results",
    "results/**",
    "models/checkpoints",
    "models/checkpoints/**",
    "data/online",
    "data/online/**",
    "**/__pycache__",
    "**/*.pyc",
]
MODAL_VENDOR_IGNORE = [
    "**/__pycache__",
    "**/*.pyc",
    "**/CMakeFiles",
    "**/CMakeFiles/**",
    "**/*.o",
    "**/*.a",
    "**/*.so",
]


def build_modal_image():
    import modal

    return (
        modal.Image.debian_slim(python_version="3.10")
        .apt_install(*MODAL_ABC_APT_PACKAGES)
        .uv_sync()
        .add_local_dir(
            str(LOCAL_MODAL_ABC_SOURCE_ROOT),
            remote_path=str(REMOTE_ABC_SOURCE_ROOT),
            copy=True,
            ignore=MODAL_VENDOR_IGNORE,
        )
        .run_commands(
            f"cmake -G Ninja -S {REMOTE_ABC_SOURCE_ROOT} -B {REMOTE_ABC_BUILD_ROOT} -DABC_SKIP_TESTS=ON -DCMAKE_BUILD_TYPE=Release",
            f"cmake --build {REMOTE_ABC_BUILD_ROOT} --target abc --parallel 8",
        )
        .add_local_dir(
            str(LOCAL_PROJECT_ROOT),
            remote_path=str(REMOTE_SNAPSHOT_ROOT),
            ignore=MODAL_PROJECT_IGNORE,
        )
    )


def _copy_path(src: Path, dst: Path) -> None:
    if not src.exists():
        return
    if src.is_dir():
        if dst.exists():
            shutil.rmtree(dst)
        shutil.copytree(src, dst)
        return
    dst.parent.mkdir(parents=True, exist_ok=True)
    shutil.copy2(src, dst)


def sync_snapshot_to_workspace() -> Path:
    REMOTE_STATE_ROOT.mkdir(parents=True, exist_ok=True)
    REMOTE_WORKSPACE.mkdir(parents=True, exist_ok=True)
    for rel in SYNC_PATHS:
        _copy_path(REMOTE_SNAPSHOT_ROOT / rel, REMOTE_WORKSPACE / rel)
    for rel in PERSISTENT_DIRS:
        (REMOTE_WORKSPACE / rel).mkdir(parents=True, exist_ok=True)
    return REMOTE_WORKSPACE


def prepare_runtime_config(
    workspace: Path,
    config_path: str,
    *,
    abc_binary: str | None = None,
    mapping_lib: str | None = None,
    require_abc: bool = False,
) -> str:
    sys.path.insert(0, str(workspace))
    try:
        from src.utils import load_config, write_json

        resolved = load_config(workspace / config_path)
        payload = {key: value for key, value in resolved.items() if not key.startswith("_")}
        payload["abc_binary"] = abc_binary or str(REMOTE_ABC_BINARY)
        payload["mapping_lib"] = mapping_lib or str(REMOTE_MAPPING_LIB)
        if require_abc:
            missing = []
            for key in ("abc_binary", "mapping_lib"):
                path = Path(str(payload[key]))
                if not path.exists():
                    missing.append(f"{key}={path}")
            if missing:
                raise RuntimeError(
                    "Remote Modal execution requires the built Linux ABC toolchain. "
                    + "Missing: "
                    + ", ".join(missing)
                )
        out_path = workspace / "configs" / f"modal_{Path(config_path).stem}.json"
        write_json(out_path, payload)
        return str(out_path.relative_to(workspace))
    finally:
        sys.path.pop(0)


def run_module(module: str, config_path: str, extra_args: Iterable[str], workspace: Path) -> None:
    cmd = [sys.executable, "-m", module, "--config", config_path, *list(extra_args)]
    env = dict(os.environ)
    env["PYTHONPATH"] = str(workspace)
    subprocess.run(cmd, cwd=workspace, env=env, check=True)
