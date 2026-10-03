"""Generate the RF Humanoid V2 body and headgear coverage variants.

This is a source generator rather than a hand-edited blend file.  It keeps the
RFCHAR V1 skeleton and attachment contract, but replaces the V1.1 block-like
body with a faceted, stylized human built from anatomical lofts:

  pelvis -> neutral waist -> ribcage/chest -> tapered deltoids
  jaw/cheek/temple/crown head mass + separate clean hair mass
  shoulder -> upper arm -> elbow -> forearm -> wrist -> hand
  pelvis -> thigh -> knee -> calf -> ankle -> foot

Source space is Blender metric, Z-up, and -Y forward; the glTF exporter
performs the standard Y-up conversion.

Run:
  blender --background --factory-startup --python this_file -- \
    --output rasterfall/private-assets/source/characters/rf_humanoid_v2.glb

The optional ``--headgear`` variants are authored into the same RFCHAR
character contract for V1 visual validation. Their geometry is rigidly
weighted to ``RF_HEAD``; this keeps the stable HEAD attachment/socket and
makes a later split into reusable head-mounted assemblies mechanical.
``--rigid-attachment`` instead exports only one unskinned, metric rigid GLB
whose origin and basis are the matching stable socket contract.
"""

import argparse
import json
import math
import site
import struct
import sys
from pathlib import Path

# Blender's embedded Python intentionally disables the user site directory.
# Enable it when present so a normal user-level numpy install is reusable by
# the stock glTF exporter; this is a no-op on machines with system numpy.
try:
    site.addsitedir(site.getusersitepackages())
except (AttributeError, OSError):
    pass

import bpy
from mathutils import Vector

sys.path.insert(0, str(Path(__file__).resolve().parent))
import rf_humanoid_equipment as equipment


SIDES_BODY = 10
SIDES_LIMB = 8
HEADGEAR_NAMES = (
    'bare', 'headset', 'patrol-cap', 'goggles', 'respirator',
    'tactical-helmet', 'engineering-helmet',
)
PROFESSIONS = ('rifleman', 'breacher', 'recon', 'medic', 'engineer', 'heavy',
               'gunner', 'gunner-elite')
RIGID_ATTACHMENTS = ('tactical-helmet', 'backpack', 'ballistic-goggles',
                     'cargo-thigh-l', 'cargo-thigh-r') + tuple(
    profession + '-' + slot
    for profession in PROFESSIONS
    for slot in ('head', 'chest', 'back', 'hip-l', 'hip-r'))


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--headgear", choices=HEADGEAR_NAMES, default="bare")
    parser.add_argument("--profession", choices=PROFESSIONS)
    parser.add_argument("--rigid-attachment", choices=RIGID_ATTACHMENTS)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:]
                             if "--" in sys.argv else [])


