"""Research V1: meter-space equipment, four flat materials, no runtime effects."""
import argparse
from pathlib import Path
import sys
import bpy

sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, export, select_only

SPECS = {
    'research_compute_rack': ((1.0, .9, 2.1), 1200),
    'research_build_rack': ((1.0, .9, 2.1), 1200),
    'research_power_cooling': ((.65, .9, 1.85), 1200),
    'research_terminal': ((1.15, .85, 1.8), 1800),
    'core_analysis_station': ((2.4, 1.15, 1.95), 1800),
    'research_prototype_bench': ((2.1, 1.0, 1.6), 1800),
    'research_status_panel': ((1.25, .18, .8), 1200),
    'research_wall_service': ((1.8, .22, .65), 1200),
}
FONT = {
    'H':'101/101/111/101/101',
    'R':'110/101/110/101/101', '0':'111/101/101/101/111',
    '1':'010/110/010/010/111', '2':'110/001/010/100/111',
    'C':'111/100/100/100/111', 'O':'111/101/101/101/111',
    'M':'101/111/111/101/101', 'P':'110/101/110/100/100',
    'U':'101/101/101/101/111', 'T':'111/010/010/010/010',
    'E':'111/100/110/100/111', 'B':'110/101/110/101/110',
    'I':'111/010/010/010/111', 'L':'100/100/100/100/111',
    'D':'110/101/101/101/110', 'A':'010/101/111/101/101',
    'S':'111/100/111/001/111', 'Y':'101/101/010/010/010',
    'N':'101/111/111/111/101', 'V':'101/101/101/101/010',
    ' ':'000/000/000/000/000',
}


