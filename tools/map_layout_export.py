#!/usr/bin/env python3
"""Export a Rasterfall .map as a compact top-down PNG and JSON sidecar."""
import argparse, json, math, os, subprocess
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError as exc:
    raise SystemExit("map layout export requires Pillow; run: make setup-map-layout") from exc

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

def find_font(requested=None):
    candidates=[]
    if requested:candidates.append(Path(requested))
    env_font=os.environ.get("RASTERFALL_MAP_FONT")
    if env_font:candidates.append(Path(env_font))
    try:
        match=subprocess.run(["fc-match","-f","%{family}|%{file}","Noto Sans CJK SC"],capture_output=True,text=True,check=False).stdout.strip()
        family,file_path=(match.split("|",1) if "|" in match else ("", ""))
        if "Noto Sans CJK" in family and file_path:candidates.append(Path(file_path))
    except OSError:
        pass
    candidates += [
        Path(__file__).resolve().parent/".map-layout-fonts/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
        Path("/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc"),
        Path("/usr/share/fonts/truetype/noto/NotoSansCJK-Regular.ttc"),
    ]
    for candidate in candidates:
        if candidate.is_file():return candidate
    raise SystemExit("Noto Sans CJK font not found; run: make setup-map-layout or pass --font PATH")

class Canvas:
    def __init__(self,w,h,font_path):
        self.w=w;self.h=h;self.image=Image.new("RGB",(w,h),(25,29,35));self.draw=ImageDraw.Draw(self.image)
        self.font_path=font_path;self.fonts={}
    def font(self,scale):
        size=14 if scale<=1 else 20
        if size not in self.fonts:self.fonts[size]=ImageFont.truetype(str(self.font_path),size)
        return self.fonts[size]
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
        self.draw.text((x,y),str(s),font=self.font(scale),fill=c,stroke_width=0)
    def save(self,path):
        self.image.save(path,"PNG",optimize=True)

def render(doc,path,w,h,font_path):
    c=Canvas(w,h,font_path);margin=70;legend=310;world=doc["world"];x0,x1,z0,z1=[world[k] for k in ("min_x","max_x","min_z","max_z")];pw=w-legend-margin*2;ph=h-margin*2;s=min(pw/(x1-x0),ph/(z1-z0));ox=margin+(pw-(x1-x0)*s)/2;oy=margin+(ph-(z1-z0)*s)/2
    def pt(x,z):return round(ox+(x-x0)*s),round(oy+(z1-z)*s)
    step=min([512,1024,2048,4096,5120,10240,20480],key=lambda v:abs(v-(x1-x0)/8))
    for x in range(math.ceil(x0/step)*step,x1+1,step):px,_=pt(x,z0);c.line(px,round(oy),px,round(oy+(z1-z0)*s),(54,61,70));c.text(px+2,h-margin+8,x,(130,143,155),1)
    for z in range(math.ceil(z0/step)*step,z1+1,step):_,py=pt(x0,z);c.line(round(ox),py,round(ox+(x1-x0)*s),py,(54,61,70));c.text(4,py-3,z,(130,143,155),1)
    pal={"box":((83,91,104),(172,181,193)),"air_wall":(None,(232,101,101)),"safe":((48,125,83),(110,231,159)),"base":((42,101,122),(84,205,235)),"spawn":((121,50,55),(240,108,108)),"ramp":((132,94,50),(244,177,91)),"platform":((74,80,127),(157,166,249)),"prop":((125,87,127),(232,160,238)),"button":((147,119,38),(255,220,94)),"ai_spawn":((77,117,146),(139,211,255))};order=list(pal)
    placed=[]
    for o in sorted(doc["objects"],key=lambda x:order.index(x["type"])):
        b=o["bounds"];x,y=pt(b["min_x"],b["max_z"]);u,v=pt(b["max_x"],b["min_z"]);x,u=(x-3,u+3) if x==u else (x,u);y,v=(y-3,v+3) if y==v else (y,v);fill,stroke=pal[o["type"]];c.rect(x,y,u,v,fill,stroke)
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
    # Keep the base as a high-priority semantic anchor, even when it overlaps
    # actors, buttons, or dense test-area labels.
    for o,x,y,u,v in placed:
        if o["type"]=="base" or (o["type"]=="ai_spawn" and o.get("name")=="BASE"):
            px,py=pt(o["center"]["x"],o["center"]["z"])
            c.star(px,py,13,(255,193,54),(255,239,145))
    # Semantic areas win label space. Dense point clusters retain every ID in
    # JSON, while the PNG suppresses labels that would collide.
    priority={"safe":0,"base":1,"spawn":2,"ramp":3,"platform":4,"prop":5,"button":6,"ai_spawn":7,"air_wall":8,"box":9}; occupied=[]
    for o,x,y,u,v in sorted(placed,key=lambda q:priority[q[0]["type"]]):
        label=o.get("export_id")
        if not label:continue
        tx=(x+u)//2-len(label)*4;ty=(y+v)//2-5;area=(tx-2,ty-2,tx+len(label)*8+2,ty+12)
        if any(not(area[2]<a[0] or area[0]>a[2] or area[3]<a[1] or area[1]>a[3]) for a in occupied):continue
        c.text(tx,ty,label);occupied.append(area)
    lx=w-legend+20;c.text(lx,30,"RASTERFALL 地图",(240,244,248),2);c.text(lx,58,"X/Z 俯视图",(160,175,190),2)
    counts={typ:sum(1 for o in doc["objects"] if o["type"]==typ) for typ in pal}
    counts["base"]+=sum(1 for o in doc["objects"] if o["type"]=="ai_spawn" and o.get("name")=="BASE")
    groups=[
        ("区域 AREAS",[("安全区 SAFE","safe"),("刷怪区 SPAWN ZONE","spawn")]),
        ("角色 ACTORS",[("基地核心 BASE CORE","base"),("AI 出生点 AI SPAWN","ai_spawn")]),
        ("通行 TRAVERSAL",[("坡道 RAMP","ramp"),("平台 PLATFORM","platform")]),
        ("世界 WORLD",[("组件 COMPONENT","prop"),("空气墙 AIR WALL","air_wall"),("重点碰撞 KEY BOX","box")]),
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
    p=argparse.ArgumentParser(description=__doc__);p.add_argument("map",type=Path);p.add_argument("--output-dir",type=Path,default=Path("."));p.add_argument("--width",type=int,default=1400);p.add_argument("--height",type=int,default=1000);p.add_argument("--font",type=Path,default=None,help="CJK-capable TTF/TTC font; defaults to Noto Sans CJK SC");a=p.parse_args()
    if a.width<640 or a.height<480:p.error("image must be at least 640x480")
    d=parse(a.map);a.output_dir.mkdir(parents=True,exist_ok=True);font_path=find_font(a.font);render(d,a.output_dir/"output.png",a.width,a.height,font_path);(a.output_dir/"output.json").write_text(json.dumps(d,ensure_ascii=False,indent=2)+"\n",encoding="utf-8");print(f"exported {a.output_dir/'output.png'} and {a.output_dir/'output.json'} ({len(d['objects'])} objects; font={font_path})")
if __name__=="__main__":main()
