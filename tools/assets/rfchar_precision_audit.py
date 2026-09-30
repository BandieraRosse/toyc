#!/usr/bin/env python3
"""Measure GLB geometry lost to RFCHAR coordinate quantization (read only)."""
import argparse
import hashlib
import json
import math
from collections import Counter
from pathlib import Path
from rfchar_import import glb, accessor


def audit(path):
    doc, blob = glb(path)
    report = {"source": str(path), "sha256": hashlib.sha256(path.read_bytes()).hexdigest(),
              "images": len(doc.get("images", [])), "materials": [], "meshes": []}
    for m in doc.get("materials", []):
        report["materials"].append(m.get("name", ""))
    for mesh in doc["meshes"]:
        for primitive in mesh["primitives"]:
            pos = accessor(doc, blob, primitive["attributes"]["POSITION"])
            indices = accessor(doc, blob, primitive["indices"])
            def triangles(points):
                return [tuple(points[j] for j in indices[i:i+3]) for i in range(0, len(indices), 3)]
            def degenerate(t):
                a, b, c = t
                u = [b[i]-a[i] for i in range(3)]
                v = [c[i]-a[i] for i in range(3)]
                return sum((u[(i+1)%3]*v[(i+2)%3]-u[(i+2)%3]*v[(i+1)%3])**2 for i in range(3)) == 0
            source = triangles([tuple(p) for p in pos])
            counts = Counter(tuple(sorted(t)) for t in source)
            entry = {"mesh": mesh.get("name", ""), "material": primitive.get("material"),
                     "vertices": len(pos), "triangles": len(source),
                     "bounds_m": [[min(p[i] for p in pos), max(p[i] for p in pos)] for i in range(3)],
                     "source_degenerate": sum(map(degenerate, source)),
                     "duplicate_faces": sum(n-1 for n in counts.values()), "quantization": {}}
            for scale in (512, 8192, 65536):
                quantized = [tuple(int(x*scale + (0.5 if x >= 0 else -0.5)) for x in p) for p in pos]
                entry["quantization"][str(scale)] = {
                    "degenerate": sum(map(degenerate, triangles(quantized))),
                    "max_error_mm": max(math.dist(p, [v/scale for v in q])*1000 for p, q in zip(pos, quantized))}
            report["meshes"].append(entry)
    return report


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = audit(args.source)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False)+"\n", encoding="utf-8")
    print(json.dumps({"source_degenerate": sum(m["source_degenerate"] for m in result["meshes"]),
                      "duplicate_faces": sum(m["duplicate_faces"] for m in result["meshes"]),
                      "degenerate_by_scale": {s: sum(m["quantization"][s]["degenerate"] for m in result["meshes"]) for s in ("512", "8192", "65536")}}))
