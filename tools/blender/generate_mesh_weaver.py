"""Generate the original RF Mesh Weaver's independently animated rigid parts.

Run with Blender in background through tools/mesh_weaver_assets.py. Authoring
coordinates are physical metres, RF X-right/Y-up/+Z-front. The Blender scene
uses X-right/Z-up/-Y-front; GLB exports restore the RF coordinate convention.
No object is recentered: every exported vertex is relative to its stated pivot.
"""

import argparse
import json
import math
from pathlib import Path
import sys

import bpy
from mathutils import Matrix, Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, select_only


PARTS = ("base", "column", "tray", "terminal", "emitter_mount",
         "emitter_yoke", "emitter_core", "emitter_petal")
COLORS = ((180, 191, 198), (37, 46, 57), (104, 124, 139),
          (214, 220, 217), (215, 124, 53), (124, 177, 181))
MATERIAL_NAMES = ("shell", "graphite", "alloy", "ceramic", "warning", "optics")
MAX_TRIANGLES = {"base": 3600, "column": 1200, "tray": 1100,
                 "terminal": 1300, "emitter_mount": 900,
                 "emitter_yoke": 700, "emitter_core": 1100,
                 "emitter_petal": 240, "frame": 18000}


def rf(v):
    return (v[0], -v[2], v[1])


class Component(Builder):
    def __init__(self, materials, precise=False):
        super().__init__(materials)
        self.precise = precise

    def box_rf(self, center, size, role=0, bevel=.008):
        # Ordinary static props are quantized to 4.31 mm. Chamfers below that
        # grid collapse into zero-area triangles, particularly after 45deg joins.
        # The three articulated parts use the importer's finer small-part grid.
        if not self.precise and (min(size) < .040 or bevel < .007):
            bevel = 0
        return self.box(rf(center), (size[0], size[2], size[1]), role, bevel)

    def mesh_rf(self, name, vertices, faces, role):
        mesh = bpy.data.meshes.new(name)
        mesh.from_pydata([rf(v) for v in vertices], [], faces)
        mesh.update()
        obj = bpy.data.objects.new(name, mesh)
        bpy.context.collection.objects.link(obj)
        obj.data.materials.append(self.materials[role])
        self.parts.append(obj)
        return obj

    def prism(self, name, polygon, low, high, axis="y", role=0):
        # polygon uses X/Z for Y extrusions and X/Y for Z extrusions.
        def point(a, b, t):
            return (a, t, b) if axis == "y" else (a, b, t)
        verts = [point(a, b, t) for t in (low, high) for a, b in polygon]
        n = len(polygon)
        faces = [tuple(reversed(range(n))), tuple(range(n, 2*n))]
        faces += [(i, (i+1) % n, (i+1) % n+n, i+n) for i in range(n)]
        return self.mesh_rf(name, verts, faces, role)

    def octagon(self, center, size, role=0, cut=.04):
        x, y, z = center
        w, h, d = size
        a, b = w/2, d/2
        c = min(cut, a*.45, b*.45)
        poly = [(x-a+c, z-b), (x+a-c, z-b), (x+a, z-b+c),
                (x+a, z+b-c), (x+a-c, z+b), (x-a+c, z+b),
                (x-a, z+b-c), (x-a, z-b+c)]
        return self.prism("chamfered_plate", poly, y-h/2, y+h/2, role=role)

    def ring(self, center, outer, inner, depth, role=2, segments=20, axis="z"):
        verts = []
        for t in (-depth/2, depth/2):
            for radius in (outer, inner):
                for i in range(segments):
                    a = i*math.tau/segments
                    p = (radius*math.cos(a), radius*math.sin(a), t)
                    if axis == "x":
                        p = (p[2], p[0], p[1])
                    elif axis == "y":
                        p = (p[0], p[2], p[1])
                    verts.append(tuple(p[j]+center[j] for j in range(3)))
        n = segments
        faces = []
        for i in range(n):
            j = (i+1) % n
            faces += [(i, j, n+j, n+i), (2*n+i, 3*n+i, 3*n+j, 2*n+j),
                      (i, 2*n+i, 2*n+j, j), (n+i, n+j, 3*n+j, 3*n+i)]
        return self.mesh_rf("annular_bearing", verts, faces, role)

    def cylinder_rf(self, center, radius, depth, role=2, segments=12, axis="z"):
        rotation = (math.pi/2, 0, 0) if axis == "z" else ((0, math.pi/2, 0) if axis == "x" else (0, 0, 0))
        bpy.ops.mesh.primitive_cylinder_add(vertices=segments, radius=radius,
            depth=depth, location=rf(center), rotation=rotation)
        obj = bpy.context.object
        obj.data.materials.append(self.materials[role])
        self.parts.append(obj)
        return obj

    def label(self, text, origin, pitch=.013, role=3):
        glyphs = {"R": ("110", "101", "110", "101", "101"),
                  "F": ("111", "100", "110", "100", "100"),
                  "M": ("101", "111", "111", "101", "101"),
                  "W": ("101", "101", "111", "111", "101"),
                  "1": ("010", "110", "010", "010", "111"),
                  "0": ("111", "101", "101", "101", "111"),
                  " ": ("000",)*5}
        for index, char in enumerate(text):
            for row, line in enumerate(glyphs[char]):
                for column, bit in enumerate(line):
                    if bit == "1":
                        # Viewed toward -Z in RF, screen-right is world -X.
                        self.box_rf((origin[0]-(index*4+column)*pitch,
                                     origin[1]+(4-row)*pitch, origin[2]),
                                    (pitch*.88, pitch*.88, .006), role, 0)


