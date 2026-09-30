#!/usr/bin/env python3
"""Public RFCHAR fixture -> real validator -> importer -> native runtime checks."""

import argparse
import copy
import json
import struct
import subprocess
import tempfile
from pathlib import Path

from rfchar_import import convert, glb, srgb


def write_glb(path, document, binary):
    data = json.dumps(document, separators=(",", ":")).encode("utf-8")
    data += b" " * (-len(data) % 4)
    path.write_bytes(struct.pack("<4sII", b"glTF", 2, 28 + len(data) + len(binary)) +
                     struct.pack("<II", len(data), 0x4E4F534A) + data +
                     struct.pack("<II", len(binary), 0x004E4942) + binary)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("fixture", type=Path)
    parser.add_argument("--validator", type=Path, required=True)
    parser.add_argument("--runtime", type=Path, required=True)
    args = parser.parse_args()
    original, binary = glb(args.fixture)
    assert [srgb(x) for x in (0,0.0031308,0.25,0.5,1)] == [0,10,137,188,255]
    passed = 0
    with tempfile.TemporaryDirectory(prefix="rfchar-material-") as root:
        source = Path(root) / "fixture.glb"
        output = Path(root) / "fixture.rmesh"

        def run(document):
            write_glb(source, document, binary)
            convert(source, output, args.validator.resolve())
            return output.read_bytes()

        def reject(document, code):
            nonlocal passed
            output.write_bytes(b"previous-good-resource")
            try:
                run(document)
            except ValueError as error:
                assert code in str(error), str(error)
            else:
                raise AssertionError("unsupported material accepted")
            assert output.read_bytes() == b"previous-good-resource", "failed import replaced output"
            passed += 1

        document = copy.deepcopy(original)
        for index, material in enumerate(document["materials"]):
            material["doubleSided"] = index % 2 == 0
            material["alphaMode"] = "OPAQUE"
            material.setdefault("pbrMetallicRoughness", {})["baseColorFactor"] = [0.25, 0.5, 1, 0.5]
        raw = run(document)
        offset = struct.unpack_from("<I", raw, 56)[0]
        for index in range(len(document["materials"])):
            record = raw[offset + 40 * index:offset + 40 * (index + 1)]
            assert record[4:8] == bytes([255, 255, 0, int(index % 2 == 0)])
        result = subprocess.run([str(args.runtime.resolve()), str(output)], capture_output=True, text=True)
        if result.returncode:
            raise AssertionError(result.stdout + result.stderr)
        print(result.stdout.strip())
        assert raw == run(document), "non-deterministic material import"
        passed += 1
        convert(source, output, args.validator.resolve(), 65536)
        precise = output.read_bytes()
        assert struct.unpack_from('<I', precise, 16)[0] == 65536
        assert struct.unpack_from('<II', precise, 8) == struct.unpack_from('<II', raw, 8)
        vertex_offset = offset + 40*len(document['materials'])
        for i in range(struct.unpack_from('<I', raw, 8)[0]):
            a = struct.unpack_from('<iii', raw, vertex_offset+i*36)
            b = struct.unpack_from('<iii', precise, vertex_offset+i*36)
            assert all(abs(x/512-y/65536) <= 0.5/512+0.5/65536 for x,y in zip(a,b))
        result = subprocess.run([str(args.runtime.resolve()), str(output)], capture_output=True, text=True)
        assert result.returncode == 0, result.stdout+result.stderr
        passed += 1
        role_name=document['materials'][0]['name']
        convert(source, output, args.validator.resolve(), 65536, {role_name:'eyes'})
        assert output.read_bytes()[offset+36] == 2
        passed += 1

        # An unassigned primitive must get glTF white, not the first colored material.
        document = copy.deepcopy(original)
        del document["meshes"][0]["primitives"][0]["material"]
        raw = run(document)
        count = len(original["materials"])
        assert struct.unpack_from("<I", raw, 48)[0] == count + 1
        offset = struct.unpack_from("<I", raw, 56)[0]
        assert struct.unpack_from("<I", raw, offset + 40 * count)[0] == 0xffffff
        refs = [struct.unpack_from("<I", raw, 64 + 16 * i + 8)[0]
                for i in range(struct.unpack_from("<I", raw, 44)[0])]
        assert count in refs
        passed += 1

        document = copy.deepcopy(original)
        document.pop("materials")
        for mesh in document["meshes"]:
            for primitive in mesh["primitives"]:
                primitive.pop("material", None)
        raw = run(document)
        assert struct.unpack_from("<I", raw, 48)[0] == 1
        passed += 1

        document = copy.deepcopy(original)
        document["meshes"][0]["primitives"][0]["material"] = len(document["materials"])
        reject(document, "MATERIAL_INDEX")

        for mode in ("MASK", "BLEND"):
            document = copy.deepcopy(original)
            document["materials"][0]["alphaMode"] = mode
            reject(document, "MATERIAL_CAPABILITY")
        document = copy.deepcopy(original)
        document["materials"][0]["extras"] = {"rf_material": {"version": 1, "id": "probe"}}
        reject(document, "MATERIAL_CAPABILITY")
        for field, value in (("doubleSided", "false"), ("baseColorFactor", [1, 1, 1, 2])):
            document = copy.deepcopy(original)
            target = document["materials"][0]
            if field == "baseColorFactor":
                target = target.setdefault("pbrMetallicRoughness", {})
            target[field] = value
            reject(document, "MATERIAL_FIELD")
    print(f"rfchar-materials: PASS ({passed} cases; native runtime, deterministic output, failure preservation)")


if __name__ == "__main__":
    main()
