"""Capture daylight receivers through the normal Windows native Scene path.

Normal color and optional exposure-independent HDR readings run separately.
These fixed views are visual/transport diagnostics, not performance samples.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image

from gpu_light_meter import read_hdr

ROOT = Path(__file__).resolve().parents[1]
VIEWS = ('atmosphere-lab', 'frontier-floor-1', 'frontier-floor-2',
         'outpost-light-b1', 'outpost-light-1f', 'outpost-light-2f', 'outpost-light-stairs',
         'frontier-workshop', 'frontier-exterior', 'frontier-stairs', 'frontier-stairs-upper', 'frontier-roof')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', required=True, type=Path)
    parser.add_argument('--views', nargs='+', choices=VIEWS, default=VIEWS[:3])
    parser.add_argument('--daylight', choices=('0', '1'), default='1')
    parser.add_argument('--indirect-mode', choices=('reference', 'fast', 'direct_only_diag'),
                        default=os.environ.get('RF_GPU_INDIRECT_MODE', 'reference'))
    parser.add_argument('--meter', action='store_true')
    parser.add_argument('--width', type=int, default=1280)
    parser.add_argument('--height', type=int, default=720)
    parser.add_argument('--executable', type=Path,
                        default=ROOT/'build-windows/rasterfall-windows/rasterfall.exe')
    args = parser.parse_args()
    if args.width<1 or args.height<1:
        parser.error('Capture extent must be positive')
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=False)
    executable = args.executable.resolve()
    options = {}
    if os.name == 'nt':
        startup = subprocess.STARTUPINFO()
        startup.dwFlags = subprocess.STARTF_USESHOWWINDOW
        startup.wShowWindow = 0
        options = dict(startupinfo=startup, creationflags=subprocess.CREATE_NO_WINDOW)
    report = dict(executable=str(executable),
                  executable_sha256=hashlib.sha256(executable.read_bytes()).hexdigest(),
                  daylight=args.daylight, indirect_mode=args.indirect_mode,
                  extent=[args.width,args.height], runs=[])
    for view in args.views:
        map_name = 'frontier_station_01' if view.startswith('frontier-') else 'outpost'
        map_path = ROOT/f'rasterfall/assets/maps/{map_name}.map'
        for meter in ((False, True) if args.meter else (False,)):
            name = view + ('-meter' if meter else '')
            folder = output/name
            folder.mkdir()
            env = dict(os.environ, RF_GPU_SKY_PRESET='clear', RF_GPU_SKY_TIME='0',
                       RF_GPU_DAYLIGHT=args.daylight, RF_GPU_LIGHT_PROFILE='0',
                       RF_GPU_INDIRECT_MODE=args.indirect_mode,
                       RF_GPU_LIGHT_ABLATION='none', RF_GPU_GI_DEBUG='3' if meter else '0')
            env.pop('RF_GPU_HDR_CAPTURE', None)
            if meter:
                env['RF_GPU_HDR_CAPTURE'] = str(folder/'meter.hdr')
            argv = [str(executable), '--renderer', 'gpu-scene', '--gpu-required', '--skip-boot',
                    '--map', str(map_path), '--window-size', str(args.width), str(args.height),
                    '--gpu-normal-scene', view, '0', '--gpu-normal-fixed-tick',
                    '--frames', '8', '--frame-audit', '--gpu-frame-capture',
                    str(folder/'capture'), '--gpu-capture-frame', '6']
            print('Capturing', name, flush=True)
            with (folder/'stdout.log').open('wb') as stdout, (folder/'stderr.log').open('wb') as stderr:
                process = subprocess.run(argv, cwd=executable.parent, env=env, stdout=stdout,
                                         stderr=stderr, timeout=180, **options)
            logs = ''.join((folder/file).read_text(encoding='utf-8', errors='replace')
                           for file in ('stdout.log', 'stderr.log'))
            ppm = folder/'capture.scene.ppm'
            if process.returncode or not ppm.is_file() or 'SCENE-NATIVE' not in logs or any(
                    error in logs for error in ('VUID-', 'SYNC-HAZARD', 'Validation Error')):
                raise RuntimeError(f'{name}: exit={process.returncode}; inspect {folder}')
            captured=Image.open(ppm)
            if captured.size!=(args.width,args.height):
                raise RuntimeError(f'{name}: unexpected capture extent {captured.size}')
            captured.save(ppm.with_suffix('.png'))
            if meter:
                read_hdr(folder/'meter.hdr')
            report['runs'].append(dict(view=view, meter=meter, exit_code=process.returncode,
                                       native_audits=logs.count('SCENE-NATIVE'), argv=argv,
                                       source_map_sha256=hashlib.sha256(map_path.read_bytes()).hexdigest()))
            (output/'report.json').write_text(json.dumps(report, indent=2)+'\n', encoding='utf-8')
            print(name, 'PASS', flush=True)


if __name__ == '__main__':
    main()
