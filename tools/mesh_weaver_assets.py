#!/usr/bin/env python3
"""Rebuild and audit the RF Mesh Weaver's independently pivoted rigid assets.

Requires an existing native asset-tool build; this script never builds the game.
Source GLB/Blend remains private. Public RMESH, manifests and mechanical layout
are derived from the same Blender generator and contain no external content.
"""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
PARTS = ("base", "column", "tray", "terminal", "emitter_mount",
         "emitter_yoke", "emitter_core", "emitter_petal", "frame")
PUBLIC = ROOT/"rasterfall/assets/models/props/mesh_weaver"
SOURCE = ROOT/"rasterfall/private-assets/source/props/mesh_weaver"
MANIFESTS = ROOT/"tools/assets/manifests/props/mesh_weaver"
LAYOUT = PUBLIC/"mesh_weaver.components.json"
HEADER = ROOT/"rasterfall/include/rf_mesh_weaver_layout_generated.h"


def run(command):
    print("+", " ".join(map(str, command)), flush=True)
    subprocess.run(list(map(str, command)), cwd=ROOT, check=True)


def audit(layout_path=LAYOUT, asset_dir=PUBLIC):
    layout = json.loads(layout_path.read_text(encoding="utf-8"))
    assert len(layout["emitters"]) == 8
    assert len([row for row in layout["fixed_instances"] if row["part"] == "column"]) == 4
    for column in range(4):
        assert {e["level"] for e in layout["emitters"] if e["column"] == column} == {"lower", "upper"}
    for name in PARTS:
        path = asset_dir/("rf_mesh_weaver_"+name+".rmesh")
        data = path.read_bytes()
        assert data[:4] == b"RFM2", path
        vertices, indices, scale = struct.unpack_from("<III", data, 8)
        bounds = struct.unpack_from("<6i", data, 20)
        primitives, materials, primitive_offset, material_offset = struct.unpack_from("<4I", data, 44)
        assert scale > 0 and indices % 3 == 0 and 1 <= materials <= 6
        vertex_offset = material_offset+materials*16
        index_offset = vertex_offset+vertices*24
        assert index_offset+indices*4 == len(data), (name, "unexpected RFM2 v2 layout")
        quantized = [struct.unpack_from("<3i", data, vertex_offset+i*24) for i in range(vertices)]
        assert tuple(min(p[i] for p in quantized) for i in range(3))+tuple(max(p[i] for p in quantized) for i in range(3)) == bounds
        reference = layout["components"][name]
        expected = reference["bounds_m"][0]+reference["bounds_m"][1]
        assert max(abs(bounds[i]/scale-expected[i]) for i in range(6)) <= 1.1/scale, (name, "pivot/bounds changed")
        assert indices//3 == reference["triangles"], (name, "triangle count changed")
        triangle_indices = struct.unpack_from("<%dI" % indices, data, index_offset)
        assert all(i < vertices for i in triangle_indices)
        if name == "tray":
            support = layout["tray_motion"]["support_surface"]
            height = int(support["height_m"]*scale+.5)
            low, high = support["bounds_xz_m"]
            support_faces = []
            for i in range(0, indices, 3):
                points = [quantized[n] for n in triangle_indices[i:i+3]]
                if all(p[1] == height and low[0]-1/scale <= p[0]/scale <= high[0]+1/scale
                       and low[1]-1/scale <= p[2]/scale <= high[1]+1/scale for p in points):
                    support_faces.append(points)
            assert len(support_faces) >= 6, "tray contact plane missing from runtime geometry"
            support["runtime_height_m"] = height/scale
            support["runtime_height_local"] = height
            support["runtime_position_scale"] = scale
            support["runtime_contact_triangles"] = len(support_faces)
            audit_tray_contacts(support, support_faces, scale)
        degenerate = 0
        collapsed_centers = []
        for i in range(0, indices, 3):
            a, b, c = (quantized[n] for n in triangle_indices[i:i+3])
            u = [b[k]-a[k] for k in range(3)]
            v = [c[k]-a[k] for k in range(3)]
            collapsed = all(u[(k+1) % 3]*v[(k+2) % 3]-u[(k+2) % 3]*v[(k+1) % 3] == 0 for k in range(3))
            degenerate += collapsed
            if collapsed:
                collapsed_centers.append([round((a[k]+b[k]+c[k])/(3*scale), 4) for k in range(3)])
        # Tiny components deliberately retain the converter's finer local unit.
        # Consumers must not apply the static prop registry's fixed 232 scale.
        reference["runtime"] = {"file": path.name, "vertices": vertices,
            "triangles": indices//3, "primitives": primitives,
            "materials": materials, "position_scale": scale,
            "quantization_degenerate_triangles": degenerate,
            "bounds_local": [list(bounds[:3]), list(bounds[3:])],
            "sha256": hashlib.sha256(data).hexdigest()}
        print("%-16s %5d tris %5d vertices %5d units/m %3d collapsed" % (name, indices//3, vertices, scale, degenerate))
        if degenerate:
            print("  Collapsed face centers:", collapsed_centers[:16])
        assert degenerate == 0, (name, "triangles collapsed during integer quantization")
    counts = {name: 0 for name in PARTS}
    for row in layout["fixed_instances"]:
        counts[row["part"]] += 1
    counts.update(emitter_yoke=8, emitter_core=8, emitter_petal=24)
    layout["assembly_metrics"] = {"instances_by_component": counts,
        "triangles": sum(counts[name]*layout["components"][name]["triangles"] for name in PARTS),
        "columns": 4, "emitters": 8, "petals": 24}
    layout_path.write_text(json.dumps(layout, indent=2)+"\n", encoding="utf-8", newline="\n")
    print("Assembly:", layout["assembly_metrics"], flush=True)
    write_header(layout)


def audit_tray_contacts(support, triangles, scale):
    # Match the shared weapon adapter and product yaw, then test the actual
    # lowest runtime vertices against the quantized contact triangles.
    from mesh_weaver_blueprints import (adapter_data, calibration_profiles,
        canonical_point, manufacturing_layout, read_rmesh)
    import math

    profiles = calibration_profiles(ROOT)
    angle = math.radians(manufacturing_layout(ROOT)[1])
    cosine, sine = math.cos(angle), math.sin(angle)

    def covers(point, triangle):
        signed = []
        for i in range(3):
            a, b = triangle[i], triangle[(i+1) % 3]
            signed.append((b[0]-a[0])*(point[1]*scale-a[2])
                          -(b[2]-a[2])*(point[0]*scale-a[0]))
        return min(signed) >= -1e-7 or max(signed) <= 1e-7

    contacts = {}
    for weapon in ("AK", "PISTOL", "SMG", "SHOTGUN"):
        profile = profiles[weapon]
        runtime = read_rmesh(ROOT/profile["runtime_path"])
        adapter = adapter_data(profile, runtime)
        center = [int((runtime["bounds"][i]+runtime["bounds"][i+3])/2) for i in range(3)]
        canonical = [canonical_point(tuple((p[i]-center[i])*adapter["scale_milli"]/512000
            for i in range(3)), adapter["asset_basis"]) for p in runtime["positions"]]
        vertices = [tuple(int(v*8192+(.5 if v >= 0 else -.5))/8192 for v in p)
                    for p in canonical]
        minimum = min(v[1] for v in vertices)
        points = {(cosine*v[0]+sine*v[2], -sine*v[0]+cosine*v[2])
                  for v in vertices if v[1] <= minimum+1/8192}
        assert points and all(any(covers(p, t) for t in triangles) for p in points), \
            (weapon, "lowest vertices lie outside the actual tray support plane")
        contacts[weapon.lower()] = {"minimum_y_m": minimum, "contact_vertices": len(points),
            "contact_bounds_xz_m": [[min(p[k] for p in points) for k in range(2)],
                                     [max(p[k] for p in points) for k in range(2)]]}
    support["verified_weapon_contacts"] = contacts
    print("Tray: %.9f m runtime contact plane, all four supported weapons contact its triangles" %
          support["runtime_height_m"])


def write_header(layout):
    def values(numbers):
        return ", ".join(format(float(n), ".9g") for n in numbers)

    k = layout["kinematics"]
    lines = ["/* Generated by tools/mesh_weaver_assets.py; edit its Blender source.",
        " * Physical metres, RF X-right/Y-up/+Z-front. Rotations use degrees.",
        " * No pivot correction or floor recentering is applied by the importer. */",
        "#ifndef RF_MESH_WEAVER_LAYOUT_GENERATED_H",
        "#define RF_MESH_WEAVER_LAYOUT_GENERATED_H", "",
        "#define RF_MESH_WEAVER_HEAD_COUNT 8",
        "struct rf_mesh_weaver_head_layout {",
        "    double position_m[3];",
        "    double yaw_degrees, idle_pitch_degrees;",
        "    int column, lower;", "};",
        "static const struct rf_mesh_weaver_head_layout rf_mesh_weaver_heads[8] = {"]
    for head in layout["emitters"]:
        lines.append("    {{%s}, %s, %s, %d, %d}," % (values(head["mount_position_m"]),
            format(head["mount_yaw_degrees"], ".9g"), format(head["idle_pitch_degrees"], ".9g"),
            head["column"], int(head["level"] == "lower")))
    lines += ["};", ""]
    for name, key in (("core_position_in_yoke_m", "core_position_in_yoke_m"),
            ("aperture_in_core_m", "aperture_in_core_m"),
            ("petal_hinge_in_core_m", "petal_hinge_in_core_m"),
            ("petal_roll_degrees", "petal_roll_degrees"),
            ("petal_open_axis", "petal_open_axis"), ("optical_axis", "optical_axis")):
        lines.append("static const double rf_mesh_weaver_%s[3] = {%s};" % (name, values(k[key])))
    for name in ("yaw_limit_degrees", "pitch_limit_degrees"):
        lines.append("static const double rf_mesh_weaver_%s[2] = {%s};" % (name, values(k[name])))
    for name in ("petal_closed_degrees", "petal_open_degrees", "aperture_radius_m"):
        lines.append("static const double rf_mesh_weaver_%s = %s;" % (name, format(k[name], ".9g")))
    for part in ("tray", "terminal"):
        row = next(row for row in layout["fixed_instances"] if row["part"] == part)
        lines.append("static const double rf_mesh_weaver_%s_position_m[3] = {%s};" % (part, values(row["position_m"])))
    motion = layout["tray_motion"]
    lines.append("/* Contact plane uses the quantized RMESH surface, excluding the raised guards. */")
    lines.append("static const double rf_mesh_weaver_tray_support_height_m = %.12g;" %
                 motion["support_surface"]["runtime_height_m"])
    lines.append("static const double rf_mesh_weaver_tray_delivery_translation_m[3] = {%s};" %
                 values(motion["delivery_translation_m"]))
    screen = layout["terminal_screen"]
    lines.append("static const double rf_mesh_weaver_screen_in_terminal_m[3] = {%s};" % values(screen["center_m"]))
    lines.append("static const double rf_mesh_weaver_screen_normal[3] = {%s};" % values(screen["normal"]))
    for name in ("width_rfu", "height_rfu"):
        lines.append("#define RF_MESH_WEAVER_SCREEN_%s %d" % (name.upper(), screen[name]))
    lines.append("static const double rf_mesh_weaver_build_center_m[3] = {%s};" % values(layout["build_center_m"]))
    for name, bounds in (("manufacturing", layout["manufacturing_bounds_m"]),
                         ("frame", layout["components"]["frame"]["bounds_m"])):
        lines.append("static const double rf_mesh_weaver_%s_bounds_m[2][3] = {{%s}, {%s}};" % (name, values(bounds[0]), values(bounds[1])))
    lines += ["", "#endif", ""]
    HEADER.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--blender", default="E:/Blender 5.2/blender.exe")
    parser.add_argument("--tool-dir", type=Path, default=ROOT/"build-windows")
    parser.add_argument("--audit-only", action="store_true")
    args = parser.parse_args()
    if not args.audit_only:
        PUBLIC.mkdir(parents=True, exist_ok=True)
        MANIFESTS.mkdir(parents=True, exist_ok=True)
        run([args.blender, "--background", "--factory-startup", "--python-exit-code", "1",
             "--python", ROOT/"tools/blender/generate_mesh_weaver.py", "--",
             "--output", SOURCE, "--layout", LAYOUT])
        for name in PARTS:
            identity = "rf_mesh_weaver_"+name
            manifest = MANIFESTS/(identity+".asset.json")
            manifest.write_text(json.dumps({"schema": 1, "id": identity,
                "type": "static_prop",
                "source": "../../../../../rasterfall/private-assets/source/props/mesh_weaver/"+identity+".glb"},
                indent=2)+"\n", encoding="utf-8", newline="\n")
            run([sys.executable, ROOT/"tools/assets/import_asset.py", "--no-build",
                "--tool-dir", args.tool_dir.resolve(), "--force", "--output-root", PUBLIC, manifest])
    audit()


if __name__ == "__main__":
    main()
