#!/usr/bin/env python3
"""
SPDX-License-Identifier: MIT
Copyright (c) 2026 BandieraRosse

encode_gpt2_prompt.py — 将文本编码为 GPT-2 token ID 序列

用法：
  echo "Your prompt text here" | python llm/tools/encode_gpt2_prompt.py > /tmp/prompt.bin
  ./build/llm generate --prompt /tmp/prompt.bin --steps 200

输出二进制格式：
  [4 bytes: uint32 count] [count * 4 bytes: int32 token IDs]

依赖：
  pip install tiktoken
"""

import struct
import sys

import tiktoken


def main() -> None:
    text = sys.stdin.read()
    enc = tiktoken.get_encoding("gpt2")
    tokens = enc.encode_ordinary(text)

    # 输出：4-byte 数量 + count * 4-byte token ID
    sys.stdout.buffer.write(struct.pack("<I", len(tokens)))
    for tok in tokens:
        sys.stdout.buffer.write(struct.pack("<i", tok))

    # 输出到 stderr 提示信息
    print(f"Encoded {len(tokens)} tokens", file=sys.stderr)


if __name__ == "__main__":
    main()
