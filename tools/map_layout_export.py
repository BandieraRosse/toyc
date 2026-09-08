#!/usr/bin/env python3
"""Export a Rasterfall .map as a compact top-down PNG and JSON sidecar."""
import argparse, json, math, struct, zlib
from pathlib import Path

BUTTONS={"button","button_air","button_alarm","button_heavy","button_fast","button_base1","button_base2","button_smoker","button_charger","button_tank","button_money","button_clear_hired","button_wave_skip","button_attack_x2","button_attack_x3","button_attack_x4","button_pose_reset","button_pose_right_arm","button_pose_arms","button_pose_body","button_anim_idle","button_anim_walk","button_anim_jog","button_glb_idle","button_glb_walk","button_glb_jog","button_vmd_walk","button_vmd_manjusaka","button_animation_composition","button_humanoid_pose_debug","button_west_corridor","button_west_corridor_no_tank"}
PREFIX={"safe":"SF","base":"B","spawn":"SP","ai_spawn":"SP","ramp":"R","platform":"P","prop":"PR","button":"BTN","air_wall":"AW","box":"BX"}
FONT={"A":"010101111101101","B":"110101110101110","C":"011100100100011","D":"110101101101110","E":"111100110100111","F":"111100110100100","G":"011100101101011","H":"101101111101101","I":"111010010010111","J":"001001001101010","K":"101101110101101","L":"100100100100111","M":"101111111101101","N":"101111111111101","O":"010101101101010","P":"110101110100100","Q":"010101101111011","R":"110101110101101","S":"011100010001110","T":"111010010010010","U":"101101101101111","V":"101101101101010","W":"101101111111101","X":"101101010101101","Y":"101101010010010","Z":"111001010100111","0":"111101101101111","1":"010110010010111","2":"110001111100111","3":"110001111001110","4":"101101111001001","5":"111100110001110","6":"011100111101111","7":"111001010010010","8":"111101111101111","9":"111101111001110","-":"000000111000000",".":"000000000000010","/":"001001010100100"," ":"0"*15}
def num(s):
    try:return int(s)
    except ValueError:return 0
def box(v): return {"min_x":v[0],"max_x":v[1],"min_z":v[2],"max_z":v[3]}
def centre(b): return {"x":(b["min_x"]+b["max_x"])/2,"z":(b["min_z"]+b["max_z"])/2}

