#!/usr/bin/env python3
"""Strict RFCHAR V1 GLB to RFM2 v14 character converter."""

import argparse, json, math, struct, subprocess
from pathlib import Path

ROLES = ["RF_ROOT","RF_HIPS","RF_SPINE","RF_CHEST","RF_UPPER_CHEST","RF_NECK","RF_HEAD",
         "RF_L_SHOULDER","RF_L_UPPER_ARM","RF_L_FOREARM","RF_L_HAND",
         "RF_R_SHOULDER","RF_R_UPPER_ARM","RF_R_FOREARM","RF_R_HAND",
         "RF_L_UPPER_LEG","RF_L_LOWER_LEG","RF_L_FOOT",
         "RF_R_UPPER_LEG","RF_R_LOWER_LEG","RF_R_FOOT"]
ATTACH = ["WEAPON_R","WEAPON_L","FOREGRIP","BACK","CHEST","HEAD","HIP_L","HIP_R"]

def glb(path):
    raw=path.read_bytes()
    if raw[:4]!=b"glTF" or struct.unpack_from("<II",raw,4)!=(2,len(raw)): raise ValueError("invalid GLB")
    at=12; doc=blob=None
    while at<len(raw):
        n,k=struct.unpack_from("<II",raw,at); data=raw[at+8:at+8+n];at+=8+n
        if k==0x4e4f534a: doc=json.loads(data.rstrip(b" \0"))
        elif k==0x004e4942: blob=data
    if doc is None or blob is None: raise ValueError("GLB requires JSON and BIN chunks")
    return doc,blob

def mm(a,b):
    return [sum(a[r+4*k]*b[k+4*c] for k in range(4)) for c in range(4) for r in range(4)]

def local(node):
    if "matrix" in node:return node["matrix"]
    x,y,z,w=node.get("rotation",[0,0,0,1]);sx,sy,sz=node.get("scale",[1,1,1]);tx,ty,tz=node.get("translation",[0,0,0])
    return [(1-2*y*y-2*z*z)*sx,(2*x*y+2*z*w)*sx,(2*x*z-2*y*w)*sx,0,
            (2*x*y-2*z*w)*sy,(1-2*x*x-2*z*z)*sy,(2*y*z+2*x*w)*sy,0,
            (2*x*z+2*y*w)*sz,(2*y*z-2*x*w)*sz,(1-2*x*x-2*y*y)*sz,0,tx,ty,tz,1]

def accessor(doc,blob,index):
    a=doc["accessors"][index];v=doc["bufferViews"][a["bufferView"]]
    comps={"SCALAR":1,"VEC2":2,"VEC3":3,"VEC4":4,"MAT4":16}[a["type"]]
    fmt={5120:"b",5121:"B",5122:"h",5123:"H",5125:"I",5126:"f"}[a["componentType"]]
    size=struct.calcsize(fmt); stride=v.get("byteStride",size*comps);start=v.get("byteOffset",0)+a.get("byteOffset",0)
    out=[]
    for i in range(a["count"]):
        values=list(struct.unpack_from("<"+fmt*comps,blob,start+i*stride))
        if a.get("normalized"):
            top={5121:255,5123:65535}.get(a["componentType"])
            values=[x/top for x in values]
        out.append(values[0] if comps==1 else values)
    return out

def srgb(x): return max(0,min(255,int(math.sqrt(max(0,min(1,x))*65025))))
def q16(x): return max(0,min(65535,int(x*65535+0.5)))
def s16(x): return max(-32767,min(32767,int(x*32767+(0.5 if x>=0 else -0.5))))
def i32(x): return int(x*512+(0.5 if x>=0 else -0.5))

