#!/usr/bin/env python3
"""Assemble existing --environment-capture BMPs; preserve original captures."""
import argparse
from pathlib import Path
from PIL import Image, ImageDraw


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("capture_dir", type=Path)
    args = parser.parse_args()
    names = ("base", "north", "west-facility", "east-facility", "west-route",
             "east-route", "hurd", "south", "ramp", "spawn", "power-yard", "north-facility")
    sheet = Image.new("RGB", (1920, ((len(names) + 2) // 3) * 384), (18, 23, 29))
    draw = ImageDraw.Draw(sheet)
    for i, name in enumerate(names):
        x, y = i % 3 * 640, i // 3 * 384
        with Image.open(args.capture_dir / (name + ".bmp")) as source:
            source.convert("RGB").save(args.capture_dir / (name + ".png"))
            sheet.paste(source.resize((640, 360)), (x, y + 24))
        draw.text((x + 12, y + 6), name, fill=(230, 230, 220))
    sheet.save(args.capture_dir / "sheet.png")
    print(args.capture_dir / "sheet.png")


if __name__ == "__main__":
    main()
