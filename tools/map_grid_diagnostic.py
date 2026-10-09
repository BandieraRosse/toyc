"""Advisory layered one-metre geometry graph from native Runtime Map collisions.

This does not execute Game movement or certify navigation reachability. All
authored hardware candidates are included; dynamic experiment groups are absent.
"""
import argparse
from collections import defaultdict, deque
import json
from pathlib import Path

from PIL import Image, ImageDraw
from map_grid import CELL
from map_layout_export import parse, runtime_collisions


def height_at(c, x, z):
    axis = "x" if c["shape"] == "ramp_x" else "z"
    if c["shape"] not in {"ramp_x", "ramp_z"}:
        return c["height"]
    length = c["max_"+axis]-c["min_"+axis]
    if length <= 0:
        raise ValueError("invalid ramp span")
    offset = max(0, min(length, (x if axis == "x" else z)-c["min_"+axis]))
    delta = (c["height2"]-c["height"])*offset
    return c["height"] + (abs(delta)//length if delta >= 0 else -(abs(delta)//length))


def contains(c, x, z):
    return c["min_x"] <= x <= c["max_x"] and c["min_z"] <= z <= c["max_z"]


def footprint_touches(c, x, z, radius):
    # Static Game cuboid queries expand X/Z bounds by radius (unit-to-unit
    # contact uses circles separately). Boundary contact alone is allowed.
    return (x+radius > c["min_x"] and x-radius < c["max_x"] and
            z+radius > c["min_z"] and z-radius < c["max_z"])


class Geometry:
    def __init__(self, collisions, radius=180, body_height=896):
        self.radius, self.body_height = radius, body_height
        self.bins = defaultdict(list)
        for c in collisions:
            if not all(k in c for k in ("height2","ramp_thickness")):
                raise ValueError("rebuild native map-inspect: ramp diagnostic fields are missing")
            if c["shape"] not in {"box", "flat", "ramp_x", "ramp_z"}:
                raise ValueError("unsupported diagnostic shape: "+c["shape"])
            for iz in range((c["min_z"]-radius)//CELL, (c["max_z"]+radius)//CELL+1):
                for ix in range((c["min_x"]-radius)//CELL, (c["max_x"]+radius)//CELL+1):
                    self.bins[ix,iz].append(c)

    def nearby(self, x, z):
        return self.bins[x//CELL,z//CELL]

    def supports(self, x, z):
        values = defaultdict(list)
        for c in self.nearby(x, z):
            if c["walkable"] and contains(c,x,z):
                values[height_at(c,x,z)].append(c["id"])
        return values

    def blocked(self, x, z, y):
        for c in self.nearby(x,z):
            if not c["collision"] or not footprint_touches(c,x,z,self.radius):
                continue
            top = height_at(c,x,z)
            if c["shape"].startswith("ramp_"):
                if c["walkable"] and abs(top-y) <= 120:
                    continue
                low = top-c["ramp_thickness"] if c["ramp_thickness"] else min(0,top)
            else:
                low = c["base_y"]
                if c["walkable"] and y < top <= y+120 and low <= y+120:
                    continue
            if c["blocks_airborne"] or (top > y and low < y+self.body_height):
                return c["id"]
        return None

    def connection(self, a, b):
        if abs(a["y"]-b["y"]) > 120:
            ramps = [c for c in self.nearby(a["x"],a["z"])+self.nearby(b["x"],b["z"])
                     if c["walkable"] and c["shape"].startswith("ramp_")]
            if not ramps:
                return False
        samples = max(abs(a["x"]-b["x"]),abs(a["z"]-b["z"]))//64+1
        for i in range(samples+1):
            x = a["x"]+(b["x"]-a["x"])*i//samples
            z = a["z"]+(b["z"]-a["z"])*i//samples
            y = a["y"]+(b["y"]-a["y"])*i//samples
            heights = self.supports(x,z)
            if not heights:
                return False
            actual = min(heights, key=lambda h:abs(h-y))
            if abs(actual-y) > 120 or self.blocked(x,z,actual):
                return False
        return True


def build(doc, radius=180, body_height=896):
    geometry = Geometry(doc["runtime_collisions"],radius,body_height)
    floors = []
    for o in doc["objects"]:
        if o.get("kind") == "building_floor":
            f = dict(w.split("=",1) for w in o["source"]["fields"] if "=" in w)
            floors.append({"id":o["source_id"], "y":int(f["attr.y"]), "bounds":o["bounds"]})
    floors.sort(key=lambda f:f["y"])
    world = doc["world"]
    grid_bounds = [world["min_x"]//CELL,(world["max_x"]+CELL-1)//CELL,
                   world["min_z"]//CELL,(world["max_z"]+CELL-1)//CELL]
    nodes, cells = [], defaultdict(list)
    for iz in range(grid_bounds[2],grid_bounds[3]):
        for ix in range(grid_bounds[0],grid_bounds[1]):
            x,z = ix*CELL+CELL//2, iz*CELL+CELL//2
            if not (world["min_x"] <= x < world["max_x"] and world["min_z"] <= z < world["max_z"]):
                continue
            for y, supports in sorted(geometry.supports(x,z).items()):
                level = "outside"
                for f in floors:
                    b=f["bounds"]
                    if b["min_x"] <= x < b["max_x"] and b["min_z"] <= z < b["max_z"] and f["y"] <= y+120:
                        level = f["id"]
                blocker = geometry.blocked(x,z,y)
                node = {"id":len(nodes), "ix":ix, "iz":iz, "x":x, "z":z, "y":y,
                        "layer":level, "state":"blocked" if blocker else "open",
                        "supports":supports, "blocker":blocker}
                cells[ix,iz].append(node["id"])
                nodes.append(node)
    edges, adjacency = [], defaultdict(list)
    for a in nodes:
        if a["state"] != "open":
            continue
        # Four unique directions represent eight neighbours in this advisory graph.
        for dx,dz in ((1,0),(0,1),(1,1),(-1,1)):
            for bid in cells[a["ix"]+dx,a["iz"]+dz]:
                b=nodes[bid]
                if b["state"] != "open":
                    continue
                if dx and dz:
                    # Both orthogonal corners need a matching supported, clear point.
                    if not all(any(nodes[n]["state"] == "open" and abs(nodes[n]["y"]-a["y"]) <= 120
                                   for n in cells[c]) for c in ((a["ix"]+dx,a["iz"]),(a["ix"],a["iz"]+dz))):
                        continue
                if geometry.connection(a,b) and geometry.connection(b,a):
                    edges.append([a["id"],bid])
                    adjacency[a["id"]].append(bid)
                    adjacency[bid].append(a["id"])
    components = []
    for a in nodes:
        if a["state"] != "open" or "component" in a:
            continue
        cid=len(components);queue=deque([a["id"]]);a["component"]=cid;count=0;layers=set()
        while queue:
            n=nodes[queue.popleft()];count+=1;layers.add(n["layer"])
            for nid in adjacency[n["id"]]:
                if "component" not in nodes[nid]:
                    nodes[nid]["component"]=cid;queue.append(nid)
        components.append({"id":cid,"nodes":count,"layers":sorted(layers)})
    return {"schema":"rasterfall-map-grid-diagnostic-v1", "source_file":doc["source_file"],
            "cell_size":CELL,"origin":[0,0],"grid_bounds":grid_bounds,"floors":floors,
            "radius":radius,"body_height":body_height,"nodes":nodes,"edges":edges,"components":components,
            "scope":"advisory Runtime Map geometry; not Game movement or navigation proof",
            "limitations":["All authored hardware rack candidates are included.",
                           "Dynamic groups and dynamic unit collisions are absent.",
                           "Support uses cell centres and 64 RFU edge samples; partial footprint support and directed drops are not reproduced.",
                           "Bidirectional geometric candidates do not replace Game's directed transactional movement validation."]}


def render(result, output):
    layers=[{"id":"outside","y":0,"bounds":None}]+result["floors"]
    for layer in layers:
        b=layer["bounds"]
        if b:
            x0,x1,z0,z1=b["min_x"]//CELL,b["max_x"]//CELL,b["min_z"]//CELL,b["max_z"]//CELL
            scale=14
        else:
            x0,x1,z0,z1=result["grid_bounds"];scale=7
        image=Image.new("RGB",((x1-x0)*scale+40,(z1-z0)*scale+85),(24,30,38))
        draw=ImageDraw.Draw(image)
        draw.text((12,10),layer["id"]+" | 1m grid | advisory geometry",fill=(230,235,240))
        draw.text((12,27),"green: clear centre / red: blocked / dark: no sampled support",fill=(190,200,210))
        draw.text((12,42),"blue: elevated support / ramp",fill=(150,185,230))
        selected={}
        for n in result["nodes"]:
            if layer["id"] != "outside" and n["layer"] != layer["id"]:
                continue
            if layer["id"] == "outside" and n["y"] != 0:
                continue
            key=n["ix"],n["iz"]
            if key not in selected or abs(n["y"]-layer["y"]) < abs(selected[key]["y"]-layer["y"]):
                selected[key]=n
        for (ix,iz),n in selected.items():
            if not (x0 <= ix < x1 and z0 <= iz < z1):
                continue
            px,py=20+(ix-x0)*scale,65+(z1-1-iz)*scale
            color=(190,70,70) if n["state"] == "blocked" else (60,146,112)
            if n["state"] == "open" and abs(n["y"]-layer["y"])>120:
                color=(85,132,188)
            draw.rectangle((px,py,px+scale-2,py+scale-2),fill=color)
        image.save(output/(layer["id"]+".png"))


def main():
    ap=argparse.ArgumentParser(description=__doc__)
    ap.add_argument("map",type=Path)
    ap.add_argument("--output-dir",type=Path,default=Path("tmp/map-grid"))
    ap.add_argument("--radius",type=int,default=180)
    ap.add_argument("--body-height",type=int,default=896)
    args=ap.parse_args()
    if args.radius <= 0 or args.body_height <= 0:
        ap.error("radius and body height must be positive")
    doc=parse(args.map)
    if "runtime_collisions" not in doc:
        doc["runtime_collisions"]=runtime_collisions(args.map)
    result=build(doc,args.radius,args.body_height)
    args.output_dir.mkdir(parents=True,exist_ok=True)
    (args.output_dir/"grid.json").write_text(json.dumps(result,indent=2)+"\n",encoding="utf-8")
    render(result,args.output_dir)
    cross=sum(result["nodes"][a]["layer"] != result["nodes"][b]["layer"] for a,b in result["edges"])
    print(f"advisory grid: {len(result['nodes'])} support nodes, {len(result['edges'])} edges, {len(result['components'])} components, {cross} cross-layer candidates")


if __name__ == "__main__":
    main()
