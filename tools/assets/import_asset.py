#!/usr/bin/env python3
"""Rasterfall's unified offline RMESH/TTEX asset importer."""

import argparse
import base64
import json
import os
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path

SCHEMA_VERSION = 1
ASSET_ID = re.compile(r"^[a-z][a-z0-9_]*$")
ASSET_TYPES = {"static_prop", "character", "weapon", "rigid_attachment"}
IMAGE_FORMATS = {".png": "png", ".jpg": "jpg", ".jpeg": "jpg", ".bmp": "bmp",
                 ".spa": "bmp", ".sph": "bmp"}


class ImportFailure(Exception):
    pass


def read_manifest(path):
    try:
        value = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, UnicodeError, json.JSONDecodeError) as error:
        raise ImportFailure("cannot read manifest %s: %s" % (path, error))
    if not isinstance(value, dict) or value.get("schema") != SCHEMA_VERSION:
        raise ImportFailure("manifest schema must be %d" % SCHEMA_VERSION)
    asset_id = value.get("id")
    if not isinstance(asset_id, str) or not ASSET_ID.fullmatch(asset_id):
        raise ImportFailure("asset id must match %s" % ASSET_ID.pattern)
    if value.get("type") not in ASSET_TYPES:
        raise ImportFailure("asset type must be one of: %s" % ", ".join(sorted(ASSET_TYPES)))
    source = value.get("source")
    if not isinstance(source, str) or not source:
        raise ImportFailure("manifest source must be a non-empty path")
    unknown = set(value) - {"schema", "id", "type", "source", "lods", "dimensions_m", "attachments",
                            "attachment_space"}
    if unknown:
        raise ImportFailure("unknown manifest fields: %s" % ", ".join(sorted(unknown)))
    if "dimensions_m" in value:
        dimensions = value["dimensions_m"]
        if (value["type"] != "static_prop" or not isinstance(dimensions, list) or
                len(dimensions) != 3 or any(not isinstance(n, (int, float)) or n <= 0 for n in dimensions)):
            raise ImportFailure("dimensions_m is three positive numbers and is only valid for static_prop")
    if "attachments" in value:
        attachments = value["attachments"]
        if (value["type"] != "weapon" or not isinstance(attachments, dict) or
                not attachments or any(not isinstance(name, str) or not name or
                                       not isinstance(node, str) or not node
                                       for name, node in attachments.items())):
            raise ImportFailure("weapon attachments must map semantic names to non-empty node/bone names")
    if value["type"] == "rigid_attachment":
        space = value.get("attachment_space")
        expected = {"origin": "mount_origin", "orientation": "canonical_character",
                    "units": "meters"}
        if space != expected:
            raise ImportFailure("rigid_attachment attachment_space must be %s" % expected)
    elif "attachment_space" in value:
        raise ImportFailure("attachment_space is only valid for rigid_attachment")
    lods = value.get("lods", [])
    if not isinstance(lods, list):
        raise ImportFailure("lods must be an array")
    levels = set()
    for lod in lods:
        if not isinstance(lod, dict) or set(lod) - {"level", "ratio", "aggressive", "preserve_primitives", "primitive_ratios"}:
            raise ImportFailure("each LOD must contain only supported LOD settings")
        level, ratio = lod.get("level"), lod.get("ratio")
        if not isinstance(level, int) or isinstance(level, bool) or level < 1 or level in levels:
            raise ImportFailure("LOD levels must be unique positive integers")
        if not isinstance(ratio, (int, float)) or not 0.01 <= ratio < 1.0:
            raise ImportFailure("LOD ratio must be in [0.01, 1.0)")
        levels.add(level)
        for name in ("preserve_primitives", "primitive_ratios"):
            if name in lod and (not isinstance(lod[name], list) or
                                any(not isinstance(n, (int, float)) or n < 0 for n in lod[name])):
                raise ImportFailure("%s must be an array of non-negative numbers" % name)
        if "preserve_primitives" in lod and "primitive_ratios" in lod:
            raise ImportFailure("an LOD cannot combine preserve_primitives and primitive_ratios")
    return value


def run(command, cwd=None):
    print("+", " ".join(str(part) for part in command))
    try:
        subprocess.run([str(part) for part in command], cwd=cwd, check=True)
    except (OSError, subprocess.CalledProcessError) as error:
        raise ImportFailure("command failed: %s" % error)


