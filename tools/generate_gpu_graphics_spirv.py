#!/usr/bin/env python3
"""Compile HG-2A shaders; checked-in SPIR-V keeps builds SDK-free."""
import pathlib
import struct
import subprocess
import sys
import tempfile

compiler = sys.argv[1] if len(sys.argv) > 1 else 'glslangValidator'
output = ['/* Generated from gpu/shaders/graphics_v0.{vert,frag}. */']
with tempfile.TemporaryDirectory() as directory:
    for stage in ('vert', 'frag'):
        path = pathlib.Path(directory) / (stage + '.spv')
        subprocess.run([compiler, '-V', '--target-env', 'vulkan1.0', '-o', str(path),
                        'gpu/shaders/graphics_v0.' + stage], check=True)
        data = path.read_bytes()
        words = struct.unpack('<' + 'I' * (len(data)//4), data)
        output.append(f'static const uint32_t rf_graphics_{stage}_spirv[] = {{')
        output.extend('    ' + ', '.join(f'0x{v:08x}U' for v in words[i:i+8]) + ','
                      for i in range(0, len(words), 8))
        output.append('};')
pathlib.Path('gpu/src/rf_gpu_graphics_spirv.inc').write_text('\n'.join(output)+'\n')
