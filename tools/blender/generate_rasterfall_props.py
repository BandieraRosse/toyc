"""Generate Rasterfall industrial kit; run with Blender 3.6+ (no add-ons).

blender -b --python-exit-code 1 --python tools/blender/generate_rasterfall_props.py
Options after --: --output DIR --all-glb --overwrite --assets ID [ID ...]
Source coordinates: metres, Z up, -Y forward, bottom-centre origin.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import sys
import zlib

import bpy
from mathutils import Vector


# name, width/depth/height in metres, recommended maximum triangles
SPECS = [
    ("rf_crate", (1.2, 1.0, 1.0), 600),
    ("rf_barrier", (2.4, .8, 1.0), 600),
    ("rf_short_wall", (2.4, .5, 1.4), 600),
    ("rf_railing", (2.4, .3, 1.1), 600),
    ("rf_lamp_post", (.8, .8, 3.2), 600),
    ("rf_vent_unit", (1.4, .9, 1.2), 800),
    ("rf_workbench", (1.8, .8, .9), 600),
    ("rf_ammo_container", (.9, .5, .6), 600),
    ("rf_industrial_pillar", (.8, .8, 2.8), 600),
    ("rf_pipe_module", (1.6, .8, 1.4), 800),
]

# Industrial palette in sRGB. Full atlases remain only as pilot A/B sources;
# normal production crops the local sign tile and uses flat surface materials.
PILOT = {'rf_crate': (128, (118, 125, 99), '01'),
         'rf_workbench': (256, (143, 127, 91), '02'),
         'rf_vent_unit': (256, (102, 125, 139), '03'),
         'rf_barrier': (128, (153, 139, 106), '04'),
         'rf_short_wall': (128, (100, 119, 129), '05'),
         'rf_railing': (128, (76, 85, 88), '06'),
         'rf_lamp_post': (128, (93, 111, 121), '07'),
         'rf_ammo_container': (128, (105, 119, 93), '08'),
         'rf_industrial_pillar': (128, (85, 99, 108), '09'),
         'rf_pipe_module': (128, (80, 89, 91), '10')}
STRUCTURE = (57, 65, 68)
INK = (213, 211, 191)
ACCENTS = {'rf_barrier': (177, 92, 71), 'rf_railing': INK,
           'rf_lamp_post': (229, 214, 172), 'rf_ammo_container': (156, 76, 64),
           'rf_industrial_pillar': (173, 133, 73), 'rf_pipe_module': (180, 112, 66)}


def albedo_material(name, out, label_only=False):
    """Eight padded, reusable face tiles; no noise, fonts or external assets."""
    size, body, number = PILOT[name]
    if label_only and name != 'rf_crate':
        size = 128  # crop to 32, independent of the old full-atlas resolution
    tile = size // 4
    pixels = bytearray(bytes((*body, 255)) * size * size)

    def rect(tile_id, x, y, w, h, color):
        # Author in a common 32px tile grid, top to bottom like PNG/glTF UV.
        ox, oy = (tile_id % 4) * tile, (tile_id // 4) * tile
        k = tile / 32
        for py in range(int(oy+y*k), int(oy+(y+h)*k)):
            for px in range(int(ox+x*k), int(ox+(x+w)*k)):
                at = (py*size+px)*4
                pixels[at:at+4] = bytes((*color, 255))

    colors = [body, STRUCTURE, body, body, body, STRUCTURE, body, body]
    for t, color in enumerate(colors):
        rect(t, 0, 0, 32, 32, color)
    # Body panel, dust collecting at its foot, two broad contact abrasions.
    rect(0, 3, 4, 26, 23, tuple(min(255, c+9) for c in body))
    rect(0, 2, 27, 28, 3, tuple(int(c*.83) for c in body))
    rect(0, 4, 26, 7, 1, (157, 153, 128))
    rect(0, 22, 28, 5, 1, (157, 153, 128))
    # Recessed lid / work surface: large tool zone and a restrained oil stain.
    rect(2, 3, 3, 26, 26, STRUCTURE)
    rect(2, 5, 5, 22, 22, tuple(int(c*.87) for c in body))
    rect(2, 19, 21, 6, 4, tuple(int(c*.70) for c in body))
    # Nameplate. Coarse 3x5 numerals survive downsampling without fine text.
    rect(3, 2, 3, 28, 26, STRUCTURE)
    digits = {'0': ['111','101','101','101','111'],
              '1': ['010','110','010','010','111'],
              '2': ['111','001','111','100','111'],
              '3': ['111','001','111','001','111'],
              '4': ['101','101','111','001','001'],
              '5': ['111','100','111','001','111'],
              '6': ['111','100','111','101','111'],
              '7': ['111','001','010','010','010'],
              '8': ['111','101','111','101','111'],
              '9': ['111','101','111','001','111']}
    for j, digit in enumerate(number):
        for y, row in enumerate(digits[digit]):
            for x, bit in enumerate(row):
                if bit == '1':
                    rect(3, 5+j*12+x*3, 8+y*3, 3, 3, INK)
    if name in ('rf_barrier', 'rf_pipe_module', 'rf_railing', 'rf_lamp_post'):
        rect(3, 0, 0, 32, 32, STRUCTURE)
        if name in ('rf_barrier', 'rf_pipe_module'):
            color = INK if name == 'rf_barrier' else ACCENTS[name]
            rect(3, 5, 13, 16, 6, color)
            for y in range(7, 25):
                rect(3, 18, y, 10-abs(16-y), 1, color)
        elif name == 'rf_lamp_post':
            for x, y, w, h in ((16,5,7,6),(12,11,8,6),(9,17,14,4),(13,21,6,6)):
                rect(3, x, y, w, h, ACCENTS[name])
        else:
            # One broad hazard triangle and an exclamation, no stripe pattern.
            for y in range(5, 27):
                half = (y-5)//2
                rect(3, 16-half, y, 2*half+1, 1, (187, 151, 82))
            rect(3, 15, 13, 3, 7, STRUCTURE)
            rect(3, 15, 22, 3, 3, STRUCTURE)
    # Service door and isolated warning patch, not stripes on every edge.
    rect(4, 3, 3, 26, 26, tuple(int(c*.85) for c in body))
    rect(4, 6, 6, 8, 5, (182, 134, 65))
    rect(4, 5, 26, 22, 3, tuple(int(c*.72) for c in body))

    if label_only:
        # Keep only the padded number tile, at its original texel density.
        pixels = b''.join(pixels[(y*size+3*tile)*4:(y*size+4*tile)*4]
                          for y in range(tile))
        size = tile

    def chunk(kind, payload):
        return (struct.pack('>I', len(payload)) + kind + payload +
                struct.pack('>I', zlib.crc32(kind+payload) & 0xffffffff))
    raw = b''.join(b'\0'+pixels[y*size*4:(y+1)*size*4] for y in range(size))
    png = (b'\x89PNG\r\n\x1a\n' + chunk(b'IHDR', struct.pack('>2I5B', size, size, 8, 6, 0, 0, 0))
           + chunk(b'IDAT', zlib.compress(raw, 9)) + chunk(b'IEND', b''))
    path = out / (name + '_albedo.png')
    path.write_bytes(png)
    mat = material(name + '_albedo', (1, 1, 1))
    image = bpy.data.images.load(str(path))
    image.pack()
    node = mat.node_tree.nodes.new('ShaderNodeTexImage')
    node.image = image
    node.interpolation = 'Closest'
    mat.node_tree.links.new(node.outputs['Color'],
                            mat.node_tree.nodes.get('Principled BSDF').inputs['Base Color'])
    mat['albedo_path'] = str(path)
    mat['albedo_size'] = size
    return [mat]


def hybrid_prop(obj):
    """Assign flat surface roles, retaining texture only on the sign face."""
    name = obj.name
    label = obj.data.materials[0]
    def linear(rgb):
        return tuple(c/255/12.92 if c/255 <= .04045 else
                     ((c/255+.055)/1.055)**2.4 for c in rgb)
    colors = [PILOT[name][1], STRUCTURE]
    if name in ACCENTS:
        colors.append(ACCENTS[name])
    mats = [material(name + '_flat_' + str(i), linear(rgb)) for i, rgb in enumerate(colors)]
    mats.append(label)
    obj.data.materials.clear()
    for mat in mats:
        obj.data.materials.append(mat)
    uv = obj.data.uv_layers.active.data
    for poly in obj.data.polygons:
        loops = list(poly.loop_indices)
        u = sum(uv[i].uv.x for i in loops)/len(loops)
        v = sum(uv[i].uv.y for i in loops)/len(loops)
        tile = int(u*4) + 4*int((1-v)*4)
        assert tile in (0, 1, 2, 3, 4, 5), (name, tile)
        poly.material_index = len(mats)-1 if tile == 3 else (1 if tile == 1 else (2 if tile == 5 else 0))
        for i in loops:
            uv[i].uv = (uv[i].uv.x*4-3, uv[i].uv.y*4-3) if tile == 3 else (0, 0)
    obj['hybrid'] = True
    textured = sum(p.material_index == len(mats)-1 for p in obj.data.polygons)
    assert textured == 2, (name, textured)
    assert set(p.material_index for p in obj.data.polygons) == set(range(len(mats))), name
    print(f'Hybrid {name}: triangles={len(obj.data.polygons)} textured={textured} '
          f'materials={len(mats)} texture=32x32', flush=True)


def hybrid_crate(obj):
    """Reassign existing V2 faces only; never alter geometry or add decals."""
    label = obj.data.materials[0]
    # GLB factors are linear; PNG palette bytes are sRGB. Match their appearance.
    def linear(rgb):
        return tuple(c/255/12.92 if c/255 <= .04045 else
                     ((c/255+.055)/1.055)**2.4 for c in rgb)
    body = material('rf_crate_flat_body', linear(tuple(c+9 for c in PILOT['rf_crate'][1])))
    frame = material('rf_crate_flat_frame', linear(STRUCTURE))
    obj.data.materials.clear()
    for mat in (body, frame, label):
        obj.data.materials.append(mat)
    uv = obj.data.uv_layers.active.data
    for poly in obj.data.polygons:
        loops = list(poly.loop_indices)
        u = sum(uv[i].uv.x for i in loops)/len(loops)
        v = sum(uv[i].uv.y for i in loops)/len(loops)
        tile = int(u*4) + 4*int((1-v)*4)
        assert tile in (0, 1, 2, 3)
        poly.material_index = 2 if tile == 3 else (1 if tile == 1 else 0)
        for i in loops:
            if tile == 3:
                uv[i].uv = (uv[i].uv.x*4-3, uv[i].uv.y*4-3)
            else:
                uv[i].uv = (0, 0)
    obj['crate_material'] = 'hybrid'
    textured = sum(p.material_index == 2 for p in obj.data.polygons)
    assert textured == 2, textured
    print(f'Hybrid crate: {textured}/{len(obj.data.polygons)} textured triangles; '
          '3 materials/primitives; 32x32 label', flush=True)


def select_only(objects):
    bpy.ops.object.select_all(action='DESELECT')
    for obj in objects:
        obj.select_set(True)
    bpy.context.view_layer.objects.active = objects[0]


def material(name, rgb):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    rgba = (*rgb, 1.0)
    mat.diffuse_color = rgba
    bsdf = mat.node_tree.nodes.get('Principled BSDF')
    bsdf.inputs['Base Color'].default_value = rgba
    bsdf.inputs['Metallic'].default_value = 0.0
    bsdf.inputs['Roughness'].default_value = .9
    return mat


class Builder:
    def __init__(self, materials):
        self.parts = []
        self.materials = materials

    def box(self, center, size, accent=False, bevel=.035, tilt=0, tile=None, face=None):
        bpy.ops.mesh.primitive_cube_add(size=1, location=center)
        obj = bpy.context.object
        obj.dimensions = size
        bpy.ops.object.transform_apply(location=False, rotation=False, scale=True)
        if bevel:
            mod = obj.modifiers.new('baked_chamfer', 'BEVEL')
            mod.width = min(bevel, min(size) * .22)
            mod.segments = 1
            bpy.ops.object.modifier_apply(modifier=mod.name)
        obj.rotation_euler.y = tilt
        obj.data.materials.append(self.materials[0 if len(self.materials) == 1 else int(accent)])
        if len(self.materials) == 1:
            uv = obj.data.uv_layers.active or obj.data.uv_layers.new(name='UVMap')
            for poly in obj.data.polygons:
                axis = max(range(3), key=lambda i: abs(poly.normal[i]))
                axes = ((1, 2), (0, 2), (0, 1))[axis]
                chosen = (1 if accent else 0) if tile is None else tile
                if face is not None and (axis, int(poly.normal[axis])) != face:
                    chosen = 1 if accent else 0
                for li in poly.loop_indices:
                    v = obj.data.vertices[obj.data.loops[li].vertex_index].co
                    u, t = (v[a]/size[a]+.5 for a in axes)
                    # Outward faces in Rasterfall's +Z-forward view convention.
                    if (axis == 1 and poly.normal.y < 0) or (axis == 0 and poly.normal.x > 0):
                        u = 1-u
                    # Two logical pixels of padding prevent neighboring tile bleed.
                    uv.data[li].uv = ((chosen % 4 + (2+28*u)/32)/4,
                                      1-(chosen // 4 + (30-28*t)/32)/4)
        self.parts.append(obj)
        return obj

    def cylinder(self, center, radius, depth, accent=False):
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=radius,
                                           depth=depth, location=center)
        obj = bpy.context.object
        obj.data.materials.append(self.materials[0 if len(self.materials) == 1 else int(accent)])
        if len(self.materials) == 1:
            uv = obj.data.uv_layers.active or obj.data.uv_layers.new(name='UVMap')
            for loop in uv.data:
                loop.uv = ((int(accent)+.5)/4, .875)
        self.parts.append(obj)

    def finish(self, name, dimensions, budget):
        select_only(self.parts)
        bpy.ops.object.join()
        obj = bpy.context.object
        obj.name = name
        bpy.context.scene.cursor.location = (0, 0, 0)
        bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        # Join may leave duplicate slots; collapse to the asset's material set.
        indices = [self.materials.index(obj.data.materials[p.material_index])
                   for p in obj.data.polygons]
        obj.data.materials.clear()
        for mat in self.materials:
            obj.data.materials.append(mat)
        for poly, index in zip(obj.data.polygons, indices):
            poly.material_index = index
            poly.use_smooth = False
        mod = obj.modifiers.new('baked_triangles', 'TRIANGULATE')
        bpy.ops.object.modifier_apply(modifier=mod.name)
        obj.data.update()
        obj.data.calc_loop_triangles()
        triangles = len(obj.data.loop_triangles)
        lo = [min(v.co[i] for v in obj.data.vertices) for i in range(3)]
        hi = [max(v.co[i] for v in obj.data.vertices) for i in range(3)]
        expected_lo = [-dimensions[0]/2, -dimensions[1]/2, 0]
        expected_hi = [dimensions[0]/2, dimensions[1]/2, dimensions[2]]
        assert all(abs(a-b) < 1e-5 for a, b in
                   zip(lo + hi, expected_lo + expected_hi)), (name, lo, hi)
        assert 200 <= triangles <= budget <= 1200, (name, triangles, budget)
        assert all(p.area > 1e-10 for p in obj.data.polygons), name
        assert not obj.modifiers and not obj.animation_data
        obj['dimensions_m'] = list(dimensions)
        obj['forward'] = '-Y (Blender), +Z (GLB)'
        obj['pivot'] = 'bottom centre'
        print(f'{name:24s} vertices={len(obj.data.vertices):4d} '
              f'triangles={triangles:4d}/{budget} materials={len(self.materials)} bounds={lo}..{hi}',
              flush=True)
        return obj


def build(name, mats):
    b = Builder(mats)
    box = b.box
    if name == 'rf_crate':
        box((0, 0, .5), (1.12, .92, .84), bevel=.07)
        for z in (.06, .94):
            if len(mats) == 1 and z == .94:
                # A real shallow lid recess, with no coplanar overlay faces.
                for x in (-.55, .55):
                    box((x, 0, z), (.1, 1, .12), True, bevel=.015)
                for y in (-.45, .45):
                    box((0, y, z), (1, .1, .12), True, bevel=.015)
            else:
                box((0, 0, z), (1.2, 1, .12), True)
        # Keep the frame rail's outer face off the body side.  At +/- .48
        # its outer edge was exactly coplanar with the body (x=+/- .56),
        # causing material z-fighting during camera rotation.
        for x in (-.50, .50):
            box((x, 0, .5), (.16, 1, .76), True)
        box((0, -.47, .5), (.44, .06, .3), tile=3, face=(1, -1))
        if len(mats) == 1:
            box((0, 0, .94), (1.02, .82, .06), bevel=0, tile=2, face=(2, 1))
            for x in (-.535, .535):
                box((x, -.465, .24), (.13, .07, .24), True, bevel=.015)
    elif name == 'rf_barrier':
        # Wide feet, narrow upper body, conspicuous sloping shoulders.
        obj = box((0, 0, .5), (2.4, .8, 1), bevel=0)
        for v in obj.data.vertices:
            if v.co.z > 0:
                v.co.y *= .45
        mod = obj.modifiers.new('shoulder_chamfer', 'BEVEL')
        mod.width = .045
        mod.segments = 1
        bpy.ops.object.modifier_apply(modifier=mod.name)
        for x in (-.85, 0, .85):
            box((x, -.185, .85), (.28, .12, .23), True, tile=5, face=(1, -1))
        for x in (-.9, .9):
            box((x, 0, .075), (.4, .8, .15))
        box((0, -.28, .48), (1.38, .065, .28), True, bevel=.02)
        for x in (-1.08, 1.08):
            box((x, -.22, .75), (.2, .08, .24), True, bevel=0)
        box((.36, -.323, .48), (.36, .02, .20), tile=3, face=(1, -1), bevel=0)
    elif name == 'rf_short_wall':
        box((0, 0, .65), (2.3, .34, 1.3), bevel=.06)
        for z in (.08, 1.32):
            box((0, 0, z), (2.4, .5, .16), True)
        for x in (-1.05, 0, 1.05):
            box((x, 0, .7), (.16, .46, 1.08))
        for x in (-.525, .525):
            box((x, -.182, .7), (.80, .055, .88), True, bevel=.018)
        for x in (-1.165, 1.165):
            box((x, 0, .7), (.05, .4, 1.06), True, bevel=0)
        box((-.525, -.222, .84), (.30, .02, .18), tile=3, face=(1, -1), bevel=0)
    elif name == 'rf_railing':
        for x in (-1.08, 0, 1.08):
            box((x, 0, .53), (.12, .18, 1.06))
            box((x, 0, .04), (.24, .3, .08))
        for z in (.48, 1.05):
            box((0, 0, z), (2.4, .16, .1), True)
        for x in (-1.08, 0, 1.08):
            box((x, 0, 1.055), (.20, .24, .09), True, bevel=.014)
            # The lower accent belongs on the front of the lower rail, not
            # inside it: both used to occupy z=0.48 with intersecting boxes.
            box((x, -.105, .48), (.24, .03, .12), tile=5, bevel=0)
            # Keep the upper accent in the same front-panel plane, but ahead
            # of the top cap so its front face cannot be coplanar with it.
            box((x, -.13, 1.01), (.24, .02, .12), tile=5, bevel=0)
        box((0, -.125, .82), (.22, .04, .24), tile=3, face=(1, -1), bevel=0)
    elif name == 'rf_lamp_post':
        box((0, 0, .08), (.8, .8, .16))
        box((0, .15, .4), (.4, .4, .48), True)
        box((0, .15, 1.8), (.18, .18, 2.72))
        box((0, 0, 3.08), (.24, .64, .16))
        box((0, -.19, 3.08), (.6, .42, .24), bevel=.06)
        box((0, -.19, 2.965), (.46, .3, .03), True, bevel=0, tile=5)
        box((0, .12, .19), (.56, .50, .06), True, bevel=.012)
        box((0, .025, 1.05), (.30, .20, .38), True, bevel=.02)
        box((0, -.085, 1.05), (.17, .02, .22), tile=3, face=(1, -1), bevel=0)
        box((0, -.19, 3.185), (.66, .42, .03), True, bevel=0)
    elif name == 'rf_vent_unit':
        box((0, 0, .56), (1.32, .82, 1.04), bevel=.06)
        for z in (.06, 1.14):
            box((0, 0, z), (1.4, .9, .12), True,
                tile=2 if z > 1 else 1, face=(2, 1))
        for z in (.3, .5, .7, .9):
            box((0, -.422, z), (1.08, .056, .1), True, bevel=.015)
        for x in (-.58, .58):
            box((x, -.425, .6), (.08, .05, .86))
        if len(mats) == 1:
            for z in (.18, 1.02):
                box((0, -.405, z), (1.24, .05, .1), True, bevel=.015)
            box((.675, 0, .61), (.03, .62, .74), tile=4, face=(0, 1))
            box((.695, -.18, .62), (.01, .09, .26), True, bevel=0)
            box((0, -.44, 1.02), (.34, .02, .085), tile=3, face=(1, -1), bevel=0)
    elif name == 'rf_workbench':
        box((0, 0, .815 if len(mats) == 1 else .83),
            (1.8, .78 if len(mats) == 1 else .8, .11 if len(mats) == 1 else .14), True, tile=2, face=(2, 1))
        for x in (-.72, .72):
            for y in (-.26, .26):
                box((x, y, .38), (.16, .16, .76), len(mats) == 1)
        box((0, 0, .18), (1.52, .64, .1), len(mats) == 1)
        box((.47, 0, .62), (.5, .62, .28))
        box((.47, -.325, .62), (.22, .03, .07), True, bevel=0)
        if len(mats) == 1:
            for x in (.34, .60):
                box((x, -.318, .62), (.24, .016, .24), tile=4, face=(1, -1), bevel=0)
            box((-.38, -.395, .83), (.32, .01, .11), tile=3, face=(1, -1), bevel=0)
            box((0, .365, .865), (1.68, .07, .07), True, bevel=.012)
    elif name == 'rf_ammo_container':
        box((0, 0, .26), (.86, .46, .52), bevel=.045)
        box((0, 0, .53), (.9, .5, .1), True)
        for x in (-.32, .32):
            box((x, -.23, .36), (.12, .04, .25), True)
        for x in (-.18, .18):
            box((x, 0, .565), (.06, .12, .07))
        box((0, 0, .58), (.4, .12, .04))
        for x in (-.415, .415):
            box((x, 0, .32), (.07, .26, .12), True, bevel=.012)
        box((0, -.242, .28), (.30, .016, .18), tile=3, face=(1, -1), bevel=0)
        # Leave a visible depth gap from the case front so the red icon does
        # not compete with the front panel after RMESH quantization.
        box((.22, -.245, .44), (.08, .01, .18), tile=5, bevel=0)
    elif name == 'rf_industrial_pillar':
        box((0, 0, 1.4), (.48, .48, 2.64), bevel=.065)
        for z in (.1, 2.7):
            box((0, 0, z), (.8, .8, .2), True)
        for z in (.42, 2.38):
            box((0, 0, z), (.64, .64, .22 if z < 1 else .12))
        box((0, -.245, 1.4), (.2, .05, .7), True)
        for z in (.24, 2.56):
            box((0, 0, z), (.64, .64, .08), True, bevel=0)
        box((0, -.282, 1.52), (.18, .024, .19), tile=3, face=(1, -1), bevel=0)
        box((0, -.335, .43), (.38, .03, .14), tile=5, bevel=0)
    elif name == 'rf_pipe_module':
        box((0, 0, .08), (1.6, .8, .16))
        for x in (-.48, .48):
            b.cylinder((x, 0, .73), .24, 1.14)
            for z in (.25, 1.18):
                b.cylinder((x, 0, z), .31, .12, True)
            box((x, 0, 1.34), (.5, .5, .12))
        box((0, .26, .7), (1.3, .12, .16), True)
        box((0, 0, .67), (.52, .34, .28), True, bevel=.025)
        box((0, -.23, .17), (.50, .28, .06), True, bevel=0)
        box((0, -.183, .67), (.30, .022, .17), tile=3, face=(1, -1), bevel=0)
        box((0, .26, .76), (.32, .14, .08), tile=5, bevel=0)
    else:
        raise ValueError(name)
    return b


def inspect_glb(path, objects):
    """Check actual exporter output, including flat-normal vertex splitting."""
    raw = path.read_bytes()
    magic, version, length, json_len, kind = struct.unpack_from('<5I', raw)
    assert (magic, version, length, kind) == (0x46546C67, 2, len(raw), 0x4E4F534A)
    doc = json.loads(raw[20:20+json_len])
    assert not any(doc.get(k) for k in ('animations', 'skins', 'cameras'))
    assert not doc.get('extensionsRequired')
    assert len(doc['meshes']) == len(objects)
    assert len(doc['nodes']) == len(objects)
    for node in doc['nodes']:
        assert 'mesh' in node
        assert 'matrix' not in node
        assert node.get('translation', [0, 0, 0]) == [0, 0, 0]
        assert node.get('rotation', [0, 0, 0, 1]) == [0, 0, 0, 1]
        assert node.get('scale', [1, 1, 1]) == [1, 1, 1]
    by_name = {obj.name: obj for obj in objects}
    for mesh in doc['meshes']:
        obj = by_name[mesh['name']]
        vertices = triangles = 0
        lows, highs = [], []
        assert len(mesh['primitives']) == len(obj.data.materials)
        assert len(mesh['primitives']) <= (4 if obj.get('hybrid') else (3 if obj.get('crate_material') == 'hybrid' else 2))
        for material in obj.data.materials:
            if 'albedo_path' not in material:
                continue
            size = material['albedo_size']
            png = Path(material['albedo_path']).read_bytes()
            assert struct.unpack_from('>II', png, 16) == (size, size)
            assert all(math.isfinite(c) and 0 <= c <= 1
                       for uv in obj.data.uv_layers.active.data for c in uv.uv)
        for prim in mesh['primitives']:
            assert prim.get('mode', 4) == 4 and not prim.get('extensions')
            pos = doc['accessors'][prim['attributes']['POSITION']]
            idx = doc['accessors'][prim['indices']]
            assert pos['componentType'] == 5126 and pos['type'] == 'VEC3'
            assert idx['componentType'] in (5121, 5123, 5125)
            assert idx['count'] % 3 == 0
            assert 'NORMAL' in prim['attributes']
            mat = doc['materials'][prim['material']]['pbrMetallicRoughness']
            if 'baseColorTexture' in mat:
                assert 'TEXCOORD_0' in prim['attributes']
            vertices += pos['count']
            triangles += idx['count'] // 3
            lows.append(pos['min'])
            highs.append(pos['max'])
        w, d, h = obj['dimensions_m']
        bounds = [min(v[i] for v in lows) for i in range(3)]
        bounds += [max(v[i] for v in highs) for i in range(3)]
        assert all(abs(a-b) < 1e-5 for a, b in
                   zip(bounds, [-w/2, 0, -d/2, w/2, h, d/2])), bounds
        assert triangles == len(obj.data.polygons)
        print(f'GLB {mesh["name"]}: vertices={vertices}, triangles={triangles}, '
              f'primitives={len(mesh["primitives"])}', flush=True)


def _pad4(data, fill=b'\0'):
    return data + fill * ((4 - len(data) % 4) % 4)


def write_static_glb(path, objects):
    """Write the generator's static GLB subset, including embedded albedo."""
    binary = bytearray()
    accessors, views, meshes, nodes = [], [], [], []
    materials, images, textures = [], [], []
    material_ids = {}
    for obj in objects:
        for mat in obj.data.materials:
            if mat.name in material_ids:
                continue
            material_ids[mat.name] = len(materials)
            pbr = {'baseColorFactor': list(mat.diffuse_color),
                   'metallicFactor': 0.0, 'roughnessFactor': .9}
            if 'albedo_path' in mat:
                payload = Path(mat['albedo_path']).read_bytes()
                views.append({'buffer': 0, 'byteOffset': len(binary), 'byteLength': len(payload)})
                binary.extend(_pad4(payload))
                images.append({'bufferView': len(views)-1, 'mimeType': 'image/png'})
                textures.append({'source': len(images)-1, 'sampler': 0})
                pbr['baseColorTexture'] = {'index': len(textures)-1}
            materials.append({'name': mat.name, 'pbrMetallicRoughness': pbr})

    def add_view(payload, target):
        offset = len(binary)
        binary.extend(_pad4(payload))
        views.append({'buffer': 0, 'byteOffset': offset,
                      'byteLength': len(payload), 'target': target})
        return len(views) - 1

    def add_accessor(view, count, kind, component, minimum=None, maximum=None):
        accessor = {'bufferView': view, 'componentType': component,
                    'count': count, 'type': kind}
        if minimum is not None:
            accessor['min'] = minimum
            accessor['max'] = maximum
        accessors.append(accessor)
        return len(accessors) - 1

    for obj in objects:
        groups = {}
        mesh = obj.data
        for polygon in mesh.polygons:
            groups.setdefault(polygon.material_index, []).append(polygon)
        primitives = []
        for material_index in sorted(groups):
            vertices, normals, indices, uvs = [], [], [], []
            textured = 'albedo_path' in mesh.materials[material_index]
            for polygon in groups[material_index]:
                for loop_index in polygon.loop_indices:
                    loop = mesh.loops[loop_index]
                    vertex = mesh.vertices[loop.vertex_index].co
                    normal = polygon.normal
                    vertices.extend((vertex.x, vertex.z, -vertex.y))
                    normals.extend((normal.x, normal.z, -normal.y))
                    indices.append(len(indices))
                    if textured:
                        uv = mesh.uv_layers.active.data[loop_index].uv
                        uvs.extend((uv.x, 1-uv.y))
            position = struct.pack('<%sf' % len(vertices), *vertices)
            normal = struct.pack('<%sf' % len(normals), *normals)
            index = struct.pack('<%sI' % len(indices), *indices)
            pview = add_view(position, 34962)
            nview = add_view(normal, 34962)
            iview = add_view(index, 34963)
            points = list(zip(vertices[0::3], vertices[1::3], vertices[2::3]))
            pmin = [min(point[i] for point in points) for i in range(3)]
            pmax = [max(point[i] for point in points) for i in range(3)]
            pa = add_accessor(pview, len(points), 'VEC3', 5126, pmin, pmax)
            na = add_accessor(nview, len(points), 'VEC3', 5126)
            ia = add_accessor(iview, len(indices), 'SCALAR', 5125)
            attributes = {'POSITION': pa, 'NORMAL': na}
            if textured:
                uvview = add_view(struct.pack('<%sf' % len(uvs), *uvs), 34962)
                attributes['TEXCOORD_0'] = add_accessor(uvview, len(points), 'VEC2', 5126)
            primitives.append({'attributes': attributes,
                               'indices': ia, 'material': material_ids[mesh.materials[material_index].name]})
        meshes.append({'name': obj.name, 'primitives': primitives})
        nodes.append({'name': obj.name, 'mesh': len(meshes) - 1})

    doc = {
        'asset': {'version': '2.0', 'generator': 'rasterfall-prop-generator'},
        'scene': 0, 'scenes': [{'nodes': list(range(len(nodes))) }],
        'nodes': nodes, 'meshes': meshes,
        'materials': materials,
        'buffers': [{'byteLength': len(_pad4(binary))}],
        'bufferViews': views, 'accessors': accessors,
    }
    if textures:
        doc.update(images=images, textures=textures,
                   samplers=[{'magFilter': 9728, 'minFilter': 9728,
                              'wrapS': 33071, 'wrapT': 33071}])
    encoded = json.dumps(doc, separators=(',', ':')).encode('utf-8')
    encoded = _pad4(encoded, b' ')
    binary = _pad4(binary)
    total = 12 + 8 + len(encoded) + 8 + len(binary)
    with path.open('wb') as output:
        output.write(struct.pack('<3I', 0x46546C67, 2, total))
        output.write(struct.pack('<2I', len(encoded), 0x4E4F534A))
        output.write(encoded)
        output.write(struct.pack('<2I', len(binary), 0x004E4942))
        output.write(binary)


