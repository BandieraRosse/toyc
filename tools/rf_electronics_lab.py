#!/usr/bin/env python3
"""Emit the RF Gen1 product gallery using the shared lab and assembly producers."""
import argparse
from pathlib import Path
from experiment_lab import generate as lab
from lab_computer import generate as computer,LAB_GROUPS

def generate():
    name='rf_electronics_lab';area=name+'_area';group=LAB_GROUPS[name]
    records=[r for r in lab(name,16384,-7000,'model','open').splitlines()
             if '_sample_info ' not in r]
    records=[r.replace('attr.text=MODEL_LAB','attr.text=RF_ELECTRONICS_GEN_1') for r in records]
    # West-side access joins the existing experiment road without cutting the yard wall.
    records.extend([
        'surface id=rf_electronics_access kind=ground min_x=8192 max_x=11264 min_z=-11776 max_z=-7000 height=0 material=485860 attr.collision_id=rf_electronics_access_col',
        'collision id=rf_electronics_access_col shape=flat min_x=8192 max_x=11264 min_z=-11776 max_z=-7000 height=0 collision=false visible=true walkable=true color=485860',
        'render id=rf_electronics_access_paint kind=floor min_x=8192 max_x=11264 min_z=-11776 max_z=-7000 height=0 color=485860'])
    # Independent exhibit specimens are enlarged for inspection, explicitly labelled.
    for part,x,scale,title in (('cpu',-3000,1500,'C1_CPU_6X'),('memory',-1000,3000,'M1_MEMORY_3X'),
                               ('board',1000,2500,'B1_MAINBOARD_2.5X'),('compute',3000,3000,'X1_COMPUTE_3X')):
        ident='rf_product_'+part
        records.extend([
            f'collision id={ident}_base_col shape=box min_x={x-550} max_x={x+550} min_z=-1900 max_z=-1100 height=620 collision=true visible=false attr.lab={area}',
            f'render id={ident}_base kind=box min_x={x-550} max_x={x+550} min_z=-1900 max_z=-1100 height=620 color=263743 attr.lab={area}',
            f'object id={ident} kind=lab_computer_{part} x={x} y=620 z=-1500 yaw=90 scale={scale} attr.length={group} attr.lab={area}',
            f'render id={ident}_label kind=sign min_x={x-600} max_x={x+600} min_z=-800 max_z=-800 height=-420 color=C4D9F4 attr.height2=-240 attr.style=3 attr.text={title} attr.facing=+z attr.texture_u=1 attr.lab={area}'])
    records.extend(computer('rf_electronics_demo',0,1100,name,'9FB4FF',area).splitlines())
    records.append(f'render id=rf_product_system_label kind=sign min_x=-900 max_x=900 min_z=1800 max_z=1800 height=-650 color=C4D9F4 attr.height2=-450 attr.style=3 attr.text=RF_GEN1_WORKSTATION_1X attr.facing=+z attr.texture_u=1 attr.lab={area}')
    return '\n'.join(records)+'\n'

if __name__=='__main__':
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--output',type=Path,required=True)
    a=p.parse_args()
    with a.output.open('x',encoding='utf-8',newline='\n') as f:f.write(generate())