def build(kind):
    palette = ((121, 141, 154), (39, 51, 63), (72, 96, 116), (186, 208, 211))
    def linear(rgb):
        return tuple(v/255/12.92 if v <= 10 else ((v/255+.055)/1.055)**2.4 for v in rgb)
    mats = [material('research_'+str(i), linear(c)) for i,c in enumerate(palette)]
    b = Builder(mats)
    def box(c, s, role=0, bevel=.025):
        return b.box(c, s, role, bevel=bevel)
    def label(text, x, y, z, height):
        # Joined planar strokes, one shared ink material; no floating UI or emitters.
        step=height/5; cursor=-(len(text)*4-1)*step/2
        verts=[]; faces=[]
        for ch in text:
            for row,line in enumerate(FONT[ch].split('/')):
                col=0
                while col<3:
                    if line[col]=='0': col+=1; continue
                    end=col+1
                    while end<3 and line[end]=='1': end+=1
                    a=x-(cursor+col*step); d=x-(cursor+end*step)
                    h=z+(4-row)*step; n=len(verts)
                    verts.extend(((a,y,h),(d,y,h),(d,y,h+step),(a,y,h+step)))
                    faces.append((n,n+1,n+2,n+3)); col=end
            cursor+=4*step
        mesh=bpy.data.meshes.new('research_ink'); mesh.from_pydata(verts,[],faces)
        obj=bpy.data.objects.new('research_ink',mesh); bpy.context.collection.objects.link(obj)
        mesh.materials.append(mats[3]); b.parts.append(obj)
    w,d,h=SPECS[kind][0]
    if kind in ('research_compute_rack','research_build_rack','research_power_cooling'):
        box((0,0,.07),(w,d,.14),1)
        box((0,.06,h/2),(w-.12,d-.16,h-.18),2)
        box((0,0,h-.055),(w,d,.11))
        for x in (-w/2+.045,w/2-.045):
            box((x,-d/2+.065,h/2),(.09,.13,h-.16))
        box((0,-.405,h-.25),(w-.2,.07,.25),1)
        if kind=='research_compute_rack':
            label('R01',0,-.447,1.82,.16)
            for z in (.52,1.08):
                box((0,-.28,z),(.75,.30,.44))
                box((0,-.437,z),(.58,.016,.27),1,0)
                for x in (-.26,.26): box((x,-.444,z),(.06,.012,.24),2,0)
            label('COMPUTE',0,-.447,1.52,.11)
        elif kind=='research_build_rack':
            label('R02',0,-.447,1.82,.16)
            for x in (-.19,.19):
                box((x,-.29,.96),(.30,.29,1.13))
                box((x,-.442,.99),(.18,.01,.69),1,0)
                box((x,-.448,.65),(.10,.004,.04),3,0)
            label('BUILD',0,-.447,1.52,.11)
        else:
            label('PDU',0,-.447,1.54,.12)
            for z in (.45,.68,.91,1.14): box((0,-.34,z),(.46,.22,.105),1)
            for x in (-.15,.15):
                box((x,-.36,.23),(.16,.16,.16),0)
                box((x,-.448,.23),(.08,.004,.07),3,0)
    elif kind in ('research_terminal','core_analysis_station'):
        hero=kind=='core_analysis_station'
        box((0,0,.08),(w,d,.16),1)
        for x in (-w*.34,w*.34):
            box((x,.1,.53),(.23,.54,.9),2)
            box((x,-.22,.35),(.16,.10,.28),0)
        box((0,.2,1.27),(w-.16,.26,h-1.0),1)
        box((0,.17,h-.06),(w-.10,.36,.12),0)
        for x in (-w/2+.12,w/2-.12):
            box((x,.08,(h+.96)/2),(.14,.38,h-.96),0)
        box((0,.025,1.47),(w-.48,.06,.49),2,0)
        box((0,-.025,.97),(w-.05,d-.10,.15),0)
        box((0,-d/2+.08,1.04),(w-.20,.16,.055),1)
        for x in (-w*.28,w*.28):
            box((x,-.17,1.065),(.21,.22,.05),2)
            box((x,-.20,1.095),(.12,.07,.008),3,0)
        label('CORE' if hero else 'R01',0,-.022,1.53,.15)
        # Readable static analysis blocks with broad separators.
        for x,z,sw in ((-.20,1.32,.25),(.15,1.35,.12),(.30,1.27,.15)):
            box((x,-.020,z),(sw,.008,.035),3,0)
        if hero:
            for x in (-.84,.84):
                box((x,-.13,.66),(.36,.62,.38),0)
                box((x,-.451,.67),(.25,.02,.22),1,0)
                box((x,-.467,.68),(.15,.012,.055),3,0)
            box((0,.20,.55),(.82,.40,.62),2)
            box((0,-.015,.55),(.53,.06,.34),1)
            for x in (-.32,.32): box((x,-.08,.57),(.09,.10,.38),0)
            label('ANALYSIS',0,-.565,.915,.11)
        else:
            box((0,.02,.56),(.60,.42,.67),2)
            box((0,-.211,.62),(.35,.045,.30),1)
            label('CONTROL',0,-.415,.92,.10)
    elif kind=='research_prototype_bench':
        box((0,0,.06),(w,d,.12),1)
        for x in (-.80,.80):
            box((x,.03,.47),(.30,.70,.82),2)
            box((x,-.335,.49),(.21,.045,.51),0)
        box((0,0,.92),(w,.98,.16),0)
        box((0,-.475,.81),(1.90,.05,.10),1)
        box((-.30,.05,1.035),(.90,.65,.07),1)
        # Gantry and instrumented test fixture, distinct from a storage workbench.
        for x in (-.68,.12):
            box((x,.30,1.28),(.12,.18,.48),2)
            box((x,.27,1.54),(.20,.30,.12),0)
        box((-.28,.28,1.54),(.90,.22,.12),0)
        for x in (-.55,-.05): box((x,-.04,1.15),(.12,.36,.20),0)
        box((-.30,.01,1.21),(.36,.28,.20),2)
        box((-.30,-.14,1.22),(.23,.03,.10),3,0)
        box((.64,.20,1.20),(.49,.20,.38),1)
        box((.64,.089,1.23),(.35,.02,.21),2,0)
        label('TEST',.64,.075,1.20,.09)
        box((.64,-.19,1.03),(.48,.30,.06),2)
        box((.65,-.21,1.066),(.24,.09,.008),3,0)
        label('PROTOTYPE',0,-.498,.86,.10)
    elif kind=='research_status_panel':
        box((0,.03,h/2),(w,d-.06,h),1)
        for x in (-w/2+.035,w/2-.035):
            box((x,0,h/2),(.07,d,h),1)
        box((0,-.042,.40),(w-.12,.065,h-.12),0)
        box((0,-.078,.44),(1.04,.006,.49),2,0)
        label('RESEARCH',0,-.089,.51,.15)
        label('R01',0,-.089,.28,.12)
        for x in (-.45,.45): box((x,-.084,.15),(.12,.006,.035),3,0)
    else:
        box((0,.03,h/2),(w,d-.06,h),1)
        for x in (-w/2+.035,w/2-.035):
            box((x,0,h/2),(.07,d,h),1)
        box((-.52,-.05,.33),(.61,.10,.49),0)
        box((.42,-.04,.33),(.88,.13,.40),2)
        for z in (.20,.44): box((.42,-.107,z),(.76,.006,.07),0,0)
        label('SERVICE',-.52,-.109,.32,.085)
        box((-.52,-.105,.19),(.22,.01,.045),3,0)
    return b


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    p.add_argument('--overwrite',action='store_true')
    a=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    if a.output.exists() and any(a.output.iterdir()) and not a.overwrite:
        raise FileExistsError('Use --overwrite to replace generated source')
    a.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system='METRIC'
    objects=[]
    for kind,(dims,budget) in SPECS.items():
        name='rf_'+kind
        obj=build(kind).finish(name,dims,budget)
        obj['hybrid']=True  # Common four-material static export contract.
        obj.data.name=name; objects.append(obj)
        export(a.output/(name+'.glb'),[obj])
    for obj in objects: obj.hide_set(obj!=objects[0])
    select_only([objects[0]])
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(a.output/'research_v1.blend'))


if __name__=='__main__': main()
