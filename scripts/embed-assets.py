#!/usr/bin/env python3
"""Generate a compact C asset table for the Windows Rasterfall build."""
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: embed-assets.py ASSET_DIR OUTPUT_C")

root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
files = sorted(p for p in root.rglob("*") if p.is_file())

with out.open("w", encoding="utf-8") as f:
    f.write("/* generated; do not edit */\n#include <stddef.h>\n\n")
    f.write("struct rasterfall_embedded_asset {\n")
    f.write("    const char *name; const unsigned char *data; size_t size;\n};\n\n")
    for index, path in enumerate(files):
        data = path.read_bytes()
        f.write("static const unsigned char asset_%d[] = {\n" % index)
        for offset in range(0, len(data), 16):
            f.write("    " + ", ".join("0x%02x" % b for b in data[offset:offset + 16]) + ",\n")
        f.write("};\n")
    f.write("\nconst struct rasterfall_embedded_asset rasterfall_embedded_assets[] = {\n")
    for index, path in enumerate(files):
        name = path.relative_to(root).as_posix()
        f.write('    { "%s", asset_%d, sizeof(asset_%d) },\n' %
                (name.replace('"', '\\"'), index, index))
    f.write("    { 0, 0, 0 }\n};\n")

