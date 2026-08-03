#!/usr/bin/env python3
"""Create a tiny deterministic Qwen2 checkpoint for the C test suite."""

import struct
import sys
from pathlib import Path

HEADER_SIZE = 32
ENTRY_SIZE = 128


def identity(rows, cols):
    return [1.0 if row == col else 0.0
            for row in range(rows) for col in range(cols)]


def main():
    output_path = Path(sys.argv[1])
    tensors = {
        "model.embed_tokens.weight": ((8, 4), [
            0, 0, 0, 0, 1, 2, 3, 4, 2, 1, 0, -1,
            1, 1, 1, 1, -1, 1, -1, 1, 3, 0, 0, 0,
            0, 3, 0, 0, 0, 0, 3, 0,
        ]),
        "model.norm.weight": ((4,), [1] * 4),
        "model.layers.0.input_layernorm.weight": ((4,), [1] * 4),
        "model.layers.0.post_attention_layernorm.weight": ((4,), [1] * 4),
        "model.layers.0.self_attn.q_proj.weight": ((4, 4), identity(4, 4)),
        "model.layers.0.self_attn.q_proj.bias": ((4,), [0] * 4),
        "model.layers.0.self_attn.k_proj.weight": ((2, 4), identity(2, 4)),
        "model.layers.0.self_attn.k_proj.bias": ((2,), [0] * 2),
        "model.layers.0.self_attn.v_proj.weight": ((2, 4), identity(2, 4)),
        "model.layers.0.self_attn.v_proj.bias": ((2,), [0] * 2),
        "model.layers.0.self_attn.o_proj.weight": ((4, 4), identity(4, 4)),
        "model.layers.0.mlp.gate_proj.weight": ((8, 4), [0] * 32),
        "model.layers.0.mlp.up_proj.weight": ((8, 4), [0] * 32),
        "model.layers.0.mlp.down_proj.weight": ((4, 8), [0] * 32),
    }
    names = sorted(tensors)
    data_offset = (HEADER_SIZE + len(names) * ENTRY_SIZE + 63) // 64 * 64
    entries = []
    with output_path.open("wb+") as output:
        output.write(b"\0" * data_offset)
        for name in names:
            shape, values = tensors[name]
            offset = output.tell()
            data = struct.pack(f"<{len(values)}f", *values)
            output.write(data)
            entries.append((name, shape, offset, len(data)))
        output.seek(0)
        output.write(struct.pack("<8sIIQQ", b"TOYLLM1\0", 1, len(entries),
                                 HEADER_SIZE, data_offset))
        output.seek(HEADER_SIZE)
        for name, shape, offset, nbytes in entries:
            dims = shape + (0,) * (4 - len(shape))
            output.write(struct.pack("<64sII4QQQ8x", name.encode(), 1,
                                     len(shape), *dims, offset, nbytes))


if __name__ == "__main__":
    main()
