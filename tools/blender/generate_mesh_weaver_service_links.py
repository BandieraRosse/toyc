"""Original static lab service cables, authored relative to the Mesh Weaver.

All coordinates are RF metres (X/Y-up/+Z-front). This prop belongs to the lab
layout, not to the machine's animated hierarchy or a gameplay power network.
"""
import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_mesh_weaver import Component, rf
from generate_rasterfall_props import material, select_only

IDENTITY = "rf_mesh_weaver_service_links"
POWER_PORT = (-.080, .162, -.908)
DATA_PORT = (.097, .162, -.908)
RF1_IO = (-1509/512, 846/512, -210/512)
POWER_BOX = (2.326, .220, -1.265)
COLORS = ((42, 48, 53), (116, 133, 144), (181, 107, 51), (94, 157, 169))


def rounded_route(points, radius=.085, steps=5):
    """Round polyline corners; preserve exact connector entry tangents."""
    points = [Vector(p) for p in points]
    result = [points[0]]
    for i in range(1, len(points)-1):
        a, p, b = points[i-1:i+2]
        u, v = (p-a).normalized(), (b-p).normalized()
        trim = min(radius, (p-a).length*.40, (b-p).length*.40)
        start, finish = p-u*trim, p+v*trim
        result.append(start)
        # Tangent quadratic is deliberately low resolution: sub-grid rings
        # would collapse when imported at the static 232 units/metre scale.
        for j in range(1, steps+1):
            t = j/steps
            result.append((1-t)**2*start+2*t*(1-t)*p+t*t*finish)
    result.append(points[-1])
    return result


def cable(b, name, points, radius):
    points = rounded_route(points)
    vertices = []
    previous_normal = None
    sides = 8
    for i, p in enumerate(points):
        tangent = (points[min(i+1, len(points)-1)]-points[max(0, i-1)]).normalized()
        if previous_normal is None:
            reference = Vector((0, 1, 0)) if abs(tangent.y) < .9 else Vector((1, 0, 0))
            normal = tangent.cross(reference).normalized()
        else:
            normal = (previous_normal-tangent*previous_normal.dot(tangent)).normalized()
        binormal = tangent.cross(normal).normalized()
        previous_normal = normal
        for j in range(sides):
            angle = j*math.tau/sides
            vertices.append(tuple(p+radius*(normal*math.cos(angle)+binormal*math.sin(angle))))
    faces = [tuple(reversed(range(sides)))]
    for i in range(len(points)-1):
        for j in range(sides):
            k = i*sides+j
            q = i*sides+(j+1) % sides
            faces.append((k, q, q+sides, k+sides))
    faces.append(tuple(range((len(points)-1)*sides, len(points)*sides)))
    b.mesh_rf(name, vertices, faces, 0)
    return {"radius_m": radius, "centerline_m": [[round(x, 9) for x in p] for p in points]}


def floor_clip(b, x, z, radius, role):
    # Open arch, two little feet. No solid slab through the cable.
    top = radius*2+.021
    half = radius+.009
    b.box_rf((x, top, z), (.030, .011, half*2+.022), 1, 0)
    for side in (-1, 1):
        b.box_rf((x, top/2, z+side*(half+.006)), (.030, top, .012), 1, 0)
        b.box_rf((x, .005, z+side*(half+.018)), (.052, .010, .035), 0, 0)
    b.box_rf((x, top+.009, z), (.016, .008, .021), role, 0)


