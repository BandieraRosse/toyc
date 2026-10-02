#!/usr/bin/env python3
"""Rebuild public shared humanoid/gear and capture the actual modular renderer.

GLB sources remain reproducible local artifacts. Only original project geometry
is generated; gunner recipes share the same RFCHAR body as the friendly roster.
Run NativeCodex.ps1 asset-tools once before --generate on Windows.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode = True
from rf_profession_round import check_profession_materials, check_rmesh_materials
from rf_profession_round import PROFESSION_GEAR_COLORS
from rf_humanoid_headgear_sheet import assemble_grid
from character_lab_sheet import write_png

ROOT = Path(__file__).resolve().parents[1]
PROFILES = ('rifleman', 'breacher', 'recon', 'medic', 'engineer', 'heavy',
            'gunner', 'gunner-elite')
PROFESSION_GEAR_COLORS.update({
    'gunner': ((.12, .065, .055), (.64, .070, .032)),
    'gunner-elite': ((.075, .087, .10), (.56, .038, .022)),
})


def entries():
    yield 'rf_humanoid_v2', [], None
    for profile in PROFILES:
        slots = ['head', 'chest', 'back']
        if profile == 'engineer':
            slots += ['hip-l']
        if profile == 'heavy':
            slots += ['hip-l', 'hip-r']
        for slot in slots:
            asset = f'rf_gear_{profile}_{slot}'.replace('-', '_')
            yield asset, [f'--rigid-attachment={profile}-{slot}'], profile


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--gunners-only', action='store_true',
                        help='rebuild only six gunner attachments while iterating')
    parser.add_argument('--blender', default='blender')
    parser.add_argument('--tool-dir', type=Path,
                        default=Path('build-windows' if os.name == 'nt' else 'build'))
    parser.add_argument('--runtime', type=Path)
    parser.add_argument('--runtime-root', type=Path,
                        default=Path('build-windows/rasterfall-windows' if os.name == 'nt' else '.'),
                        help='working directory containing rasterfall/assets')
    parser.add_argument('--output', type=Path, default=Path('tmp/combat-v0/assets'))
    args = parser.parse_args()
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    tools = (ROOT / args.tool_dir).resolve()
    suffix = '.exe' if os.name == 'nt' else ''
    models = ROOT / 'rasterfall/assets/models/characters'
    runtime_test = tools / ('rfchar-runtime-test.exe' if os.name == 'nt'
                            else 'rfchar_runtime_test')

    def run(command, label, cwd=ROOT):
        with (output / f'{label}.log').open('w', encoding='utf-8', newline='\n') as log:
            subprocess.run([str(c) for c in command], cwd=cwd, stdout=log,
                           stderr=subprocess.STDOUT, check=True)
        print('PASS ' + label, flush=True)

    report = []
    for asset, flags, profile in entries():
        if args.gunners_only and not asset.startswith('rf_gear_gunner_'):
            continue
        manifest = ROOT / f'tools/assets/manifests/characters/{asset}.asset.json'
        value = json.loads(manifest.read_text(encoding='utf-8'))
        source = (manifest.parent / value['source']).resolve()
        expected = None
        if args.generate:
            source.parent.mkdir(parents=True, exist_ok=True)
            run([args.blender, '--background', '--factory-startup', '--python-exit-code', '1',
                 '--python', ROOT / 'tools/blender/generate_rasterfall_humanoid_v2.py',
                 '--', '--output', source, *flags], asset + '-generate')
            if profile:
                expected = check_profession_materials(source, profile)
            run([sys.executable, ROOT / 'tools/assets/import_asset.py', '--no-build',
                 '--tool-dir', tools, '--output-root', models, '--force', manifest], asset + '-import')
        mesh = models / (asset + '.rmesh')
        raw = mesh.read_bytes()
        if profile and args.generate:
            check_rmesh_materials(mesh, profile, expected)
        elif not profile:
            run([runtime_test, mesh], asset + '-runtime')
        version, vertices, indices, units = struct.unpack_from('<4I', raw, 4)
        primitives, materials = struct.unpack_from('<2I', raw, 44)
        report.append(dict(asset=asset, version=version, vertices=vertices,
                           triangles=indices//3, units=units, primitives=primitives,
                           materials=materials, bytes=len(raw), sha256=hashlib.sha256(raw).hexdigest()))
    (output / 'asset-report.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    if args.capture:
        runtime = (ROOT / (args.runtime or
                   Path('build-windows/rasterfall-windows/rasterfall.exe' if os.name == 'nt'
                        else 'build/rasterfall'))).resolve()
        capture = output / 'capture'
        run([runtime, '--combat-character-capture', capture], 'combat-character-capture',
            (ROOT / args.runtime_root).resolve())
        for view in ('front', 'side', 'back', 'three-quarter', 'rts'):
            write_png(output / f'combat-{view}.png', assemble_grid(
                [capture / f'{view}-{distance}.bmp' for distance in ('near', 'mid', 'far')], 1, 1600))
        write_png(output / 'combat-motion.png', assemble_grid(
            [capture / f'move-{frame}.bmp' for frame in range(8)], 4, 800))
    print(f'PASS {len(report)} public resources; report: {output / "asset-report.json"}', flush=True)


if __name__ == '__main__':
    main()
