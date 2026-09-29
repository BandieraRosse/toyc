"""Assembly and neutral export never execute geometry generators."""
import json
import math
import struct
from pathlib import Path

import bpy

from .records import rig_record, scene_record
from .storage import (load_objects, manifest_check, material_key, part_record,
                      read, resolve, sha, write)


def assemble(manifest_path, output):
    root, manifest = manifest_check(manifest_path)
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(output)
    bpy.ops.wm.open_mainfile(filepath=str(resolve(root, manifest['review']['file'])))
    bpy.context.preferences.filepaths.save_version = 0
    rig = manifest['rig']
    with bpy.data.libraries.load(str(resolve(root, rig['file'])), link=False) as (src, dst):
        dst.objects = [rig['object']]
        dst.actions = rig['actions']
    arm = dst.objects[0]
    bpy.context.scene.collection.objects.link(arm)
    for action in dst.actions:
        action.use_fake_user = True
    arm.animation_data_create()
    arm.animation_data.action = bpy.data.actions[rig['review_action']]
    for part_id, entry in manifest['parts'].items():
        objects = load_objects(resolve(root, entry['file']), entry['objects'])
        if set(o.get('object_id') for o in objects) != set(entry['records']):
            raise ValueError('Object identity mismatch: ' + part_id)
        if any(o.get('part_id') != part_id or o.type != 'MESH' for o in objects):
            raise ValueError('Part ownership mismatch: ' + part_id)
        if part_record(objects) != entry['records']:
            raise ValueError('Part semantic mismatch: ' + part_id)
        collection = bpy.data.collections.new('Part / ' + part_id)
        bpy.context.scene.collection.children.link(collection)
        for obj in objects:
            if obj.parent or any(m.type == 'ARMATURE' for m in obj.modifiers):
                raise ValueError('Unexpected rig embedded in part')
            for old in list(obj.users_collection):
                old.objects.unlink(obj)
            collection.objects.link(obj)
            obj.parent = arm
            mod = obj.modifiers.new('RFCHAR skin', 'ARMATURE')
            mod.object = arm
    scene = bpy.context.scene
    scene.frame_set(manifest['review']['frame'])
    scene['assembly_manifest'] = str(Path(manifest_path).resolve())
    scene['assembly_sha256'] = sha(manifest_path)
    scene['design_version'] = manifest['asset_id'] + ' modular authoring'
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(output))
    return output


def verify(blend, manifest_path, baseline_path=None, allowed=()):
    root, manifest = manifest_check(manifest_path)
    bpy.ops.wm.open_mainfile(filepath=str(Path(blend).resolve()))
    objects = [o for o in bpy.context.scene.objects if o.get('part_id')]
    if len({o['object_id'] for o in objects}) != len(objects):
        raise ValueError('Duplicate object ID')
    actual = {}
    for part_id, entry in manifest['parts'].items():
        selected = [o for o in objects if o['part_id'] == part_id]
        records = part_record(selected)
        if records != entry['records']:
            raise ValueError('Assembly differs from part source: ' + part_id)
        actual[part_id] = records
    if len(objects) != sum(len(e['records']) for e in manifest['parts'].values()):
        raise ValueError('Unexpected part')
    arms = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
    if len(arms) != 1:
        raise ValueError('Expected one armature')
    arm = arms[0]
    for obj in objects:
        mods = [m for m in obj.modifiers if m.type == 'ARMATURE']
        if obj.parent != arm or len(mods) != 1 or mods[0].object != arm:
            raise ValueError('Incorrect rig binding: ' + obj.name)
        for v in obj.data.vertices:
            if not all(math.isfinite(c) for c in v.co):
                raise ValueError('Non-finite vertex')
            weights = [g for g in v.groups if g.weight > 1e-6]
            if any(not math.isfinite(g.weight) or g.weight < 0 for g in v.groups) or not 1 <= len(weights) <= 2 or abs(sum(g.weight for g in weights) - 1) > .001:
                raise ValueError('Invalid two-weight binding')
            for g in weights:
                bone = arm.data.bones.get(obj.vertex_groups[g.group].name)
                if bone is None or not bone.use_deform:
                    raise ValueError('Invalid weighted bone')
    rig = rig_record(arm)
    studio = scene_record(bpy.context.scene)
    if rig != manifest['rig']['record']:
        raise ValueError('Rig/actions changed')
    if studio != manifest['review']['record']:
        raise ValueError('Review scene changed')
    baseline = read(baseline_path) if baseline_path else manifest
    if not set(allowed) <= set(manifest['parts']):
        raise ValueError('Unknown allowed part')
    for key in ('rig', 'review', 'interfaces'):
        if baseline[key] != manifest[key]:
            raise ValueError('Shared contract changed: ' + key)
    if set(baseline['parts']) != set(actual):
        raise ValueError('Part inventory changed')
    for key in actual:
        if key not in allowed and actual[key] != baseline['parts'][key]['records']:
            raise ValueError('Change outside selected parts: ' + key)
    return {'parts': list(actual), 'allowed_changes': list(allowed),
            'vertices': sum(len(o.data.vertices) for o in objects),
            'bones': len(arm.data.bones), 'rig_actions_unchanged': True,
            'review_unchanged': True, 'unselected_parts_unchanged': True}


