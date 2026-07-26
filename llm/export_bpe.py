#!/usr/bin/env python3
"""
SPDX-License-Identifier: MIT
Copyright (c) 2026 BandieraRosse

export_bpe.py — 从 tiktoken 导出 GPT-2 BPE merge 规则为二进制文件

输出：llm/models/bpe_merges.bin

二进制格式：
  [4 bytes: num_merges]
  [256 × 2 bytes: byte_to_token_id]   // 每个字节对应的 token ID
  [num_merges × {2 bytes left, 2 bytes right, 2 bytes merged_id}]
    // left, right: 两个父 token 的 ID
    // merged_id: merge 产生的新 token ID

依赖：pip install tiktoken
"""

import struct
import sys

import tiktoken


def build_merge_table(enc) -> list:
    """
    从 tiktoken 的 mergeable_ranks 中重建 BPE merge 树。

    mergeable_ranks 是 bytes→rank 的字典，rank 即 token ID。
    对于每个长度 >1 的 token，找到它的两个父 token（即 merge 了哪两个 token 得到它）。
    """
    mergeable_ranks = enc._mergeable_ranks  # type: ignore

    # 按 rank (token ID) 升序遍历
    sorted_items = sorted(mergeable_ranks.items(), key=lambda x: x[1])

    # byte_to_token: byte value (0-255) → token ID
    byte_to_token = {}

    # merge_table: merged_token_id → (left_id, right_id)
    merge_table = {}

    for token_bytes, token_id in sorted_items:
        if len(token_bytes) == 1:
            # 字节级 token
            byte_to_token[token_bytes[0]] = token_id
            continue

        # 寻找最优分割点：尝试所有切分方式，找到 max(left_rank, right_rank) 最小的
        best_split = None
        best_max_rank = 2**31  # 足够大

        for k in range(1, len(token_bytes)):
            left_bytes = token_bytes[:k]
            right_bytes = token_bytes[k:]

            if left_bytes in mergeable_ranks and right_bytes in mergeable_ranks:
                left_id = mergeable_ranks[left_bytes]
                right_id = mergeable_ranks[right_bytes]
                max_rank = max(left_id, right_id)

                # 两个父 token 的 rank 必须都小于合并后的 token 的 rank
                if max_rank < token_id and max_rank < best_max_rank:
                    best_max_rank = max_rank
                    best_split = (left_id, right_id)

        if best_split is not None:
            merge_table[token_id] = best_split
        else:
            # 理论上不应发生，但安全起见忽略
            print(
                f"Warning: cannot decompose token {token_id} "
                f"(bytes={token_bytes.hex()})",
                file=sys.stderr,
            )

    return byte_to_token, merge_table


def main():
    enc = tiktoken.get_encoding("gpt2")
    byte_to_token, merge_table = build_merge_table(enc)

    # 验证字节映射完整性
    for b in range(256):
        if b not in byte_to_token:
            print(f"Error: byte {b} has no token mapping!", file=sys.stderr)
            sys.exit(1)

    # 写二进制文件（按 left,right 排序，C 端可直接二分查找）
    merges = sorted(merge_table.items(), key=lambda x: (x[1][0], x[1][1]))
    num_merges = len(merges)

    path = "llm/models/bpe_merges.bin"
    with open(path, "wb") as f:
        # header: merge 数量
        f.write(struct.pack("<I", num_merges))
        # byte_to_token 表 (256 × uint16)
        for b in range(256):
            f.write(struct.pack("<H", byte_to_token[b]))
        # merge 规则
        for merged_id, (left, right) in merges:
            f.write(struct.pack("<HHH", left, right, merged_id))

    byte_tokens = [byte_to_token[b] for b in range(256)]
    print(f"Written {path}: {num_merges} merge rules, {len(byte_tokens)} byte tokens")
    print(f"  Byte token ID range: {min(byte_tokens)} - {max(byte_tokens)}")
    print(f"  Merge token ID range: {merges[0][0]} - {merges[-1][0]}")


if __name__ == "__main__":
    main()
