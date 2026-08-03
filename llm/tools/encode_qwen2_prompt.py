#!/usr/bin/env python3
"""Encode a Qwen2.5 ChatML prompt into Toyc's uint32 token file."""

import argparse
import struct
import sys
from pathlib import Path


def load_tokenizer(model_dir):
    try:
        from modelscope import AutoTokenizer
    except ImportError:
        from transformers import AutoTokenizer
    return AutoTokenizer.from_pretrained(str(model_dir), trust_remote_code=False)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model_dir", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--system", default="You are Qwen, created by Alibaba Cloud. You are a helpful assistant.")
    parser.add_argument("--prompt", help="user message; stdin is used when omitted")
    args = parser.parse_args()
    prompt = args.prompt if args.prompt is not None else sys.stdin.read()
    tokenizer = load_tokenizer(args.model_dir)
    messages = [
        {"role": "system", "content": args.system},
        {"role": "user", "content": prompt},
    ]
    token_ids = tokenizer.apply_chat_template(
        messages, tokenize=True, add_generation_prompt=True
    )
    # ModelScope 1.39 may return a Transformers BatchEncoding, while other
    # versions return the input-id list directly.
    if hasattr(token_ids, "keys") and "input_ids" in token_ids:
        token_ids = token_ids["input_ids"]
    if hasattr(token_ids, "tolist"):
        token_ids = token_ids.tolist()
    if token_ids and isinstance(token_ids[0], (list, tuple)):
        token_ids = token_ids[0]
    token_ids = [int(token_id) for token_id in token_ids]
    with args.output.open("wb") as output:
        output.write(struct.pack("<I", len(token_ids)))
        output.write(struct.pack(f"<{len(token_ids)}I", *token_ids))
    print(f"encoded {len(token_ids)} tokens to {args.output}", file=sys.stderr)


if __name__ == "__main__":
    main()
