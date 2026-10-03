#!/usr/bin/env python3
"""Emit the 3-character by 5-firearm animation comparison plot."""
import argparse
from pathlib import Path
from experiment_lab import generate as standard_lab


def generate():
    name = "weapon_cycle_lab"
    area = name + "_area"
    lines = standard_lab(name, 68096, -19968, "animation", "open",
                         width=14336).splitlines()
    lines = [line.replace("attr.text=ANIMATION_LAB", "attr.text=WEAPON_CYCLE_3_X_5")
             for line in lines if "id=weapon_cycle_lab_sample_info " not in line]
    for row, body in enumerate(("BLOCK", "HUMANOID", "RF-C01")):
        for column, weapon in enumerate(("PISTOL", "SMG", "SHOTGUN", "AK", "AWP")):
            x, z = -5200+column*2600, 2600-row*2600
            station = row*5+column
            lines.append(f"render id=weapon_cycle_station_{station} kind=sign min_x={x-1150} max_x={x+1150} min_z={z+850} max_z={z+870} height=-730 color=79E8C5 attr.height2=-460 attr.style=4 attr.text={body}_{weapon} attr.facing=+z attr.texture_u=2 attr.channel=weapon_cycle_station_{station} attr.lab={area}")
    for i, (x0, x1, z0, z1) in enumerate(((59904,79360,-14848,-11776),
                                         (59904,79360,-28160,-25088),
                                         (76288,79360,-25088,-14848))):
        identity = f"weapon_cycle_road_{i}"
        bounds = f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"
        lines.extend((f"surface id={identity} kind=ground {bounds} height=0 material=485860 attr.collision_id={identity}_col",
                      f"collision id={identity}_col shape=flat {bounds} height=0 collision=false visible=true walkable=true color=485860",
                      f"render id={identity}_paint kind=floor {bounds} height=0 color=485860 attr.style=11"))
    lines.append("region id=weapon_cycle_safe kind=safe min_x=59904 max_x=79360 min_z=-28160 max_z=-11776")
    return "# BEGIN WEAPON CYCLE LAB (tools/weapon_cycle_lab.py)\n" + "\n".join(lines) + "\n# END WEAPON CYCLE LAB\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        output.write(generate())