def export(blend, output):
    output = Path(output).resolve()
    if output.exists():
        raise FileExistsError(output)
    bpy.ops.wm.open_mainfile(filepath=str(Path(blend).resolve()))
    scene = bpy.context.scene
    arm = next(o for o in scene.objects if o.type == 'ARMATURE')
    arm.animation_data.action = None
    for pb in arm.pose.bones:
        pb.rotation_mode = 'QUATERNION'
        pb.rotation_quaternion = (1, 0, 0, 0)
        pb.location = (0, 0, 0)
        pb.scale = (1, 1, 1)
    bpy.context.view_layer.update()
    bpy.ops.object.select_all(action='DESELECT')
    copies, materials = [], {}
    for source in sorted((o for o in scene.objects if o.get('part_id')),
                         key=lambda o: o['object_id']):
        obj = source.copy()
        obj.data = source.data.copy()
        scene.collection.objects.link(obj)
        if obj.data.shape_keys:
            obj.shape_key_clear()
        for i, mat in enumerate(obj.data.materials):
            if mat:
                key = material_key(mat)
                if not key[0]:
                    raise ValueError('Missing material export identity')
                obj.data.materials[i] = materials.setdefault(key, mat)
        obj.select_set(True)
        copies.append(obj)
    bpy.context.view_layer.objects.active = copies[0]
    bpy.ops.object.join()
    bpy.context.object.name = 'RFCHAR_EXPORT'
    arm.select_set(True)
    bpy.context.view_layer.objects.active = arm
    output.parent.mkdir(parents=True, exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(output), export_format='GLB', use_selection=True,
        export_skins=True, export_influence_nb=4, export_all_influences=True,
        export_morph=False, export_animations=False, export_yup=True, export_apply=False,
        export_armature_object_remove=True, export_extras=False)
    raw = output.read_bytes()
    size, kind = struct.unpack_from('<II', raw, 12)
    doc = json.loads(raw[20:20 + size])
    root = next(i for i, node in enumerate(doc['nodes']) if node.get('name') == 'RF_ROOT')
    for skin in doc['skins']:
        skin['skeleton'] = root
    for mat in doc.get('materials', []):
        mat.pop('extensions', None)
    doc.pop('extensionsUsed', None)
    doc.pop('extensionsRequired', None)
    primitives = sum(len(m['primitives']) for m in doc['meshes'])
    if primitives > 32:
        raise ValueError('Export exceeds 32 primitives; author material batching explicitly')
    encoded = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    encoded += b' ' * (-len(encoded) % 4)
    tail = raw[20 + size:]
    output.write_bytes(b'glTF' + struct.pack('<II', 2, 20 + len(encoded) + len(tail)) +
                       struct.pack('<II', len(encoded), kind) + encoded + tail)
    return {'primitives': primitives, 'morphs': False, 'skins': len(doc['skins'])}


def render(blend, output, views, structure=False, expression=None):
    bpy.ops.wm.open_mainfile(filepath=str(Path(blend).resolve()))
    scene = bpy.context.scene
    if structure:
        for obj in scene.objects:
            if obj.get('part_id', '').startswith('hair_'):
                obj.hide_render = True
    if expression:
        for obj in scene.objects:
            if obj.type == 'MESH' and obj.data.shape_keys:
                key = obj.data.shape_keys.key_blocks.get(expression)
                if key:
                    key.value = 1
    Path(output).mkdir(parents=True, exist_ok=True)
    for view in views:
        scene.camera = bpy.data.objects['Compare / ' + view]
        scene.render.filepath = str(Path(output).resolve() / (view + '.png'))
        bpy.ops.render.render(write_still=True)