def longitudinal_normals(mesh, ring_count, sides):
    """Smooth along a cloth volume, retaining the authored radial facets.

    Each angular sector shares normals along its length, never across its
    radial boundary or end caps. This removes triangulation/band seams without
    turning the low-poly silhouette into a smooth cylinder. Normals are source
    data, exported and skinned through the ordinary RFCHAR path.
    """
    sector_normals = []
    for band in range(ring_count - 1):
        row = []
        for side in range(sides):
            first = (band * sides + side) * 2
            a, b = mesh.polygons[first], mesh.polygons[first + 1]
            row.append((a.normal * a.area + b.normal * b.area).normalized())
        sector_normals.append(row)
    normal_values = [None] * len(mesh.loops)
    side_faces = (ring_count - 1) * sides * 2
    for polygon in mesh.polygons:
        polygon.use_smooth = polygon.index < side_faces
        side = (polygon.index // 2) % sides
        for loop_index in polygon.loop_indices:
            if polygon.index >= side_faces:
                normal = polygon.normal
            else:
                ring = mesh.loops[loop_index].vertex_index // sides
                normal = Vector((0.0, 0.0, 0.0))
                if ring:
                    normal += sector_normals[ring - 1][side]
                if ring < ring_count - 1:
                    normal += sector_normals[ring][side]
                normal.normalize()
            normal_values[loop_index] = tuple(normal)
    mesh.normals_split_custom_set(normal_values)


def ring_mesh(name, rings, sides, material, arm, scene, front_cut=False,
              longitudinal_smooth=False):
    """Create a capped ring loft.

    Each ring is ``(center, basis_a, basis_b, radius_a, radius_b)``.
    Broad radial facets remain a feature of the low-poly body. Cloth can opt
    into longitudinal normal continuity; headgear and hard pieces stay flat.
    """
    vertices = []
    for center, basis_a, basis_b, radius_a, radius_b in rings:
        center = Vector(center)
        basis_a = Vector(basis_a)
        basis_b = Vector(basis_b)
        for side in range(sides):
            angle = 2.0 * math.pi * side / sides
            vertices.append(tuple(
                center + basis_a * (radius_a * math.cos(angle)) +
                basis_b * (radius_b * math.sin(angle))))

    faces = []
    for ring in range(len(rings) - 1):
        for side in range(sides):
            next_side = (side + 1) % sides
            a = ring * sides + side
            b = ring * sides + next_side
            c = (ring + 1) * sides + next_side
            d = (ring + 1) * sides + side
            for face in ((a, b, c), (a, c, d)):
                if not front_cut or ring >= 1 or sum(vertices[index][1] for index in face) / 3.0 >= -0.015:
                    faces.append(face)

    # End caps are intentionally low-poly n-gons. Blender/glTF triangulates
    # them on export, giving clean terminal facets without adding dense poles.
    if not front_cut:
        faces.append(tuple(range(sides - 1, -1, -1)))
    last = (len(rings) - 1) * sides
    faces.append(tuple(last + side for side in range(sides)))

    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    if longitudinal_smooth:
        if front_cut:
            raise ValueError('longitudinal cloth normals require complete rings')
        longitudinal_normals(mesh, len(rings), sides)
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = arm
    modifier = obj.modifiers.new("RFCHAR Skin", "ARMATURE")
    modifier.object = arm
    return obj


def weighted_rings(name, rings, sides, material, arm, scene, weights,
                   front_cut=False, longitudinal_smooth=False):
    """Create a ring loft and assign one or two weights to every vertex ring.

    ``weights`` contains one ``[(bone, value), ...]`` entry per ring.  Each
    entry has at most two non-zero values, so the exported asset stays inside
    the BDEF1/BDEF2 contract without relying on importer truncation.
    """
    obj = ring_mesh(name, rings, sides, material, arm, scene, front_cut,
                    longitudinal_smooth)
    groups = {}
    for ring_weights in weights:
        for bone, value in ring_weights:
            if value > 0.001 and bone not in groups:
                groups[bone] = obj.vertex_groups.new(name=bone)
    for ring, ring_weights in enumerate(weights):
        for side in range(sides):
            index = ring * sides + side
            for bone, value in ring_weights:
                if value > 0.001:
                    groups[bone].add([index], value, 'REPLACE')
    return obj


def box_mesh(name, minimum, maximum, material, arm, scene, bone='RF_HEAD'):
    """Create a rigid low-poly box in armature space.

    Headgear is intentionally made from a few large boxes and lofts. This is
    easier to read at Rasterfall distances than a detailed hard-surface mesh,
    and the single-bone weight makes its HEAD mounting explicit.
    """
    x0, y0, z0 = minimum
    x1, y1, z1 = maximum
    vertices = [
        (x0, y0, z0), (x1, y0, z0), (x1, y1, z0), (x0, y1, z0),
        (x0, y0, z1), (x1, y0, z1), (x1, y1, z1), (x0, y1, z1),
    ]
    faces = (
        (0, 3, 2, 1), (4, 5, 6, 7), (0, 1, 5, 4),
        (1, 2, 6, 5), (2, 3, 7, 6), (3, 0, 4, 7),
    )
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], faces)
    mesh.update()
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = arm
    modifier = obj.modifiers.new('RFCHAR Skin', 'ARMATURE')
    modifier.object = arm
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(vertices))), 1.0, 'REPLACE')
    return obj


