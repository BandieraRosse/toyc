#!/usr/bin/env python3
"""Build/audit the lab's original static power and RF1 data service harness.

Uses existing native asset tools; never builds or launches the game. The cable
layout is site-specific and preserves the machine origin in its static mesh.
"""
import argparse
import hashlib
import json
import math
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
IDENTITY = "rf_mesh_weaver_service_links"
PUBLIC = ROOT/"rasterfall/assets/models/props/mesh_weaver"
SOURCE = ROOT/"rasterfall/private-assets/source/props/mesh_weaver"
MANIFEST = ROOT/"tools/assets/manifests/props/mesh_weaver"/(IDENTITY+".asset.json")
LAYOUT = PUBLIC/"mesh_weaver_service_links.layout.json"


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True)


def read_mesh(path, origin=(0, 0, 0), size=1):
    data = path.read_bytes()
    assert data[:4] == b"RFM2", path
    vertices, indices, scale = struct.unpack_from("<III", data, 8)
    bounds = struct.unpack_from("<6i", data, 20)
    primitives, materials, _, material_offset = struct.unpack_from("<4I", data, 44)
    vertex_offset = material_offset+materials*16
    index_offset = vertex_offset+vertices*24
    assert index_offset+indices*4 == len(data)
    raw = [struct.unpack_from("<3i", data, vertex_offset+i*24) for i in range(vertices)]
    points = [tuple(v[k]/scale*size+origin[k] for k in range(3)) for v in raw]
    faces = struct.unpack_from("<%dI" % indices, data, index_offset)
    assert max(faces) < vertices
    return {"positions": points, "raw": raw, "faces": faces, "scale": scale,
        "bounds": bounds, "primitives": primitives, "materials": materials,
        "sha256": hashlib.sha256(data).hexdigest()}


def sub(a, b):
    return tuple(a[k]-b[k] for k in range(3))


def dot(a, b):
    return sum(x*y for x, y in zip(a, b))


def cross(a, b):
    return (a[1]*b[2]-a[2]*b[1], a[2]*b[0]-a[0]*b[2], a[0]*b[1]-a[1]*b[0])


def distance(a, b):
    return math.sqrt(dot(sub(a, b), sub(a, b)))


def segment_distance(a, b, c, d):
    u, v, w = sub(b, a), sub(d, c), sub(a, c)
    aa, bb, cc, dd, ee = dot(u, u), dot(u, v), dot(v, v), dot(u, w), dot(v, w)
    determinant = aa*cc-bb*bb
    s = max(0, min(1, (bb*ee-cc*dd)/determinant)) if determinant > 1e-16 else 0
    t = (bb*s+ee)/cc
    if t < 0:
        t, s = 0, max(0, min(1, -dd/aa))
    elif t > 1:
        t, s = 1, max(0, min(1, (bb-dd)/aa))
    return distance(tuple(a[k]+s*u[k] for k in range(3)), tuple(c[k]+t*v[k] for k in range(3)))


def intersection_count(mesh, start, end):
    direction = sub(end, start)
    positions, faces = mesh["positions"], mesh["faces"]
    count = 0
    for i in range(0, len(faces), 3):
        a, b, c = (positions[n] for n in faces[i:i+3])
        e1, e2 = sub(b, a), sub(c, a)
        p = cross(direction, e2)
        determinant = dot(e1, p)
        if abs(determinant) < 1e-12:
            continue
        s = sub(start, a)
        u = dot(s, p)/determinant
        if u < -1e-7 or u > 1+1e-7:
            continue
        q = cross(s, e1)
        v = dot(direction, q)/determinant
        t = dot(e2, q)/determinant
        count += v >= -1e-7 and u+v <= 1+1e-7 and -1e-7 <= t <= 1+1e-7
    return count


