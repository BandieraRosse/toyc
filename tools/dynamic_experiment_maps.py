"""Author the consolidated empty campus and ordinary Runtime Map group templates."""
from pathlib import Path
from experiment_lab import generate as lab
from building_kit import BuildingKit
from rf_electronics_lab import generate as electronics
from mesh_weaver_lab import generate as weaver

ROOT=Path(__file__).resolve().parents[1]
MAPS=ROOT/'rasterfall/assets/maps'
BEGIN='# BEGIN DYNAMIC EXPERIMENT CAMPUS'
END='# END DYNAMIC EXPERIMENT CAMPUS'

def fields(line):
    words=line.split()
    return (words[0] if words else '',dict(w.split('=',1) for w in words[1:] if '=' in w))

def write(path,lines):
    old=path.read_bytes() if path.exists() else b''
    nl=b'\r\n' if b'\r\n' in old else b'\n'
    path.write_bytes(nl.join(s.encode('utf-8') for s in lines)+nl)

def fragment(name,records,area,x,z):
    result=['# Dynamic experiment group; ordinary components in world coordinates.',
        'map version=1 units=rfu',
        'world min_x=-41472 max_x=80384 min_z=-55808 max_z=19968 room_limit=80640']
    for line in records:
        words=line.split()
        if f'attr.lab={area}' in words:
            lowered=[]
            for word in words:
                if word==f'attr.lab={area}':continue
                key,sep,value=word.partition('=')
                if key in ('x','min_x','max_x'):value=str(int(value)+x)
                elif key in ('z','min_z','max_z'):value=str(int(value)+z)
                lowered.append(key+sep+value)
            line=' '.join(lowered)
        result.append(line)
    write(MAPS/(name+'.map'),result)

