from __future__ import annotations

import shlex
import sys
from pathlib import Path


def _ensure_project_root_on_path() -> None:
    candidates = [
        Path(__file__).resolve().parent,
        Path.cwd(),
        Path('/root/project_snapshot/rl_logic_seq'),
        Path('/root/modal_state/rl_logic_seq'),
    ]
    for candidate in candidates:
        if (candidate / 'src').is_dir():
            candidate_str = str(candidate)
            if candidate_str not in sys.path:
                sys.path.insert(0, candidate_str)
            return
    raise ModuleNotFoundError("Could not locate rl_logic_seq project root for importing src")


_ensure_project_root_on_path()

import modal

from src.modal_utils import REMOTE_STATE_ROOT, build_modal_image, prepare_runtime_config, run_module, sync_snapshot_to_workspace

APP_NAME = "rl-logic-seq-train"
VOLUME_NAME = "rl-logic-seq-state"
IMAGE = build_modal_image()
app = modal.App(APP_NAME)
volume = modal.Volume.from_name(VOLUME_NAME, create_if_missing=True)
MODULES = {
    "collect": "src.collect_d2",
    "offline": "src.train_gso2o",
    "online": "src.online_gso2o",
}


@app.function(
    image=IMAGE,
    volumes={str(REMOTE_STATE_ROOT): volume},
    timeout=12 * 60 * 60,
    cpu=8,
    memory=32768,
)
def run_training(
    stage: str = "offline",
    config: str = "configs/iscas85_hpwl.json",
    abc_binary: str | None = None,
    mapping_lib: str | None = None,
    extra_args: list[str] | None = None,
) -> None:
    if stage not in MODULES:
        raise ValueError(f"Unsupported stage: {stage}")
    workspace = sync_snapshot_to_workspace()
    require_abc = stage in {"collect", "online"}
    runtime_config = prepare_runtime_config(
        workspace,
        config,
        abc_binary=abc_binary,
        mapping_lib=mapping_lib,
        require_abc=require_abc,
    )
    run_module(MODULES[stage], runtime_config, extra_args or [], workspace)
    volume.commit()


@app.local_entrypoint()
def main(
    stage: str = "offline",
    config: str = "configs/iscas85_hpwl.json",
    abc_binary: str = "",
    mapping_lib: str = "",
    extra_args: str = "",
) -> None:
    run_training.remote(
        stage=stage,
        config=config,
        abc_binary=abc_binary or None,
        mapping_lib=mapping_lib or None,
        extra_args=shlex.split(extra_args) if extra_args else [],
    )
