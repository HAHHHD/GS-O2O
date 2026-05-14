# GS-O2O

Greedy-Structured Offline-to-Online Reinforcement Learning for HPWL-driven logic optimization sequencing.

Repository:
[https://github.com/HAHHHD/GS-O2O](https://github.com/HAHHHD/GS-O2O)

## Overview

This repository contains the code for `GS-O2O`, an offline-to-online reinforcement learning framework for sequential logic optimization under an expensive HPWL reward oracle.

At each step, the agent selects one ABC optimization operator. The true reward is computed from a deterministic backend flow that includes mapping and physically-aware replacement, while test-time action selection is restricted to cheap AIG-level features only.

The main method:

- collects a greedy-structured offline dataset by evaluating all actions at each state and rolling forward only the best immediate-HPWL branch,
- learns decomposed reward, value, and Q-ensemble models from cheap AIG features,
- performs budgeted online adaptation using only one HPWL query per executed action,
- deploys without querying mapping, placement, or HPWL during action selection.

The main experiment setting in this release is:

- `comb45` benchmark suite
- `32` training circuits, `13` test circuits
- horizon `H = 20`
- `9` logic optimization operators
- fair round-aligned online budgets `0 / 32 / 64 / 96`

## Repository Layout

This release contains only source code, configs, included benchmarks, scripts, and the vendored Modal ABC source snapshot.

```text
GS-O2O-release/
  benchmarks/          benchmark BLIFs and split metadata
  configs/             stable experiment configs
  scripts/             runnable entrypoints
  src/                 main implementation
  vendor/              vendored ABC source snapshot for Modal builds

  modal_train.py
  modal_evaluate.py
  pyproject.toml
  uv.lock
  README.md
```

The following directories are created only when you run experiments and are not tracked in Git:

- `data*/`
- `models/checkpoints*/`
- `results*/`
- `work*/`

## Main Files

- `src/collect_d2.py`: offline D2 dataset collection
- `src/train_gso2o.py`: offline GS-O2O training
- `src/online_gso2o.py`: online GS-O2O training
- `src/evaluate.py`: evaluation on train/test splits
- `src/baselines_awac.py`: AWAC baseline
- `src/baselines_iql.py`: IQL baseline
- `src/baselines_cql.py`: CQL baseline
- `src/expanded_experiments.py`: serial experiment driver for the main comparison
- `scripts/run_comb45_main.sh`: one-command reproduction of the main paper experiment

## Environment Setup

### Requirements

- Python `>= 3.10`
- [`uv`](https://docs.astral.sh/uv/)
- a working ABC binary
- the ASAP7 liberty file used by the HPWL backend flow

### Install Python Dependencies

From the repository root:

```bash
uv sync
```

## External Paths

The release is set up to use the vendored local runtime assets inside this repository:

- `vendor/modal_abc_src/abc`
- `vendor/modal_abc_src/abc.rc`
- `vendor/modal_abc_src/asap7_clean.lib`

The ABC runner automatically sources `abc.rc` when it is located next to the configured `abc` binary.

Important caveat: the checked-in `vendor/modal_abc_src/abc` binary is platform-specific. It should work if it matches your local OS and architecture, but users on other systems may need to rebuild or replace it while keeping the same path.

The main config is:

- `configs/comb45_hpwl_h20_op9.json`

Before running locally, update `abc_binary` and `mapping_lib` in the config to:

- `vendor/modal_abc_src/abc`
- `vendor/modal_abc_src/asap7_clean.lib`

YAML mirrors are also included for convenience.

## Reproducing the Main Experiment

The cleanest way to reproduce the main `comb45` comparison is:

```bash
./scripts/run_comb45_main.sh
```

This runs:

1. D2 collection if processed tensors are missing
2. offline training for `gso2o`, `awac`, `iql`, and `cql`
3. offline evaluation at budget `0`
4. online training
5. evaluation at budgets `32`, `64`, and `96`
6. aggregation tables

Equivalent manual command:

```bash
uv run python -m src.expanded_experiments \
  --config configs/comb45_hpwl_h20_op9.json \
  --methods gso2o,awac,iql,cql \
  --seeds 0 \
  --budgets 0,32,64,96
```

### Why `32 / 64 / 96`?

The online comparison uses shuffled rounds over the `32` training circuits:

- `32` episodes = 1 full pass over the train pool
- `64` episodes = 2 full passes
- `96` episodes = 3 full passes

This is the fair comparison used in the final benchmark table.

## Reproducing Individual Stages

### 1. Offline D2 collection

```bash
uv run python -m src.collect_d2 --config configs/comb45_hpwl_h20_op9.json
```

Generated outputs:

- `data_comb45_h20_op9/offline_d2/`
- `data_comb45_h20_op9/processed/`

### 2. GS-O2O offline training

```bash
uv run python -m src.train_gso2o --config configs/comb45_hpwl_h20_op9.json
```

### 3. GS-O2O online training

```bash
uv run python -m src.online_gso2o --config configs/comb45_hpwl_h20_op9.json
```

### 4. Evaluate a method

Offline-only:

```bash
uv run python -m src.evaluate \
  --config configs/comb45_hpwl_h20_op9.json \
  --method gso2o \
  --split test \
  --budget 0 \
  --method-label gso2o_budget000
```

Evaluate a saved online checkpoint:

```bash
uv run python -m src.evaluate \
  --config configs/comb45_hpwl_h20_op9.json \
  --method gso2o \
  --split test \
  --budget 96 \
  --method-label gso2o_budget096 \
  --checkpoint-dir models/checkpoints_comb45_h20_op9/seed_000/budget_0096
```

### 5. Baselines

Offline:

```bash
uv run python -m src.baselines_awac --config configs/comb45_hpwl_h20_op9.json --stage offline
uv run python -m src.baselines_iql  --config configs/comb45_hpwl_h20_op9.json --stage offline
uv run python -m src.baselines_cql  --config configs/comb45_hpwl_h20_op9.json --stage offline
```

Online:

```bash
uv run python -m src.baselines_awac --config configs/comb45_hpwl_h20_op9.json --stage online
uv run python -m src.baselines_iql  --config configs/comb45_hpwl_h20_op9.json --stage online
uv run python -m src.baselines_cql  --config configs/comb45_hpwl_h20_op9.json --stage online
```

## Generated Outputs

After running experiments, the main generated outputs are:

- `results_comb45_h20_op9/seed_000/logs/`
- `results_comb45_h20_op9/seed_000/sequences/`
- `results_comb45_h20_op9/seed_000/tables/`

Important aggregate tables include:

- `results_comb45_h20_op9/tables/all_checkpoint_results.csv`
- `results_comb45_h20_op9/tables/refine_variant_checkpoint_results.csv`
- `results_comb45_h20_op9/tables/final_two_settings_32_64_96.csv`

## Notes on Metrics

- The project uses corrected geomeans for ratio metrics.
- `parity` can produce zero HPWL under the deterministic backend flow, so ratio geomeans exclude it.
- The greedy reference is a privileged oracle policy that evaluates all actions using true HPWL at every step. It is an upper bound, not a deployment-feasible baseline under the cheap-test-time constraint.

## Modal Support

Modal entrypoints are included but optional:

- `modal_train.py`
- `modal_evaluate.py`

The main benchmark results discussed during development were run locally on CPU, but the Modal infrastructure is included for remote execution.

## Publishing This Repository

If this directory is already your clean standalone repo, push it with:

```bash
git add .
git commit -m "Initial commit: GS-O2O project release"
git push -u origin main
```
