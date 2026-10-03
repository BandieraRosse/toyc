#!/usr/bin/env python3
"""Regenerate original skinned clothing and textured Heavy rigid equipment.

Requires Blender and prebuilt native asset tools. The private GLB intermediates
are reproducible; public meshes/textures and provenance are the packaged output.
"""
import argparse
import json
from pathlib import Path
import struct
import subprocess
import sys

sys.dont_write_bytecode=True
from assets.rfchar_import import glb,accessor

ROOT=Path(__file__).resolve().parents[1]
CLOTHING={'rf_clothing_field_jacket':'jacket','rf_clothing_combat_trousers':'trousers'}
GEAR=['rf_gear_heavy_'+slot for slot in ('head','chest','back','hip_l','hip_r')]


def inspect_source(path,skinned):
    doc,binary=glb(path)
    if skinned and (len(doc.get('skins',[]))!=1 or len(doc['skins'][0]['joints'])!=49):
        raise ValueError('clothing requires the shared body rig including finger chains')
    if not 1<=len(doc.get('images',[]))<=8: raise ValueError('texture budget')
    for image in doc['images']:
        if image.get('mimeType')!='image/png' or 'uri' in image: raise ValueError('embedded PNG required')
        view=doc['bufferViews'][image['bufferView']]
        start=view.get('byteOffset',0)
        data=binary[start:start+view['byteLength']]
        if data[:8]!=b'\x89PNG\r\n\x1a\n': raise ValueError('invalid PNG')
        width,height=struct.unpack_from('>II',data,16)
        if width>1024 or height>1024: raise ValueError('texture dimensions')
    for material in doc['materials']:
        if material.get('alphaMode','OPAQUE')!='OPAQUE': raise ValueError('opaque contract')
    for sampler in doc.get('samplers',[]):
        if (sampler.get('wrapS')!=33071 or sampler.get('wrapT')!=33071 or
            sampler.get('magFilter')!=9729 or sampler.get('minFilter')!=9987):
            raise ValueError('clamp / linear mip sampler required')
    triangles=0
    for mesh in doc['meshes']:
        for primitive in mesh['primitives']:
            triangles+=doc['accessors'][primitive['indices']]['count']//3
            material=doc['materials'][primitive.get('material',0)]
            if 'baseColorTexture' in material.get('pbrMetallicRoughness',{}):
                uv=accessor(doc,binary,primitive['attributes']['TEXCOORD_0'])
                if any(not 0<=v<=1 for pair in uv for v in pair): raise ValueError('UV0 range')
    if triangles>12000: raise ValueError('per-module triangle budget')
    return {'asset':path.stem,'triangles':triangles,'images':len(doc['images'])}


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--blender',default='blender')
    parser.add_argument('--tool-dir',type=Path,default=Path('build-windows'))
    parser.add_argument('--output',type=Path,default=Path('tmp/clothing'))
    parser.add_argument('--generate',action='store_true')
    parser.add_argument('--import-assets',action='store_true')
    parser.add_argument('--with-body',action='store_true',help='also rebuild/import the unchanged-body surface sample')
    args=parser.parse_args()
    output=(ROOT/args.output).resolve();output.mkdir(parents=True,exist_ok=True)
    report=[]
    for name in [*(['rf_humanoid_v2'] if args.with_body else []),*CLOTHING,*GEAR]:
        manifest=ROOT/'tools/assets/manifests/characters'/f'{name}.asset.json'
        value=json.loads(manifest.read_text(encoding='utf-8'))
        source=(manifest.parent/value['source']).resolve()
        commands=[]
        if args.generate:
            if name in CLOTHING:
                generator=ROOT/'tools/blender/generate_rasterfall_clothing.py'
                flags=['--garment',CLOTHING[name]]
            elif name=='rf_humanoid_v2':
                generator=ROOT/'tools/blender/generate_rasterfall_humanoid_v2.py'
                flags=[]
            else:
                generator=ROOT/'tools/blender/generate_rasterfall_humanoid_v2.py'
                flags=['--rigid-attachment='+name.removeprefix('rf_gear_').replace('_','-')]
            commands.append(('generate',[args.blender,'--background','--factory-startup',
                '--python-exit-code','1','--python',generator,'--','--output',source,*flags]))
        for step,command in commands:
            with (output/f'{name}-{step}.log').open('w',encoding='utf-8') as log:
                subprocess.run([str(x) for x in command],cwd=ROOT,stdout=log,
                    stderr=subprocess.STDOUT,check=True)
        skinned=name in CLOTHING or name=='rf_humanoid_v2'
        report.append(inspect_source(source,skinned))
        if name in GEAR:
            from rf_profession_round import check_profession_materials
            expected=check_profession_materials(source,'heavy')
        if args.import_assets:
            with (output/f'{name}-import.log').open('w',encoding='utf-8') as log:
                subprocess.run([sys.executable,str(ROOT/'tools/assets/import_asset.py'),
                    '--no-build','--tool-dir',str((ROOT/args.tool_dir).resolve()),
                    '--output-root',str(ROOT/'rasterfall/assets/models/characters'),
                    '--force',*(['--character-surface'] if skinned else []),
                    str(manifest)],cwd=ROOT,stdout=log,stderr=subprocess.STDOUT,check=True)
            if name=='rf_humanoid_v2':
                from rf_combat_character_round import check_shared_body_palette
                check_shared_body_palette(source,
                    (ROOT/'rasterfall/assets/models/characters/rf_humanoid_v2.rmesh').read_bytes())
            elif name in GEAR:
                from rf_profession_round import check_rmesh_materials
                check_rmesh_materials(ROOT/'rasterfall/assets/models/characters'/f'{name}.rmesh',
                    'heavy',expected)
        print('PASS '+name,flush=True)
    (output/'asset-report.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')


if __name__=='__main__': main()
