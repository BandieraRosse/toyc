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
            # Blender -Y exports to runtime +Z. The front lens normal is
            # (0,-.6,-.8), matching the runtime socket's forward/down axis.
            b.box((0,.08,.225),(.65,.09,.45),0,bevel=0)
            vertices=[(-.285,.011,.109),(.285,.011,.109),
                      (.285,-.125,.211),(-.285,-.125,.211),
                      (-.285,.07,.109),(.285,.07,.109),
                      (.285,.07,.33),(-.285,.07,.33)]
            mesh=bpy.data.meshes.new('wall_optical_housing')
            mesh.from_pydata(vertices,[],[(0,1,2,3),(4,7,6,5),(0,4,5,1),
                                         (3,2,6,7),(0,3,7,4),(1,5,6,2)])
            housing=bpy.data.objects.new('wall_optical_housing',mesh)
            bpy.context.collection.objects.link(housing)
            housing.data.materials.append(mats[1]);b.parts.append(housing)
            # One luminous face, inset within the dark frame. No luminous
            # backside/edges and no coplanar white/dark polygons.
            lens=bpy.data.meshes.new('wall_aperture')
            lens.from_pydata([(x,-.057-.8*t-.0006,.16+.6*t-.0008)
                              for x,t in ((-.24,-.065),(.24,-.065),(.24,.065),(-.24,.065))],[],[(0,1,2,3)])
            aperture=bpy.data.objects.new('wall_aperture',lens)
            bpy.context.collection.objects.link(aperture)
            aperture.data.materials.append(mats[2]);b.parts.append(aperture)
            b.box((0,-.025,.38),(.65,.20,.07),0,bevel=0)
        obj=b.finish('rf_light_'+kind,dims,240)
        obj['hybrid']=True
        obj.data.name=obj.name
        if kind=='wall':
            emitting=[p for p in obj.data.polygons if p.material_index==2]
            assert len(emitting)==2 and all(p.normal.y<-.599 and p.normal.z<-.799 for p in emitting)
        export(args.output/(obj.name+'.glb'),[obj])
if __name__=='__main__':main()
