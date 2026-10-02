"""RF Generation One laboratory computer and independently authored components.

Run through tools/lab_computer.py --build-assets. Source uses RFU dimensions
for convenient mechanical mating, converted to metres before GLB export.
"""
import argparse
from pathlib import Path
import sys
import math
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, select_only

COLORS = ((151, 166, 177), (28, 36, 45), (99, 194, 184), (190, 145, 77),
          (32, 79, 72), (216, 226, 222), (63, 74, 87))


def build(name, b):
    def box(x, y, z, w, h, d, role=0, bevel=5):
        # RF Y-up/+Z-forward -> Blender Z-up/-Y-forward.
        # C1 is authored 4x and mounted at scale=250, retaining the prop profile's
        # 232-unit quantization without changing the shared importer contract.
        unit=128 if name=='cpu' else 512
        if name in ('board', 'cpu', 'memory', 'compute', 'cooling'):
            x,z=-z,x  # Components face the outward (-X) service panel.
            w,d=d,w
        b.box((x/unit, -z/unit, y/unit), (w/unit, d/unit, h/unit),
              accent=role, bevel=bevel/unit)

    def disc(x,y,z,r,depth,role=0,segments=16):
        # Cylinders in the component's authoring plane, along its face normal.
        unit=128 if name=='cpu' else 512
        bpy.ops.mesh.primitive_cylinder_add(vertices=segments, radius=r/unit,
            depth=depth/unit, location=(-z/unit,-x/unit,y/unit), rotation=(0,math.pi/2,0))
        obj=bpy.context.object;obj.data.materials.append(b.materials[role]);b.parts.append(obj)

    def fan(x,y,z,r):
        # Static housing only. Runtime emits rotating blades in the open annulus.
        verts=[];faces=[]
        for depth in (z-5,z+5):
            for radius in (r,r-7):
                for i in range(24):
                    a=i*math.tau/24
                    verts.append((-depth/512,-(x+radius*math.cos(a))/512,(y+radius*math.sin(a))/512))
        for i in range(24):
            j=(i+1)%24
            faces.extend(((i,j,24+j,24+i),(48+i,72+i,72+j,48+j),
                          (i,48+i,48+j,j),(24+i,24+j,72+j,72+i)))
        mesh=bpy.data.meshes.new('fan_ring');mesh.from_pydata(verts,[],faces);mesh.update()
        obj=bpy.data.objects.new('fan_ring',mesh);bpy.context.collection.objects.link(obj)
        obj.data.materials.append(b.materials[0]);b.parts.append(obj)
        disc(x,y,z+5,r*.23,10,1);disc(x,y,z+11,r*.11,3,2)

    def label(text,x,y,z,pitch=3,ink=5):
        glyphs={'R':('110','101','110','101','101'),'F':('111','100','110','100','100'),
                'B':('110','101','110','101','110'),'C':('111','100','100','100','111'),
                'M':('101','111','111','101','101'),'X':('101','101','010','101','101'),
                '1':('010','110','010','010','111'),' ':('000',)*5}
        for n,ch in enumerate(text):
            for row,line in enumerate(glyphs[ch]):
                for col,bit in enumerate(line):
                    if bit=='1':
                        # RF's observer-facing horizontal axis runs opposite the
                        # authored board width on these outward-facing surfaces.
                        box(x+(len(text)*5-3-n*5-col)*pitch,y+(4-row)*pitch,z,
                            pitch*.72,pitch*.72,2,ink,0)

    def model_label(text,x,y,z,pitch=2.1):
        # Continuous 5x7 strokes, two empty columns between characters. Center
        # the whole horizontal word on its plate; never stack letters on DIMMs.
        glyphs={'B':('11110','10001','10001','11110','10001','10001','11110'),
                'M':('10001','11011','10101','10101','10001','10001','10001'),
                'X':('10001','10001','01010','00100','01010','10001','10001'),
                '1':('00100','01100','00100','00100','00100','00100','01110')}
        width=len(text)*7-2
        for n,ch in enumerate(text):
            for row,line in enumerate(glyphs[ch]):
                col=0
                while col<5:
                    if line[col]=='0':
                        col+=1
                        continue
                    end=col+1
                    while end<5 and line[end]=='1':end+=1
                    cx=(width-1)/2-(n*7+(col+end-1)/2)
                    box(x+cx*pitch,y+(3-row)*pitch,z,
                        (end-col)*pitch,pitch,2,5,0)
                    col=end

    if name == 'stand':
        for x in (-390, 390):
            box(x, 18, 0, 110, 36, 500, 1)
            box(x, 290, -125, 64, 544, 90)
        box(0, 555, 0, 920, 46, 500)
        box(0, 524, -165, 780, 32, 55, 1)
        box(0, 547, 248, 620, 12, 6, 2, 0)
        box(130, 616, -100, 60, 76, 80, 1)
    elif name == 'case':
        # Left side opens toward the outside of the desk. Rear plates surround
        # real ventilation / I/O apertures rather than covering them with a slab.
        box(0, 20, 0, 272, 40, 340, 1)
        box(0, 420, 0, 272, 40, 340)
        box(124,220,0,24,360,340)
        for y in (114,252,390):
            for z in (-118,118):box(101,y,z,22,12,12,3,0)
        # Rear pillar ends before the I/O panel; no overlapping coplanar shells.
        box(-124,220,156,24,360,28,1)
        box(-124,220,-144,24,360,8,1,0)
        box(0,220,158,224,360,24,1)
        for y in range(94,350,22):box(-12,y,174,160,8,8,0,0)
        box(74,378,174,16,16,8,2,3)
        box(25,378,175,52,18,8,1,1)
        # Recessed side status strip stays clear of the inspection pane.
        box(-140,32,0,8,18,92,1,1)
        for x in (-126,126):box(x,220,-162,20,360,16,0,0)
        for y in (68,264,394):box(0,y,-162,232,16,16,0,0)
        # Upper rear fan grille, open between bars; I/O column at the right.
        box(62,302,-162,12,172,16,0,0)
        for x in range(-104,56,16):box(x,331,-163,5,116,10,6,0)
        for y in range(280,389,18):box(-26,y,-171,166,4,6,1,0)
        box(91,302,-156,46,168,18,1,0)
        for y in (240,268,296,334,366):
            box(91,y,-169,32,18,10,0,0)
            box(91,y,-176,24,10,5,1,0)
            box(91,y-2,-180,18,4,3,2 if y<300 else 3,0)
        # Expansion brackets and lower PSU inlet, with three visible contacts.
        for y in (108,141,174):
            box(0,y,-162,228,26,12,6,1)
            for x in (-82,-56,-30):box(x,y,-170,16,5,5,1,0)
            box(63,y,-171,48,16,10,0,1);box(63,y,-178,36,8,5,1,0)
        # X1 owns the installed dual-slot rear bracket (case Y=179..257).
        for x in (-111,100):box(x,222,-162,22,68,16,0,0)
        box(0,50,-171,58,28,12,1,2)
        for x in (-16,0,16):box(x,50,-179,4,12,4,3,0)
        for y in (64,384):
            for z in (-136,136):box(-139,y,z,6,14,14,0,2)
        # PSU enclosure and routed power harness remain below the board.
        box(15,67,-35,186,54,236,1,4)
        for z in (112,122,132):
            box(100,198,z,5,198,5,3,0)
            box(82,297,z,36,5,5,3,0)
        for y in (142,244):box(100,y,122,10,8,30,1,0)
        box(0,57,172,80,24,6,1,0)
        label('RF 1',-22,52,176,2.5)
    elif name == 'board':
        box(0,150,0,260,300,6,4,0)
        for x in (-118,118):
            for y in (12,150,288):
                disc(x,y,-7,7,8,3,12);disc(x,y,5,6,4,0,12);disc(x,y,8,2.5,3,1,8)
        # C1 socket / retention arms; CPU remains an independent package.
        box(-22,210,9,82,82,12,1,0)
        for x in (-61,17):box(x,210,19,6,82,8,0,0)
        for y in (171,249):box(-22,y,19,82,6,8,0,0)
        box(24,214,25,5,86,5,3,0)
        # Dual M1 DIMM sockets, full-length gold slot and locking tabs.
        for x in (76,104):
            box(x,205,14,18,152,22,1,0)
            box(x,205,26,6,132,4,3,0)
            for y in (127,283):box(x,y,23,20,10,22,0,1)
        # X1 full length connector and a spare short expansion slot.
        for y,w in ((81,170),(39,100)):
            box(-8,y,15,w,16,22,1,0);box(-8,y,27,w-12,5,4,3,0)
        for x in range(-96,45,22):
            box(x,274,16,14,18,20,6,1)
            disc(x,294,12,5,16,0,12)
        for y in range(164,258,22):
            box(-89,y,15,16,14,18,6,1);disc(-110,y,14,5,18,0,12)
        for x in range(-54,49,12):box(x,268,34,5,19,12,0,0)
        box(51,109,13,40,38,18,6,2)
        for x in (37,49,61):box(x,109,25,5,32,8,0,0)
        for x,y in ((-80,112),(-42,124),(28,54),(86,60)):
            box(x,y,8,18,16,8,1,0)
            for dx in (-13,13):
                for dy in (-5,5):box(x+dx,y+dy,6,6,3,3,3,0)
        # Copper buses and paired solder pads survive RMESH quantization.
        for i in range(5):
            box(-58+i*10,146,5,4,28,3,3,0)
            box(12,118-i*7,5,56,3,3,3,0)
        # Four routed bus bundles with right-angle escapes, solder vias and
        # clear lanes between socket, chipset, DIMMs and expansion connector.
        for i in range(5):
            xx=31+i*7
            box(xx,208,5,3,92-i*10,3,3,0)
            box((xx+68)/2,162+i*5,5,68-xx,3,3,3,0)
            box(-78+i*7,63,5,3,26,3,3,0)
            box(-77+i*6,99-i*5,5,46,3,3,3,0)
            box(89+i*7,86,5,3,32,3,3,0)
            box(72,134+i*5,5,64,3,3,3,0)
            disc(xx,254-i*5,5,2.5,3,3,8)
            disc(-78+i*7,50,5,2.5,3,3,8)
        for x in range(-94,100,14):box(x,16,9,5,8,10,3,0)
        box(124,195,18,12,65,30,1,0)
        for y in range(169,224,9):box(124,y,35,6,4,4,3,0)
        for y in (138,166,194,232,264):
            box(-141,y,7,42,18,28,0,0)
            box(-164,y,7,6,10,20,1,0)
        box(-12,56,7,34,22,4,1,0)
        model_label('B1',-12,56,10)
    elif name == 'cpu':
        box(0,32,0,64,64,6,4,0)
        box(0,32,7,56,56,8,0,2)
        box(0,32,13,48,48,4,5,2)
        for x in (-28,28):
            for y in range(8,60,8):box(x,y,5,4,4,3,3,0)
        box(-20,10,17,6,6,3,3,0)
        label('C1',-9,28,17,2.5,1)
    elif name == 'memory':
        box(0,72,0,5,144,34,4,0)
        for y in range(12,134,16):
            box(-5,y,5,5,12,20,1,0);box(5,y,5,5,12,20,1,0)
            box(0,y,-19,5,9,5,3,0)
        box(0,70,23,24,140,8,0,1)
        box(0,70,29,9,112,4,2,0)
        for y in (4,136):box(0,y,9,15,8,26,6,0)
        box(0,74,31,27,22,4,1,0)
        model_label('M1',0,74,35)
    elif name == 'compute':
        box(0,8,68,224,6,142,4,0)
        box(0,4,10,160,8,6,3,0)
        box(0,17,79,208,12,124,1,1)
        for x in range(-96,103,12):box(x,33,79,4,22,120,0,0)
        for z in (32,58,110):box(0,22,z,212,8,6,3,0)
        box(0,48,79,224,8,144,6,1)
        # Two real side-facing fans in an open shroud.
        for x in (-55,55):fan(x,37,150,31)
        for x in (-110,0,110):box(x,36,151,8,64,12,1,0)
        box(0,70,146,224,16,24,0,1)
        box(0,70,160,34,22,4,1,0)
        model_label('X1',0,70,164)
        box(-132,8,68,40,6,142,4,0)
        box(-158,39,70,8,78,144,0,0)
        for z in (50,106):box(-164,34,z,8,22,38,1,0)
        box(98,40,24,26,24,20,1,0)
        for x in (90,100,110):box(x,54,24,5,5,10,3,0)
    elif name == 'cooling':
        box(0,72,4,70,70,8,3,0)
        for x in (-30,-10,10,30):box(x,72,42,7,92,76,3,1)
        for y in range(10,139,12):box(0,y,56,94,5,76,0,0)
        for x in (-52,52):box(x,72,59,8,144,16,1,0)
        fan(0,72,102,51)
        for x in (-47,47):
            for y in (25,119):disc(x,y,103,5,8,3,12)
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
    for name in ('stand','case','board','cpu','memory','compute','cooling','display','keyboard'):
        b=Builder(mats);build(name,b);select_only(b.parts);bpy.ops.object.join()
        obj=bpy.context.object;obj.name='rf_lab_computer_'+name
        bpy.context.scene.cursor.location=(0,0,0);bpy.ops.object.origin_set(type='ORIGIN_CURSOR')
        bpy.ops.object.transform_apply(location=True,rotation=True,scale=True)
        if name=='board':
            assert obj.dimensions.x < .1 and obj.dimensions.y > .5, 'board must be thin along X'
        # Preserve mating coordinates, bottom pivot, independent object identity.
        mod=obj.modifiers.new('triangles','TRIANGULATE');bpy.ops.object.modifier_apply(modifier=mod.name)
        assert len(obj.data.polygons)<=8000, (name,'triangle budget')
        assert abs(min(v.co.z for v in obj.data.vertices))<1e-6, (name,'bottom pivot')
        assert all(p.area>1e-10 for p in obj.data.polygons), (name,'degenerate face')
        bpy.ops.export_scene.gltf(filepath=str(args.output/(obj.name+'.glb')),
            export_format='GLB',use_selection=True,export_yup=True,
            export_animations=False,export_skins=False,export_morph=False,
            export_texcoords=False,export_normals=True,export_materials='EXPORT')
        print(obj.name, len(obj.data.polygons), 'triangles',flush=True)
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'lab_computer.blend'))

if __name__=='__main__': main()
