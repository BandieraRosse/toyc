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


SIDES_BODY = 10
SIDES_LIMB = 8
HEADGEAR_NAMES = (
    'bare', 'headset', 'patrol-cap', 'goggles', 'respirator',
    'tactical-helmet', 'engineering-helmet',
)
PROFESSIONS = ('rifleman', 'breacher', 'recon', 'medic', 'engineer', 'heavy')


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", required=True)
    parser.add_argument("--headgear", choices=HEADGEAR_NAMES, default="bare")
    parser.add_argument("--profession", choices=PROFESSIONS)
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:]
                             if "--" in sys.argv else [])


def ring_mesh(name, rings, sides, material, arm, scene, front_cut=False):
    """Create a capped ring loft.

    Each ring is ``(center, basis_a, basis_b, radius_a, radius_b)``.  The
    helper deliberately leaves the mesh flat shaded: the broad facets are a
    feature of the low-poly body, not a missing normal pass.
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
    mesh.materials.append(material)
    obj = bpy.data.objects.new(name, mesh)
    scene.collection.objects.link(obj)
    obj.parent = arm
    modifier = obj.modifiers.new("RFCHAR Skin", "ARMATURE")
    modifier.object = arm
    return obj


def weighted_rings(name, rings, sides, material, arm, scene, weights,
                   front_cut=False):
    """Create a ring loft and assign one or two weights to every vertex ring.

    ``weights`` contains one ``[(bone, value), ...]`` entry per ring.  Each
    entry has at most two non-zero values, so the exported asset stays inside
    the BDEF1/BDEF2 contract without relying on importer truncation.
    """
    obj = ring_mesh(name, rings, sides, material, arm, scene, front_cut)
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
                  front_cut=False):
    """Build a vertical human volume.

    Profiles are ``(z, center_y, width_x, depth_y)``.  The explicit center_y
    offsets are important for the V2 head: the face, cheek and rear skull no
    longer collapse to a symmetric ball.
    """
    rings = [((0.0, center_y, z), (1, 0, 0), (0, 1, 0), width_x, depth_y)
             for z, center_y, width_x, depth_y in profiles]
    return weighted_rings(name, rings, sides, material, arm, scene, weights,
                          front_cut)


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


def create_materials():
    def material(name, color):
        result = bpy.data.materials.new(name)
        result.diffuse_color = (*color, 1.0)
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
    }


def create_body(armature, scene, materials):
    shirt = materials['shirt']
    pants = materials['pants']
    skin = materials['skin']
    hair = materials['hair']
    boots = materials['boots']

    # Pelvis is a full second volume. Its widest ring is around the hips and
    # its lower edge narrows into the thighs; this avoids the V1.1 skirt/armor
    # plate read.
    vertical_loft(
        'Pelvis',
        [
            (0.70, 0.000, 0.20, 0.14),
            (0.77, 0.018, 0.255, 0.188),
            (0.88, 0.025, 0.275, 0.205),
            (0.98, 0.000, 0.255, 0.16),
            (1.04, 0.000, 0.215, 0.115),
        ],
        SIDES_BODY, pants, armature, scene,
        [
            [('RF_HIPS', 1.0)],
            [('RF_HIPS', 1.0)],
            [('RF_HIPS', 0.85), ('RF_SPINE', 0.15)],
            [('RF_HIPS', 0.35), ('RF_SPINE', 0.65)],
            [('RF_SPINE', 1.0)],
        ])

    # One continuous shirt loft carries the neutral waist into the ribcage.
    # Width and depth both change gradually; there is no broad rectangular
    # chest slab.
    vertical_loft(
        'Torso',
        [
            (0.96, -0.005, 0.270, 0.190),
            (1.04, -0.008, 0.235, 0.150),
            (1.14, -0.010, 0.240, 0.160),
            (1.25, -0.012, 0.255, 0.195),
            (1.36, -0.008, 0.280, 0.220),
            (1.47, 0.004, 0.290, 0.215),
            (1.56, 0.010, 0.255, 0.175),
        ],
        SIDES_BODY, shirt, armature, scene,
        [
            [('RF_HIPS', 0.55), ('RF_SPINE', 0.45)],
            [('RF_HIPS', 0.30), ('RF_SPINE', 0.70)],
            [('RF_SPINE', 1.0)],
            [('RF_SPINE', 0.65), ('RF_CHEST', 0.35)],
            [('RF_CHEST', 0.80), ('RF_UPPER_CHEST', 0.20)],
            [('RF_CHEST', 0.45), ('RF_UPPER_CHEST', 0.55)],
            [('RF_UPPER_CHEST', 1.0)],
        ])

    # A tapered neck bridges the shirt collar and the head instead of ending
    # in a square peg.
    vertical_loft(
        'Neck',
        [
            (1.52, 0.005, 0.105, 0.095),
            (1.59, 0.008, 0.098, 0.090),
            (1.70, 0.012, 0.082, 0.078),
            (1.75, 0.015, 0.078, 0.073),
        ],
        SIDES_LIMB, skin, armature, scene,
        [[('RF_NECK', 1.0)]] * 4)

    # Head profile: narrow jaw/chin, broad cheek and temple, then a smaller
    # crown. Center-y changes expose forehead/face/rear-skull volume from the
    # side without adding small facial features.
    vertical_loft(
        'HeadMass',
        [
            (1.65, -0.020, 0.135, 0.105),  # jaw underside
            (1.70, -0.055, 0.155, 0.135),  # chin
            (1.76, -0.035, 0.205, 0.175),  # cheek
            (1.84, 0.000, 0.220, 0.190),  # temple
            (1.92, 0.018, 0.205, 0.180),  # forehead / upper skull
            (2.00, 0.020, 0.180, 0.160),  # crown
            (2.045, 0.010, 0.105, 0.105),
        ],
        SIDES_BODY, skin, armature, scene,
        [[('RF_HEAD', 1.0)]] * 7)

    # A closed crown stops at a clean hairline; no independent dark strip is
    # drawn across the upper face. Two restrained side locks complete the
    # silhouette while leaving the simplified face as one stable skin plane.
    vertical_loft(
        'HairCap',
        [
            (1.94, 0.025, 0.205, 0.190),
            (1.98, 0.030, 0.220, 0.200),
            (2.020, 0.040, 0.215, 0.190),
            (2.060, 0.050, 0.185, 0.165),
            (2.080, 0.035, 0.120, 0.115),
        ],
        SIDES_BODY, hair, armature, scene,
        [[('RF_HEAD', 1.0)]] * 5,
        front_cut=False)
    for side, sign in (('L', 1.0), ('R', -1.0)):
        segment_loft(
            'HairLock' + side, (0.18 * sign, 0.025, 1.78),
            (0.19 * sign, 0.045, 1.96),
            [(0.0, 0.040, 0.045, 0.0),
             (0.45, 0.055, 0.060, 0.0),
             (1.0, 0.045, 0.050, 0.0)],
            SIDES_LIMB, hair, armature, scene, 'RF_HEAD')

    # Shoulder caps establish a deltoid volume separate from the ribcage.
    for side, sign in (('L', 1.0), ('R', -1.0)):
        segment_loft(
            side + 'Shoulder', (0.23 * sign, 0.0, 1.52),
            (0.44 * sign, 0.0, 1.50),
            [(0.0, 0.090, 0.125, 0.0),
             (0.35, 0.120, 0.135, 0.25),
             (0.70, 0.110, 0.115, 0.75),
             (1.0, 0.098, 0.100, 1.0)],
            SIDES_LIMB, shirt, armature, scene,
            'RF_' + side + '_SHOULDER', 'RF_' + side + '_UPPER_ARM')

        # Upper arm tapers into a readable elbow rather than staying a tube.
        segment_loft(
            side + 'UpperArm', (0.39 * sign, 0.0, 1.50),
            (0.61 * sign, 0.0, 1.50),
            [(0.0, 0.103, 0.108, 0.0),
             (0.28, 0.108, 0.103, 0.0),
             (0.70, 0.090, 0.082, 0.0),
             (0.88, 0.082, 0.075, 0.45),
             (1.0, 0.078, 0.070, 1.0)],
            SIDES_LIMB, shirt, armature, scene,
            'RF_' + side + '_UPPER_ARM', 'RF_' + side + '_FOREARM')

        # Forearm re-widens slightly in the middle and then pinches to a
        # narrow wrist. It is pants-like undersuit color, not a blocky glove.
        segment_loft(
            side + 'Forearm', (0.58 * sign, 0.0, 1.50),
            (0.84 * sign, 0.0, 1.50),
            [(0.0, 0.082, 0.075, 0.0),
             (0.25, 0.086, 0.078, 0.0),
             (0.60, 0.090, 0.080, 0.0),
             (0.84, 0.073, 0.068, 0.35),
             (1.0, 0.065, 0.060, 1.0)],
            SIDES_LIMB, shirt, armature, scene,
            'RF_' + side + '_FOREARM', 'RF_' + side + '_HAND')

        segment_loft(
            side + 'Hand', (0.81 * sign, -0.005, 1.50),
            (1.00 * sign, -0.015, 1.50),
            [(0.0, 0.065, 0.060, 0.0),
             (0.30, 0.078, 0.067, 0.0),
             (0.78, 0.073, 0.062, 0.0),
             (1.0, 0.052, 0.048, 0.0)],
            SIDES_LIMB, skin, armature, scene, 'RF_' + side + '_HAND')

        # Thighs are full at the hip and taper toward an explicit knee volume.
        segment_loft(
            side + 'Thigh', (0.15 * sign, 0.0, 0.90),
            (0.15 * sign, 0.0, 0.51),
            [(0.0, 0.132, 0.155, 0.0),
             (0.18, 0.138, 0.155, 0.0),
             (0.52, 0.125, 0.120, 0.0),
             (0.80, 0.118, 0.108, 0.35),
             (1.0, 0.105, 0.098, 1.0)],
            SIDES_LIMB, pants, armature, scene,
            'RF_' + side + '_UPPER_LEG', 'RF_' + side + '_LOWER_LEG')

        knee = vertical_loft(
            side + 'Knee',
            [(0.46, -0.012, 0.098, 0.092),
             (0.51, -0.018, 0.108, 0.105),
             (0.56, -0.010, 0.102, 0.095)],
            SIDES_LIMB, pants, armature, scene,
            [[('RF_' + side + '_UPPER_LEG', 0.35),
              ('RF_' + side + '_LOWER_LEG', 0.65)],
             [('RF_' + side + '_LOWER_LEG', 1.0)],
             [('RF_' + side + '_LOWER_LEG', 1.0)]])
        # Author in armature space; object TRS must stay identity for RFCHAR.
        for vertex in knee.data.vertices:
            vertex.co.x += 0.15 * sign

        # The calf peaks above mid-shin and narrows clearly at the ankle.
        segment_loft(
            side + 'Calf', (0.15 * sign, 0.0, 0.55),
            (0.15 * sign, 0.0, 0.12),
            [(0.0, 0.100, 0.094, 0.0),
             (0.25, 0.114, 0.112, 0.0),
             (0.48, 0.108, 0.106, 0.0),
             (0.74, 0.084, 0.082, 0.25),
             (0.92, 0.069, 0.066, 0.70),
             (1.0, 0.066, 0.064, 1.0)],
            SIDES_LIMB, pants, armature, scene,
            'RF_' + side + '_LOWER_LEG', 'RF_' + side + '_FOOT')

        # A shallow, forward wedge gives the boot a toe/heel read without the
        # V1.1 rock shape. The lowest ring is close to z=0 so the mesh remains
        # grounded in the canonical bind pose.
        foot = segment_loft(
            side + 'Foot', (0.15 * sign, -0.015, 0.105),
            (0.15 * sign, -0.31, 0.070),
            [(0.0, 0.085, 0.080, 0.0),
             (0.22, 0.108, 0.090, 0.0),
             (0.72, 0.108, 0.075, 0.0),
             (1.0, 0.082, 0.060, 0.0)],
            SIDES_LIMB, boots, armature, scene,
            'RF_' + side + '_FOOT')
        # A planar sole gives both boots an actual canonical ground contact.
        for vertex in foot.data.vertices:
            if vertex.co.z < 0.055:
                vertex.co.z = 0.0


def create_headgear(armature, scene, materials, variant):
    """Add one intentionally broad HEAD-mounted coverage module.

    Source forward is -Y. All pieces are rigidly weighted to RF_HEAD rather
    than being loose scene props, so the module follows bind/idle/aim and the
    existing HEAD attachment remains the one stable authoring seam.
    """
    if variant == 'bare':
        return

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

    if variant == 'goggles':
        box_mesh('GoggleLensL', (-0.175, -0.270, 1.785),
                 (-0.018, -0.185, 1.875), visor, armature, scene)
        box_mesh('GoggleLensR', (0.018, -0.270, 1.785),
                 (0.175, -0.185, 1.875), visor, armature, scene)
        box_mesh('GoggleBridge', (-0.030, -0.265, 1.815),
                 (0.030, -0.185, 1.850), light, armature, scene)
        box_mesh('GoggleStrapL', (-0.225, 0.030, 1.805),
                 (-0.170, 0.085, 1.875), gear, armature, scene)
        box_mesh('GoggleStrapR', (0.170, 0.030, 1.805),
                 (0.225, 0.085, 1.875), gear, armature, scene)
        return

    if variant == 'respirator':
        box_mesh('RespiratorShell', (-0.145, -0.255, 1.635),
                 (0.145, -0.130, 1.785), mask, armature, scene)
        box_mesh('RespiratorFilterL', (-0.185, -0.300, 1.655),
                 (-0.105, -0.225, 1.735), gear, armature, scene)
        box_mesh('RespiratorFilterR', (0.105, -0.300, 1.655),
                 (0.185, -0.225, 1.735), gear, armature, scene)
        box_mesh('RespiratorStrapL', (-0.225, -0.040, 1.700),
                 (-0.175, 0.060, 1.755), light, armature, scene)
        box_mesh('RespiratorStrapR', (0.175, -0.040, 1.700),
                 (0.225, 0.060, 1.755), light, armature, scene)
        return

    if variant == 'tactical-helmet':
        vertical_loft(
            'TacticalHelmetShell',
            [(1.895, 0.025, 0.225, 0.195),
             (1.965, 0.030, 0.245, 0.215),
             (2.060, 0.040, 0.235, 0.205),
             (2.145, 0.045, 0.185, 0.170),
             (2.185, 0.035, 0.095, 0.090)],
            SIDES_BODY, gear, armature, scene,
            [[('RF_HEAD', 1.0)]] * 5)
        box_mesh('TacticalHelmetBrow', (-0.235, -0.285, 1.875),
                 (0.235, -0.105, 1.935), light, armature, scene)
        box_mesh('TacticalHelmetEarL', (-0.275, -0.020, 1.765),
                 (-0.205, 0.105, 1.970), gear, armature, scene)
        box_mesh('TacticalHelmetEarR', (0.205, -0.020, 1.765),
                 (0.275, 0.105, 1.970), gear, armature, scene)
        box_mesh('TacticalHelmetVisor', (-0.190, -0.245, 1.790),
                 (0.190, -0.185, 1.835), visor, armature, scene)
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
    """V1 visual carriers: a few large masses, weighted to socket parents.

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
    }
    shirt, pants, gear, accent = palettes[profession]
    for key, color in (('shirt', shirt), ('pants', pants),
                       ('headgear', gear), ('headgear_light', accent)):
        materials[key].diffuse_color = (*color, 1.0)
    g, a = materials['headgear'], materials['headgear_light']

    def box(name, lo, hi, mat=g, bone='RF_CHEST'):
        return box_mesh(profession + '_' + name, lo, hi, mat, armature, scene, bone)

    def plate(width, bottom, top, front, mat=g):
        # Taper at the clavicle leaves the shoulder/upper-arm rotation clear.
        return vertical_loft(profession + '_Chest',
            [(bottom, front + .045, width * .86, .065),
             (bottom + .07, front + .025, width, .075),
             (top - .07, front + .030, width, .070),
             (top, front + .045, width * .72, .055)],
            8, mat, armature, scene, [[('RF_CHEST', 1.0)]] * 4)

    heads = {'rifleman': ('tactical-helmet',), 'breacher': ('tactical-helmet', 'respirator'),
             'recon': ('patrol-cap', 'headset'), 'medic': ('goggles', 'respirator'),
             'engineer': ('engineering-helmet',), 'heavy': ('tactical-helmet',)}
    for head in heads[profession]:
        create_headgear(armature, scene, materials, head)
    if profession == 'rifleman':
        plate(.205, 1.12, 1.46, -.235)
        box('ShortPack', (-.19, .17, 1.13), (.19, .36, 1.48))
        box('MagazineBlock', (-.17, -.29, 1.08), (.17, -.23, 1.22), a)
    elif profession == 'breacher':
        plate(.255, 1.035, 1.50, -.255)
        box('Collar', (-.15, -.18, 1.48), (.15, .17, 1.60), a)
        box('FlatBackPlate', (-.245, .18, 1.08), (.245, .30, 1.50))
        box('HipShield', (-.20, -.205, .86), (.20, -.15, 1.04), a, 'RF_HIPS')
    elif profession == 'recon':
        plate(.16, 1.15, 1.34, -.215)
        box('NarrowPack', (-.12, .19, 1.20), (.12, .32, 1.58))
        box('RigBand', (-.19, -.25, 1.12), (.19, -.20, 1.20), a)
    elif profession == 'medic':
        plate(.22, 1.12, 1.46, -.235)
        box('MedicalPack', (-.27, .18, 1.02), (.27, .44, 1.60))
        # Broad orange panels; identity stays readable without tiny symbols.
        box('ChestPanel', (-.10, -.29, 1.22), (.10, -.245, 1.42), a)
        box('BackPanel', (-.15, .435, 1.18), (.15, .455, 1.47), a)
        for sign in (-1, 1):
            box('PackSide' + str(sign), (sign * .27 - .025, .23, 1.18),
                (sign * .27 + .025, .39, 1.46), a)
    elif profession == 'engineer':
        plate(.185, 1.14, 1.40, -.225, a)
        box('ToolCase', (-.23, .19, 1.03), (.21, .40, 1.43))
        box('ToolHandle', (-.27, .25, 1.39), (-.20, .34, 1.76), a)
        box('ToolHead', (-.36, .24, 1.65), (-.10, .35, 1.77), a)
        box('HipToolbox', (.235, -.09, .65), (.40, .18, .94), a, 'RF_L_UPPER_LEG')
    else:
        plate(.285, 1.05, 1.51, -.26)
        box('AmmoBack', (-.32, .19, 1.01), (.32, .48, 1.62))
        for sign, side in ((-1, 'R'), (1, 'L')):
            box('AmmoStack' + side, (sign * .30 - .06, .22, 1.12),
                (sign * .30 + .06, .43, 1.58), a)
            box('HipAmmo' + side, (sign * .27 - .075, -.08, .70),
                (sign * .27 + .075, .16, .94), g, 'RF_' + side + '_UPPER_LEG')
        box('ChestAmmo', (-.22, -.31, 1.10), (.22, -.25, 1.24), a)


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
    create_body(armature, scene, materials)
    if args.profession:
        if args.headgear != 'bare':
            raise ValueError('--profession owns its headgear combination')
        create_profession(armature, scene, materials, args.profession)
    else:
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
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.gltf(
        filepath=args.output,
        export_format='GLB',
        use_selection=True,
        export_skins=True,
        export_influence_nb=4,
        export_all_influences=True,
        export_morph=False,
        export_animations=False,
        export_yup=True,
        export_apply=False,
        export_armature_object_remove=True)

    path = Path(args.output)
    patch_glb_skeleton(path)
    vertex_count = sum(len(obj.data.vertices) for obj in scene.objects
                       if obj.type == 'MESH')
    triangle_count = 0
    for obj in scene.objects:
        if obj.type != 'MESH':
            continue
        obj.data.calc_loop_triangles()
        triangle_count += len(obj.data.loop_triangles)
    print('rfchar-humanoid-v2: %s headgear=%s source_vertices=%d source_triangles=%d materials=%d' %
          (args.output, args.headgear, vertex_count, triangle_count, len(materials)))


if __name__ == '__main__':
    main()
