#!/usr/bin/env python3
"""Stable geometry, unit and stale-cache contracts for manufacturing assets."""
import json
from pathlib import Path
import shutil
import struct
import tempfile
import unittest

import mesh_weaver_blueprints as weaver


TETRA_VERTICES = [(0., 0., 0.), (1., 0., 0.), (0., 1., 0.), (0., 0., 1.)]
TETRA_TRIANGLES = [(0, 2, 1), (0, 1, 3), (0, 3, 2), (1, 2, 3)]


class GeometryContract(unittest.TestCase):
    def test_volume_is_cubic_and_independent_of_translation_or_winding(self):
        base = weaver.topology_stats(TETRA_VERTICES, TETRA_TRIANGLES)
        self.assertTrue(base["volume_valid"])
        self.assertAlmostEqual(base["volume_m3"], 1/6)
        transformed = [(-2*x+1000, 2*y-500, 2*z+900) for x, y, z in TETRA_VERTICES]
        changed = weaver.topology_stats(transformed, TETRA_TRIANGLES)
        self.assertTrue(changed["volume_valid"])
        self.assertAlmostEqual(changed["volume_m3"], 8*base["volume_m3"])
        self.assertEqual(changed["inward_shell_count"], 1)

    def test_uv_seams_weld_but_real_topology_changes_the_count(self):
        duplicated = [TETRA_VERTICES[index] for triangle in TETRA_TRIANGLES for index in triangle]
        triangles = [(i, i+1, i+2) for i in range(0, 12, 3)]
        stats = weaver.topology_stats(duplicated, triangles)
        self.assertEqual((stats["vertex_count"], stats["exported_vertex_count"], stats["triangle_count"]), (4, 12, 4))
        self.assertAlmostEqual(stats["volume_m3"], 1/6)
        # Subdivide one face without altering the solid: work changes, volume does not.
        vertices = TETRA_VERTICES + [(1/3, 1/3, 1/3)]
        divided = TETRA_TRIANGLES[:3] + [(1, 2, 4), (2, 3, 4), (3, 1, 4)]
        stats = weaver.topology_stats(vertices, divided)
        self.assertEqual((stats["vertex_count"], stats["triangle_count"]), (5, 6))
        self.assertAlmostEqual(stats["volume_m3"], 1/6)

    def test_touching_closed_shells_split_and_local_winding_can_be_measured(self):
        vertices = TETRA_VERTICES + [(x, -y, -z) for x, y, z in TETRA_VERTICES]
        triangles = TETRA_TRIANGLES + [tuple(i+4 for i in t) for t in TETRA_TRIANGLES]
        # Simulate one incorrectly wound exported face. Only measurement flips it.
        triangles[1] = tuple(reversed(triangles[1]))
        before = list(triangles)
        stats = weaver.topology_stats(vertices, triangles)
        self.assertEqual(stats["global_nonmanifold_edges"], 1)
        self.assertEqual(stats["shell_count"], 2)
        self.assertTrue(stats["volume_valid"])
        self.assertGreater(stats["measurement_flipped_triangles"], 0)
        self.assertAlmostEqual(stats["volume_m3"], 1/3)
        self.assertEqual(triangles, before)

    def test_open_and_degenerate_meshes_have_no_manufacturing_volume(self):
        for triangles in (TETRA_TRIANGLES[:-1], TETRA_TRIANGLES + [(0, 0, 1)]):
            stats = weaver.topology_stats(TETRA_VERTICES, triangles)
            self.assertFalse(stats["volume_valid"])
            self.assertIsNone(stats["volume_m3"])

    def test_glb_scene_instances_include_parent_transform_and_mirror_scale(self):
        positions = b"".join(struct.pack("<3f", *v) for v in TETRA_VERTICES)
        indices = b"".join(struct.pack("<3H", *v) for v in TETRA_TRIANGLES)
        document = {
            "scene": 0, "scenes": [{"nodes": [0]}],
            "nodes": [{"translation": [10, 20, 30], "children": [1, 2]},
                      {"mesh": 0}, {"mesh": 0, "translation": [5, 0, 0], "scale": [-2, 2, 2]}],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0}, "indices": 1}]}],
            "accessors": [{"bufferView": 0, "componentType": 5126, "count": 4, "type": "VEC3"},
                          {"bufferView": 1, "componentType": 5123, "count": 12, "type": "SCALAR"}],
            "bufferViews": [{"byteOffset": 0, "byteLength": len(positions)},
                            {"byteOffset": len(positions), "byteLength": len(indices)}],
        }
        source = weaver.flatten_glb(document, positions+indices)
        stats = weaver.topology_stats(source["positions"], source["triangles"], source["weld_groups"])
        self.assertEqual(source["positions"][0], (10, 20, 30))
        self.assertEqual((len(source["instances"]), stats["vertex_count"], stats["triangle_count"]), (2, 8, 8))
        self.assertAlmostEqual(stats["volume_m3"], 1.5)
        # Coincident distinct instances still each contribute vertices and volume.
        document["nodes"][2] = {"mesh": 0}
        source = weaver.flatten_glb(document, positions+indices)
        stats = weaver.topology_stats(source["positions"], source["triangles"], source["weld_groups"])
        self.assertEqual(stats["vertex_count"], 8)
        self.assertAlmostEqual(stats["volume_m3"], 1/3)