def vertical_loft(name, profiles, sides, material, arm, scene, weights,
                  front_cut=False, longitudinal_smooth=False):
    """Build a vertical human volume.

    Profiles are ``(z, center_y, width_x, depth_y)``.  The explicit center_y
    offsets are important for the V2 head: the face, cheek and rear skull no
    longer collapse to a symmetric ball.
    """
    rings = [((0.0, center_y, z), (1, 0, 0), (0, 1, 0), width_x, depth_y)
             for z, center_y, width_x, depth_y in profiles]
    return weighted_rings(name, rings, sides, material, arm, scene, weights,
                          front_cut, longitudinal_smooth)


def segment_loft(name, p0, p1, profiles, sides, material, arm, scene,
                 bone_a, bone_b=None):
    """Build an elliptical segment between two joints.

    Profiles are ``(t, radius_a, radius_b, blend_to_bone_b)``.  The basis is
    rebuilt from the segment axis, so the same helper works for horizontal
    arms, vertical legs and the forward-pointing foot.
    """
    p0 = Vector(p0)
    p1 = Vector(p1)
    axis = (p1 - p0).normalized()
    helper = Vector((0, 1, 0)) if abs(axis.y) < 0.8 else Vector((0, 0, 1))
    basis_a = axis.cross(helper).normalized()
    basis_b = axis.cross(basis_a).normalized()
    rings = []
    weights = []
    for t, radius_a, radius_b, blend in profiles:
        rings.append((p0.lerp(p1, t), basis_a, basis_b, radius_a, radius_b))
        if bone_b is None or bone_a == bone_b:
            weights.append([(bone_a, 1.0)])
        else:
            blend = max(0.0, min(1.0, blend))
            weights.append([(bone_a, 1.0 - blend), (bone_b, blend)])
    return weighted_rings(name, rings, sides, material, arm, scene, weights)


def make_armature(scene):
    bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
    armature = bpy.context.object
    armature.name = 'RFCHAR_Armature'
    edit_bones = armature.data.edit_bones
    edit_bones.remove(edit_bones[0])
    bones = {}

    # The role names and direct parent chain are unchanged from RF Humanoid
    # V1.  The longer leg / slightly shorter torso read is expressed here and
    # in the mesh, while runtime still sees the same 21 canonical roles.
    specs = [
        ('RF_ROOT', None, (0, 0, 0), (0, 0, 0.10)),
        ('RF_HIPS', 'RF_ROOT', (0, 0, 0.88), (0, 0, 1.03)),
        ('RF_SPINE', 'RF_HIPS', (0, 0, 1.03), (0, 0, 1.18)),
        ('RF_CHEST', 'RF_SPINE', (0, 0, 1.18), (0, 0, 1.42)),
        ('RF_UPPER_CHEST', 'RF_CHEST', (0, 0, 1.42), (0, 0, 1.56)),
        ('RF_NECK', 'RF_UPPER_CHEST', (0, 0, 1.56), (0, 0, 1.72)),
        ('RF_HEAD', 'RF_NECK', (0, 0, 1.72), (0, 0, 1.98)),
        ('RF_L_SHOULDER', 'RF_UPPER_CHEST', (0.12, 0, 1.50),
         (0.28, 0, 1.50)),
        ('RF_L_UPPER_ARM', 'RF_L_SHOULDER', (0.28, 0, 1.50),
         (0.58, 0, 1.50)),
        ('RF_L_FOREARM', 'RF_L_UPPER_ARM', (0.58, 0, 1.50),
         (0.83, 0, 1.50)),
        ('RF_L_HAND', 'RF_L_FOREARM', (0.83, 0, 1.50),
         (1.01, 0, 1.50)),
        ('RF_R_SHOULDER', 'RF_UPPER_CHEST', (-0.12, 0, 1.50),
         (-0.28, 0, 1.50)),
        ('RF_R_UPPER_ARM', 'RF_R_SHOULDER', (-0.28, 0, 1.50),
         (-0.58, 0, 1.50)),
        ('RF_R_FOREARM', 'RF_R_UPPER_ARM', (-0.58, 0, 1.50),
         (-0.83, 0, 1.50)),
        ('RF_R_HAND', 'RF_R_FOREARM', (-0.83, 0, 1.50),
         (-1.01, 0, 1.50)),
        ('RF_L_UPPER_LEG', 'RF_HIPS', (0.15, 0, 0.88),
         (0.15, 0, 0.50)),
        ('RF_L_LOWER_LEG', 'RF_L_UPPER_LEG', (0.15, 0, 0.50),
         (0.15, 0, 0.12)),
        ('RF_L_FOOT', 'RF_L_LOWER_LEG', (0.15, 0, 0.12),
         (0.15, -0.28, 0.07)),
        ('RF_R_UPPER_LEG', 'RF_HIPS', (-0.15, 0, 0.88),
         (-0.15, 0, 0.50)),
        ('RF_R_LOWER_LEG', 'RF_R_UPPER_LEG', (-0.15, 0, 0.50),
         (-0.15, 0, 0.12)),
        ('RF_R_FOOT', 'RF_R_LOWER_LEG', (-0.15, 0, 0.12),
         (-0.15, -0.28, 0.07)),
    ]
    for name, parent, head, tail in specs:
        bone = edit_bones.new(name)
        bone.head = head
        bone.tail = tail
        bone.use_deform = True
        bones[name] = bone
        if parent:
            bone.parent = bones[parent]

    attachments = {
        'WEAPON_R': ('RF_R_HAND', (-0.93, -0.025, 1.50)),
        'WEAPON_L': ('RF_L_HAND', (0.93, -0.025, 1.50)),
        'FOREGRIP': ('RF_L_HAND', (0.93, -0.025, 1.50)),
        'BACK': ('RF_CHEST', (0, 0.18, 1.36)),
        'CHEST': ('RF_CHEST', (0, -0.22, 1.36)),
        'HEAD': ('RF_HEAD', (0, 0.005, 1.99)),
        'HIP_L': ('RF_L_UPPER_LEG', (0.20, 0, 0.87)),
        'HIP_R': ('RF_R_UPPER_LEG', (-0.20, 0, 0.87)),
    }
    for attachment_id, (parent, head) in attachments.items():
        bone = edit_bones.new('RF_ATTACH_' + attachment_id)
        bone.head = head
        bone.tail = (head[0], head[1], head[2] + 0.08)
        bone.parent = bones[parent]
        bone.use_deform = False

    bpy.ops.object.mode_set(mode='OBJECT')
    return armature