def build_base(b):
    b.octagon((0, .151, 0), (1.64, .202, 1.64), 1, .12)
    b.octagon((0, .280, 0), (1.68, .100, 1.68), 0, .105)
    b.octagon((0, .338, 0), (1.56, .028, 1.56), 2, .105)
    b.octagon((0, .356, 0), (1.13, .012, 1.13), 1, .055)
    for x in (-.697, .697):
        for z in (-.697, .697):
            b.octagon((x, .035, z), (.31, .070, .31), 1, .05)
            b.octagon((x, .078, z), (.245, .024, .245), 2, .04)
            b.box_rf((x, .052, z+.134), (.17, .012, .009), 4, .002)
            b.octagon((x, .353, z), (.248, .040, .248), 1, .035)
            for dx in (-.083, .083):
                b.cylinder_rf((x+dx, .380, z), .014, .008, 2, 6, "y")
    # Four large replaceable access panels; actual separations read in shadow.
    for side in (-1, 1):
        for x in (-.48, .48):
            b.box_rf((x, .184, side*.814), (.44, .146, .032), 0, .018)
            b.box_rf((x, .235, side*.836), (.19, .011, .010), 1, .002)
            b.box_rf((x+.15, .145, side*.836), (.034, .042, .008), 4, .004)
        for z in (-.46, .46):
            b.box_rf((side*.814, .184, z), (.032, .146, .45), 0, .018)
        # Louver recess and six broad fins rather than a dense grille.
        b.box_rf((side*.832, .169, 0), (.020, .140, .260), 1, .004)
        for z in (-.10, -.06, -.02, .02, .06, .10):
            b.box_rf((side*.845, .173, z), (.012, .103, .012), 2, .003)
    b.box_rf((0, .169, .838), (.34, .144, .042), 1, .014)
    b.label("RF MW1", (.131, .149, .863), .0105)
    # Rear power/data manifold: keyed rectangular data and ring power socket.
    b.box_rf((0, .164, -.846), (.33, .167, .052), 1, .014)
    b.box_rf((.097, .162, -.878), (.094, .077, .027), 2, .008)
    b.box_rf((.097, .162, -.895), (.066, .046, .011), 1, .003)
    b.box_rf((.097, .179, -.903), (.043, .006, .006), 5, 0)
    b.ring((-.080, .162, -.883), .052, .034, .042, 2, 12)
    b.cylinder_rf((-.080, .162, -.868), .033, .006, 1, 12)
    for dx in (-.013, .013):
        b.box_rf((-.080+dx, .162, -.882), (.007, .025, .014), 4, .001)
    # Separated corner fiducials, with no continuous glowing border.
    for s in (-1, 1):
        for z in (-.532, .532):
            b.box_rf((s*.492, .367, z), (.065, .009, .016), 4, .002)
            b.box_rf((s*.532, .367, z-s*.040), (.016, .009, .065), 4, .002)
    # Fixed drawer rails and end stops remain with the machine frame. The
    # carriage travels 205 mm along +Z without altering its bearing height.
    for x in (-.355, .355):
        b.box_rf((x, .349, .095), (.064, .026, 1.130), 1, 0)
        b.box_rf((x, .360, .095), (.031, .010, 1.130), 2, 0)
        for z in (-.480, .670):
            b.box_rf((x, .371, z), (.070, .024, .026), 1, 0)


