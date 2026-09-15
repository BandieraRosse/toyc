#!/usr/bin/env python3
"""Run the deterministic legacy-Eula acceptance and assemble one comparison PNG."""
import argparse
import struct
import subprocess
import zlib
from pathlib import Path

MODELS=("full","lod1","compact-lod2","hybrid")
POSES=("bind","idle","walk-left","walk-right","knee-bend","head-left","head-right","head-up","head-down","rifle-idle","rifle-aim")

def bmp(path):
    d=path.read_bytes(); off=struct.unpack_from("<I",d,10)[0]
    w,h=struct.unpack_from("<ii",d,18); bpp=struct.unpack_from("<H",d,28)[0]
    top=h<0;h=abs(h);stride=((w*bpp+31)//32)*4;out=bytearray(w*h*3)
    for y in range(h):
        row=off+(y if top else h-1-y)*stride
        for x in range(w):
            s=row+x*(bpp//8);q=(y*w+x)*3;out[q:q+3]=(d[s+2],d[s+1],d[s])
    return w,h,out

def chunk(kind,data):
    return struct.pack(">I",len(data))+kind+data+struct.pack(">I",zlib.crc32(kind+data)&0xffffffff)

def png(path,w,h,pixels):
    rows=b"".join(b"\0"+pixels[y*w*3:(y+1)*w*3] for y in range(h))
    path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",w,h,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(rows,9))+chunk(b"IEND",b""))

def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--models",default="rasterfall/private-assets/models")
    ap.add_argument("--output",default="tmp/eula-animation-acceptance/comparison.png")
    ap.add_argument("--capture-dir")
    ap.add_argument("--rasterfall",default="build/rasterfall")
    ap.add_argument("--cell-width",type=int,default=240)
    a=ap.parse_args(); out=Path(a.output); cap=Path(a.capture_dir or out.with_suffix("")/"captures")
    cap.mkdir(parents=True,exist_ok=True)
    if not a.capture_dir: subprocess.run([a.rasterfall,"--eula-animation-acceptance",a.models,str(cap)],check=True)
    paths=[cap/m/f"{p}.bmp" for m in MODELS for p in POSES]
    missing=[str(p) for p in paths if not p.is_file()]
    if missing: raise SystemExit("missing capture(s): "+", ".join(missing))
    images=[bmp(p) for p in paths];cw=a.cell_width;ch=images[0][1]*cw//images[0][0]
    sw=cw*len(POSES);sh=ch*len(MODELS);sheet=bytearray(sw*sh*3)
    for n,(w,h,src) in enumerate(images):
        row,col=divmod(n,len(POSES))
        for y in range(ch):
            sy=min(h-1,y*h//ch)
            for x in range(cw):
                sx=min(w-1,x*w//cw);s=(sy*w+sx)*3;d=((row*ch+y)*sw+col*cw+x)*3
                sheet[d:d+3]=src[s:s+3]
    out.parent.mkdir(parents=True,exist_ok=True);png(out,sw,sh,sheet)
    print(f"Eula animation acceptance sheet: {out}")

if __name__=="__main__": main()