def build(b):
    data = cable(b, "data_cable", [(.097, .162, -.889), (.097, .162, -1.075),
        (.097, .025, -1.075), (RF1_IO[0], .025, -1.075),
        (RF1_IO[0], .025, -.560), (RF1_IO[0], RF1_IO[1], -.560),
        (RF1_IO[0], RF1_IO[1], RF1_IO[2]+.013)], .014)
    power = cable(b, "power_cable", [(-.080, .162, -.890), (-.080, .162, -1.280),
        (-.080, .029, -1.280), (2.130, .029, -1.280),
        (2.130, .220, -1.280), (2.285, .220, -1.280)], .021)
    # Machine ring plug: the collar overlaps the existing socket annulus;
    # keyed data nose enters the actual rectangular dark socket.
    b.cylinder_rf((-.080, .162, -.918), .040, .030, 1, 8)
    b.cylinder_rf((-.080, .162, -.954), .030, .047, 0, 8)
    b.cylinder_rf((-.080, .162, -.944), .033, .012, 2, 8)
    b.cylinder_rf((-.080, .162, -.976), .025, .018, 0, 8)
    b.box_rf((.097, .162, -.919), (.061, .039, .064), 1, 0)
    b.box_rf((.097, .162, -.962), (.044, .032, .039), 0, 0)
    b.box_rf((.097, .185, -.928), (.035, .009, .022), 3, 0)
    # RF1 I/O slot is 24 x 10 RFU; the thin nose enters that real aperture.
    x, y, z = RF1_IO
    b.box_rf((x, y, z-.005), (.042, .015, .032), 1, 0)
    b.box_rf((x, y, z-.034), (.046, .032, .032), 0, 0)
    b.box_rf((x, y+.020, z-.037), (.029, .008, .018), 3, 0)
    # Two broad rear clips anchor the descending cable to the existing desk
    # apron / case. Standoffs finish against their actual rear surfaces.
    b.box_rf((x, 1.023, -.463), (.072, .031, .204), 1, 0)
    b.box_rf((x, 1.405, -.476), (.060, .027, .182), 1, 0)
    for height in (1.023, 1.405):
        b.box_rf((x, height, -.579), (.050, .036, .017), 0, 0)
    # The power-unit housing has no existing outlet. This small junction box
    # bites 16 mm into its west side (world local X=2.355 m); only its face
    # and two mounting tabs protrude. No changes to the generic power asset.
    b.box_rf(POWER_BOX, (.090, .204, .184), 0, 0)
    b.box_rf((2.274, .220, -1.265), (.014, .178, .158), 1, 0)
    for height in (.099, .341):
        b.box_rf((2.347, height, -1.265), (.042, .034, .140), 1, 0)
    b.cylinder_rf((2.259, .220, -1.280), .040, .036, 1, 8, "x")
    b.cylinder_rf((2.225, .220, -1.280), .030, .036, 0, 8, "x")
    b.cylinder_rf((2.232, .220, -1.280), .033, .012, 2, 8, "x")
    b.box_rf((2.262, .282, -1.265), (.009, .026, .066), 2, 0)
    for x in (-2.10, -.48):
        floor_clip(b, x, -1.075, .014, 3)
    for x in (.62, 1.66):
        floor_clip(b, x, -1.280, .021, 2)
    return {"data": data, "power": power}


def finish(b):
    select_only(b.parts)
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = IDENTITY
    bpy.context.scene.cursor.location = (0, 0, 0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    indices = [b.materials.index(obj.data.materials[p.material_index]) for p in obj.data.polygons]
    obj.data.materials.clear()
    for mat in b.materials:
        obj.data.materials.append(mat)
    for p, i in zip(obj.data.polygons, indices):
        p.material_index, p.use_smooth = i, False
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    mod = obj.modifiers.new("triangles", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=mod.name)
    assert len(obj.data.polygons) <= 2400
    assert all(p.area > 1e-10 for p in obj.data.polygons)
    verts = [(v.co.x, v.co.z, -v.co.y) for v in obj.data.vertices]
    return obj, [[round(f(v[k] for v in verts), 9) for k in range(3)] for f in (min, max)]


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--layout", type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
    args.output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    def linear(c):
        return tuple(v/255/12.92 if v <= 10 else ((v/255+.055)/1.055)**2.4 for v in c)
    mats = [material("mw_service_"+name, linear(color)) for name, color in
            zip(("jacket", "hardware", "power", "data"), COLORS)]
    b = Component(mats)
    routes = build(b)
    obj, bounds = finish(b)
    bpy.ops.export_scene.gltf(filepath=str(args.output/(IDENTITY+".glb")),
        export_format="GLB", use_selection=True, export_yup=True,
        export_animations=False, export_skins=False, export_morph=False,
        export_texcoords=False, export_normals=True, export_materials="EXPORT")
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/"mesh_weaver_service_links.blend"))
    layout = {"schema": 1, "id": IDENTITY, "source": "original procedural geometry",
        "generator": "tools/blender/generate_mesh_weaver_service_links.py",
        "units": "metres; RF X/Y-up/+Z-front", "pivot_m": [0, 0, 0],
        "pivot_kind": "mesh_weaver_lab_origin", "bounds_m": bounds,
        "triangles": len(obj.data.polygons), "routes": routes,
        "contacts_m": {"machine_power": POWER_PORT, "machine_data": DATA_PORT,
            "rf1_data": RF1_IO, "power_unit_west_shell_x": 2.355,
            "power_junction_box_bounds": [[2.281, .118, -1.357], [2.371, .322, -1.173]]},
        "map_anchors_rfu": {"machine": [0, 0, 0], "rf1_assembly": [-1280, 0, 0],
            "rf1_case_in_assembly": [-320, 578, -30],
            "power_unit": [1600, 0, -600], "power_unit_scale_milli": 350},
        "collision": "none; rear service route, independent of gameplay power"}
    args.layout.parent.mkdir(parents=True, exist_ok=True)
    args.layout.write_text(json.dumps(layout, indent=2)+"\n", encoding="utf-8", newline="\n")
    print(IDENTITY, layout["triangles"], "triangles", bounds, flush=True)


if __name__ == "__main__":
    main()
