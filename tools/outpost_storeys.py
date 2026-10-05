#!/usr/bin/env python3
"""Author the outpost's B1 / 1F / 2F / roof and north switchback stair.

The existing map remains the source for ground-floor furniture and the park.
Maintains the marked structure, named ground-floor connections and safe regions.
"""
from pathlib import Path
import argparse

BEGIN = "# BEGIN OUTPOST STOREYS"
END = "# END OUTPOST STOREYS"


def geometry():
    lines = [BEGIN, "# Signed heights are relative to the existing outdoor ground."]
    add = lines.append

    def bounds(x0, x1, z0, z1):
        return f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"

    def solid(name, x0, x1, z0, z1, bottom, top, color, walk=False):
        b = bounds(x0, x1, z0, z1)
        add(f"collision id={name}_col shape=box {b} height={top} attr.base_y={bottom} collision=true visible=false walkable={str(walk).lower()}")
        add(f"render id={name} kind=box {b} height={top} attr.base_y={bottom} color={color}")
        if walk:
            add(f"surface id={name}_surface kind=platform {b} height={top} material={color} attr.collision_id={name}_col")

    def sign(name, x, z, y, text):
        add(f"render id={name} kind=sign min_x={x-600} max_x={x+600} min_z={z} max_z={z} height={y+300} attr.height2={y+600} color=9AC5CF attr.style=1 attr.facing=-z attr.text={text}")

    def lamp(name,x,z,y):
        add(f"object id={name} kind=lamp_post x={x} y={y} z={z} yaw=0 scale=800 attr.collision=none")

    add(f"region id=outpost_bounds kind=area {bounds(-41216,41216,-41472,19533)}")

    for level, y, ceiling, roof, label in (
        (-1, -2458, -154, 0, "B1地下室"),
        (1, 0, 2304, 0, "1F一层"),
        (2, 2458, 4762, 0, "2F二层"),
        (3, 4916, 7220, 1, "屋顶"),
    ):
        name = "b1" if level < 0 else str(level)
        add(f"region id=outpost_floor_{name} kind=building_floor {bounds(-10240,10240,-4096,19456)} attr.building=outpost_base attr.level={level} attr.y={y} attr.ceiling={ceiling} attr.roof={roof} attr.name={label}")

    # The basement extends under the hall and infrastructure, with one entry.
    solid("outpost_basement", -4608,4608,-4096,10240,-2612,-2458,"485A64",True)
    for name, b in (
        ("w",(-4685,-4531,-4096,10240)),
        ("e",(4531,4685,-4096,10240)),
        ("s",(-4608,4608,-4173,-4019)),
        ("nw",(-4608,-1536,10163,10317)),
        ("ne",(1536,4608,10163,10317)),
    ):
        solid("outpost_b1_wall_"+name,*b,-2458,-154,"526670")
    sign("outpost_b1_sign",0,10140,-2458,"B1_STORAGE_/_STAIRS")
    lamp("outpost_b1_hall_light",-4200,0,-2458)
    lamp("outpost_b1_infra_light",4200,7168,-2458)

    # Second floor follows the existing cross-shaped building footprint.
    footprint = (
        ("hall",(-4608,4608,-4096,4096)),
        ("infra",(-4608,4608,4096,10240)),
        ("research",(-10240,-4608,-3072,3072)),
        ("operations",(4608,10240,-3072,3072)),
    )
    outline = (
        ("s",(-4608,4608,-4173,-4019)),
        ("nw",(-4608,-1536,10163,10317)),
        ("ne",(1536,4608,10163,10317)),
        ("w",(-4685,-4531,3072,10240)),
        ("e",(4531,4685,3072,10240)),
        ("sw",(-4685,-4531,-4096,-3072)),
        ("se",(4531,4685,-4096,-3072)),
        ("research_w",(-10317,-10163,-3072,3072)),
        ("research_s",(-10240,-4608,-3149,-2995)),
        ("research_n",(-10240,-4608,2995,3149)),
        ("operations_e",(10163,10317,-3072,3072)),
        ("operations_s",(4608,10240,-3149,-2995)),
        ("operations_n",(4608,10240,2995,3149)),
    )
    for floor, y, color in (("2f",2458,"758995"),("roof",4916,"637A86")):
        for name, b in footprint:
            solid(f"outpost_{floor}_{name}",*b,y-154,y,color,True)
        for name, b in outline:
            solid(f"outpost_{floor}_wall_{name}",*b,y,y+(2304 if floor=="2f" else 512),"526875")
        sign(f"outpost_{floor}_sign",0,10140,y,"2F_/_STAIRS" if floor=="2f" else "ROOF_/_STAIRS")
        if floor=="2f":
            lamp("outpost_2f_hall_light",-4200,0,y)
            lamp("outpost_2f_infra_light",4200,7168,y)

    # Floor landings are south; half-storey landings are north. Both flights
    # occupy the same X/Z on successive storeys, with a real parallel underside.
    for name, y in (("b1",-2458),("1f",0),("2f",2458),("roof",4916)):
        solid(f"outpost_stair_{name}_landing",-3072,3072,10240,12288,y-154,y,"8296A0",True)
        lamp(f"outpost_stair_{name}_light",2750,11264,y)
        if name=="roof":
            continue
        mid=y+1229
        solid(f"outpost_stair_{name}_half",-3072,3072,17408,19456,mid-154,mid,"8296A0",True)
        lamp(f"outpost_stair_{name}_half_light",2750,18432,mid)
        for side, x0, x1, h0, h1 in (("w",-2560,-512,y,mid),("e",512,2560,y+2458,mid)):
            prefix=f"outpost_stair_{name}_{side}"
            b=bounds(x0,x1,12288,17408)
            add(f"collision id={prefix}_col shape=ramp_z {b} height={h0} height2={h1} attr.thickness=154 collision=true visible=false walkable=true")
            add(f"surface id={prefix}_surface kind=ramp {b} height={h0} height2={h1} axis=z material=8BA0AA attr.collision_id={prefix}_col")
            add(f"render id={prefix} kind=ramp {b} height={h0} attr.height2={h1} attr.style=3 attr.thickness=154 attr.steps=12 color=8BA0AA")

    # Continuous outer walls and the central spine keep actors off stair edges.
    for name, b in (("w",(-3149,-2995,10240,19456)),
                    ("e",(2995,3149,10240,19456)),
                    ("n",(-3072,3072,19379,19533)),
                    ("spine",(-256,256,12288,17408))):
        solid("outpost_stair_wall_"+name,*b,-2458,7220,"526875")
    solid("outpost_stair_cap",-3072,3072,10240,19456,7220,7374,"637A86")
    # East-side ledge over the lower flight is deliberately absent at every
    # half landing: the two flights join across the full north landing.
    sign("outpost_stair_entry_sign",0,10140,0,"STAIRS_B1_/_2F_/_ROOF")
    add(END)
    return "\n".join(lines)+"\n"


