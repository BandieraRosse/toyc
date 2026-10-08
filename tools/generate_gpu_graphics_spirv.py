#!/usr/bin/env python3
"""Compile GPU-GRAPHICS shaders; checked-in SPIR-V keeps builds SDK-free."""
import pathlib
import struct
import subprocess
import sys
import tempfile

compiler = sys.argv[1] if len(sys.argv) > 1 else 'glslangValidator'
output = ['/* Generated from gpu/shaders/graphics_scene, graphics_sky, graphics_shadow, graphics_tonemap and graphics_skin. */']
with tempfile.TemporaryDirectory() as directory:
    for variant, stage in [('skin', 'comp'), ('scene','vert'), ('scene','frag'), ('scene_ray','frag'), ('scene_profile','frag'), ('scene_ray_profile','frag'), ('scene_ray_loop','frag'), ('scene_ray_loop_profile','frag'), ('depth','frag'), ('light_tiles','comp'), ('indirect','comp'), ('indirect_ray','comp'), ('daylight','comp'), ('daylight_ray','comp'), ('receiver','comp'), ('receiver_ray','comp'), ('environment','comp'), ('color','vert'), ('shadow','vert'), ('tonemap','comp'), ('sky','comp'), ('sky_noise','comp'), ('baked','comp'), ('baked_ray','comp')]:
        path = pathlib.Path(directory) / (variant + stage + '.spv')
        source_variant = 'scene' if variant == 'color' or variant.startswith('scene') else variant
        if variant in ('indirect_ray', 'daylight', 'daylight_ray'):
            source_variant = 'indirect'
        defines = ['-DRF_SCENE_COLOR=1'] if variant == 'color' else []
        if variant in ('receiver_ray', 'baked_ray'):
            source_variant = variant.removesuffix('_ray')
        if variant.startswith('scene_ray') or variant in ('indirect_ray', 'daylight_ray', 'receiver_ray', 'baked_ray'):
            defines = ['-DRF_ARCHITECTURE_RAY_QUERY=1']
        if variant in ('scene_ray', 'scene_ray_profile'):
            defines.append('-DRF_ARCHITECTURE_SINGLE_PROCEED=1')
        if variant.startswith('daylight'):
            defines.append('-DRF_DAYLIGHT_PROBES=1')
        if variant.endswith('_profile'):
            defines.append('-DRF_LIGHT_PROFILE=1')
        target_env = 'vulkan1.2' if variant.startswith('scene_ray') or variant in ('indirect_ray', 'daylight_ray', 'receiver_ray', 'baked_ray') else 'vulkan1.0'
        subprocess.run([compiler, '-V', '--target-env', target_env, '-o', str(path)] + defines +
                       ['gpu/shaders/graphics_' + source_variant + '.' + stage], check=True)
        data = path.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data)//4), data)
        name = stage if variant == 'v0' else variant + '_' + stage
        output.append(f'static const uint32_t rf_graphics_{name}_spirv[] = {{')
        output.extend('    ' + ', '.join(f'0x{v:08x}U' for v in words[i:i+8]) + ','
                      for i in range(0, len(words), 8))
        output.append('};')
target = pathlib.Path('gpu/src/rf_gpu_graphics_spirv.inc')
newline = '\r\n' if b'\r\n' in target.read_bytes() else '\n'
target.write_bytes((newline.join(output)+newline).encode('utf-8'))
