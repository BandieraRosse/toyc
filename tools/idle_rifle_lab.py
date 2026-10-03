#!/usr/bin/env python3
"""Generate the live AI holding test plot south of the rifle cycle plot."""
import argparse
from pathlib import Path
from experiment_lab import generate as standard_lab


def generate():
    name = "idle_rifle_lab"
    area = name + "_area"
    lines = standard_lab(name, 48640, -33280, "animation", "open").splitlines()
    lines = [line.replace("attr.text=ANIMATION_LAB", "attr.text=LIVE_AI_HOLDS")
             for line in lines if "id=idle_rifle_lab_sample_info " not in line]
    lines = [line.replace("min_x=-2400 max_x=2400", "min_x=5700 max_x=8100")
             if "id=idle_rifle_lab_display " in line else line for line in lines]
    for i, label in enumerate(("BLOCK", "RIFLEMAN", "HEAVY", "SCOUT")):
        x = -2700 + i * 1800
        lines.append(f"render id=idle_rifle_lane_{i} kind=sign min_x={x-800} max_x={x+800} min_z=1600 max_z=1620 height=-650 color=79E8C5 attr.height2=-380 attr.style=4 attr.text={label}_LIVE_AI attr.facing=+z attr.texture_u=2 attr.channel=idle_rifle_lane_{i} attr.lab={area}")
    for i, (x0, x1, z0, z1) in enumerate(((40448, 59904, -41472, -38400),
                                         (56832, 59904, -38400, -28160))):
        identity = f"idle_rifle_road_{i}"
        bounds = f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"
        lines.extend((f"surface id={identity} kind=ground {bounds} height=0 material=485860 attr.collision_id={identity}_col",
                      f"collision id={identity}_col shape=flat {bounds} height=0 collision=false visible=true walkable=true color=485860",
                      f"render id={identity}_paint kind=floor {bounds} height=0 color=485860 attr.style=11"))
    bounds = "min_x=37376 max_x=59904 min_z=-41472 max_z=-25088"
    lines.extend((f"surface id=idle_rifle_site kind=ground {bounds} height=0 material=78858A attr.collision_id=idle_rifle_site_col",
                  f"collision id=idle_rifle_site_col shape=flat {bounds} height=0 collision=false visible=false walkable=true color=78858A",
                  "region id=idle_rifle_safe kind=safe min_x=40448 max_x=59904 min_z=-41472 max_z=-28160"))
    return "# BEGIN LIVE AI HOLD LAB (tools/idle_rifle_lab.py)\n" + "\n".join(lines) + "\n# END LIVE AI HOLD LAB\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        output.write(generate())
