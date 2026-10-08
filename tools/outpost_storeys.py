#!/usr/bin/env python3
"""Author the outpost's B1 / 1F / 2F / roof and north switchback stair.

The existing map remains the source for ground-floor furniture and the park.
Maintains the marked building shell, ground-floor connections and safe regions.
"""
from pathlib import Path
import argparse
from building_kit import BuildingKit

BEGIN = "# BEGIN OUTPOST STOREYS"
END = "# END OUTPOST STOREYS"
CEILING_COLOR = "E3E6E8"
WALL_COLOR = "B4C0C5"


def geometry(prefix=()):
    lines = [*prefix, BEGIN, "# Signed heights are relative to the existing outdoor ground."]
    add = lines.append

    def bounds(x0, x1, z0, z1):
        return f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"

    kit = BuildingKit(lines)
    solid = kit.solid

    def sign(name, x, z, y, text):
        add(f"render id={name} kind=sign min_x={x-600} max_x={x+600} min_z={z} max_z={z} height={y+300} attr.height2={y+600} color=9AC5CF attr.style=1 attr.facing=-z attr.text={text}")

    def lamp(name,x,z,y,wall=False,yaw=0):
        kind="light_wall" if wall else "light_ceiling"
        add(f"object id={name} kind={kind} x={x} y={y} z={z} yaw={yaw} scale=1000 attr.collision=none")

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
    for name, axis, at, start, end, doors in (
        ("w","z",-4608,-4096,10240,()),
        ("e","z",4608,-4096,10240,()),
        ("s","x",-4096,-4608,4608,()),
        ("nw","x",10240,-4608,-3072,()),
        ("ne","x",10240,3072,4608,()),
    ):
        kit.wall("outpost_b1_wall_"+name,axis,at,start,end,-2458,-154,WALL_COLOR,doors)
    sign("outpost_b1_sign",0,10140,-2458,"B1_STORAGE_/_STAIRS")

    # Second floor follows the existing cross-shaped building footprint.
    footprint = (
        ("hall",(-4608,4608,-4096,4096)),
        ("infra",(-4608,4608,4096,10240)),
        ("research",(-10240,-4608,-3072,3072)),
        ("operations",(4608,10240,-3072,3072)),
    )
    outline = (
        ("s","x",-4096,-4608,4608),
        ("nw","x",10240,-4608,-3072),
        ("ne","x",10240,3072,4608),
        ("w","z",-4608,3072,10240),
        ("e","z",4608,3072,10240),
        ("sw","z",-4608,-4096,-3072),
        ("se","z",4608,-4096,-3072),
        ("research_w","z",-10240,-3072,3072),
        ("research_s","x",-3072,-10240,-4608),
        ("research_n","x",3072,-10240,-4608),
        ("operations_e","z",10240,-3072,3072),
        ("operations_s","x",-3072,4608,10240),
        ("operations_n","x",3072,4608,10240),
    )
    for floor, y, color in (("1f",0,"64717A"),("2f",2458,"758995"),("roof",4916,"637A86")):
        if floor != "1f":
            for name, b in footprint:
                kit.slab(f"outpost_{floor}_{name}",b,y,color,ceiling_color=CEILING_COLOR)
        for name, axis, at, start, end in outline:
            doors = ()
            if name=="s" and floor=="1f":
                doors=((-1229,1229,1843),)
            kit.wall(f"outpost_{floor}_wall_{name}",axis,at,start,end,y,y+(512 if floor=="roof" else 2304),"526875" if floor=="roof" else WALL_COLOR,doors,walk=floor=="roof")
        if floor=="1f":
            for side, x in (("w",-4608),("e",4608)):
                kit.wall(f"outpost_1f_wing_{side}","z",x,-3072,3072,0,2304,WALL_COLOR,((-1229,1229,1843),))
            kit.wall("outpost_1f_hall_n","x",4096,-4608,4608,0,2304,WALL_COLOR,((-1229,1229,1843),))
            for side,x in (("power",-1536),("control",1536)):
                kit.wall(f"outpost_1f_{side}_partition","z",x,4096,10240,0,2304,WALL_COLOR,((5939,8397,1843),))
            continue
        sign(f"outpost_{floor}_sign",0,10140,y,"2F_/_STAIRS" if floor=="2f" else "ROOF_/_STAIRS")

    # Flush lenses replace the underside volume; finish() keeps a sealed cap.
    for floor,y in (("b1",-2458),("1f",0),("2f",2458)):
        for x in (-2304,2304):
            for z in (-512,7680):
                kit.ceiling_light(f"outpost_{floor}_ceiling_{x}_{z}",x,z,y+2304)
        if floor!="b1":
            for x in (-7424,7424):
                for z in (0,):
                    kit.ceiling_light(f"outpost_{floor}_wing_{x}_{z}",x,z,y+2304)
        if floor=="1f":
            # Separate central service corridor from the Power/Control rooms.
            for z in (7168,):kit.ceiling_light(f"outpost_1f_corridor_{z}",0,z,y+2304)

    storeys=(("b1",-2458),("1f",0),("2f",2458),("roof",4916))
    kit.switchback("outpost_stair",(-3072,3072,10240,19456),storeys,7220,
                   wall_color=WALL_COLOR,ceiling_color=CEILING_COLOR)
    for name, y in storeys:
        # One light per flight avoids the divider and reaches both end landings.
        if name=="roof":continue
        lamp(f"outpost_stair_{name}_flight_light",-2928,15360,y+2200,True,90)
        lamp(f"outpost_stair_{name}_return_light",2928,15360,y+3400,True,270)
    # Replace the two forecourt poles with facade-mounted warm downlights.
    for x in (-3000,3000):lamp(f"outpost_entry_light_{x}",x,-4237,1550,True,180)
    # East-side ledge over the lower flight is deliberately absent at every
    # half landing: the two flights join across the full north landing.
    sign("outpost_stair_entry_sign",0,10140,0,"STAIRS_B1_/_2F_/_ROOF")
    add(END)
    kit.finish()
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
    source_lines=source.rstrip().splitlines()
    source_fields={}
    for line in source_lines:
        words=line.split()
        fields=dict(w.split("=",1) for w in words[1:] if "=" in w)
        if words and words[0]=="collision":source_fields[fields.get("id","")]=fields
    for line in source_lines:
        words=line.split()
        fields=dict(w.split("=",1) for w in words[1:] if "=" in w)
        name=fields.get("id","")
        if any(name.startswith(floor+"_paint_recess_") for floor in indoor):
            continue
        if words and words[0]=="object" and fields.get("kind")=="lamp_post":
            continue
        if words and words[0]=="region" and fields.get("kind")=="safe":
            continue
        if name=="service_spine_pillar":
            continue
        if name=="infrastructure_north":
            continue
        if words and words[0]=="object" and fields.get("kind") in {"boundary_wall","arch_doorway","arch_beam","arch_wall"}:
            # BuildingKit now owns the indoor shell; retain the outdoor yard.
            if not name.startswith("yard_") and "attr.lab" not in fields:
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
            # Recreate the unsplit slab from its stable gameplay footprint;
            # moved/removed fixtures must not leave last run's recess pieces.
            col=source_fields[name[:-6]+"_col"]
            fields["kind"]="box"
            for key in ("min_x","max_x","min_z","max_z","height"):
                fields[key]=col[key]
            fields["attr.base_y"]="-154"
            fields["attr.bottom_color"]=CEILING_COLOR
            line="render "+" ".join(f"{key}={value}" for key,value in fields.items())
        if line.startswith("# Outpost V1. RFU"):
            line="# Outpost. RFU = metres * 512; B1, 1F, 2F, accessible roof, north stairs."
        lines.append(line)
    result=geometry([*lines, ""])
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
