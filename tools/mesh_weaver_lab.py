#!/usr/bin/env python3
"""Generate the RF Mesh Weaver test plot in the existing Outpost campus."""
import argparse
from pathlib import Path

from experiment_lab import generate as standard_lab
from lab_computer import generate as computer

ROOT = Path(__file__).resolve().parents[1]
BEGIN = "# BEGIN MESH WEAVER LAB (tools/mesh_weaver_lab.py)"
END = "# END MESH WEAVER LAB"


def generate():
    name = "mesh_weaver_lab"
    area = name + "_area"
    lines = standard_lab(name, 68096, -33280, "model", "open",
                         width=6144, depth=6144, projection_beams=False).splitlines()
    lines = [line.replace("attr.text=MODEL_LAB", "attr.text=RF_MESH_WEAVER")
             for line in lines
             if "mesh_weaver_lab_button" not in line
             and "mesh_weaver_lab_sample_info" not in line]
    lines.extend(computer("mesh_weaver_rf1", -1280, 0, name,
                          "79DDE8", area).splitlines())
    lines.append(f"object id=mesh_weaver kind=mesh_weaver_frame x=0 y=0 z=0 yaw=0 scale=1000 attr.lab={area}")
    lines.append(f"object id=mesh_weaver_service_links kind=mesh_weaver_service_links x=0 y=0 z=0 yaw=0 scale=1000 attr.lab={area}")
    # Support/collision remain independent of the authored visual meshes.
    lines.append(f"collision id=mesh_weaver_base_col shape=box min_x=-440 max_x=440 min_z=-440 max_z=440 height=166 collision=true visible=false walkable=true color=526874 attr.lab={area}")
    for i, (x, z) in enumerate(((-340,-340),(340,-340),(340,340),(-340,340))):
        lines.append(f"collision id=mesh_weaver_post_{i} shape=box min_x={x-45} max_x={x+45} min_z={z-45} max_z={z+45} height=922 collision=true visible=false walkable=false color=526874 attr.lab={area}")
    lines.append(f"object id=mesh_weaver_power kind=power_unit x=1600 y=0 z=-600 yaw=0 scale=350 attr.collision=component attr.lab={area}")
    lines.append(f"render id=mesh_weaver_screen kind=sign min_x=-372 max_x=-212 min_z=510 max_z=510 height=-630 color=79DDE8 attr.height2=-526 attr.style=5 attr.text=RF_MESH_WEAVER attr.facing=+z attr.texture_u=160 attr.texture_v=104 attr.channel=mesh_weaver_screen attr.lab={area}")
    lines.append(f"render id=mesh_weaver_power_label kind=sign min_x=1130 max_x=2070 min_z=-115 max_z=-95 height=-500 color=FFB875 attr.height2=-220 attr.style=4 attr.text=RF_POWER attr.facing=+z attr.texture_u=3 attr.projection_beams=0 attr.channel=mesh_weaver_power attr.lab={area}")
    lines.append(f"render id=mesh_weaver_rf1_label kind=sign min_x=-1860 max_x=-750 min_z=510 max_z=530 height=-670 color=79DDE8 attr.height2=-460 attr.style=4 attr.text=RF1 attr.facing=+z attr.texture_u=1 attr.projection_beams=0 attr.channel=mesh_weaver_rf1 attr.lab={area}")
    for i, (x0,x1,z0,z1) in enumerate(((59904,79360,-28160,-25088),
                                      (59904,79360,-41472,-38400),
                                      (76288,79360,-38400,-28160))):
        identity = f"mesh_weaver_road_{i}"
        b = f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"
        # The northern connection already belongs to the five-weapon plot.
        if i == 0:
            continue
        lines.extend((f"surface id={identity} kind=ground {b} height=0 material=485860 attr.collision_id={identity}_col",
                      f"collision id={identity}_col shape=flat {b} height=0 collision=false visible=true walkable=true color=485860",
                      f"render id={identity}_paint kind=floor {b} height=0 color=485860 attr.style=11"))
    lines.append("region id=mesh_weaver_safe kind=safe min_x=59904 max_x=79360 min_z=-41472 max_z=-28160")
    return BEGIN + "\n" + "\n".join(lines) + "\n" + END + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path)
    parser.add_argument("--install", action="store_true")
    args = parser.parse_args()
    if args.install:
        path = ROOT / "rasterfall/assets/maps/outpost.map"
        original = path.read_bytes()
        bom = original.startswith(b"\xef\xbb\xbf")
        text = original.decode("utf-8-sig" if bom else "utf-8")
        newline = "\r\n" if "\r\n" in text else "\n"
        part = generate().replace("\n", newline)
        if BEGIN in text:
            start = text.index(BEGIN)
            finish = text.index(END, start) + len(END)
            finish += len(newline) if text[finish:].startswith(newline) else 0
            text = text[:start] + part + text[finish:]
        else:
            text = text.rstrip("\r\n") + newline + part
        path.write_bytes(text.encode("utf-8-sig" if bom else "utf-8"))
    elif args.output:
        with args.output.open("x", encoding="utf-8", newline="\n") as stream:
            stream.write(generate())
    else:
        print(generate(), end="")


if __name__ == "__main__":
    main()