def main():
    path=MAPS/'outpost.map'
    lines=path.read_text(encoding='utf-8').splitlines()
    if BEGIN in lines:
        base=lines[:lines.index(BEGIN)]
        tail=lines[lines.index(END)+1:]
    else:
        start=next(i for i,s in enumerate(lines) if s.startswith('# Standard open experiment plot:'))
        end=lines.index('# BEGIN OUTPOST STOREYS')
        base=lines[:start];tail=lines[end:]
        material=[];atmosphere=[]
        for line in lines[start:end]:
            kind,f=fields(line);name=f.get('id','')
            if f.get('attr.lab')=='rf_light_lab_area' and (name.startswith(('rf_light_base_','rf_light_label_','rf_light_occluder_'))):
                material.append(line)
                if kind=='render' and f.get('kind')=='box':
                    bounds=' '.join(f'{k}={f[k]}' for k in ('min_x','max_x','min_z','max_z'))
                    material.extend([f'collision id={name}_col shape=box {bounds} height={f["height"]} collision=true visible=false walkable=true attr.lab=rf_light_lab_area',
                        f'surface id={name}_surface kind=platform {bounds} height={f["height"]} attr.collision_id={name}_col attr.lab=rf_light_lab_area'])
            if f.get('attr.lab')=='atmosphere_lab_area':atmosphere.append(line)
        # Solid sample envelopes share their plinths' positions and the visible top.
        for i in range(12):
            x=-2198+i%4*2300;z=1326-i//4*2300
            bounds=f'min_x={x-650} max_x={x+650} min_z={z-650} max_z={z+650}'
            material.extend([f'collision id=material_sample_{i}_col shape=box {bounds} height=1550 attr.base_y=250 collision=true visible=false walkable=true attr.lab=rf_light_lab_area',
                f'surface id=material_sample_{i}_top kind=platform {bounds} height=1550 attr.collision_id=material_sample_{i}_col attr.lab=rf_light_lab_area'])
        fragment('experiment_material',material,'rf_light_lab_area',29184,-19968)
        # Existing atmosphere geometry remains an authored experiment, no longer a permanent second site.
        fragment('experiment_atmosphere',atmosphere,'atmosphere_lab_area',29184,-19968)
    records=electronics().splitlines()
    wanted=[]
    for line in records:
        kind,f=fields(line);name=f.get('id','')
        if name.startswith(('rf_product_','rf_electronics_demo')):
            if kind=='collision':line=line.replace('visible=false','visible=false walkable=true')
            wanted.append(line)
    fragment('experiment_electronics',wanted,'rf_electronics_lab_area',9728,-19968)
    wanted=[]
    for line in weaver().splitlines():
        kind,f=fields(line);name=f.get('id','')
        if name in ('mesh_weaver','mesh_weaver_service_links') or name.startswith(('mesh_weaver_base_','mesh_weaver_post_','mesh_weaver_power','mesh_weaver_screen','mesh_weaver_rf1')):
            if kind=='collision':line=line.replace('walkable=false','walkable=true')
            wanted.append(line)
    fragment('experiment_weaver',wanted,'mesh_weaver_lab_area',9728,-19968)
    crates=[f'object id=experiment_load_{i} kind=crate x={-5250+i%6*2000} y=0 z={-3100+i//6*2000} yaw=0 scale=1000 attr.collision=component attr.lab=perf_low_area' for i in range(24)]
    fragment('experiment_components',crates,'perf_low_area',0,-35000)
    campus=[BEGIN]
    for name,x,z,category in [('actor_actions_lab',-9728,-19968,'animation'),
        ('rf_electronics_lab',9728,-19968,'model'),('rf_light_lab',29184,-19968,'lighting')]:
        width=12288 if name=='rf_light_lab' else 10240
        campus.extend(line for line in lab(name,x,z,category,'open',width=width,depth=9216,
                          projection_beams=False).splitlines() if '_sample_info' not in line)
    # A single performance footprint. Workloads own everything inside it.
    campus.extend(line for line in lab('perf_low',0,-35000,'performance','walled',width=13500,depth=9000,
                      plot_width=16384,plot_depth=10240,projection_beams=False,entry_gap=0).splitlines()
                      if 'perf_low_button' not in line and '_sample_info' not in line)
    campus += ['object id=perf_control_terminal kind=facility_terminal x=7800 y=0 z=-31000 yaw=0 scale=1000 attr.collision=component',
        'object id=perf_result_terminal kind=facility_terminal x=9200 y=0 z=-31000 yaw=0 scale=1000 attr.collision=component',
        'object id=combat_control_terminal kind=facility_terminal x=-7800 y=0 z=-31000 yaw=0 scale=1000 attr.collision=component',
        'object id=combat_result_terminal kind=facility_terminal x=-9200 y=0 z=-31000 yaw=0 scale=1000 attr.collision=component']
    # Tile the roads against plot edges without overlapping visible floor paint.
    roads = [(-20992,40448,-14848,-11776),
             (-20992,40448,-28160,-25088),
             (-20992,-17920,-25088,-14848),
             (-1536,1536,-25088,-14848),
             (17920,20992,-25088,-14848),
             (37376,40448,-25088,-14848),
             (-11264,11264,-29880,-28160),
             (-11264,-8192,-40120,-29880),
             (8192,11264,-40120,-29880),
             (-11264,11264,-43192,-40120)]
    for i,(x0,x1,z0,z1) in enumerate(roads):
        b=f'min_x={x0} max_x={x1} min_z={z0} max_z={z1}'
        campus += [f'collision id=experiment_road_{i}_col shape=flat {b} height=0 collision=false visible=true walkable=true',
            f'surface id=experiment_road_{i} kind=ground {b} height=0 attr.collision_id=experiment_road_{i}_col',
            f'render id=experiment_road_{i}_paint kind=floor {b} height=0 color=485860 attr.style=11']
    # Continuous invisible support carries actor footprints across road/plot seams.
    # Overlap only inside already paved ground, including the existing hall yard.
    for name,(x0,x1,z0,z1) in [('main',(-20992,40448,-28160,-11776)),
                              ('performance',(-11264,11264,-43192,-25088)),
                              ('entry',(-5120,5120,-14848,-5632))]:
        b=f'min_x={x0} max_x={x1} min_z={z0} max_z={z1}'
        campus += [f'collision id=experiment_support_{name}_col shape=flat {b} height=0 collision=false visible=false walkable=true',
            f'surface id=experiment_support_{name} kind=ground {b} height=0 attr.collision_id=experiment_support_{name}_col']
    campus.append(END)
    from grid_lab import update as update_grid
    write(path,base+campus+tail)
    update_grid(path)

if __name__=='__main__':main()
