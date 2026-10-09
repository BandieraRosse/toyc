"""Plan and align Outpost equipment using actual Runtime Map component bounds.

Run after the structural/campus generators. Never resize collision primitives or
models. Planning footprints belong to equipment groups, not navigation truth.
"""
import argparse
import json
from pathlib import Path

from map_grid import CELL, Footprint
from map_layout_export import parse

ROOT = Path(__file__).resolve().parents[1]


def fields(line):
    words = line.split()
    return (words[0] if words else "", dict(w.split("=", 1) for w in words[1:] if "=" in w))


def replace_fields(line, changes):
    words = line.split()
    for i, word in enumerate(words):
        key = word.partition("=")[0]
        if key in changes:
            words[i] = f"{key}={changes.pop(key)}"
    words.extend(f"{k}={v}" for k, v in changes.items())
    return " ".join(words)


def save(path, lines):
    raw = path.read_bytes()
    bom = b"\xef\xbb\xbf" if raw.startswith(b"\xef\xbb\xbf") else b""
    nl = "\r\n" if b"\r\n" in raw else "\n"
    result = bom + (nl.join(lines) + nl).encode("utf-8")
    if raw != result:
        path.write_bytes(result)


def overlap(a, b):
    return a[0] < b[1] and a[1] > b[0] and a[2] < b[3] and a[3] > b[2]