def export(path, objects):
    select_only(objects)
    try:
        import numpy  # noqa: F401
        use_native_exporter = True
    except ModuleNotFoundError:
        use_native_exporter = False
    if use_native_exporter:
        bpy.ops.export_scene.gltf(
            filepath=str(path), export_format='GLB', use_selection=True,
            export_yup=True, export_apply=False, export_animations=False,
            export_skins=False, export_morph=False, export_cameras=False,
            export_lights=False, export_extras=False,
            export_texcoords=any('albedo_path' in mat for obj in objects for mat in obj.data.materials),
            export_normals=True, export_tangents=False, export_materials='EXPORT',
            export_draco_mesh_compression_enable=False)
    else:
        # Blender's bundled exporter imports numpy in newer releases.  Keep
        # this generator usable in the minimal Blender packages used by CI by
        # writing the small static-prop GLB contract directly.
        write_static_glb(path, objects)
    inspect_glb(path, objects)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path,
                        default=Path(__file__).resolve().parents[2] / 'tmp/rasterfall-props')
    parser.add_argument('--all-glb', action='store_true', help='Also export overlapping asset library')
    parser.add_argument('--overwrite', action='store_true')
    parser.add_argument('--crate-material', choices=('full', 'hybrid'), default='hybrid',
                        help='Crate-only material prototype; identical V2 geometry')
    parser.add_argument('--assets', nargs='+', choices=[s[0] for s in SPECS],
                        help='Generate only these asset IDs')
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out = args.output.resolve()
    specs = [s for s in SPECS if not args.assets or s[0] in args.assets]
    targets = [out / (name + '.glb') for name, _, _ in specs]
    targets += [out / (name + '_albedo.png') for name, _, _ in specs if name in PILOT]
    targets += [out / 'rasterfall_props.blend']
    if args.all_glb:
        targets.append(out / 'rasterfall_props_all.glb')
    if not args.overwrite and any(p.exists() for p in targets):
        raise FileExistsError('Output exists; use a fresh --output or explicit --overwrite')
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system = 'METRIC'
    bpy.context.scene.unit_settings.scale_length = 1.0
    out.mkdir(parents=True, exist_ok=True)
    mats = [material('rf_olive', (.12, .16, .09)),
            material('rf_safety_ochre', (.65, .36, .045))]
    objects = []
    for name, dimensions, budget in specs:
        hybrid = name != 'rf_crate' or args.crate_material == 'hybrid'
        asset_mats = albedo_material(name, out, label_only=hybrid) if name in PILOT else mats
        obj = build(name, asset_mats).finish(name, dimensions, budget)
        if hybrid:
            if name == 'rf_crate':
                hybrid_crate(obj)
            else:
                hybrid_prop(obj)
        obj.data.name = name
        objects.append(obj)
        export(out / (name + '.glb'), [obj])
    if args.all_glb:
        export(out / 'rasterfall_props_all.glb', objects)
    # Separate collections + local view make an origin-aligned library inspectable.
    for obj in objects:
        collection = bpy.data.collections.new(obj.name)
        bpy.context.scene.collection.children.link(collection)
        for old in list(obj.users_collection):
            old.objects.unlink(obj)
        collection.objects.link(obj)
        obj.hide_set(obj != objects[0])
    select_only([objects[0]])
    bpy.context.preferences.filepaths.save_version = 0
    bpy.ops.wm.save_as_mainfile(filepath=str(out / 'rasterfall_props.blend'))
    print(f'Finished: {len(objects)} assets -> {out}', flush=True)


if __name__ == '__main__':
    main()
