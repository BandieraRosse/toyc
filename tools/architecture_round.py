#!/usr/bin/env python3
"""Reproduce Architectural Environment V1 with existing Builder/importer/CLI.

No private input is needed to regenerate these original procedural assets.
Images are actual Rasterfall BMPs; Pillow only assembles review sheets.
"""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import subprocess

from PIL import Image, ImageDraw

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('beam', 'support', 'wall', 'doorway', 'pipe_straight', 'pipe_elbow',
         'pipe_tee', 'service_panel', 'cable_tray', 'floor_hatch')
SCENES = ('arch-family', 'arch-alley', 'arch-alley-inside', 'arch-alley-far',
          'arch-alley-reverse', 'arch-hall', 'arch-hall-inside',
          'arch-hall-far', 'arch-hall-reverse')


def run(command, log):
    with log.open('w') as stream:
        subprocess.run(list(map(str, command)), cwd=ROOT, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def sheet(paths, target, columns=3, width=480):
    cell_h = width * 5 // 8 + 26
    output = Image.new('RGB', (columns*width, ((len(paths)+columns-1)//columns)*cell_h), '#22292e')
    draw = ImageDraw.Draw(output)
    for i, path in enumerate(paths):
        with Image.open(path) as source:
            source.convert('RGB').save(path.with_suffix('.png'))
            thumb = source.convert('RGB')
            thumb.thumbnail((width, cell_h-26))
            x, y = i%columns*width, i//columns*cell_h
            output.paste(thumb, (x+(width-thumb.width)//2, y+26))
            draw.text((x+8, y+7), path.parent.name if path.stem=='sheet' else path.stem, fill='#e2e3da')
    output.save(target)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, default=ROOT/'tmp/architecture-v1')
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--deterministic', action='store_true')
    args = parser.parse_args()
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=True)
    ids = ['rf_arch_'+name for name in NAMES]
    if args.generate:
        run(['make', '-j4', 'build/toyasset', 'app-glb2rmesh'], out/'converters.log')
        source = out/'source'
        run(['blender', '-b', '--python-exit-code', '1', '--python',
             'tools/blender/generate_rasterfall_props.py', '--', '--output', source,
             '--overwrite', '--assets', *ids], out/'generation.log')
        destination = ROOT/'rasterfall/private-assets/source/props/industrial'
        destination.mkdir(parents=True, exist_ok=True)
        for asset in ids:
            shutil.copy2(source/(asset+'.glb'), destination/(asset+'.glb'))
            run(['python3', 'tools/assets/import_asset.py', '--force', '--no-build', '--output-root',
                 'rasterfall/assets/models/props/industrial',
                 'tools/assets/manifests/props/industrial/'+asset+'.asset.json'], out/(asset+'-import.log'))
        shutil.copy2(source/'rasterfall_props.blend', destination/'architectural_v1.blend')
        if args.deterministic:
            repeat = out/'source-repeat'
            run(['blender', '-b', '--python-exit-code', '1', '--python',
                 'tools/blender/generate_rasterfall_props.py', '--', '--output', repeat,
                 '--overwrite', '--assets', *ids], out/'generation-repeat.log')
            for asset in ids:
                assert (source/(asset+'.glb')).read_bytes() == (repeat/(asset+'.glb')).read_bytes(), asset
    if args.capture:
        capture = out/'captures'
        capture.mkdir(exist_ok=True)
        for scene in SCENES:
            command = [ROOT/'build/rasterfall', '--visual-capture', scene, '--visual-output']
            path = capture/(scene+'.bmp')
            run([*command, path], out/(scene+'.log'))
            if args.deterministic:
                repeat = capture/(scene+'-repeat.bmp')
                run([*command, repeat], out/(scene+'-repeat.log'))
                assert path.read_bytes() == repeat.read_bytes(), scene
        sheet([capture/(s+'.bmp') for s in SCENES], out/'prototypes.png')
        individual = []
        details = []
        for asset in ids:
            directory = out/'individual'/asset
            directory.mkdir(parents=True, exist_ok=True)
            run([ROOT/'build/rasterfall', '--model-static-views',
                 'rasterfall/assets/models/props/industrial/'+asset+'.rmesh', directory],
                out/(asset+'-views.log'))
            paths = sorted(directory.glob('*.bmp'))
            if not paths:
                raise RuntimeError('Missing individual captures: '+asset)
            sheet(paths, directory/'sheet.png', columns=2)
            individual.append(directory/'sheet.png')
            detail = capture/(asset+'.bmp')
            run([ROOT/'build/rasterfall', '--visual-capture',
                 'arch-asset-'+asset.removeprefix('rf_arch_'), '--visual-output', detail],
                out/(asset+'-detail.log'))
            details.append(detail)
        sheet(individual, out/'individual.png', columns=2, width=800)
        sheet(details, out/'details.png', columns=2, width=640)
    fingerprints = {}
    for asset in ids:
        path = ROOT/'rasterfall/assets/models/props/industrial'/(asset+'.rmesh')
        if path.exists():
            fingerprints[asset] = {'bytes': path.stat().st_size,
                                   'sha256': hashlib.sha256(path.read_bytes()).hexdigest()}
    (out/'runtime-fingerprints.json').write_text(json.dumps(fingerprints, indent=2)+'\n')
    print(out)


if __name__ == '__main__':
    main()
