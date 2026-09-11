#!/bin/sh
set -eu

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
printf '%s\n' "$formal_output" | grep -q '^interactions: 33$'
printf '%s\n' "$formal_output" | grep -q '^collisions: 70$'
printf '%s\n' "$formal_output" | grep -q '^surfaces: 32$'
printf '%s\n' "$formal_output" | grep -q '^spawns: 10$'
printf '%s\n' "$formal_output" | grep -q '^pickups: 11$'
printf '%s\n' "$formal_output" | grep -q '^objects: 10$'
printf '%s\n' "$formal_output" | grep -q '^renders: 98$'
printf '%s\n' "$formal_output" | grep -q '^safe: safe_start$'
printf '%s\n' 'map runtime tests: ok'
