#!/usr/bin/env python3
"""Write reference last-token logits for a Toyc prompt token file."""

import argparse
import struct
from pathlib import Path


def load_classes():
    try:
        from modelscope import AutoModelForCausalLM
    except ImportError:
        from transformers import AutoModelForCausalLM
    return AutoModelForCausalLM


def read_tokens(path):
    data = path.read_bytes()
    if len(data) < 4:
        raise ValueError("prompt token file is truncated")
    count = struct.unpack_from("<I", data)[0]
    if len(data) != 4 + count * 4:
        raise ValueError("prompt token file has an invalid size")
    return struct.unpack_from(f"<{count}I", data, 4)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model_dir", type=Path)
    parser.add_argument("prompt_tokens", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    import torch

    model_class = load_classes()
    model = model_class.from_pretrained(
        str(args.model_dir), dtype=torch.float32,
        local_files_only=True, trust_remote_code=False,
    )
    model.eval()
    tokens = torch.tensor([read_tokens(args.prompt_tokens)], dtype=torch.long)
    with torch.inference_mode():
        logits = model(input_ids=tokens, use_cache=False).logits[0, -1].float().cpu()
    args.output.write_bytes(struct.pack(f"<{logits.numel()}f", *logits.tolist()))
    print(f"wrote {logits.numel()} logits to {args.output}")


if __name__ == "__main__":
    main()
