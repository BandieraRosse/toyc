#!/bin/sh
set -eu
validator=${1:-glslangValidator}
out=${2:-gpu/src/rf_gpu_post_spirv.inc}
tmp_dir=$(mktemp -d)
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
for size in 16 8; do
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size -o "$tmp_dir/post_$size.spv" gpu/shaders/post_raster_v1.comp >/dev/null
    xxd -i "$tmp_dir/post_$size.spv" > "$tmp_dir/post_$size.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_post_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_post_${size}_spirv_len/" "$tmp_dir/post_$size.inc"
done
{
    printf '/* Generated from gpu/shaders/post_raster_v1.comp. */\n'
    cat "$tmp_dir/post_16.inc" "$tmp_dir/post_8.inc"
} > "$out"
