#!/bin/bash
# SPDX-License-Identifier: MIT
# Copyright (c) 2026 BandieraRosse
#
# download_gpt2_model.sh — 下载 GPT-2 124M checkpoint + tokenizer
#
# 数据来源：Karpathy llmc-starter-pack (HuggingFace datasets)
#    gpt2_124M.bin         — 预训练权重（约 500MB）
#    gpt2_tokenizer.bin    — BPE 词表（约 24MB）
#
# 参考：https://github.com/karpathy/llm.c

set -e

BASE_URL="https://huggingface.co/datasets/karpathy/llmc-starter-pack/resolve/main"
MODEL_DIR="llm/models"

mkdir -p "$MODEL_DIR"

echo "╔══════════════════════════════════════════╗"
echo "║  下载 GPT-2 124M 模型资源                ║"
echo "╚══════════════════════════════════════════╝"

if [ -f "$MODEL_DIR/gpt2_124M.bin" ] && [ -f "$MODEL_DIR/gpt2_tokenizer.bin" ]; then
    echo "  模型文件已存在，跳过下载。"
    echo "  如需重新下载，请删除："
    echo "    rm $MODEL_DIR/gpt2_124M.bin"
    echo "    rm $MODEL_DIR/gpt2_tokenizer.bin"
    echo "  然后重新运行 $0"
    exit 0
fi

if [ ! -f "$MODEL_DIR/gpt2_124M.bin" ]; then
    echo ""
    echo "  [1/2] 下载 gpt2_124M.bin (~500MB) ..."
    echo "  从 $BASE_URL/gpt2_124M.bin"
    curl -L -o "$MODEL_DIR/gpt2_124M.bin" "$BASE_URL/gpt2_124M.bin?download=true" \
        --progress-bar
    echo "  ✓ 完成"
fi

if [ ! -f "$MODEL_DIR/gpt2_tokenizer.bin" ]; then
    echo ""
    echo "  [2/2] 下载 gpt2_tokenizer.bin (~24MB) ..."
    echo "  从 $BASE_URL/gpt2_tokenizer.bin"
    curl -L -o "$MODEL_DIR/gpt2_tokenizer.bin" "$BASE_URL/gpt2_tokenizer.bin?download=true" \
        --progress-bar
    echo "  ✓ 完成"
fi

echo ""
echo "  所有文件已保存到 $MODEL_DIR/"
ls -lh "$MODEL_DIR/"
echo ""
echo "现在可以运行推理："
echo "  make llm                     # 编译"
echo "  ./build/llm generate --steps 100  # 无条件生成"
