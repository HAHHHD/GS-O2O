#!/usr/bin/env bash
set -euo pipefail

ROOT="/Users/cusgadmin/Documents/code/abcP/rl_logic_seq"
CONFIG="$ROOT/configs/comb45_hpwl_h20_op9.json"
D2_PT="$ROOT/data_comb45_h20_op9/processed/d2_train.pt"
GREEDY_PT="$ROOT/data_comb45_h20_op9/processed/greedy_trajs.pt"
NORM_JSON="$ROOT/data_comb45_h20_op9/processed/feature_normalizer.json"

while [[ ! -f "$D2_PT" || ! -f "$GREEDY_PT" || ! -f "$NORM_JSON" ]]; do
  echo "[$(date '+%Y-%m-%d %H:%M:%S')] waiting for comb45 processed data..."
  sleep 60
done

cd "$ROOT"
exec ./.venv/bin/python -m src.expanded_experiments \
  --config "$CONFIG" \
  --methods gso2o,awac,iql,cql \
  --seeds 0 \
  --budgets 0,10,50,200,400 \
  --skip-collect
