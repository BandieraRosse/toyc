"""Public, independently authored GLB probes for capability inventory boundaries."""

import json
import struct
import tempfile
import unittest
from pathlib import Path

from rfchar_audit import audit


class AuditTests(unittest.TestCase):
    def probe(self, *, weights=(1, 0, 0, 0), count=3, material=None,
              morph=False, animation=False, instances=1):
        # Minimal inventory input, deliberately not a complete RFCHAR skeleton.
        # The audit must never label this a validated or renderable character.
        document = {
            "asset": {"version": "2.0"},
            "nodes": [{"mesh": 0} for _ in range(instances)],
            "meshes": [{"primitives": [{"attributes": {"POSITION": 0, "WEIGHTS_0": 2},
                                         "indices": 1, "material": 0}]}],
            "accessors": [{"count": 1}, {"count": count},
                          {"count": 1, "type": "VEC4", "componentType": 5126,
                           "bufferView": 0}],
            "bufferViews": [{"buffer": 0, "byteLength": 16}],
            "buffers": [{"byteLength": 16}],
            "materials": [material or {}],
        }
        if morph:
            document["meshes"][0]["primitives"][0]["targets"] = [{"POSITION": 0}]
        if animation:
            document["animations"] = [{}]
        text = json.dumps(document).encode("utf-8")
        text += b" " * (-len(text) % 4)
        binary = struct.pack("<4f", *weights)
        raw = (struct.pack("<4sII", b"glTF", 2, 28 + len(text) + len(binary)) +
               struct.pack("<II", len(text), 0x4E4F534A) + text +
               struct.pack("<II", len(binary), 0x004E4942) + binary)
        with tempfile.TemporaryDirectory() as root:
            path = Path(root) / "probe.glb"
            path.write_bytes(raw)
            return audit(path)

    def test_flat_probe_is_only_an_inventory(self):
        result = self.probe(weights=(0.5, 0.5, 0, 0))
        self.assertEqual(result["current_pipeline_gaps"], [])
        self.assertEqual(result["max_influences_0"], 2)
        self.assertEqual(result["scope"], "inventory_only_not_contract_or_visual_acceptance")
        self.assertEqual(result, self.probe(weights=(0.5, 0.5, 0, 0)))

    def test_material_semantics_lost_by_current_pipeline(self):
        result = self.probe(material={"alphaMode": "MASK", "doubleSided": True,
                                     "pbrMetallicRoughness": {"baseColorTexture": {"index": 0}}})
        self.assertEqual(result["current_pipeline_gaps"], ["MASK_IMPORT_REJECTED",
                         "OPAQUE_SURFACE_PROFILE_REQUIRED"])

    def test_deformation_and_animation_are_not_silently_accepted(self):
        result = self.probe(weights=(0.4, 0.3, 0.2, 0.1), morph=True, animation=True)
        self.assertEqual(result["current_pipeline_gaps"], ["GLB_ANIMATION_NOT_IMPORTED",
                         "MORPH_UNSUPPORTED", "SKIN_GT2_UNSUPPORTED"])

    def test_capacity_counts_mesh_node_occurrences(self):
        self.assertNotIn("SCENE_INDEX_CAPACITY", self.probe(count=65535)["current_pipeline_gaps"])
        self.assertNotIn("SCENE_INDEX_CAPACITY", self.probe(count=32769, instances=2)["current_pipeline_gaps"])
        self.assertIn("SCENE_INDEX_CAPACITY", self.probe(count=65535 * 8 + 3, instances=2)["current_pipeline_gaps"])


if __name__ == "__main__":
    unittest.main()
