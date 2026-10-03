#!/usr/bin/env python3
"""Build independent lab computer assets or emit the reusable map assembly."""
import argparse
import re
from pathlib import Path
import subprocess
import struct
import sys

ROOT=Path(__file__).resolve().parents[1]
LAB_GROUPS={name:i+1 for i,name in enumerate(('character_lab','walk_lab','actor_actions_lab',
    'actor_walk_lab','rf_model_lab','rf_light_lab','rf_electronics_lab','rifle_cycle_lab','idle_rifle_lab'))}
PARTS=(('', 'stand', 0, 0, 0), ('_case','case',-320,578,-30),
       ('_board','board',-240,680,-30), ('_cpu','cpu',-258,858,-52),
       ('_memory_a','memory',-281,815,46), ('_memory_b','memory',-281,815,74),
       ('_compute','compute',-257,757,-30), ('_cooling','cooling',-273,818,-52),
       ('_monitor','display',130,628,-64), ('_keyboard','keyboard',130,578,140))

def generate(identity,x,z,channel,color,lab=None):
    for value in (identity,channel,lab):
        if value is not None and not re.fullmatch(r'[A-Za-z_][A-Za-z_0-9.-]{0,45}',value):
            raise ValueError('computer id/channel/lab must be a stable name, at most 46 characters')
    if not re.fullmatch(r'[0-9A-Fa-f]{6}',color): raise ValueError('color must be six hex digits')
    if max(abs(x),abs(z))>1000000: raise ValueError('origin exceeds assembly range')
    assembly=identity+'_assembly'
    lines=[f'assembly id={assembly} x={x} y=0 z={z}'+(f' attr.lab={lab}' if lab else '')]
    for suffix,part,px,py,pz in PARTS:
        scale=250 if part=='cpu' else 1000
        group=LAB_GROUPS.get(channel,0)
        lines.append(f'object id={identity}{suffix} kind=lab_computer_{part} x={px} y={py} z={pz} yaw=0 scale={scale} attr.assembly={assembly} attr.length={group}'+(' attr.collision=component' if not suffix else ''))
    lines.append(f'render id={identity}_screen kind=sign min_x=-126 max_x=386 min_z=-29 max_z=-29 height=676 color={color} attr.height2=996 attr.style=5 attr.text=READY attr.facing=+z attr.texture_u=256 attr.texture_v=160 attr.channel={channel}_computer attr.assembly={assembly}')
    # Outward (-X) service pane, with a real open frame in the case asset.
    lines.append(f'render id={identity}_glass kind=sign min_x=-458 max_x=-458 min_z=-170 max_z=110 height=620 color=87BFC8 attr.height2=976 attr.style=6 attr.assembly={assembly}')
    return '\n'.join(lines)+'\n'

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--build-assets',action='store_true');p.add_argument('--blender',default='blender')
    p.add_argument('--id',default='lab_computer');p.add_argument('--x',type=int,default=0);p.add_argument('--z',type=int,default=0)
    p.add_argument('--channel',default='character_lab');p.add_argument('--color',default='79E8C5');p.add_argument('--lab');p.add_argument('--output',type=Path)
    a=p.parse_args()
    if not a.build_assets:
        content=generate(a.id,a.x,a.z,a.channel,a.color,a.lab)
        if a.output:
            with a.output.open('x',encoding='utf-8',newline='\n') as f:f.write(content)
        else: print(content,end='')
        return
    source=ROOT/'rasterfall/private-assets/source/props/lab'
    subprocess.run([a.blender,'-b','--python-exit-code','1','--python',str(ROOT/'tools/blender/generate_lab_computer.py'),'--','--output',str(source)],check=True)
    dest=ROOT/'rasterfall/assets/models/props/lab';dest.mkdir(parents=True,exist_ok=True)
    for part in dict.fromkeys(row[1] for row in PARTS):
        asset='rf_lab_computer_'+part
        manifest=ROOT/'tools/assets/manifests/props/lab'/(asset+'.asset.json')
        subprocess.run([sys.executable,str(ROOT/'tools/assets/import_asset.py'),'--no-build','--tool-dir',str(ROOT/'build-windows'),'--force','--output-root',str(dest),str(manifest)],cwd=ROOT,check=True)
        if struct.unpack_from('<I',(dest/(asset+'.rmesh')).read_bytes(),16)[0]!=232:
            raise ValueError(asset+': prop profile requires 232 RMESH units/metre')

if __name__=='__main__':main()