def audit():
    layout = json.loads(LAYOUT.read_text(encoding="utf-8"))
    mesh = read_mesh(PUBLIC/(IDENTITY+".rmesh"))
    assert mesh["scale"] == 232 and mesh["materials"] == 4
    assert len(mesh["faces"])//3 == layout["triangles"] <= 2400
    raw = mesh["raw"]
    bounds = tuple(min(p[i] for p in raw) for i in range(3))+tuple(max(p[i] for p in raw) for i in range(3))
    assert bounds == mesh["bounds"]
    assert bounds[1] == 0, "floor clamp feet must meet the authored lab ground"
    for i in range(0, len(mesh["faces"]), 3):
        a, b, c = (raw[n] for n in mesh["faces"][i:i+3])
        assert cross(sub(b, a), sub(c, a)) != (0, 0, 0), ("quantized degenerate", i//3)
    power, data = (layout["routes"][key] for key in ("power", "data"))
    gap = min(segment_distance(a, b, c, d)
        for a, b in zip(power["centerline_m"], power["centerline_m"][1:])
        for c, d in zip(data["centerline_m"], data["centerline_m"][1:]))-power["radius_m"]-data["radius_m"]
    assert gap > .025, ("cable crossing clearance", gap)
    # Every point of the service route stays behind the machine. At the only
    # line crossing the power lead is still at socket height, above the data.
    for route in (power, data):
        assert all(p[2] <= -.39 and p[1]-route["radius_m"] >= .006 for p in route["centerline_m"])
    assert max(p[2] for p in mesh["positions"] if abs(p[0]) < 1.2) < -.88
    # These short segments lie inside new connector hardware and intersect
    # real target triangles after the exact map transforms, proving contact.
    frame = read_mesh(PUBLIC/"rf_mesh_weaver_frame.rmesh")
    case = read_mesh(ROOT/"rasterfall/assets/models/props/lab/rf_lab_computer_case.rmesh",
                     (-1600/512, 578/512, -30/512))
    unit = read_mesh(ROOT/"rasterfall/assets/models/props/industrial/rf_power_unit.rmesh",
                     (1600/512, 0, -600/512), .350)
    x, y, z = layout["contacts_m"]["rf1_data"]
    probes = {
        "machine_power_collar": (frame, (-.040, .162, -.932), (-.040, .162, -.900)),
        "machine_data_nose": (frame, (.097, .162, -.938), (.097, .162, -.887)),
        "rf1_data_nose": (case, (x, y, z-.021), (x, y, z+.011)),
        "power_box_to_existing_shell": (unit, (2.300, .220, -1.265), (2.371, .220, -1.265)),
        "rf1_lower_clip_to_apron": (read_mesh(ROOT/"rasterfall/assets/models/props/lab/rf_lab_computer_stand.rmesh",
            (-1280/512, 0, 0)), (x, 1.023, -.400), (x, 1.023, -.361)),
        "rf1_upper_clip_to_case": (case, (x, 1.405, -.420), (x, 1.405, -.385))}
    contacts = {name: intersection_count(target, a, b) for name, (target, a, b) in probes.items()}
    assert all(contacts.values()), ("connector not touching actual target", contacts)
    # Guard the placement contract, without changing parser or gameplay state.
    from mesh_weaver_lab import generate
    map_text = generate()
    for row in ("kind=mesh_weaver_frame x=0 y=0 z=0 yaw=0 scale=1000",
                "kind=power_unit x=1600 y=0 z=-600 yaw=0 scale=350",
                "kind=lab_computer_case x=-320 y=578 z=-30 yaw=0 scale=1000"):
        assert row in map_text, ("service harness map anchor changed", row)
    layout["runtime"] = {"position_scale": mesh["scale"], "bounds_local": [list(bounds[:3]), list(bounds[3:])],
        "triangles": len(mesh["faces"])//3, "vertices": len(raw), "materials": mesh["materials"],
        "primitives": mesh["primitives"], "sha256": mesh["sha256"],
        "degenerate_triangles": 0, "cable_to_cable_gap_m": round(gap, 6),
        "actual_target_triangle_contacts": contacts,
        "front_pickup_route_clear": True, "independent_static_prop": True}
    LAYOUT.write_text(json.dumps(layout, indent=2)+"\n", encoding="utf-8", newline="\n")
    print(json.dumps(layout["runtime"], indent=2), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", type=Path, default=Path("E:/Blender 5.2/blender.exe"))
    parser.add_argument("--tool-dir", type=Path, default=ROOT/"build-windows")
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()
    if not args.audit_only:
        PUBLIC.mkdir(parents=True, exist_ok=True)
        MANIFEST.parent.mkdir(parents=True, exist_ok=True)
        run([args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
            "--python", ROOT/"tools/blender/generate_mesh_weaver_service_links.py", "--",
            "--output", SOURCE, "--layout", LAYOUT])
        MANIFEST.write_text(json.dumps({"schema": 1, "id": IDENTITY, "type": "static_prop",
            "source": "../../../../../rasterfall/private-assets/source/props/mesh_weaver/"+IDENTITY+".glb"},
            indent=2)+"\n", encoding="utf-8", newline="\n")
        run([sys.executable, ROOT/"tools/assets/import_asset.py", "--no-build", "--tool-dir",
            args.tool_dir.resolve(), "--force", "--output-root", PUBLIC, MANIFEST])
    audit()


if __name__ == "__main__":
    main()