def set_material_color(material, color):
    """Keep Blender's viewport and glTF-exported material colors identical."""
    rgba = (*color, 1.0)
    material.diffuse_color = rgba
    material.use_nodes = True
    principled = material.node_tree.nodes.get('Principled BSDF')
    if principled is None:
        raise RuntimeError('material is missing Principled BSDF: ' + material.name)
    principled.inputs['Base Color'].default_value = rgba


def create_materials():
    def material(name, color):
        result = bpy.data.materials.new(name)
        set_material_color(result, color)
        return result

    return {
        'skin': material('RF_Skin', (0.48, 0.25, 0.16)),
        'hair': material('RF_Hair', (0.025, 0.035, 0.045)),
        'shirt': material('RF_Shirt', (0.14, 0.22, 0.25)),
        'pants': material('RF_Pants', (0.29, 0.34, 0.30)),
        'boots': material('RF_Boots', (0.045, 0.060, 0.065)),
        'headgear': material('RF_Headgear', (0.075, 0.105, 0.115)),
        'headgear_light': material('RF_HeadgearLight', (0.22, 0.275, 0.27)),
        'visor': material('RF_Visor', (0.035, 0.13, 0.16)),
        'mask': material('RF_Mask', (0.095, 0.125, 0.13)),
        'webbing': material('RF_Webbing', (0.055, 0.061, 0.050)),
        'rubber': material('RF_Rubber', (0.020, 0.025, 0.029)),
        'metal': material('RF_Hardware', (0.23, 0.255, 0.27)),
        'cloth': material('RF_OuterCloth', (0.16, 0.175, 0.13)),
        'lens_glint': material('RF_LensGlint', (0.16, 0.30, 0.32)),
    }


