"""Bake fixed GPU lighting with the staged Windows renderer, then verify reuse.

Run windows/NativeCodex.ps1 run --help once to build/stage the current assets.
The output directory is the same RF_GPU_BAKE_CACHE directory used by the game.
Uses the same GPU producer as native prewarm, with no CPU lightmap conversion.
Each native process is waited to completion.
"""
import argparse
import os
from pathlib import Path
import subprocess


def main():
    root = Path(__file__).resolve().parents[1]
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--executable', type=Path,
                        default=root / 'build-windows/rasterfall-windows/rasterfall.exe')
    parser.add_argument('--map', action='append', dest='maps', required=True,
                        help='Map path relative to the staged executable directory; repeat for more maps')
    parser.add_argument('--output', type=Path, help='Cache directory; default is build/ next to the executable')
    parser.add_argument('--logs', type=Path, default=root / 'tmp/lighting-bake')
    args = parser.parse_args()
    exe = args.executable.resolve(strict=True)
    output = (args.output or exe.parent / 'build').resolve()
    output.mkdir(parents=True, exist_ok=True)
    args.logs.mkdir(parents=True, exist_ok=True)
    env = dict(os.environ, RF_GPU_BAKED_LIGHTING='1', RF_GPU_BAKED_SURFACES='1',
               RF_GPU_INDIRECT_MODE='fast', RF_GPU_GI='1', RF_GPU_DAYLIGHT='1',
               RF_GPU_SKY_TIME='0', RF_GPU_BAKE_CACHE=str(output))
    for index, map_name in enumerate(args.maps):
        if not (exe.parent / map_name).is_file():
            raise FileNotFoundError(f'Map is not staged: {exe.parent / map_name}')
        argv = [str(exe), '--renderer', 'gpu-scene', '--skip-boot', '--map', map_name,
                '--window-size', '640', '480', '--frames', '3', '--frame-audit']
        for phase in ('prepare', 'verify'):
            log = args.logs / f'{index}-{Path(map_name).stem}-{phase}.log'
            with log.open('wb') as stream:
                process = subprocess.run(argv, cwd=exe.parent, env=env, stdout=stream,
                                         stderr=subprocess.STDOUT, timeout=240)
            text = log.read_text(encoding='utf-8', errors='replace')
            if process.returncode or any(marker in text for marker in ('VUID-', 'SYNC-HAZARD', 'Validation Error')):
                raise RuntimeError(f'{map_name}: {phase} failed ({process.returncode}); inspect {log}')
            if phase == 'verify' and 'rf-gpu-bake: hit key=' not in text:
                raise RuntimeError(f'{map_name}: persisted lighting was not reused; inspect {log}')
            print(f'{map_name}: {phase} PASS ({log})', flush=True)
    print(f'Lighting cache: {output}')


if __name__ == '__main__':
    main()
