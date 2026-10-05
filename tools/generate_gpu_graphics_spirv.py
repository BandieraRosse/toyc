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
    for variant, stage in [('skin', 'comp'), ('scene','vert'), ('scene','frag'), ('scene_ray','frag'), ('scene_profile','frag'), ('scene_ray_profile','frag'), ('light_tiles','comp'), ('color','vert'), ('shadow','vert'), ('tonemap','comp'), ('sky','comp'), ('sky_noise','comp')]:
        path = pathlib.Path(directory) / (variant + stage + '.spv')
        source_variant = 'scene' if variant == 'color' or variant.startswith('scene') else variant
        defines = ['-DRF_SCENE_COLOR=1'] if variant == 'color' else []
        if variant.startswith('scene_ray'):
            defines = ['-DRF_ARCHITECTURE_RAY_QUERY=1']
        if variant.endswith('_profile'):
            defines.append('-DRF_LIGHT_PROFILE=1')
        target_env = 'vulkan1.2' if variant.startswith('scene_ray') else 'vulkan1.0'
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
