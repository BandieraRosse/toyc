"""Repair inherited near-collinear ngon tessellation without altering interfaces."""
import bpy,sys
from pathlib import Path
from mathutils import Vector
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION='v025c';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=True)
assert not (OUT/('rf_c01-'+VERSION+'.blend')).exists()
BASE=ROOT/'assembly-hair_base-v025b.json';base=read(BASE)
bpy.ops.wm.read_factory_settings(use_empty=True)
entry=base['parts']['head'];objects=load_objects(ROOT/entry['file'],entry['objects'])
obj=next(o for o in objects if o.get('expression_role')=='head');m=obj.data
adjusted={}
for iteration in range(6):
 m.calc_loop_triangles();bad=[t for t in m.loop_triangles if t.area<1e-11]
 if not bad:break
 for tri in bad:
  ids=list(tri.vertices);points=[m.vertices[i].co.copy() for i in ids]
  polygon=m.polygons[tri.polygon_index]
  # Move the triangle's middle point very slightly toward the parent polygon
  # interior. This is a tessellation repair, not a new facial feature.
  longest=max(((points[b]-points[a]).length,a,b) for a,b in ((0,1),(0,2),(1,2)))
  middle=next(i for i in range(3) if i not in longest[1:]);index=ids[middle]
  d=polygon.center-m.vertices[index].co
  if d.length<1e-8:d=polygon.normal.copy()
  delta=d.normalized()*.00004
  before=m.vertices[index].co.copy()
  if m.shape_keys:
   for key in m.shape_keys.key_blocks:key.data[index].co+=delta
  m.vertices[index].co=before+delta
  adjusted[index]=adjusted.get(index,0)+delta.length
 m.update()
assert not bad,'Tessellation did not stabilize'
work=OUT/'head-work.blend';save_part(work,objects)
candidate=Path(revise(BASE,'head',VERSION,'publish',source=work))
for part in ('hair_front','hair_back','hair_base'):
 bpy.ops.wm.read_factory_settings(use_empty=True)
 entry=read(candidate)['parts'][part];hair=load_objects(ROOT/entry['file'],entry['objects'])
 for obj in hair:
  for mat in obj.data.materials:
   if 'Hair' not in mat.name:continue
   rgb=(.036,.078,.096)
   mat.diffuse_color=(*rgb,1)
   for node in mat.node_tree.nodes:
    if node.type=='BSDF_PRINCIPLED':node.inputs['Base Color'].default_value=(*rgb,1)
 work=OUT/(part+'-work.blend');save_part(work,hair)
 candidate=Path(revise(candidate,part,VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,BASE,('head','hair_front','hair_back','hair_base'))
report.update(candidate=str(candidate),protected_sources={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()},inherited_collinear_vertices_repaired=adjusted,baseline_v023a_zero_triangles=7,previous_v025b_zero_triangles=1,hair_midtones_linear_rgb=[.036,.078,.096])
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
print('V25c TESSELLATION REPAIR PASS',adjusted,flush=True)
