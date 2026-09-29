import hashlib
import json
from pathlib import Path

import bpy

from .records import digest, material_record, mesh_record


def read(path):
    return json.loads(Path(path).read_text(encoding='utf-8'))


def write(path, value):
    path = Path(path)
    path.parent.mkdir(parents=True, exist_ok=True)
    temp = path.with_suffix(path.suffix + '.tmp')
    temp.write_text(json.dumps(value, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    temp.replace(path)


def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def resolve(root, relative):
    path = (Path(root) / relative).resolve()
    if not path.is_relative_to(Path(root).resolve()):
        raise ValueError('Asset path must remain inside authoring root: ' + relative)
    return path


def load_objects(path, names):
    with bpy.data.libraries.load(str(path), link=False) as (src, dst):
        if not set(names).issubset(src.objects):
            raise ValueError('Missing objects in ' + str(path))
        dst.objects = list(names)
    for obj in dst.objects:
        bpy.context.scene.collection.objects.link(obj)
    return dst.objects


def save_part(path, objects):
    """Standalone part library: no implicit rig, camera or other part dependency."""
    path = Path(path)
    if path.exists():
        raise FileExistsError('Create a new revision instead of replacing ' + str(path))
    path.parent.mkdir(parents=True, exist_ok=True)
    for obj in objects:
        if obj.parent or any(m.type == 'ARMATURE' for m in obj.modifiers):
            raise ValueError('Part source must not carry a rig')
        # Material ownership is per part; Blender users may edit these freely.
        for mat in obj.data.materials:
            if mat and not mat.get('export_id'):
                raise ValueError('Material needs explicit export_id: ' + mat.name)
    scene = bpy.data.scenes.new('Part / ' + objects[0]['part_id'])
    scene.unit_settings.system = 'METRIC'
    scene.unit_settings.scale_length = 1
    previous_scene = bpy.context.window.scene
    try:
        for obj in objects:
            scene.collection.objects.link(obj)
        # Blender 5.2 partial scene writing needs a synchronized view layer.
        bpy.context.window.scene = scene
        bpy.context.view_layer.update()
        bpy.data.libraries.write(str(path), {scene}, fake_user=True)
    finally:
        bpy.context.window.scene = previous_scene
        bpy.data.scenes.remove(scene)


def part_record(objects):
    return {o['object_id']: mesh_record(o) for o in objects}


def material_key(mat):
    return mat.get('export_id'), digest(material_record(mat))


def manifest_check(path):
    manifest = read(path)
    root = Path(path).resolve().parent
    if manifest['schema'] != 1:
        raise ValueError('Unsupported assembly schema')
    for entry in [manifest['rig'], manifest['review'], manifest['interfaces'],
                  *manifest['parts'].values()]:
        source = resolve(root, entry['file'])
        if sha(source) != entry['sha256']:
            raise ValueError('Changed locked source: ' + str(source))
    for entry in manifest['parts'].values():
        if 'parameters_sha256' in entry:
            if sha(resolve(root, entry['parameters'])) != entry['parameters_sha256']:
                raise ValueError('Changed locked parameters: ' + entry['parameters'])
    return root, manifest
