"""Create immutable part revisions and a candidate assembly, never edit a baseline."""
import copy
import importlib.util
import re
from pathlib import Path

import bpy

from .records import rig_record, scene_record
from .storage import (load_objects, manifest_check, part_record, read, resolve,
                      save_part, sha, write)


def revise(manifest_path, part_id, revision, operation, source=None, parameters=None):
    root, manifest = manifest_check(manifest_path)
    if part_id not in manifest['parts'] or not re.fullmatch(r'[a-zA-Z0-9_-]+', revision):
        raise ValueError('Unknown part or invalid revision')
    entry = manifest['parts'][part_id]
    output = root / 'parts' / part_id / (revision + '.blend')
    candidate = root / ('assembly-' + part_id + '-' + revision + '.json')
    if output.exists() or candidate.exists():
        raise FileExistsError('Revision already exists')
    dependencies = {}
    if operation == 'publish':
        source = Path(source).resolve()
        bpy.ops.wm.open_mainfile(filepath=str(source))
        all_parts = [o for o in bpy.data.objects if o.type == 'MESH' and o.get('part_id')]
        objects = [o for o in all_parts if o['part_id'] == part_id]
        # A whole-assembly edit must not smuggle changes to unrelated components.
        other_ids = {o['part_id'] for o in all_parts} - {part_id}
        for other in other_ids:
            if other not in manifest['parts'] or part_record([o for o in all_parts if o['part_id'] == other]) != manifest['parts'][other]['records']:
                raise ValueError('Unselected part was edited: ' + other)
        if bpy.context.scene.get('assembly_manifest'):
            arms = [o for o in bpy.context.scene.objects if o.type == 'ARMATURE']
            if len(arms) != 1 or rig_record(arms[0]) != manifest['rig']['record']:
                raise ValueError('Rig/actions were edited')
            if scene_record(bpy.context.scene) != manifest['review']['record']:
                raise ValueError('Review scene was edited')
        dependencies['edited_source_sha256'] = sha(source)
    else:
        bpy.ops.wm.read_factory_settings(use_empty=True)
        objects = load_objects(resolve(root, entry['file']), entry['objects'])
        if operation == 'expressions':
            if part_id not in {'head', 'eyes', 'mouth'}:
                raise ValueError('Expressions can only edit head, eyes or mouth')
            generator_path = resolve(root, manifest['expressions']['generator'])
            references = []
            if part_id == 'head':
                eye_entry = manifest['parts']['eyes']
                references = load_objects(resolve(root, eye_entry['file']), eye_entry['objects'])
                dependencies['eyes_sha256'] = eye_entry['sha256']
            params = {}
        else:
            if entry['mode'] != 'procedural':
                raise ValueError('This part is edited directly; use publish')
            generator_path = resolve(root, entry['generator'])
            parameter_path = Path(parameters).resolve() if parameters else resolve(root, entry['parameters'])
            params = read(parameter_path)
            dependencies['parameters'] = params
            dependencies['parameters_sha256'] = sha(parameter_path)
            references = []
        spec = importlib.util.spec_from_file_location('character_part_generator', generator_path)
        module = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(module)
        dependencies['generator_sha256'] = sha(generator_path)
        reference_before = part_record(references)
        module.regenerate(part_id, objects, read(resolve(root, manifest['interfaces']['file'])), params, references)
        if part_record(references) != reference_before:
            raise ValueError('Generator modified a read-only dependency')
    if len(objects) != len(entry['records']) or {o.get('object_id') for o in objects} != set(entry['records']):
        raise ValueError('Revision must retain object IDs; inventory migrations are explicit')
    for obj in objects:
        if obj.get('part_id') != part_id:
            raise ValueError('Generator changed ownership')
        obj.parent = None
        for mod in list(obj.modifiers):
            if mod.type == 'ARMATURE':
                obj.modifiers.remove(mod)
        obj['part_revision'] = revision
        # Baking unrelated modifiers or remapping UVs is never implicit.
    records = part_record(objects)
    save_part(output, objects)
    # Reopen the written library before publishing its lockfile.
    names = [o.name for o in objects]
    bpy.ops.wm.read_factory_settings(use_empty=True)
    reopened = load_objects(output, names)
    if part_record(reopened) != records:
        raise ValueError('Saved revision differs from memory')
    result = copy.deepcopy(manifest)
    result['parts'][part_id].update(file=str(output.relative_to(root)).replace('\\', '/'),
        sha256=sha(output), revision=revision, objects=names, records=records,
        provenance={'operation': operation, 'previous_sha256': entry['sha256'], **dependencies})
    if operation == 'rebuild':
        # The next rebuild must inherit this revision's recipe, not revert to the
        # original parameter file when --parameters is omitted.
        parameter_copy = output.with_suffix('.parameters.json')
        write(parameter_copy, params)
        result['parts'][part_id]['parameters'] = str(parameter_copy.relative_to(root)).replace('\\', '/')
        result['parts'][part_id]['parameters_sha256'] = sha(parameter_copy)
    write(candidate, result)
    return str(candidate)
