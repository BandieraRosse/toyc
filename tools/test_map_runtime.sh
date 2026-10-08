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
printf '%s\n' "$outpost_output" | grep -q '^interactions: 0$'
printf '%s\n' "$outpost_output" | grep -q '^safe: outpost_safe$'
printf '%s\n' "$outpost_output" | grep -q '^spawns: 0$'
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
source = (root / "rasterfall/assets/maps/outpost.map").read_text(encoding="utf-8")
surface = next(line for line in source.splitlines() if line.startswith("surface "))
surface_id = re.search(r"\bid=(\S+)", surface).group(1)
binding = re.search(r"attr.collision_id=\S+", surface).group(0)
with tempfile.TemporaryDirectory() as folder:
    path = Path(folder) / "surface.map"
    def run(text, error=None):
        path.write_text(text, encoding="utf-8")
        result = subprocess.run([str(exe), str(path)], capture_output=True, text=True)
        if error:
            assert result.returncode != 0, result.stdout
            assert error in result.stderr, result.stderr
        else:
            assert result.returncode == 0, result.stderr
    run(source)
    run(re.sub(r" attr.legacy_index=\d+", "", source))
    run("\n".join(reversed(source.splitlines())) + "\n")
    run(source.replace(binding, "attr.collision_id=missing", 1),
        "does not reference a collision")
    run(source.replace(binding, "attr.collision_id=" + surface_id, 1),
        "does not reference a collision")
    run(source.replace(binding, "attr.legacy_index=0", 1),
        "surface legacy_index removed")
    run(source + surface.replace("id=" + surface_id + " ", "id=duplicate_floor ", 1) + "\n",
        "duplicate surface collision_id binding")
print("stable surface reference tests: ok")
PYTEST