def create_body(armature, scene, materials):
    # Body art evolves independently of the shared rig and modular equipment.
    directory = str(Path(__file__).resolve().parent)
    if directory not in sys.path:
        sys.path.insert(0, directory)
    from rf_humanoid_body import create_body as sculpt_body
    sculpt_body(sys.modules[__name__], armature, scene, materials)


def create_headgear(armature, scene, materials, variant):
    """Add one intentionally broad HEAD-mounted coverage module.

    Source forward is -Y. All pieces are rigidly weighted to RF_HEAD rather
    than being loose scene props, so the module follows bind/idle/aim and the
    existing HEAD attachment remains the one stable authoring seam.
    """
    if variant == 'bare':
        return
    if variant == 'tactical-helmet':
        return equipment.helmet(globals(), armature, scene, materials)
    if variant == 'goggles':
        return equipment.goggles(globals(), armature, scene, materials)
    if variant == 'respirator':
        return equipment.respirator(globals(), armature, scene, materials)

    gear = materials['headgear']
    light = materials['headgear_light']
    visor = materials['visor']
    mask = materials['mask']

    if variant == 'headset':
        for side, sign in (('L', 1.0), ('R', -1.0)):
            segment_loft(
                'HeadsetArc' + side,
                (0.205 * sign, 0.035, 1.91),
                (0.0, 0.060, 2.105),
                [(0.0, 0.025, 0.020, 0.0),
                 (0.55, 0.030, 0.024, 0.0),
                 (1.0, 0.026, 0.020, 0.0)],
                SIDES_LIMB, gear, armature, scene, 'RF_HEAD')
            box_mesh('HeadsetEar' + side,
                     (0.205 * sign - 0.025, -0.035, 1.78),
                     (0.205 * sign + 0.025, 0.095, 1.98),
                     gear, armature, scene)
        segment_loft(
            'HeadsetMic', (0.235, -0.015, 1.82), (0.235, -0.275, 1.78),
            [(0.0, 0.018, 0.014, 0.0),
             (0.55, 0.015, 0.012, 0.0),
             (1.0, 0.012, 0.010, 0.0)],
            SIDES_LIMB, light, armature, scene, 'RF_HEAD')
        box_mesh('HeadsetMicTip', (0.205, -0.30, 1.76),
                 (0.265, -0.245, 1.81), light, armature, scene)
        return

    if variant == 'patrol-cap':
        vertical_loft(
            'PatrolCap',
            [(1.975, 0.028, 0.230, 0.215),
             (2.035, 0.035, 0.235, 0.210),
             (2.090, 0.045, 0.145, 0.135),
             (2.115, 0.040, 0.075, 0.075)],
            SIDES_BODY, gear, armature, scene,
            [[('RF_HEAD', 1.0)]] * 4)
        box_mesh('PatrolCapBrim', (-0.215, -0.330, 1.945),
                 (0.215, -0.105, 1.985), light, armature, scene)
        return

    if variant == 'engineering-helmet':
        vertical_loft(
            'EngineeringHelmetShell',
            [(1.910, 0.045, 0.220, 0.185),
             (1.985, 0.055, 0.255, 0.215),
             (2.075, 0.065, 0.245, 0.205),
             (2.135, 0.060, 0.170, 0.150),
             (2.165, 0.045, 0.075, 0.070)],
            SIDES_BODY, light, armature, scene,
            [[('RF_HEAD', 1.0)]] * 5)
        box_mesh('EngineeringEarL', (-0.285, -0.030, 1.745),
                 (-0.205, 0.130, 1.950), gear, armature, scene)
        box_mesh('EngineeringEarR', (0.205, -0.030, 1.745),
                 (0.285, 0.130, 1.950), gear, armature, scene)
        box_mesh('EngineeringBrim', (-0.245, -0.300, 1.895),
                 (0.245, -0.105, 1.945), gear, armature, scene)
        box_mesh('EngineeringLamp', (-0.055, -0.335, 2.000),
                 (0.055, -0.265, 2.080), visor, armature, scene)
        return

    raise ValueError('unknown RF Humanoid V2 headgear: ' + variant)


