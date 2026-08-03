#!/bin/bash
# SPDX-License-Identifier: MIT
set -euo pipefail

MODEL_ID="${QWEN2_MODEL_ID:-Qwen/Qwen2.5-0.5B-Instruct}"
MODEL_DIR="${QWEN2_MODEL_DIR:-llm/models/qwen2.5-0.5b-instruct}"

if ! command -v modelscope >/dev/null 2>&1; then
    echo "error: modelscope CLI is not installed" >&2
    echo "install it with: python3 -m pip install modelscope" >&2
    exit 1
fi

mkdir -p "$MODEL_DIR"
echo "Downloading $MODEL_ID from ModelScope to $MODEL_DIR"
modelscope download --model "$MODEL_ID" --local_dir "$MODEL_DIR"
