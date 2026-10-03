"""Package native PPM frames into a WebP animation; never synthesize frames."""
import argparse
import json
from pathlib import Path

from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("directory", type=Path)
    parser.add_argument("--width", type=int, default=960)
    args = parser.parse_args()
    record = json.loads((args.directory / "capture.json").read_text(encoding="utf-8"))
    if record["status"] != "passed" or not 320 <= args.width <= 1920:
        raise SystemExit("Need a passed native capture and width 320..1920")
    images = []
    for capture in record["captures"]:
        with Image.open(args.directory / capture["file"]) as source:
            frame = source.convert("RGB")
            if frame.width > args.width:
                frame = frame.resize(
                    (args.width, round(frame.height * args.width / frame.width)),
                    Image.Resampling.LANCZOS,
                )
            images.append(frame)
    destination = args.directory / "manufacturing.webp"
    images[0].save(
        destination, save_all=True, append_images=images[1:],
        duration=16 * record["capture_every"], loop=0,
        quality=85, method=4, minimize_size=False,
    )
    print(f"Native motion preview: {destination} ({len(images)} frames)")


if __name__ == "__main__":
    main()
