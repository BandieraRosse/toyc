#!/usr/bin/env python3
"""Derive fixed manufacturing blueprints from full weapon GLBs and runtime meshes.

The original GLBs are local inputs, not public redistribution artifacts.  The
checked-in JSON and C header retain their hashes and the complete calculation.
Run with --check to reject stale runtime geometry, physical adapters, source
geometry (when available), or generated C data.  No rendering LOD is consulted.
"""
from __future__ import annotations

import argparse
from collections import Counter, defaultdict
import hashlib
import json
import math
from pathlib import Path
import re
import struct
import sys


ROOT = Path(__file__).resolve().parents[1]
JSON_PATH = Path("rasterfall/assets/manufacturing/blueprints.json")
HEADER_PATH = Path("rasterfall/include/rf_weaver_blueprints_generated.h")
SCHEMA = 1
ALGORITHM = "exact-position-welded-closed-shell-sum-v1"
# These are source identities, not weapon-specific production coefficients.
CATALOG = (
    ("ak", "AK", "AK", "Assault Rifle-XGeBGFQxYg.glb"),
    ("pistol", "PISTOL", "Pistol", "Pistol-L1u7KkJzY2.glb"),
    ("smg", "SMG", "SMG", "Submachine Gun-thBPAYTK5R.glb"),
    ("shotgun", "SHOTGUN", "Shotgun", "Shotgun-f54ZSBZZ8k.glb"),
    ("awp", "AWP", "AWP", "Sniper Rifle-ZkwSIy3JOV.glb"),
)


class BlueprintError(ValueError):
    pass


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def canonical_json(value):
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"))


def fingerprint(value):
    return sha256(canonical_json(value).encode("utf-8"))


def generator_fingerprint():
    # Git may check the same Python source out as LF or CRLF. Encoding/newline
    # transport must not require unavailable private GLBs to be regenerated.
    return sha256(Path(__file__).read_text(encoding="utf-8-sig").encode("utf-8"))


def rounded(value):
    return round(value, 12)


def half_up(value):
    return int(math.floor(value + 0.5))


def c_function(text, name):
    """Read one known C definition; fail closed if its layout is no longer found."""
    match = re.search(r"\b" + re.escape(name) + r"\s*\([^;{}]*\)\s*\{", text)
    if not match:
        raise BlueprintError("missing physical adapter function: " + name)
    start, depth, at = match.start(), 1, match.end()
    while depth and at < len(text):
        depth += (text[at] == "{") - (text[at] == "}")
        at += 1
    if depth:
        raise BlueprintError("unclosed physical adapter function: " + name)
    return text[start:at]


def calibration_profiles(root):
    """Extract the existing calibration table; never invent fallback lengths."""
    text = (root / "rasterfall/src/rasterfall_calibration.c").read_text(encoding="utf-8-sig")
    header = (root / "rasterfall/include/toy_game.h").read_text(encoding="utf-8-sig")
    units = (root / "rasterfall/include/rasterfall_units.h").read_text(encoding="utf-8-sig")
    unit_match = re.search(r"#define\s+RASTERFALL_RFU_PER_METER\s+(\d+)\b", units)
    if not unit_match or int(unit_match[1]) != 512:
        raise BlueprintError("RFU/metre changed; update the physical adapter extractor")
    enum = re.search(r"enum toy_game_weapon\s*\{([^}]+)\}", header)
    paths = re.search(r"static const char \*paths\[TOY_GAME_WEAPON_COUNT\]\s*=\s*\{([^}]+)\}", text)
    if not enum or not paths:
        raise BlueprintError("weapon enum/path table changed; update the extractor")
    ids, next_id = {}, 0
    for token, explicit in re.findall(r"\b(TOY_GAME_WEAPON_\w+)\s*(?:=\s*(\d+))?", enum[1]):
        next_id = int(explicit) if explicit else next_id
        ids[token] = next_id
        next_id += 1
    model_paths = re.findall(r'"([^"\n]+[.]rmesh)"', paths[1])
    fields = {"AK": {}}
    for key in ("asset_basis", "length_mm"):
        match = re.search(r"asset_profiles\[TOY_GAME_WEAPON_AK\][.]" + key + r"\s*=\s*(\d+)\s*;", text)
        if not match:
            raise BlueprintError("missing AK adapter field: " + key)
        fields["AK"][key] = int(match[1])
    for weapon, basis, length in re.findall(r"\{TOY_GAME_WEAPON_(\w+),\s*(\d+),\s*(\d+),", text):
        fields[weapon] = {"asset_basis": int(basis), "length_mm": int(length)}
    functions = [c_function(text, name) for name in
                 ("rasterfall_weapon_asset_to_canonical", "rasterfall_weapon_model_adapt")]
    # Hash executable text too: a rounding or basis algorithm change requires
    # explicit regeneration even if all lengths remain equal.
    functions = [re.sub(r"\s+", " ", re.sub(r"/\*.*?\*/|//[^\n]*", "", x, flags=re.S)).strip()
                 for x in functions]
    profiles = {}
    for _, token, _, _ in CATALOG:
        enum_name = "TOY_GAME_WEAPON_" + token
        index = ids.get(enum_name)
        if index is None or index >= len(model_paths) or token not in fields:
            raise BlueprintError("missing calibrated weapon: " + token)
        p = dict(fields[token], weapon=index, weapon_enum=enum_name,
                 runtime_path=model_paths[index], rfu_per_metre=512,
                 adapter_code_sha256=fingerprint(functions))
        if p["asset_basis"] not in (0, 1, 2) or p["length_mm"] <= 0:
            raise BlueprintError("unsupported physical adapter: " + token)
        profiles[token] = p
    return profiles


