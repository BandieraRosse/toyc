"""Author the single-layer lab maps from a native tactical log's map facts."""
import argparse
import json
from pathlib import Path
from building_kit import BuildingKit

def build(source):
    data=json.loads(Path(source).read_text(encoding="utf-8").splitlines()[0])
    width,height=data['width_m'],data['height_m']
    half_x,half_z=round(width*256),round(height*256)
    arena_bounds=(-half_x-1024,half_x+1024,-half_z-4096,half_z+1024)
    def shell(identity,x0,x1,z0,z1):
        return ["map version=1 units=rfu",
            f"world min_x={x0} max_x={x1} min_z={z0} max_z={z1} room_limit=65000 attr.identity={identity}",
            f"surface id=ground kind=ground min_x={x0} max_x={x1} min_z={z0} max_z={z1} height=0 material=344650 attr.collision_id=ground_col",
            f"collision id=ground_col shape=flat min_x={x0} max_x={x1} min_z={z0} max_z={z1} height=0 collision=false visible=true walkable=true color=344650",
            f"render id=ground_paint kind=floor min_x={x0} max_x={x1} min_z={z0} max_z={z1} height=0 color=344650",
            f"region id=player_start kind=start min_x=0 max_x=0 min_z={z0+1300} max_z={z0+1300} attr.sy=0 attr.cy=1024",
            f"object id=tactical_terminal kind=facility_terminal x=0 y=0 z={z0+2100} yaw=0 scale=1000 attr.collision=component",
            f"render id=control_line kind=floor min_x={x0} max_x={x1} min_z={z0+3100} max_z={z0+3164} height=0 color=4AC8CC"]
    arena=shell('tactical_arena',*arena_bounds)
    arena.append(f'region id=frontier_station_bounds kind=mission_area min_x={-half_x} max_x={half_x} min_z={-half_z} max_z={half_z}')
    for i,c in enumerate(data['covers']):
        x0,z0,x1,z1,cover_height=c
        x0,x1=round((x0-width/2)*512),round((x1-width/2)*512)
        z0,z1=round((z0-height/2)*512),round((z1-height/2)*512)
        h=round((1.10 if cover_height==1 else 2.20)*512)
        arena.append(f'collision id=cover_{i} shape=box min_x={x0} max_x={x1} min_z={z0} max_z={z1} height={h} collision=true visible=true walkable=true color=647881')
        arena.append(f'surface id=cover_{i}_surface kind=platform min_x={x0} max_x={x1} min_z={z0} max_z={z1} height={h} material=8A9A9F attr.collision_id=cover_{i}')
        arena.append(f'render id=cover_draw_{i} kind=box min_x={x0} max_x={x1} min_z={z0} max_z={z1} height={h} attr.base_y=0 color=8A9A9F')
    # Replace the wall volume at each window; glass never overlays a solid wall.
    kit=BuildingKit(arena,250)
    windows=[(round(a*width/64),round(b*width/64)) for a,b in [(-14500,-8500),(-6000,0),(2500,8500),(11000,16000)]]
    observer_z=-half_z-187
    kit.solid('observer_sill',arena_bounds[0],arena_bounds[1],observer_z-125,observer_z+125,0,512,'536B78')
    kit.wall('observer_wall','x',observer_z,arena_bounds[0],arena_bounds[1],512,2304,'536B78',
             openings=[(a,b,1280) for a,b in windows])
    for i,(a,b) in enumerate(windows):
        kit.window(f'observer_glass_{i}', 'x', observer_z, a, b, 512, 1792, depth=50)
    ranges=shell('tactical_range',-8192,8192,-4096,53248)
    for i,d in enumerate([5,10,15,20,30,40,60,80,100]):
        x=(i-4)*1536;z=d*512
        ranges += [f'render id=distance_{i} kind=sign min_x={x-500} max_x={x+500} min_z={z+64} max_z={z+80} height=1100 attr.height2=1450 color=8DE2CF attr.facing=-z attr.text={d}m',
                   f'render id=lane_{i} kind=floor min_x={x-10} max_x={x+10} min_z=0 max_z={z} height=0 color=607B83']
    ranges.append('collision id=backstop shape=box min_x=-8192 max_x=8192 min_z=52224 max_z=52480 height=2500 collision=true visible=true walkable=false color=263640')
    ranges.append('render id=backstop_draw kind=box min_x=-8192 max_x=8192 min_z=52224 max_z=52480 height=2500 attr.base_y=0 color=536B78')
    for lines,x0,x1,z0,z1 in [(arena,*arena_bounds),(ranges,-8192,8192,-4096,53248)]:
        for name,a,b,c,d in [('west',x0,x0+64,z0,z1),('east',x1-64,x1,z0,z1),
                              ('south',x0,x1,z0,z0+64),('north',x0,x1,z1-64,z1)]:
            # The glass and its collision share the same vaultable height.
            axis,at,start,end = ('z',(a+b)//2,c,d) if name in ('west','east') else ('x',(c+d)//2,a,b)
            at += 32 if name in ('west','south') else -32
            # Butt the north/south rails against the side frames at corners.
            if axis == 'x': start,end = start+64,end-64
            BuildingKit(lines).window(f'perimeter_{name}_glass', axis, at,
                                      start, end, 0, 512, depth=128, walk=True)
    root=Path('rasterfall/assets/maps')
    for name,lines in [('tactical_arena',arena),('tactical_range',ranges)]:
        (root/(name+'.map')).write_text('\n'.join(lines)+'\n',encoding='utf-8',newline='\n')

if __name__=='__main__':
    parser=argparse.ArgumentParser();parser.add_argument('log');args=parser.parse_args();build(args.log)
