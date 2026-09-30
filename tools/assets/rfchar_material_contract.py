#!/usr/bin/env python3
"""Validate proposed RF material metadata; does not import or certify a GLB."""

import argparse
import json
import math
import re
from pathlib import Path

from import_asset import ImportFailure, glb_document


class MaterialError(ValueError):
    def __init__(self, code, field, reason):
        super().__init__(f"RFCHAR PACKAGE ERROR {code}: {field}: {reason}")


def fail(field, reason, code="FIELD_INVALID"):
    raise MaterialError(code, field, reason)


def obj(value, field, allowed=None):
    if not isinstance(value, dict):
        fail(field, "expected object")
    if allowed is not None and set(value) - set(allowed):
        fail(field, "unknown fields: " + ", ".join(sorted(set(value) - set(allowed))))
    return value


def number(value, field, low=0, high=1, exclusive_low=False):
    if (type(value) not in (int, float) or not math.isfinite(value)
            or value < low or value > high or (exclusive_low and value == low)):
        fail(field, f"expected finite number in {'(' if exclusive_low else '['}{low},{high}]")
    return float(value)


def vector(value, field, size, low=0, high=1):
    if not isinstance(value, list) or len(value) != size:
        fail(field, f"expected {size} components")
    return [number(v, f"{field}/{i}", low, high) for i, v in enumerate(value)]


def reference(items, index, field):
    if not isinstance(items, list) or type(index) is not int or not 0 <= index < len(items):
        fail(field, "invalid index")
    return obj(items[index], field)


def texture_binding(document, value, field, mask):
    value = obj(value, field, {"index", "texCoord", "extras"})
    if type(value.get("texCoord", 0)) is not int or value.get("texCoord", 0) != 0:
        fail(field + "/texCoord", "only UV0 is supported")
    texture = reference(document.get("textures", []), value.get("index"), field + "/index")
    if texture.get("extensions"):
        fail(field, "texture extensions are not supported", "CAPABILITY_MISSING")
    image = reference(document.get("images", []), texture.get("source"), field + "/source")
    mime = image.get("mimeType")
    uri = image.get("uri", "")
    if mime is None and isinstance(uri, str):
        if uri.startswith("data:"):
            mime = uri[5:].split(";", 1)[0]
        else:
            mime = {".png": "image/png", ".jpg": "image/jpeg", ".jpeg": "image/jpeg"}.get(Path(uri).suffix.lower())
    if mime not in ("image/png", "image/jpeg"):
        fail(field + "/source", "expected PNG or JPEG declaration")
    if mask and mime != "image/png":
        fail(field + "/source", "MASK requires PNG to preserve alpha")
    profile = {"wrapS": 33071, "wrapT": 33071, "magFilter": 9729, "minFilter": 9987}
    if "sampler" in texture:
        sampler = reference(document.get("samplers", []), texture["sampler"], field + "/sampler")
        # An explicitly declared glTF sampler has repeat wrapping by default.
        defaults = {"wrapS": 10497, "wrapT": 10497, "magFilter": None, "minFilter": None}
        for name, expected in profile.items():
            actual = sampler.get(name, defaults[name])
            if type(actual) is not int or actual != expected:
                fail(field + "/sampler/" + name, f"RF profile requires {expected}")
        if sampler.get("extensions"):
            fail(field + "/sampler", "sampler extensions unsupported", "CAPABILITY_MISSING")
    return {"index": value["index"], "image": texture["source"], "texcoord": 0,
            "color_space": "srgb", "alpha_space": "linear", "sampler": profile}