def manufacturing_layout(root):
    text = (root / "rasterfall/include/toy_mesh_weaver.h").read_text(encoding="utf-8-sig")
    values = []
    for name in ("TOY_WEAVER_ENVELOPE_MM", "TOY_WEAVER_PRODUCT_YAW_DEG"):
        match = re.search(r"#define\s+" + name + r"\s+(\d+)\b", text)
        if not match:
            raise BlueprintError("missing machine layout constant: " + name)
        values.append(int(match[1]))
    return values[0] / 1000, values[1]


IDENTITY = (1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1., 0., 0., 0., 0., 1.)


def matrix_multiply(a, b):
    return tuple(sum(a[row + k * 4] * b[k + col * 4] for k in range(4))
                 for col in range(4) for row in range(4))


def node_matrix(node):
    if "matrix" in node:
        if any(k in node for k in ("translation", "rotation", "scale")):
            raise BlueprintError("GLB node mixes matrix and TRS")
        result = tuple(node["matrix"])
        if len(result) != 16 or result[3::4] != (0, 0, 0, 1):
            raise BlueprintError("GLB node matrix must be affine")
        return result
    t = node.get("translation", (0, 0, 0))
    q = node.get("rotation", (0, 0, 0, 1))
    s = node.get("scale", (1, 1, 1))
    if len(t) != 3 or len(q) != 4 or len(s) != 3:
        raise BlueprintError("invalid GLB TRS dimensions")
    norm = math.sqrt(sum(x * x for x in q))
    if abs(norm - 1) > 1e-5 or any(x == 0 for x in s):
        raise BlueprintError("invalid GLB rotation or zero scale")
    x, y, z, w = q
    return ((1 - 2*y*y - 2*z*z)*s[0], (2*x*y + 2*z*w)*s[0], (2*x*z - 2*y*w)*s[0], 0,
            (2*x*y - 2*z*w)*s[1], (1 - 2*x*x - 2*z*z)*s[1], (2*y*z + 2*x*w)*s[1], 0,
            (2*x*z + 2*y*w)*s[2], (2*y*z - 2*x*w)*s[2], (1 - 2*x*x - 2*y*y)*s[2], 0,
            *t, 1)


def transform_point(matrix, point):
    return tuple(sum(matrix[row + col*4] * point[col] for col in range(3)) + matrix[row+12]
                 for row in range(3))


