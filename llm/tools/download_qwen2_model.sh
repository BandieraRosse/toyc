#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

MODEL_ID="${QWEN2_MODEL_ID:-Qwen/Qwen2.5-0.5B-Instruct}"
MODEL_DIR="${QWEN2_MODEL_DIR:-llm/models/qwen2.5-0.5b-instruct}"
BASE_URL="${QWEN2_BASE_URL:-https://huggingface.co/${MODEL_ID}/resolve/main}"

if ! command -v curl >/dev/null 2>&1; then
    echo "error: curl is required to download model files" >&2
    exit 1
fi

mkdir -p "$MODEL_DIR"
echo "Downloading $MODEL_ID to $MODEL_DIR"
files=(config.json generation_config.json tokenizer.json tokenizer_config.json model.safetensors)
for file in "${files[@]}"; do
    if [ -s "$MODEL_DIR/$file" ]; then
        echo "  exists: $file"
        continue
    fi
    echo "  fetch:  $file"
    curl --fail --location --retry 3 --output "$MODEL_DIR/$file.part" "$BASE_URL/$file"
    mv "$MODEL_DIR/$file.part" "$MODEL_DIR/$file"
done
