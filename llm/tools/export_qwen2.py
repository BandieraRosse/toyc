#!/usr/bin/env python3
"""Export Hugging Face Qwen2/Qwen2.5 safetensors to Toyc FP32 checkpoint."""

import argparse
import json
import struct
from pathlib import Path

MAGIC = b"TOYLLM1\0"
VERSION = 1
HEADER_SIZE = 32
ENTRY_SIZE = 128
NAME_SIZE = 64
ALIGNMENT = 64
DTYPE_F32 = 1


def align(value, alignment=ALIGNMENT):
    return (value + alignment - 1) // alignment * alignment


def wanted(name):
    if name in {"model.embed_tokens.weight", "model.norm.weight", "lm_head.weight"}:
        return True
    if not name.startswith("model.layers."):
        return False
    suffixes = (
        ".input_layernorm.weight", ".post_attention_layernorm.weight",
        ".self_attn.q_proj.weight", ".self_attn.q_proj.bias",
        ".self_attn.k_proj.weight", ".self_attn.k_proj.bias",
        ".self_attn.v_proj.weight", ".self_attn.v_proj.bias",
        ".self_attn.o_proj.weight", ".mlp.gate_proj.weight",
        ".mlp.up_proj.weight", ".mlp.down_proj.weight",
    )
    return name.endswith(suffixes)


def validate_config(model_dir):
    config = json.loads((model_dir / "config.json").read_text(encoding="utf-8"))
    if config.get("model_type") != "qwen2":
        raise ValueError("only model_type=qwen2 is supported")
    required = (
        "vocab_size", "hidden_size", "intermediate_size", "num_hidden_layers",
        "num_attention_heads", "num_key_value_heads", "max_position_embeddings",
        "rms_norm_eps", "rope_theta",
    )
    missing = [key for key in required if key not in config]
    if missing:
        raise ValueError(f"missing config fields: {', '.join(missing)}")
    return config


def tensor_sources(model_dir):
    shards = sorted(model_dir.glob("*.safetensors"))
    if not shards:
        raise FileNotFoundError(f"no *.safetensors files in {model_dir}")
    return shards


def collect_names(shards):
    from safetensors import safe_open

    sources = {}
    for shard in shards:
        with safe_open(shard, framework="pt", device="cpu") as handle:
            for name in handle.keys():
                if wanted(name):
                    if name in sources:
                        raise ValueError(f"duplicate tensor {name}")
                    sources[name] = shard
    return sources


def expected_names(config):
    names = {"model.embed_tokens.weight", "model.norm.weight"}
    for layer in range(config["num_hidden_layers"]):
        prefix = f"model.layers.{layer}"
        names.update({
            f"{prefix}.input_layernorm.weight",
            f"{prefix}.post_attention_layernorm.weight",
            f"{prefix}.self_attn.q_proj.weight", f"{prefix}.self_attn.q_proj.bias",
            f"{prefix}.self_attn.k_proj.weight", f"{prefix}.self_attn.k_proj.bias",
            f"{prefix}.self_attn.v_proj.weight", f"{prefix}.self_attn.v_proj.bias",
            f"{prefix}.self_attn.o_proj.weight",
            f"{prefix}.mlp.gate_proj.weight", f"{prefix}.mlp.up_proj.weight",
            f"{prefix}.mlp.down_proj.weight",
        })
    if not config.get("tie_word_embeddings", False):
        names.add("lm_head.weight")
    return names


def write_tensor(output, handle, name):
    tensor_slice = handle.get_slice(name)
    shape = tuple(tensor_slice.get_shape())
    if not 1 <= len(shape) <= 4:
        raise ValueError(f"unsupported rank for {name}: {shape}")
    row_size = 1
    for dimension in shape[1:]:
        row_size *= dimension
    rows_per_chunk = max(1, 16_000_000 // row_size)
    nbytes = 0
    for start in range(0, shape[0], rows_per_chunk):
        tensor = tensor_slice[start:start + rows_per_chunk]
        data = tensor.float().contiguous().numpy().tobytes(order="C")
        output.write(data)
        nbytes += len(data)
    return shape, nbytes


def export(model_dir, output_path):
    from safetensors import safe_open

    config = validate_config(model_dir)
    shards = tensor_sources(model_dir)
    sources = collect_names(shards)
    required = expected_names(config)
    missing = sorted(required - sources.keys())
    if missing:
        raise ValueError("checkpoint is missing tensors:\n  " + "\n  ".join(missing))
    names = sorted(required)
    directory_offset = HEADER_SIZE
    data_offset = align(directory_offset + len(names) * ENTRY_SIZE)
    entries = []

    with output_path.open("wb+") as output:
        output.write(b"\0" * data_offset)
        for index, name in enumerate(names, 1):
            position = align(output.tell())
            output.write(b"\0" * (position - output.tell()))
            with safe_open(sources[name], framework="pt", device="cpu") as handle:
                shape, nbytes = write_tensor(output, handle, name)
            entries.append((name, shape, position, nbytes))
            print(f"[{index:3}/{len(names)}] {name} {shape}")

        output.seek(0)
        output.write(struct.pack("<8sIIQQ", MAGIC, VERSION, len(entries),
                                 directory_offset, data_offset))
        output.seek(directory_offset)
        for name, shape, offset, nbytes in entries:
            encoded = name.encode("utf-8")
            if len(encoded) >= NAME_SIZE:
                raise ValueError(f"tensor name is too long: {name}")
            padded_shape = shape + (0,) * (4 - len(shape))
            entry = struct.pack("<64sII4QQQ8x", encoded, DTYPE_F32, len(shape),
                                *padded_shape, offset, nbytes)
            if len(entry) != ENTRY_SIZE:
                raise AssertionError("bad tensor directory entry size")
            output.write(entry)

    print(f"wrote {output_path} ({output_path.stat().st_size / (1024**3):.2f} GiB)")


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("model_dir", type=Path,
                        help="Hugging Face Qwen2/Qwen2.5 model directory")
    parser.add_argument("output", type=Path, help="output .bin checkpoint")
    args = parser.parse_args()
    export(args.model_dir, args.output)


if __name__ == "__main__":
    main()
