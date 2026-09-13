#!/usr/bin/env python3
"""Temporary metric campus parts. Reuses the industrial Builder/export checks."""
import argparse
from pathlib import Path
import sys
import bpy
import bmesh
sys.path.insert(0, str(Path(__file__).resolve().parent))
from generate_rasterfall_props import Builder, material, export, select_only

SPECS = [["rf_campus_wall_plain",[4,0.24,3.2]],["rf_campus_wall_window",[4,0.24,3.2]],["rf_campus_window_strip",[4,0.24,1.4]],["rf_campus_entrance",[4,1.2,3.2]],["rf_campus_roof_edge",[4,0.6,0.3]],["rf_campus_column",[0.4,0.4,3.2]],["rf_campus_stair_short",[4,1.2,0.6]],["rf_campus_stair_long",[4,2.4,1.2]],["rf_campus_retaining_wall",[4,0.4,1.2]],["rf_campus_curb",[4,0.2,0.15]],["rf_campus_sidewalk",[4,2,0.15]],["rf_campus_tree_proxy",[3,3,5]]]

def build(name, b):
    def box(c, s, role=0):
        obj=b.box(c, s, role, bevel=0)
        # Temporary double-sided visual shell: omit rear and underside planes.
        # Legacy static RMESH draws both sides; close thin opposing planes
        # compete at distance. Author the shell rather than changing renderer.
        mesh=bmesh.new(); mesh.from_mesh(obj.data)
        mesh.normal_update()
        faces=[f for f in mesh.faces if f.normal.y>.9 or f.normal.z<-.9 or
               (name.endswith(('wall_plain','wall_window','window_strip')) and f.normal.z>.9) or
               (name.endswith(('wall_window','window_strip')) and abs(f.normal.x)>.9
                and abs(f.calc_center_median().x+obj.location.x)<1.999)]
        bmesh.ops.delete(mesh,geom=faces,context='FACES')
        mesh.to_mesh(obj.data); mesh.free()
        return obj
    n = name.removeprefix('rf_campus_')
    if n == 'wall_plain':
        box((0,0,1.6),(4,.24,3.2))
    elif n in ('wall_window','window_strip'):
        base = 1 if n == 'wall_window' else 0
        if base:
            box((0,0,.5),(4,.24,1))
            box((0,0,2.8),(4,.24,.8))
        # Adjacent opaque panes and mullions, no overlay / coplanar faces.
        for i in range(4):
            x = -2+i
            box((x+.06,0,base+.7),(.12,.24,1.4),1)
            box((x+.56,0,base+.7),(.88,.24,1.4),2)
    elif n == 'entrance':
        for x in (-1.5,1.5):
            box((x,0,1.6),(1,.24,3.2))
        box((0,0,2.85),(2,.24,.7))
        box((0,0,3.05),(4,1.2,.3),1)
    elif n == 'roof_edge':
        box((0,0,.15),(4,.6,.3),1)
    elif n == 'column':
        box((0,0,1.6),(.4,.4,3.2))
    elif n.startswith('stair_'):
        count = 4 if n == 'stair_short' else 8
        verts=[]; faces=[]
        def face(points):
            start=len(verts); verts.extend(points)
            faces.append(tuple(range(start,start+len(points))))
        depth=count*.3
        for i in range(count):
            a=-depth/2+i*.3; c=a+.3; lo=i*.15; hi=lo+.15
            # Only exposed riser and tread, no buried full-height box fronts.
            face([(-2,a,lo),(2,a,lo),(2,a,hi),(-2,a,hi)])
            face([(-2,a,hi),(2,a,hi),(2,c,hi),(-2,c,hi)])
        profile=[(-depth/2,0)]
        for i in range(count):
            profile.extend([(-depth/2+i*.3,(i+1)*.15),(-depth/2+(i+1)*.3,(i+1)*.15)])
        profile.append((depth/2,0))
        for x in (-2,2):
            points=[(x,y,z) for y,z in profile]
            # Side surface partitioned as a fixed fan, avoiding overlapping
            # boxes and retaining the complete step silhouette.
            for i in range(1,len(points)-1):
                tri=[points[0],points[i],points[i+1]]
                if x>0: tri.reverse()
                face(tri)
        mesh=bpy.data.meshes.new(name)
        mesh.from_pydata(verts,[],faces)
        obj=bpy.data.objects.new(name,mesh)
        bpy.context.collection.objects.link(obj)
        mesh.materials.append(b.materials[0]); b.parts.append(obj)
    elif n == 'retaining_wall':
        box((0,0,.55),(4,.4,1.1))
        box((0,0,1.15),(4,.4,.1),1)
    elif n in ('curb','sidewalk'):
        box((0,0,.075),(4,.2 if n=='curb' else 2,.15),1)
    elif n == 'tree_proxy':
        b.cylinder((0,0,1.5),.16,3,1)
        # Deliberate faceted proxy; no foliage textures or vegetation runtime.
        bpy.ops.mesh.primitive_uv_sphere_add(segments=8, ring_count=4, radius=1,
                                           location=(0,0,3.4))
        obj=bpy.context.object
        obj.scale=(1.5,1.5,1.6)
        bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
        obj.data.materials.append(b.materials[2])
        # Symmetric crown quads otherwise leave Blender's beauty diagonal
        # tie-breaking dependent on allocation order between processes.
        mod=obj.modifiers.new('fixed_proxy_triangles','TRIANGULATE')
        mod.quad_method='FIXED'
        mod.ngon_method='CLIP'
        bpy.ops.object.modifier_apply(modifier=mod.name)
        b.parts.append(obj)

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--output',type=Path,required=True)
    args=p.parse_args(sys.argv[sys.argv.index('--')+1:])
    args.output.mkdir(parents=True,exist_ok=True)
    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.context.scene.unit_settings.system='METRIC'
    for name,dims in SPECS:
        colors=((.72,.73,.70),(.51,.54,.53),(.19,.30,.35))
        if name.endswith('tree_proxy'):
            colors=((.72,.73,.70),(.32,.27,.21),(.28,.40,.27))
        mats=[material(name+str(i),c) for i,c in enumerate(colors)]
        b=Builder(mats)
        build(name,b)
        # Remove unused material slots so exporter checks count actual primitives.
        used={m for o in b.parts for m in o.data.materials}
        b.materials=[m for m in mats if m in used]
        obj=b.finish(name,dims,400)
        # Canonical triangle order removes Blender join allocation ordering.
        triangles=[]
        for poly in obj.data.polygons:
            points=[tuple(round(v,6) for v in obj.data.vertices[i].co) for i in poly.vertices]
            rotations=[points[i:]+points[:i] for i in range(3)]
            triangles.append((poly.material_index,min(rotations)))
        triangles.sort()
        stable=bpy.data.meshes.new(name+'_stable')
        stable.from_pydata([v for _,pts in triangles for v in pts],[],
                          [(i*3,i*3+1,i*3+2) for i in range(len(triangles))])
        for mat in obj.data.materials:
            stable.materials.append(mat)
        for poly,(index,_) in zip(stable.polygons,triangles):
            poly.material_index=index
        obj.data=stable
        obj.data.name=name
        export(args.output/(name+'.glb'),[obj])
    bpy.context.preferences.filepaths.save_version=0
    bpy.ops.wm.save_as_mainfile(filepath=str(args.output/'temporary_campus_v0.blend'))
if __name__=='__main__':
    main()