def normalize_materials(document):
    """Return canonical metadata and required material capabilities without mutation.

    Geometry, UV payloads, image bytes, paths, skin and package dependencies are
    intentionally outside this validator. No runtime binary is produced.
    """
    obj(document, "document")
    materials = document.get("materials")
    if not isinstance(materials, list) or not materials:
        fail("materials", "expected nonempty explicit material table")
    normalized, ids = [], set()
    capabilities = {"rfchar.material.v1"}
    for index, source in enumerate(materials):
        field = f"materials/{index}"
        source = obj(source, field, {"name", "extras", "extensions", "pbrMetallicRoughness",
                                     "alphaMode", "alphaCutoff", "doubleSided", "normalTexture",
                                     "occlusionTexture", "emissiveTexture", "emissiveFactor"})
        extras = obj(source.get("extras", {}), field + "/extras")
        rf = obj(extras.get("rf_material"), field + "/extras/rf_material",
                 {"version", "id", "toon", "outline", "face_light"})
        if type(rf.get("version")) is not int or rf["version"] != 1:
            fail(field + "/version", "expected version 1", "VERSION_UNSUPPORTED")
        identity = rf.get("id")
        if not isinstance(identity, str) or not re.fullmatch(r"[a-z][a-z0-9_]*", identity):
            fail(field + "/id", "expected lowercase asset ID")
        if identity in ids:
            fail(field + "/id", identity, "ID_DUPLICATE")
        ids.add(identity)
        if source.get("extensions"):
            fail(field + "/extensions", "material extensions unsupported", "CAPABILITY_MISSING")
        for name in ("normalTexture", "occlusionTexture", "emissiveTexture"):
            if name in source:
                fail(field + "/" + name, "unsupported material input", "CAPABILITY_MISSING")
        emission = vector(source.get("emissiveFactor", [0, 0, 0]), field + "/emissiveFactor", 3)
        if any(emission):
            fail(field + "/emissiveFactor", "emission unsupported", "CAPABILITY_MISSING")
        pbr = obj(source.get("pbrMetallicRoughness", {}), field + "/pbrMetallicRoughness",
                  {"baseColorFactor", "baseColorTexture", "metallicFactor", "roughnessFactor", "extras"})
        # RF lighting replaces metallic/roughness scalar lighting. Validate their
        # glTF domain, but preserve neither as an RF shading control.
        for name in ("metallicFactor", "roughnessFactor"):
            number(pbr.get(name, 1), field + "/" + name)
        mode = source.get("alphaMode", "OPAQUE")
        if mode not in ("OPAQUE", "MASK"):
            fail(field + "/alphaMode", "only OPAQUE/MASK supported", "CAPABILITY_MISSING")
        sided = source.get("doubleSided", False)
        if type(sided) is not bool:
            fail(field + "/doubleSided", "expected boolean")
        result = {"id": identity, "version": 1,
                  "base_color_factor": vector(pbr.get("baseColorFactor", [1, 1, 1, 1]), field + "/baseColorFactor", 4),
                  "alpha_mode": mode,
                  "alpha_cutoff": number(source.get("alphaCutoff", 0.5), field + "/alphaCutoff"),
                  "double_sided": sided, "base_color_texture": None,
                  "toon": None, "outline": {"width_px": 0.0, "color": [0.0, 0.0, 0.0]},
                  "face_light": {"role": "RF_HEAD", "normal": [0.0, 0.0, 1.0], "strength": 0.0}}
        if mode == "MASK":
            capabilities.add("rfchar.alpha_mask.v1")
        if sided:
            capabilities.add("rfchar.double_sided.v1")
        if "baseColorTexture" in pbr:
            result["base_color_texture"] = texture_binding(document, pbr["baseColorTexture"], field + "/baseColorTexture", mode == "MASK")
            capabilities.add("rfchar.texture_rgba_mip.v1")
        if "toon" in rf:
            toon = obj(rf["toon"], field + "/toon", {"threshold", "softness", "shade_color"})
            result["toon"] = {"threshold": number(toon.get("threshold", 0.5), field + "/toon/threshold"),
                              "softness": number(toon.get("softness", 0.05), field + "/toon/softness", exclusive_low=True),
                              "shade_color": vector(toon.get("shade_color", [0.65]*3), field + "/toon/shade_color", 3)}
        if "outline" in rf:
            outline = obj(rf["outline"], field + "/outline", {"width_px", "color"})
            result["outline"] = {"width_px": number(outline.get("width_px", 0), field + "/outline/width_px", high=8),
                                 "color": vector(outline.get("color", [0]*3), field + "/outline/color", 3)}
        if "face_light" in rf:
            face = obj(rf["face_light"], field + "/face_light", {"role", "normal", "strength"})
            if face.get("role", "RF_HEAD") != "RF_HEAD":
                fail(field + "/face_light/role", "only RF_HEAD supported")
            normal = vector(face.get("normal", [0, 0, 1]), field + "/face_light/normal", 3, -1, 1)
            if abs(sum(v*v for v in normal) - 1) > 1e-5:
                fail(field + "/face_light/normal", "expected unit vector (squared-length tolerance 1e-5)")
            result["face_light"] = {"role": "RF_HEAD", "normal": normal,
                                    "strength": number(face.get("strength", 0), field + "/face_light/strength")}
        normalized.append(result)
    return {"scope": "material_metadata_only_not_import_or_visual_acceptance",
            "materials": normalized, "requires": sorted(capabilities)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    try:
        document, _ = glb_document(args.input)
        report = normalize_materials(document)
    except (ImportFailure, OSError, ValueError) as error:
        parser.exit(1, f"{error}\n")
    print(json.dumps(report, sort_keys=True, indent=2, allow_nan=False))


if __name__ == "__main__":
    main()
