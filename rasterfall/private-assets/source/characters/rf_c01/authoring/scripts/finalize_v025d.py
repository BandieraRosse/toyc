"""Freeze head triangulation to avoid inherited ngon tessellation changing in pose."""
import bpy,bmesh,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION='v025d';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-hair_base-v025c.json';base=read(BASE)
bpy.ops.wm.read_factory_settings(use_empty=True)
entry=base['parts']['head'];objects=load_objects(ROOT/entry['file'],entry['objects'])
obj=next(o for o in objects if o.get('expression_role')=='head');m=obj.data
bm=bmesh.new();bm.from_mesh(m)
if m.shape_keys:
 for key in m.shape_keys.key_blocks:
  layer=bm.verts.layers.shape.get(key.name) or bm.verts.layers.shape.new(key.name)
  for v in bm.verts:v[layer]=key.data[v.index].co
bmesh.ops.triangulate(bm,faces=list(bm.faces),quad_method='BEAUTY',ngon_method='BEAUTY')
bm.normal_update();adjusted=0
for _ in range(6):
 bad=[f for f in bm.faces if f.calc_area()<1e-11]
 if not bad:break
 for f in bad:
  v=max(f.verts,key=lambda v:len(v.link_faces));delta=v.normal.normalized()*.00004
  v.co+=delta
  for layer in bm.verts.layers.shape.values():v[layer]+=delta
  adjusted+=1
 bm.normal_update()
assert not bad
bm.to_mesh(m);bm.free();m.update()
work=OUT/'head-work.blend';save_part(work,objects)
candidate=Path(revise(BASE,'head',VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,BASE,('head',))
report.update(candidate=str(candidate),protected_sources={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()},explicit_head_triangulation=True,subvisual_collinear_vertex_adjustments=adjusted)
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
print('V25d HEAD TRIANGULATION PASS',adjusted,flush=True)
