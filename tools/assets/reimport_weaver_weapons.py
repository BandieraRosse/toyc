#!/usr/bin/env python3
"""Re-export legacy rigid weapons from the hashed manufacturing catalog.

This narrow color refresh retains legacy filenames (including rf_AWP), which
predate schema-1 manifests. It rejects any geometry/physical-material change,
then regenerates and verifies the manufacturing cache. It never stages a build.
"""
from __future__ import annotations

import argparse
import copy
import json
from pathlib import Path
import struct
import subprocess
import sys
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parents[1]))
import mesh_weaver_blueprints as blueprints


def material_only(before, after):
    if before[:8] != b"RFM2\x02\0\0\0" or before[:64] != after[:64]:
        raise ValueError("RFM2 v2 header, counts, bounds or scale changed")
    count = struct.unpack_from("<I", before, 48)[0]
    offset = struct.unpack_from("<I", before, 56)[0]
    end = offset + count * 16
    if before[:offset] != after[:offset] or before[end:] != after[end:]:
        raise ValueError("primitive records or vertex/normal/UV/index data changed")
    colors, absent_slots = 0, 0
    for at in range(offset, end, 16):
        old = struct.unpack_from("<IHHII", before, at)
        new = struct.unpack_from("<IHHII", after, at)
        if old[1:3] != new[1:3] or old[4] != new[4]:
            raise ValueError("metallic, roughness or material flags changed")
        if old[3] not in (0, 0xFFFFFFFF) or new[3] != 0xFFFFFFFF:
            raise ValueError("unexpected texture reference in untextured source")
        colors += old[0] != new[0]
        absent_slots += old[3] != new[3]
    return colors, absent_slots


def semantic_document(document):
    result = copy.deepcopy(document)
    for row in result["blueprints"]:
        del row["runtime_sha256"]
        del row["runtime_texture_sha256"]
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--converter", type=Path, default=blueprints.ROOT / "build-windows/glb2rmesh.exe")
    args = parser.parse_args()
    root, converter = blueprints.ROOT, args.converter.resolve()
    json_path, header_path = root / blueprints.JSON_PATH, root / blueprints.HEADER_PATH
    document = json.loads(json_path.read_text(encoding="utf-8-sig"))
    blueprints.check(root, document, header_path.read_text(encoding="utf-8-sig"))
    old_files = {json_path: json_path.read_bytes(), header_path: header_path.read_bytes()}
    outputs, reports = {}, []
    (root / "tmp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="weaver-color-import-", dir=root / "tmp") as temporary:
        for row in document["blueprints"]:
            source, target = root / row["source_path"], root / row["runtime_path"]
            raw = source.read_bytes()
            if blueprints.sha256(raw) != row["source_sha256"]:
                raise ValueError("source hash mismatch: " + row["id"])
            length, kind = struct.unpack_from("<II", raw, 12)
            if raw[:4] != b"glTF" or kind != 0x4E4F534A:
                raise ValueError("expected source GLB JSON chunk")
            source_document = json.loads(raw[20:20 + length])
            if source_document.get("textures") or source_document.get("images"):
                raise ValueError("this refresh only accepts untextured sources: " + row["id"])
            before = target.read_bytes()
            old_files[target] = before
            staged = Path(temporary) / target.name
            subprocess.run([str(converter), str(source), str(staged)], check=True)
            after = staged.read_bytes()
            colors, absent_slots = material_only(before, after)
            outputs[target] = after
            reports.append((row["id"], colors, absent_slots))
        try:
            for path, content in outputs.items():
                # Write into the existing file, preserving its normal inherited ACL.
                path.write_bytes(content)
            updated = blueprints.generate(root)
            if semantic_document(updated) != semantic_document(document):
                raise ValueError("source, topology, volume, adapter, bounds, textures or manufacturing inputs changed")
            blueprints.write_utf8(json_path, json.dumps(updated, ensure_ascii=False, indent=2) + "\n")
            blueprints.write_utf8(header_path, blueprints.generated_header(updated))
            blueprints.check(root, updated, header_path.read_text(encoding="utf-8-sig"))
        except BaseException:
            for path, content in old_files.items():
                path.write_bytes(content)
            raise
    for name, colors, absent_slots in reports:
        print("%s: %d RGB records refreshed, %d absent texture slots normalized; geometry and manufacturing inputs unchanged" %
              (name, colors, absent_slots))
    print("weaver-color-import: complete exports installed and both hash caches checked; build/package staging not performed")


if __name__ == "__main__":
    main()
