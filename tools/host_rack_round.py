"""Generate and install the nine public Host Rack V2 components."""
import argparse
from pathlib import Path
import shutil
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[1]
NAMES = ('rack_frame', 'blank_panel', 'cpu_module', 'memory_module',
         'rack_fan_panel', 'cpu_header', 'memory_header', 'power_bundle', 'data_bundle')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender', default='blender')
    parser.add_argument('--tool-dir', type=Path, default=ROOT/'build-windows')
    args = parser.parse_args()
    source = ROOT/'tmp/host-rack/source'
    subprocess.run([args.blender, '-b', '--python-exit-code', '1', '--python',
                    str(ROOT/'tools/blender/generate_host_rack.py'), '--',
                    '--output', str(source)], check=True, cwd=ROOT)
    private = ROOT/'rasterfall/private-assets/source/props/host'
    private.mkdir(parents=True, exist_ok=True)
    for name in NAMES:
        asset = 'rf_host_'+name
        shutil.copy2(source/(asset+'.glb'), private/(asset+'.glb'))
        subprocess.run([sys.executable, str(ROOT/'tools/assets/import_asset.py'),
                        '--no-build', '--force', '--tool-dir', str(args.tool_dir.resolve()),
                        '--output-root', str(ROOT/'rasterfall/assets/models/props/host'),
                        str(ROOT/'tools/assets/manifests/props/host'/(asset+'.asset.json'))],
                       check=True, cwd=ROOT)
    shutil.copy2(source/'host_rack_v2.blend', private/'host_rack_v2.blend')


if __name__ == '__main__':
    main()
