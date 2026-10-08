#!/usr/bin/env python3
"""Emit a standard experiment composite as a V1 map fragment (UTF-8)."""
import argparse
import re
from pathlib import Path
from lab_computer import generate as computer
from building_kit import BuildingKit

PALETTE = {"model": ("9FB4FF", 1), "animation": ("79E8C5", 2),
           "lighting": ("FFD283", 3), "performance": ("FFAB78", 4)}

def site_finish(name, width, depth, category, plot_width=16384, plot_depth=10240,
                plot_offset_x=0, projection_beams=True):
    """Finish the setback with ground support independent of the paint."""
    hx, hz = width // 2, depth // 2
    left, right = plot_offset_x-plot_width//2, plot_offset_x+plot_width//2
    south, north = -plot_depth//2, plot_depth//2
    if left > -hx or right < hx or south > -hz or north < hz:
        raise ValueError("working area must fit inside the planning plot")
    color, icon = PALETTE[category]
    # One invisible support spans the plot, including paint/entry seams.
    # Working-area walls, safety policy and visible floor remain independent.
    bounds = f'min_x={left} max_x={right} min_z={south} max_z={north}'
    records = [
        f'surface id={name}_site_ground kind=ground {bounds} height=0 material=78858A attr.collision_id={name}_site_ground_col attr.lab={name}',
        f'collision id={name}_site_ground_col shape=flat {bounds} height=0 collision=false visible=false walkable=true color=78858A attr.lab={name}',
    ]
    for side, (a,b,c,d) in zip(('w','e','s','n'),
            ((left,-hx,south,north),(hx,right,south,north),
             (-hx,hx,south,-hz),(-hx,hx,hz,north))):
        if a == b or c == d:
            continue
        records.append(f'render id={name}_fill_{side} kind=floor min_x={a} max_x={b} min_z={c} max_z={d} height=0 color=78858A attr.style=12 attr.lab={name}')
    # Low optical plinths just outside the working boundary; never in the road.
    # Shared corner-beacon geometry supplies housing and crossed projections.
    for side, x, z in (('nw',-hx-220,hz-180),('ne',hx+220,hz-180),
                       ('sw',-hx-220,-hz+180),('se',hx+220,-hz+180)):
        if x-180 < left or x+180 > right:
            raise ValueError("plot needs 400 RFU side clearance for corner beacons")
        optical = '' if projection_beams else ' attr.projection_beams=0'
        records.append(f'render id={name}_beacon_{side} kind=sign min_x={x-180} max_x={x+180} min_z={z-140} max_z={z+140} height=-896 color={color} attr.height2=-52 attr.style=7{optical} attr.lab={name}')
        records.append(f'collision id={name}_beacon_{side}_col shape=box min_x={x-180} max_x={x+180} min_z={z-120} max_z={z+120} height=292 collision=true visible=false walkable=true color=526874 attr.lab={name}')
    return records