def plan(path):
    doc = parse(path)
    lines = path.read_text(encoding="utf-8-sig").splitlines()
    records = {f["id"]: (i, kind, f) for i, line in enumerate(lines)
               for kind, f in [fields(line)] if "id" in f}
    props = {o["source_id"]: o for o in doc["objects"] if o["type"] == "prop"}
    colliders = [o for o in doc["objects"] if o["type"] == "air_wall" and not o.get("generated")]
    modules = [o for o in doc["objects"] if o.get("kind") == "building_module"]
    groups, assigned = [], set()
    for name, prop in props.items():
        if name in assigned or not prop.get("collision_bounds"):
            continue
        # Outdoor architectural walls are wall lines, not equipment reservations.
        if prop["asset"] in {"boundary_wall", "gate_frame"}:
            continue
        members = [name]
        if name == "command_table":
            members += ["command_map_screen"]
        elif name == "null_desk":
            members = [n for n in props if n.startswith("null_")]
        elif name.startswith("work_desk_"):
            suffix = name.rsplit("_", 1)[1]
            members += ["work_chair_"+suffix, "work_monitor_"+suffix]
        elif name.startswith("host_") and name.endswith("_frame"):
            members = [n for n in props if n.startswith(name[:-5])]
        members = [n for n in members if n in props]
        assigned.update(members)
        groups.append((name, members))

    reservations, report, null_delta = [], [], (0, 0)
    for name, members in groups:
        p = props[name]
        if p["yaw_degrees"] % 90:
            raise ValueError(f"{name}: planning only supports cardinal yaw")
        boxes = [props[n]["collision_bounds"] for n in members if props[n].get("collision_bounds")]
        b = (min(v["min_x"] for v in boxes), max(v["max_x"] for v in boxes),
             min(v["min_z"] for v in boxes), max(v["max_z"] for v in boxes))
        target = Footprint.near_bounds(b)
        room = next((o["bounds"] for o in modules if o["bounds"]["min_x"] < p["x"] < o["bounds"]["max_x"]
                     and o["bounds"]["min_z"] < p["z"] < o["bounds"]["max_z"]), None)
        candidates = []
        for ix in range(-4, 5):
            for iz in range(-4, 5):
                grid = Footprint(target.min_x+ix*CELL, target.min_z+iz*CELL, target.width, target.depth)
                cx, cz = grid.center
                dx, dz = cx-(b[0]+b[1])//2, cz-(b[2]+b[3])//2
                physical = (b[0]+dx,b[1]+dx,b[2]+dz,b[3]+dz)
                if room and not (physical[0] >= room["min_x"]+75 and physical[1] <= room["max_x"]-75
                                 and physical[2] >= room["min_z"]+75 and physical[3] <= room["max_z"]-75):
                    continue
                if any(overlap(grid.bounds, r) for r in reservations):
                    continue
                # Ground-floor walls: metadata alone must not move furniture through a partition.
                if any(o["height"] > 0 and int(fields(lines[o["source"]["line"]-1])[1].get("attr.base_y",0)) < 896
                       and overlap(physical, (o["bounds"]["min_x"],o["bounds"]["max_x"],o["bounds"]["min_z"],o["bounds"]["max_z"]))
                       for o in colliders):
                    continue
                candidates.append((dx*dx+dz*dz, abs(dx)+abs(dz), grid.min_z, grid.min_x, grid, dx, dz))
        if not candidates:
            raise ValueError(f"{name}: no non-overlapping planning placement within four cells")
        _, _, _, _, grid, dx, dz = min(candidates)
        reservations.append(grid.bounds)
        index, _, f = records[name]
        # Metadata uses the same local X/Z frame as the root object.
        local_x, local_z = p["x"]-int(f["x"]), p["z"]-int(f["z"])
        assembly = f.get("attr.assembly")
        if assembly:
            ai, _, af = records[assembly]
            lines[ai] = replace_fields(lines[ai], {"x": int(af["x"])+dx, "z": int(af["z"])+dz})
            local_x += dx
            local_z += dz
        else:
            for member in members:
                mi, _, mf = records[member]
                lines[mi] = replace_fields(lines[mi], {"x": int(mf["x"])+dx, "z": int(mf["z"])+dz})
        lines[index] = replace_fields(lines[index], {
            "attr.grid_min_x": grid.min_x-local_x, "attr.grid_min_z": grid.min_z-local_z,
            "attr.grid_width": grid.width, "attr.grid_depth": grid.depth})
        report.append({"id": name, "members": members, "before": b, "planning_bounds": grid.bounds,
                       "cells": [grid.width,grid.depth], "delta": [dx,dz]})
        if name == "null_desk":
            null_delta = dx, dz
    return lines, report, null_delta


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--map", type=Path, default=ROOT/"rasterfall/assets/maps/outpost.map")
    ap.add_argument("--output", type=Path, default=ROOT/"tmp/outpost-grid-plan.json")
    ap.add_argument("--write", action="store_true")
    ap.add_argument("--check", action="store_true", help="fail if equipment would move or metadata is stale")
    ap.add_argument("--rebuild", action="store_true", help="rebuild the canonical Outpost producers before planning")
    args = ap.parse_args()
    if args.rebuild:
        if not args.write or args.map.resolve() != ROOT/"rasterfall/assets/maps/outpost.map":
            ap.error("--rebuild requires --write and the canonical Outpost map")
        from dynamic_experiment_maps import main as campus
        from host_rack_layout import main as racks
        from outpost_storeys import update_map
        campus()
        racks()
        update_map(args.map)
        template=ROOT/"rasterfall/assets/maps/experiment_components.map"
        template_lines, _, _ = plan(template)
        save(template, template_lines)
    lines, report, delta = plan(args.map)
    if not args.check:
        args.output.parent.mkdir(parents=True, exist_ok=True)
        args.output.write_text(json.dumps({"cell_size": CELL, "groups": report}, indent=2)+"\n", encoding="utf-8")
    original = args.map.read_text(encoding="utf-8-sig").splitlines()
    if args.check and lines != original:
        raise SystemExit("outpost equipment grid is stale; run outpost_grid.py --write after generators")
    if args.write:
        save(args.map, lines)
        # Grid centres are spawn anchors; movement itself stays continuous.
        entries = args.map.read_text(encoding="utf-8-sig").splitlines()
        for i, line in enumerate(entries):
            kind, f = fields(line)
            if kind == "region" and f.get("kind") == "start":
                changes={}
                for axis in ("x","z"):
                    value=int(f["min_"+axis])
                    if value != int(f["max_"+axis]):
                        raise ValueError("Outpost player start must be a point")
                    center=(value//CELL)*CELL+CELL//2
                    changes["min_"+axis]=changes["max_"+axis]=center
                entries[i]=replace_fields(line,changes)
            elif kind == "object" and f.get("kind") == "boundary_wall" and f.get("id","").startswith("yard_"):
                from map_grid import nearest_line
                name=f["id"]
                changes={}
                if name in ("yard_west_edge","yard_east_edge"):
                    changes["x"]=nearest_line(int(f["x"]))
                elif name in ("yard_south_left","yard_south_right"):
                    changes.update(x=-3072 if name.endswith("left") else 3072)
                    changes["attr.length"]=3072
                entries[i]=replace_fields(line,changes)
        save(args.map,entries)
        content = args.map.parent.parent/"worlds"/(args.map.stem+".content")
        if content.exists():
            entries = content.read_text(encoding="utf-8-sig").splitlines()
            for i, line in enumerate(entries):
                kind, f = fields(line)
                if kind == "actor" and f.get("id") == "null":
                    entries[i] = replace_fields(line, {"x": int(f["x"])+delta[0], "z": int(f["z"])+delta[1]})
                    _,f=fields(entries[i])
                if kind == "actor":
                    entries[i]=replace_fields(entries[i], {axis:(int(f[axis])//CELL)*CELL+CELL//2 for axis in ("x","z")})
            save(content, entries)
    if args.check:
        print(f"validated {len(report)} equipment groups")
    else:
        print(f"planned {len(report)} equipment groups; {sum(r['delta'] != [0,0] for r in report)} moved; {args.output}")


if __name__ == "__main__":
    main()
