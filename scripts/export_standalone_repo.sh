#!/usr/bin/env bash
set -euo pipefail

SRC_DIR="$(cd "$(dirname "$0")/.." && pwd)"
DEST_DIR="${1:-${SRC_DIR%/}-release}"

if [ -e "$DEST_DIR" ]; then
  echo "Destination already exists: $DEST_DIR" >&2
  echo "Choose a new path or remove the existing directory first." >&2
  exit 1
fi

mkdir -p "$DEST_DIR"

rsync -a \
  --exclude '.git/' \
  --exclude '.venv/' \
  --exclude '.uv-cache/' \
  --exclude '__pycache__/' \
  --exclude '*/__pycache__/' \
  --exclude '*.pyc' \
  --exclude '*.pyo' \
  --exclude '*.pyd' \
  --exclude '*.so' \
  --exclude '*.egg-info/' \
  --exclude '.DS_Store' \
  --exclude 'abc.history' \
  --exclude 'data/' \
  --exclude 'data_*/' \
  --exclude 'results/' \
  --exclude 'results_*/' \
  --exclude 'work/' \
  --exclude 'work_*/' \
  --exclude 'models/checkpoints/' \
  --exclude 'models/checkpoints_*/' \
  --exclude 'paper/*.aux' \
  --exclude 'paper/*.bbl' \
  --exclude 'paper/*.blg' \
  --exclude 'paper/*.fdb_latexmk' \
  --exclude 'paper/*.fls' \
  --exclude 'paper/*.log' \
  --exclude 'paper/*.out' \
  --exclude 'paper/*.pdfsync' \
  --exclude 'paper/*.synctex.gz' \
  --exclude 'paper/assets/.mplcache/' \
  "$SRC_DIR/" "$DEST_DIR/"

cat <<EOF
Standalone export created at:
  $DEST_DIR

Next steps:
  cd "$DEST_DIR"
  git init
  git branch -M main
  git remote add origin https://github.com/HAHHHD/GS-O2O.git
  git add .
  git commit -m "Initial commit: GS-O2O project release"
  git push -u origin main
EOF
