#!/usr/bin/env python3
"""Build Rasterfall's fixed GB2312 8/16x16 bitmap font from a Unicode BDF."""

import argparse
import gzip
import struct
from pathlib import Path

MAGIC = b"RFHZK16\0"
HEADER_SIZE = 32
ASCII_FIRST = 0x20
ASCII_COUNT = 95
GB_ROWS = 87                 # GB2312 byte 1: A1-F7
GB_COLUMNS = 94             # GB2312 byte 2: A1-FE
GLYPH_ALIASES = {0x2015: 0x2014}  # WHATWG/Python GB2312 mapping vs BDF name


def read_bdf(path):
    glyphs = {}
    ascent = 14
    current = None
    bitmap = []
    in_bitmap = False
    if path.suffix == ".gz":
        text = gzip.open(path, "rt", encoding="ascii", errors="replace").read()
    else:
        text = path.read_text(encoding="ascii", errors="replace")
    for raw in text.splitlines():
        raw = raw.strip()
        if raw.startswith("FONT_ASCENT "):
            ascent = int(raw.split()[1])
        elif raw.startswith("STARTCHAR "):
            current = {"encoding": -1, "dwidth": 0, "bbx": (0, 0, 0, 0)}
            bitmap = []
            in_bitmap = False
        elif current is not None and raw.startswith("ENCODING "):
            current["encoding"] = int(raw.split()[1])
        elif current is not None and raw.startswith("DWIDTH "):
            current["dwidth"] = int(raw.split()[1])
        elif current is not None and raw.startswith("BBX "):
            current["bbx"] = tuple(map(int, raw.split()[1:5]))
        elif current is not None and raw == "BITMAP":
            in_bitmap = True
        elif current is not None and raw == "ENDCHAR":
            codepoint = current["encoding"]
            if codepoint >= 0:
                glyphs[codepoint] = rasterize(current, bitmap, ascent)
            current = None
            in_bitmap = False
        elif current is not None and in_bitmap:
            bitmap.append(int(raw, 16))
    return glyphs


def read_vga8x16(path):
    """Read the restored pre-GB2312 95-glyph ASCII table."""
    import re
    values = [int(value, 16) for value in
              re.findall(r"0x([0-9a-fA-F]{2})", path.read_text())]
    expected = ASCII_COUNT * 16
    if len(values) != expected:
        raise SystemExit(f"{path}: expected {expected} VGA glyph bytes, got {len(values)}")
    return [(8, values[offset:offset + 16])
            for offset in range(0, expected, 16)]


def rasterize(glyph, bitmap, ascent):
    width, height, xoff, yoff = glyph["bbx"]
    cell_width = 8 if glyph["dwidth"] <= 8 else 16
    rows = [0] * 16
    top = ascent - (height + yoff)
    encoded_width = max(8, ((width + 7) // 8) * 8)
    for source_y, bits in enumerate(bitmap[:height]):
        target_y = top + source_y
        if not 0 <= target_y < 16:
            continue
        for source_x in range(width):
            if bits & (1 << (encoded_width - 1 - source_x)):
                target_x = xoff + source_x
                if 0 <= target_x < cell_width:
                    rows[target_y] |= 1 << (cell_width - 1 - target_x)
    return cell_width, rows


def halfwidth(rows, width):
    if width == 8:
        return rows
    occupied = [x for x in range(16)
                if any(row & (1 << (15 - x)) for row in rows)]
    if not occupied:
        return [0] * 16
    left, right = min(occupied), max(occupied)
    span = right - left + 1
    output = []
    for row in rows:
        bits = 0
        for target_x in range(8):
            source_a = left + target_x * span // 8
            source_b = left + ((target_x + 1) * span + 7) // 8
            if any(row & (1 << (15 - x))
                   for x in range(source_a, min(source_b, right + 1))):
                bits |= 1 << (7 - target_x)
        output.append(bits)
    return output


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("bdf", type=Path, help="16x16 CJK BDF")
    parser.add_argument("output", type=Path)
    parser.add_argument("--ascii-bdf", type=Path,
                        help="optional 8-pixel ASCII strike from the same family")
    parser.add_argument("--ascii-vga", type=Path,
                        help="optional restored legacy 8x16 ASCII table")
    args = parser.parse_args()
    glyphs = read_bdf(args.bdf)
    if args.ascii_vga:
        ascii_glyphs = {ASCII_FIRST + index: glyph
                        for index, glyph in enumerate(read_vga8x16(args.ascii_vga))}
    else:
        ascii_glyphs = read_bdf(args.ascii_bdf) if args.ascii_bdf else glyphs

    ascii_data = bytearray()
    for codepoint in range(ASCII_FIRST, ASCII_FIRST + ASCII_COUNT):
        width, rows = ascii_glyphs[codepoint]
        ascii_data.extend(halfwidth(rows, width))

    gb_data = bytearray(GB_ROWS * GB_COLUMNS * 32)
    index = []
    for lead in range(0xA1, 0xF8):
        for trail in range(0xA1, 0xFF):
            try:
                character = bytes((lead, trail)).decode("gb2312")
            except UnicodeDecodeError:
                continue
            if len(character) != 1:
                continue
            codepoint = ord(character)
            glyph = glyphs.get(codepoint) or glyphs.get(GLYPH_ALIASES.get(codepoint, -1))
            if not glyph:
                raise SystemExit(f"missing glyph for U+{codepoint:04X}")
            slot = (lead - 0xA1) * GB_COLUMNS + trail - 0xA1
            rows = glyph[1] if glyph[0] == 16 else [bits << 4 for bits in glyph[1]]
            for row, bits in enumerate(rows):
                struct.pack_into(">H", gb_data, slot * 32 + row * 2, bits)
            index.append((codepoint, slot))

    index.sort()
    ascii_offset = HEADER_SIZE
    gb_offset = ascii_offset + len(ascii_data)
    index_offset = gb_offset + len(gb_data)
    header = struct.pack("<8s6I", MAGIC, 1, ascii_offset, gb_offset,
                         GB_ROWS, index_offset, len(index))
    payload = header + ascii_data + gb_data + b"".join(
        struct.pack("<II", codepoint, slot) for codepoint, slot in index)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_bytes(payload)
    print(f"wrote {args.output}: {len(payload)} bytes, {len(index)} GB2312 glyphs")


if __name__ == "__main__":
    main()
