"""Original ceiling and wall luminaires. Bottom-centred origin, +Z front.

Emitter sockets in RFU are owned by rasterfall_prop_light_profile; the ceiling
aperture is the bottom face, the wall aperture is the forward sloping lens.
"""
import argparse
from pathlib import Path
import sys
import bpy
sys.path.insert(0,str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, export, material

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--output',type=Path,required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    colors=((.11,.15,.18),(.035,.045,.055),(.78,.86,.91))
    mats=[material('luminaire_'+str(i),c,metallic=0.0,roughness=.6) for i,c in enumerate(colors)]
    for kind,dims in [('ceiling',(1.4,.7,.16)),('wall',(.65,.25,.45))]:
        b=Builder(mats)
        if kind=='ceiling':
            b.box((0,0,.10),(1.4,.7,.12),0,bevel=0)
            # The lens owns its bottom plane. A full dark box behind it used
            # to share z=0 with the luminous face and flicker black at angles.
            b.box((0,0,.033),(1.22,.57,.014),1,bevel=0)
            for x in (-.5875,.5875):
                b.box((x,0,.013),(.045,.57,.026),1,bevel=0)
            for y in (-.25,.25):
                b.box((0,y,.013),(1.13,.07,.026),1,bevel=0)
            b.box((0,0,.013),(1.13,.43,.026),2,bevel=0)
            for x in (-.65,.65):b.box((x,0,.035),(.08,.64,.07),0,bevel=0)
        else:
            # Blender -Y exports to runtime +Z. A wall mount, hood and lens.
            b.box((0,.08,.225),(.65,.09,.45),0,bevel=0)
            b.box((0,-.02,.22),(.57,.2,.28),1,bevel=0)
            b.box((0,-.113,.16),(.48,.024,.16),2,bevel=0)
            b.box((0,-.025,.38),(.65,.20,.07),0,bevel=0)
        obj=b.finish('rf_light_'+kind,dims,240)
        obj['hybrid']=True
        obj.data.name=obj.name
        export(args.output/(obj.name+'.glb'),[obj])
if __name__=='__main__':main()
