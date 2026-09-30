"""Blender reference of the exact exported GLB, using RF's diagnostic cameras.

Run in background with --python-exit-code 1, then -- source.glb output_directory.
No authoring file, mesh, rig, or export is modified.
"""
import argparse
import json
import math
from pathlib import Path
import struct
import sys
import bpy
from mathutils import Vector

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('source', type=Path)
parser.add_argument('output', type=Path)
parser.add_argument('--width', type=int, default=1280)
parser.add_argument('--height', type=int, default=720)
parser.add_argument('--distance', type=int, default=384)
args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
args.output.mkdir(parents=True, exist_ok=True)
raw = args.source.read_bytes()
size = struct.unpack_from('<I', raw, 12)[0]
document = json.loads(raw[20:20+size])
palette = [0xe6194b,0x3cb44b,0xffe119,0x4363d8,0xf58231,0x911eb4,0x42d4f4,
           0xf032e6,0xbfef45,0xfabed4,0x469990,0xdcbeff,0x9a6324,0xfffac8,
           0x800000,0xaaffc3,0x808000,0xffd8b1,0x000075]
bpy.ops.wm.read_factory_settings(use_empty=True)
bpy.ops.import_scene.gltf(filepath=str(args.source.resolve()))
scene = bpy.context.scene
scene.render.engine = 'BLENDER_EEVEE' if bpy.app.version >= (5,0,0) else 'BLENDER_EEVEE_NEXT'
scene.render.resolution_x = args.width
scene.render.resolution_y = args.height
scene.render.resolution_percentage = 100
scene.render.image_settings.file_format = 'PNG'
scene.render.film_transparent = True
scene.view_settings.view_transform = 'Standard'
scene.view_settings.look = 'None'
scene.view_settings.exposure = 0
scene.view_settings.gamma = 1
camera_data = bpy.data.cameras.new('RF fidelity camera')
camera = bpy.data.objects.new('RF fidelity camera', camera_data)
scene.collection.objects.link(camera)
scene.camera = camera
camera_data.sensor_fit = 'HORIZONTAL'
camera_data.sensor_width = 36
camera_data.lens = 27
camera_data.clip_start = 64/512
camera_data.clip_end = 100
# RF's camera right vector is (cos(yaw), 0, -sin(yaw)); its view basis has
# opposite handedness to Blender's camera. Match the actual projection,
# explicitly, rather than mirroring the asset or changing its rig.
camera.scale.x = -1

def linear(x):
    return x/12.92 if x <= 0.04045 else ((x+0.055)/1.055)**2.4

for mode in ('unlit', 'parts', 'studio'):
    for index, source in enumerate(document.get('materials', [])):
        material = bpy.data.materials.get(source.get('name', ''))
        if material is None:
            raise ValueError('Imported material name lost: '+source.get('name', ''))
        color = source.get('pbrMetallicRoughness', {}).get('baseColorFactor', [1,1,1,1])
        material.use_nodes = True
        nodes = material.node_tree.nodes
        nodes.clear()
        output = nodes.new('ShaderNodeOutputMaterial')
        if mode == 'parts':
            c = palette[index % len(palette)]
            color = [linear(((c >> shift) & 255)/255) for shift in (16,8,0)]+[1]
        shader = nodes.new('ShaderNodeEmission' if mode != 'studio' else 'ShaderNodeBsdfPrincipled')
        shader.inputs['Color' if mode != 'studio' else 'Base Color'].default_value = color
        if mode == 'studio':
            shader.inputs['Roughness'].default_value = 0.8
        material.node_tree.links.new(shader.outputs[0], output.inputs['Surface'])
    if mode == 'studio':
        world = bpy.data.worlds.new('Studio world')
        scene.world = world
        world.use_nodes = True
        world.node_tree.nodes['Background'].inputs['Color'].default_value = (0.6,0.6,0.6,1)
        world.node_tree.nodes['Background'].inputs['Strength'].default_value = 0.6
        light_data = bpy.data.lights.new('Key', 'AREA')
        light_data.energy = 100
        light_data.size = 3
        light = bpy.data.objects.new('Key', light_data)
        scene.collection.objects.link(light)
        light.location = (-2,-3,4)
        light.rotation_euler = (Vector((0,0,1.5))-light.location).to_track_quat('-Z','Y').to_euler()
    for name, angle in [('front',0),('quarter',math.pi/4),('side',math.pi/2)]:
        target = Vector((0,0,(-114+900)/512))
        camera.location = target+Vector((math.sin(angle)*args.distance/512,-math.cos(angle)*args.distance/512,0))
        camera.rotation_euler = (target-camera.location).to_track_quat('-Z','Y').to_euler()
        scene.render.filepath = str((args.output/f'{name}-{mode}.png').resolve())
        bpy.ops.render.render(write_still=True)
print('RF fidelity Blender reference complete')
