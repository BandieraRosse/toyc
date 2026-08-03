#!/usr/bin/env python3
"""Compare two little-endian FP32 logit files."""

import argparse
import struct
from pathlib import Path


def read(path):
    data = path.read_bytes()
    if len(data) % 4:
        raise ValueError(f"{path} size is not a multiple of four")
    return struct.unpack(f"<{len(data) // 4}f", data)


def top(values, count=10):
    return sorted(range(len(values)), key=values.__getitem__, reverse=True)[:count]


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("reference", type=Path)
    parser.add_argument("actual", type=Path)
    args = parser.parse_args()
    reference = read(args.reference)
    actual = read(args.actual)
    if len(reference) != len(actual):
        raise ValueError(f"length mismatch: {len(reference)} != {len(actual)}")
    errors = [abs(a - b) for a, b in zip(reference, actual)]
    ref_top = top(reference)
    actual_top = top(actual)
    print(f"count: {len(reference)}")
    print(f"max_abs_error: {max(errors):.9g}")
    print(f"mean_abs_error: {sum(errors) / len(errors):.9g}")
    print(f"reference_argmax: {ref_top[0]}")
    print(f"actual_argmax: {actual_top[0]}")
    print(f"reference_top10: {ref_top}")
    print(f"actual_top10: {actual_top}")
    print(f"top10_overlap: {len(set(ref_top) & set(actual_top))}/10")


if __name__ == "__main__":
    main()
