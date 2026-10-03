"""Original deterministic textile swatch and UV0 for tactical garments/gear."""
import math
import json
import struct
from pathlib import Path

import bpy


def projected_uv(obj):
    mesh=obj.data
    uv=mesh.uv_layers.get('UVMap') or mesh.uv_layers.new(name='UVMap')
    for polygon in mesh.polygons:
        axis=max(range(3),key=lambda a:abs(polygon.normal[a]))
        axes=((1,2),(0,2),(0,1))[axis]
        for loop in polygon.loop_indices:
            co=mesh.vertices[mesh.loops[loop].vertex_index].co
            uv.data[loop].uv=((co[axes[0]]+1.2)/2.4,
                (co[axes[1]]+(0 if axes[1]==2 else 1.2))/2.4)


def apply_textiles(scene,materials,keys):
    image=bpy.data.images.get('RF_TacticalFabric')
    if image is None:
        size=256
        image=bpy.data.images.new('RF_TacticalFabric',width=size,height=size,alpha=False)
        pixels=[]
        for y in range(size):
            for x in range(size):
                # Quiet ripstop, dye variation and a woven fibre cue. No
                # borrowed camouflage, logos or photographic source content.
                seed=(x*374761393+y*668265263)&0xffffffff
                seed=((seed^(seed>>13))*1274126177)&0xffffffff
                noise=((seed^(seed>>16))&65535)/65535-.5
                grid=.014 if x%32==0 or y%32==0 else 0
                # Separate warp/weft dots avoid diagonal screen-space bands
                # when the fabric is minified on a limb or oblique armor face.
                weave=(.004 if x%2 else -.004)+(.003 if y%2 else -.003)
                broad=.010*math.sin(x*.068+math.sin(y*.047))*math.cos(y*.051)
                value=max(.82,min(.94,.89+weave+broad+noise*.012-grid))
                pixels.extend((value,value,value,1))
        image.pixels=pixels
        image.file_format='PNG'
        image.pack()
    active=set()
    for key in keys:
        material=materials[key]
        active.add(material)
        principled=material.node_tree.nodes.get('Principled BSDF')
        principled.inputs['Roughness'].default_value=.82
        texture=material.node_tree.nodes.new('ShaderNodeTexImage')
        texture.image=image
        texture.interpolation='Linear'
        texture.extension='EXTEND'
        material['rf_textile_factor']=list(material.diffuse_color)
        material.node_tree.links.new(texture.outputs['Color'],principled.inputs['Base Color'])
    for obj in scene.objects:
        if obj.type=='MESH' and any(m in active for m in obj.data.materials):
            projected_uv(obj)


def patch_export_materials(path):
    """Keep authored palette factors explicit across Blender exporter versions."""
    path=Path(path)
    raw=path.read_bytes()
    size,kind=struct.unpack_from('<II',raw,12)
    document=json.loads(raw[20:20+size])
    for material in document.get('materials',[]):
        original=bpy.data.materials.get(material.get('name',''))
        if original is not None and 'rf_textile_factor' in original:
            material['pbrMetallicRoughness']['baseColorFactor']=list(original['rf_textile_factor'])
            material['alphaMode']='OPAQUE'
    encoded=json.dumps(document,separators=(',',':')).encode('utf-8')
    encoded+=b' '*(-len(encoded)%4)
    remainder=raw[20+size:]
    path.write_bytes(b'glTF'+struct.pack('<II',2,20+len(encoded)+len(remainder))+
        struct.pack('<II',len(encoded),kind)+encoded+remainder)
