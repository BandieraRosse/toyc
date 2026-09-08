"""Generate Rasterfall industrial kit; run with Blender 3.6+ (no add-ons).

blender -b --python-exit-code 1 --python tools/blender/generate_rasterfall_props.py
Options after --: --output DIR --all-glb --overwrite
Source coordinates: metres, Z up, -Y forward, bottom-centre origin.
"""

import argparse
import json
import math
from pathlib import Path
import struct
import sys

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

    def box(self, center, size, accent=False, bevel=.035, tilt=0):
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
        obj.data.materials.append(self.materials[int(accent)])
        self.parts.append(obj)
        return obj

    def cylinder(self, center, radius, depth, accent=False):
        bpy.ops.mesh.primitive_cylinder_add(vertices=8, radius=radius,
                                           depth=depth, location=center)
        obj = bpy.context.object
        obj.data.materials.append(self.materials[int(accent)])
        self.parts.append(obj)

    def finish(self, name, dimensions, budget):
        select_only(self.parts)
        bpy.ops.object.join()
        obj = bpy.context.object
        obj.name = name
        bpy.context.scene.cursor.location = (0, 0, 0)
        bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        bpy.ops.object.transform_apply(location=True, rotation=True, scale=True)
        # Join may leave duplicate material slots. Collapse to exactly two.
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
              f'triangles={triangles:4d}/{budget} materials=2 bounds={lo}..{hi}',
              flush=True)
        return obj


def build(name, mats):
    b = Builder(mats)
    box = b.box
    if name == 'rf_crate':
        box((0, 0, .5), (1.12, .92, .84), bevel=.07)
        for z in (.06, .94):
            box((0, 0, z), (1.2, 1, .12), True)
        for x in (-.48, .48):
            box((x, 0, .5), (.16, 1, .76), True)
        box((0, -.47, .5), (.44, .06, .3))
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
            box((x, -.185, .85), (.28, .12, .23), True)
        for x in (-.9, .9):
            box((x, 0, .075), (.4, .8, .15))
    elif name == 'rf_short_wall':
        box((0, 0, .65), (2.3, .34, 1.3), bevel=.06)
        for z in (.08, 1.32):
            box((0, 0, z), (2.4, .5, .16), True)
        for x in (-1.05, 0, 1.05):
            box((x, 0, .7), (.16, .46, 1.08))
    elif name == 'rf_railing':
        for x in (-1.08, 0, 1.08):
            box((x, 0, .53), (.12, .18, 1.06))
            box((x, 0, .04), (.24, .3, .08))
        for z in (.48, 1.05):
            box((0, 0, z), (2.4, .16, .1), True)
    elif name == 'rf_lamp_post':
        box((0, 0, .08), (.8, .8, .16))
        box((0, .15, .4), (.4, .4, .48), True)
        box((0, .15, 1.8), (.18, .18, 2.72))
        box((0, 0, 3.08), (.24, .64, .16))
        box((0, -.19, 3.08), (.6, .42, .24), bevel=.06)
        box((0, -.19, 2.965), (.46, .3, .03), True, bevel=0)
    elif name == 'rf_vent_unit':
        box((0, 0, .56), (1.32, .82, 1.04), bevel=.06)
        for z in (.06, 1.14):
            box((0, 0, z), (1.4, .9, .12), True)
        for z in (.3, .5, .7, .9):
            box((0, -.422, z), (1.08, .056, .1), True, bevel=.015)
        for x in (-.58, .58):
            box((x, -.425, .6), (.08, .05, .86))
    elif name == 'rf_workbench':
        box((0, 0, .83), (1.8, .8, .14), True)
        for x in (-.72, .72):
            for y in (-.26, .26):
                box((x, y, .38), (.16, .16, .76))
        box((0, 0, .18), (1.52, .64, .1))
        box((.47, 0, .62), (.5, .62, .28))
        box((.47, -.325, .62), (.22, .03, .07), True, bevel=0)
    elif name == 'rf_ammo_container':
        box((0, 0, .26), (.86, .46, .52), bevel=.045)
        box((0, 0, .53), (.9, .5, .1), True)
        for x in (-.32, .32):
            box((x, -.23, .36), (.12, .04, .25), True)
        for x in (-.18, .18):
            box((x, 0, .565), (.06, .12, .07))
        box((0, 0, .58), (.4, .12, .04))
    elif name == 'rf_industrial_pillar':
        box((0, 0, 1.4), (.48, .48, 2.64), bevel=.065)
        for z in (.1, 2.7):
            box((0, 0, z), (.8, .8, .2), True)
        for z in (.42, 2.38):
            box((0, 0, z), (.64, .64, .16))
        box((0, -.245, 1.4), (.2, .05, .7), True)
    elif name == 'rf_pipe_module':
        box((0, 0, .08), (1.6, .8, .16))
        for x in (-.48, .48):
            b.cylinder((x, 0, .73), .24, 1.14)
            for z in (.25, 1.18):
                b.cylinder((x, 0, z), .31, .12, True)
            box((x, 0, 1.34), (.5, .5, .12))
        box((0, .26, .7), (1.3, .12, .16), True)
    else:
        raise ValueError(name)
    return b


