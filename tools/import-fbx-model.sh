#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: tools/import-fbx-model.sh [--force] [--target-triangles N] FBX MODEL_NAME" >&2
    exit 2
}

force=0
target_triangles=5000
while [[ $# -gt 0 ]]; do
    case $1 in
        --force) force=1; shift ;;
        --target-triangles)
            [[ $# -ge 2 && $2 =~ ^[0-9]+$ ]] || usage
            target_triangles=$2; shift 2 ;;
        --) shift; break ;;
        -*) usage ;;
        *) break ;;
    esac
done
[[ $# -eq 2 ]] || usage

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
fbx=$(cd -- "$(dirname -- "$1")" && pwd)/$(basename -- "$1")
model_name=$2
[[ -f $fbx && -n $model_name && $model_name != */* ]] || usage

blender=${BLENDER:-}
if [[ -z $blender ]]; then
    blender=$(command -v blender || command -v blender.exe || true)
fi
[[ -n $blender ]] || {
    echo "import-fbx-model: Blender not found; set BLENDER=/path/to/blender" >&2
    exit 1
}
if [[ -z ${BLENDER_PYTHONPATH:-} && -d $repo_root/.blender-python ]]; then
    BLENDER_PYTHONPATH=$repo_root/.blender-python
fi

work_dir=$(mktemp -d /tmp/rasterfall-fbx.XXXXXX)
trap 'rm -rf -- "$work_dir"' EXIT
pmx="$work_dir/$model_name.pmx"

echo "import-fbx-model: Blender=$blender target_triangles=$target_triangles"
XDG_CONFIG_HOME="$work_dir/config" \
BLENDER_PYTHONPATH="${BLENDER_PYTHONPATH:-}" \
PYTHONPATH="${BLENDER_PYTHONPATH:-${PYTHONPATH:-}}" \
"$blender" --background --factory-startup \
    --python "$script_dir/blender/export_rasterfall_character.py" -- \
    --input "$fbx" --output "$pmx" --target-triangles "$target_triangles"
[[ -s $pmx ]] || {
    echo "import-fbx-model: Blender did not produce $model_name.pmx" >&2
    exit 1
}

args=()
if [[ $force -eq 1 ]]; then args+=(--force); fi
TOYASSET_PNG_FORMAT=png \
"$script_dir/import-pmx-model.sh" "${args[@]}" "$work_dir" "$model_name"
find "$repo_root/rasterfall/private-assets/models/$model_name.textures" \
    -maxdepth 1 -type f ! -name '*.ttex' -delete
echo "import-fbx-model: wrote rasterfall/private-assets/models/$model_name.rmesh"
