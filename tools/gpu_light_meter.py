"""Capture exposure-independent GPU illuminance (direct / GI / artistic fill).

Readings apply to the visible receiving surface, not to the screen's brightness.
The HDR file stores little-endian RGBA16F after a four-uint32 header; one stored
unit is 100 lux in meter mode. UI, sky and viewmodels are not measurement areas.
"""
import argparse
import json
import os
from pathlib import Path
import struct
import subprocess

ROOT = Path(__file__).resolve().parents[1]


def read_hdr(path):
    data = path.read_bytes()
    magic, width, height, mode = struct.unpack_from('<4I', data)
    if magic != 0x31484452 or mode != 3 or len(data) != 16 + width * height * 8:
        raise ValueError('Expected a complete RF HDR v1 illuminance capture')
    return width, height, list(struct.iter_unpack('<4e', data[16:]))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--view', default='outpost-light-1f')
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--region', nargs=4, type=int, metavar=('X0', 'Y0', 'X1', 'Y1'),
                        help='Receiver-only rectangle; exclude the HUD and other surfaces')
    parser.add_argument('--frames', type=int, default=8)
    args = parser.parse_args()
    if args.frames < 3:
        parser.error('--frames must be at least 3')
    out = args.output.resolve()
    out.mkdir(parents=True, exist_ok=False)
    package = ROOT / 'build-windows/rasterfall-windows'
    env = dict(os.environ, RF_GPU_GI_DEBUG='3', RF_GPU_HDR_CAPTURE=str(out/'meter.hdr'), RF_GPU_SKY_TIME='0')
    argv = [str(package/'rasterfall.exe'), '--renderer', 'gpu-scene', '--skip-boot',
            '--map', 'rasterfall/assets/maps/outpost.map', '--window-size', '1280', '720',
            '--gpu-normal-scene', args.view, '0', '--frames', str(args.frames),
            '--gpu-frame-capture', str(out/'meter'), '--gpu-capture-frame', str(args.frames-2), '--frame-audit']
    with (out/'stdout.log').open('wb') as stdout, (out/'stderr.log').open('wb') as stderr:
        process = subprocess.run(argv, cwd=package, env=env, stdout=stdout, stderr=stderr, timeout=180)
    logs=(out/'stdout.log').read_text(encoding='utf-8', errors='replace')+(out/'stderr.log').read_text(encoding='utf-8', errors='replace')
    if process.returncode or any(error in logs for error in ('VUID-', 'SYNC-HAZARD', 'Validation Error')):
        raise RuntimeError(f'Capture failed ({process.returncode}); inspect {out}')
    width, height, pixels = read_hdr(out/'meter.hdr')
    region = args.region or [width//2, height//2, width//2+1, height//2+1]
    x0,y0,x1,y1 = region
    if not (0<=x0<x1<=width and 0<=y0<y1<=height):
        raise ValueError('Region outside the captured image')
    rows = []
    for y in range(y0,y1):
        for x in range(x0,x1):
            p = pixels[y*width+x]
            if p[3] != 1 or min(p[:3]) < 0: continue
            rows.append([v*100 for v in p[:3]])
    if not rows: raise ValueError('The region has no valid receiver pixels')
    report = {'view': args.view, 'region': region, 'samples':len(rows),
              'excluded_pixels':(x1-x0)*(y1-y0)-len(rows), 'unit':'lux',
              'area_weighting':'screen pixels, not physical surface area', 'exposure_independent':True}
    for index,name in enumerate(('direct','indirect','artistic_fill','total','with_artistic_fill')):
        values=sorted(row[index] if index<3 else sum(row[:2]) if index==3 else sum(row) for row in rows)
        report[name]={'min':values[0], 'median':values[len(values)//2], 'mean':sum(values)/len(values), 'max':values[-1]}
    (out/'report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
    print(json.dumps(report,indent=2))


if __name__ == '__main__': main()
