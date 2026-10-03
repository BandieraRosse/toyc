"""Original UV0/base-color detail for the unchanged five-material Humanoid.

No geometry, weights, rig or profession colors are edited. Textures modulate the
existing palette; the shirt/pants texture is neutral so roster recoloring works.
"""
import math

import bpy


def clamp(value): return min(1.,max(0.,value))


def image(name,size,pixel):
    result=bpy.data.images.new(name,width=size,height=size,alpha=False)
    values=[]
    for y in range(size):
        for x in range(size):
            rgb=pixel((x+.5)/size,(y+.5)/size,x,y)
            values.extend((*[clamp(c) for c in rgb],1.))
    result.pixels=values
    result.file_format='PNG'
    result.pack()
    return result


def gaussian(u,v,x,y,sx,sy):
    return math.exp(-(((u-x)/sx)**2+((v-y)/sy)**2)*.5)


def noise(x,y):
    return (((x*1664525+y*1013904223)^((x*y+7919)*374761393))&255)/255.-.5


def skin_pixel(u,v,x,y):
    grain=noise(x,y)*.015
    if v<.19:
        # Hands: shallow knuckle bands and a warm palm crease. Kept subtle at
        # gameplay scale; no baked directional light or hard cast shadows.
        crease=.075*math.exp(-((u-.58)/.035)**2)+.045*math.exp(-((u-.83)/.022)**2)
        value=.95-crease+grain
        return value, value-.015, value-.022
    t=(v-.20)/.80
    eye=sum(gaussian(u,t,.5+s*.071,.407,.025,.036) for s in (-1,1))
    cheek=sum(gaussian(u,t,.5+s*.089,.307,.037,.065) for s in (-1,1))
    lip=gaussian(u,t,.5,.221,.027,.018)
    stubble=gaussian(u,t,.5,.175,.096,.11)
    temple=sum(gaussian(u,t,.5+s*.144,.43,.025,.17) for s in (-1,1))
    value=.975-.18*eye-.06*temple-.045*stubble+grain
    return value-.11*lip, value-.15*lip-.065*cheek, value-.13*lip-.075*cheek


def detail_pixel(u,v,x,y):
    if v<.30:
        # Leather/canvas toe grain, welt and paired seam lanes.
        seam=.17*(math.exp(-((u-.12)/.016)**2)+math.exp(-((u-.88)/.016)**2))
        value=(.64 if v<.045 else .88)-seam+noise(x,y)*.07
        stitch=.06 if (x%9<3 and (.055<v<.062 or .238<v<.245)) else 0
        return (value+stitch,)*3
    t=(v-.34)/.65
    # Swept short strands change direction gradually over the existing crop.
    phase=u*210+7*math.sin(t*3.4)+t*11
    strand=.065*math.sin(phase)+.027*math.sin(phase*2.13)
    value=.86+strand+noise(x,y)*.035
    return (value,)*3


def fabric_pixel(u,v,x,y):
    seam=.010 if x%32==0 or y%32==0 else 0
    weave=(.0018 if x%2 else -.0018)+(.0012 if y%2 else -.0012)
    value=.985-seam+weave+noise(x,y)*.004
    return (value,)*3


def apply_surface(scene,materials):
    textures={'skin':image('RF_BodySkin',512,skin_pixel),
        'detail':image('RF_BodyDetail',512,detail_pixel),
        'fabric':image('RF_BodyFabric',256,fabric_pixel)}
    for key in ('pants','shirt','skin','hair','boots'):
        material=materials[key]
        texture=material.node_tree.nodes.new('ShaderNodeTexImage')
        texture.image=textures['skin' if key=='skin' else
            'fabric' if key in ('pants','shirt') else 'detail']
        texture.interpolation='Linear';texture.extension='EXTEND'
        material['rf_textile_factor']=list(material.diffuse_color)
        bsdf=material.node_tree.nodes.get('Principled BSDF')
        bsdf.inputs['Roughness'].default_value={'skin':.67,'hair':.72,'boots':.79}.get(key,.87)
        material.node_tree.links.new(texture.outputs['Color'],bsdf.inputs['Base Color'])
    for obj in scene.objects:
        if obj.type!='MESH': continue
        mesh=obj.data;uv=mesh.uv_layers.new(name='UVMap')
        for polygon in mesh.polygons:
            material=mesh.materials[polygon.material_index]
            mapped=[]
            for loop in polygon.loop_indices:
                co=mesh.vertices[mesh.loops[loop].vertex_index].co
                if material==materials['skin']:
                    if co.z<1.62 and abs(co.x)>.70:
                        value=((abs(co.x)-.82)/.24,.025+(co.y+.12)/.24*.135)
                    else:
                        angle=math.atan2(co.x,-(co.y-.02))
                        value=(.5+angle/math.tau,.20+(co.z-1.65)/.44*.80)
                elif material==materials['hair'] and obj.name.startswith('Hair'):
                    value=(.5+math.atan2(co.x,-(co.y-.03))/math.tau,
                        .34+(co.z-1.78)/.31*.65)
                elif material==materials['boots']:
                    center=.15 if co.x>0 else -.15
                    value=((co.x-center+.12)/.24,.02+(-co.y+.08)/.42*.23)
                elif material==materials['hair']:
                    # Eye/brow/crease meshes keep a uniform dark facial cue.
                    value=(.50,.012)
                else:
                    axis=max(range(3),key=lambda a:abs(polygon.normal[a]))
                    axes=((1,2),(0,2),(0,1))[axis]
                    value=((co[axes[0]]+1.2)/2.4,
                        (co[axes[1]]+(0 if axes[1]==2 else 1.2))/2.4)
                mapped.append([clamp(value[0]),clamp(value[1])])
            if material in (materials['skin'],materials['hair']) and (
                    max(v[0] for v in mapped)-min(v[0] for v in mapped)>.5):
                for value in mapped:
                    if value[0]<.5: value[0]=1.
            for loop,value in zip(polygon.loop_indices,mapped): uv.data[loop].uv=value
