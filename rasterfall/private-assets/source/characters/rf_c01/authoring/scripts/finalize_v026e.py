"""V26e establishes clear central-lock overlap rather than grazing surfaces."""
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
VERSION='v026e';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-hair_base-v026d.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()};protected[str(BASE)]=sha(BASE)
def smooth(t):
 t=max(0,min(1,t));return t*t*(3-2*t)
C=Vector((0,.015,1.541));bpy.ops.wm.read_factory_settings(use_empty=True)
e=base['parts']['hair_back'];objects=load_objects(ROOT/e['file'],e['objects'])
for obj in objects:
 index=int(obj['object_id'].rsplit('_',1)[1])
 if index!=5:continue
 for v in obj.data.vertices:
  p=v.co.copy();d=(p-C).normalized();row=(v.index%279)//9;t=row/30
  w=smooth(t/.26)*smooth((1-t)/.16)
  v.co=p+d*(.0026*w)
 obj.data.update()
interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['hair_back'],'reason':'Lift central back lock 2.6mm through its middle to establish an intentional front/back overlap. Roots and tips taper to the locked previous positions. Topology, UV, weights and other sources retained.'}
interface['lock_overlap_v026e']={'script':'scripts/finalize_v026e.py','sha256':sha(Path(__file__))}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface);base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
seed=ROOT/('assembly-'+VERSION+'-seed.json');write(seed,base)
work=OUT/'hair_back-work.blend';save_part(work,objects)
candidate=Path(revise(seed,'hair_back',VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,('hair_back',));assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),body_positions_weights_unchanged=True,texture_contract='Inherited V26b five-image base-color contract, V26c hair roughness.')
write(OUT/'validation.json',report);write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={k.split('.')[0]:v for k,v in read(ROOT/'build/v026d/visual-roles.json').items()}
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
shutil.copytree(ROOT/'build/v026d/textures',OUT/'textures')
manifest=read(ROOT/'build/v026d/texture-manifest.json');manifest['glb_materials']=doc['materials'];manifest['material_roles']=read(OUT/'visual-roles.json');write(OUT/'texture-manifest.json',manifest)
print('V26e OVERLAP PASS',len(doc['materials']),len(doc['images']),flush=True)
