#!/usr/bin/env bash
set -euo pipefail

repo_root=$(cd -- "$(dirname -- "$0")/.." && pwd)
venv_dir="$repo_root/.venv/map-layout"
python_bin="${PYTHON:-python3}"

"$python_bin" -m venv "$venv_dir"
"$venv_dir/bin/python" -m pip install -r "$repo_root/tools/requirements-map-layout.txt"

if ! command -v fc-match >/dev/null 2>&1 || ! fc-match -f '%{family}' 'Noto Sans CJK SC' 2>/dev/null | grep -qi 'Noto Sans CJK'; then
    if command -v apt-get >/dev/null 2>&1 && command -v sudo >/dev/null 2>&1 && sudo -n true 2>/dev/null; then
        sudo apt-get install -y fonts-noto-cjk
    elif command -v apt-get >/dev/null 2>&1 && command -v dpkg-deb >/dev/null 2>&1; then
        font_root="$repo_root/tools/.map-layout-fonts"
        download_dir=$(mktemp -d)
        trap 'rm -rf "$download_dir"' EXIT
        (cd "$download_dir" && apt-get download fonts-noto-cjk)
        package=$(find "$download_dir" -maxdepth 1 -name 'fonts-noto-cjk_*.deb' -print -quit)
        test -n "$package"
        rm -rf "$font_root"
        mkdir -p "$font_root"
        dpkg-deb -x "$package" "$font_root"
    else
        echo "Noto Sans CJK SC not found; install fonts-noto-cjk or provide --font PATH" >&2
        exit 2
    fi
fi

echo "map-layout environment ready: $venv_dir"
