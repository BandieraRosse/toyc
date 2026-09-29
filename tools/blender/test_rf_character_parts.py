"""Blender integration checks using a supplied private assembly, in an isolated copy."""
import argparse
import shutil
import sys
from pathlib import Path

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from rf_parts import pipeline
from rf_parts.revisions import revise
from rf_parts.storage import manifest_check, material_key, read, sha, write


def rejected(callback, fragment):
    try:
        callback()
    except ValueError as error:
        assert fragment in str(error), str(error)
        return str(error)
    raise AssertionError('Expected rejection: ' + fragment)


def run(manifest_path, output):
    root, manifest = manifest_check(manifest_path)
    output = Path(output).resolve()
    output.mkdir(parents=True, exist_ok=False)
    paths = {e['file'] for e in [manifest['rig'], manifest['review'], manifest['interfaces'], *manifest['parts'].values()]}
    paths.add(manifest['expressions']['generator'])
    for entry in manifest['parts'].values():
        paths.update(entry[k] for k in ('generator', 'parameters') if k in entry)
    for relative in paths:
        target = output / relative
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copy2(root / relative, target)
    baseline = output / 'assembly.json'
    write(baseline, manifest)
    source_hashes = {p: sha(output / p) for p in paths}
    pipeline.assemble(baseline, output / 'baseline.blend')
    report = {'baseline': pipeline.verify(output / 'baseline.blend', baseline)}
    # A zero-edit generator migration must reproduce the original mesh semantics.
    for part in ('hair_base', 'hair_back'):
        candidate = revise(baseline, part, 'no-change', 'rebuild')
        current = read(candidate)
        assert current['parts'][part]['records'] == manifest['parts'][part]['records'], part
    report['hair_noop_exact'] = True
    params = read(output / manifest['parts']['hair_back']['parameters'])
    lock_id = sorted(params['locks'])[0]
    params['locks'][lock_id]['width'] += .035
    write(output / 'modified-hair.json', params)
    changed = revise(baseline, 'hair_back', 'changed', 'rebuild', parameters=output / 'modified-hair.json')
    current = read(changed)
    changed_objects = [key for key, value in current['parts']['hair_back']['records'].items()
                       if value != manifest['parts']['hair_back']['records'][key]]
    assert changed_objects == [lock_id], changed_objects
    repeated = revise(changed, 'hair_back', 'changed-repeat', 'rebuild')
    assert read(repeated)['parts']['hair_back']['records'] == current['parts']['hair_back']['records']
    report['revision_recipe_inherited'] = True
    pipeline.assemble(changed, output / 'hair-change.blend')
    report['hair_revision'] = pipeline.verify(output / 'hair-change.blend', changed, baseline, ['hair_back'])
    report['single_lock_changed'] = changed_objects
    report['undeclared_change_rejected'] = rejected(lambda: pipeline.verify(output / 'hair-change.blend', changed, baseline), 'outside selected')
    # Publish an edited face without regenerating other parts or silently accepting
    # a changed studio/body in a full assembly supplied by an artist.
    bpy.ops.wm.open_mainfile(filepath=str(output / 'baseline.blend'))
    head = next(o for o in bpy.context.scene.objects if o.get('expression_role') == 'head')
    head.data.vertices[100].co.y += .0002
    for key in head.data.shape_keys.key_blocks:
        key.data[100].co.y += .0002
    bpy.ops.wm.save_as_mainfile(filepath=str(output / 'edited-head.blend'))
    head_candidate = revise(baseline, 'head', 'edited', 'publish', source=output / 'edited-head.blend')
    pipeline.assemble(head_candidate, output / 'head-change.blend')
    report['head_revision'] = pipeline.verify(output / 'head-change.blend', head_candidate, baseline, ['head'])
    bpy.ops.wm.open_mainfile(filepath=str(output / 'edited-head.blend'))
    body = next(o for o in bpy.context.scene.objects if o.get('part_id') == 'body')
    body.data.uv_layers.active.data[0].uv.x += .01
    bpy.ops.wm.save_as_mainfile(filepath=str(output / 'accidental-body-edit.blend'))
    report['unrelated_uv_edit_rejected'] = rejected(lambda: revise(baseline, 'head', 'bad', 'publish', source=output / 'accidental-body-edit.blend'), 'Unselected part')
    for part in ('head', 'eyes', 'mouth'):
        candidate = revise(baseline, part, 'expressions', 'expressions')
        assert read(candidate)['parts'][part]['records'] == manifest['parts'][part]['records'], part + ' expressions differ'
    report['expression_noop_exact'] = True
    mat = bpy.data.materials.new('Fingerprint test')
    mat.use_nodes = True
    mat['export_id'] = 'same-identity'
    other = mat.copy()
    other.node_tree.nodes.get('Principled BSDF').inputs['Roughness'].default_value = .123
    assert tuple(mat.diffuse_color) == tuple(other.diffuse_color)
    assert material_key(mat) != material_key(other)
    report['same_color_different_shader_not_merged'] = True
    assert all(sha(output / p) == value for p, value in source_hashes.items())
    report['source_files_unchanged'] = True
    write(output / 'report.json', report)
    print('PART ISOLATION TEST PASS', flush=True)


if __name__ == '__main__':
    parser = argparse.ArgumentParser()
    parser.add_argument('--manifest', required=True)
    parser.add_argument('--output', required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--') + 1:])
    run(args.manifest, args.output)
