"""Repair pre-existing coincident glove-panel bevel rings in the body candidate."""
import bpy,bmesh,sys
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts import pipeline
from rf_parts.storage import read,write,sha
from rf_parts.revisions import revise
OUT=ROOT/'build/v023a';OUT.mkdir(exist_ok=False)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'build/v023/rf_c01-v023.blend'))
body=next(o for o in bpy.context.scene.objects if o.get('part_id')=='body')
bm=bmesh.new();bm.from_mesh(body.data)
bad=[f for f in bm.faces if f.calc_area()<1e-14]
assert len(bad)==24,'Only the inherited left/right glove bevels are expected'
vertices=list({v for f in bad for v in f.verts})
assert all(.59<abs(v.co.x)<.65 and 1.30<v.co.z<1.32 for v in vertices)
bmesh.ops.remove_doubles(bm,verts=vertices,dist=1e-6)
assert all(f.calc_area()>1e-14 for f in bm.faces)
bmesh.ops.recalc_face_normals(bm,faces=bm.faces)
bm.to_mesh(body.data);bm.free();body.data.update()
work=OUT/'body-work.blend';bpy.ops.wm.save_as_mainfile(filepath=str(work))
candidate=Path(revise(ROOT/'assembly-body-v023.json','body','v023a','publish',source=work))
blend=OUT/'rf_c01-v023a.blend';pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,ROOT/'assembly-hair_base-v022h.json',('body',))
body=next(o for o in bpy.context.scene.objects if o.get('part_id')=='body')
build=read(ROOT/'build/v023/validation.json')
report.update({k:v for k,v in build.items() if k not in report})
report.update(after_body_vertices=len(body.data.vertices),after_body_triangles=sum(len(p.vertices)-2 for p in body.data.polygons),
    candidate=str(candidate),inherited_degenerate_faces_removed=len(bad))
assert all(sha(Path(p))==h for p,h in report['protected_sources'].items())
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/'rf_c01-v023a.glb'))
print('V23a BODY FINALIZE PASS',report['after_body_vertices'],report['after_body_triangles'],flush=True)
