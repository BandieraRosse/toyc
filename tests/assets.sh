#!/bin/sh
set -eu
tool=build/toyasset
for f in assets/generated/test.ttex assets/generated/test.jpg.ttex assets/generated/test.tsnd assets/generated/test.tmesh assets/generated/uv_test.ttex assets/generated/wall.ttex; do
    "$tool" validate "$f"
    "$tool" inspect "$f" >/dev/null
done
cp /dev/null /tmp/toyasset-bad 2>/dev/null || true
if "$tool" validate /tmp/toyasset-bad >/dev/null 2>&1; then exit 1; fi
echo "asset tests: 4 formats, 5 files validated"