def create_profession(armature, scene, materials, profession):
    """Build reusable professional equipment in canonical bind space.

    These are authoring profiles, independent of the legacy Hurd identities.
    CHEST/BACK use RF_CHEST and HIP gear uses its upper-leg parent; attachment
    bones remain unweighted. No actor or generic attachment runtime is needed.
    """
    palettes = {
        'rifleman': ((.16, .23, .12), (.23, .28, .16), (.12, .16, .08), (.34, .36, .20)),
        'breacher': ((.075, .10, .14), (.12, .15, .19), (.07, .09, .12), (.24, .30, .36)),
        'recon': ((.25, .29, .18), (.19, .23, .13), (.12, .16, .10), (.35, .38, .24)),
        'medic': ((.20, .28, .29), (.13, .21, .22), (.65, .72, .64), (.72, .14, .055)),
        'engineer': ((.32, .20, .07), (.20, .18, .13), (.18, .16, .105), (.68, .43, .075)),
        'heavy': ((.19, .15, .10), (.15, .14, .11), (.13, .115, .08), (.43, .32, .13)),
        'gunner': ((.526, .089, .052), (.111, .082, .077),
                   (.12, .065, .055), (.64, .070, .032)),
        'gunner-elite': ((.314, .036, .032), (.042, .052, .063),
                         (.075, .087, .10), (.56, .038, .022)),
    }
    shirt, pants, gear, accent = palettes[profession]
    for key, color in (('shirt', shirt), ('pants', pants),
                       ('headgear', gear), ('headgear_light', accent)):
        set_material_color(materials[key], color)
    heads = {'rifleman': ('tactical-helmet',), 'breacher': ('tactical-helmet', 'respirator'),
             'recon': ('patrol-cap', 'headset'), 'medic': ('goggles', 'respirator'),
             'engineer': ('engineering-helmet',), 'heavy': ('tactical-helmet',),
             'gunner': ('patrol-cap', 'goggles'),
             'gunner-elite': ('tactical-helmet', 'respirator')}
    for head in heads[profession]:
        before = {obj.name for obj in scene.objects}
        create_headgear(armature, scene, materials, head)
        for obj in scene.objects:
            if obj.type == 'MESH' and obj.name not in before:
                obj['rf_attachment_slot'] = 'head'
    equipment.profession(globals(), armature, scene, materials, profession)


def create_rifleman_backpack(armature, scene, material, prefix=''):
    """Shared carrier/rigid geometry for the first modular BACK asset."""
    return box_mesh(prefix + 'ShortPack', (-.19, .17, 1.13),
                    (.19, .36, 1.48), material, armature, scene, 'RF_CHEST')


def make_rigid_attachment(scene, armature, materials, name):
    """Build only attachment geometry and rebase it to its stable socket."""
    before = {obj.name for obj in scene.objects}
    if name == 'ballistic-goggles':
        equipment.goggles(globals(), armature, scene, materials)
        origin = Vector((0.0, 0.005, 1.99))
    elif name.startswith('cargo-thigh-'):
        side = name[-1]
        equipment.outer_thigh(globals(), armature, scene, materials, side)
        origin = Vector((0.20 if side == 'l' else -0.20, 0.0, 0.87))
    elif name == 'tactical-helmet':
        create_headgear(armature, scene, materials, name)
        origin = Vector((0.0, 0.005, 1.99))
    elif name == 'backpack':
        create_rifleman_backpack(armature, scene, materials['headgear'])
        origin = Vector((0.0, 0.18, 1.36))
    else:
        profession, slot = name.rsplit('-', 1)
        if slot in ('l', 'r'):
            profession, slot = profession.rsplit('-', 1)[0], 'hip-' + slot
        create_profession(armature, scene, materials, profession)
        origins = {
            'head': (0.0, 0.005, 1.99),
            'chest': (0.0, -0.22, 1.36),
            'back': (0.0, 0.18, 1.36),
            'hip-l': (0.20, 0.0, 0.87),
            'hip-r': (-0.20, 0.0, 0.87),
        }
        origin = Vector(origins[slot])
        for obj in list(scene.objects):
            if (obj.type == 'MESH' and obj.name not in before and
                    obj.get('rf_attachment_slot') != slot):
                bpy.data.objects.remove(obj, do_unlink=True)
    meshes = [obj for obj in scene.objects
              if obj.type == 'MESH' and obj.name not in before]
    for obj in meshes:
        for vertex in obj.data.vertices:
            vertex.co -= origin
        obj.parent = None
        obj.modifiers.clear()
        obj.vertex_groups.clear()
    bpy.data.objects.remove(armature, do_unlink=True)
    return meshes


