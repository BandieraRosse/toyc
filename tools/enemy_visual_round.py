#!/usr/bin/env python3
"""Reproduce Enemy Visual V2 assets, validate contracts and capture the renderer.

No image synthesis: BMPs come from Rasterfall; PNG sheets only arrange pixels.
Generated GLB sources are local private-assets, public RFM2s ship with the game.
"""
import argparse
import json
import struct
import subprocess
from pathlib import Path
from character_lab_sheet import read_bmp, resize_rgb, write_png

ROOT = Path(__file__).resolve().parents[1]
FAMILIES = ('block', 'humanoid')
KINDS = ('common', 'fast', 'heavy')


def run(args, log):
    with log.open('w') as stream:
        subprocess.run([str(a) for a in args], cwd=ROOT, stdout=stream,
                       stderr=subprocess.STDOUT, check=True)


def sheet(paths, columns, output, width=400):
    cells = [read_bmp(p) for p in paths]
    height = cells[0][1] * width // cells[0][0]
    cells = [resize_rgb(im, width, height) for im in cells]
    rows = (len(cells) + columns-1)//columns
    full_width = columns*width
    data = bytearray(full_width*rows*height*3)
    for i, (_, _, pixels) in enumerate(cells):
        row, col = divmod(i, columns)
        for y in range(height):
            dst = ((row*height+y)*full_width+col*width)*3
            data[dst:dst+width*3] = pixels[y*width*3:(y+1)*width*3]
    write_png(output, (full_width, rows*height, data))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generate', action='store_true')
    parser.add_argument('--capture', action='store_true')
    parser.add_argument('--deterministic', action='store_true')
    parser.add_argument('--output', type=Path, default=Path('tmp/enemy-visual-v2'))
    args = parser.parse_args()
    output = (ROOT / args.output).resolve()
    output.mkdir(parents=True, exist_ok=True)
    if args.generate:
        run(['make','-j8','app-glb-inspect','build/toyasset'], output/'tools-build.log')
    report = []
    for family in FAMILIES:
        for kind in KINDS:
            name = f'rf_infected_{family}_{kind}'
            source = ROOT / f'rasterfall/private-assets/source/enemies/{name}.glb'
            manifest = ROOT / f'tools/assets/manifests/enemies/{name}.asset.json'
            model = ROOT / f'rasterfall/assets/models/enemies/{name}.rmesh'
            if args.generate:
                run(['blender','--background','--factory-startup','--python-exit-code','1','--python',
                     'tools/blender/generate_rasterfall_infected.py','--',
                     '--family',family,'--type',kind,'--output',source],output/(name+'-generate.log'))
                if not source.exists():
                    raise RuntimeError(f'Blender did not export {source}; inspect generation log')
                run(['python3','tools/assets/import_asset.py','--no-build','--force',
                     '--output-root','rasterfall/assets/models/enemies',manifest],output/(name+'-import.log'))
            if source.exists():
                run(['build/glb-inspect',source,'contract'],output/(name+'-contract.log'))
                run(['python3','tools/assets/import_asset.py','--validate-only','--output-root','rasterfall/assets/models/enemies',manifest],output/(name+'-manifest.log'))
            run(['build/rfchar_runtime_test',model],output/(name+'-runtime.log'))
            data = model.read_bytes()
            version, vertices, indices, units = struct.unpack_from('<4I',data,4)
            primitives, materials = struct.unpack_from('<2I',data,44)
            assert data[:4] == b'RFM2' and version == 14 and units == 512
            budget = 600 if family == 'block' else 1800
            assert indices % 3 == 0 and indices//3 <= budget and materials <= 9
            report.append(dict(asset=name,triangles=indices//3,vertices=vertices,
                               materials=materials,primitives=primitives,bytes=len(data)))
        if args.capture:
            old = output/family/'world-100m.bmp'
            if old.exists(): old.unlink()
            run(['build/rasterfall','--enemy-visual-family',family+'-infected',
                 '--enemy-visual-capture',output/family],output/(family+'-capture.log'))
        if args.deterministic:
            other = output/(family+'-repeat')
            run(['build/rasterfall','--enemy-visual-family',family+'-infected',
                 '--enemy-visual-capture',other],output/(family+'-repeat.log'))
            original = sorted((output/family).glob('*.bmp'))
            assert len(original) == 33
            for path in original:
                if path.read_bytes() != (other/path.name).read_bytes():
                    raise RuntimeError(f'Non-deterministic capture: {path}')
        if args.capture or args.deterministic:
            for kind in KINDS:
                sheet([output/family/f'{kind}-{pose}-{view}.bmp'
                       for pose in ('bind','idle','move')
                       for view in ('front','side','three-quarter')],3,
                      output/f'{family}-{kind}.png')
            sheet([output/family/(f'world-{distance}m.bmp' if distance < 100 else 'distance-100m.bmp') for distance in (10,30,100)],1,
                  output/f'{family}-world.png',1600)
            sheet([output/family/f'death-{i}.bmp' for i in range(3)],1,
                  output/f'{family}-death.png',1200)
    if args.capture or args.deterministic:
        sheet([output/family/f'{kind}-idle-three-quarter.bmp'
               for family in FAMILIES for kind in KINDS],3,output/'six-infected.png',600)
    (output/'asset-report.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))


if __name__ == '__main__':
    main()
