"""Frontier Station industrial kit: metres, flat materials, sparse readable forms.

The existing Builder/export boundary owns normalization to GLB. No external
textures, downloaded meshes, or baked lighting are used by this kit.
"""
import argparse
import math
from pathlib import Path
import sys

import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, export, material, select_only

SPECS = {
    "frontier_canopy": ((6, 3, .4), 240),
    "frontier_cargo_rack": ((4, 1.8, 3.2), 700),
    "frontier_fence": ((6, .32, 2.5), 350),
    "frontier_collector": ((4.8, 3.4, 4.5), 1000),
    "frontier_rock": ((4, 3, 1.8), 150),
    "frontier_bollard": ((.32, .32, 1.2), 120),
}


def build(kind):
    colors = [(124, 140, 148), (46, 58, 64), (101, 118, 104), (192, 148, 79)]
    if kind == "frontier_rock":
        colors = [(119, 128, 125), (87, 101, 103), (141, 143, 127), (111, 119, 110)]
    def linear(rgb):
        return tuple(c/255/12.92 if c<=10 else ((c/255+.055)/1.055)**2.4 for c in rgb)
    mats=[material(kind+str(i),linear(c)) for i,c in enumerate(colors)]
    b=Builder(mats)
    def box(center,size,role=0):
        return b.box(center,size,role,bevel=0)
    if kind == "frontier_canopy":
        # Bottom pivot is the underside of the beams; broad deck spans them.
        box((0,0,.34),(6,3,.12))
        for y in (-1.35,1.35):
            box((0,y,.14),(6,.22,.28),1)
        for x in (-2.85,2.85):
            box((x,0,.14),(.30,3,.28),1)
        box((0,0,.14),(.16,2.7,.28),1)
    elif kind == "frontier_cargo_rack":
        for x in (-1.91,1.91):
            for y in (-.81,.81):
                box((x,y,1.6),(.18,.18,3.2),1)
        for z in (.18,1.52,2.9):
            box((0,0,z),(3.82,1.8,.16),0)
        # Only broad, distinct cargo blocks; open shelf fronts are readable.
        for x in (-1.12,1.03):
            box((x,.08,.78),(1.34,1.28,1.02),2)
            box((x,-.575,.8),(1.05,.03,.52),1)
            box((x+.39,-.60,.93),(.16,.02,.21),3)
        for x in (-1.2,0,1.2):
            box((x,.15,2.22),(.92,1.05,1.18),2)
            box((x,-.385,2.28),(.68,.03,.70),0)
        for x in (-1.91,1.91):
            box((x,-.805,.58),(.18,.19,.48),3)
    elif kind == "frontier_fence":
        for x in (-2.92,2.92):
            box((x,0,1.25),(.16,.32,2.5),1)
        box((0,0,.5),(5.84,.20,1),0)
        for z in (1.08,2.40):
            box((0,0,z),(5.84,.14,.14),1)
        for x in (-2,-1,0,1,2):
            box((x,0,1.74),(.10,.12,1.18),1)
        box((0,-.111,.48),(.75,.022,.28),2)
    elif kind == "frontier_collector":
        box((0,0,.14),(4.8,3.4,.28),1)
        box((0,.3,1.10),(3.72,2.35,1.92),2)
        box((0,.3,2.12),(4.15,2.65,.16),0)
        for x in (-1.60,1.60):
            box((x,-1.0,1.17),(.24,.28,1.75),1)
        box((0,-.93,1.25),(2.4,.14,.90),1)
        for x in (-.77,.77):
            box((x,-1.04,1.28),(.82,.12,.65),0)
        for x in (-1.18,1.18):
            b.cylinder((x,.32,2.91),.66,1.5,0)
            b.cylinder((x,.32,2.28),.76,.16,1)
            b.cylinder((x,.32,3.64),.76,.16,1)
            box((x,-.35,2.92),(.30,.10,.84),3)
        box((0,.32,4.03),(.48,.55,.65),1)
        box((0,.32,4.42),(3.6,.95,.16),0)
        for x in (-1.15,1.15):
            box((x,.32,3.92),(.22,.4,.42),1)
        box((0,-1.15,.74),(.70,.22,.88),0)
        box((0,-1.27,.92),(.42,.035,.26),1)
    elif kind == "frontier_rock":
        # An asymmetric convex low-poly outcrop with a broad grounded base.
        rings=[(0,[( -1.7,-1.1),( .4,-1.5),(2,-.7),(1.7,1.2),(-.8,1.5),(-2,.3)]),
               (1.08,[(-1.4,-.8),(.3,-1.1),(1.3,-.5),(1.4,.8),(-.6,1.0),(-1.5,.2)]),
               (1.8,[(-.55,-.3),(.35,-.45),(.55,-.05),(.4,.4),(-.4,.45),(-.7,.1)])]
        vertices=[(x,y,z) for z,ring in rings for x,y in ring]
        faces=[tuple(range(5,-1,-1))]
        for level in range(2):
            for i in range(6):
                faces.append((level*6+i,level*6+(i+1)%6,(level+1)*6+(i+1)%6,(level+1)*6+i))
        faces.append(tuple(range(12,18)))
        mesh=bpy.data.meshes.new(kind);mesh.from_pydata(vertices,[],faces);mesh.update()
        obj=bpy.data.objects.new(kind,mesh);bpy.context.collection.objects.link(obj)
        for mat in mats:mesh.materials.append(mat)
        for i,poly in enumerate(mesh.polygons):poly.material_index=(i*7)%3
        b.parts.append(obj)
    else:
        box((0,0,.06),(.32,.32,.12),1)
        box((0,0,.65),(.20,.20,1.1),3)
        box((0,0,.85),(.205,.205,.22),1)
        box((0,0,1.15),(.24,.24,.1),0)
    return b


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output",type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index("--")+1:])
    args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system="METRIC"
    objects=[]
    for kind,(dimensions,budget) in SPECS.items():
        obj=build(kind).finish("rf_"+kind,dimensions,budget)
        obj["hybrid"]=True
        obj.data.name=obj.name
        used=sorted({p.material_index for p in obj.data.polygons})
        indices=[used.index(p.material_index) for p in obj.data.polygons]
        materials=[obj.data.materials[i] for i in used]
        obj.data.materials.clear()
        for mat in materials:obj.data.materials.append(mat)
        for poly,index in zip(obj.data.polygons,indices):poly.material_index=index
        export(args.output/("rf_"+kind+".glb"),[obj])
        objects.append(obj)
    for obj in objects:obj.hide_set(obj!=objects[0])
    select_only([objects[0]])
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/"frontier_station.blend"))


if __name__=="__main__":main()
