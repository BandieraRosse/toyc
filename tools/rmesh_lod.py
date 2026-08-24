#!/usr/bin/env python3
"""Build a deterministic, skin-compatible RFM2 index LOD.

The output keeps the source vertex and SKN1 data byte-for-byte.  Vertices are
clustered by position, UV and primary skin influences; triangles collapsing to
an edge or point are removed per material primitive.  Keeping the vertex table
stable means animation tracks and bone indices remain directly compatible.
"""

import argparse
import struct
from pathlib import Path

HEADER = 64
PRIMITIVE = 16


def u32(data, at):
    return struct.unpack_from("<I", data, at)[0]


def i32(data, at):
    return struct.unpack_from("<i", data, at)[0]


def validate(data):
    if len(data) < HEADER or data[:4] != b"RFM2":
        raise ValueError("not an RFM2 mesh")
    version = u32(data, 4)
    if version < 10 or version > 13:
        raise ValueError("LOD builder requires RFM2 v10-v13")
    vertices, indices = u32(data, 8), u32(data, 12)
    primitives, materials = u32(data, 44), u32(data, 48)
    material_bytes = 40 if version >= 9 else 24 if version >= 8 else 16
    vertex_bytes = 36 if version >= 10 else 32 if version >= 6 else 24
    primitive_at, material_at = u32(data, 52), u32(data, 56)
    vertex_at = material_at + materials * material_bytes
    index_at = vertex_at + vertices * vertex_bytes
    skin_at = index_at + indices * 4
    if primitive_at != HEADER or material_at != HEADER + primitives * PRIMITIVE:
        raise ValueError("invalid RFM2 offsets")
    if version >= 11 and u32(data, 60) != skin_at:
        raise ValueError("invalid SKN1 offset")
    if skin_at > len(data) or (version < 11 and skin_at != len(data)):
        raise ValueError("truncated RFM2 mesh")
    return {
        "version": version, "vertices": vertices, "indices": indices,
        "primitives": primitives, "materials": materials,
        "material_bytes": material_bytes, "vertex_bytes": vertex_bytes,
        "primitive_at": primitive_at, "material_at": material_at,
        "vertex_at": vertex_at, "index_at": index_at, "skin_at": skin_at,
    }


def skin_keys(data, info):
    count = info["vertices"]
    if info["version"] < 11:
        return [(0, 0, 0)] * count
    at = info["skin_at"]
    bones = u32(data, at + 8)
    bone_bytes = u32(data, at + 12)
    skin_vertices = at + 32 + bones * bone_bytes
    keys = []
    for i in range(count):
        bone0, bone1, weight = struct.unpack_from(
            "<HHH", data, skin_vertices + i * 8)
        keys.append((bone0, bone1, weight >> 12))
    return keys


def cluster_map(data, info, divisions):
    mins = (i32(data, 20), i32(data, 24), i32(data, 28))
    maxs = (i32(data, 32), i32(data, 36), i32(data, 40))
    spans = tuple(max(1, maxs[i] - mins[i]) for i in range(3))
    skin = skin_keys(data, info)
    representatives = {}
    mapped = []
    for i in range(info["vertices"]):
        at = info["vertex_at"] + i * info["vertex_bytes"]
        position = struct.unpack_from("<iii", data, at)
        uv = struct.unpack_from("<HH", data, at + 18)
        cell = tuple((position[a] - mins[a]) * divisions // spans[a]
                     for a in range(3))
        # UV and primary influences protect seams and animated joints while
        # still allowing dense, locally redundant surface vertices to merge.
        key = cell + (uv[0] >> 12, uv[1] >> 12) + skin[i]
        representative = representatives.setdefault(key, i)
        mapped.append(representative)
    return mapped


def simplify(data, info, divisions):
    mapped = cluster_map(data, info, divisions)
    output = []
    counts = []
    for primitive in range(info["primitives"]):
        at = info["primitive_at"] + primitive * PRIMITIVE
        first, count = u32(data, at), u32(data, at + 4)
        seen = set()
        kept = []
        for j in range(first, first + count, 3):
            tri = tuple(mapped[u32(data, info["index_at"] + (j + k) * 4)]
                        for k in range(3))
            if len(set(tri)) < 3:
                continue
            canonical = tuple(sorted(tri))
            if canonical in seen:
                continue
            seen.add(canonical)
            kept.extend(tri)
        output.extend(kept)
        counts.append(len(kept))
    return output, counts


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ratio", type=float, default=0.4)
    args = parser.parse_args()
    if not 0.05 <= args.ratio < 1.0:
        parser.error("--ratio must be in [0.05, 1.0)")
    data = args.input.read_bytes()
    info = validate(data)
    target = info["indices"] * args.ratio
    best = None
    for divisions in range(4, 129):
        indices, counts = simplify(data, info, divisions)
        score = abs(len(indices) - target)
        if best is None or score < best[0]:
            best = score, divisions, indices, counts
    _, divisions, indices, counts = best
    header = bytearray(data[:HEADER])
    struct.pack_into("<I", header, 12, len(indices))
    new_skin_at = (info["index_at"] + len(indices) * 4)
    if info["version"] >= 11:
        struct.pack_into("<I", header, 60, new_skin_at)
    primitives = bytearray(data[info["primitive_at"]:info["material_at"]])
    first = 0
    for i, count in enumerate(counts):
        struct.pack_into("<II", primitives, i * PRIMITIVE, first, count)
        first += count
    index_data = struct.pack("<%dI" % len(indices), *indices)
    output = (bytes(header) + bytes(primitives) +
              data[info["material_at"]:info["index_at"]] + index_data +
              data[info["skin_at"]:])
    validate(output)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print("rmesh-lod: %s -> %s divisions=%d triangles=%d -> %d ratio=%.3f" %
          (args.input, args.output, divisions, info["indices"] // 3,
           len(indices) // 3, len(indices) / info["indices"]))


if __name__ == "__main__":
    main()
