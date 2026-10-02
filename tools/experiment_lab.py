#!/usr/bin/env python3
"""Emit a standard experiment composite as a V1 map fragment (UTF-8)."""
import argparse
import re
from pathlib import Path
from lab_computer import generate as computer

PALETTE = {"model": ("9FB4FF", 1), "animation": ("79E8C5", 2),
           "lighting": ("FFD283", 3), "performance": ("FFAB78", 4)}

def generate(name, x, z, category, enclosure, width=10240, depth=9216):
    if not re.fullmatch(r"[A-Za-z_][A-Za-z_0-9.-]{0,35}", name):
        raise ValueError("id must be a stable map name of at most 36 characters")
    if width < 2048 or depth < 2048 or width % 2 or depth % 2:
        raise ValueError("width/depth must be even and at least 2048 RFU")
    if max(width, depth, abs(x), abs(z)) > 1000000:
        raise ValueError("dimensions/origin exceed the map lab range")
    hx, hz = width // 2, depth // 2
    color, icon = PALETTE[category]
    lab = name + "_area"
    records = [f"lab id={lab} x={x} z={z} width={width} depth={depth} category={category} enclosure={enclosure}"]
    def add(record, suffix, **fields):
        records.append(f"{record} id={name}{suffix} " + " ".join(f"{k}={v}" for k, v in fields.items()) + f" attr.lab={lab}")
    bounds = dict(min_x=-hx, max_x=hx, min_z=-hz, max_z=hz)
    add("surface", "", kind="ground", **bounds, height=0, material="52616A", **{"attr.collision_id": name+"_col"})
    add("collision", "_col", shape="flat", **bounds, height=0, collision="false", visible="true", walkable="true", color="52616A")
    add("render", "_paint", kind="floor", min_x=-hx+64, max_x=hx-64, min_z=-hz+64, max_z=hz-64, height=0, color="52616A")
    strips = [(-hx,hx,hz-64,hz),(-hx,hx,-hz,-hz+64),(-hx,-hx+64,-hz+64,hz-64),(hx-64,hx,-hz+64,hz-64)]
    for side, b in zip(("north","south","west","east"), strips):
        add("render", "_"+side+"_line", kind="floor", **dict(zip(bounds,b)), height=0, color=color)
    # North entry gap stays open for physical enclosures.
    if enclosure == "walled":
        walls = [(-hx,-hx+128,-hz,hz),(hx-128,hx,-hz,hz),(-hx+128,hx-128,-hz,-hz+128),
                 (-hx+128,-768,hz-128,hz),(768,hx-128,hz-128,hz)]
        for i,b in enumerate(walls):
            add("collision", f"_wall_{i}", shape="box", **dict(zip(bounds,b)), height=650, visible="true", collision="true", color="6B8E92")
    elif enclosure == "backdrop":
        add("render", "_backdrop", kind="wall", min_x=-hx+64, max_x=hx-64, min_z=-hz+64, max_z=-hz+64, height=4400, color="65717D")
    bx, bz = hx-768, hz-768
    records.extend(computer(name+"_button",bx,bz,name,color,lab).splitlines())
    def panel(suffix, left, right, at_z, bottom, top, style, text, dynamic=False):
        attrs={"attr.height2":top,"attr.style":style,"attr.text":text,"attr.facing":"+z","attr.texture_u":icon}
        if dynamic: attrs["attr.channel"]=name
        add("render", suffix, kind="sign", min_x=left, max_x=right, min_z=at_z, max_z=at_z+20, height=bottom, color=color, **attrs)
    half=min(2400,hx-128)
    panel("_title",-half,half,-hz+400,500,1100,2,category.upper()+"_LAB")
    panel("_display",-half,half,hz-330,900,1500,4,"EXHIBIT_OFF",True)
    panel("_sample_info",-min(900,hx-128),min(900,hx-128),-800,-700,-450,3,"SAMPLE_IDLE")
    records.append("# Sample origin is local (0, 0); add local records with attr.lab="+lab)
    return "\n".join(records)+"\n"

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument("id");p.add_argument("--x",type=int,required=True);p.add_argument("--z",type=int,required=True)
    p.add_argument("--category",choices=PALETTE,required=True)
    p.add_argument("--enclosure",choices=("open","backdrop","walled"),default="open")
    p.add_argument("--width",type=int,default=10240);p.add_argument("--depth",type=int,default=9216)
    p.add_argument("--output",type=Path,required=True,help="new fragment file; existing files are refused")
    args=p.parse_args()
    try: text=generate(args.id,args.x,args.z,args.category,args.enclosure,args.width,args.depth)
    except ValueError as error: p.error(str(error))
    with args.output.open("x",encoding="utf-8",newline="\n") as f: f.write(text)
    print(f"Created {args.output}; register the control/content producer in rf_experiment_labs.inc.")

if __name__ == "__main__": main()
