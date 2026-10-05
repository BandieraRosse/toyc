"""Generate and import the two original, model-bound outpost luminaires."""
import json
from pathlib import Path
import subprocess
import sys
ROOT=Path(__file__).resolve().parents[1]
source=ROOT/'rasterfall/private-assets/source/props/lighting'
public=ROOT/'rasterfall/assets/models/props/lighting'
manifests=ROOT/'tools/assets/manifests/props/lighting'
subprocess.run(['E:/Blender 5.2/blender.exe','--background','--factory-startup',
    '--python-exit-code','1','--python',str(ROOT/'tools/blender/generate_outpost_lights.py'),
    '--','--output',str(source)],cwd=ROOT,check=True)
manifests.mkdir(parents=True,exist_ok=True)
for kind,dimensions in [('ceiling',(1.4,.7,.16)),('wall',(.65,.25,.45))]:
    name='rf_light_'+kind
    manifest=manifests/(name+'.asset.json')
    doc={'schema':1,'id':name,'type':'static_prop',
         'source':'../../../../../rasterfall/private-assets/source/props/lighting/'+name+'.glb',
         'dimensions_m':dimensions}
    manifest.write_text(json.dumps(doc,indent=2)+'\n',encoding='utf-8',newline='\n')
    subprocess.run([sys.executable,'tools/assets/import_asset.py','--no-build','--force',
        '--tool-dir','build-windows','--output-root',str(public),str(manifest)],cwd=ROOT,check=True)
