#!/usr/bin/env python3
"""Rebuild and audit Frontier Station's original static industrial kit.

Uses the established Blender -> GLB -> native importer -> RFM2 path. The public
runtime files and manifests are deterministic; source GLB/Blend stays private.
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import sys

ROOT=Path(__file__).resolve().parents[1]
SPECS={"canopy":(6,3,.4),"cargo_rack":(4,1.8,3.2),"fence":(6,.32,2.5),
       "collector":(4.8,3.4,4.5),"rock":(4,3,1.8),"bollard":(.32,.32,1.2)}
PUBLIC=ROOT/"rasterfall/assets/models/props/frontier"
SOURCE=ROOT/"rasterfall/private-assets/source/props/frontier"
MANIFESTS=ROOT/"tools/assets/manifests/props/frontier"


def run(args):
    subprocess.run(list(map(str,args)),cwd=ROOT,check=True)


def audit():
    metrics={}
    for part,dimensions in SPECS.items():
        name="rf_frontier_"+part
        path=PUBLIC/(name+".rmesh")
        data=path.read_bytes()
        assert data[:4]==b"RFM2"
        version,vertices,indices,scale=struct.unpack_from("<4I",data,4)
        assert version==2 and scale==232 and indices%3==0
        bounds=struct.unpack_from("<6i",data,20)
        primitives,materials,po,mo=struct.unpack_from("<4I",data,44)
        assert 1<=materials<=4 and 1<=primitives<=4
        vo=mo+materials*16
        io=vo+vertices*24
        assert io+indices*4==len(data)
        points=[struct.unpack_from("<3i",data,vo+i*24) for i in range(vertices)]
        tris=struct.unpack_from("<%dI"%indices,data,io)
        assert all(i<vertices for i in tris)
        width,depth,height=dimensions
        expected=(-width/2,0,-depth/2,width/2,height,depth/2)
        assert max(abs(bounds[i]/scale-expected[i]) for i in range(6))<=1.1/scale,(name,bounds)
        for at in range(0,indices,3):
            a,b,c=[points[i] for i in tris[at:at+3]]
            u=[b[k]-a[k] for k in range(3)];v=[c[k]-a[k] for k in range(3)]
            assert any(u[(k+1)%3]*v[(k+2)%3]-u[(k+2)%3]*v[(k+1)%3] for k in range(3)),(name,"collapsed triangle")
        surfaces=[]
        for index in range(materials):
            color,metallic,roughness,texture=struct.unpack_from("<IHHI",data,mo+index*16)
            assert texture==0xffffffff,(name,"unexpected texture")
            surfaces.append({"color":f"{color&0xffffff:06X}",
                             "metallic":round(metallic/65535,4),
                             "roughness":round(roughness/65535,4)})
        metrics[part]={"triangles":indices//3,"vertices":vertices,"materials":materials,
                       "surfaces":surfaces,"bounds":bounds,"sha256":hashlib.sha256(data).hexdigest()}
        print(f"{name}: {indices//3} triangles / {materials} materials / pivot and quantization OK")
        print("  PBR",json.dumps(surfaces,separators=(",",":")))
    return metrics


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--generate",action="store_true")
    parser.add_argument("--blender",default="E:/Blender 5.2/blender.exe")
    parser.add_argument("--tool-dir",type=Path,default=ROOT/"build-windows")
    parser.add_argument("--report",type=Path)
    parser.add_argument("--capture",action="store_true",help="capture native model views using the existing staged game")
    parser.add_argument("--capture-dir",type=Path,default=ROOT/"tmp/frontier-assets/model-views")
    args=parser.parse_args()
    if args.generate:
        run([args.blender,"--background","--factory-startup","--python-exit-code","1",
             "--python",ROOT/"tools/blender/generate_frontier_station.py","--","--output",SOURCE])
        MANIFESTS.mkdir(parents=True,exist_ok=True);PUBLIC.mkdir(parents=True,exist_ok=True)
        for part,dimensions in SPECS.items():
            name="rf_frontier_"+part
            manifest=MANIFESTS/(name+".asset.json")
            doc={"schema":1,"id":name,"type":"static_prop",
                 "source":"../../../../../rasterfall/private-assets/source/props/frontier/"+name+".glb",
                 "dimensions_m":dimensions}
            manifest.write_text(json.dumps(doc,separators=(",",":"))+"\n",encoding="utf-8",newline="\n")
            run([sys.executable,ROOT/"tools/assets/import_asset.py","--no-build","--force",
                 "--tool-dir",args.tool_dir.resolve(),"--output-root",PUBLIC,manifest])
    report=audit()
    if args.capture:
        executable=ROOT/"build-windows/rasterfall-windows/rasterfall.exe"
        for part in SPECS:
            target=(args.capture_dir/part).resolve();target.mkdir(parents=True,exist_ok=True)
            with (target/"stdout.log").open("wb") as stdout, (target/"stderr.log").open("wb") as stderr:
                result=subprocess.run([str(executable),"--model-views",str(PUBLIC/("rf_frontier_"+part+".rmesh")),str(target)],
                    cwd=executable.parent,stdout=stdout,stderr=stderr,timeout=60,
                    creationflags=subprocess.CREATE_NO_WINDOW if os.name=="nt" else 0)
            if result.returncode:
                raise SystemExit(f"Model capture failed: {part}; see {target}")
            print(f"Captured {part}: {target}",flush=True)
    if args.report:
        args.report.parent.mkdir(parents=True,exist_ok=True)
        args.report.write_text(json.dumps(report,indent=2)+"\n",encoding="utf-8",newline="\n")


if __name__=="__main__":main()
