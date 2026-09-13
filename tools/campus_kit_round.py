#!/usr/bin/env python3
"""Generate/import/check and capture Temporary Campus Kit V0; no map writes."""
import argparse
import hashlib
import json
from pathlib import Path
import shutil
import struct
import sys
from architecture_round import run, sheet, ROOT
sys.path.insert(0,str(ROOT/'tools/assets'))
from import_asset import read_manifest, validate_outputs

MANIFESTS=sorted((ROOT/'tools/assets/manifests/props/campus').glob('*.asset.json'))
SCENES=('campus-corner','campus-corner-near','campus-corner-mid','campus-corner-far','campus-corner-ground')

def check_mesh(path,dims):
    validate_outputs(path,path.with_suffix('.textures'),[])
    data=path.read_bytes()
    version,nv,ni=struct.unpack_from('<III',data,4)
    np,nm,pa,ma=struct.unpack_from('<IIII',data,44)
    assert version==2 and 1<=np<=3 and 1<=nm<=3
    va=ma+nm*16
    ia=va+nv*24
    assert ia+ni*4==len(data)
    positions=[struct.unpack_from('<iii',data,va+i*24) for i in range(nv)]
    indices=struct.unpack_from('<'+'I'*ni,data,ia)
    assert all(i<nv for i in indices)
    w,d,h=dims
    expected=(-w*116,0,-d*116,w*116,h*232,d*116)
    bounds=tuple(min(v[i] for v in positions) for i in range(3))+tuple(max(v[i] for v in positions) for i in range(3))
    assert all(abs(a-b)<=1 for a,b in zip(bounds,expected)),(path,bounds,expected)
    assert 2<=ni//3<=400
    snap=[int(bounds[i+3]*2207/1000)-int(bounds[i]*2207/1000) for i in range(3)]
    return dict(bounds_rmesh=bounds,snap_dimensions_rfu=snap,triangles=ni//3,materials=nm,bytes=len(data),
                sha256=hashlib.sha256(data).hexdigest())

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,default=ROOT/'tmp/campus-kit-v0')
    p.add_argument('--generate',action='store_true')
    p.add_argument('--capture',action='store_true')
    p.add_argument('--deterministic',action='store_true')
    p.add_argument('--audit',action='store_true',help='Check and sheet existing public industrial/architectural assets')
    a=p.parse_args()
    if a.deterministic and not (a.generate or a.capture):
        p.error('--deterministic requires --generate or --capture')
    out=a.output.resolve(); out.mkdir(parents=True,exist_ok=True)
    specs=[read_manifest(m) for m in MANIFESTS]
    if a.generate:
        run(['make','app-glb2rmesh','build/toyasset'],out/'converters.log')
        command=['blender','-b','--python-exit-code','1','--python',
                 'tools/blender/generate_campus_kit.py','--','--output']
        run([*command,out/'source'],out/'generation.log')
        dest=ROOT/'rasterfall/private-assets/source/props/campus'
        dest.mkdir(parents=True,exist_ok=True)
        for m,s in zip(MANIFESTS,specs):
            shutil.copy2(out/'source'/(s['id']+'.glb'),dest/(s['id']+'.glb'))
            run(['python3','tools/assets/import_asset.py','--force','--no-build',
                 '--output-root','rasterfall/assets/models/props/campus',m],out/(s['id']+'-import.log'))
        shutil.copy2(out/'source/temporary_campus_v0.blend',dest/'temporary_campus_v0.blend')
        if a.deterministic:
            run([*command,out/'source-repeat'],out/'generation-repeat.log')
            for s in specs:
                name=s['id']+'.glb'
                assert (out/'source'/name).read_bytes()==(out/'source-repeat'/name).read_bytes(),name
    report={}
    if a.audit:
        audit=out/'audit'; audit.mkdir(exist_ok=True)
        pictures=[]
        for manifest in sorted((ROOT/'tools/assets/manifests/props/industrial').glob('*.asset.json')):
            spec=read_manifest(manifest)
            path=ROOT/'rasterfall/assets/models/props/industrial'/(spec['id']+'.rmesh')
            validate_outputs(path,path.with_suffix('.textures'),[])
            directory=audit/spec['id']; directory.mkdir(exist_ok=True)
            run([ROOT/'build/rasterfall','--model-static-views',path,directory],audit/(spec['id']+'.log'))
            views=sorted(directory.glob('*.bmp'))
            assert len(views)==4,(spec['id'],views)
            labelled=audit/(spec['id']+'.bmp')
            shutil.copy2(views[-1],labelled)
            pictures.append(labelled)
            report[spec['id']]=dict(bytes=path.stat().st_size,sha256=hashlib.sha256(path.read_bytes()).hexdigest())
        sheet(pictures,out/'existing-assets.png',columns=4,width=400)
        run([ROOT/'build/rasterfall','--visual-capture','arch-family','--visual-output',audit/'arch-family.bmp'],audit/'arch-family.log')
        sheet([audit/'arch-family.bmp'],out/'existing-architecture.png',columns=1,width=1280)
    for m,s in zip(MANIFESTS,specs):
        run(['python3','tools/assets/import_asset.py','--validate-only','--output-root',
             'rasterfall/assets/models/props/campus',m],out/(s['id']+'-validate.log'))
        report[s['id']]=check_mesh(ROOT/'rasterfall/assets/models/props/campus'/(s['id']+'.rmesh'),s['dimensions_m'])
    if a.capture:
        capture=out/'captures'; capture.mkdir(exist_ok=True)
        scenes=list(SCENES)+['campus-asset-'+s['id'].removeprefix('rf_campus_') for s in specs]
        for scene in scenes:
            target=capture/(scene+'.bmp')
            cmd=[ROOT/'build/rasterfall','--visual-capture',scene,'--visual-output']
            run([*cmd,target],out/(scene+'.log'))
            if a.deterministic:
                repeat=capture/(scene+'-repeat.bmp')
                run([*cmd,repeat],out/(scene+'-repeat.log'))
                assert target.read_bytes()==repeat.read_bytes(),scene
            report[scene]=dict(sha256=hashlib.sha256(target.read_bytes()).hexdigest())
        sheet([capture/(s+'.bmp') for s in SCENES[:4]],out/'campus-corner.png',columns=2,width=640)
        sheet([capture/'campus-corner-ground.bmp'],out/'campus-ground.png',columns=1,width=1280)
        sheet([capture/(s+'.bmp') for s in scenes[len(SCENES):]],out/'kit-sheet.png',columns=3,width=480)
    (out/'integrity-and-fingerprints.json').write_text(json.dumps(report,indent=2)+'\n')
    print('Campus integrity, metric bounds and requested deterministic checks PASS:',out)
if __name__=='__main__':
    main()
