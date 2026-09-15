#!/usr/bin/env python3
"""Build a deterministic, skin-compatible RFM2 vertex/index LOD.

Vertices are clustered by position, UV and primary skin influences; triangles
collapsing to an edge or point are removed per material primitive.  The output
then compacts the vertex table and matching SKN1 weight records to the vertices
referenced by those indices.  Skeleton, IK, CHR1, materials and primitives keep
their existing layouts and semantics.
"""

import argparse
import json
import math
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
    if version < 2 or version > 14:
        raise ValueError("LOD builder requires RFM2 v2-v14")
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


def skin_data(data, info):
    """Return bone metadata and unquantized BDEF records."""
    if info["version"] < 11:
        return [], [(0, 0, 65535, 0)] * info["vertices"]
    at = info["skin_at"]
    bone_count = u32(data, at + 8)
    bone_bytes = u32(data, at + 12)
    vertex_count = u32(data, at + 16)
    names_bytes = u32(data, at + 24)
    bones_at = at + 32
    weights_at = bones_at + bone_count * bone_bytes
    names_at = weights_at + vertex_count * 8
    names = data[names_at:names_at + names_bytes]
    bones = []
    for i in range(bone_count):
        record = bones_at + i * bone_bytes
        name_at = u32(data, record + 20)
        name_end = names.find(b"\0", name_at)
        if name_end < 0:
            raise ValueError("invalid SKN1 bone name")
        bones.append({
            "name": names[name_at:name_end].decode("utf-8"),
            "parent": i32(data, record),
            "rest": struct.unpack_from("<iii", data, record + 8),
        })
    weights = [struct.unpack_from("<HHHH", data, weights_at + i * 8)
               for i in range(vertex_count)]
    return bones, weights


def load_region_profile(path, data, info):
    """Resolve an offline descriptor to bone indices and joint zones."""
    raw = json.loads(path.read_text(encoding="utf-8"))
    allowed = {"schema", "name", "high_bones", "medium_bones",
               "high_primitives", "medium_primitives", "joint_zones",
               "high_divisor", "medium_divisor", "weight_step"}
    unknown = set(raw) - allowed
    if unknown or raw.get("schema") != 1:
        raise ValueError("invalid region profile%s" %
                         (": unknown " + ",".join(sorted(unknown)) if unknown else ""))
    bones, weights = skin_data(data, info)
    by_name = {bone["name"]: i for i, bone in enumerate(bones)}

    def resolve(names, label):
        missing = [name for name in names if name not in by_name]
        if missing:
            raise ValueError("%s bones not found: %s" % (label, ", ".join(missing)))
        return {by_name[name] for name in names}

    high = resolve(raw.get("high_bones", []), "high")
    medium = resolve(raw.get("medium_bones", []), "medium")
    joints = []
    for joint in raw.get("joint_zones", []):
        if set(joint) != {"bones", "radius_ratio", "level"} or \
                len(joint["bones"]) != 2 or joint["level"] not in (1, 2):
            raise ValueError("invalid joint zone")
        pair = list(resolve(joint["bones"], "joint"))
        a, b = pair
        length = math.sqrt(sum((bones[a]["rest"][axis] - bones[b]["rest"][axis]) ** 2
                               for axis in range(3)))
        joints.append((set(pair), bones[b]["rest"],
                       max(1.0, length * float(joint["radius_ratio"])),
                       joint["level"]))
    result = {
        "name": raw.get("name", path.stem), "bones": bones, "weights": weights,
        "high": high, "medium": medium, "joints": joints,
        "high_primitives": set(raw.get("high_primitives", [])),
        "medium_primitives": set(raw.get("medium_primitives", [])),
        "high_divisor": int(raw.get("high_divisor", 4)),
        "medium_divisor": int(raw.get("medium_divisor", 2)),
        "weight_step": int(raw.get("weight_step", 4096)),
    }
    result["protection"] = vertex_protection(data, info, result)
    return result


