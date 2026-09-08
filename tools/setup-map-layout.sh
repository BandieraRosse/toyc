#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
venv_dir="$repo_root/.venv/map-layout"
python_bin="${PYTHON:-python3}"

"$python_bin" -m venv "$venv_dir"
"$venv_dir/bin/python" -m pip install -r "$repo_root/tools/requirements-map-layout.txt"

echo "map-layout environment ready: $venv_dir"