def glb_document(path):
    data = path.read_bytes()
    if len(data) < 20 or data[:4] != b"glTF" or struct.unpack_from("<II", data, 4) != (2, len(data)):
        raise ImportFailure("invalid GLB header: %s" % path)
    offset, document, binary = 12, None, None
    while offset + 8 <= len(data):
        length, kind = struct.unpack_from("<II", data, offset)
        offset += 8
        if offset + length > len(data):
            raise ImportFailure("truncated GLB chunk")
        chunk = data[offset:offset + length]
        offset += length
        if kind == 0x4E4F534A and document is None:
            try:
                document = json.loads(chunk.rstrip(b" \0").decode("utf-8"))
            except (UnicodeError, json.JSONDecodeError) as error:
                raise ImportFailure("invalid GLB JSON: %s" % error)
        elif kind == 0x004E4942 and binary is None:
            binary = chunk
    if not isinstance(document, dict) or binary is None:
        raise ImportFailure("GLB must contain JSON and BIN chunks")
    return document, binary


def image_bytes(document, binary, source_path, image):
    mime = image.get("mimeType")
    if "bufferView" in image:
        try:
            view = document["bufferViews"][image["bufferView"]]
            start = view.get("byteOffset", 0)
            payload = binary[start:start + view["byteLength"]]
        except (KeyError, IndexError, TypeError) as error:
            raise ImportFailure("invalid GLB image bufferView: %s" % error)
    elif isinstance(image.get("uri"), str):
        uri = image["uri"]
        if uri.startswith("data:"):
            try:
                header, encoded = uri.split(",", 1)
                if ";base64" not in header:
                    raise ValueError("data URI is not base64")
                mime = mime or header[5:].split(";", 1)[0]
                payload = base64.b64decode(encoded, validate=True)
            except (ValueError, base64.binascii.Error) as error:
                raise ImportFailure("invalid GLB image data URI: %s" % error)
        else:
            if ":" in uri or uri.startswith(("/", "\\")):
                raise ImportFailure("GLB image URI must be relative")
            image_path = (source_path.parent / uri).resolve()
            try:
                image_path.relative_to(source_path.parent.resolve())
                payload = image_path.read_bytes()
            except (ValueError, OSError) as error:
                raise ImportFailure("cannot read GLB image URI %s: %s" % (uri, error))
    else:
        raise ImportFailure("GLB image has neither bufferView nor URI")
    if payload.startswith(b"\x89PNG\r\n\x1a\n") or mime == "image/png":
        return payload, ".png", "png"
    if payload.startswith(b"\xff\xd8") or mime in ("image/jpeg", "image/jpg"):
        return payload, ".jpg", "jpg"
    raise ImportFailure("GLB base color image is not PNG or JPEG")


def extract_glb_textures(source, raw_dir):
    document, binary = glb_document(source)
    textures, images = document.get("textures", []), document.get("images", [])
    used = set()
    for material in document.get("materials", []):
        texture = material.get("pbrMetallicRoughness", {}).get("baseColorTexture", {}).get("index")
        if texture is not None:
            used.add(texture)
    extracted = []
    for index in sorted(used):
        try:
            image = images[textures[index]["source"]]
        except (IndexError, KeyError, TypeError) as error:
            raise ImportFailure("invalid GLB baseColorTexture %s: %s" % (index, error))
        payload, suffix, image_format = image_bytes(document, binary, source, image)
        path = raw_dir / ("texture_%03d%s" % (index, suffix))
        path.write_bytes(payload)
        extracted.append((index, path, image_format))
    return extracted