def build_column(b):
    b.octagon((0, .055, 0), (.226, .110, .226), 1, .032)
    b.box_rf((0, .716, -.025), (.146, 1.29, .137), 1, .016)
    # Two long shells wrap a narrow recessed inner service channel.
    for x in (-.055, .055):
        b.box_rf((x, .740, .029), (.052, 1.205, .104), 0, .010)
    b.box_rf((0, .747, .055), (.038, 1.04, .017), 2, .002)
    b.box_rf((0, .747, .093), (.014, .738, .006), 5, .001)
    # The outward face needs its own broad service armour: from the usual
    # quarter view it is as visible as the recessed inward instrument channel.
    # Deliberate gaps expose the dark extrusion and the central latch.
    for center, height in ((.385, .350), (.950, .610)):
        low, high = center-height/2, center+height/2
        outline = [(-.057, low), (.057, low), (.071, low+.014),
                   (.071, high-.020), (.051, high), (-.051, high),
                   (-.071, high-.020), (-.071, low+.014)]
        b.prism("rear_service_armour", outline, -.132, -.109, "z", 0)
    b.box_rf((0, .710, -.138), (.062, .014, .012), 1, 0)
    b.box_rf((-.048, 1.043, -.138), (.013, .128, .012), 4, 0)
    b.box_rf((.043, .276, -.138), (.025, .037, .012), 2, 0)
    for y in (.178, 1.269):
        b.box_rf((0, y, .093), (.115, .095, .036), 1, .006)
        b.box_rf((-.052, y, .116), (.009, .055, .006), 4, .001)
    for y in (.12, 1.346):
        b.octagon((0, y, 0), (.201, .046, .199), 2, .020)
    b.octagon((0, 1.381, 0), (.208, .032, .206), 1, .028)
    for y in (.259, 1.157):
        b.box_rf((.063, y, -.050), (.012, .075, .041), 4, .002)
    # Mid-height collar and one recessed latch provide a readable rhythm.
    b.box_rf((0, .61, -.108), (.067, .130, .015), 1, .009)
    b.box_rf((0, .642, -.118), (.035, .022, .012), 3, .004)


def build_tray(b):
    b.octagon((0, .015, 0), (1.064, .030, .92), 1, .066)
    b.octagon((0, .040, 0), (1.010, .028, .866), 2, .058)
    b.octagon((0, .058, 0), (.944, .008, .786), 1, .052)
    # Physical inset ribs at useful scale, kept below the rim.
    for x in (-.340, -.170, 0, .170, .340):
        b.box_rf((x, .064, 0), (.012, .006, .630), 2, .002)
    for z in (-.365, .365):
        b.box_rf((0, .065, z), (.70, .012, .020), 0, .003)
        for x in (-.39, .39):
            b.box_rf((x, .069, z), (.044, .012, .023), 4, .003)
    for x in (-.504, .504):
        b.box_rf((x, .075, 0), (.025, .034, .530), 0, .005)
    # A real flat contact plate spans the calibrated weapons' lowest points,
    # including the shotgun butt at X=-.469 m after its 70-degree product yaw.
    # The side guards remain higher but outside all supported gun footprints.
    b.octagon((0, .065, 0), (1.006, .012, .456), 2, .020)
    b.octagon((0, .071, 0), (.990, .006, .440), 1, .020)


def build_terminal(b):
    b.octagon((0, .030, 0), (.29, .060, .24), 1, .04)
    b.box_rf((0, .239, -.018), (.133, .396, .135), 2, .015)
    b.box_rf((0, .261, .057), (.073, .234, .016), 1, .005)
    b.box_rf((0, .525, 0), (.377, .287, .112), 1, .023)
    for x in (-.180, .180):
        b.box_rf((x, .525, .009), (.036, .249, .119), 0, .010)
    for y in (.394, .656):
        b.box_rf((0, y, .011), (.322, .029, .117), 0, .008)
    # Recessed empty screen aperture. Runtime places pixels just in front.
    b.box_rf((0, .529, .058), (.319, .211, .010), 1, .002)
    b.box_rf((0, .386, .063), (.217, .012, .016), 2, .002)
    b.box_rf((-.135, .386, .068), (.018, .012, .006), 4, .002)
    b.box_rf((.138, .386, .068), (.014, .009, .006), 5, .001)
    b.box_rf((0, .525, -.064), (.260, .167, .018), 2, .007)
    for x in (-.090, -.045, 0, .045, .090):
        b.box_rf((x, .525, -.077), (.017, .095, .010), 1, .002)


