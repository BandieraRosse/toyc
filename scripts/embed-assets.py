#!/usr/bin/env python3
"""Generate the embedded asset lookup used by Linux and Windows Rasterfall."""
import pathlib
import sys

if len(sys.argv) != 3:
    raise SystemExit("usage: embed-assets.py ASSET_DIR OUTPUT_C")

root = pathlib.Path(sys.argv[1])
out = pathlib.Path(sys.argv[2])
if root.name == "rasterfall":
    roots = [root / "assets", root / "private-assets"]
    files = sorted(
        p for asset_root in roots if asset_root.is_dir()
        for p in asset_root.rglob("*")
        if p.is_file() and "source" not in p.relative_to(root).parts and
        p.name != "UAL1_Standard_RM.glb"
    )
    logical_prefix = "rasterfall"
else:
    files = sorted(p for p in root.rglob("*") if p.is_file())
    logical_prefix = "rasterfall/assets"

with out.open("w", encoding="utf-8") as f:
    f.write("/* generated; do not edit */\n#include <toy_assets.h>\n\n")
    f.write("struct rasterfall_embedded_asset {\n")
    f.write("    const char *name; const unsigned char *data; uint32_t size;\n};\n\n")
    for index, path in enumerate(files):
        data = path.read_bytes()
        f.write("static const unsigned char asset_%d[] = {\n" % index)
        for offset in range(0, len(data), 16):
            f.write("    " + ", ".join("0x%02x" % b for b in data[offset:offset + 16]) + ",\n")
        f.write("};\n")
    f.write("\nstatic const struct rasterfall_embedded_asset rasterfall_embedded_assets[] = {\n")
    for index, path in enumerate(files):
        name = logical_prefix + "/" + path.relative_to(root).as_posix()
        f.write('    { "%s", asset_%d, sizeof(asset_%d) },\n' %
                (name.replace('"', '\\"'), index, index))
    f.write("};\n\n")
    f.write("static int asset_equal(const char *a, const char *b) {\n")
    f.write("    if (!a || !b) return 0;\n")
    f.write("    while (*a && *a == *b) { a++; b++; }\n")
    f.write("    return *a == *b;\n}\n\n")
    f.write("const unsigned char *toy_embedded_asset_find(const char *path, uint32_t *size) {\n")
    f.write("    int i;\n    if (size) *size = 0;\n")
    f.write("    for (i = 0; i < %d; i++) if (asset_equal(path, rasterfall_embedded_assets[i].name)) {\n" % len(files))
    f.write("        if (size) *size = rasterfall_embedded_assets[i].size;\n")
    f.write("        return rasterfall_embedded_assets[i].data;\n    }\n    return 0;\n}\n\n")
    f.write("int toy_embedded_asset_count(void) { return %d; }\n" % len(files))
    f.write("const char *toy_embedded_asset_path(int index) {\n")
    f.write("    if (index < 0 || index >= %d) return 0;\n" % len(files))
    f.write("    return rasterfall_embedded_assets[index].name;\n}\n")
