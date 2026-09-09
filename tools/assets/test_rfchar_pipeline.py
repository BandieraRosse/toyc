#!/usr/bin/env python3
"""End-to-end RFCHAR fixture, importer, runtime pose and visual smoke test."""
import argparse, hashlib, os, struct, subprocess, tempfile
from pathlib import Path
import rfchar_import

def check_attachment_bind(glb, mesh):
    document, _ = rfchar_import.glb(glb)
    nodes=document['nodes']; parents={c:i for i,n in enumerate(nodes) for c in n.get('children',[])}
    def global_matrix(i):
        m=rfchar_import.local(nodes[i])
        return rfchar_import.mm(global_matrix(parents[i]),m) if i in parents else m
    raw=mesh.read_bytes(); skin=struct.unpack_from('<I',raw,60)[0]
    char=skin+struct.unpack_from('<I',raw,skin+4)[0]
    count=struct.unpack_from('<I',raw,char+16)[0]
    joints=document['skins'][0]['joints']
    for i in range(count):
        aid,parent,x,y,z,*tail=struct.unpack_from('<IIiii4fI',raw,char+32+84+i*40)
        node=next(j for j,n in enumerate(nodes) if n.get('name')=='RF_ATTACH_'+rfchar_import.ATTACH[aid])
        expected=global_matrix(node); base=global_matrix(joints[parent])
        actual=rfchar_import.local({'rotation':tail[:4]})
        assert max(abs(actual[r+4*c]-expected[r+4*c]) for r in range(3) for c in range(3))<1e-5
        assert max(abs(v+base[12+k]*512-expected[12+k]*512) for k,v in enumerate((x,y,z)))<=0.501

BASELINE = {
    'bind/front.bmp': '78acc24b1adbb56dc6127d499a432ccbfc8e3b187eb68fdf5b0af91749c724c7',
    'bind/three-quarter.bmp': '287822f6e916d8d5f0b22b75a6a697d6754248fd03e0231d108ba9b9f195208d',
    'posed/front.bmp': '192edeca5f21b3840bc78983e9393558b67a858778a24c352f4b057b99b5d76f',
    'posed/three-quarter.bmp': 'dfac02cea3e3ba777f636995b8228eaae7bf4cb951d8ff10d2cfc6cd8728ab20',
}

def run(argv,cwd): subprocess.run([str(x) for x in argv],cwd=cwd,check=True)
def main():
    p=argparse.ArgumentParser();p.add_argument('--blender',default='blender');p.add_argument('--blender-python-path');a=p.parse_args()
    repo=Path(__file__).resolve().parents[2]
    run(['make','app-glb-inspect','app-rasterfall','build/rfchar_runtime_test'],repo)
    with tempfile.TemporaryDirectory(prefix='rfchar-v1-') as td:
        root=Path(td);glb=root/'fixture.glb';mesh=root/'fixture.rmesh';bind=root/'bind';posed=root/'posed'
        command=[a.blender,'--background','--factory-startup']
        if a.blender_python_path:command += ['--python-expr',f"import sys;sys.path.insert(0,{a.blender_python_path!r})"]
        command += ['--python',repo/'tools/blender/generate_rfchar_fixture.py','--','--output',glb]
        run(command,repo);run([repo/'build/glb-inspect',glb,'contract'],repo)
        run(['python3',repo/'tools/assets/rfchar_import.py',glb,mesh],repo)
        check_attachment_bind(glb,mesh)
        run([repo/'build/rfchar_runtime_test',mesh],repo)
        run([repo/'build/rasterfall','--model-pose-views',mesh,bind,'bind'],repo)
        run([repo/'build/rasterfall','--model-pose-views',mesh,posed,'rfchar-test'],repo)
        wanted=['front.bmp','three-quarter.bmp']; hashes={}
        for name in wanted:
            for kind,path in [('bind',bind/name),('posed',posed/name)]:
                hashes[kind+'/'+name]=hashlib.sha256(path.read_bytes()).hexdigest()
        if hashes['bind/front.bmp']==hashes['posed/front.bmp']:raise SystemExit('posed capture equals bind capture')
        if hashes != BASELINE:raise SystemExit('RFCHAR visual baseline mismatch: '+repr(hashes))
        print('rfchar-pipeline: PASS')
        for name,value in hashes.items():print(name,value)
if __name__=='__main__':main()
