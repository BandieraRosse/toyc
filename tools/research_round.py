"""Generate and validate Research V1 through the shared static prop importer."""
import argparse
from pathlib import Path
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--blender',default='blender')
    p.add_argument('--tool-dir',type=Path,default=ROOT/'build-windows')
    p.add_argument('--generate',action='store_true')
    a=p.parse_args()
    source=ROOT/'rasterfall/private-assets/source/props/research'
    if a.generate:
        subprocess.run([a.blender,'-b','--python-exit-code','1','--python',
            str(ROOT/'tools/blender/generate_research.py'),'--','--output',str(source),
            '--overwrite'],cwd=ROOT,check=True)
    manifests=sorted((ROOT/'tools/assets/manifests/props/research').glob('*.asset.json'))
    if len(manifests)!=8:
        raise ValueError('Research V1 requires all eight asset manifests')
    for manifest in manifests:
        subprocess.run([sys.executable,str(ROOT/'tools/assets/import_asset.py'),
            '--no-build','--tool-dir',str(a.tool_dir.resolve()),'--force','--output-root',
            str(ROOT/'rasterfall/assets/models/props/research'),str(manifest)],cwd=ROOT,check=True)


if __name__=='__main__': main()
