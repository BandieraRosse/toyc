#!/usr/bin/env python3
"""Minimal regression tests for the unified Rasterfall asset contract."""

import json
import shutil
import struct
import subprocess
import tempfile
import unittest
import zlib
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parents[1]
IMPORTER = HERE / "import_asset.py"


def png_rgb():
    def chunk(kind, payload):
        return (struct.pack(">I", len(payload)) + kind + payload +
                struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))
    return (b"\x89PNG\r\n\x1a\n" +
            chunk(b"IHDR", struct.pack(">IIBBBBB", 1, 1, 8, 2, 0, 0, 0)) +
            chunk(b"IDAT", zlib.compress(b"\x00\xff\x40\x20")) + chunk(b"IEND", b""))


def make_glb(path):
    positions = struct.pack("<9f", 0, 0, 0, 1, 0, 0, 0, 1, 0)
    normals = struct.pack("<9f", 0, 0, 1, 0, 0, 1, 0, 0, 1)
    uvs = struct.pack("<6f", 0, 0, 1, 0, 0, 1)
    indices = struct.pack("<3H", 0, 1, 2)
    image = png_rgb()
    offsets = []
    binary = bytearray()
    for payload in (positions, normals, uvs, indices, image):
        while len(binary) % 4:
            binary.append(0)
        offsets.append(len(binary))
        binary.extend(payload)
    document = {
        "asset": {"version": "2.0"},
        "buffers": [{"byteLength": len(binary)}],
        "bufferViews": [
            {"buffer": 0, "byteOffset": offsets[i], "byteLength": len(payload)}
            for i, payload in enumerate((positions, normals, uvs, indices, image))
        ],
        "accessors": [
            {"bufferView": 0, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 1, "componentType": 5126, "count": 3, "type": "VEC3"},
            {"bufferView": 2, "componentType": 5126, "count": 3, "type": "VEC2"},
            {"bufferView": 3, "componentType": 5123, "count": 3, "type": "SCALAR"}
        ],
        "images": [{"bufferView": 4, "mimeType": "image/png"}],
        "textures": [{"source": 0}],
        "materials": [{"pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}}],
        "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "NORMAL": 1, "TEXCOORD_0": 2},
                                      "indices": 3, "material": 0}]}]
    }
    encoded = json.dumps(document, separators=(",", ":")).encode()
    encoded += b" " * (-len(encoded) % 4)
    binary += b"\0" * (-len(binary) % 4)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    path.write_bytes(b"glTF" + struct.pack("<II", 2, total) +
                     struct.pack("<II", len(encoded), 0x4e4f534a) + encoded +
                     struct.pack("<II", len(binary), 0x004e4942) + binary)


class AssetImporterTest(unittest.TestCase):
    def test_glb_texture_lod_and_validation(self):
        if not (REPO / "build/glb2rmesh").is_file() or not (REPO / "build/toyasset").is_file():
            self.skipTest("converter binaries were not built")
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            source, manifest, output = root / "crate.glb", root / "crate.json", root / "out"
            make_glb(source)
            manifest.write_text(json.dumps({
                "schema": 1, "id": "test_crate", "type": "static_prop",
                "source": "crate.glb", "lods": [{"level": 1, "ratio": 0.5}]
            }))
            command = [str(IMPORTER), str(manifest), "--output-root", str(output), "--no-build"]
            subprocess.run(command, cwd=REPO, check=True)
            self.assertEqual(struct.unpack_from("<I", (output / "test_crate.rmesh").read_bytes(), 88)[0], 0)
            self.assertTrue((output / "test_crate.textures/texture_000.ttex").is_file())
            self.assertTrue((output / "test_crate_lod1.rmesh").is_file())
            subprocess.run(command + ["--validate-only"], cwd=REPO, check=True)

    def test_manifest_rejects_global_derived_fields(self):
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            manifest = root / "bad.json"
            manifest.write_text(json.dumps({"schema": 1, "id": "bad", "type": "static_prop",
                                            "source": "bad.glb", "output": "bad.rmesh"}))
            result = subprocess.run([str(IMPORTER), str(manifest), "--validate-only"],
                                    cwd=REPO, stdout=subprocess.PIPE, stderr=subprocess.PIPE)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"unknown manifest fields", result.stderr)


if __name__ == "__main__":
    unittest.main()