def flatten_glb(document, binary):
    """Expand active-scene nodes and every referenced primitive, including instances."""
    def accessor(index, expected):
        a = document["accessors"][index]
        if "sparse" in a or a.get("normalized") or a["type"] != expected:
            raise BlueprintError("unsupported sparse/normalized/accessor type")
        fmt = {5126: "f", 5121: "B", 5123: "H", 5125: "I"}.get(a["componentType"])
        if not fmt or (expected == "VEC3" and fmt != "f"):
            raise BlueprintError("unsupported GLB component type")
        view = document["bufferViews"][a["bufferView"]]
        if view.get("buffer", 0) != 0:
            raise BlueprintError("external GLB buffer is not supported")
        fmt = "<" + fmt * (3 if expected == "VEC3" else 1)
        size = struct.calcsize(fmt)
        stride = view.get("byteStride", size)
        start = view.get("byteOffset", 0) + a.get("byteOffset", 0)
        end = start + max(0, a["count"] - 1) * stride + size
        if a["count"] <= 0 or stride < size or end > min(len(binary), view.get("byteOffset", 0) + view["byteLength"]):
            raise BlueprintError("truncated GLB accessor")
        return [struct.unpack_from(fmt, binary, start + i*stride) for i in range(a["count"])]

    nodes, meshes = document.get("nodes", []), document.get("meshes", [])
    scene = document.get("scene", 0)
    scenes = document.get("scenes", [])
    if not scenes or not 0 <= scene < len(scenes):
        raise BlueprintError("manufacturing GLB needs an active scene")
    positions, triangles, weld_groups, instances, used_materials = [], [], [], [], set()

    def visit(index, parent, ancestors):
        if index in ancestors or not 0 <= index < len(nodes):
            raise BlueprintError("invalid/cyclic GLB node tree")
        node = nodes[index]
        world = matrix_multiply(parent, node_matrix(node))
        if any(not math.isfinite(x) for x in world):
            raise BlueprintError("non-finite GLB transform")
        if "skin" in node or node.get("weights"):
            raise BlueprintError("manufacturing blueprint requires a fixed unskinned mesh")
        if "mesh" in node:
            instance_id = len(instances)
            first_vertex, first_triangle = len(positions), len(triangles)
            for primitive in meshes[node["mesh"]]["primitives"]:
                if primitive.get("mode", 4) != 4 or primitive.get("targets"):
                    raise BlueprintError("manufacturing requires fixed triangle primitives")
                vertices = accessor(primitive["attributes"]["POSITION"], "VEC3")
                ids = ([x[0] for x in accessor(primitive["indices"], "SCALAR")]
                       if "indices" in primitive else list(range(len(vertices))))
                if len(ids) % 3 or any(not 0 <= x < len(vertices) for x in ids):
                    raise BlueprintError("invalid GLB triangle index")
                base = len(positions)
                positions.extend(transform_point(world, vertex) for vertex in vertices)
                weld_groups.extend([instance_id] * len(vertices))
                triangles.extend(tuple(base + x for x in ids[i:i+3]) for i in range(0, len(ids), 3))
                material = primitive.get("material", -1)
                if material >= len(document.get("materials", [])) or material < -1:
                    raise BlueprintError("invalid GLB material index")
                used_materials.add(material)
            instances.append({"node": index, "mesh": node["mesh"], "name": node.get("name", ""),
                              "matrix": [rounded(x) for x in world],
                              "exported_vertex_count": len(positions)-first_vertex,
                              "triangle_count": len(triangles)-first_triangle})
        for child in node.get("children", []):
            visit(child, world, ancestors | {index})

    for root in scenes[scene].get("nodes", []):
        visit(root, IDENTITY, set())
    if not triangles or any(not math.isfinite(x) for p in positions for x in p):
        raise BlueprintError("empty or non-finite manufacturing GLB")
    texture_ids = set()

    def material_textures(value):
        if not isinstance(value, dict):
            return
        for key, child in value.items():
            if key.endswith("Texture") and isinstance(child, dict) and "index" in child:
                texture_ids.add(child["index"])
            material_textures(child)

    for material in used_materials:
        if material >= 0:
            material_textures(document["materials"][material])
    image_ids = {document["textures"][index]["source"] for index in texture_ids}
    texture_bytes = 0
    for index in sorted(image_ids):
        image = document["images"][index]
        if "bufferView" not in image:
            raise BlueprintError("external textures need an explicit hashed dependency")
        texture_bytes += document["bufferViews"][image["bufferView"]]["byteLength"]
    return {"positions": positions, "triangles": triangles, "weld_groups": weld_groups,
            "instances": instances, "material_count": len(used_materials),
            "texture_bytes": texture_bytes, "materials": document.get("materials", [])}


