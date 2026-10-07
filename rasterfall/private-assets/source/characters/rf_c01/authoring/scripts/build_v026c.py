"""Continue locked V26b: soften the common crown and bury closed clump roots.

No topology, UV, weights, rig, action, face or body modifications. Preserve the
5-image texture contract while reducing polished broad hair highlights.
"""
import bpy,math,sys,json,struct,shutil
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION='v026c';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-body-v026b.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()}
protected[str(BASE)]=sha(BASE)
def smooth(t):
 t=max(0,min(1,t));return t*t*(3-2*t)
C=Vector((0,.015,1.541))
bpy.ops.wm.read_factory_settings(use_empty=True)
objects={p:load_objects(ROOT/base['parts'][p]['file'],base['parts'][p]['objects']) for p in ('hair_base','hair_front','hair_back')}
cap=objects['hair_base'][0];mesh=cap.data;pts=[v.co.copy() for v in mesh.vertices]
adj=[set() for p in pts]
for e in mesh.edges:
 a,b=e.vertices;adj[a].add(b);adj[b].add(a)
radii=[(p-C).length for p in pts]
for iteration in range(18):
 radii=[r+(sum(radii[j] for j in adj[i])/len(adj[i])-r)*.64*smooth((pts[i].z-1.55)/.065) for i,r in enumerate(radii)]
for v,p,r in zip(mesh.vertices,pts,radii):v.co=C+(p-C).normalized()*r
mesh.update()
tree=BVHTree.FromPolygons([v.co for v in mesh.vertices],[list(p.vertices) for p in mesh.polygons])
for part in ('hair_front','hair_back'):
 for obj in objects[part]:
  old=[v.co.copy() for v in obj.data.vertices]
  sections=[(0,31)] if part=='hair_back' else [(0,33)]+([(594,21)] if len(old)==972 else [])
  for start,rows in sections:
   layer=rows*9
   for j in range(rows):
    t=j/(rows-1);w=1-smooth(t/(.42 if part=='hair_back' else .22))
    if w==0:continue
    for side in range(2):
     for k in range(9):
      n=start+side*layer+j*9+k;p=old[n];d=(p-C).normalized();r=(p-C).length
      hit,normal,face,dist=tree.ray_cast(C+d*.4,-d,.4)
      if hit is None:continue
      # Both surface layers enter the common cap; preserve small thickness
      # so there are no new degenerate triangles in the buried root.
      cap_r=(hit-C).length-.0017-(.0003 if side else 0)
      obj.data.vertices[n].co=C+d*(r*(1-w)+cap_r*w)
  obj.data.update()
for items in objects.values():
 for obj in items:
  for mat in obj.data.materials:
   for node in mat.node_tree.nodes:
    if node.type=='BSDF_PRINCIPLED':node.inputs['Roughness'].default_value=.58
interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['hair_base','hair_front','hair_back'],'reason':'Smooth the common crown radial envelope and bury the closed clump roots. Existing topology, UV0, weights and all non-hair sources are retained.'}
interface['hair_root_v026c']={'script':'scripts/build_v026c.py','sha256':sha(Path(__file__))}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface)
base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
candidate=ROOT/('assembly-'+VERSION+'-seed.json');write(candidate,base)
# Save all source-only candidates before revise opens their individual files.
for part,items in objects.items():save_part(OUT/(part+'-work.blend'),items)
for part in objects:candidate=Path(revise(candidate,part,VERSION,'publish',source=OUT/(part+'-work.blend')))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,tuple(objects))
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),body_positions_weights_unchanged=True,texture_contract='Inherited V26b embedded sRGB PNG; face/body separate; unchanged UV0 and tint factors.')
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
oldroles=read(ROOT/'build/v026b/visual-roles.json');roles={k.split('.')[0]:v for k,v in oldroles.items()}
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
shutil.copytree(ROOT/'build/v026b/textures',OUT/'textures')
manifest=read(ROOT/'build/v026b/texture-manifest.json');manifest['glb_materials']=doc['materials'];manifest['glb_samplers']=doc.get('samplers',[])
manifest['material_roles']=read(OUT/'visual-roles.json');write(OUT/'texture-manifest.json',manifest)
print('V26c ROOT PASS',len(doc['materials']),len(doc['images']),flush=True)