def build_mount(b):
    # Mount's origin is the steering axis, not its lower bounding face.
    b.box_rf((0, 0, -.109), (.17, .187, .055), 1, .012)
    b.box_rf((0, 0, -.075), (.244, .161, .043), 2, .010)
    for x in (-.139, .139):
        poly = [(x-.026, -.095), (x+.026, -.078),
                (x+.026, .068), (x+.009, .118), (x-.026, .081)]
        b.prism("angular_guard", poly, -.096, -.024, "z", 0)
        b.box_rf((x, -.025, -.017), (.032, .056, .012), 1, .006)
        b.box_rf((x, .063, -.018), (.020, .020, .010), 4, .004)
    b.box_rf((0, -.095, -.083), (.208, .025, .043), 1, .007)
    # A rear-set angled cap reads as a protective brow without entering the
    # petals' forward sweep or changing the yaw/pitch axes.
    hood = [(-.098, -.092), (.098, -.092), (.080, -.025), (-.080, -.025)]
    b.prism("recessed_visor", hood, .121, .143, role=2)
    b.box_rf((0, .136, -.067), (.132, .022, .058), 0, 0)
    b.cylinder_rf((0, -.080, -.076), .030, .023, 2, 12, "y")


def build_yoke(b):
    b.box_rf((0, 0, -.060), (.193, .058, .031), 1, .007)
    for x in (-.091, .091):
        b.box_rf((x, 0, -.012), (.027, .082, .100), 2, .007)
        b.cylinder_rf((x, 0, .015), .035, .022, 1, 16, "x")
        b.cylinder_rf((x+(.014 if x > 0 else -.014), 0, .015), .021, .006, 3, 12, "x")
    b.box_rf((0, -.048, -.033), (.133, .026, .048), 2, .005)
    b.box_rf((0, -.063, -.025), (.051, .010, .034), 4, .003)


def build_core(b):
    b.ring((0, 0, .011), .075, .049, .102, 1, 18)
    b.ring((0, 0, .066), .080, .054, .016, 3, 18)
    b.ring((0, 0, -.043), .071, .039, .013, 2, 18)
    b.cylinder_rf((0, 0, -.035), .048, .015, 1, 18)
    # Small triangular aperture in a dark, stepped triangular optical well.
    for radius, z, depth, role in ((.047, .041, .011, 0),
                                  (.037, .052, .009, 1),
                                  (.026, .060, .007, 5)):
        poly = [(math.cos(a)*radius, math.sin(a)*radius)
                for a in (math.pi/2, math.pi/2+math.tau/3, math.pi/2+2*math.tau/3)]
        b.prism("triangular_aperture", poly, z-depth/2, z+depth/2, "z", role)
    for i in range(3):
        a = i*math.tau/3
        # Paired bearing cheeks make the tangential leaf hinge readable. They
        # stay with the core while the shaft and shutter rotate about local X.
        for x in (-.036, .036):
            cheek = b.box_rf((x, .064, .077), (.012, .020, .016), 2, .003)
            cheek.matrix_world = Matrix.Rotation(-a, 4, "Y") @ cheek.matrix_world


def build_petal(b):
    # Hinge is at local origin. +Y points radially outward, -Y toward aperture.
    # Keep the armour wholly inward of the hinge: positive local Y would swing
    # back through the optical rim as the shutter opens toward +Z.
    outline = [(-.027, -.010), (.027, -.010), (.049, -.029),
               (.016, -.051), (0, -.060), (-.016, -.051), (-.049, -.029)]
    b.prism("shutter_leaf", outline, -.004, .008, "z", 0)
    inset = [(-.021, -.017), (.021, -.017), (.038, -.030),
             (.008, -.051), (-.014, -.044), (-.038, -.030)]
    b.prism("shutter_inset", inset, .008, .011, "z", 2)
    b.box_rf((0, -.010, .003), (.034, .014, .012), 1, 0)
    b.cylinder_rf((0, 0, .002), .009, .070, 1, 12, "x")
    for x in (-.038, .038):
        b.cylinder_rf((x, 0, .002), .009, .006, 3, 10, "x")
    b.box_rf((0, -.036, .013), (.020, .012, .004), 4, 0)


BUILDERS = dict(zip(PARTS, (build_base, build_column, build_tray,
    build_terminal, build_mount, build_yoke, build_core, build_petal)))