def read_glb(path):
    data = path.read_bytes()
    if len(data) < 20 or struct.unpack_from("<III", data) != (0x46546c67, 2, len(data)):
        raise BlueprintError("invalid GLB header: " + str(path))
    chunks, offset = {}, 12
    while offset + 8 <= len(data):
        size, kind = struct.unpack_from("<II", data, offset)
        if offset + 8 + size > len(data) or kind in chunks:
            raise BlueprintError("invalid GLB chunks")
        chunks[kind] = data[offset+8:offset+8+size]
        offset += 8 + size
    if offset != len(data) or 0x4e4f534a not in chunks or 0x004e4942 not in chunks:
        raise BlueprintError("GLB needs JSON and BIN chunks")
    result = flatten_glb(json.loads(chunks[0x4e4f534a]), chunks[0x004e4942])
    result["sha256"] = sha256(data)
    return result


def read_rmesh(path):
    data = path.read_bytes()
    if len(data) < 64 or data[:4] != b"RFM2":
        raise BlueprintError("invalid runtime mesh: " + str(path))
    h = struct.unpack_from("<16I", data)
    if not 2 <= h[1] <= 15 or not h[2] or not h[3] or h[3] % 3 or not h[4]:
        raise BlueprintError("invalid runtime mesh version/counts")
    vertex_size = 36 if h[1] >= 10 else 32 if h[1] >= 6 else 24
    material_size = 40 if h[1] >= 9 else 24 if h[1] >= 8 else 16
    start = h[14] + h[12] * material_size
    end = start + h[2]*vertex_size + h[3]*4
    if h[13] != 64 or h[14] != 64+h[11]*16 or end > len(data):
        raise BlueprintError("invalid runtime mesh layout")
    positions = [struct.unpack_from("<3i", data, start+i*vertex_size) for i in range(h[2])]
    ids = struct.unpack_from("<" + str(h[3]) + "I", data, start+h[2]*vertex_size)
    if any(x >= len(positions) for x in ids):
        raise BlueprintError("invalid runtime mesh indices")
    bounds = [min(p[k] for p in positions) for k in range(3)] + [max(p[k] for p in positions) for k in range(3)]
    if tuple(bounds) != struct.unpack_from("<6i", data, 20):
        raise BlueprintError("runtime mesh bounds do not match its vertices")
    texture_hashes = {}
    for i in range(h[12]):
        texture = struct.unpack_from("<I", data, h[14]+i*material_size+8)[0]
        if texture != 0xffffffff:
            texture_path = path.with_suffix(".textures") / ("texture_%03d.ttex" % texture)
            # Legacy v2 weapons carry texture slot zero even when their GLB has
            # no textures; runtime intentionally falls back to material color.
            # Preserve absence in the fingerprint so adding a file invalidates
            # the cache as surely as modifying a real texture does.
            texture_hashes[texture_path.name] = (sha256(texture_path.read_bytes())
                                                if texture_path.exists() else None)
    return {"positions": positions, "triangles": [tuple(ids[i:i+3]) for i in range(0, len(ids), 3)],
            "sha256": sha256(data), "position_scale": h[4], "bounds": bounds,
            "material_count": h[12], "texture_hashes": texture_hashes}


def edges_of(triangle):
    return zip(triangle, triangle[1:] + triangle[:1])


def subtract(a, b):
    return tuple(a[k] - b[k] for k in range(3))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def dot(a, b):
    return sum(a[k]*b[k] for k in range(3))


