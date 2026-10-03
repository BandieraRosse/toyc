#!/usr/bin/env python3
"""MAT1 producer/native consumer boundaries using the public RFCHAR fixture."""
import argparse
import copy
import struct
import subprocess
import tempfile
from pathlib import Path
from rfchar_import import convert, glb
from import_asset import ImportFailure, validate_outputs
from test_rfchar_materials import write_glb


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('fixture',type=Path)
    p.add_argument('--validator',type=Path,required=True)
    p.add_argument('--runtime',type=Path,required=True)
    a=p.parse_args()
    original,binary=glb(a.fixture)
    checks=0
    with tempfile.TemporaryDirectory(prefix='rfchar-surface-') as temp:
        root=Path(temp);source=root/'fixture.glb';mesh=root/'fixture.rmesh'
        textures=root/'fixture.textures';textures.mkdir()
        def runtime(ok):
            nonlocal checks
            r=subprocess.run([str(a.runtime.resolve()),str(mesh)],capture_output=True,text=True)
            assert (r.returncode==0)==ok,r.stdout+r.stderr
            checks+=1
        def run(doc):
            write_glb(source,doc,binary)
            convert(source,mesh,a.validator,surface=True)
            return mesh.read_bytes()
        document=copy.deepcopy(original)
        for material in document['materials']:
            material['pbrMetallicRoughness'].update(roughnessFactor=.73,metallicFactor=.12)
        raw=run(document)
        assert struct.unpack_from('<I',raw,4)[0]==15
        tail=32+16*len(document['materials']);offset=len(raw)-tail
        assert raw[offset:offset+4]==b'MAT1'
        validate_outputs(mesh,textures,[]);runtime(True)
        assert run(document)==raw
        for field,value,code in [('roughnessFactor',-1,'MATERIAL_FIELD'),
                                 ('metallicFactor',float('nan'),'MATERIAL_FIELD'),
                                 ('metallicRoughnessTexture',{'index':0},'MATERIAL_CAPABILITY')]:
            bad=copy.deepcopy(document);bad['materials'][0]['pbrMetallicRoughness'][field]=value
            mesh.write_bytes(raw)
            try:run(bad)
            except (ValueError,subprocess.CalledProcessError) as e:
                if isinstance(e,ValueError): assert code in str(e),str(e)
            else:raise AssertionError('invalid surface accepted')
            assert mesh.read_bytes()==raw
            checks+=1
        for at,value in [(offset+8,2),(offset+12,100),(offset+24,1),(offset+32,3),
                         (offset+36,0x7fc00000),(offset+44,1)]:
            bad=bytearray(raw);struct.pack_into('<I',bad,at,value);mesh.write_bytes(bad)
            runtime(False)
            try:validate_outputs(mesh,textures,[])
            except ImportFailure:pass
            else:raise AssertionError('invalid MAT1 installed')
        mesh.write_bytes(raw[:-1]);runtime(False)
        # A texture reference is mandatory, not a silent solid-color fallback.
        document['textures']=[{'source':0,'sampler':0}]
        document['images']=[{'uri':'texture.png'}]
        document['samplers']=[{'wrapS':33071,'wrapT':33071,'magFilter':9729,'minFilter':9987}]
        document['materials'][0]['pbrMetallicRoughness']['baseColorTexture']={'index':0}
        textured=run(document);runtime(False)
        header=struct.pack('<4sHHIIHHIII',b'TTEX',1,32,2,2,3,1,32,12,0)
        texture=textures/'texture_000.ttex';texture.write_bytes(header+bytes([255,255,255]*4))
        validate_outputs(mesh,textures,[]);runtime(True)
        for field,value in [('wrapS',10497),('minFilter',9729)]:
            bad=copy.deepcopy(document);bad['samplers'][0][field]=value
            try:run(bad)
            except ValueError as e:assert 'MATERIAL_CAPABILITY' in str(e)
            else:raise AssertionError('unsupported sampler accepted')
            assert mesh.read_bytes()==textured
            checks+=1
        bad=copy.deepcopy(document)
        del bad['meshes'][0]['primitives'][0]['attributes']['TEXCOORD_0']
        try:run(bad)
        except ValueError as e:assert 'MATERIAL_UV' in str(e)
        else:raise AssertionError('missing UV accepted')
        checks+=1
        texture.write_bytes(header+bytes(3));runtime(False)
    print(f'rfchar-surfaces: PASS ({checks} producer/native boundary checks)')


if __name__=='__main__':main()
