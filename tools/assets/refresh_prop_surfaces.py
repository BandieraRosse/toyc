#!/usr/bin/env python3
"""Refresh authored kit PBR in existing GLBs and reimport without geometry changes.

Generate missing private GLBs with the kit's Blender generator first. Uses the
normal manifest importer and existing texture conversions; preserves geometry,
UVs, indices, primitive layout and texture payloads of installed assets.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

from import_asset import install_atomically, read_manifest
from prop_surface_profiles import ASSET_BINDINGS, SURFACES, linear_rgb

ROOT = Path(__file__).resolve().parents[2]


def refreshed_glb(raw, bindings):
    if raw[:4] != b"glTF" or struct.unpack_from("<II", raw, 4) != (2, len(raw)):
        raise ValueError("invalid GLB header")
    size, kind = struct.unpack_from("<II", raw, 12)
    if kind != 0x4E4F534A:
        raise ValueError("first GLB chunk must be JSON")
    doc = json.loads(raw[20:20 + size].decode("utf-8"))
    profiles = []
    for mat in doc["materials"]:
        name = mat["name"]
        if name not in bindings:
            raise ValueError("unmapped material: " + name)
        surface = bindings[name]
        metallic, roughness, color = SURFACES[surface]
        pbr = mat.setdefault("pbrMetallicRoughness", {})
        pbr.update(metallicFactor=metallic, roughnessFactor=roughness)
        if color is not None:
            pbr["baseColorFactor"] = [*linear_rgb(color),
                                      pbr.get("baseColorFactor", [1, 1, 1, 1])[3]]
        profiles.append(dict(name=name, surface=surface, metallic=metallic,
                             roughness=roughness, color_srgb=color))
    encoded = json.dumps(doc, separators=(",", ":")).encode("utf-8")
    encoded += b" " * (-len(encoded) % 4)
    chunks = struct.pack("<II", len(encoded), kind) + encoded + raw[20 + size:]
    return struct.pack("<4sII", b"glTF", 2, 12 + len(chunks)) + chunks, profiles


def geometry_bytes(raw):
    if raw[:8] != b"RFM2\x02\0\0\0":
        raise ValueError("requires static RFM2 v2")
    count = struct.unpack_from("<I", raw, 48)[0]
    offset = struct.unpack_from("<I", raw, 56)[0]
    masked = bytearray(raw)
    for i in range(count):
        masked[offset + i * 16:offset + i * 16 + 8] = bytes(8)
    return masked


def textures(path):
    return {p.relative_to(path).as_posix(): p.read_bytes() for p in path.rglob("*")
            if p.is_file()} if path.is_dir() else {}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--assets", nargs="+", choices=sorted(ASSET_BINDINGS))
    parser.add_argument("--tool-dir", type=Path, required=True)
    parser.add_argument("--report", type=Path, required=True)
    args = parser.parse_args()
    names = args.assets or sorted(ASSET_BINDINGS)
    prepared = []
    for name in names:
        manifests = list((ROOT / "tools/assets/manifests/props").rglob(name + ".asset.json"))
        if len(manifests) != 1:
            raise ValueError("expected one manifest: " + name)
        manifest = manifests[0]
        source = (manifest.parent / read_manifest(manifest)["source"]).resolve()
        original = source.read_bytes()
        updated, profiles = refreshed_glb(original, ASSET_BINDINGS[name])
        output = ROOT / "rasterfall/assets/models/props" / manifest.parent.name
        prepared.append((name, manifest, source, original, updated, profiles, output))
    report = []
    for name, manifest, source, original, updated, profiles, output in prepared:
        source.write_bytes(updated)
        try:
            with tempfile.TemporaryDirectory(prefix=".surface-", dir=output) as temporary:
                stage = Path(temporary)
                subprocess.run([sys.executable, str(ROOT / "tools/assets/import_asset.py"),
                                str(manifest), "--no-build", "--tool-dir", str(args.tool_dir.resolve()),
                                "--output-root", str(stage)], check=True)
                installed = output / (name + ".rmesh")
                mesh = stage / installed.name
                if geometry_bytes(installed.read_bytes()) != geometry_bytes(mesh.read_bytes()):
                    raise ValueError("geometry/layout changed: " + name)
                texture_name = name + ".textures"
                if textures(output / texture_name) != textures(stage / texture_name):
                    raise ValueError("texture payloads changed: " + name)
                install_atomically([mesh, stage / texture_name], output, name, True)
        except BaseException:
            source.write_bytes(original)
            raise
        report.append(dict(asset=name, materials=profiles, geometry_unchanged=True,
                           texture_payloads_unchanged=True))
        print("SURFACE PASS", name, flush=True)
    args.report.parent.mkdir(parents=True, exist_ok=True)
    args.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print("SURFACE SET PASS assets=%d" % len(report))


if __name__ == "__main__":
    main()
