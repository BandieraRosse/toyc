"""Maintain the outpost's one-metre gameplay grid, west of the animation lab."""
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
BEGIN = '# BEGIN GRID GAMEPLAY LAB'
END = '# END GRID GAMEPLAY LAB'
CELL = 512


def update(path=ROOT / 'rasterfall/assets/maps/outpost.map'):
    raw = path.read_bytes()
    bom = raw.startswith(b'\xef\xbb\xbf')
    nl = '\r\n' if b'\r\n' in raw else '\n'
    lines = raw.decode('utf-8-sig' if bom else 'utf-8').splitlines()
    if BEGIN in lines:
        a, b = lines.index(BEGIN), lines.index(END)
        lines[a:b+1] = []
    # The northeast corner touches the animation campus's western road.
    east, north = -20992, -14848
    west, south = east-64*CELL, north-64*CELL
    bounds = f'min_x={west} max_x={east} min_z={south} max_z={north}'
    x, z = west+32*CELL+CELL//2, south+32*CELL+CELL//2
    records = [BEGIN,
        f'region id=grid_lab_area kind=experiment {bounds} attr.cell_size={CELL}',
        f'collision id=grid_lab_col shape=flat {bounds} height=0 collision=false visible=false walkable=true',
        f'surface id=grid_lab kind=ground {bounds} height=0 attr.collision_id=grid_lab_col',
        f'render id=grid_lab_floor kind=floor {bounds} height=0 color=536570 attr.style=13',
        # Overlap support at the seam; visible floor paint never overlaps the road.
        f'collision id=grid_lab_join_col shape=flat min_x={east-1024} max_x={east+1024} min_z=-24064 max_z=-15872 height=0 collision=false visible=false walkable=true',
        'surface id=grid_lab_join kind=ground min_x=-22016 max_x=-19968 min_z=-24064 max_z=-15872 height=0 attr.collision_id=grid_lab_join_col',
        f'object id=grid_lab_terminal kind=facility_terminal x={x} y=0 z={z} yaw=0 scale=1000 attr.collision=component',
        f'render id=grid_lab_title kind=sign min_x={x-1700} max_x={x+1700} min_z={z-650} max_z={z-630} height=700 attr.height2=1100 color=79E8C5 attr.style=2 attr.text=GRID_LAB_/_1m attr.projection_beams=0',
        END]
    for i, line in enumerate(lines):
        if line.startswith('world '):
            words = line.split()
            words = [f'min_x={min(int(w[6:]),west-512)}' if w.startswith('min_x=') else w for w in words]
            lines[i] = ' '.join(words)
    at = lines.index('# END DYNAMIC EXPERIMENT CAMPUS')+1
    lines[at:at] = records
    path.write_bytes((b'\xef\xbb\xbf' if bom else b'')+(nl.join(lines)+nl).encode('utf-8'))


if __name__ == '__main__':
    update()
