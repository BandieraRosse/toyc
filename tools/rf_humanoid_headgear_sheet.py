#!/usr/bin/env python3
"""Capture and compare RF Humanoid V2 headgear coverage variants.

The Rasterfall CLI remains the renderer. This helper only runs the existing
Character Acceptance / World Capture paths and arranges their BMP evidence
into compact comparison PNGs.
"""

import argparse
import subprocess
from pathlib import Path

from character_lab_sheet import (MODES, VIEWS, assemble, read_bmp,
                                 resize_rgb, write_png)
from character_world_sheet import (DISTANCES, POSES, assemble as assemble_world)


VARIANTS = (
    ("bare", "rf_humanoid_v2.rmesh"),
    ("headset", "rf_humanoid_v2_headset.rmesh"),
    ("patrol-cap", "rf_humanoid_v2_patrol_cap.rmesh"),
    ("goggles", "rf_humanoid_v2_goggles.rmesh"),
    ("respirator", "rf_humanoid_v2_respirator.rmesh"),
    ("tactical-helmet", "rf_humanoid_v2_tactical_helmet.rmesh"),
    ("engineering-helmet", "rf_humanoid_v2_engineering_helmet.rmesh"),
)

LAB_ROWS = (
    ("bind-front", "bind", "front"),
    ("bind-three-quarter", "bind", "three-quarter"),
    ("rifle-idle-three-quarter", "rifle-idle", "three-quarter"),
    ("rifle-aim-front", "rifle-aim", "front"),
)


def assemble_grid(paths, columns, cell_width):
    source = [read_bmp(path) for path in paths]
    width, height, _ = source[0]
    cell_height = max(1, height * cell_width // width)
    cells = [resize_rgb(image, cell_width, cell_height) for image in source]
    rows = (len(cells) + columns - 1) // columns
    sheet = bytearray(cell_width * columns * cell_height * rows * 3)
    for index, (_, _, pixels) in enumerate(cells):
        row, column = divmod(index, columns)
        for y in range(cell_height):
            dst = ((row * cell_height + y) * cell_width * columns +
                   column * cell_width) * 3
            src = y * cell_width * 3
            sheet[dst:dst + cell_width * 3] = pixels[src:src + cell_width * 3]
    return cell_width * columns, cell_height * rows, sheet


def capture_lab(args, name, model, directory):
    expected = [directory / mode / f"{view}.bmp"
                for mode in MODES for view in VIEWS]
    if args.capture:
        return
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.rasterfall, "--character-acceptance", str(model),
                    str(directory)], check=True)
    missing = [path for path in expected if not path.is_file()]
    if missing:
        raise SystemExit("missing Character Lab capture(s): " +
                         ", ".join(map(str, missing)))


def capture_world(args, name, model, directory):
    expected = [directory / f"{distance}-{pose}.bmp"
                for distance in DISTANCES for pose in POSES]
    if args.capture_world:
        return
    directory.mkdir(parents=True, exist_ok=True)
    subprocess.run([args.rasterfall, "--character-world-capture", str(directory),
                    "--character-world-model", str(model)], check=True)
    missing = [path for path in expected if not path.is_file()]
    if missing:
        raise SystemExit("missing world capture(s): " +
                         ", ".join(map(str, missing)))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--model-dir",
                        default="rasterfall/private-assets/models")
    parser.add_argument("--output",
                        default="tmp/rf-headgear-v1/headgear-lab.png")
    parser.add_argument("--capture-root",
                        help="reuse existing captures below this directory")
    parser.add_argument("--capture", action="store_true",
                        help="reuse captures already present below capture-root")
    parser.add_argument("--capture-world", action="store_true",
                        help="reuse existing world captures below capture-root")
    parser.add_argument("--rasterfall", default="build/rasterfall")
    parser.add_argument("--cell-width", type=int, default=260)
    parser.add_argument("--world", action="store_true",
                        help="also capture and compare representative world distances")
    parser.add_argument("--world-variants",
                        default="bare,goggles,respirator,tactical-helmet,engineering-helmet")
    args = parser.parse_args()

    model_dir = Path(args.model_dir)
    output = Path(args.output)
    root = Path(args.capture_root or output.with_suffix("") / "captures")
    root.mkdir(parents=True, exist_ok=True)

    lab_paths = {}
    for name, filename in VARIANTS:
        model = model_dir / filename
        capture_dir = root / "lab" / name
        capture_lab(args, name, model, capture_dir)
        lab_paths[name] = capture_dir
        lab_output = root / "individual" / f"{name}.png"
        lab_output.parent.mkdir(parents=True, exist_ok=True)
        paths = [capture_dir / mode / f"{view}.bmp"
                 for mode in MODES for view in VIEWS]
        write_png(lab_output, assemble(paths, args.cell_width))

    comparison = []
    for _, mode, view in LAB_ROWS:
        for name, _ in VARIANTS:
            comparison.append(lab_paths[name] / mode / f"{view}.bmp")
    comparison_output = output
    comparison_output.parent.mkdir(parents=True, exist_ok=True)
    write_png(comparison_output,
              assemble_grid(comparison, len(VARIANTS), args.cell_width))
    print(f"headgear lab comparison: {comparison_output}")

    if not args.world:
        return

    requested = {item.strip() for item in args.world_variants.split(",")
                 if item.strip()}
    world_paths = {}
    for name, filename in VARIANTS:
        if name not in requested:
            continue
        model = model_dir / filename
        capture_dir = root / "world" / name
        capture_world(args, name, model, capture_dir)
        world_paths[name] = capture_dir
        world_output = root / "individual-world" / f"{name}.png"
        world_output.parent.mkdir(parents=True, exist_ok=True)
        paths = [capture_dir / f"{distance}-{pose}.bmp"
                 for distance in DISTANCES for pose in POSES]
        write_png(world_output, assemble_world(paths, args.cell_width))

    world_names = [name for name, _ in VARIANTS if name in world_paths]
    world_comparison = []
    for distance in DISTANCES:
        for name in world_names:
            world_comparison.append(world_paths[name] / f"{distance}-idle.bmp")
    world_output = root.parent / "headgear-world-idle.png"
    write_png(world_output,
              assemble_grid(world_comparison, len(world_names), args.cell_width))
    print(f"headgear world comparison: {world_output}")


if __name__ == "__main__":
    main()