def finish(b, name):
    select_only(b.parts)
    bpy.ops.object.join()
    obj = bpy.context.object
    obj.name = "rf_mesh_weaver_"+name
    bpy.context.scene.cursor.location = (0, 0, 0)
    bpy.ops.object.origin_set(type="ORIGIN_CURSOR")
    bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
    indices = [b.materials.index(obj.data.materials[p.material_index]) for p in obj.data.polygons]
    obj.data.materials.clear()
    for mat in b.materials:
        obj.data.materials.append(mat)
    for polygon, index in zip(obj.data.polygons, indices):
        polygon.material_index = index
        polygon.use_smooth = False
    # Correct winding for custom solids before triangulation; no topology merge.
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    mod = obj.modifiers.new("export_triangles", "TRIANGULATE")
    bpy.ops.object.modifier_apply(modifier=mod.name)
    obj.data.update()
    count = len(obj.data.polygons)
    assert count <= MAX_TRIANGLES[name], (name, count, "triangle budget")
    assert all(p.area > 1e-10 for p in obj.data.polygons), (name, "degenerate triangle")
    vertices = [(v.co.x, v.co.z, -v.co.y) for v in obj.data.vertices]
    bounds = [[round(min(v[i] for v in vertices), 7) for i in range(3)],
              [round(max(v[i] for v in vertices), 7) for i in range(3)]]
    if name in ("base", "column", "tray", "terminal", "frame"):
        assert abs(bounds[0][1]) < 1e-6, (name, "bottom pivot")
    return obj, {"pivot_m": [0, 0, 0], "bounds_m": bounds,
                 "triangles": count, "source_vertices": len(vertices),
                 "pivot_kind": "bottom_center" if name in (*PARTS[:4], "frame") else "mechanical_mount"}


def layout():
    fixed = [{"name": "base", "part": "base", "position_m": [0, 0, 0], "yaw_degrees": 0},
             {"name": "tray", "part": "tray", "position_m": [0, .362, 0], "yaw_degrees": 0},
             {"name": "terminal", "part": "terminal", "position_m": [-.57, .10, .93], "yaw_degrees": 0}]
    emitters = []
    for index, (x, z) in enumerate(((-.665, .665), (.665, .665), (.665, -.665), (-.665, -.665))):
        yaw = math.degrees(math.atan2(-x, -z))
        fixed.append({"name": "column_%d" % index, "part": "column",
                      "position_m": [x, .373, z], "yaw_degrees": yaw})
        direction = (-x/math.hypot(x, z), -z/math.hypot(x, z))
        for level, y in (("lower", .542), ("upper", 1.620)):
            position = [round(x+direction[0]*.113, 7), y, round(z+direction[1]*.113, 7)]
            head = {"name": "head_%d_%s" % (index, level), "column": index,
                    "level": level, "mount_position_m": position,
                    "mount_yaw_degrees": yaw,
                    "idle_pitch_degrees": -28 if level == "lower" else 36}
            emitters.append(head)
            fixed.append({"name": head["name"]+"_mount", "part": "emitter_mount",
                          "position_m": position, "yaw_degrees": yaw})
    return {"schema": 1, "id": "rf_mesh_weaver", "source_units": "metres",
            "coordinate_system": "X-right Y-up +Z-front", "rfu_per_metre": 512,
            "manufacturing_bounds_m": [[-.51, .48, -.51], [.51, 1.50, .51]],
            "build_center_m": [0, .965, 0], "fixed_instances": fixed,
            "emitters": emitters,
            "tray_motion": {"delivery_translation_m": [0, 0, .205],
                "support_surface": {"kind": "horizontal_chamfered_plate",
                    "height_m": .074, "normal": [0, 1, 0],
                    "bounds_xz_m": [[-.495, -.220], [.495, .220]],
                    "corner_chamfer_m": .020,
                    "description": "Gun minimum Y rests on this plane, not the higher side guards."}},
            "kinematics": {
                "mount_origin": "yaw-axis center; fixed shell uses mount_yaw_degrees",
                "yoke_pivot_m": [0, 0, 0], "yoke_yaw_axis": [0, 1, 0],
                "yaw_limit_degrees": [-34, 34],
                "core_position_in_yoke_m": [0, 0, .015],
                "core_pitch_axis": [1, 0, 0], "pitch_limit_degrees": [-62, 62],
                "optical_axis": [0, 0, 1], "aperture_in_core_m": [0, 0, .065],
                "aperture_radius_m": .026,
                "petal_hinge_in_core_m": [0, .064, .080],
                "petal_roll_degrees": [0, 120, 240],
                "petal_open_axis": [1, 0, 0], "petal_closed_degrees": 0,
                "petal_open_degrees": -64},
            "terminal_screen": {"part": "terminal", "center_m": [0, .529, .066],
                "normal": [0, 0, 1], "width_rfu": 160, "height_rfu": 104,
                "logical_pixels": [160, 104]},
            "service_ports": {"power_m": [-.080, .162, -.908],
                              "data_m": [.097, .162, -.908], "outward": [0, 0, -1]},
            "export_contract": {"node_transforms_baked": True,
                "recentering": False, "automatic_floor_offset": False,
                "runtime_scale": "Read each RMESH header position_scale; RFU = local * 512 / position_scale.",
                "animation": "Independent rigid transforms; no baked beam or luminous aperture overlay."}}