def vertex_protection(data, info, profile):
    levels = []
    for i, (bone0, bone1, weight, _kind) in enumerate(profile["weights"]):
        influences = {bone0}
        if bone1 != bone0 and weight < 65535:
            influences.add(bone1)
        level = 2 if influences & profile["high"] else \
            1 if influences & profile["medium"] else 0
        at = info["vertex_at"] + i * info["vertex_bytes"]
        position = struct.unpack_from("<iii", data, at)
        for joint_bones, center, radius, joint_level in profile["joints"]:
            if not influences & joint_bones:
                continue
            distance = math.sqrt(sum((position[a] - center[a]) ** 2 for a in range(3)))
            if distance <= radius:
                level = max(level, joint_level)
        levels.append(level)
    return levels


def cluster_map(data, info, divisions, aggressive=False, profile=None,
                primitive=-1):
    mins = (i32(data, 20), i32(data, 24), i32(data, 28))
    maxs = (i32(data, 32), i32(data, 36), i32(data, 40))
    spans = tuple(max(1, maxs[i] - mins[i]) for i in range(3))
    skin = skin_keys(data, info)
    protection = profile["protection"] if profile else None
    representatives = {}
    mapped = []
    for i in range(info["vertices"]):
        at = info["vertex_at"] + i * info["vertex_bytes"]
        position = struct.unpack_from("<iii", data, at)
        uv = struct.unpack_from("<HH", data, at + 18)
        level = protection[i] if protection else 0
        if profile and primitive in profile["high_primitives"]:
            level = 2
        elif profile and primitive in profile["medium_primitives"]:
            level = max(level, 1)
        local_divisions = divisions
        if profile and level == 2:
            local_divisions *= profile["high_divisor"]
        elif profile and level == 1:
            local_divisions *= profile["medium_divisor"]
        cell = tuple((position[a] - mins[a]) * local_divisions // spans[a]
                     for a in range(3))
        # Normal LOD protects UV seams and the complete two-weight skin key.
        # LOD2 trades texture fidelity for stronger reduction, but keeps the
        # primary-bone partition so unrelated animated limbs do not collapse.
        if profile:
            bone0, bone1, weight, _kind = profile["weights"][i]
            dominant = bone0 if weight >= 32768 else bone1
            pair = tuple(sorted((bone0, bone1)))
            weight_bucket = min(65535, weight) // profile["weight_step"]
            # Region level prevents a protected silhouette/joint vertex from
            # using an unprotected representative. Exact BDEF pair plus a
            # bounded weight bucket prevents merges across skin discontinuities.
            key = ((level,) + cell + (uv[0] >> (13 if level == 0 else 12),
                                      uv[1] >> (13 if level == 0 else 12),
                                      dominant) + pair + (weight_bucket,))
        else:
            key = (cell + (skin[i][0],) if aggressive else
                   cell + (uv[0] >> 12, uv[1] >> 12) + skin[i])
        representative = representatives.setdefault(key, i)
        mapped.append(representative)
    return mapped


def simplify_primitive(data, info, mapped, primitive):
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
    return kept


def original_primitive(data, info, primitive):
    at = info["primitive_at"] + primitive * PRIMITIVE
    first, count = u32(data, at), u32(data, at + 4)
    return [u32(data, info["index_at"] + j * 4)
            for j in range(first, first + count)]


def simplify(data, info, divisions, aggressive=False, profile=None):
    output = []
    counts = []
    maps = {}
    for primitive in range(info["primitives"]):
        primitive_level = 2 if profile and primitive in profile["high_primitives"] else \
            1 if profile and primitive in profile["medium_primitives"] else 0
        if primitive_level not in maps:
            representative = primitive if primitive_level else -1
            maps[primitive_level] = cluster_map(
                data, info, divisions, aggressive, profile, representative)
        mapped = maps[primitive_level]
        kept = simplify_primitive(data, info, mapped, primitive)
        output.extend(kept)
        counts.append(len(kept))
    return output, counts


