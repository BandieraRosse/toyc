"""V26d removes almost-coplanar posterior cap / lock intersections.

Retain V26c roots, material/UVs and all mesh topology. The cap stays outside
the locked head surface and below the visible posterior locks.
"""
import bpy,sys,json,struct,shutil
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION='v026d';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-hair_back-v026c.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()};protected[str(BASE)]=sha(BASE)
def smooth(t):
 t=max(0,min(1,t));return t*t*(3-2*t)
C=Vector((0,.015,1.541));bpy.ops.wm.read_factory_settings(use_empty=True)
e=base['parts']['hair_base'];objects=load_objects(ROOT/e['file'],e['objects']);cap=objects[0]
e=base['parts']['head'];heads=load_objects(ROOT/e['file'],e['objects'])
trees=[BVHTree.FromPolygons([v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons]) for o in heads]
for v in cap.data.vertices:
 p=v.co.copy();d=(p-C).normalized();r=(p-C).length
 w=smooth((1.653-p.z)/.049)*smooth((d.y+.42)/.62)
 target=r-.0044*w
 for tree in trees:
  hit,n,face,dist=tree.ray_cast(C+d*.4,-d,.4)
  if hit is not None:target=max(target,(hit-C).length+.0011)
 v.co=C+d*target
cap.data.update()
for o in heads:bpy.data.objects.remove(o,do_unlink=True)
interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['hair_base'],'reason':'Inset the lower posterior support cap under the visible locks, clamped outside the unchanged head. Topology, UV, weights and other sources retained.'}
interface['cap_inset_v026d']={'script':'scripts/finalize_v026d.py','sha256':sha(Path(__file__))}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface);base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
seed=ROOT/('assembly-'+VERSION+'-seed.json');write(seed,base)
work=OUT/'hair_base-work.blend';save_part(work,objects)
candidate=Path(revise(seed,'hair_base',VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,('hair_base',));assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),body_positions_weights_unchanged=True,texture_contract='Inherited V26b five-image base-color contract, V26c hair roughness.')
write(OUT/'validation.json',report);write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={k.split('.')[0]:v for k,v in read(ROOT/'build/v026c/visual-roles.json').items()}
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
shutil.copytree(ROOT/'build/v026c/textures',OUT/'textures')
manifest=read(ROOT/'build/v026c/texture-manifest.json');manifest['glb_materials']=doc['materials'];manifest['material_roles']=read(OUT/'visual-roles.json');write(OUT/'texture-manifest.json',manifest)
print('V26d CAP PASS',len(doc['materials']),len(doc['images']),flush=True)