def make_assembly(objects, description):
    # A reproducible authoring inspection scene; RF runtime consumes JSON poses.
    collection = bpy.data.collections.new("RF_MESH_WEAVER_ASSEMBLED")
    bpy.context.scene.collection.children.link(collection)
    basis = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))

    def place(name, part, matrix):
        obj = objects[part].copy()
        obj.data = objects[part].data
        obj.name = name
        collection.objects.link(obj)
        obj.matrix_world = basis @ matrix @ basis.inverted()
        return obj

    for row in description["fixed_instances"]:
        matrix = Matrix.Translation(Vector(row["position_m"])) @ Matrix.Rotation(math.radians(row["yaw_degrees"]), 4, "Y")
        place(row["name"], row["part"], matrix)
    for head in description["emitters"]:
        mount = Matrix.Translation(Vector(head["mount_position_m"])) @ Matrix.Rotation(math.radians(head["mount_yaw_degrees"]), 4, "Y")
        place(head["name"]+"_yoke", "emitter_yoke", mount)
        core = mount @ Matrix.Translation(Vector((0, 0, .015))) @ Matrix.Rotation(math.radians(head["idle_pitch_degrees"]), 4, "X")
        place(head["name"]+"_core", "emitter_core", core)
        for i in range(3):
            petal = core @ Matrix.Rotation(i*math.tau/3, 4, "Z") @ Matrix.Translation(Vector((0, .064, .080)))
            place(head["name"]+"_petal_%d" % i, "emitter_petal", petal)
    for obj in objects.values():
        obj.hide_render = True
        obj.hide_set(True)


def make_frame(objects, description, materials):
    b = Component(materials)
    basis = Matrix(((1, 0, 0, 0), (0, 0, -1, 0), (0, 1, 0, 0), (0, 0, 0, 1)))
    for row in description["fixed_instances"]:
        if row["part"] == "tray":
            continue
        obj = objects[row["part"]].copy()
        obj.data = objects[row["part"]].data.copy()
        bpy.context.collection.objects.link(obj)
        matrix = Matrix.Translation(Vector(row["position_m"])) @ Matrix.Rotation(math.radians(row["yaw_degrees"]), 4, "Y")
        obj.matrix_world = basis @ matrix @ basis.inverted()
        b.parts.append(obj)
    return finish(b, "frame")


