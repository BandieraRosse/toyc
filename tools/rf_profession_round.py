#!/usr/bin/env python3
"""Reproduce V2.1 body, Headgear V1 and six Profession V1 visual carriers.

Generation uses Blender and the existing manifest importer/contract gate.
Capture uses the real Rasterfall CLI, never a second character renderer.
"""
import argparse
import hashlib
import json
import math
import subprocess
import sys
from pathlib import Path

sys.dont_write_bytecode = True
from character_lab_sheet import MODES, VIEWS, assemble, read_bmp, write_png
from character_world_sheet import DISTANCES, POSES, assemble as world_sheet
from rf_humanoid_headgear_sheet import assemble_grid

PROFESSIONS = ('rifleman', 'breacher', 'recon', 'medic', 'engineer', 'heavy')
HEADS = ('headset', 'patrol-cap', 'goggles', 'respirator',
         'tactical-helmet', 'engineering-helmet')
PROFESSION_GEAR_COLORS = {
    'rifleman': ((.12, .16, .08), (.34, .36, .20)),
    'breacher': ((.07, .09, .12), (.24, .30, .36)),
    'recon': ((.12, .16, .10), (.35, .38, .24)),
    'medic': ((.65, .72, .64), (.72, .14, .055)),
    'engineer': ((.18, .16, .105), (.68, .43, .075)),
    'heavy': ((.13, .115, .08), (.43, .32, .13)),
}


def srgb(value):
    return max(0, min(255, int(math.sqrt(max(0, min(1, value)) * 65025))))


def check_profession_materials(glb, profession):
    raw = glb.read_bytes()
    json_length = int.from_bytes(raw[12:16], 'little')
    document = json.loads(raw[20:20 + json_length].rstrip(b' \0'))
    actual = {
        material['name']: tuple(material['pbrMetallicRoughness']['baseColorFactor'][:3])
        for material in document['materials']
    }
    expected = dict(zip(('RF_Headgear', 'RF_HeadgearLight'),
                        PROFESSION_GEAR_COLORS[profession]))
    checked = 0
    for name, color in expected.items():
        if name not in actual:
            continue
        checked += 1
        if any(abs(a - b) > 1e-6 for a, b in zip(actual[name], color)):
            raise SystemExit(f'{profession}: exported {name} color {actual.get(name)} != {color}')
    if not checked:
        raise SystemExit(f'{profession}: export has no profession gear material')
    return {sum(srgb(channel) << shift
                for channel, shift in zip(actual[name], (16, 8, 0)))
            for name in expected if name in actual}