def topology_stats(positions, triangles, weld_groups=None):
    """Measure closed shells without mistaking edge-touching solids for one shell.

    Exact duplicate coordinates are welded across material/normal/UV seams only
    within each node instance.  Adjacency crosses only two-face edges.  Every
    resulting shell must independently have two opposite directed uses per edge;
    an open/non-manifold shell invalidates its volume, rather than being capped.
    Closed orientable shells can be consistently oriented for measurement only;
    every flipped triangle is reported and the source asset remains untouched.
    Whole-shell inward winding is valid and its signed volume is made positive.
    Overlap between closed shells is not subtracted: this is an explicit sum,
    never a claim of exact boolean-union volume.
    """
    if not positions or not triangles:
        raise BlueprintError("empty manufacturing mesh")
    groups = weld_groups if weld_groups is not None else [0] * len(positions)
    if (len(groups) != len(positions) or
            any(len(p) != 3 or any(not math.isfinite(x) for x in p) for p in positions) or
            any(len(t) != 3 or any(not 0 <= x < len(positions) for x in t) for t in triangles)):
        raise BlueprintError("invalid manufacturing vertex/index input")
    mapping, vertices, remap = {}, [], []
    for group, position in zip(groups, positions):
        key = (group, tuple(position))
        if key not in mapping:
            mapping[key] = len(vertices)
            vertices.append(position)
        remap.append(mapping[key])
    welded = [tuple(remap[x] for x in triangle) for triangle in triangles]
    edges, degenerate = defaultdict(list), set()
    for index, triangle in enumerate(welded):
        a, b, c = [vertices[x] for x in triangle]
        if len(set(triangle)) < 3 or dot(cross(subtract(b, a), subtract(c, a)), cross(subtract(b, a), subtract(c, a))) == 0:
            degenerate.add(index)
            continue
        for a, b in edges_of(triangle):
            edges[tuple(sorted((a, b)))].append(index)
    adjacency = [[] for _ in welded]
    for faces in edges.values():
        if len(faces) == 2:
            adjacency[faces[0]].append(faces[1])
            adjacency[faces[1]].append(faces[0])
    visited, shells = set(degenerate), []
    for index in range(len(welded)):
        if index in visited:
            continue
        stack, faces = [index], []
        visited.add(index)
        while stack:
            face = stack.pop()
            faces.append(face)
            for other in adjacency[face]:
                if other not in visited:
                    visited.add(other)
                    stack.append(other)
        local_edges, directions, edge_faces = Counter(), Counter(), defaultdict(list)
        # Recenter the determinant to avoid cancellation far from the origin.
        origin = vertices[welded[faces[0]][0]]
        terms = {}
        for face in faces:
            triangle = welded[face]
            a, b, c = [subtract(vertices[x], origin) for x in triangle]
            terms[face] = dot(a, cross(b, c)) / 6
            for a, b in edges_of(triangle):
                edge = tuple(sorted((a, b)))
                local_edges[edge] += 1
                directions[edge] += 1 if a < b else -1
                edge_faces[edge].append((face, a < b))
        closed = all(count == 2 for count in local_edges.values())
        oriented = all(value == 0 for value in directions.values())
        flips, orientable = {faces[0]: False}, closed
        if closed:
            neighbors = defaultdict(list)
            for uses in edge_faces.values():
                (a, da), (b, db) = uses
                neighbors[a].append((b, da == db))
                neighbors[b].append((a, da == db))
            pending = [faces[0]]
            while pending:
                face = pending.pop()
                for other, must_flip in neighbors[face]:
                    value = flips[face] ^ must_flip
                    if other in flips:
                        if flips[other] != value:
                            orientable = False
                    else:
                        flips[other] = value
                        pending.append(other)
        signed_volume = math.fsum(value * (-1 if flips.get(face) else 1) for face, value in terms.items())
        valid = closed and orientable and abs(signed_volume) > 0
        shells.append({"triangle_count": len(faces), "closed": closed, "consistent_winding": oriented,
                       "orientable": orientable,
                       "measurement_flipped_triangles": sum(flips.values()) if orientable else 0,
                       "inward_winding": signed_volume < 0, "valid_volume": valid,
                       "volume_m3": abs(signed_volume) if valid else None,
                       "boundary_edges": sum(x == 1 for x in local_edges.values()),
                       "nonmanifold_edges": sum(x > 2 for x in local_edges.values())})
    valid = not degenerate and bool(shells) and all(shell["valid_volume"] for shell in shells)
    return {"vertex_count": len(vertices), "exported_vertex_count": len(positions),
            "triangle_count": len(triangles), "degenerate_triangles": len(degenerate),
            "global_boundary_edges": sum(len(faces) == 1 for faces in edges.values()),
            "global_nonmanifold_edges": sum(len(faces) > 2 for faces in edges.values()),
            "shell_count": len(shells), "inward_shell_count": sum(shell["inward_winding"] for shell in shells),
            "measurement_flipped_triangles": sum(shell["measurement_flipped_triangles"] for shell in shells),
            "volume_valid": valid,
            "volume_m3": math.fsum(shell["volume_m3"] for shell in shells) if valid else None,
            "shells": shells}