def update_map(path):
    raw=path.read_bytes()
    bom=b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    newline="\r\n" if b"\r\n" in raw else "\n"
    source=raw[len(bom):].decode("utf-8")
    if BEGIN in source:
        start=source.index(BEGIN)
        end=source.index(END,start)+len(END)
        source=source[:start]+source[end:].lstrip("\r\n")
    lines=[]
    indoor={"hall_entry","hall_west","command_floor","hall_east","hall_north",
            "service_strip","research_floor","operations_floor","infrastructure_floor"}
    for line in source.splitlines():
        words=line.split()
        fields=dict(w.split("=",1) for w in words[1:] if "=" in w)
        name=fields.get("id","")
        if words and words[0]=="region" and fields.get("kind")=="safe":
            continue
        if name=="service_spine_pillar":
            continue
        if name=="infrastructure_north":
            for side,x in (("left",-3072),("right",3072)):
                lines.append(f"object id=infrastructure_north_{side} kind=boundary_wall x={x} y=0 z=10240 yaw=0 scale=1000 attr.collision=component attr.length=3072")
            lines.append("object id=infrastructure_stair_door kind=arch_doorway x=0 y=0 z=10240 yaw=0 scale=1000 attr.collision=component")
            continue
        if words and words[0]=="world":
            line=line.replace("max_z=10752","max_z=19968")
        if name in indoor and words[0]=="surface":
            line=line.replace("kind=ground","kind=platform")
        if any(name==floor+"_col" for floor in indoor) and words[0]=="collision":
            line=line.replace("shape=flat","shape=box").replace("collision=false","collision=true").replace("visible=true","visible=false")+" attr.base_y=-154"
            # Idempotent: remove an already-present bottom before appending.
            line=line.replace(" attr.base_y=-154 attr.base_y=-154"," attr.base_y=-154")
        if any(name==floor+"_paint" for floor in indoor) and words[0]=="render":
            line=line.replace("kind=ground","kind=box")
            if "attr.base_y=" not in line: line+=" attr.base_y=-154"
        if line.startswith("# Outpost V1. RFU"):
            line="# Outpost. RFU = metres * 512; B1, 1F, 2F, accessible roof, north stairs."
        lines.append(line)
    result="\n".join(lines).rstrip()+"\n\n"+geometry()
    encoded=bom+result.replace("\n",newline).encode("utf-8")
    if encoded!=raw:
        path.write_bytes(encoded)


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--map",type=Path,default=Path(__file__).resolve().parents[1]/"rasterfall/assets/maps/outpost.map")
    parser.add_argument("--write",action="store_true",help="update the named map in place, preserving UTF-8 BOM and newlines")
    args=parser.parse_args()
    if args.write:
        update_map(args.map)
        print(f"updated {args.map}")
    else:
        print(geometry(),end="")
