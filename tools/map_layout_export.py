#!/usr/bin/env python3
"""Export a Rasterfall .map as a compact top-down PNG and JSON sidecar."""
import argparse, json, math, struct, sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw
except ImportError as exc:
    raise SystemExit("map layout export requires Pillow; run: make setup-map-layout") from exc

BUTTONS={"button","button_air","button_alarm","button_heavy","button_fast","button_base1","button_base2","button_smoker","button_charger","button_tank","button_money","button_clear_hired","button_wave_skip","button_attack_x2","button_attack_x3","button_attack_x4","button_pose_reset","button_pose_right_arm","button_pose_arms","button_pose_body","button_anim_idle","button_anim_walk","button_anim_jog","button_glb_idle","button_glb_walk","button_glb_jog","button_vmd_walk","button_vmd_manjusaka","button_animation_composition","button_humanoid_pose_debug","button_west_corridor","button_west_corridor_no_tank","button_enemy_death_test"}
PREFIX={"safe":"SF","base":"B","spawn":"SP","ai_spawn":"SP","ramp":"R","platform":"P","prop":"PR","button":"BTN","air_wall":"AW","box":"BX","model":"MD"}
LAYOUT_RECORDS={"world","safe","base","spawn","ai_spawn","prop","ramp","platform","platform_roof","box","model"}|BUTTONS
def num(s):
    try:return int(s)
    except ValueError:return 0
def box(v): return {"min_x":v[0],"max_x":v[1],"min_z":v[2],"max_z":v[3]}
def centre(b): return {"x":(b["min_x"]+b["max_x"])/2,"z":(b["min_z"]+b["max_z"])/2}

def v1_fields(fields):
    return {item.split("=",1)[0]:item.split("=",1)[1] for item in fields if "=" in item}