def rmesh_info(path):
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ImportFailure("cannot read RMESH %s: %s" % (path, error))
    if len(data) < 64 or data[:4] != b"RFM2":
        raise ImportFailure("not an RFM2 mesh: %s" % path)
    version, vertices, indices = struct.unpack_from("<III", data, 4)
    primitives, materials, primitive_at, material_at = struct.unpack_from("<IIII", data, 44)
    if not 2 <= version <= 14 or not vertices or not indices or indices % 3:
        raise ImportFailure("invalid RFM2 counts/version: %s" % path)
    material_bytes = 40 if version >= 9 else 24 if version >= 8 else 16
    vertex_bytes = 36 if version >= 10 else 32 if version >= 6 else 24
    vertex_at = material_at + materials * material_bytes
    index_at = vertex_at + vertices * vertex_bytes
    end = index_at + indices * 4
    if primitive_at != 64 or material_at != 64 + primitives * 16 or end > len(data):
        raise ImportFailure("invalid or truncated RFM2 layout: %s" % path)
    texture_indices = set()
    for index in range(materials):
        at = material_at + index * material_bytes
        texture = struct.unpack_from("<I", data, at + 8)[0]
        if texture != 0xFFFFFFFF:
            texture_indices.add(texture)
        if version >= 5 and data[at + 6] == 1:
            texture_indices.add(data[at + 5])
        if version >= 8:
            sphere = struct.unpack_from("<I", data, at + 12)[0]
            if (sphere >> 16) & 3:
                texture_indices.add(sphere & 0xffff)
    return {"version": version, "textures": sorted(texture_indices)}


def validate_ttex(path):
    try:
        data = path.read_bytes()
    except OSError as error:
        raise ImportFailure("cannot read TTEX %s: %s" % (path, error))
    if (len(data) < 32 or data[:4] != b"TTEX" or
            struct.unpack_from("<H", data, 4)[0] != 1 or
            struct.unpack_from("<H", data, 16)[0] not in (3, 4) or
            struct.unpack_from("<H", data, 18)[0] != 1 or
            struct.unpack_from("<I", data, 20)[0] != 32 or
            struct.unpack_from("<I", data, 24)[0] != len(data) - 32):
        raise ImportFailure("invalid TTEX: %s" % path)


def convert_textures(raw_dir, texture_dir, toyasset):
    texture_dir.mkdir()
    count = 0
    for source in sorted(raw_dir.iterdir()):
        match = re.fullmatch(r"texture_(\d{3})\.[^.]+", source.name, re.IGNORECASE)
        image_format = IMAGE_FORMATS.get(source.suffix.lower())
        if not match or not image_format:
            raise ImportFailure("unsupported converter texture: %s" % source.name)
        output = texture_dir / ("texture_%s.ttex" % match.group(1))
        run([toyasset, "convert", image_format, source, output])
        run([toyasset, "validate", output])
        count += 1
    return count


def validate_outputs(mesh, texture_dir, lods):
    info = rmesh_info(mesh)
    if not texture_dir.is_dir() and info["textures"]:
        raise ImportFailure("missing texture directory: %s" % texture_dir)
    if texture_dir.is_dir():
        for path in texture_dir.iterdir():
            if not path.is_file() or not re.fullmatch(r"texture_\d{3}\.ttex", path.name):
                raise ImportFailure("non-contract file in texture directory: %s" % path.name)
            validate_ttex(path)
    for texture in info["textures"]:
        expected = texture_dir / ("texture_%03d.ttex" % texture)
        if not expected.is_file():
            raise ImportFailure("RMESH references missing texture: %s" % expected.name)
    for lod in lods:
        lod_info = rmesh_info(lod)
        if lod_info["textures"] != info["textures"]:
            raise ImportFailure("LOD material texture table differs from base mesh: %s" % lod)


def install_atomically(staged, output_root, asset_id, force):
    targets = [(path, output_root / path.name) for path in staged]
    existing_family = list(output_root.glob(asset_id + "_lod*.rmesh"))
    expected = {target for _, target in targets}
    existing = [target for _, target in targets if target.exists()]
    existing += [path for path in existing_family if path not in expected]
    if existing and not force:
        raise ImportFailure("output exists (use --force): %s" % existing[0])
    backup = Path(tempfile.mkdtemp(prefix=".asset-backup-%s-" % asset_id, dir=output_root))
    installed = []
    try:
        for old in existing:
            os.replace(old, backup / old.name)
        for source, target in targets:
            os.replace(source, target)
            installed.append(target)
    except Exception:
        for target in installed:
            if target.is_dir():
                shutil.rmtree(target)
            elif target.exists():
                target.unlink()
        for old in backup.iterdir():
            os.replace(old, output_root / old.name)
        raise
    finally:
        shutil.rmtree(backup, ignore_errors=True)