def compact_vertices(data, info, indices):
    """Return remapped indices, compact vertex bytes and a compact SKN1 tail."""
    referenced = sorted(set(indices))
    if any(index >= info["vertices"] for index in referenced):
        raise ValueError("index out of range")
    remap = {old: new for new, old in enumerate(referenced)}
    compact_indices = [remap[index] for index in indices]
    vertices = b"".join(
        data[info["vertex_at"] + old * info["vertex_bytes"]:
             info["vertex_at"] + (old + 1) * info["vertex_bytes"]]
        for old in referenced
    )
    if info["version"] < 11:
        return referenced, compact_indices, vertices, b""

    skin_at = info["skin_at"]
    if skin_at + 32 > len(data) or data[skin_at:skin_at + 4] != b"SKN1":
        raise ValueError("invalid SKN1 section")
    skin_bytes = u32(data, skin_at + 4)
    bone_count = u32(data, skin_at + 8)
    bone_bytes = u32(data, skin_at + 12)
    skin_vertex_count = u32(data, skin_at + 16)
    skin_vertex_bytes = u32(data, skin_at + 20)
    if (skin_vertex_count != info["vertices"] or skin_vertex_bytes != 8 or
            skin_at + skin_bytes > len(data)):
        raise ValueError("invalid SKN1 vertex layout")
    weights_at = skin_at + 32 + bone_count * bone_bytes
    weights_end = weights_at + skin_vertex_count * skin_vertex_bytes
    if weights_end > skin_at + skin_bytes:
        raise ValueError("truncated SKN1 vertex records")
    weights = b"".join(
        data[weights_at + old * skin_vertex_bytes:
             weights_at + (old + 1) * skin_vertex_bytes]
        for old in referenced
    )
    skin_header = bytearray(data[skin_at:skin_at + 32])
    new_skin_bytes = skin_bytes - (skin_vertex_count - len(referenced)) * skin_vertex_bytes
    struct.pack_into("<I", skin_header, 4, new_skin_bytes)
    struct.pack_into("<I", skin_header, 16, len(referenced))
    skin = (bytes(skin_header) + data[skin_at + 32:weights_at] + weights +
            data[weights_end:skin_at + skin_bytes] +
            data[skin_at + skin_bytes:])
    return referenced, compact_indices, vertices, skin


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--ratio", type=float, default=0.4)
    parser.add_argument(
        "--primitive-ratios",
        help="comma-separated triangle budgets for each material primitive; "
             "overrides --ratio and protects important material groups",
    )
    parser.add_argument(
        "--preserve-primitives",
        help="comma-separated primitive indices to keep byte-for-byte; "
             "all other primitives use --ratio",
    )
    parser.add_argument(
        "--aggressive", action="store_true",
        help="use LOD2 clustering: drop UV/secondary-weight protection",
    )
    parser.add_argument(
        "--keep-unused-vertices", action="store_true",
        help="retain the legacy full vertex/SKN1 tables",
    )
    parser.add_argument(
        "--region-profile", type=Path,
        help="JSON humanoid/material protection descriptor; adds non-uniform "
             "spatial and BDEF constraints",
    )
    args = parser.parse_args()
    if not 0.01 <= args.ratio < 1.0:
        parser.error("--ratio must be in [0.01, 1.0)")
    data = args.input.read_bytes()
    info = validate(data)
    profile = load_region_profile(args.region_profile, data, info) \
        if args.region_profile else None
    preserved = set()
    if args.preserve_primitives:
        try:
            preserved = {int(value) for value in args.preserve_primitives.split(",")}
        except ValueError:
            parser.error("--preserve-primitives must contain integer indices")
        if any(value < 0 or value >= info["primitives"] for value in preserved):
            parser.error("--preserve-primitives contains an invalid primitive index")

    if args.primitive_ratios:
        ratios = [float(value) for value in args.primitive_ratios.split(",")]
        if len(ratios) != info["primitives"] or any(r < 0 for r in ratios):
            parser.error("--primitive-ratios must contain one non-negative ratio per primitive")
        if preserved:
            parser.error("--preserve-primitives cannot be combined with --primitive-ratios")
        indices, counts, selected = [], [], []
        for primitive, ratio in enumerate(ratios):
            at = info["primitive_at"] + primitive * PRIMITIVE
            target = u32(data, at + 4) * ratio
            if ratio >= 1.0:
                kept = original_primitive(data, info, primitive)
                indices.extend(kept)
                counts.append(len(kept))
                selected.append("full")
                continue
            choice = min(
                ((abs(len(simplify_primitive(data, info,
                    cluster_map(data, info, candidate, args.aggressive,
                                profile, primitive), primitive)) - target),
                  candidate)
                 for candidate in range(4, 129)),
                key=lambda item: item[0],
            )
            kept = simplify_primitive(data, info,
                cluster_map(data, info, choice[1], args.aggressive,
                            profile, primitive), primitive)
            indices.extend(kept)
            counts.append(len(kept))
            selected.append(choice[1])
        divisions = "profile[" + ",".join(str(value) for value in selected) + "]"
    elif preserved:
        indices, counts, selected = [], [], []
        for primitive in range(info["primitives"]):
            at = info["primitive_at"] + primitive * PRIMITIVE
            if primitive in preserved:
                kept = original_primitive(data, info, primitive)
                selected.append("full")
            else:
                target = u32(data, at + 4) * args.ratio
                choice = min(
                    ((abs(len(simplify_primitive(data, info,
                        cluster_map(data, info, candidate, args.aggressive,
                                    profile, primitive), primitive)) - target),
                      candidate)
                     for candidate in range(4, 129)),
                    key=lambda item: item[0],
                )
                kept = simplify_primitive(data, info,
                    cluster_map(data, info, choice[1], args.aggressive,
                                profile, primitive), primitive)
                selected.append(choice[1])
            indices.extend(kept)
            counts.append(len(kept))
        divisions = "preserved[" + ",".join(str(value)
                                            for value in sorted(preserved)) + "]"
    else:
        target = info["indices"] * args.ratio
        best = None
        for divisions in range(4, 129):
            indices, counts = simplify(data, info, divisions, args.aggressive,
                                       profile)
            score = abs(len(indices) - target)
            if best is None or score < best[0]:
                best = score, divisions, indices, counts
        _, divisions, indices, counts = best
    source_vertices = info["vertices"]
    if args.keep_unused_vertices:
        referenced = list(range(source_vertices))
        vertices = data[info["vertex_at"]:info["index_at"]]
        tail = data[info["skin_at"]:]
    else:
        referenced, indices, vertices, tail = compact_vertices(data, info, indices)
    header = bytearray(data[:HEADER])
    struct.pack_into("<I", header, 8, len(referenced))
    struct.pack_into("<I", header, 12, len(indices))
    new_skin_at = info["vertex_at"] + len(vertices) + len(indices) * 4
    if info["version"] >= 11:
        struct.pack_into("<I", header, 60, new_skin_at)
    primitives = bytearray(data[info["primitive_at"]:info["material_at"]])
    first = 0
    for i, count in enumerate(counts):
        struct.pack_into("<II", primitives, i * PRIMITIVE, first, count)
        first += count
    index_data = struct.pack("<%dI" % len(indices), *indices)
    output = (bytes(header) + bytes(primitives) +
              data[info["material_at"]:info["vertex_at"]] + vertices +
              index_data + tail)
    output_info = validate(output)
    if output_info["vertices"] != len(referenced):
        raise ValueError("compacted vertex count mismatch")
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(output)
    print("rmesh-lod: %s -> %s divisions=%s profile=%s triangles=%d -> %d ratio=%.3f "
          "vertices=%d -> %d" %
          (args.input, args.output, divisions,
           profile["name"] if profile else "uniform", info["indices"] // 3,
           len(indices) // 3, len(indices) / info["indices"],
           source_vertices, len(referenced)))


if __name__ == "__main__":
    main()