def check_rmesh_materials(mesh, profession, expected=None):
    raw = mesh.read_bytes()
    version = int.from_bytes(raw[4:8], 'little')
    primitive_count = int.from_bytes(raw[44:48], 'little')
    material_count = int.from_bytes(raw[48:52], 'little')
    material_bytes = 40 if version >= 9 else 24 if version >= 8 else 16
    offset = 64 + primitive_count * 16
    colors = {int.from_bytes(raw[offset + i * material_bytes:
                                 offset + i * material_bytes + 4], 'little')
              for i in range(material_count)}
    if expected is None:
        expected = {sum(srgb(channel) << shift
                        for channel, shift in zip(color, (16, 8, 0)))
                    for color in PROFESSION_GEAR_COLORS[profession]}
    def close_color(want, got):
        return all(abs(((want >> shift) & 255) - ((got >> shift) & 255)) <= 2
                   for shift in (16, 8, 0))
    if any(not any(close_color(want, got) for got in colors)
           for want in expected):
        raise SystemExit(f'{profession}: RFM2 gear colors {sorted(colors)} missing {sorted(expected)}')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--world', action='store_true')
    parser.add_argument('--deterministic', action='store_true')
    parser.add_argument('--lineup', action='store_true', help='capture only the seven lineup views')
    parser.add_argument('--output', type=Path, default=Path('tmp/rf-v21-professions'))
    args = parser.parse_args()
    root = args.output.resolve()
    root.mkdir(parents=True, exist_ok=True)
    models = Path('rasterfall/private-assets/models')
    source = Path('rasterfall/private-assets/source/characters')
    manifests = Path('tools/assets/manifests/characters')
    entries = [('rf_humanoid_v2', [])]
    entries += [('rf_humanoid_v2_' + h.replace('-', '_'), ['--headgear', h]) for h in HEADS]
    entries += [('rf_profession_' + p, ['--profession', p]) for p in PROFESSIONS]
    rigid_entries = []
    for profession in PROFESSIONS:
        slots = ['head', 'chest', 'back']
        if profession == 'engineer': slots += ['hip-l']
        if profession == 'heavy': slots += ['hip-l', 'hip-r']
        rigid_entries += [('rf_gear_' + profession + '_' + slot.replace('-', '_'),
                           [f'--rigid-attachment={profession}-{slot}'])
                          for slot in slots]

    def run(command, label):
        log = root / (label + '.log')
        with log.open('w') as stream:
            result = subprocess.run([str(c) for c in command], stdout=stream, stderr=subprocess.STDOUT)
        if result.returncode:
            raise SystemExit(f'FAILED ({result.returncode}): {label}; see {log}')
        print('PASS ' + label, flush=True)

    if args.generate:
        source.mkdir(parents=True, exist_ok=True)
        for asset, flags in entries:
            manifest = manifests / (asset + '.asset.json')
            glb = (manifest.parent / json.loads(manifest.read_text())['source']).resolve()
            expected_colors = None
            run(['blender', '--background', '--factory-startup', '--python-exit-code', '1', '--python',
                 'tools/blender/generate_rasterfall_humanoid_v2.py', '--', '--output', glb, *flags], asset + '-generate')
            if asset.startswith('rf_profession_'):
                expected_colors = check_profession_materials(
                    glb, asset.removeprefix('rf_profession_'))
            run(['build/glb-inspect', glb, 'contract'], asset + '-contract')
            run(['python3', 'tools/assets/import_asset.py', '--no-build', '--force', manifest], asset + '-import')
            run(['build/rfchar_runtime_test', models / (asset + '.rmesh')], asset + '-runtime')
            if asset.startswith('rf_profession_'):
                check_rmesh_materials(models / (asset + '.rmesh'),
                                      asset.removeprefix('rf_profession_'),
                                      expected_colors)
        attachment_source = Path('rasterfall/private-assets/source/attachments')
        attachment_source.mkdir(parents=True, exist_ok=True)
        for asset, flags in rigid_entries:
            glb = (attachment_source / (asset + '.glb')).resolve()
            run(['blender', '--background', '--factory-startup', '--python-exit-code', '1', '--python',
                 'tools/blender/generate_rasterfall_humanoid_v2.py', '--', '--output', glb, *flags],
                asset + '-generate')
            profession = asset.removeprefix('rf_gear_').split('_', 1)[0]
            expected_colors = check_profession_materials(glb, profession)
            manifest = root / (asset + '.asset.json')
            manifest.write_text(json.dumps({
                'schema': 1, 'id': asset, 'type': 'rigid_attachment',
                'source': str(glb), 'attachment_space': {
                    'origin': 'mount_origin', 'orientation': 'canonical_character',
                    'units': 'meters'}, 'lods': []}, indent=2) + '\n')
            run(['python3', 'tools/assets/import_asset.py', '--no-build', '--force', manifest],
                asset + '-import')
            check_rmesh_materials(models / (asset + '.rmesh'), profession,
                                  expected_colors)

    if args.capture:
        for asset, _ in entries:
            capture = root / 'lab' / asset
            run(['build/rasterfall', '--character-acceptance', models / (asset + '.rmesh'), capture], asset + '-lab')
            write_png(root / (asset + '-lab.png'), assemble(
                [capture / m / (v + '.bmp') for m in MODES for v in VIEWS], 400))
    if args.capture or args.lineup:
        run(['build/rasterfall', '--profession-lineup', models, root / 'lineup'], 'lineup')
        for view in ('front', 'three-quarter'):
            write_png(root / ('lineup-' + view + '.png'), assemble_grid(
                [root / 'lineup' / f'{view}-{d}.bmp' for d in DISTANCES], 1, 1600))
        write_png(root / 'lineup-side.png', read_bmp(root / 'lineup/side-mid.bmp'))

    if args.world:
        for asset, _ in [entries[0], *entries[-6:]]:
            capture = root / 'world' / asset
            capture.mkdir(parents=True, exist_ok=True)
            run(['build/rasterfall', '--character-world-capture', capture,
                 '--character-world-model', models / (asset + '.rmesh')], asset + '-world')
            write_png(root / (asset + '-world.png'), world_sheet(
                [capture / f'{d}-{p}.bmp' for d in DISTANCES for p in POSES], 400))

    if args.deterministic:
        run(['build/rasterfall', '--profession-lineup', models, root / 'lineup-repeat'], 'lineup-repeat')
        hashes = {}
        lineup_names = {f'{view}-{distance}.bmp'
                        for view in ('front', 'three-quarter')
                        for distance in DISTANCES} | {'side-mid.bmp'}
        for path in sorted((root / 'lineup').glob('*.bmp')):
            if path.name not in lineup_names:
                continue
            first = path.read_bytes()
            if first != (root / 'lineup-repeat' / path.name).read_bytes():
                raise SystemExit('non-deterministic capture: ' + path.name)
            hashes[path.name] = hashlib.sha256(first).hexdigest()
        if len(hashes) != 7:
            raise SystemExit('expected seven lineup captures')
        (root / 'lineup-sha256.json').write_text(json.dumps(hashes, indent=2) + '\n')
        print('PASS seven deterministic lineup captures', flush=True)


if __name__ == '__main__':
    main()
