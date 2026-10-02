"""RF laboratory computer: six independently authored, metre-scale modules.

Run through tools/lab_computer.py --build-assets. Source uses RFU dimensions
for convenient mechanical mating, converted to metres before GLB export.
"""
import argparse
from pathlib import Path
import sys
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, select_only

COLORS = ((119, 139, 150), (34, 46, 56), (117, 195, 186), (181, 151, 106))


def build(name, b):
    def box(x, y, z, w, h, d, role=0, bevel=5):
        # RF Y-up/+Z-forward -> Blender Z-up/-Y-forward.
        # Keep each prop above glb2rmesh's tiny-mesh precision threshold.
        unit=512/1.25 if name=='cooling' else 512
        b.box((x/unit, -z/unit, y/unit), (w/unit, d/unit, h/unit),
              accent=role, bevel=bevel/512)

    if name == 'stand':
        for x in (-390, 390):
            box(x, 18, 0, 110, 36, 500, 1)
            box(x, 290, -125, 64, 544, 90)
        box(0, 555, 0, 920, 46, 500)
        box(0, 524, -165, 780, 32, 55, 1)
        box(0, 547, 248, 620, 12, 6, 2, 0)
        box(130, 616, -100, 60, 76, 80, 1)
    elif name == 'case':
        # Open front inspection bay; no hidden solid block behind the window.
        box(0, 20, 0, 272, 40, 340, 1)
        box(0, 420, 0, 272, 40, 340)
        box(0, 220, -155, 272, 360, 30, 1)
        for x in (-122, 122):
            box(x, 220, 0, 28, 360, 340)
        box(0, 58, 150, 216, 36, 32, 1)
        box(0, 389, 150, 216, 22, 32, 1)
        for x in (-96, 96):
            box(x, 222, 158, 16, 312, 24, 1)
        # Discrete drive and power-button cluster in the lower fascia.
        box(24, 58, 168, 100, 10, 4, 0, 0)
        box(-68, 58, 168, 16, 16, 4, 2, 2)
        for y in (115, 155, 195, 235, 275):
            box(137, y, -70, 4, 16, 100, 1, 0)
    elif name == 'board':
        box(0, 145, 0, 176, 290, 12, 2, 0)
        box(10, 170, 12, 66, 66, 12, 1, 2)
        box(10, 170, 22, 48, 48, 8, 0, 2)
        for x in (-66, -44):
            box(x, 182, 14, 12, 132, 16, 1, 0)
            box(x, 182, 24, 8, 92, 4, 3, 0)
        for y in (38, 64):
            box(10, y, 16, 112, 14, 18, 1, 0)
        for x in (48, 68):
            box(x, 248, 15, 10, 30, 18, 0, 1)
        box(-10, 98, 12, 120, 5, 4, 3, 0)
    elif name == 'cooling':
        box(0, 64, 0, 112, 128, 66, 1)
        for x in (-43, -26, -9, 9, 26, 43):
            box(x, 64, 37, 7, 108, 14, 0, 0)
        # An octagonal fan ring and four broad vanes read through the pane.
        for x,y,w,h in ((0,120,104,10),(0,8,104,10),(-51,64,10,104),(51,64,10,104)):
            box(x,y,52,w,h,10,2,1)
        box(0,64,55,22,22,14,1,3)
        for x,y,w,h in ((-25,64,30,12),(25,64,30,12),(0,39,12,30),(0,89,12,30)):
            box(x,y,55,w,h,8,0,2)
    elif name == 'display':
        box(0, 202, -23, 560, 404, 54, 1, 8)
        # The live 512 x 320 RFU face fits the inner bezel exactly.
        box(0, 24, 16, 560, 48, 36)
        box(0, 386, 16, 560, 36, 36)
        for x in (-270, 270):
            box(x, 208, 16, 20, 320, 36)
        box(198, 22, 36, 24, 6, 4, 2, 0)
        for x in (-130, 0, 130):
            box(x, 252, -52, 60, 100, 8, 0, 1)
    elif name == 'keyboard':
        box(0, 12, 0, 500, 24, 170, 1, 7)
        for row in range(3):
            for col in range(12):
                box(-218+col*36, 29, -50+row*34, 29, 10, 25,
                    2 if row==1 and col==2 else 0, 0)
        box(-25, 29, 58, 190, 10, 24, 0, 2)
        box(213, 29, 36, 40, 10, 62, 2, 3)
    else:
        raise ValueError(name)


def main():
    p=argparse.ArgumentParser();p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    def linear(rgb):
        return tuple(v/255/12.92 if v<=10 else ((v/255+.055)/1.055)**2.4 for v in rgb)
    mats=[material('lab_'+str(i),linear(rgb)) for i,rgb in enumerate(COLORS)]
    for name in ('stand','case','board','cooling','display','keyboard'):
        b=Builder(mats);build(name,b);select_only(b.parts);bpy.ops.object.join()
        obj=bpy.context.object;obj.name='rf_lab_computer_'+name
        bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        # Preserve mating coordinates, bottom pivot, independent object identity.
        mod=obj.modifiers.new('triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
        assert len(obj.data.polygons)<=900, (name,'triangle budget')
        assert abs(min(v.co.z for v in obj.data.vertices))<1e-6, (name,'bottom pivot')
        assert all(p.area>1e-10 for p in obj.data.polygons), (name,'degenerate face')
        bpy.ops.export_scene.gltf(filepath=str(args.output/(obj.name+'.glb')),
            export_format='GLB',use_selection=True,export_yup=True,
            export_animations=False,export_skins=False,export_morph=False,
            export_texcoords=False,export_normals=True,export_materials='EXPORT')
        print(obj.name, len(obj.data.polygons), 'triangles',flush=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'lab_computer.blend'))

if __name__=='__main__': main()