class CacheContract(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(prefix="rf-blueprints-")
        self.root = Path(self.temp.name)
        self.document = json.loads((weaver.ROOT / weaver.JSON_PATH).read_text(encoding="utf-8-sig"))
        self.header = (weaver.ROOT / weaver.HEADER_PATH).read_text(encoding="utf-8-sig")
        self.dependencies = ["rasterfall/src/rasterfall_calibration.c", "rasterfall/include/toy_game.h",
                             "rasterfall/include/rasterfall_units.h", "rasterfall/include/toy_mesh_weaver.h"]
        self.dependencies.extend(row["runtime_path"] for row in self.document["blueprints"])
        for relative in self.dependencies:
            destination = self.root / relative
            destination.parent.mkdir(parents=True, exist_ok=True)
            shutil.copyfile(weaver.ROOT / relative, destination)

    def tearDown(self):
        self.temp.cleanup()

    def test_public_checkout_checks_runtime_and_adapter_without_private_source(self):
        self.assertEqual(weaver.check(self.root, self.document, self.header), 0)
        row = self.document["blueprints"][0]
        source = self.root / row["source_path"]
        source.parent.mkdir(parents=True, exist_ok=True)
        source.write_bytes(b"changed source")
        with self.assertRaisesRegex(weaver.BlueprintError, "source asset changed"):
            weaver.check(self.root, self.document, self.header)

    def test_runtime_and_adapter_changes_invalidate_cache(self):
        mesh = self.root / self.document["blueprints"][0]["runtime_path"]
        data = mesh.read_bytes()
        mesh.write_bytes(data + b"changed mesh")
        with self.assertRaisesRegex(weaver.BlueprintError, "runtime mesh/texture changed"):
            weaver.check(self.root, self.document, self.header)
        mesh.write_bytes(data)
        calibration = self.root / self.dependencies[0]
        text = calibration.read_text(encoding="utf-8-sig")
        self.assertIn(".length_mm = 880;", text)
        calibration.write_bytes(text.replace(".length_mm = 880;", ".length_mm = 881;", 1).encode("utf-8"))
        with self.assertRaisesRegex(weaver.BlueprintError, "physical adapter changed"):
            weaver.check(self.root, self.document, self.header)

    def test_machine_orientation_and_generated_header_are_checked(self):
        with self.assertRaisesRegex(weaver.BlueprintError, "C blueprint header is stale"):
            weaver.check(self.root, self.document, self.header + "/* stale */")
        path = self.root / "rasterfall/include/toy_mesh_weaver.h"
        text = path.read_text(encoding="utf-8-sig")
        path.write_bytes(text.replace("TOY_WEAVER_PRODUCT_YAW_DEG 70", "TOY_WEAVER_PRODUCT_YAW_DEG 71").encode("utf-8"))
        with self.assertRaisesRegex(weaver.BlueprintError, "machine envelope/orientation changed"):
            weaver.check(self.root, self.document, self.header)


if __name__ == "__main__":
    unittest.main()