def convert(source,output,validator):
    subprocess.run([str(validator),str(source),"contract"],check=True)
    d,b=glb(source); nodes=d["nodes"]; parent=[-1]*len(nodes)
    for i,n in enumerate(nodes):
        for c in n.get("children",[]): parent[c]=i
    cache={}
    def glob(i):
        if i not in cache: cache[i]=mm(glob(parent[i]),local(nodes[i])) if parent[i]>=0 else local(nodes[i])
        return cache[i]
    names={n.get("name",""):i for i,n in enumerate(nodes)}; skin=d["skins"][0]; joints=skin["joints"]
    joint_runtime={node:i for i,node in enumerate(joints)}
    vertices=[];weights=[];indices=[];primitives=[]
    for ni,n in enumerate(nodes):
        if "mesh" not in n:continue
        for p in d["meshes"][n["mesh"]]["primitives"]:
            a=p["attributes"]; pos=accessor(d,b,a["POSITION"]); nor=accessor(d,b,a["NORMAL"])
            uv=accessor(d,b,a["TEXCOORD_0"]) if "TEXCOORD_0" in a else [[0,0]]*len(pos)
            jo=accessor(d,b,a["JOINTS_0"]);we=accessor(d,b,a["WEIGHTS_0"]);base=len(vertices)
            for k in range(len(pos)):
                vertices.append((i32(pos[k][0]),i32(pos[k][1]),i32(pos[k][2]),s16(nor[k][0]),s16(nor[k][1]),s16(nor[k][2]),q16(uv[k][0]),q16(uv[k][1])))
                active=[(int(jo[k][x]),we[k][x]) for x in range(4) if we[k][x]>1e-6]
                if len(active)==1: weights.append((active[0][0],0xffff,65535,0))
                else:
                    total=active[0][1]+active[1][1];weights.append((active[0][0],active[1][0],q16(active[0][1]/total),1))
            first=len(indices);indices += [base+x for x in accessor(d,b,p["indices"])]
            primitives.append((first,len(indices)-first,p.get("material",0)))
    materials=d.get("materials",[]) or [{}]
    out=bytearray(64);out[:4]=b"RFM2";struct.pack_into("<IIII",out,4,14,len(vertices),len(indices),512)
    mins=[min(v[i] for v in vertices) for i in range(3)];maxs=[max(v[i] for v in vertices) for i in range(3)]
    struct.pack_into("<6i",out,20,*mins,*maxs);struct.pack_into("<IIIII",out,44,len(primitives),len(materials),64,64+16*len(primitives),0)
    for first,count,mat in primitives: out+=struct.pack("<IIII",first,count,mat,0)
    for m in materials:
        p=m.get("pbrMetallicRoughness",{});c=p.get("baseColorFactor",[1,1,1,1]);color=(srgb(c[0])<<16)|(srgb(c[1])<<8)|srgb(c[2]);tex=p.get("baseColorTexture",{}).get("index",0xffffffff)
        rec=bytearray(40);struct.pack_into("<I",rec,0,color);struct.pack_into("<H",rec,4,q16(c[3]));struct.pack_into("<I",rec,8,tex);out+=rec
    for v in vertices: out+=struct.pack("<iii3hHH",*v)+bytes(14)
    out+=struct.pack("<"+"I"*len(indices),*indices)
    struct.pack_into("<I",out,60,len(out)); names_blob=bytearray();offsets=[]
    for j in joints: offsets.append(len(names_blob));names_blob+=nodes[j].get("name","").encode()+b"\0"
    skin_body=bytearray()
    for j,node in enumerate(joints):
        g=glob(node);pj=parent[node];runtime_parent=joint_runtime.get(pj,-1)
        skin_body+=struct.pack("<iHHiiiIif",runtime_parent,0,0,i32(g[12]),i32(g[13]),i32(g[14]),offsets[j],-1,0.0)
    for w in weights:skin_body+=struct.pack("<HHHBB",*w,0)
    skin_total=32+len(skin_body)+len(names_blob);out+=struct.pack("<8I",0x314e4b53,skin_total,len(joints),32,len(vertices),8,len(names_blob),0)+skin_body+names_blob
    attachments=[]
    for aid,name in enumerate(ATTACH):
        node=names.get("RF_ATTACH_"+name)
        if node is None:continue
        p=parent[node];t=nodes[node].get("translation",[0,0,0]);q=nodes[node].get("rotation",[0,0,0,1])
        attachments.append(struct.pack("<IIiii4fI",aid,joint_runtime[p],i32(t[0]),i32(t[1]),i32(t[2]),*q,0))
    roles=[joint_runtime[names[x]] if x in names else 0xffffffff for x in ROLES]
    size=32+4*len(roles)+40*len(attachments)
    out+=struct.pack("<8I",0x31524843,size,1,len(roles),len(attachments),40,0,0)+struct.pack("<21I",*roles)+b"".join(attachments)
    output.write_bytes(out)
    print(f"rfchar-import: {source} -> {output} ({len(vertices)} vertices, {len(indices)//3} triangles, {len(joints)} bones, {len(attachments)} attachments, RFM2 v14)")

def main():
    p=argparse.ArgumentParser();p.add_argument("input",type=Path);p.add_argument("output",type=Path);p.add_argument("--validator",type=Path,default=Path("build/glb-inspect"));a=p.parse_args()
    try:convert(a.input,a.output,a.validator)
    except (ValueError,OSError,subprocess.CalledProcessError) as e:p.exit(1,f"rfchar-import: {e}\n")
if __name__=="__main__":main()
