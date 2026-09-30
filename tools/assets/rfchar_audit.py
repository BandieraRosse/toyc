#!/usr/bin/env python3
"""Read-only inventory of RFCHAR GLB requirements; not a contract validator."""

import argparse
import hashlib
import json
import struct
from pathlib import Path

from import_asset import ImportFailure, glb_document
from rfchar_import import accessor


def audit(path):
    document, binary = glb_document(path)
    meshes = document.get("meshes", [])
    vertices = indices = max_influences = morph_targets = 0
    issues = set()
    used_materials = set()
    # Count mesh-node occurrences, as the current importer does, rather than
    # assuming every mesh in the file is instantiated exactly once.
    for node in document.get("nodes", []):
        if "mesh" not in node:
            continue
        for primitive in meshes[node["mesh"]]["primitives"]:
            attributes = primitive["attributes"]
            vertices += document["accessors"][attributes["POSITION"]]["count"]
            indices += document["accessors"][primitive["indices"]]["count"]
            if "material" in primitive:
                used_materials.add(primitive["material"])
            targets = len(primitive.get("targets", []))
            morph_targets = max(morph_targets, targets)
            if targets:
                issues.add("MORPH_UNSUPPORTED")
            if "WEIGHTS_1" in attributes or "JOINTS_1" in attributes:
                issues.add("SECOND_SKIN_SET_UNSUPPORTED")
            if "WEIGHTS_0" in attributes:
                weights_index = attributes["WEIGHTS_0"]
                if "sparse" in document["accessors"][weights_index]:
                    raise ValueError("sparse weights require the GLB contract validator")
                for weights in accessor(document, binary, weights_index):
                    max_influences = max(max_influences, sum(w > 1e-6 for w in weights))
    if max_influences > 2:
        issues.add("SKIN_GT2_UNSUPPORTED")
    # Scene retains triangle boundaries across at most 16 expanded chunks.
    if indices > 65535 * 16:
        issues.add("SCENE_INDEX_CAPACITY")
    if document.get("animations"):
        issues.add("GLB_ANIMATION_NOT_IMPORTED")
    materials = []
    for index, material in enumerate(document.get("materials", [])):
        pbr = material.get("pbrMetallicRoughness", {})
        alpha_mode = material.get("alphaMode", "OPAQUE")
        double_sided = material.get("doubleSided", False)
        textured = "baseColorTexture" in pbr
        materials.append({"index": index, "name": material.get("name", ""),
                          "used": index in used_materials, "alpha_mode": alpha_mode,
                          "double_sided": double_sided, "base_color_texture": textured})
        if index not in used_materials:
            continue
        if alpha_mode == "MASK":
            issues.add("MASK_IMPORT_REJECTED")
        elif alpha_mode != "OPAQUE":
            issues.add("ALPHA_MODE_UNSUPPORTED")
        if textured:
            issues.add("SCENE_CHARACTER_TEXTURE_UNSUPPORTED")
        if "rf_material" in material.get("extras", {}):
            issues.add("RF_MATERIAL_IMPORT_REJECTED")
    return {
        "audit_schema": 1,
        "source_sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
        "scope": "inventory_only_not_contract_or_visual_acceptance",
        "vertices": vertices, "indices": indices,
        "skin_joint_counts": [len(s["joints"]) for s in document.get("skins", [])],
        "max_influences_0": max_influences, "max_morph_targets": morph_targets,
        "images": len(document.get("images", [])),
        "textures": len(document.get("textures", [])),
        "animations": len(document.get("animations", [])),
        "materials": materials,
        "current_pipeline_gaps": sorted(issues),
    }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", type=Path)
    args = parser.parse_args()
    try:
        report = audit(args.input)
    except (ImportFailure, OSError, ValueError, KeyError, IndexError, TypeError,
            struct.error, ZeroDivisionError) as error:
        parser.exit(1, f"rfchar-audit: cannot inventory input: {error}\n")
    print(json.dumps(report, ensure_ascii=True, indent=2, sort_keys=True))


if __name__ == "__main__":
    main()
