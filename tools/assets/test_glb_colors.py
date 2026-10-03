#!/usr/bin/env python3
"""Exercise the actual rigid GLB converter's linear-factor to sRGB8 boundary."""
from __future__ import annotations

import argparse
import json
import math
from pathlib import Path
import struct
import subprocess
import tempfile


ROOT = Path(__file__).resolve().parents[2]


def srgb8(value):
    value = min(1.0, max(0.0, value))
    encoded = 12.92 * value if value <= 0.0031308 else 1.055 * value ** (1.0 / 2.4) - 0.055
    return math.floor(encoded * 255.0 + 0.5)


def make_glb(path, factors):
    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    normals = struct.pack("<9f", 0, 0, 1, 0, 0, 1, 0, 0, 1)
    indices = struct.pack("<3H", 0, 1, 2)
    binary = positions + normals + indices + b"\0\0"
    document = {
        "asset": {"version": "2.0"}, "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [{"buffer": 0, "byteOffset": offset, "byteLength": size}
                        for offset, size in ((0, 36), (36, 36), (72, 6))],
        "accessors": [{"bufferView": index, "componentType": kind, "count": 3, "type": shape}
                      for index, kind, shape in ((0, 5126, "VEC3"), (1, 5126, "VEC3"), (2, 5123, "SCALAR"))],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1},
                                     "indices": 2, "material": 0}]}],
        "materials": [{"pbrMetallicRoughness": {
            "metallicFactor": 0.312, "roughnessFactor": 0.673,
            **({"baseColorFactor": "__COLOR_%d__" % i} if factor is not None else {})}}
            for i, factor in enumerate(factors)],
    }
    encoded = json.dumps(document, separators=(",", ":"))
    for i, factor in enumerate(factors):
        if factor is not None:
            encoded = encoded.replace('"__COLOR_%d__"' % i, "[" + ",".join(factor) + ",1]")
    encoded = encoded.encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 28 + len(encoded) + len(binary)) +
                     struct.pack("<II", len(encoded), 0x4E4F534A) + encoded +
                     struct.pack("<II", len(binary), 0x004E4942) + binary)


def check(converter):
    # Named anchors are independently known output bytes, not current asset RGB.
    anchors = [("0", 0), ("1", 255), ("0.18", 118), ("0.5", 188),
               ("0.0031308", 10), ("0.0031307", 10), ("0.0031309", 10),
               ("0.000381", 1), ("1e-5", 0), ("1.8E-1", 118),
               ("5E-1", 188), ("1e+0", 255), ("-0.1", 0), ("1.1", 255)]
    cases = [(name, (name, name, name), (value,) * 3) for name, value in anchors]
    cases += [("channel order and dim precision", ("3.81e-4", "0.18", "1"), (1, 118, 255)),
              ("default white", None, (255, 255, 255))]
    # Both sides of every 8-bit half-step catch truncation, early quantization,
    # wrong gamma and incorrect rounding, including every low-light byte edge.
    for byte in range(255):
        encoded = (byte + 0.5) / 255.0
        linear = encoded / 12.92 if encoded <= 0.04045 else ((encoded + 0.055) / 1.055) ** 2.4
        for direction in (-1, 1):
            value = linear + direction * 1e-10
            tokens = (format(value, ".17g"), format(value, ".17e"), format(1.0 - value, ".17g"))
            cases.append(("byte %d %s half-step" % (byte, "below" if direction < 0 else "above"),
                          tokens, tuple(srgb8(float(token)) for token in tokens)))
    with tempfile.TemporaryDirectory(prefix="glb-colors-", dir=ROOT / "tmp") as temporary:
        directory = Path(temporary)
        geometry = None
        for first in range(0, len(cases), 32):
            batch = cases[first:first + 32]
            factors = [item[1] for item in batch] + [None] * (32 - len(batch))
            source, target = directory / "colors.glb", directory / "colors.rmesh"
            make_glb(source, factors)
            subprocess.run([str(converter), str(source), str(target)], check=True,
                           stdout=subprocess.DEVNULL)
            data = target.read_bytes()
            assert data[:8] == b"RFM2\x02\0\0\0", "unexpected rigid mesh format"
            count, offset = struct.unpack_from("<I", data, 48)[0], struct.unpack_from("<I", data, 56)[0]
            assert count == 32
            current_geometry = data[:offset] + data[offset + count * 16:]
            if geometry is None:
                geometry = current_geometry
            assert geometry == current_geometry, "color changes altered geometry, bounds or primitive records"
            for i, (name, _, expected) in enumerate(batch):
                color, metallic, roughness, texture, flags = struct.unpack_from("<IHHII", data, offset + i * 16)
                actual = ((color >> 16) & 255, (color >> 8) & 255, color & 255)
                assert actual == expected, "%s: expected %s, got %s" % (name, expected, actual)
                assert (metallic, roughness, texture, flags) == (312 * 65535 // 1000, 673 * 65535 // 1000, 0xFFFFFFFF, 0), \
                    "color parsing altered non-color material fields"
    print("glb-colors: %d factor cases passed; all 255 sRGB byte boundaries, non-color fields and geometry preserved" % len(cases))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, default=ROOT / "build-windows/glb2rmesh.exe")
    args = parser.parse_args()
    (ROOT / "tmp").mkdir(exist_ok=True)
    check(args.converter.resolve())


if __name__ == "__main__":
    main()
