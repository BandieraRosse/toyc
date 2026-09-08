#!/usr/bin/env python3
"""Query the JSON sidecar produced by map_layout_export.py.

This is intentionally a small layout fact lookup tool. It does not parse .map
files and does not infer regions, paths, chokepoints, or other map semantics.
"""
import argparse
import json
import math
import sys
from pathlib import Path

SCHEMA = "rasterfall-map-layout-v1"
REQUIRED_BOUNDS = ("min_x", "max_x", "min_z", "max_z")


class QueryError(Exception):
    pass


def fail(message):
    raise QueryError(message)


def load_layout(path):
    try:
        doc = json.loads(path.read_text(encoding="utf-8"))
    except FileNotFoundError:
        fail(f"layout JSON not found: {path}")
    except (OSError, json.JSONDecodeError) as exc:
        fail(f"cannot read layout JSON '{path}': {exc}")
    if not isinstance(doc, dict) or doc.get("schema") != SCHEMA:
        fail(f"schema mismatch: expected {SCHEMA}")
    if not valid_bounds(doc.get("world")) or not isinstance(doc.get("objects"), list):
        fail("schema mismatch: layout must contain world and objects")
    for index, obj in enumerate(doc["objects"]):
        if not isinstance(obj, dict) or (obj.get("export_id") is not None and not isinstance(obj.get("export_id"), str)):
            fail(f"schema mismatch: invalid export_id in objects[{index}]")
        if not isinstance(obj.get("type"), str) or not valid_bounds(obj.get("bounds")):
            fail(f"schema mismatch: invalid object {obj.get('export_id', index)}")
        if not isinstance(obj.get("center"), dict) or not all(k in obj["center"] for k in ("x", "z")):
            fail(f"schema mismatch: invalid center for {obj['export_id']}")
    check_stale(doc)
    return doc


def valid_bounds(bounds):
    return isinstance(bounds, dict) and all(isinstance(bounds.get(k), (int, float)) for k in REQUIRED_BOUNDS) and bounds["min_x"] <= bounds["max_x"] and bounds["min_z"] <= bounds["max_z"]


def check_stale(doc):
    source = doc.get("source_file")
    if not isinstance(source, dict) or not isinstance(source.get("path"), str):
        return
    path = Path(source["path"])
    if not path.is_absolute():
        path = Path.cwd() / path
    try:
        stat = path.stat()
    except OSError:
        print(f"warning: source map unavailable for stale check: {path}", file=sys.stderr)
        return
    if stat.st_size != source.get("size") or stat.st_mtime_ns != source.get("mtime_ns"):
        print(f"warning: layout JSON may be stale; source map changed: {path}", file=sys.stderr)


def bounds_distance(a, b):
    dx = max(a["min_x"] - b["max_x"], b["min_x"] - a["max_x"], 0)
    dz = max(a["min_z"] - b["max_z"], b["min_z"] - a["max_z"], 0)
    return math.hypot(dx, dz)


def point_distance(x, z, b):
    dx = max(b["min_x"] - x, x - b["max_x"], 0)
    dz = max(b["min_z"] - z, z - b["max_z"], 0)
    return math.hypot(dx, dz)


def intersects(a, b):
    return a["min_x"] <= b["max_x"] and a["max_x"] >= b["min_x"] and a["min_z"] <= b["max_z"] and a["max_z"] >= b["min_z"]


def number(value, name):
    try:
        result = float(value)
    except ValueError:
        fail(f"invalid {name}: {value}")
    if not math.isfinite(result) or result < 0 and name == "radius":
        fail(f"invalid {name}: {value}")
    return result


def object_by_id(doc, export_id):
    for obj in doc["objects"]:
        if obj["export_id"] == export_id:
            return obj
    fail(f"object ID not found: {export_id}")


def compact(obj):
    b = obj["bounds"]
    source = obj.get("source", {})
    fields = [obj.get("export_id") or "-", obj["type"], f"center=({obj['center']['x']},{obj['center']['z']})", f"bounds=({b['min_x']},{b['max_x']},{b['min_z']},{b['max_z']})"]
    if source:
        fields.append(f"source=line {source.get('line')} record {source.get('record')}")
    return " ".join(fields)


def parse_rect(values):
    nums = [number(value, "rectangle coordinate") for value in values]
    if nums[0] > nums[1] or nums[2] > nums[3]:
        fail("invalid rectangle: minimum must not exceed maximum")
    return {"min_x": nums[0], "max_x": nums[1], "min_z": nums[2], "max_z": nums[3]}


def run(doc, args):
    objects = doc["objects"]
    if args.command == "summary":
        counts = {}
        for obj in objects:
            counts[obj["type"]] = counts.get(obj["type"], 0) + 1
        return {"schema": doc["schema"], "world": doc["world"], "object_count": len(objects), "counts": dict(sorted(counts.items()))}
    if args.command == "get":
        return object_by_id(doc, args.id)
    if args.command == "type":
        known = sorted({obj["type"] for obj in objects})
        if args.type not in known:
            fail(f"unknown object type: {args.type} (available: {', '.join(known) or 'none'})")
        return [obj for obj in objects if obj["type"] == args.type]
    if args.command == "near":
        target = object_by_id(doc, args.id)
        radius = number(args.radius, "radius")
        return [{"distance": bounds_distance(target["bounds"], obj["bounds"]), "object": obj} for obj in objects if obj is not target and bounds_distance(target["bounds"], obj["bounds"]) <= radius]
    if args.command == "near-pos":
        x, z, radius = number(args.x, "x"), number(args.z, "z"), number(args.radius, "radius")
        return [{"distance": point_distance(x, z, obj["bounds"]), "object": obj} for obj in objects if point_distance(x, z, obj["bounds"]) <= radius]
    if args.command == "rect":
        rectangle = parse_rect((args.minx, args.maxx, args.minz, args.maxz))
        return [obj for obj in objects if intersects(rectangle, obj["bounds"])]
    fail(f"unsupported command: {args.command}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("layout", type=Path, help="output.json from map_layout_export.py")
    parser.add_argument("--json", action="store_true", dest="as_json", help="emit machine-readable JSON")
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("summary")
    get = sub.add_parser("get"); get.add_argument("id")
    typ = sub.add_parser("type"); typ.add_argument("type")
    near = sub.add_parser("near"); near.add_argument("id"); near.add_argument("radius")
    near_pos = sub.add_parser("near-pos"); near_pos.add_argument("x"); near_pos.add_argument("z"); near_pos.add_argument("radius")
    rect = sub.add_parser("rect"); rect.add_argument("minx"); rect.add_argument("maxx"); rect.add_argument("minz"); rect.add_argument("maxz")
    args = parser.parse_args()
    try:
        result = run(load_layout(args.layout), args)
    except QueryError as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 2
    if args.as_json:
        print(json.dumps(result, ensure_ascii=False, indent=2))
    elif args.command == "summary":
        world = result["world"]
        print(f"world bounds: ({world['min_x']},{world['max_x']},{world['min_z']},{world['max_z']})")
        print(f"objects: {result['object_count']}")
        print("counts: " + " ".join(f"{key}={value}" for key, value in result["counts"].items()))
    elif args.command == "get":
        print(json.dumps(result, ensure_ascii=False, indent=2))
    else:
        for item in result:
            if isinstance(item, dict) and "object" in item:
                print(f"distance={item['distance']:g} {compact(item['object'])}")
            else:
                print(compact(item))
    return 0


if __name__ == "__main__":
    sys.exit(main())
