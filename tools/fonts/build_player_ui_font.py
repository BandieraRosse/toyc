#!/usr/bin/env python3
"""Build an OFL Noto Sans CJK SC glyph-run atlas for the player UI.

Runtime requires neither FreeType nor an operating-system font installation.
The OTF is an offline build input and is not packaged or committed.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct

from PIL import Image, ImageDraw, ImageFont


def codepoints() -> list[int]:
    values = set(range(32, 127)) | {0x00B7, 0xFFFD, 0x2026, 0x2013, 0x2014, 0x2190, 0x2191, 0x2192, 0x2193}
    for first in range(0xA1, 0xF8):
        for second in range(0xA1, 0xFF):
            try:
                values.add(ord(bytes((first, second)).decode("gb2312")))
            except UnicodeDecodeError:
                pass
    return sorted(values)


def glyph_rectangles(font: ImageFont.FreeTypeFont, codepoint: int):
    char = chr(codepoint)
    advance = max(1, int(font.getlength(char) + 0.9999))
    image = Image.new("L", (max(32, advance), 20))
    ImageDraw.Draw(image).text((0, 16), char, font=font, fill=255, anchor="ls")
    # Three nonzero coverage values retain antialiasing with bounded geometry.
    pixels = image.load()
    rectangles: list[list[int]] = []
    previous = {}
    for y in range(image.height):
        current = {}
        x = 0
        while x < image.width:
            alpha = min(255, ((pixels[x, y] + 42) // 85) * 85)
            if not alpha:
                x += 1
                continue
            end = x + 1
            while end < image.width and min(255, ((pixels[end, y] + 42) // 85) * 85) == alpha:
                end += 1
            key = (x, end - x, alpha)
            if key in previous:
                index = previous[key]
                rectangles[index][3] += 1
            else:
                index = len(rectangles)
                rectangles.append([x, y, end - x, 1, alpha])
            current[key] = index
            x = end
        previous = current
    return advance, rectangles


def build(source: Path, output: Path):
    font = ImageFont.truetype(str(source), 18)
    characters = codepoints()
    entries = bytearray()
    spans = bytearray()
    maximum = 0
    for codepoint in characters:
        advance, rectangles = glyph_rectangles(font, codepoint)
        entries += struct.pack("<IIII", codepoint, advance, len(spans) // 5, len(rectangles))
        maximum = max(maximum, len(rectangles))
        for rectangle in rectangles:
            spans += struct.pack("<BBBBB", *rectangle)
    header = struct.pack("<8sIIIIII", b"RFUIS18\0", 1, 20, len(characters), 32, 32 + len(entries), len(spans) // 5)
    output.parent.mkdir(parents=True, exist_ok=True)
    output.write_bytes(header + entries + spans)
    manifest = {
        "name": "Rasterfall UI Sans 18",
        "upstream": "Noto Sans CJK SC Regular",
        "source_url": "https://raw.githubusercontent.com/notofonts/noto-cjk/Sans2.004/Sans/OTF/SimplifiedChinese/NotoSansCJKsc-Regular.otf",
        "source_sha256": hashlib.sha256(source.read_bytes()).hexdigest(),
        "copyright": "Copyright 2014-2021 Adobe (http://www.adobe.com/).",
        "license": "SIL Open Font License 1.1",
        "pixel_size": 18, "line_height": 20, "baseline": 16,
        "glyphs": len(characters), "rectangles": len(spans) // 5,
        "maximum_rectangles_per_glyph": maximum,
        "atlas_sha256": hashlib.sha256(output.read_bytes()).hexdigest(),
        "generator": "tools/fonts/build_player_ui_font.py",
    }
    output.with_suffix(".json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print(f"player-ui-font: {len(characters)} glyphs, {len(spans) // 5} rectangles, {output.stat().st_size} bytes")


def check(output: Path):
    data = output.read_bytes()
    magic, version, height, count, index, spans, total = struct.unpack_from("<8sIIIIII", data)
    assert magic == b"RFUIS18\0" and version == 1 and height == 20
    assert index == 32 and spans == 32 + count * 16 and len(data) == spans + total * 5
    actual = []
    for i in range(count):
        cp, advance, first, length = struct.unpack_from("<IIII", data, index + i * 16)
        assert 0 < advance <= 32 and first + length <= total
        actual.append(cp)
        for j in range(first, first + length):
            x, y, w, h, alpha = struct.unpack_from("<BBBBB", data, spans + j * 5)
            assert w and h and x + w <= 32 and y + h <= height and alpha in (85, 170, 255)
    assert actual == codepoints()
    manifest = json.loads(output.with_suffix(".json").read_text(encoding="utf-8"))
    assert manifest["atlas_sha256"] == hashlib.sha256(data).hexdigest()
    print(f"player-ui-font: checked {count} glyphs and {total} cached rectangles")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path)
    parser.add_argument("--output", type=Path, default=Path("rasterfall/assets/fonts/ui-sans18.rff"))
    parser.add_argument("--check", action="store_true")
    args = parser.parse_args()
    if args.check:
        check(args.output)
    else:
        if not args.source:
            parser.error("--source must name the official NotoSansCJKsc-Regular.otf build input")
        build(args.source, args.output)


if __name__ == "__main__":
    main()
