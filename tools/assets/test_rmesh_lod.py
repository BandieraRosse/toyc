#!/usr/bin/env python3
"""Regression tests for vertex-compacted RFM2 LOD output."""

import importlib.util
import struct
import unittest
from pathlib import Path


REPO = Path(__file__).resolve().parents[2]
SPEC = importlib.util.spec_from_file_location("rmesh_lod", REPO / "tools/rmesh_lod.py")
LOD = importlib.util.module_from_spec(SPEC)
SPEC.loader.exec_module(LOD)


class RmeshLodTest(unittest.TestCase):
    def test_region_profile_skin_key_separates_weight_discontinuity(self):
        profile = {"weights": [(1, 2, 30000, 1), (1, 2, 38000, 1)],
                   "high": set(), "medium": set(), "joints": [],
                   "high_primitives": set(), "medium_primitives": set(),
                   "high_divisor": 4, "medium_divisor": 2,
                   "weight_step": 4096, "protection": [0, 0]}
        vertex = struct.pack("<iii", 0, 0, 0) + bytes(6) + struct.pack("<HH", 0, 0) + bytes(10)
        data = bytes(64 + 16 + 40) + vertex + vertex
        info = {"version": 10, "vertices": 2, "vertex_at": 120,
                "vertex_bytes": 32}
        mapped = LOD.cluster_map(data, info, 4, profile=profile)
        self.assertEqual(mapped, [0, 1])

    def test_compacts_matching_vertex_and_skin_records_and_preserves_chr1(self):
        vertex_bytes = 36
        vertices = b"".join(bytes([value]) * vertex_bytes for value in range(4))
        bones = bytes(range(32))
        weights = b"".join(bytes([10 + value]) * 8 for value in range(4))
        names_and_ik = b"root\0IK-tail"
        skin_bytes = 32 + len(bones) + len(weights) + len(names_and_ik)
        skin = bytearray(32)
        skin[:4] = b"SKN1"
        struct.pack_into("<IIIIII", skin, 4, skin_bytes, 1, 32, 4, 8,
                         len(names_and_ik))
        chr1 = b"CHR1-preserved-byte-for-byte"
        prefix = bytes(64 + 16 + 40)
        data = prefix + vertices + bytes(12) + bytes(skin) + bones + weights + names_and_ik + chr1
        info = {
            "version": 14, "vertices": 4, "vertex_bytes": vertex_bytes,
            "vertex_at": len(prefix), "index_at": len(prefix) + len(vertices),
            "skin_at": len(prefix) + len(vertices) + 12,
        }

        referenced, indices, compact, tail = LOD.compact_vertices(data, info, [3, 1, 3])

        self.assertEqual(referenced, [1, 3])
        self.assertEqual(indices, [1, 0, 1])
        self.assertEqual(compact, vertices[vertex_bytes:2 * vertex_bytes] + vertices[3 * vertex_bytes:])
        self.assertEqual(struct.unpack_from("<I", tail, 16)[0], 2)
        self.assertEqual(tail[32 + len(bones):32 + len(bones) + 16], weights[8:16] + weights[24:32])
        self.assertTrue(tail.endswith(chr1))


if __name__ == "__main__":
    unittest.main()
