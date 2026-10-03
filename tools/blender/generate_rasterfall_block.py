"""RFCHAR Block teammate: rigid cuboids on the canonical Humanoid armature.

Run with Blender --background --factory-startup --python this_file -- --output FILE.
Rigid pieces use single-bone weights; a small collar bridges the neck joint.
"""
import argparse
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import generate_rasterfall_humanoid_v2 as humanoid


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1.0
    arm = humanoid.make_armature(scene)
    # Distinct authored defaults also identify the four recolorable surfaces.
    materials = []
    for name, color in [('Shirt', 0xD94F70), ('Pants', 0x25354A),
                        ('Skin', 0xF0C3A5), ('Hair', 0x512B3A)]:
        material = bpy.data.materials.new('Block_' + name)
        srgb = [((color >> shift) & 255) / 255 for shift in (16, 8, 0)]
        linear = [v / 12.92 if v <= .04045 else ((v + .055) / 1.055) ** 2.4 for v in srgb]
        humanoid.set_material_color(material, linear)
        materials.append(material)

    def box(name, lo, hi, mat, bone):
        return humanoid.box_mesh(name, lo, hi, materials[mat], arm, scene, bone)

    box('Hips', (-.25, -.15, .76), (.25, .15, 1.04), 1, 'RF_HIPS')
    box('Waist', (-.25, -.155, 1.00), (.25, .155, 1.20), 0, 'RF_SPINE')
    box('Chest', (-.27, -.16, 1.16), (.27, .16, 1.45), 0, 'RF_CHEST')
    box('Shoulders', (-.28, -.16, 1.40), (.28, .16, 1.56), 0, 'RF_UPPER_CHEST')
    box('Neck', (-.08, -.08, 1.52), (.08, .08, 1.72), 2, 'RF_NECK')
    collar = box('Collar', (-.085, -.085, 1.55), (.085, .085, 1.59), 0, 'RF_UPPER_CHEST')
    collar.vertex_groups['RF_UPPER_CHEST'].add(list(range(8)), .5, 'REPLACE')
    collar.vertex_groups.new(name='RF_NECK').add(list(range(8)), .5, 'REPLACE')
    box('Head', (-.225, -.22, 1.66), (.225, .19, 2.075), 2, 'RF_HEAD')
    box('Hair', (-.23, -.225, 2.01), (.23, .195, 2.09), 3, 'RF_HEAD')
    # Preserve the simple cross-shaped face, on separated opaque surfaces.
    box('Face', (-.115, -.228, 1.71), (.115, -.223, 2.005), 3, 'RF_HEAD')
    box('FaceVertical', (-.025, -.235, 1.76), (.025, -.23, 1.955), 2, 'RF_HEAD')
    box('FaceHorizontal', (-.115, -.24, 1.835), (.115, -.236, 1.875), 2, 'RF_HEAD')
    for side, sign in [('L', 1), ('R', -1)]:
        prefix = 'RF_' + side + '_'

        def limb(name, x0, x1, y0, y1, z0, z1, mat, bone):
            xs = sorted((x0 * sign, x1 * sign))
            box(side + name, (xs[0], y0, z0), (xs[1], y1, z1), mat, prefix + bone)

        limb('UpperArm', .27, .60, -.067, .067, 1.433, 1.567, 0, 'UPPER_ARM')
        limb('Forearm', .56, .845, -.06, .06, 1.44, 1.56, 2, 'FOREARM')
        limb('Palm', .815, .966, -.065, .065, 1.471, 1.529, 2, 'HAND')
        limb('Thigh', .065, .235, -.12, .12, .48, .90, 1, 'UPPER_LEG')
        limb('Shin', .065, .235, -.11, .11, .10, .52, 1, 'LOWER_LEG')
        limb('Foot', .06, .24, -.28, .12, 0, .14, 1, 'FOOT')
        for bone in arm.data.bones:
            if not bone.name.startswith(prefix + 'FINGER_'):
                continue
            a, b = bone.head_local, bone.tail_local
            box(bone.name, tuple(min(a[i], b[i]) - .012 for i in range(3)),
                tuple(max(a[i], b[i]) + .012 for i in range(3)), 2, bone.name)

    bpy.ops.object.select_all(action='DESELECT')
    meshes = [obj for obj in scene.objects if obj.type == 'MESH']
    for obj in meshes:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = meshes[0]
    bpy.ops.object.join()
    for obj in scene.objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = arm
    path = Path(args.output)
    path.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(path), export_format='GLB',
        use_selection=True, export_skins=True, export_influence_nb=4,
        export_all_influences=True, export_morph=False, export_animations=False,
        export_yup=True, export_apply=False, export_armature_object_remove=True)
    humanoid.patch_glb_skeleton(path)
    print('RFCHAR Block:', path)


if __name__ == '__main__':
    main()
