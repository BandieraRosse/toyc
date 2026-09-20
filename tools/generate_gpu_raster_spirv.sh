#!/bin/sh
set -eu
validator=${1:-glslangValidator}
out=${2:-gpu/src/rf_gpu_raster_v1_spirv.inc}
full_out=${3:-gpu/src/rf_gpu_raster_v1_full_spirv.inc}
image_out=${4:-gpu/src/rf_gpu_raster_v1_image_spirv.inc}
tmp_dir=$(mktemp -d "${RF_GPU_TMPDIR:-/tmp}/rf-gpu-raster.XXXXXXXXXX")
trap 'rm -rf "$tmp_dir"' EXIT HUP INT TERM
for size in 16 8; do
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size \
        -o "$tmp_dir/rf_gpu_raster_v1_${size}.spv" \
        gpu/shaders/raster_v1.comp >/dev/null
    xxd -i "$tmp_dir/rf_gpu_raster_v1_${size}.spv" \
        > "$tmp_dir/rf_gpu_raster_v1_${size}.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_raster_v1_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_raster_v1_${size}_spirv_len/" \
        "$tmp_dir/rf_gpu_raster_v1_${size}.inc"
done
{
    printf '/* Generated from gpu/shaders/raster_v1.comp. */\n'
    cat "$tmp_dir/rf_gpu_raster_v1_16.inc"
    cat "$tmp_dir/rf_gpu_raster_v1_8.inc"
} > "$out"
for size in 16 8; do
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size \
        -o "$tmp_dir/rf_gpu_raster_v1_full_${size}.spv" \
        gpu/shaders/raster_v1_full_scan.comp >/dev/null
    xxd -i "$tmp_dir/rf_gpu_raster_v1_full_${size}.spv" \
        > "$tmp_dir/rf_gpu_raster_v1_full_${size}.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_raster_v1_full_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_raster_v1_full_${size}_spirv_len/" \
        "$tmp_dir/rf_gpu_raster_v1_full_${size}.inc"
done
{
    printf '/* Generated from gpu/shaders/raster_v1_full_scan.comp. */\n'
    cat "$tmp_dir/rf_gpu_raster_v1_full_16.inc"
    cat "$tmp_dir/rf_gpu_raster_v1_full_8.inc"
} > "$full_out"
for size in 16 8; do
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size -DRF_RASTER_IMAGE_COLOR=1 \
        -o "$tmp_dir/rf_gpu_raster_v1_image_${size}.spv" gpu/shaders/raster_v1.comp >/dev/null
    xxd -i "$tmp_dir/rf_gpu_raster_v1_image_${size}.spv" > "$tmp_dir/rf_gpu_raster_v1_image_${size}.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_raster_v1_image_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_raster_v1_image_${size}_spirv_len/" "$tmp_dir/rf_gpu_raster_v1_image_${size}.inc"
    "$validator" -V -DRF_RASTER_LOCAL_SIZE=$size -DRF_RASTER_IMAGE_COLOR=1 \
        -o "$tmp_dir/rf_gpu_raster_v1_full_image_${size}.spv" gpu/shaders/raster_v1_full_scan.comp >/dev/null
    xxd -i "$tmp_dir/rf_gpu_raster_v1_full_image_${size}.spv" > "$tmp_dir/rf_gpu_raster_v1_full_image_${size}.inc"
    sed -i "s/unsigned char .*\[\]/_Alignas(4) static const unsigned char rf_gpu_raster_v1_full_image_${size}_spirv[]/; s/unsigned int .*_len/static const unsigned int rf_gpu_raster_v1_full_image_${size}_spirv_len/" "$tmp_dir/rf_gpu_raster_v1_full_image_${size}.inc"
done
{
    printf '/* Generated image-color Raster variants. */\n'
    cat "$tmp_dir/rf_gpu_raster_v1_image_16.inc" "$tmp_dir/rf_gpu_raster_v1_image_8.inc"
    cat "$tmp_dir/rf_gpu_raster_v1_full_image_16.inc" "$tmp_dir/rf_gpu_raster_v1_full_image_8.inc"
} > "$image_out"