def parse(path):
    doc={"schema":"rasterfall-map-layout-v1","source_map":str(path),"coordinate_system":{"plane":"x/z","up":"y","unit":"RFU","rfu_per_meter":512,"note":"512 RFU = 1 m"},"world":None,"objects":[]}; counts={}; candidates=[]
    for line_no,line in enumerate(path.read_text(encoding="utf-8").splitlines(),1):
        words=line.split("#",1)[0].split()
        if not words:continue
        kind,f=words[0],words[1:]; raw={"line":line_no,"record":kind,"fields":f}; typ=None; o=None
        if kind=="world" and len(f)>=5:
            b=box(list(map(num,f[:4]))); doc["world"]={**b,"room_limit":num(f[4]),"source":raw}; continue
        if kind=="safe" and len(f)>=5:
            b=box(list(map(num,f[:4]))); typ="safe"; o={"type":typ,"role":f[4],"bounds":b,"center":centre(b)}
        elif kind=="base" and len(f)>=5:
            b=box(list(map(num,f[1:5]))); typ="base"; o={"type":typ,"map_id":num(f[0]),"bounds":b,"center":centre(b)}
        elif kind=="spawn" and len(f)>=4:
            b=box(list(map(num,f[:4]))); typ="spawn"; o={"type":typ,"color":f[4] if len(f)>4 else None,"bounds":b,"center":centre(b)}
        elif kind=="ai_spawn" and len(f)>=5:
            x,z=num(f[3]),num(f[4]); typ="ai_spawn"; o={"type":typ,"name":f[0],"base_id":num(f[1]),"class":f[2],"x":x,"z":z,"downed":bool(num(f[5])) if len(f)>5 else True,"weapon":f[6] if len(f)>6 else None,"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind in BUTTONS and len(f)>=3:
            x,z,y=map(num,f[:3]); typ="button"; o={"type":typ,"button_kind":kind,"x":x,"z":z,"y":y,"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind=="prop" and len(f)>=5:
            x,z=num(f[1]),num(f[2]); typ="prop"; o={"type":typ,"asset":f[0],"x":x,"z":z,"yaw_degrees":num(f[3]),"scale_milli":num(f[4]),"options":f[5:],"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind in ("ramp","platform","platform_roof") and len(f)>=5:
            b=box(list(map(num,f[:4]))); typ="ramp" if kind=="ramp" else "platform"; o={"type":typ,"record":kind,"bounds":b,"center":centre(b),"height_fields":list(map(num,f[4:6] if kind=="ramp" else f[4:5]))}
            if kind=="ramp" and len(f)>6:o["axis"]=f[6]
        elif kind=="box" and len(f)>=6:
            b=box(list(map(num,f[:4]))); opts=f[6:]; visible="hidden" not in opts and "air" not in opts; collision="nocollision" not in opts; role=next((x[5:] for x in opts if x.startswith("role=")),None); typ="air_wall" if collision and not visible else "box"; o={"type":typ,"height":num(f[4]),"color":f[5],"visible":visible,"collision":collision,"walkable":"walkable" in opts,"role":role,"bounds":b,"center":centre(b)}
        if o:
            family=PREFIX[typ]; counts[family]=counts.get(family,0)+1; o["export_id"]=family+str(counts[family]); o["source"]=raw; doc["objects"].append(o)
            if typ=="box" and o["collision"]: candidates.append((len(doc["objects"])-1,(b["max_x"]-b["min_x"])*(b["max_z"]-b["min_z"]),bool(o["role"])))
    if not doc["world"]:raise ValueError("map has no valid world record")
    chosen={i for i,_,role in candidates if role}
    for i,_,_ in sorted(candidates,key=lambda x:x[1],reverse=True):
        if len(chosen)>=8:break
        chosen.add(i)
    bx=0
    for i,_,_ in candidates:
        if i in chosen: bx+=1; doc["objects"][i].update(export_id=f"BX{bx}",key_box=True)
        else: doc["objects"][i].update(export_id=None,key_box=False)
    return doc

class Canvas:
    def __init__(self,w,h):self.w=w;self.h=h;self.p=bytearray((25,29,35)*(w*h))
    def pixel(self,x,y,c):
        if 0<=x<self.w and 0<=y<self.h:i=(y*self.w+x)*3;self.p[i:i+3]=bytes(c)
    def line(self,x,y,u,v,c):
        dx=abs(u-x);sx=1 if x<u else -1;dy=-abs(v-y);sy=1 if y<v else -1;e=dx+dy
        while True:
            self.pixel(x,y,c)
            if x==u and y==v:break
            q=2*e
            if q>=dy:e+=dy;x+=sx
            if q<=dx:e+=dx;y+=sy
    def rect(self,x,y,u,v,fill,stroke):
        x,u=sorted((max(0,x),min(self.w-1,u)));y,v=sorted((max(0,y),min(self.h-1,v)))
        if fill:
            for yy in range(y,v+1):i=(yy*self.w+x)*3;self.p[i:i+(u-x+1)*3]=bytes(fill)*(u-x+1)
        self.line(x,y,u,y,stroke);self.line(x,v,u,v,stroke);self.line(x,y,x,v,stroke);self.line(u,y,u,v,stroke)
    def text(self,x,y,s,c=(240,244,248),scale=2):
        for ch in str(s).upper():
            for j,b in enumerate(FONT.get(ch,FONT[" "])):
                if b=="1":
                    for yy in range(scale):
                        for xx in range(scale):self.pixel(x+(j%3)*scale+xx,y+(j//3)*scale+yy,c)
            x+=4*scale
    def save(self,path):
        raw=b"".join(b"\0"+self.p[y*self.w*3:(y+1)*self.w*3] for y in range(self.h))
        def chunk(t,d):return struct.pack(">I",len(d))+t+d+struct.pack(">I",zlib.crc32(t+d)&0xffffffff)
        path.write_bytes(b"\x89PNG\r\n\x1a\n"+chunk(b"IHDR",struct.pack(">IIBBBBB",self.w,self.h,8,2,0,0,0))+chunk(b"IDAT",zlib.compress(raw,9))+chunk(b"IEND",b""))

def render(doc,path,w,h):
    c=Canvas(w,h);margin=70;legend=250;world=doc["world"];x0,x1,z0,z1=[world[k] for k in ("min_x","max_x","min_z","max_z")];pw=w-legend-margin*2;ph=h-margin*2;s=min(pw/(x1-x0),ph/(z1-z0));ox=margin+(pw-(x1-x0)*s)/2;oy=margin+(ph-(z1-z0)*s)/2
    def pt(x,z):return round(ox+(x-x0)*s),round(oy+(z1-z)*s)
    step=min([512,1024,2048,4096,5120,10240,20480],key=lambda v:abs(v-(x1-x0)/8))
    for x in range(math.ceil(x0/step)*step,x1+1,step):px,_=pt(x,z0);c.line(px,round(oy),px,round(oy+(z1-z0)*s),(54,61,70));c.text(px+2,h-margin+8,x,(130,143,155),1)
    for z in range(math.ceil(z0/step)*step,z1+1,step):_,py=pt(x0,z);c.line(round(ox),py,round(ox+(x1-x0)*s),py,(54,61,70));c.text(4,py-3,z,(130,143,155),1)
    pal={"box":((83,91,104),(172,181,193)),"air_wall":(None,(232,101,101)),"safe":((48,125,83),(110,231,159)),"base":((42,101,122),(84,205,235)),"spawn":((121,50,55),(240,108,108)),"ramp":((132,94,50),(244,177,91)),"platform":((74,80,127),(157,166,249)),"prop":((125,87,127),(232,160,238)),"button":((147,119,38),(255,220,94)),"ai_spawn":((77,117,146),(139,211,255))};order=list(pal)
    placed=[]
    for o in sorted(doc["objects"],key=lambda x:order.index(x["type"])):
        b=o["bounds"];x,y=pt(b["min_x"],b["max_z"]);u,v=pt(b["max_x"],b["min_z"]);x,u=(x-3,u+3) if x==u else (x,u);y,v=(y-3,v+3) if y==v else (y,v);fill,stroke=pal[o["type"]];c.rect(x,y,u,v,fill,stroke)
        if o["type"]=="air_wall":c.line(x,y,u,v,stroke);c.line(x,v,u,y,stroke)
        placed.append((o,x,y,u,v))
    # Semantic areas win label space. Dense point clusters retain every ID in
    # JSON, while the PNG suppresses labels that would collide.
    priority={"safe":0,"base":1,"spawn":2,"ramp":3,"platform":4,"prop":5,"button":6,"ai_spawn":7,"air_wall":8,"box":9}; occupied=[]
    for o,x,y,u,v in sorted(placed,key=lambda q:priority[q[0]["type"]]):
        label=o.get("export_id")
        if not label:continue
        tx=(x+u)//2-len(label)*4;ty=(y+v)//2-5;area=(tx-2,ty-2,tx+len(label)*8+2,ty+12)
        if any(not(area[2]<a[0] or area[0]>a[2] or area[3]<a[1] or area[1]>a[3]) for a in occupied):continue
        c.text(tx,ty,label);occupied.append(area)
    lx=w-legend+20;c.text(lx,35,"RASTERFALL MAP");c.text(lx,60,"X/Z TOP DOWN",(160,175,190),1)
    for i,(name,typ) in enumerate([("SAFE","safe"),("BASE","base"),("SPAWN/AI","spawn"),("RAMP","ramp"),("PLATFORM","platform"),("PROP","prop"),("BUTTON","button"),("AIR WALL","air_wall"),("KEY BOX","box")]):
        y=100+i*38;fill,stroke=pal[typ];c.rect(lx,y,lx+24,y+18,fill,stroke);c.text(lx+34,y+5,name,(220,226,232),1)
    c.text(lx,465,"GRID RFU",(160,175,190),1);c.text(lx,485,"512 RFU / 1 M",(160,175,190),1);c.text(lx,520,"NORTH +Z",(160,175,190),1);c.line(lx+35,570,lx+35,540,(160,175,190));c.save(path)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("map",type=Path);p.add_argument("--output-dir",type=Path,default=Path("."));p.add_argument("--width",type=int,default=1400);p.add_argument("--height",type=int,default=1000);a=p.parse_args()
    if a.width<640 or a.height<480:p.error("image must be at least 640x480")
    d=parse(a.map);a.output_dir.mkdir(parents=True,exist_ok=True);render(d,a.output_dir/"output.png",a.width,a.height);(a.output_dir/"output.json").write_text(json.dumps(d,ensure_ascii=False,indent=2)+"\n",encoding="utf-8");print(f"exported {a.output_dir/'output.png'} and {a.output_dir/'output.json'} ({len(d['objects'])} objects)")
if __name__=="__main__":main()