def audit_runtime_clearance(directory, pose_csv=None):
    """Check quantized shutter motion, without rendering or using a GPU."""
    import struct
    from mathutils.bvhtree import BVHTree

    def read(name):
        data = (directory/("rf_mesh_weaver_"+name+".rmesh")).read_bytes()
        nv, ni, scale = struct.unpack_from("<III", data, 8)
        nm = struct.unpack_from("<I", data, 48)[0]
        offset = struct.unpack_from("<I", data, 56)[0]+nm*16
        vertices = [Vector(tuple(x/scale for x in struct.unpack_from("<3i", data, offset+i*24)))
                    for i in range(nv)]
        ids = struct.unpack_from("<%dI" % ni, data, offset+nv*24)
        return vertices, [ids[i:i+3] for i in range(0, ni, 3)]

    def tree(mesh, transform=Matrix.Identity(4)):
        vertices, faces = mesh
        return BVHTree.FromPolygons([transform@v for v in vertices], faces,
                                   all_triangles=True, epsilon=0)

    def armour(mesh):
        # GLB separates vertices at hard normals. Reconnect only equal-position
        # vertices to identify authored solids, excluding intentional shaft /
        # bearing engagement from the moving plate versus core-shell test.
        vertices, faces = mesh
        neighbors = {}
        for face in faces:
            keys = [tuple(vertices[n]) for n in face]
            for key in keys:
                neighbors.setdefault(key, set()).update(keys)
        components, selected = {}, set()
        for key in neighbors:
            if key in components:
                continue
            pending, reached = [key], set()
            while pending:
                point = pending.pop()
                if point in reached:
                    continue
                reached.add(point)
                pending.extend(neighbors[point]-reached)
            for point in reached:
                components[point] = key
            if max(v[1] for v in reached) < -.003 and min(v[1] for v in reached) < -.020:
                selected.add(key)
        plates = [face for face in faces if components[tuple(vertices[face[0]])] in selected]
        assert plates, "cannot identify the shutter armour solids"
        return vertices, plates

    mount, yoke, core, petal = (read(name) for name in
        ("emitter_mount", "emitter_yoke", "emitter_core", "emitter_petal"))
    description_path = directory/"mesh_weaver.components.json"
    description = json.loads(description_path.read_text(encoding="utf-8"))
    plate = armour(petal)
    frame_mesh = read("frame")
    fixed, barrel, frame = tree(mount), tree(core), tree(frame_mesh)
    counts = {"petal_core": 0, "petal_petal": 0, "petal_mount": 0,
              "petal_yoke": 0, "petal_frame": 0}
    examples, working_examples, samples, frame_samples, working_frame_hits = [], [], 0, 0, 0
    def overlap(kind, a, b, state):
        hits = a.overlap(b)
        counts[kind] += len(hits)
        if hits and len(examples) < 12:
            example = {"kind": kind, "state": state, "triangles": len(hits)}
            if kind == "petal_frame":
                vertices, faces = frame_mesh
                example["obstacle_center_m"] = list(sum((vertices[n] for n in faces[hits[0][1]]), Vector())/3)
            examples.append(example)
        return len(hits)

    for step in range(17):
        opening = step/16
        children = [Matrix.Rotation(math.radians(roll), 4, "Z") @
                    Matrix.Translation(Vector((0, .064, .080))) @
                    Matrix.Rotation(math.radians(-64*opening), 4, "X") for roll in (0, 120, 240)]
        petals = [tree(petal, child) for child in children]
        for i, child in enumerate(children):
            overlap("petal_core", tree(plate, child), barrel, [opening, i])
            overlap("petal_petal", petals[i], petals[(i+1) % 3], [opening, i])
        for idle_pitch in (-28, 36):
            world_mounts = [Matrix.Translation(Vector(head["mount_position_m"])) @
                Matrix.Rotation(math.radians(head["mount_yaw_degrees"]), 4, "Y")
                for head in description["emitters"] if head["idle_pitch_degrees"] == idle_pitch]
            for yaw_step in range(9):
                yaw = (-34+yaw_step*8.5)*opening
                for pitch_step in range(9):
                    target_pitch = -62+pitch_step*15.5
                    pitch = idle_pitch*(1-opening)+target_pitch*opening
                    working_cone = target_pitch <= 0 if idle_pitch < 0 else target_pitch >= 0
                    turn = Matrix.Rotation(math.radians(yaw), 4, "Y")
                    tilt = turn @ Matrix.Translation(Vector((0, 0, .015))) @ \
                        Matrix.Rotation(math.radians(pitch), 4, "X")
                    cradle = tree(yoke, turn)
                    for i, child in enumerate(children):
                        moving = tree(petal, tilt@child)
                        state = [opening, yaw, pitch, i]
                        overlap("petal_mount", moving, fixed, state)
                        overlap("petal_yoke", moving, cradle, state)
                        for world_mount in world_mounts:
                            hits = overlap("petal_frame", tree(petal, world_mount@tilt@child), frame, state)
                            if working_cone:
                                working_frame_hits += hits
                                if hits and len(working_examples) < 6:
                                    working_examples.append({"idle_pitch": idle_pitch, "state": state,
                                                             "triangles": hits})
                            frame_samples += 1
                        samples += 1
    report = {"method": "Quantized RMESH triangle BVH overlap; no rendered approximation.",
        "petal_pose_samples": samples, "opening_samples": 17,
        "assembled_frame_pose_samples": frame_samples,
        "yaw_samples": 9, "pitch_samples": 9, "idle_pitch_variants": 2,
        "intersections": counts,
        "assembled_frame_working_cone_intersections": working_frame_hits,
        "working_target_pitch_degrees": {"lower": [-62, 0], "upper": [0, 62]},
        "outside_working_cone_examples": examples[:2],
        "limitation": "A fully opened lower head aimed steeply downward can strike its column foot; manufacturing aims lower heads upward.",
        "exclusion": "Core-shell check excludes intentional hinge shaft / bearing engagement."}
    if any(value for kind, value in counts.items() if kind != "petal_frame"):
        print(json.dumps({"intersections": counts, "examples": examples[:4]}), flush=True)
    assert not any(value for kind, value in counts.items() if kind != "petal_frame"), \
        "shutter armour intersects another head part"
    if working_examples:
        print(json.dumps(working_examples, indent=2), flush=True)
    assert not working_frame_hits, "shutter intersects the assembled frame in its manufacturing direction"
    description["mechanical_clearance_audit"] = report
    description_path.write_text(json.dumps(description, indent=2)+"\n", encoding="utf-8", newline="\n")
    print("Runtime shutter clearance:", json.dumps(report), flush=True)
    if pose_csv:
        import csv
        import hashlib
        actual = {"source_csv": str(pose_csv),
            "source_sha256": hashlib.sha256(pose_csv.read_bytes()).hexdigest(),
            "head_pose_samples": 0, "petal_pose_samples": 0, "intersections": 0,
            "weapons": {}, "examples": [], "assets": {}}
        for name in ("frame", "emitter_petal", "emitter_core", "emitter_yoke"):
            asset = directory/("rf_mesh_weaver_"+name+".rmesh")
            actual["assets"][asset.name] = hashlib.sha256(asset.read_bytes()).hexdigest()
        with pose_csv.open(encoding="utf-8-sig", newline="") as handle:
            for row in csv.DictReader(handle):
                head = description["emitters"][int(row["head"])]
                yaw = head["mount_yaw_degrees"]+float(row["yaw_delta"])
                root = Matrix.Translation(Vector(head["mount_position_m"])) @ \
                    Matrix.Rotation(math.radians(yaw), 4, "Y")
                parent = root @ Matrix.Translation(Vector((0, 0, .015))) @ \
                    Matrix.Rotation(math.radians(float(row["pitch"])), 4, "X")
                actual["head_pose_samples"] += 1
                actual["weapons"][row["weapon"]] = actual["weapons"].get(row["weapon"], 0)+1
                for roll in (0, 120, 240):
                    child = Matrix.Rotation(math.radians(roll), 4, "Z") @ \
                        Matrix.Translation(Vector((0, .064, .080))) @ \
                        Matrix.Rotation(math.radians(-64*float(row["opening"])), 4, "X")
                    hits = tree(petal, parent@child).overlap(frame)
                    actual["petal_pose_samples"] += 1
                    actual["intersections"] += len(hits)
                    if hits and len(actual["examples"]) < 12:
                        actual["examples"].append(dict(row, petal_roll=roll, intersections=len(hits)))
        evidence = pose_csv.with_suffix(".clearance.json")
        evidence.write_text(json.dumps(actual, indent=2)+"\n", encoding="utf-8", newline="\n")
        print("Actual manufacturing shutter clearance:", json.dumps(actual), flush=True)
        assert not actual["intersections"], "actual manufacturing trajectory intersects the machine frame"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--layout", type=Path)
    parser.add_argument("--audit-clearance", type=Path, metavar="RMESH_DIRECTORY")
    parser.add_argument("--pose-csv", type=Path, help="Optional real pose trajectory for an additional frame clearance audit")
    args = parser.parse_args(sys.argv[sys.argv.index("--")+1:])
    if args.audit_clearance:
        audit_runtime_clearance(args.audit_clearance, args.pose_csv)
        return
    if not args.output or not args.layout:
        parser.error("--output and --layout are required for asset generation")
    args.output.mkdir(parents=True, exist_ok=True)
    args.layout.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = "METRIC"
    bpy.context.scene.unit_settings.scale_length = 1

    def linear(color):
        return tuple(c/255/12.92 if c <= 10 else ((c/255+.055)/1.055)**2.4 for c in color)

    materials = [material("mw_"+name, linear(color)) for name, color in zip(MATERIAL_NAMES, COLORS)]
    for index, mat in enumerate(materials):
        shader = mat.node_tree.nodes.get("Principled BSDF")
        shader.inputs["Metallic"].default_value = (.30, .65, .78, .12, .20, .18)[index]
        shader.inputs["Roughness"].default_value = (.69, .62, .44, .73, .61, .34)[index]
    description = layout()
    description["components"] = {}
    objects = {}
    for name in PARTS:
        b = Component(materials, precise=name in ("emitter_yoke", "emitter_core", "emitter_petal"))
        BUILDERS[name](b)
        obj, metrics = finish(b, name)
        objects[name] = obj
        description["components"][name] = metrics
        bpy.ops.export_scene.gltf(filepath=str(args.output/(obj.name+".glb")),
            export_format="GLB", use_selection=True, export_yup=True,
            export_animations=False, export_skins=False, export_morph=False,
            export_texcoords=False, export_normals=True, export_materials="EXPORT")
        print(obj.name, metrics["triangles"], "triangles", metrics["bounds_m"], flush=True)
    frame, metrics = make_frame(objects, description, materials)
    description["components"]["frame"] = metrics
    bpy.ops.export_scene.gltf(filepath=str(args.output/(frame.name+".glb")),
        export_format="GLB", use_selection=True, export_yup=True,
        export_animations=False, export_skins=False, export_morph=False,
        export_texcoords=False, export_normals=True, export_materials="EXPORT")
    print(frame.name, metrics["triangles"], "triangles", metrics["bounds_m"], flush=True)
    frame.hide_render = True
    frame.hide_set(True)
    make_assembly(objects, description)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/"mesh_weaver.blend"))
    args.layout.write_text(json.dumps(description, indent=2)+"\n", encoding="utf-8", newline="\n")


if __name__ == "__main__":
    main()
