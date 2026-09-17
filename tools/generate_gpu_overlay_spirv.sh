#!/bin/sh
set -eu
validator=${1:-glslangValidator}
out=${2:-gpu/src/rf_gpu_overlay_spirv.inc}
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
for size in 16 8; do
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size \
        -o "$tmp_dir/overlay_$size.spv" gpu/shaders/overlay_composite.comp >/dev/null
    xxd -i "$tmp_dir/overlay_$size.spv" > "$tmp_dir/overlay_$size.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_overlay_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_overlay_${size}_spirv_len/" \
        "$tmp_dir/overlay_$size.inc"
done
{
    printf '/* Generated from gpu/shaders/overlay_composite.comp. */\n'
    cat "$tmp_dir/overlay_16.inc" "$tmp_dir/overlay_8.inc"
} > "$out"