def generate(name, x, z, category, enclosure, width=10240, depth=9216,
             plot_width=16384, plot_depth=10240, projection_beams=True, entry_gap=1536):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9.-]{0,35}", name):
        raise ValueError("id must be a stable map name of at most 36 characters")
    if width < 2048 or depth < 2048 or width % 2 or depth % 2:
        raise ValueError("width/depth must be even and at least 2048 RFU")
    if max(width, depth, abs(x), abs(z)) > 1000000:
        raise ValueError("dimensions/origin exceed the map lab range")
    if plot_width % 2 or plot_depth % 2 or max(plot_width,plot_depth)>1000000:
        raise ValueError("plot dimensions must be even and within the map range")
    hx, hz = width // 2, depth // 2
    if entry_gap < 0 or entry_gap % 2 or entry_gap > width-256:
        raise ValueError("entry gap must be even and fit between the side walls")
    color, icon = PALETTE[category]
    lab = name + "_area"
    records = [f"lab id={lab} x={x} z={z} width={width} depth={depth} category={category} enclosure={enclosure}"]
    def add(record, suffix, **fields):
        records.append(f"{record} id={name}{suffix} " + " ".join(f"{k}={v}" for k, v in fields.items()) + f" attr.lab={lab}")
    bounds = dict(min_x=-hx, max_x=hx, min_z=-hz, max_z=hz)
    add("surface", "", kind="ground", **bounds, height=0, material="52616A", **{"attr.collision_id": name+"_col"})
    add("collision", "_col", shape="flat", **bounds, height=0, collision="false", visible="true", walkable="true", color="52616A")
    add("render", "_paint", kind="floor", min_x=-hx+64, max_x=hx-64, min_z=-hz+64, max_z=hz-64, height=0, color="607985", **{"attr.style":10})
    strips = [(-hx,hx,hz-64,hz),(-hx,hx,-hz,-hz+64),(-hx,-hx+64,-hz+64,hz-64),(hx-64,hx,-hz+64,hz-64)]
    for side, b in zip(("north","south","west","east"), strips):
        add("render", "_"+side+"_line", kind="floor", **dict(zip(bounds,b)), height=0, color=color)
    # A zero entry gap closes the perimeter, including matching collision.
    if enclosure == "walled":
        walls = [(-hx,-hx+128,-hz,hz),(hx-128,hx,-hz,hz),(-hx+128,hx-128,-hz,-hz+128)]
        if entry_gap:
            walls.extend([(-hx+128,-entry_gap//2,hz-128,hz),
                          (entry_gap//2,hx-128,hz-128,hz)])
        else:
            walls.append((-hx+128,hx-128,hz-128,hz))
        for i,b in enumerate(walls):
            a,c,d,e=b
            glass=[]
            axis,at,start,end=("z",(a+c)//2,d,e) if c-a==128 else ("x",(d+e)//2,a,c)
            BuildingKit(glass).window(f"{name}_wall_{i}",axis,at,start,end,
                                      0,512,depth=128,walk=True)
            records.extend(line+f" attr.lab={lab}" for line in glass)
    elif enclosure == "backdrop":
        add("render", "_backdrop", kind="wall", min_x=-hx+64, max_x=hx-64, min_z=-hz+64, max_z=-hz+64, height=4400, color="65717D")
    bx, bz = hx-768, hz-768
    records.extend(computer(name+"_button",bx,bz,name,color,lab).splitlines())
    def panel(suffix, left, right, at_z, bottom, top, style, text, dynamic=False):
        attrs={"attr.height2":top,"attr.style":style,"attr.text":text,"attr.facing":"+z","attr.texture_u":icon}
        if not projection_beams: attrs["attr.projection_beams"]=0
        if dynamic: attrs["attr.channel"]=name
        add("render", suffix, kind="sign", min_x=left, max_x=right, min_z=at_z, max_z=at_z+20, height=bottom, color=color, **attrs)
    half=min(2400,hx-128)
    panel("_title",-half,half,-hz+400,500,1100,2,category.upper()+"_LAB")
    panel("_display",-half,half,hz-330,900,1500,4,"EXHIBIT_OFF",True)
    panel("_sample_info",-min(900,hx-128),min(900,hx-128),-800,-700,-450,3,"SAMPLE_IDLE")
    records.extend(site_finish(lab,width,depth,category,plot_width,plot_depth,
                               projection_beams=projection_beams))
    records.append("# Sample origin is local (0, 0); add local records with attr.lab="+lab)
    return "\n".join(records)+"\n"

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("id");p.add_argument("--x",type=int,required=True);p.add_argument("--z",type=int,required=True)
    p.add_argument("--category",choices=PALETTE,required=True)
    p.add_argument("--enclosure",choices=("open","backdrop","walled"),default="open")
    p.add_argument("--entry-gap",type=int,default=1536,help="north opening in RFU; 0 closes a walled perimeter")
    p.add_argument("--width",type=int,default=10240);p.add_argument("--depth",type=int,default=9216)
    p.add_argument("--plot-width",type=int,default=16384,help="planning plot width in RFU")
    p.add_argument("--plot-depth",type=int,default=10240,help="planning plot depth in RFU")
    p.add_argument("--projection-beams",choices=("on","off"),default="on",
                   help="title/display/beacon scattering beams; off retains housings, signs and backdrops")
    p.add_argument("--output",type=Path,required=True,help="new fragment file; existing files are refused")
    args=p.parse_args()
    try: text=generate(args.id,args.x,args.z,args.category,args.enclosure,args.width,args.depth,args.plot_width,args.plot_depth,
                       projection_beams=args.projection_beams=="on",entry_gap=args.entry_gap)
    except ValueError as error: p.error(str(error))
    with args.output.open("x",encoding="utf-8",newline="\n") as f: f.write(text)
    print(f"Created {args.output}; register the control/content producer in rf_experiment_labs.inc.")

if __name__ == "__main__": main()