def adapter_data(profile, runtime):
    result = dict(profile)
    bounds = runtime["bounds"]
    axis = 0 if profile["asset_basis"] == 1 else 2
    extent = bounds[axis+3] - bounds[axis]
    if extent <= 0:
        raise BlueprintError("zero-length runtime gun")
    scale_milli = (profile["length_mm"]*512 + extent//2)//extent
    result.update(runtime_position_scale=runtime["position_scale"], scale_milli=scale_milli,
                  presented_length_mm=extent*scale_milli/512,
                  source_units_to_m=runtime["position_scale"]*scale_milli/512000)
    return result


def canonical_point(point, basis):
    if basis == 1:
        return point[2], point[1], point[0]
    if basis == 2:
        return -point[0], point[1], -point[2]
    return tuple(point)


def bounds_dimensions(positions):
    return [max(p[k] for p in positions)-min(p[k] for p in positions) for k in range(3)]


def float32(value):
    return struct.unpack("<f", struct.pack("<f", value))[0]


def validate_source_runtime(source, runtime):
    if source["triangles"] != runtime["triangles"] or len(source["positions"]) != len(runtime["positions"]):
        raise BlueprintError("source/runtime mesh topology differs; re-export the full manufacturing asset")
    # Legacy glb2rmesh uses single-precision multiplication and symmetric rounding.
    scale = runtime["position_scale"]
    for index, (source_point, runtime_point) in enumerate(zip(source["positions"], runtime["positions"])):
        expected = tuple(int(float32(float32(value*scale) + (-.5 if value < 0 else .5))) for value in source_point)
        if expected != runtime_point:
            raise BlueprintError("source/runtime position mismatch at vertex %d; re-export the source" % index)
    if source["material_count"] != runtime["material_count"]:
        raise BlueprintError("source/runtime material counts differ")


def blueprint(root, catalog, profile):
    identifier, _, label, source_name = catalog
    source_path = Path(".claude/glb") / source_name
    source = read_glb(root / source_path)
    runtime = read_rmesh(root / profile["runtime_path"])
    validate_source_runtime(source, runtime)
    adapter = adapter_data(profile, runtime)
    positions = [canonical_point(tuple(x*adapter["source_units_to_m"] for x in p), adapter["asset_basis"])
                 for p in source["positions"]]
    # Weld before the metre transform so that arithmetic does not split exact
    # authored duplicates.  Every authored duplicate gets the same transform.
    stats = topology_stats(positions, source["triangles"], source["weld_groups"])
    runtime_positions = [canonical_point(tuple(x*adapter["scale_milli"]/512000 for x in p), adapter["asset_basis"])
                         for p in runtime["positions"]]
    runtime_stats = topology_stats(runtime_positions, runtime["triangles"])
    dimensions = bounds_dimensions(positions)
    workspace_metres, manufacturing_yaw = manufacturing_layout(root)
    angle = math.radians(manufacturing_yaw)
    rotated_dimensions = [abs(math.cos(angle))*dimensions[0] + abs(math.sin(angle))*dimensions[2],
                          dimensions[1],
                          abs(math.sin(angle))*dimensions[0] + abs(math.cos(angle))*dimensions[2]]
    size_supported = max(rotated_dimensions) <= workspace_metres
    unsupported = (1 if stats["global_boundary_edges"] else 2) if not stats["volume_valid"] else 0
    unsupported |= 4 if not size_supported else 0
    reasons = (["open manufacturing mesh" if unsupported & 1 else "invalid manufacturing volume"] if unsupported & 3 else [])
    if unsupported & 4:
        reasons.append("does not fit the work volume at the manufacturing orientation")
    result = {"id": identifier, "name": label, "weapon": profile["weapon"], "weapon_enum": profile["weapon_enum"],
              "source_path": source_path.as_posix(), "source_sha256": source["sha256"],
              "runtime_path": profile["runtime_path"], "runtime_sha256": runtime["sha256"],
              "runtime_texture_sha256": runtime["texture_hashes"],
              "adapter": adapter, "adapter_sha256": fingerprint(adapter),
              "instances": source["instances"], "material_count": source["material_count"],
              "texture_bytes": source["texture_bytes"], "materials": source["materials"],
              "bounds_m": dimensions, "runtime_bounds_m": bounds_dimensions(runtime_positions),
              "manufacturing_yaw_degrees": manufacturing_yaw,
              "rotated_bounds_m": rotated_dimensions, "workspace_edge_m": workspace_metres,
              "bounds_mm": [math.ceil(x*1000) for x in dimensions],
              "volume_mm3": half_up(stats["volume_m3"]*1e9) if stats["volume_valid"] else 0,
              "statistics": stats,
              "runtime_quantization": {key: runtime_stats[key] for key in
                                       ("vertex_count", "exported_vertex_count", "triangle_count", "degenerate_triangles")},
              "volume_method": "sum of absolute signed-tetrahedron volumes of independently closed oriented shells",
              "volume_caveat": "overlapping shell volume is counted more than once; this is not exact boolean-union volume",
              "manufacturable": stats["volume_valid"],
              "size_supported": size_supported, "unsupported_reasons": unsupported,
              "unsupported_reason": "; ".join(reasons)}
    return result


def generate(root):
    profiles = calibration_profiles(root)
    return {"schema": SCHEMA, "algorithm": ALGORITHM,
            "generator_sha256": generator_fingerprint(),
            "vertex_count_policy": "exact POSITION coordinates welded across UV/normal/material seams within each scene node instance; distinct instances are counted separately",
            "triangle_count_policy": "all triangles of every primitive of every active-scene instance in the full source mesh; no render LOD",
            "volume_policy": "closed orientable shells only; split adjacency at edges with more than two incident faces; verify every resulting shell independently; repair inconsistent winding only in the measurement and report the flipped triangles; take absolute shell volume to handle mirrored/inward winding; sum may overestimate overlap",
            "scale_policy": "source GLB coordinates times runtime position_scale times the existing quantized physical adapter scale_milli / 512000; bounds include the resulting actual metre scale",
            "blueprints": [blueprint(root, item, profiles[item[1]]) for item in CATALOG]}


def c_string(value):
    return json.dumps(value, ensure_ascii=True)


def generated_header(document):
    lines = ["/* Generated by tools/mesh_weaver_blueprints.py; do not edit. */",
             "#ifndef RF_WEAVER_BLUEPRINTS_GENERATED_H", "#define RF_WEAVER_BLUEPRINTS_GENERATED_H", "",
             '#include "toy_game.h"', '#include "toy_mesh_weaver.h"', "",
             "#define RF_WEAVER_BLUEPRINT_OPEN_VOLUME 1u",
             "#define RF_WEAVER_BLUEPRINT_INVALID_VOLUME 2u",
             "#define RF_WEAVER_BLUEPRINT_SIZE_LIMIT 4u", "",
             "struct rf_weaver_blueprint {", "    struct toy_mesh_blueprint geometry;",
             "    const char *id, *name, *model_path;", "    const char *source_sha256, *runtime_sha256;",
             "    unsigned exported_vertex_count, material_count;", "    int manufacturable, size_supported;",
             "    unsigned unsupported_reasons;", "    const char *unsupported_reason;", "};", "",
             "static const struct rf_weaver_blueprint rf_weaver_blueprints[] = {"]
    for row in document["blueprints"]:
        stats = row["statistics"]
        lines += ["    {", "        {%s, %du, %du, %dULL, {%s}, %dULL}," %
                  (row["weapon_enum"], stats["vertex_count"], stats["triangle_count"], row["volume_mm3"],
                   ", ".join(str(x)+"u" for x in row["bounds_mm"]), row["texture_bytes"]),
                  "        %s, %s, %s," % tuple(c_string(row[k]) for k in ("id", "name", "runtime_path")),
                  "        %s, %s," % (c_string(row["source_sha256"]), c_string(row["runtime_sha256"])),
                  "        %du, %du, %d, %d," % (stats["exported_vertex_count"], row["material_count"],
                                                row["manufacturable"], row["size_supported"]),
                  "        %du, %s" % (row["unsupported_reasons"], c_string(row["unsupported_reason"])), "    },"]
    lines += ["};", "", "#define RF_WEAVER_BLUEPRINT_COUNT (sizeof(rf_weaver_blueprints) / sizeof(rf_weaver_blueprints[0]))", "",
              "#endif", ""]
    return "\n".join(lines)


def check(root, document, header):
    if document.get("schema") != SCHEMA or document.get("algorithm") != ALGORITHM:
        raise BlueprintError("blueprint schema/algorithm changed; regenerate")
    if document.get("generator_sha256") != generator_fingerprint():
        raise BlueprintError("blueprint generator changed; regenerate with the original source assets")
    profiles = calibration_profiles(root)
    workspace_metres, manufacturing_yaw = manufacturing_layout(root)
    rows = document.get("blueprints", [])
    if [row.get("id") for row in rows] != [item[0] for item in CATALOG]:
        raise BlueprintError("blueprint catalog changed; regenerate")
    source_count = 0
    for row, item in zip(rows, CATALOG):
        profile = profiles[item[1]]
        if (row["source_path"] != (Path(".claude/glb") / item[3]).as_posix() or
                row["runtime_path"] != profile["runtime_path"] or row["weapon"] != profile["weapon"] or
                row["weapon_enum"] != profile["weapon_enum"]):
            raise BlueprintError("%s source/runtime identity changed; regenerate" % row["id"])
        if row["workspace_edge_m"] != workspace_metres or row["manufacturing_yaw_degrees"] != manufacturing_yaw:
            raise BlueprintError("%s machine envelope/orientation changed; regenerate" % row["id"])
        runtime = read_rmesh(root / profile["runtime_path"])
        if runtime["sha256"] != row["runtime_sha256"] or runtime["texture_hashes"] != row["runtime_texture_sha256"]:
            raise BlueprintError("%s runtime mesh/texture changed; regenerate" % row["id"])
        adapter = adapter_data(profile, runtime)
        if adapter != row["adapter"] or fingerprint(adapter) != row["adapter_sha256"]:
            raise BlueprintError("%s physical adapter changed; regenerate" % row["id"])
        source_path = root / row["source_path"]
        if source_path.exists():
            source_count += 1
            if sha256(source_path.read_bytes()) != row["source_sha256"]:
                raise BlueprintError("%s source asset changed; regenerate" % row["id"])
            if blueprint(root, item, profile) != row:
                raise BlueprintError("%s cached statistics differ from their source; regenerate" % row["id"])
    if header != generated_header(document):
        raise BlueprintError("generated C blueprint header is stale; regenerate")
    return source_count


def write_utf8(path, text):
    path.parent.mkdir(parents=True, exist_ok=True)
    # Preserve existing BOM/newline style. New generated files use UTF-8 LF.
    old = path.read_bytes() if path.exists() else b""
    newline = "\r\n" if b"\r\n" in old else "\n"
    encoding = "utf-8-sig" if old.startswith(b"\xef\xbb\xbf") else "utf-8"
    path.write_bytes(text.replace("\n", newline).encode(encoding))


def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--check", action="store_true", help="verify hashes, adapter and generated statistics without writing")
    parser.add_argument("--root", type=Path, default=ROOT, help="repository root (normally inferred)")
    args = parser.parse_args(argv)
    try:
        root = args.root.resolve()
        if args.check:
            document = json.loads((root / JSON_PATH).read_text(encoding="utf-8-sig"))
            count = check(root, document, (root / HEADER_PATH).read_text(encoding="utf-8-sig"))
            print("mesh-weaver-blueprints: checked %d runtime assets/adapters and %d available source assets" % (len(document["blueprints"]), count))
        else:
            document = generate(root)
            write_utf8(root / JSON_PATH, json.dumps(document, ensure_ascii=False, indent=2) + "\n")
            write_utf8(root / HEADER_PATH, generated_header(document))
            for row in document["blueprints"]:
                stats = row["statistics"]
                print("%s: Nv=%d (export=%d) Nt=%d volume=%.6f cm3 shells=%d usable=%s size=%s" %
                      (row["id"], stats["vertex_count"], stats["exported_vertex_count"], stats["triangle_count"],
                       row["volume_mm3"]/1000, stats["shell_count"], row["manufacturable"], row["size_supported"]))
        return 0
    except (BlueprintError, OSError, KeyError, IndexError, struct.error, json.JSONDecodeError) as error:
        print("mesh-weaver-blueprints: ERROR: " + str(error), file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
