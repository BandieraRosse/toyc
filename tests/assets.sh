#!/bin/sh
set -eu
tool=build/toyasset
count=0
# 自动发现 assets/generated/ 下的全部资产文件逐个校验，
# 新增资产无需改动本脚本；目录为空或混入非资产文件时校验会失败。
for f in assets/generated/*; do
    "$tool" validate "$f"
    "$tool" inspect "$f" >/dev/null
    count=$((count + 1))
done
cp /dev/null /tmp/toyasset-bad 2>/dev/null || true
if "$tool" validate /tmp/toyasset-bad >/dev/null 2>&1; then exit 1; fi
echo "asset tests: TTEX/TSND/TMES, $count files validated"
