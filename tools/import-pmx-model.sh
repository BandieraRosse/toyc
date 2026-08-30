#!/usr/bin/env bash
set -euo pipefail

usage() {
    echo "usage: tools/import-pmx-model.sh [--force] MODEL_DIR [MODEL_NAME]" >&2
    exit 2
}

force=0
if [[ ${1:-} == --force ]]; then
    force=1
    shift
fi
[[ $# -ge 1 && $# -le 2 ]] || usage

script_dir=$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)
repo_root=$(cd -- "$script_dir/.." && pwd)
model_dir=$(cd -- "$1" 2>/dev/null && pwd) || {
    echo "import-pmx-model: model directory not found: $1" >&2
    exit 1
}

mapfile -d '' pmx_files < <(find "$model_dir" -type f -iname '*.pmx' -print0)
if [[ ${#pmx_files[@]} -ne 1 ]]; then
    echo "import-pmx-model: expected exactly one PMX file, found ${#pmx_files[@]} in $model_dir" >&2
    exit 1
fi
pmx_file=${pmx_files[0]}
model_name=${2:-$(basename -- "${pmx_file%.*}")}
[[ -n $model_name && $model_name != */* && $model_name != . && $model_name != .. ]] || {
    echo "import-pmx-model: invalid model name: $model_name" >&2
    exit 1
}

output_root="$repo_root/rasterfall/private-assets/models"
output_mesh="$output_root/$model_name.rmesh"
output_textures="$output_root/$model_name.textures"
mkdir -p -- "$output_root"

if [[ -e $output_mesh || -e $output_textures ]]; then
    if [[ $force -ne 1 ]]; then
        echo "import-pmx-model: output already exists; use --force to replace $model_name" >&2
        exit 1
    fi
fi

work_dir=$(mktemp -d "$output_root/.import-$model_name.XXXXXX")
trap 'rm -rf -- "$work_dir"' EXIT
temp_mesh="$work_dir/$model_name.rmesh"
temp_textures="$work_dir/$model_name.textures"

echo "import-pmx-model: building converters"
make -C "$repo_root" -j app-pmx2rmesh build/toyasset

echo "import-pmx-model: importing $pmx_file"
"$repo_root/build/pmx2rmesh" "$pmx_file" "$temp_mesh" "$temp_textures"

texture_count=0
while IFS= read -r -d '' texture; do
    extension=${texture##*.}
    extension=${extension,,}
    output=${texture%.*}.ttex
    case $extension in
        png) format=${TOYASSET_PNG_FORMAT:-png1024} ;;
        bmp|spa|sph) format=bmp ;;
        jpg|jpeg) format=jpg ;;
        *)
            echo "import-pmx-model: unsupported copied texture: $texture" >&2
            exit 1
            ;;
    esac
    "$repo_root/build/toyasset" convert "$format" "$texture" "$output"
    "$repo_root/build/toyasset" validate "$output"
    texture_count=$((texture_count + 1))
done < <(find "$temp_textures" -maxdepth 1 -type f ! -name '*.ttex' -print0)

if [[ $force -eq 1 ]]; then
    rm -f -- "$output_mesh"
    rm -rf -- "$output_textures"
fi
mv -- "$temp_mesh" "$output_mesh"
mv -- "$temp_textures" "$output_textures"
echo "import-pmx-model: wrote $output_mesh and $texture_count TTEX textures"
