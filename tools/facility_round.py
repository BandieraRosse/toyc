#!/usr/bin/env python3
"""Generate and import the seven flat-material Outpost Hall V1 furniture assets."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('desk', 'chair', 'monitor', 'command_table', 'low_cabinet', 'bench', 'terminal')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender', default='blender')
    parser.add_argument('--tool-dir', type=Path, default=ROOT/'build-windows')
    parser.add_argument('--output', type=Path, default=ROOT/'tmp/outpost-hall')
    args = parser.parse_args()
    source = args.output.resolve()/'source'
    command = [args.blender, '-b', '--python-exit-code', '1', '--python',
               str(ROOT/'tools/blender/generate_rasterfall_props.py'), '--',
               '--output', str(source), '--overwrite', '--assets']
    subprocess.run(command + ['rf_facility_'+n for n in NAMES], cwd=ROOT, check=True)
    destination = ROOT/'rasterfall/private-assets/source/props/industrial'
    destination.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        asset = 'rf_facility_'+name
        shutil.copy2(source/(asset+'.glb'), destination/(asset+'.glb'))
        subprocess.run([sys.executable, str(ROOT/'tools/assets/import_asset.py'),
                        '--no-build', '--tool-dir', str(args.tool_dir.resolve()), '--force',
                        '--output-root', str(ROOT/'rasterfall/assets/models/props/industrial'),
                        str(ROOT/'tools/assets/manifests/props/industrial'/(asset+'.asset.json'))],
                       cwd=ROOT, check=True)
    shutil.copy2(source/'rasterfall_props.blend', destination/'facility_v1.blend')


if __name__ == '__main__':
    main()