def parse_v1(path):
    stat=path.stat(); doc={"schema":"rasterfall-map-layout-v1","source_map":str(path),"source_file":{"path":str(path),"size":stat.st_size,"mtime_ns":stat.st_mtime_ns},"coordinate_system":{"plane":"x/z","up":"y","unit":"RFU","rfu_per_meter":512,"note":"512 RFU = 1 m"},"world":None,"objects":[]}; counts={}; candidates=[]; warnings=[]
    for line_no,line in enumerate(path.read_text(encoding="utf-8").splitlines(),1):
        words=line.split("#",1)[0].split()
        if not words: continue
        kind, f = words[0], v1_fields(words[1:]); raw={"line":line_no,"record":kind,"fields":words[1:]}; typ=None; o=None
        if kind=="world":
            b={"min_x":num(f.get("min_x","0")),"max_x":num(f.get("max_x","0")),"min_z":num(f.get("min_z","0")),"max_z":num(f.get("max_z","0"))}; doc["world"]={**b,"room_limit":num(f.get("room_limit","0")),"source":raw}; continue
        if kind=="region":
            typ={"safe":"safe","spawn":"spawn","base":"base"}.get(f.get("kind"));
            if typ:
                b={"min_x":num(f.get("min_x","0")),"max_x":num(f.get("max_x","0")),"min_z":num(f.get("min_z","0")),"max_z":num(f.get("max_z","0"))}; o={"type":typ,"role":f.get("attr.role",f.get("role",f.get("id"))),"bounds":b,"center":centre(b)}
        elif kind=="actor_spawn":
            x,z=num(f.get("x","0")),num(f.get("z","0")); typ="ai_spawn"; o={"type":typ,"name":f.get("id",""),"base_id":num(f.get("base_id","0")),"class":f.get("class",f.get("type","")),"x":x,"z":z,"downed":bool(num(f.get("downed","1"))),"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind=="interaction":
            x,z,y=num(f.get("x","0")),num(f.get("z","0")),num(f.get("y","0")); typ="button"; o={"type":typ,"button_kind":"button_"+f.get("action","unknown"),"x":x,"z":z,"y":y,"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind=="object":
            x,z=num(f.get("x","0")),num(f.get("z","0")); typ="prop"; o={"type":typ,"asset":f.get("kind",""),"x":x,"z":z,"yaw_degrees":num(f.get("yaw","0")),"scale_milli":num(f.get("scale","1000")),"bounds":box([x,x,z,z]),"center":{"x":x,"z":z}}
        elif kind=="render":
            typ={"model":"model","box":"box"}.get(f.get("kind"),"box"); b={"min_x":num(f.get("min_x","0")),"max_x":num(f.get("max_x","0")),"min_z":num(f.get("min_z","0")),"max_z":num(f.get("max_z","0"))}; o={"type":typ,"style":num(f.get("attr.style","0")),"color":f.get("color","000000"),"height":num(f.get("height","0")),"bounds":b,"center":centre(b)}
        elif kind=="surface":
            typ={"ramp":"ramp","platform":"platform","platform_roof":"platform"}.get(f.get("kind"));
            if typ:
                b={"min_x":num(f.get("min_x","0")),"max_x":num(f.get("max_x","0")),"min_z":num(f.get("min_z","0")),"max_z":num(f.get("max_z","0"))}; o={"type":typ,"record":f.get("kind"),"bounds":b,"center":centre(b),"height_fields":[num(f.get("height","0")),num(f.get("height2",f.get("height","0")))]}
                if f.get("axis"): o["axis"]=f["axis"]
        elif kind=="collision":
            b={"min_x":num(f.get("min_x","0")),"max_x":num(f.get("max_x","0")),"min_z":num(f.get("min_z","0")),"max_z":num(f.get("max_z","0"))}; visible=f.get("visible","true")=="true"; collision=f.get("collision","true")=="true"; typ="air_wall" if collision and not visible else "box"; o={"type":typ,"height":num(f.get("height","0")),"color":f.get("color","000000"),"visible":visible,"collision":collision,"walkable":f.get("walkable","false")=="true","role":f.get("role"),"bounds":b,"center":centre(b)}
        if o:
            family=PREFIX[typ]; counts[family]=counts.get(family,0)+1; o["export_id"]=family+str(counts[family]); o["source"]=raw; doc["objects"].append(o)
            if typ=="box" and o.get("collision"): candidates.append((len(doc["objects"])-1,(o["bounds"]["max_x"]-o["bounds"]["min_x"])*(o["bounds"]["max_z"]-o["bounds"]["min_z"]),bool(o.get("role"))))
        elif kind not in {"map","world","region","interaction","actor_spawn","pickup","object","render","surface","collision"}:
            warnings.append(f"line {line_no}: ignored record '{kind}' (not represented in layout JSON)")
    if not doc["world"]: raise ValueError("map has no valid world record")
    chosen={i for i,_,role in candidates if role}
    for i,_,_ in sorted(candidates,key=lambda x:x[1],reverse=True):
        if len(chosen)>=8: break
        chosen.add(i)
    bx=0
    for i,_,_ in candidates:
        if i in chosen: bx+=1; doc["objects"][i].update(export_id=f"BX{bx}",key_box=True)
        else: doc["objects"][i].update(export_id=None,key_box=False)
    if warnings:
        print(f"map layout: {len(warnings)} record(s) are not represented:",file=sys.stderr)
        for warning in warnings: print(f"  warning: {warning}",file=sys.stderr)
    return doc

def parse(path):
    if any(line.split("#",1)[0].strip().startswith("map version=1") for line in path.read_text(encoding="utf-8").splitlines()):
        return parse_v1(path)
    stat=path.stat()
    doc={"schema":"rasterfall-map-layout-v1","source_map":str(path),"source_file":{"path":str(path),"size":stat.st_size,"mtime_ns":stat.st_mtime_ns},"coordinate_system":{"plane":"x/z","up":"y","unit":"RFU","rfu_per_meter":512,"note":"512 RFU = 1 m"},"world":None,"objects":[]}; counts={}; candidates=[]; warnings=[]
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
        elif kind=="model" and len(f)>=8:
            b=box(list(map(num,f[:4]))); typ="model"; o={"type":typ,"style":num(f[7]),"y_min":num(f[4]),"y_max":num(f[5]),"color":f[6],"bounds":b,"center":centre(b)}
        elif kind in ("ramp","platform","platform_roof") and len(f)>=5:
            b=box(list(map(num,f[:4]))); typ="ramp" if kind=="ramp" else "platform"; o={"type":typ,"record":kind,"bounds":b,"center":centre(b),"height_fields":list(map(num,f[4:6] if kind=="ramp" else f[4:5]))}
            if kind=="ramp" and len(f)>6:o["axis"]=f[6]
        elif kind=="box" and len(f)>=6:
            b=box(list(map(num,f[:4]))); opts=f[6:]; visible="hidden" not in opts and "air" not in opts; collision="nocollision" not in opts; role=next((x[5:] for x in opts if x.startswith("role=")),None); typ="air_wall" if collision and not visible else "box"; o={"type":typ,"height":num(f[4]),"color":f[5],"visible":visible,"collision":collision,"walkable":"walkable" in opts,"role":role,"bounds":b,"center":centre(b)}
        if o:
            family=PREFIX[typ]; counts[family]=counts.get(family,0)+1; o["export_id"]=family+str(counts[family]); o["source"]=raw; doc["objects"].append(o)
            if typ=="box" and o["collision"]: candidates.append((len(doc["objects"])-1,(b["max_x"]-b["min_x"])*(b["max_z"]-b["min_z"]),bool(o["role"])))
        elif kind not in LAYOUT_RECORDS:
            warnings.append(f"line {line_no}: ignored record '{kind}' (not represented in layout JSON)")
    if not doc["world"]:raise ValueError("map has no valid world record")
    chosen={i for i,_,role in candidates if role}
    for i,_,_ in sorted(candidates,key=lambda x:x[1],reverse=True):
        if len(chosen)>=8:break
        chosen.add(i)
    bx=0
    for i,_,_ in candidates:
        if i in chosen: bx+=1; doc["objects"][i].update(export_id=f"BX{bx}",key_box=True)
        else: doc["objects"][i].update(export_id=None,key_box=False)
    if warnings:
        print(f"map layout: {len(warnings)} record(s) are not represented:", file=sys.stderr)
        for warning in warnings:
            print(f"  warning: {warning}", file=sys.stderr)
    return doc

FONT_PATH=Path(__file__).resolve().parents[1]/"rasterfall/assets/fonts/gb2312-16.rfh"

class GB2312Font:
    def __init__(self,path=FONT_PATH):
        self.data=path.read_bytes()
        magic,version,self.ascii_offset,self.gb_offset,rows,self.index_offset,count=struct.unpack_from("<8s6I",self.data)
        if magic!=b"RFHZK16\0" or version!=1 or rows!=87 or self.index_offset+count*8>len(self.data):
            raise ValueError(f"invalid Rasterfall GB2312 font: {path}")
    def glyph(self,ch):
        cp=ord(ch)
        if 0x20<=cp<=0x7e:
            off=self.ascii_offset+(cp-0x20)*16
            return 8,[self.data[off+y]<<8 for y in range(16)]
        try: encoded=ch.encode("gb2312")
        except UnicodeEncodeError:return 8,[0]*16
        if len(encoded)!=2 or not 0xa1<=encoded[0]<=0xf7:return 8,[0]*16
        slot=(encoded[0]-0xa1)*94+encoded[1]-0xa1;off=self.gb_offset+slot*32
        return 16,[struct.unpack_from(">H",self.data,off+y*2)[0] for y in range(16)]

class Canvas:
    def __init__(self,w,h):
        self.w=w;self.h=h;self.image=Image.new("RGB",(w,h),(17,21,27));self.draw=ImageDraw.Draw(self.image)
        self.font=GB2312Font()
    def pixel(self,x,y,c):
        if 0<=x<self.w and 0<=y<self.h:self.draw.point((x,y),fill=c)
    def line(self,x,y,u,v,c):
        self.draw.line((x,y,u,v),fill=c,width=1)
    def rect(self,x,y,u,v,fill,stroke):
        x,u=sorted((max(0,x),min(self.w-1,u)));y,v=sorted((max(0,y),min(self.h-1,v)))
        self.draw.rectangle((x,y,u,v),fill=fill,outline=stroke,width=1)
    def polygon(self,points,fill,stroke=None):
        if not points:return
        ys=[p[1] for p in points];y0=max(0,min(ys));y1=min(self.h-1,max(ys))
        for y in range(y0,y1+1):
            xs=[]
            for i,(x,a) in enumerate(points):
                u,b=points[(i+1)%len(points)]
                if a==b:continue
                if (a<=y<b) or (b<=y<a):xs.append(round(x+(y-a)*(u-x)/(b-a)))
            xs.sort()
            for i in range(0,len(xs)-1,2):
                for x in range(xs[i],xs[i+1]+1):self.pixel(x,y,fill)
        if stroke:
            for i,(x,y) in enumerate(points):
                u,v=points[(i+1)%len(points)];self.line(x,y,u,v,stroke)
    def star(self,cx,cy,r,fill,stroke):
        points=[]
        for i in range(10):
            a=-math.pi/2+i*math.pi/5;rr=r if i%2==0 else r*.45
            points.append((round(cx+math.cos(a)*rr),round(cy+math.sin(a)*rr)))
        self.polygon(points,fill,stroke)
    def text(self,x,y,s,c=(240,244,248),scale=2):
        x0=x
        for ch in str(s):
            if ch=="\n":x=x0;y+=16;continue
            width,rows=self.font.glyph(ch)
            for row,bits in enumerate(rows):
                for col in range(width):
                    if bits&(0x8000>>col):self.pixel(x+col,y+row,c)
            x+=width
    def save(self,path):
        self.image.save(path,"PNG",optimize=True)

def render(doc,path,w,h):
    c=Canvas(w,h);margin=70;legend=310;world=doc["world"];x0,x1,z0,z1=[world[k] for k in ("min_x","max_x","min_z","max_z")];pw=w-legend-margin*2;ph=h-margin*2;s=min(pw/(x1-x0),ph/(z1-z0));ox=margin+(pw-(x1-x0)*s)/2;oy=margin+(ph-(z1-z0)*s)/2
    def pt(x,z):return round(ox+(x-x0)*s),round(oy+(z1-z)*s)
    step=min([512,1024,2048,4096,5120,10240,20480],key=lambda v:abs(v-(x1-x0)/8))
    for x in range(math.ceil(x0/step)*step,x1+1,step):px,_=pt(x,z0);c.line(px,round(oy),px,round(oy+(z1-z0)*s),(43,49,58));c.text(px+2,h-margin+8,x,(130,143,155),1)
    for z in range(math.ceil(z0/step)*step,z1+1,step):_,py=pt(x0,z);c.line(round(ox),py,round(ox+(x1-x0)*s),py,(43,49,58));c.text(4,py-3,z,(130,143,155),1)
    # ``render kind=ground`` is a world substrate, not a gameplay collision
    # box.  Keep it visually quiet so the semantic regions remain legible.
    ground_fill=(35,41,49)
    ground_stroke=None
    pal={"box":((63,70,82),(158,169,184)),"air_wall":(None,(232,101,101)),"safe":((48,125,83),(110,231,159)),"base":((42,101,122),(84,205,235)),"spawn":((121,50,55),(240,108,108)),"ramp":((132,94,50),(244,177,91)),"platform":((74,80,127),(157,166,249)),"prop":((125,87,127),(232,160,238)),"button":((147,119,38),(255,220,94)),"ai_spawn":((77,117,146),(139,211,255)),"model":((67,88,99),(190,225,230))};order=list(pal)
    def is_ground(o):
        source=o.get("source",{})
        return o["type"]=="box" and source.get("record")=="render" and "kind=ground" in source.get("fields",[])
    placed=[]
    for o in sorted(doc["objects"],key=lambda x:order.index(x["type"])):
        b=o["bounds"];x,y=pt(b["min_x"],b["max_z"]);u,v=pt(b["max_x"],b["min_z"]);x,u=(x-3,u+3) if x==u else (x,u);y,v=(y-3,v+3) if y==v else (y,v);fill,stroke=pal[o["type"]]
        if is_ground(o): c.rect(x,y,u,v,ground_fill,ground_stroke)
        else: c.rect(x,y,u,v,fill,stroke)
        placed.append((o,x,y,u,v))
    # Collision and semantic overlays are deliberately drawn after filled
    # geometry so platforms/props cannot hide important map boundaries.
    for o,x,y,u,v in placed:
        if o["type"] in ("safe","spawn","base"):
            c.rect(x,y,u,v,None,pal[o["type"]][1])
        elif o["type"]=="air_wall":
            c.rect(x,y,u,v,None,(255,116,116));c.line(x,y,u,v,(255,116,116));c.line(x,v,u,y,(255,116,116))
        elif o["type"]=="box" and o.get("key_box"):
            c.rect(x,y,u,v,None,(244,244,244));c.line(x,y,u,v,(244,244,244));c.line(x,v,u,y,(244,244,244))
    # Keep the base region as a high-priority semantic anchor, even when it
    # overlaps actors, buttons, or dense test-area labels.  An actor named
    # BASE is an AI spawn, not a second base region; its type remains
    # ``ai_spawn`` and it must use the normal AI-spawn marker.
    for o,x,y,u,v in placed:
        if o["type"]=="base":
            px,py=pt(o["center"]["x"],o["center"]["z"])
            c.star(px,py,13,(255,193,54),(255,239,145))
    # Semantic areas win label space. Dense point clusters retain every ID in
    # JSON, while the PNG suppresses labels that would collide.
    priority={"safe":0,"base":1,"spawn":2,"ramp":3,"platform":4,"prop":5,"model":6,"button":7,"ai_spawn":8,"air_wall":9,"box":10}; occupied=[]
    for o,x,y,u,v in sorted(placed,key=lambda q:priority[q[0]["type"]]):
        label=o.get("export_id")
        if not label:continue
        tx=(x+u)//2-len(label)*4;ty=(y+v)//2-5;area=(tx-2,ty-2,tx+len(label)*8+2,ty+12)
        if any(not(area[2]<a[0] or area[0]>a[2] or area[3]<a[1] or area[1]>a[3]) for a in occupied):continue
        c.text(tx,ty,label);occupied.append(area)
    lx=w-legend+20;c.text(lx,30,"RASTERFALL 地图",(240,244,248),2);c.text(lx,58,"X/Z 俯视图",(160,175,190),2)
    counts={typ:sum(1 for o in doc["objects"] if o["type"]==typ) for typ in pal}
    groups=[
        ("区域 AREAS",[("安全区 SAFE","safe"),("刷怪区 SPAWN ZONE","spawn")]),
        ("角色 ACTORS",[("基地核心 BASE CORE","base"),("AI 出生点 AI SPAWN","ai_spawn")]),
        ("通行 TRAVERSAL",[("坡道 RAMP","ramp"),("平台 PLATFORM","platform")]),
        ("世界 WORLD",[("组件 COMPONENT","prop"),("模型展示 MODEL","model"),("空气墙 AIR WALL","air_wall"),("重点碰撞 KEY BOX","box")]),
        ("交互 INTERACTION",[("按钮 BUTTON","button")]),
    ]
    y=100
    for heading,entries in groups:
        c.text(lx,y,heading,(130,211,255),1);y+=25
        for name,typ in entries:
            yy=y;fill,stroke=pal[typ]
            if typ=="base":c.star(lx+14,yy+12,11,(255,193,54),(255,239,145))
            else:c.rect(lx,yy,lx+28,yy+22,fill,stroke)
            c.text(lx+40,yy+3,f"{name} ({counts.get(typ,0)})",(220,226,232),2);y+=32
        y+=13
    y+=14
    c.text(lx,y,"网格 GRID RFU",(160,175,190),2);c.text(lx,y+25,"512 RFU / 1 M",(160,175,190),2);c.text(lx,y+55,"北方 NORTH +Z",(160,175,190),2);c.line(lx+45,y+115,lx+45,y+75,(160,175,190));c.save(path)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("map",type=Path);p.add_argument("--output-dir",type=Path,default=Path("."));p.add_argument("--width",type=int,default=1400);p.add_argument("--height",type=int,default=1000);a=p.parse_args()
    if a.width<640 or a.height<480:p.error("image must be at least 640x480")
    d=parse(a.map);a.output_dir.mkdir(parents=True,exist_ok=True);render(d,a.output_dir/"output.png",a.width,a.height);(a.output_dir/"output.json").write_text(json.dumps(d,ensure_ascii=False,indent=2)+"\n",encoding="utf-8");print(f"exported {a.output_dir/'output.png'} and {a.output_dir/'output.json'} ({len(d['objects'])} objects; font={FONT_PATH})")
if __name__=="__main__":main()
