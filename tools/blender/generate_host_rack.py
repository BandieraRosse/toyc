"""Host Rack V2: six large bays, shared frame and readable hardware."""
import argparse
from pathlib import Path
import sys
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, export

SPECS = {
    'rack_frame': (.80, 1.06, 2.2),
    'blank_panel': (.70, 1.00, .26),
    'cpu_module': (.70, 1.00, .26),
    'memory_module': (.70, 1.00, .26),
    'rack_fan_panel': (.70, 1.00, .30),
    'cpu_header': (.70, 1.00, .17),
    'memory_header': (.70, 1.00, .17),
    'power_bundle': (.08, .60, 1.10),
    'data_bundle': (.05, .60, 1.10),
}


def build(kind):
    # Flat materials: framed open apertures preserve hardware visibility in
    # both renderers; the static RMESH path does not implement tinted glass.
    # sRGB industrial shell, recessed displays and restrained terminal ink.
    palette = [(0x11, 0x14, 0x18), (0x1b, 0x20, 0x26),
               (0x45, 0x51, 0x5b), (0x8f, 0xa4, 0xb5),
               (0x36, 0xa8, 0xff), (0x4b, 0xe3, 0x8a),
               (0x0a, 0x11, 0x16), (0xf2, 0xc1, 0x4e),
               (0x0d, 0x15, 0x1b), (0xdc, 0xe8, 0xf2),
               (0x1e, 0x6f, 0xaf), (0x1e, 0x8e, 0x59)]
    def linear(rgb):
        return tuple(c/255/12.92 if c <= 10 else ((c/255+.055)/1.055)**2.4 for c in rgb)
    mats = [material('host_'+str(i), linear(rgb)) for i, rgb in enumerate(palette)]
    b = Builder(mats)
    def box(p, s, m=0):
        return b.box(p, s, m, bevel=0)
    def label(text, p, size, m=3):
        # Thick 5x7 glyphs survive RMESH integer quantization at room distance.
        font = {
            'A':'01110/10001/10001/11111/10001/10001/10001',
            'B':'11110/10001/10001/11110/10001/10001/11110',
            'C':'01111/10000/10000/10000/10000/10000/01111',
            'E':'11111/10000/10000/11110/10000/10000/11111',
            'G':'01111/10000/10000/10111/10001/10001/01110',
            'H':'10001/10001/10001/11111/10001/10001/10001',
            'I':'111/010/010/010/010/010/111',
            'K':'10001/10010/10100/11000/10100/10010/10001',
            'L':'10000/10000/10000/10000/10000/10000/11111',
            'M':'10001/11011/10101/10101/10001/10001/10001',
            'N':'10001/11001/11001/10101/10011/10011/10001',
            'O':'01110/10001/10001/10001/10001/10001/01110',
            'P':'11110/10001/10001/11110/10000/10000/10000',
            'R':'11110/10001/10001/11110/10100/10010/10001',
            'S':'01111/10000/10000/01110/00001/00001/11110',
            'T':'11111/00100/00100/00100/00100/00100/00100',
            'U':'10001/10001/10001/10001/10001/10001/01110',
            'W':'10001/10001/10001/10101/10101/11011/10001',
            'Y':'10001/10001/01010/00100/00100/00100/00100',
            '/':'00001/00001/00010/00100/01000/10000/10000',
            ' ':'000/000/000/000/000/000/000'}
        step=size/7
        width=sum(len(font[c].split('/')[0])+1 for c in text)-1
        verts,faces=[],[]
        cursor=-width*step/2
        for c in text:
            rows=font[c].split('/')
            for row,line in enumerate(rows):
                for col,bit in enumerate(line):
                    if bit=='0': continue
                    x=p[0]-(cursor+col*step); z=p[2]+(6-row)*step
                    n=len(verts)
                    verts.extend([(x,p[1],z),(x-step,p[1],z),
                                  (x-step,p[1],z+step),(x,p[1],z+step)])
                    faces.append((n,n+1,n+2,n+3))
            cursor+=(len(rows[0])+1)*step
        mesh=bpy.data.meshes.new('rack_label'); mesh.from_pydata(verts,[],faces)
        obj=bpy.data.objects.new('rack_label',mesh); bpy.context.collection.objects.link(obj)
        mesh.materials.append(mats[m])
        b.parts.append(obj)
    if kind == 'rack_frame':
        for z in (.035, 2.165):
            box((0, 0, z), (.80, 1.06, .07), 0)
        for x in (-.375, .375):
            for y in (-.50, .50):
                box((x, y, 1.1), (.05, .06, 2.06), 2)
        for x in (-.345, .345):
            box((x, -.51, 1.16), (.025, .025, 1.66), 3)
        # Rear cable exit and a grounded power supply plinth.
        box((.20, .475, 2.04), (.12, .09, .09), 6)
        for x in (-.20, .20):
            box((x, .48, .20), (.23, .09, .18), 2)
    elif kind in ('cpu_header', 'memory_header'):
        box((0, .003, .085), (.70, .994, .17), 1)
        label('HOST', (0, -.500, .066), .050)
        label('CPU / COMPUTE' if kind == 'cpu_header' else 'MEMORY / BANK',
              (0, -.500, .002), .045, 4 if kind == 'cpu_header' else 5)
    elif kind in ('cpu_module', 'memory_module', 'blank_panel'):
        box((0, 0, .009), (.57, .96, .018), 1)
        box((0, 0, .191), (.57, .96, .018), 0)
        box((0, .466, .10), (.57, .028, .164), 1)
        box((0, -.452, .10), (.57, .024, .164), 1)
        for x in (-.274, .274):
            # Side aperture: 67% length, 50% height, per module.
            for z in (.037, .163):
                box((x, 0, z), (.022, .91, .038), 2)
            for y in (-.39, .39):
                box((x, y, .10), (.022, .14, .088), 0)
        if kind == 'blank_panel':
            for x in (-.273, .273):
                box((x, 0, .10), (.024, .91, .09), 1)
            for z in (.072, .13):
                box((0, -.470, z), (.48, .008, .012), 2)
        else:
            cpu = kind == 'cpu_module'
            signal = 4 if cpu else 5
            # Recessed blue-grey display behind the world-space telemetry.
            box((0, -.469, .105), (.52, .006, .092), 1)
            box((0, -.473, .105), (.48, .006, .080), 6)
            # Dark strip bed; the live presentation overlays the lit portion.
            box((0, -.470, .033), (.48, .008, .015), 10 if cpu else 11)
            if cpu:
                for row in range(4):
                    for i in range(17):
                        box((-.224+i*.028, -.470, .074+row*.022), (.018, .008, .009), 6)
            else:
                for i in range(12):
                    x = -.22+i*.038+(0.012 if i >= 6 else 0)
                    box((x, -.472, .112), (.023, .008, .085), 2)
                    box((x, -.477, .112), (.009, .006, .066), 6)
            for i, mat in enumerate((5, signal, 3)):
                box((.175+i*.026, -.474, .143), (.012, .006, .012), mat)
            box((.217, -.474, .096), (.026, .006, .018), 2)
            label('CPU' if cpu else 'MEMORY', (-.09, -.480, .156), .042, signal)
            box((0, -.02, .030), (.48, .79, .014), 0 if not cpu else 1)
            # Side-facing fan housings; rotating blades are presentation geometry.
            for x in (-.238, .238):
                for y in (-.27, .27):
                    box((x, y, .105), (.024, .105, .105), 6)
            if cpu:
                for y in (-.22, .055):
                    box((0, y, .050), (.40, .225, .022), 2)
                    box((0, y, .068), (.43, .20, .018), 3)
                    for i in range(10):
                        box((0, y-.09+i*.020, .118), (.43, .009, .085), 3)
            else:
                # 16 independent thin boards, two banks of eight; each bank
                # has a 4+4 rhythm and visible chips on its outward face.
                for x in (-.135, .135):
                    for i in range(8):
                        y = -.30+i*.058+(0.02 if i >= 4 else 0)
                        box((x, y, .048), (.19, .027, .019), 2)
                        box((x, y, .103), (.18, .009, .10), 0)
                        for dx in (-.060, -.020, .020, .060):
                            box((x+dx, y-.007, .107), (.027, .009, .056), 6)
                        for dx in (-.094, .094):
                            box((x+dx, y, .075), (.01, .022, .055), 3)
        for x in (-.258, .258):
            for z in (.06, .15):
                box((x, -.474, z), (.016, .006, .012), 3)
        # Metre-space expansion, not a runtime scale compensation.
        for part in b.parts:
            part.location.x *= .70/.57
            part.location.y *= 1.00/.96
            part.location.z *= .26/.20
            part.scale.x *= .70/.57
            part.scale.y *= 1.00/.96
            part.scale.z *= .26/.20
    elif kind == 'rack_fan_panel':
        box((0, .01, .15), (.70, .98, .30), 1)
        for x in (-.19, .19):
            box((x, -.485, .17), (.27, .010, .16), 6)
            for i in range(6):
                box((x, -.494, .108+i*.025), (.24, .008, .008), 2)
        label('COOLING / POWER', (0, -.500, .035), .037, 3)
        box((.29, -.496, .048), (.015, .008, .012), 5)
    else:
        w = SPECS[kind][0]
        m = 6 if kind == 'power_bundle' else 4
        box((0, -.3+w/2, .55), (w, w, 1.10), m)
        box((0, 0, 1.10-w/2), (w, .60, w), m)
        for z in (.18, .55, .92):
            box((0, -.3+w/2, z), (w, w, .022), 2)
    # Remove unused slots before the common GLB primitive/bounds checks.
    used = [m for m in mats if any(m in list(p.data.materials) for p in b.parts)]
    b.materials = used
    return b


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    args.output.mkdir(parents=True, exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    for kind, dims in SPECS.items():
        name = 'rf_host_'+kind
        obj = build(kind).finish(name, dims, 6000)
        obj.data.name = name
        export(args.output/(name+'.glb'), [obj])
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'host_rack_v2.blend'))


if __name__ == '__main__':
    main()