def inspect_glb(path, objects):
    """Check actual exporter output, including flat-normal vertex splitting."""
    raw = path.read_bytes()
    magic, version, length, json_len, kind = struct.unpack_from('<5I', raw)
    assert (magic, version, length, kind) == (0x46546C67, 2, len(raw), 0x4E4F534A)
    doc = json.loads(raw[20:20+json_len])
    assert not any(doc.get(k) for k in ('animations', 'skins', 'cameras', 'textures'))
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
        assert 1 <= len(mesh['primitives']) <= 2
        for prim in mesh['primitives']:
            assert prim.get('mode', 4) == 4 and not prim.get('extensions')
            pos = doc['accessors'][prim['attributes']['POSITION']]
            idx = doc['accessors'][prim['indices']]
            assert pos['componentType'] == 5126 and pos['type'] == 'VEC3'
            assert idx['componentType'] in (5121, 5123, 5125)
            assert idx['count'] % 3 == 0
            assert 'NORMAL' in prim['attributes']
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
    """Write the generator's deliberately small, untextured GLB subset."""
    binary = bytearray()
    accessors, views, meshes, nodes = [], [], [], []

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
            vertices, normals, indices = [], [], []
            for polygon in groups[material_index]:
                for loop_index in polygon.loop_indices:
                    loop = mesh.loops[loop_index]
                    vertex = mesh.vertices[loop.vertex_index].co
                    normal = polygon.normal
                    vertices.extend((vertex.x, vertex.z, -vertex.y))
                    normals.extend((normal.x, normal.z, -normal.y))
                    indices.append(len(indices))
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
            primitives.append({'attributes': {'POSITION': pa, 'NORMAL': na},
                               'indices': ia, 'material': material_index})
        meshes.append({'name': obj.name, 'primitives': primitives})
        nodes.append({'name': obj.name, 'mesh': len(meshes) - 1})

    doc = {
        'asset': {'version': '2.0', 'generator': 'rasterfall-prop-generator'},
        'scene': 0, 'scenes': [{'nodes': list(range(len(nodes))) }],
        'nodes': nodes, 'meshes': meshes,
        'materials': [
            {'name': 'rf_olive', 'pbrMetallicRoughness':
             {'baseColorFactor': [0.12, 0.16, 0.09, 1.0],
              'metallicFactor': 0.0, 'roughnessFactor': 0.9}},
            {'name': 'rf_safety_ochre', 'pbrMetallicRoughness':
             {'baseColorFactor': [0.65, 0.36, 0.045, 1.0],
              'metallicFactor': 0.0, 'roughnessFactor': 0.9}},
        ],
        'buffers': [{'byteLength': len(_pad4(binary))}],
        'bufferViews': views, 'accessors': accessors,
    }
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
            export_lights=False, export_extras=False, export_texcoords=False,
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
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:] if '--' in sys.argv else [])
    out = args.output.resolve()
    targets = [out / (name + '.glb') for name, _, _ in SPECS]
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
    for name, dimensions, budget in SPECS:
        obj = build(name, mats).finish(name, dimensions, budget)
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
