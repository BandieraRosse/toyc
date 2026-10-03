#!/usr/bin/env python3
"""Emit the Outpost rifle cycle plot and its east-side road connection."""
import argparse
from pathlib import Path
from experiment_lab import generate as standard_lab


def generate():
    name = "rifle_cycle_lab"
    area = name + "_area"
    lines = standard_lab(name, 48640, -19968, "animation", "open").splitlines()
    lines = [line.replace("attr.text=ANIMATION_LAB", "attr.text=AI_RIFLE_CYCLE")
             for line in lines if "id=rifle_cycle_lab_sample_info " not in line]
    # Keep the entrance status screen beside the lanes, clear of the actors
    # as they approach the north turnaround point.
    lines = [line.replace("min_x=-2400 max_x=2400", "min_x=5700 max_x=8100")
             if "id=rifle_cycle_lab_display " in line else line for line in lines]
    for i, label in enumerate(("BLOCK", "HUMANOID", "RF-C01")):
        x = -3000 + i * 3000
        lines.append(f"render id=rifle_cycle_lane_{i} kind=sign min_x={x-1250} max_x={x+1250} min_z=3300 max_z=3320 height=-650 color=79E8C5 attr.height2=-380 attr.style=4 attr.text={label}_AIM_FIRE_MOVE attr.facing=+z attr.texture_u=2 attr.channel=rifle_cycle_lane_{i} attr.lab={area}")
    # Roads meet the existing first-row east edge; their visible rectangles
    # are disjoint. Invisible support overlaps the old road to cover foot seams.
    for i, (x0,x1,z0,z1) in enumerate(((40448,59904,-14848,-11776),
                                      (40448,59904,-28160,-25088),
                                      (56832,59904,-25088,-14848))):
        identity = f"rifle_cycle_road_{i}"
        bounds = f"min_x={x0} max_x={x1} min_z={z0} max_z={z1}"
        lines.extend((f"surface id={identity} kind=ground {bounds} height=0 material=485860 attr.collision_id={identity}_col",
                      f"collision id={identity}_col shape=flat {bounds} height=0 collision=false visible=true walkable=true color=485860",
                      f"render id={identity}_paint kind=floor {bounds} height=0 color=485860 attr.style=11"))
    bounds = "min_x=37376 max_x=59904 min_z=-28160 max_z=-11776"
    lines.extend((f"surface id=rifle_cycle_site kind=ground {bounds} height=0 material=78858A attr.collision_id=rifle_cycle_site_col",
                  f"collision id=rifle_cycle_site_col shape=flat {bounds} height=0 collision=false visible=false walkable=true color=78858A",
                  "region id=rifle_cycle_safe kind=safe min_x=40448 max_x=59904 min_z=-28160 max_z=-11776"))
    return "# BEGIN RIFLE CYCLE LAB (tools/rifle_cycle_lab.py)\n" + "\n".join(lines) + "\n# END RIFLE CYCLE LAB\n"


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    with args.output.open("x", encoding="utf-8", newline="\n") as output:
        output.write(generate())
