#!/usr/bin/env python3
"""Capture and assemble the current RF Humanoid character lab sheet.

The rasterfall CLI owns rendering.  This script only arranges the normal
bind/idle/aim captures into a compact three-row, four-view contact sheet.
It deliberately does not use the lighting A/B captures.
"""

import argparse
import struct
import subprocess
from pathlib import Path
import zlib


MODES = ("bind", "rifle-idle", "rifle-aim")
VIEWS = ("front", "side", "back", "three-quarter")


def read_bmp(path):
    data = path.read_bytes()
    if data[:2] != b"BM":
        raise ValueError(f"not a BMP: {path}")
    pixel_offset = struct.unpack_from("<I", data, 10)[0]
    dib_size = struct.unpack_from("<I", data, 14)[0]
    width, height = struct.unpack_from("<ii", data, 18)
    planes, bpp, compression = struct.unpack_from("<HHI", data, 26)
    if dib_size < 40 or planes != 1 or compression != 0 or bpp not in (24, 32):
        raise ValueError(f"unsupported BMP format: {path}")
    top_down = height < 0
    height = abs(height)
    stride = ((width * bpp + 31) // 32) * 4
    pixels = bytearray(width * height * 3)
    for y in range(height):
        source_y = y if top_down else height - 1 - y
        row = pixel_offset + source_y * stride
        for x in range(width):
            src = row + x * (bpp // 8)
            dst = (y * width + x) * 3
            pixels[dst:dst + 3] = bytes((data[src + 2], data[src + 1], data[src]))
    return width, height, pixels


def resize_rgb(image, target_width, target_height):
    width, height, source = image
    if (width, height) == (target_width, target_height):
        return image
    result = bytearray(target_width * target_height * 3)
    for y in range(target_height):
        sy = min(height - 1, y * height // target_height)
        for x in range(target_width):
            sx = min(width - 1, x * width // target_width)
            src = (sy * width + sx) * 3
            dst = (y * target_width + x) * 3
            result[dst:dst + 3] = source[src:src + 3]
    return target_width, target_height, result


def png_chunk(kind, payload):
    return (struct.pack(">I", len(payload)) + kind + payload +
            struct.pack(">I", zlib.crc32(kind + payload) & 0xffffffff))


def write_png(path, image):
    width, height, pixels = image
    rows = b"".join(b"\0" + pixels[y * width * 3:(y + 1) * width * 3]
                    for y in range(height))
    payload = (b"\x89PNG\r\n\x1a\n" +
               png_chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 2, 0, 0, 0)) +
               png_chunk(b"IDAT", zlib.compress(rows, 9)) + png_chunk(b"IEND", b""))
    path.write_bytes(payload)


def assemble(paths, cell_width):
    source = [read_bmp(path) for path in paths]
    first_width, first_height, _ = source[0]
    cell_height = max(1, first_height * cell_width // first_width)
    cells = [resize_rgb(image, cell_width, cell_height) for image in source]
    sheet_width = cell_width * 4
    sheet = bytearray(sheet_width * cell_height * 3 * 3)
    for index, (_, _, pixels) in enumerate(cells):
        row, column = divmod(index, 4)
        for y in range(cell_height):
            dst = ((row * cell_height + y) * sheet_width + column * cell_width) * 3
            src = y * cell_width * 3
            sheet[dst:dst + cell_width * 3] = pixels[src:src + cell_width * 3]
    return sheet_width, cell_height * 3, sheet


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model", default="rasterfall/private-assets/models/rf_humanoid_v2.rmesh")
    parser.add_argument("--output", default="tmp/rf-humanoid-v2/humanoid-v2-lab.png")
    parser.add_argument("--capture-dir", help="reuse captures from this directory")
    parser.add_argument("--rasterfall", default="build/rasterfall")
    parser.add_argument("--cell-width", type=int, default=400)
    args = parser.parse_args()
    capture_dir = Path(args.capture_dir or Path(args.output).with_suffix("") / "captures")
    capture_dir.mkdir(parents=True, exist_ok=True)
    expected = [capture_dir / mode / f"{view}.bmp" for mode in MODES for view in VIEWS]
    if not args.capture_dir:
        subprocess.run([args.rasterfall, "--character-acceptance", args.model, str(capture_dir)],
                       check=True)
    missing = [path for path in expected if not path.is_file()]
    if missing:
        raise SystemExit("missing capture(s): " + ", ".join(map(str, missing)))
    output = Path(args.output)
    output.parent.mkdir(parents=True, exist_ok=True)
    write_png(output, assemble(expected, args.cell_width))
    print(f"character lab sheet: {output}")


if __name__ == "__main__":
    main()
