"""Capture the normal GPU scene in the three outpost storeys and stairwell.

Run NativeCodex run/test first to build and stage assets. Each child is waited
on, with separate logs and a required capture; screenshots are not perf data.
"""
import argparse
import os
from pathlib import Path
import subprocess
import json
ROOT=Path(__file__).resolve().parents[1]
def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output',type=Path,default=ROOT/'tmp/outpost-lighting')
    args=parser.parse_args()
    out=args.output.resolve();out.mkdir(parents=True,exist_ok=True)
    package=ROOT/'build-windows/rasterfall-windows'
    env=dict(os.environ,RF_GPU_SKY_TIME='0',RF_GPU_VULKAN_VENDOR_ID='10de')
    results=[]
    for view in ('b1','1f','2f','stairs','research'):
        capture=out/view
        argv=[str(package/'rasterfall.exe'),'--renderer','gpu-scene','--skip-boot',
              '--map','rasterfall/assets/maps/outpost.map','--window-size','1280','720',
              '--gpu-normal-scene','outpost-light-'+view,'0','--frames','8',
              '--gpu-frame-capture',str(capture),'--gpu-capture-frame','6','--frame-audit']
        with (out/(view+'.out')).open('wb') as stdout,(out/(view+'.err')).open('wb') as stderr:
            process=subprocess.run(argv,cwd=package,env=env,stdout=stdout,stderr=stderr,timeout=120)
        ppm=out/(view+'.scene.ppm')
        logs=(out/(view+'.out')).read_text(encoding='utf-8',errors='replace')+(out/(view+'.err')).read_text(encoding='utf-8',errors='replace')
        if process.returncode or not ppm.is_file() or any(e in logs for e in ('VUID-','SYNC-HAZARD','Validation Error')):
            raise RuntimeError(f'{view}: exit={process.returncode}; inspect {out}')
        if 'Loading world source: rasterfall/assets/maps/rasterfall.map' in logs:
            raise RuntimeError(f'{view}: fixture switched away from the outpost')
        from PIL import Image
        Image.open(ppm).save(out/(view+'.png'))
        results.append({'view':view,'exit':process.returncode,'capture':str(ppm)})
        print(view,'PASS',flush=True)
    (out/'report.json').write_text(json.dumps(results,indent=2)+'\n',encoding='utf-8')
if __name__=='__main__':main()