def import_asset(args):
    manifest_path = args.manifest.resolve()
    manifest = read_manifest(manifest_path)
    source = (manifest_path.parent / manifest["source"]).resolve()
    if not source.is_file() or source.suffix.lower() not in (".glb", ".pmx"):
        raise ImportFailure("source must be an existing .glb or .pmx file: %s" % source)
    if manifest["type"] in ("static_prop", "rigid_attachment") and source.suffix.lower() != ".glb":
        raise ImportFailure("%s requires a standardized GLB source" % manifest["type"])
    if manifest["type"] == "character" and source.suffix.lower() not in (".glb", ".pmx"):
        raise ImportFailure("character requires an RFCHAR GLB or compatibility PMX source")
    repo = Path(__file__).resolve().parents[2]
    output_root = (args.output_root or repo / "rasterfall/private-assets/models").resolve()
    output_root.mkdir(parents=True, exist_ok=True)
    if args.validate_only:
        base = output_root / (manifest["id"] + ".rmesh")
        lods = [output_root / ("%s_lod%d.rmesh" % (manifest["id"], lod["level"]))
                for lod in manifest.get("lods", [])]
        validate_outputs(base, output_root / (manifest["id"] + ".textures"), lods)
        print("asset-import: valid", manifest["id"])
        return
    if not args.no_build:
        if manifest["type"] == "character" and source.suffix.lower() == ".glb":
            targets = ["build/toyasset", "app-glb-inspect"]
        else:
            targets = ["build/toyasset", "app-glb2rmesh" if source.suffix.lower() == ".glb" else "app-pmx2rmesh"]
        run([args.make, "-j"] + targets, cwd=repo)
    toyasset = repo / "build/toyasset"
    converter = repo / "build" / ("glb2rmesh" if source.suffix.lower() == ".glb" else "pmx2rmesh")
    lod_tool = repo / "tools/rmesh_lod.py"
    asset_id = manifest["id"]
    with tempfile.TemporaryDirectory(prefix=".asset-import-%s-" % asset_id, dir=output_root) as temp:
        stage = Path(temp)
        mesh = stage / (asset_id + ".rmesh")
        raw = stage / "raw-textures"
        raw.mkdir()
        textures = stage / (asset_id + ".textures")
        if source.suffix.lower() == ".glb":
            if manifest["type"] == "character":
                run([sys.executable, repo / "tools/assets/rfchar_import.py", source, mesh,
                     "--validator", repo / "build/glb-inspect"])
            else:
                run([converter, source, mesh])
            extract_glb_textures(source, raw)
        else:
            run([converter, source, mesh, raw])
        texture_count = convert_textures(raw, textures, toyasset)
        lod_paths = []
        for lod in manifest.get("lods", []):
            output = stage / ("%s_lod%d.rmesh" % (asset_id, lod["level"]))
            command = [sys.executable, lod_tool, mesh, output, "--ratio", str(lod["ratio"])]
            if lod.get("aggressive"):
                command.append("--aggressive")
            if "preserve_primitives" in lod:
                command += ["--preserve-primitives", ",".join(str(int(n)) for n in lod["preserve_primitives"])]
            if "primitive_ratios" in lod:
                command += ["--primitive-ratios", ",".join(str(n) for n in lod["primitive_ratios"])]
            run(command)
            lod_paths.append(output)
        validate_outputs(mesh, textures, lod_paths)
        install_atomically([mesh, textures] + lod_paths, output_root, asset_id, args.force)
    print("asset-import: installed %s (%s, %d textures, %d LODs)" %
          (asset_id, manifest["type"], texture_count, len(lod_paths)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("manifest", type=Path)
    parser.add_argument("--output-root", type=Path)
    parser.add_argument("--force", action="store_true")
    parser.add_argument("--no-build", action="store_true", help="use converters already in build/")
    parser.add_argument("--validate-only", action="store_true", help="validate installed outputs")
    parser.add_argument("--make", default="make", help="make command (default: make)")
    args = parser.parse_args()
    try:
        import_asset(args)
    except (ImportFailure, OSError) as error:
        parser.exit(1, "asset-import: %s\n" % error)


if __name__ == "__main__":
    main()
