"""Independent metadata probes for the proposed RFCHAR material contract.

These documents intentionally contain no mesh, UV payload, or image bytes. Those
belong to the package importer and rendering acceptance checks.
"""

import copy
import unittest

from rfchar_material_contract import MaterialError, normalize_materials


def material(identity="body"):
    return {"extras": {"rf_material": {"version": 1, "id": identity}}}


def document(*materials):
    return {"materials": list(materials or (material(),))}


class MaterialContractTests(unittest.TestCase):
    def reject(self, source, code):
        with self.assertRaises(MaterialError) as caught:
            normalize_materials(source)
        self.assertIn("RFCHAR PACKAGE ERROR " + code + ":", str(caught.exception))

    def test_defaults_are_canonical_deterministic_and_do_not_mutate_input(self):
        source = document(material("body"), material("hair"))
        original = copy.deepcopy(source)
        first = normalize_materials(source)
        self.assertEqual(first, normalize_materials(source))
        self.assertEqual(source, original)
        self.assertEqual(first["scope"], "material_metadata_only_not_import_or_visual_acceptance")
        self.assertEqual(first["requires"], ["rfchar.material.v1"])
        self.assertEqual(first["materials"][0], {
            "id": "body", "version": 1,
            "base_color_factor": [1.0, 1.0, 1.0, 1.0],
            "alpha_mode": "OPAQUE", "alpha_cutoff": 0.5,
            "double_sided": False, "base_color_texture": None, "toon": None,
            "outline": {"width_px": 0.0, "color": [0.0, 0.0, 0.0]},
            "face_light": {"role": "RF_HEAD", "normal": [0.0, 0.0, 1.0],
                           "strength": 0.0},
        })

    def test_identity_and_version_fail_closed(self):
        self.reject(document(material("body"), material("body")), "ID_DUPLICATE")
        for identity in ("", "Body", "bad-name", "1body", True):
            with self.subTest(identity=identity):
                self.reject(document(material(identity)), "FIELD_INVALID")
        for version in (0, 2, True, "1"):
            with self.subTest(version=version):
                source = document()
                source["materials"][0]["extras"]["rf_material"]["version"] = version
                self.reject(source, "VERSION_UNSUPPORTED")
        for materials in ([], None):
            self.reject({"materials": materials}, "FIELD_INVALID")

    def test_unknown_rf_and_nested_map_fields_are_rejected(self):
        source = document()
        source["materials"][0]["unexpected"] = 1
        self.reject(source, "FIELD_INVALID")
        for owner, value in (("rf_material", {"hair_highlight": {}}),
                             ("toon", {"unexpected": 1}),
                             ("outline", {"unexpected": 1}),
                             ("face_light", {"unexpected": 1})):
            with self.subTest(owner=owner):
                source = document()
                rf = source["materials"][0]["extras"]["rf_material"]
                if owner == "rf_material":
                    rf.update(value)
                else:
                    rf[owner] = value
                self.reject(source, "FIELD_INVALID")
        source = document()
        source["materials"][0]["pbrMetallicRoughness"] = {"metallicRoughnessTexture": {"index": 0}}
        self.reject(source, "FIELD_INVALID")

    def test_numeric_factors_vectors_and_booleans(self):
        cases = (
            ("baseColorFactor", [1, 0, 0]),
            ("baseColorFactor", [1, 0, 0, 1.1]),
            ("baseColorFactor", [1, 0, 0, float("nan")]),
            ("metallicFactor", True),
            ("roughnessFactor", float("inf")),
        )
        for name, value in cases:
            with self.subTest(name=name, value=value):
                source = document()
                source["materials"][0]["pbrMetallicRoughness"] = {name: value}
                self.reject(source, "FIELD_INVALID")
        for name, value in (("alphaCutoff", -0.01), ("alphaCutoff", float("inf")),
                            ("doubleSided", 1), ("emissiveFactor", [0, 0, True])):
            with self.subTest(name=name, value=value):
                source = document()
                source["materials"][0][name] = value
                self.reject(source, "FIELD_INVALID")
        source = document()
        source["materials"][0]["emissiveFactor"] = [0, 0, 0.01]
        self.reject(source, "CAPABILITY_MISSING")

    def test_toon_outline_and_face_ranges(self):
        cases = (
            ("toon", "threshold", -0.01), ("toon", "softness", 0),
            ("toon", "softness", float("inf")),
            ("toon", "shade_color", [1, 1, 1.01]),
            ("outline", "width_px", 8.01),
            ("outline", "color", [0, 0, float("nan")]),
            ("face_light", "strength", True),
            ("face_light", "role", "WORLD"),
            ("face_light", "normal", [0, 0, 0]),
            ("face_light", "normal", [0, 0, 0.5]),
            ("face_light", "normal", [0, 0, 1.01]),
        )
        for owner, name, value in cases:
            with self.subTest(owner=owner, name=name, value=value):
                source = document()
                source["materials"][0]["extras"]["rf_material"][owner] = {name: value}
                self.reject(source, "FIELD_INVALID")
        source = document()
        source["materials"][0]["extras"]["rf_material"].update({
            "toon": {"threshold": 1, "softness": 1, "shade_color": [0, 0.5, 1]},
            "outline": {"width_px": 8, "color": [1, 0, 0]},
            "face_light": {"role": "RF_HEAD", "normal": [0, 1, 0], "strength": 1},
        })
        normalized = normalize_materials(source)["materials"][0]
        self.assertEqual(normalized["outline"]["width_px"], 8)
        self.assertEqual(normalized["face_light"]["normal"], [0, 1, 0])

    def test_alpha_modes_and_capabilities(self):
        source = document()
        source["materials"][0].update({"alphaMode": "MASK", "alphaCutoff": 0.3,
                                        "doubleSided": True})
        report = normalize_materials(source)
        self.assertEqual(report["materials"][0]["alpha_cutoff"], 0.3)
        self.assertEqual(report["requires"], ["rfchar.alpha_mask.v1",
                                              "rfchar.double_sided.v1", "rfchar.material.v1"])
        source["materials"][0]["alphaMode"] = "BLEND"
        self.reject(source, "CAPABILITY_MISSING")

    def test_texture_image_refs_and_jpeg_mask(self):
        source = document()
        source["materials"][0]["pbrMetallicRoughness"] = {"baseColorTexture": {"index": 0}}
        source["textures"] = [{"source": 0}]
        source["images"] = [{"mimeType": "image/png"}]
        report = normalize_materials(source)
        self.assertEqual(report["materials"][0]["base_color_texture"], {
            "index": 0, "image": 0, "texcoord": 0, "color_space": "srgb",
            "alpha_space": "linear", "sampler": {"wrapS": 33071, "wrapT": 33071,
                                                 "magFilter": 9729, "minFilter": 9987},
        })
        self.assertIn("rfchar.texture_rgba_mip.v1", report["requires"])
        for mutation in (lambda d: d["textures"].clear(),
                         lambda d: d["textures"][0].update(source=1),
                         lambda d: d["images"][0].update(mimeType="image/webp"),
                         lambda d: d["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"].update(texCoord=1),
                         lambda d: d["materials"][0]["pbrMetallicRoughness"]["baseColorTexture"].update(index=True)):
            with self.subTest(mutation=mutation):
                invalid = copy.deepcopy(source)
                mutation(invalid)
                self.reject(invalid, "FIELD_INVALID")
        for declaration in ({"mimeType": "image/jpeg"}, {"uri": "face.jpg"},
                            {"uri": "data:image/jpeg;base64,AA=="}):
            with self.subTest(declaration=declaration):
                masked = copy.deepcopy(source)
                masked["images"] = [declaration]
                masked["materials"][0]["alphaMode"] = "MASK"
                self.reject(masked, "FIELD_INVALID")

    def test_explicit_sampler_must_match_rf_profile(self):
        source = document()
        source["materials"][0]["pbrMetallicRoughness"] = {"baseColorTexture": {"index": 0}}
        source["textures"] = [{"source": 0, "sampler": 0}]
        source["images"] = [{"mimeType": "image/png"}]
        source["samplers"] = [{}]
        self.reject(source, "FIELD_INVALID")  # glTF's implicit REPEAT is not RF clamp.
        source["samplers"] = [{"wrapS": 33071, "wrapT": 33071,
                                "magFilter": 9729, "minFilter": 9987}]
        self.assertIsNotNone(normalize_materials(source)["materials"][0]["base_color_texture"])
        source["samplers"][0]["minFilter"] = 9729
        self.reject(source, "FIELD_INVALID")

    def test_unsupported_extensions_and_material_inputs(self):
        for name, value in (("extensions", {"KHR_materials_unlit": {}}),
                            ("normalTexture", {"index": 0}),
                            ("occlusionTexture", {"index": 0}),
                            ("emissiveTexture", {"index": 0})):
            with self.subTest(name=name):
                source = document()
                source["materials"][0][name] = value
                self.reject(source, "CAPABILITY_MISSING")
        source = document()
        source["materials"][0]["pbrMetallicRoughness"] = {"baseColorTexture": {"index": 0}}
        source["textures"] = [{"source": 0, "extensions": {"KHR_texture_basisu": {}}}]
        source["images"] = [{"mimeType": "image/png"}]
        self.reject(source, "CAPABILITY_MISSING")


if __name__ == "__main__":
    unittest.main()
