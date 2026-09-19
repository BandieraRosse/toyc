#!/usr/bin/env python3
"""Generate the hosted HG-2 graphics ABI subset from Khronos vk.xml v1.3.290.

Usage: python3 tools/generate_gpu_graphics_abi.py path/to/vk.xml
No SDK is required to build the checked-in result. Existing backend handles
are aliases, so graphics and compute use the same device/queue/memory owner.
"""
import pathlib
import hashlib
import re
import sys
import xml.etree.ElementTree as ET

registry = pathlib.Path(sys.argv[1]).read_bytes()
if hashlib.sha256(registry).hexdigest() != '4e63ee09a641f311e7d666e1f5d61a40acdff5d84d9ebffaa4a0b14c9abcc427':
    raise ValueError('Expected unmodified Khronos Vulkan-Headers v1.3.290 registry/vk.xml')
root = ET.fromstring(registry)
types = {t.get('name') or t.findtext('name'): t for t in root.findall('./types/type')}
commands = {c.findtext('proto/name'): c for c in root.findall('./commands/command')}
names = '''GetPhysicalDeviceFormatProperties CreateImage DestroyImage
GetImageMemoryRequirements BindImageMemory CreateImageView DestroyImageView
CreateRenderPass DestroyRenderPass CreateFramebuffer DestroyFramebuffer
CreateGraphicsPipelines CmdBeginRenderPass CmdEndRenderPass CmdBindVertexBuffers
CmdBindIndexBuffer CmdDrawIndexed CmdSetViewport CmdSetScissor CmdPushConstants
CmdCopyImageToBuffer'''.split()
aliases = {
    'VkAllocationCallbacks': 'void', 'VkDeviceSize': 'uint64_t',
    'VkResult': 'rf_vk_result', 'VkBool32': 'rf_vk_bool32', 'VkSampleMask': 'uint32_t',
}
for name in ('PhysicalDevice Device DeviceMemory Buffer Image Pipeline PipelineCache '
             'PipelineLayout ShaderModule CommandBuffer').split():
    snake = re.sub(r'(?<!^)(?=[A-Z])', '_', name).lower()
    aliases['Vk' + name] = 'rf_vk_' + snake
out = ['/* Copyright 2015-2024 The Khronos Group Inc.',
       ' * SPDX-License-Identifier: Apache-2.0 OR MIT',
       ' * Generated from Khronos Vulkan-Headers v1.3.290 registry/vk.xml.',
       ' * Regenerate with tools/generate_gpu_graphics_abi.py. */',
       '#ifndef RF_VULKAN_GRAPHICS_MIN_H', '#define RF_VULKAN_GRAPHICS_MIN_H',
       '#include "rf_vulkan_min.h"']
seen = set()


def decl(element):
    return ''.join(element.itertext()).strip()


def emit(name):
    if not name or name in seen or not name.startswith('Vk'):
        return
    seen.add(name)
    if name in aliases:
        out.append(f'typedef {aliases[name]} {name};')
        return
    t = types[name]
    cat = t.get('category')
    if cat in ('struct', 'union'):
        members = [m for m in t.findall('member')
                   if m.get('api', 'vulkan') == 'vulkan']
        for member in members:
            emit(member.findtext('type'))
        out.append(f'typedef {cat} {name} {{')
        for member in members:
            # Comments are documentation, not part of a C declaration.
            comment = member.find('comment')
            if comment is not None:
                member.remove(comment)
            out.append('    ' + decl(member) + ';')
        out.append(f'}} {name};')
    elif cat == 'handle':
        out.append(f'typedef struct {name}_T *{name};')
    elif cat in ('enum', 'bitmask'):
        out.append(f'typedef uint32_t {name};')
    else:
        raise ValueError((name, cat))


emit('VkPushConstantRange')
for name in names:
    c = commands['vk' + name]
    emit(c.findtext('proto/type'))
    for p in c.findall('param'):
        emit(p.findtext('type'))
    out.append('typedef ' + c.findtext('proto/type') +
               f' (RF_VK_CALL *rf_gfx_{name}_fn)(' +
               ', '.join(decl(p) for p in c.findall('param')) + ');')

# Only constants actually consumed by the graphics implementation are emitted.
source = pathlib.Path('gpu/src/rf_gpu_vulkan_graphics.inc').read_text()
constants = set(re.findall(r'\bVK_[A-Z0-9_]+\b', source))
enums = {e.get('name'): e for e in root.findall('.//enums/enum')}
for name in sorted(constants):
    e = enums[name]
    value = e.get('value')
    if value is None:
        value = str(1 << int(e.get('bitpos')))
    out.append(f'#define {name} {value}')
out += ['#endif', '']
pathlib.Path('gpu/include/rf_vulkan_graphics_min.h').write_text('\n'.join(out))