def patch_glb_skeleton(path):
    """Ensure Blender writes the RFCHAR-required skin skeleton node."""
    raw = path.read_bytes()
    json_length, json_kind = struct.unpack_from('<II', raw, 12)
    document = json.loads(raw[20:20 + json_length].rstrip(b' \0'))
    root = next(index for index, node in enumerate(document['nodes'])
                if node.get('name') == 'RF_ROOT')
    document['skins'][0]['skeleton'] = root
    encoded = json.dumps(document, separators=(',', ':')).encode()
    encoded += b' ' * (-len(encoded) % 4)
    binary_at = 20 + json_length
    binary_length, binary_kind = struct.unpack_from('<II', raw, binary_at)
    blob = raw[binary_at + 8:binary_at + 8 + binary_length]
    total = 12 + 8 + len(encoded) + 8 + len(blob)
    path.write_bytes(
        b'glTF' + struct.pack('<II', 2, total) +
        struct.pack('<II', len(encoded), json_kind) + encoded +
        struct.pack('<II', len(blob), binary_kind) + blob)


def main():
    args = arguments()
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0

    armature = make_armature(scene)
    materials = create_materials()
    if args.rigid_attachment:
        if args.profession or args.headgear != 'bare':
            raise ValueError('--rigid-attachment cannot combine with character variants')
        make_rigid_attachment(scene, armature, materials, args.rigid_attachment)
    else:
        create_body(armature, scene, materials)
    if args.profession:
        if args.headgear != 'bare':
            raise ValueError('--profession owns its headgear combination')
        create_profession(armature, scene, materials, args.profession)
        # Legacy full carriers remain an honest A/B for the modular recipe.
        shared_materials = create_materials()
        if args.profession in ('rifleman', 'breacher', 'heavy', 'gunner-elite'):
            equipment.goggles(globals(), armature, scene, shared_materials)
        for side in ('l', 'r'):
            equipment.outer_thigh(globals(), armature, scene, shared_materials, side)
    elif not args.rigid_attachment:
        create_headgear(armature, scene, materials, args.headgear)

    # Merge authored pieces before export: RFM2 has a 32-primitive budget.
    # glTF emits one primitive per material on the joined mesh, retaining
    # vertex groups and the one armature modifier without changing geometry.
    bpy.ops.object.select_all(action='DESELECT')
    meshes = [obj for obj in scene.objects if obj.type == 'MESH']
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()

    # Keep every object at identity TRS; all geometry is authored in armature
    # space and the canonical GLB exporter handles only the Y-up conversion.
    for obj in list(scene.objects):
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0] if args.rigid_attachment else armature
    bpy.ops.export_scene.gltf(
        filepath=args.output,
        export_format='GLB',
        use_selection=True,
        export_skins=not args.rigid_attachment,
        export_influence_nb=4,
        export_all_influences=True,
        export_morph=False,
        export_animations=False,
        export_yup=True,
        export_apply=False,
        export_armature_object_remove=not args.rigid_attachment)

    path = Path(args.output)
    if not args.rigid_attachment:
        patch_glb_skeleton(path)
    vertex_count = sum(len(obj.data.vertices) for obj in scene.objects
                       if obj.type == 'MESH')
    triangle_count = 0
    for obj in scene.objects:
        if obj.type != 'MESH':
            continue
        obj.data.calc_loop_triangles()
        triangle_count += len(obj.data.loop_triangles)
    print('rfchar-humanoid-v2: %s headgear=%s rigid=%s source_vertices=%d source_triangles=%d materials=%d' %
          (args.output, args.headgear, args.rigid_attachment, vertex_count, triangle_count, len(materials)))


if __name__ == '__main__':
    main()
