#!/bin/sh
set -eu

root=$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)
inspect="$root/build/map-inspect"
valid="$root/rasterfall/tests/map_v1_valid.map"
duplicate="$root/rasterfall/tests/map_v1_duplicate_id.map"
missing="$root/rasterfall/tests/map_v1_missing_field.map"
unknown_record="$root/rasterfall/tests/map_v1_unknown_record.map"
unknown_field="$root/rasterfall/tests/map_v1_unknown_field.map"
invalid_number="$root/rasterfall/tests/map_v1_invalid_number.map"
outside_world="$root/rasterfall/tests/map_v1_outside_world.map"
mkdir -p "$root/tmp"

output=$($inspect "$valid")
printf '%s\n' "$output" | grep -q '^parse success$'
printf '%s\n' "$output" | grep -q '^Regions: 1$'
printf '%s\n' "$output" | grep -q '^Collision: 1$'
printf '%s\n' "$output" | grep -q '^Interactions: 1$'
printf '%s\n' "$output" | grep -q '^Actor spawns: 1$'
printf '%s\n' "$output" | grep -q '^Pickups: 1$'
printf '%s\n' "$output" | grep -q '^Objects: 1$'

if $inspect "$duplicate" >"$root/tmp/map-parser-duplicate.out" 2>"$root/tmp/map-parser-duplicate.err"; then exit 1; fi
grep -q 'map_v1_duplicate_id.map:4:' "$root/tmp/map-parser-duplicate.err"
grep -q 'duplicate id arena' "$root/tmp/map-parser-duplicate.err"

if $inspect "$missing" >"$root/tmp/map-parser-missing.out" 2>"$root/tmp/map-parser-missing.err"; then exit 1; fi
grep -q 'map_v1_missing_field.map:3:' "$root/tmp/map-parser-missing.err"
grep -q 'collision missing max_z' "$root/tmp/map-parser-missing.err"

for case in unknown_record unknown_field invalid_number outside_world; do
	file=$(eval "printf '%s' \"\$$case\"")
	if "$inspect" "$file" >"$root/tmp/map-parser-$case.out" 2>"$root/tmp/map-parser-$case.err"; then exit 1; fi
done
grep -q 'unknown record type portal' "$root/tmp/map-parser-unknown_record.err"
grep -q 'unknown field mystery' "$root/tmp/map-parser-unknown_field.err"
grep -q 'invalid x' "$root/tmp/map-parser-invalid_number.err"
grep -q 'region bounds outside world' "$root/tmp/map-parser-outside_world.err"

capacity="$root/tmp/map-parser-capacity.map"
printf '%s\n' 'map version=1 units=rfu' 'world min_x=-1000 max_x=1000 min_z=-1000 max_z=1000' >"$capacity"
i=0
while [ "$i" -lt 65 ]; do
	printf 'region id=region_%d kind=test min_x=-900 max_x=900 min_z=-900 max_z=900\n' "$i" >>"$capacity"
	i=$((i + 1))
done
if "$inspect" "$capacity" >"$root/tmp/map-parser-capacity.out" 2>"$root/tmp/map-parser-capacity.err"; then exit 1; fi
grep -q 'region capacity exceeded' "$root/tmp/map-parser-capacity.err"
printf '%s\n' 'map parser tests: ok'
