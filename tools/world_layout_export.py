#!/usr/bin/env python3
"""Export the Content Map: Spatial Map plus World Content V1."""
import argparse, json
from pathlib import Path
from PIL import Image, ImageDraw
from map_layout_export import parse as parse_map

def fields(words):
    return {x.split("=", 1)[0]: x.split("=", 1)[1] for x in words if "=" in x}

def number(value):
    try: return int(value)
    except (TypeError, ValueError): return 0

def parse_content(path):
    out = {"actors": [], "terminals": [], "flags": [], "formations": [], "fixtures": []}
    groups = {"actor": "actors", "terminal": "terminals", "flag": "flags", "formation": "formations", "fixture": "fixtures"}
    for line_no, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        words = line.split("#", 1)[0].split()
        if not words: continue
        kind, f = words[0], fields(words[1:])
        if kind not in groups: raise ValueError(f"line {line_no}: unknown content record {kind}")
        group = groups[kind]
        if not f.get("id"): raise ValueError(f"line {line_no}: missing id")
        if any(item["id"] == f["id"] for values in out.values() for item in values):
            raise ValueError(f"line {line_no}: duplicate id {f['id']}")
        if kind != "formation" and any(k not in f or not f[k].lstrip("+-").isdigit() for k in ("x", "y", "z")):
            raise ValueError(f"line {line_no}: malformed position")
        item = {"type": kind, "id": f.get("id", ""), "x": number(f.get("x")),
                "y": number(f.get("y")), "z": number(f.get("z")),
                "yaw": number(f.get("yaw")), "source": str(path),
                "source_line": line_no}
        if kind in ("actor", "terminal", "fixture"): item["kind"] = f.get("kind", f.get("character", ""))
        if kind == "formation": item["members"] = f.get("members", "").split(",") if f.get("members") else []
        out[group].append(item)
    ids = {item["id"] for values in out.values() for item in values}
    for formation in out["formations"]:
        missing = [member for member in formation["members"] if member not in ids]
        if missing: raise ValueError(f"formation {formation['id']}: unknown member {missing[0]}")
    return out

def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("map", type=Path); ap.add_argument("content", type=Path)
    ap.add_argument("--output-dir", type=Path, required=True)
    args = ap.parse_args(); spatial = parse_map(args.map); content = parse_content(args.content)
    world = spatial["world"]; objects = spatial["objects"]
    doc = {"schema": "rasterfall-world-layout-v1", "world": {"id": args.content.stem},
           "sources": {"map": str(args.map), "content": str(args.content)},
           "spatial": {"world": world, "objects": objects}, "content": content}
    args.output_dir.mkdir(parents=True, exist_ok=True)
    (args.output_dir / "output.json").write_text(json.dumps(doc, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    w, h = 1400, 1000; image = Image.new("RGB", (w, h), (18, 23, 29)); draw = ImageDraw.Draw(image)
    bx = world["max_x"] - world["min_x"] or 1; bz = world["max_z"] - world["min_z"] or 1
    def point(x, z): return (int((x - world["min_x"]) * (w - 80) / bx + 40), int((world["max_z"] - z) * (h - 80) / bz + 40))
    colors = {"box": (75, 91, 103), "air_wall": (112, 65, 80), "wall": (86, 103, 116), "ramp": (82, 125, 96), "platform": (95, 115, 80), "safe": (42, 103, 91), "spawn": (205, 179, 70), "prop": (111, 102, 80)}
    for obj in objects:
        b = obj.get("bounds");
        if not b: continue
        p0, p1 = point(b["min_x"], b["max_z"]), point(b["max_x"], b["min_z"])
        draw.rectangle((*p0, *p1), outline=colors.get(obj.get("type"), (65, 75, 85)))
    marks = {"actors": (235, 235, 235), "terminals": (83, 189, 220), "flags": (225, 157, 56), "formations": (120, 220, 150), "fixtures": (180, 100, 210)}
    for group, items in content.items():
        for item in items:
            x, y = point(item["x"], item["z"]); c = marks[group]
            draw.ellipse((x - 6, y - 6, x + 6, y + 6), fill=c)
            label = item["id"] if group not in ("terminals", "formations") else ("TERM:" + item.get("kind", "") if group == "terminals" else "FORMATION:" + item["id"])
            draw.text((x + 8, y - 6), label, fill=c)
    image.save(args.output_dir / "layout.png")

if __name__ == "__main__": main()
