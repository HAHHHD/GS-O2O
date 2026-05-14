#!/usr/bin/env bash
set -euo pipefail

cd "$(dirname "$0")/.."

uv run python -m src.expanded_experiments \
  --config configs/comb45_hpwl_h20_op9.json \
  --methods gso2o,awac,iql,cql \
  --seeds 0 \
  --budgets 0,32,64,96
