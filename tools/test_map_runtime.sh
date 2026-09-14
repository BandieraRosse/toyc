#!/bin/sh
set -eu

# Catch capacity-sized Map IR locals overflowing the Windows startup stack.
ulimit -s 512

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
runtime="$root/build/map-runtime-test"
map="$root/rasterfall/tests/map_v1_runtime.map"

output=$($runtime "$map")
printf '%s\n' "$output" | grep -q '^runtime map success$'
printf '%s\n' "$output" | grep -q '^regions: 3$'
printf '%s\n' "$output" | grep -q '^interactions: 3$'
printf '%s\n' "$output" | grep -q '^collisions: 1$'
printf '%s\n' "$output" | grep -q '^surfaces: 1$'
printf '%s\n' "$output" | grep -q '^renders: 1$'
printf '%s\n' "$output" | grep -q '^safe: start_area$'
printf '%s\n' "$output" | grep -q '^interaction: wave_skip action=wave_skip$'
formal_output=$($runtime "$root/rasterfall/assets/maps/rasterfall.map")
printf '%s\n' "$formal_output" | grep -q '^runtime map success$'
printf '%s\n' "$formal_output" | grep -q '^regions: 9$'
printf '%s\n' "$formal_output" | grep -q '^interactions: 34$'
printf '%s\n' "$formal_output" | grep -q '^collisions: 330$'
printf '%s\n' "$formal_output" | grep -q '^surfaces: 32$'
printf '%s\n' "$formal_output" | grep -q '^spawns: 9$'
printf '%s\n' "$formal_output" | grep -q '^pickups: 11$'
printf '%s\n' "$formal_output" | grep -q '^objects: 134$'
printf '%s\n' "$formal_output" | grep -q '^renders: 83$'
printf '%s\n' "$formal_output" | grep -q '^safe: safe_start$'
outpost_output=$($runtime "$root/rasterfall/assets/maps/outpost.map")
printf '%s\n' "$outpost_output" | grep -q '^runtime map success$'
printf '%s\n' "$outpost_output" | grep -q '^regions: 2$'
printf '%s\n' "$outpost_output" | grep -q '^interactions: 1$'
printf '%s\n' "$outpost_output" | grep -q '^collisions: 5$'
printf '%s\n' "$outpost_output" | grep -q '^surfaces: 1$'
printf '%s\n' "$outpost_output" | grep -q '^renders: 12$'
printf '%s\n' "$outpost_output" | grep -q '^spawns: 0$'
printf '%s\n' "$outpost_output" | grep -q '^interaction: return_to_whu_v0 action=return_to_whu_v0$'
whu_output=$($runtime "$root/rasterfall/assets/maps/return_whu_planar_massing_v0.map")
printf '%s\n' "$whu_output" | grep -q '^identity: return_to_whu_v0$'
printf '%s\n' "$whu_output" | grep -q '^player_start: -7000 8000 facing=-724 -724$'
printf '%s\n' "$whu_output" | grep -q '^runtime map success$'
printf '%s\n' "$outpost_output" | grep -q '^identity: outpost$'
printf '%s\n' "$formal_output" | grep -q '^identity: campaign_01$'
printf '%s\n' 'map runtime tests: ok'

# Stable surface bindings resolve independently of source order and array slots.
python3 - "$root" <<'PYTEST'
from pathlib import Path
import subprocess
import re
import sys
import tempfile
root = Path(sys.argv[1])
exe = root / "build/map-runtime-test"
source = (root / "rasterfall/assets/maps/outpost.map").read_text()
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "surface.map"
    def run(text, error=None):
        path.write_text(text)
        result = subprocess.run([str(exe), str(path)], capture_output=True, text=True)
        if error:
            assert result.returncode != 0, result.stdout
            assert error in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr
    run(source)
    run(re.sub(r" attr.legacy_index=\d+", "", source))
    run("\n".join(reversed(source.splitlines())) + "\n")
    run(source.replace("attr.collision_id=outpost_floor_collision", "attr.collision_id=missing"),
        "does not reference a collision")
    run(source.replace("attr.collision_id=outpost_floor_collision", "attr.collision_id=outpost_floor"),
        "does not reference a collision")
    run(source.replace("attr.collision_id=outpost_floor_collision", "attr.legacy_index=0"),
        "surface legacy_index removed")
    surface = next(line for line in source.splitlines() if line.startswith("surface "))
    run(source + surface.replace("id=outpost_floor ", "id=duplicate_floor ") + "\n",
        "duplicate surface collision_id binding")
print("stable surface reference tests: ok")
PYTEST
