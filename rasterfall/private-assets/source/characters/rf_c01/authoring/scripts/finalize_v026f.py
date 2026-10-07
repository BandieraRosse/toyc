"""Relax the neutral eyelids using the authored half-blink correspondence.

The existing full and half blink targets remain exact. Basis and all other
expression targets receive 24% of the old Basis-to-BlinkMid displacement, so
non-blink expression deltas are preserved. No eye-center/rig/topology edits.
"""
import bpy,sys,json,struct,shutil
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION='v026f';OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-hair_back-v026e.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()};protected[str(BASE)]=sha(BASE)
interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['head','eyes'],'reason':'Relax the neutral eyelids by 24% of the existing half-blink displacement. Half/full Blink endpoints remain exact and every other expression receives the same Basis displacement. Eye centers, mouth, skull, rig and topology retained.'}
interface['neutral_lid_v026f']={'script':'scripts/finalize_v026f.py','sha256':sha(Path(__file__)),'half_blink_fraction':.24,'blink_targets_preserved':True}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface);base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
candidate=ROOT/('assembly-'+VERSION+'-seed.json');write(candidate,base);changes={}
for part in ('head','eyes'):
 bpy.ops.wm.read_factory_settings(use_empty=True);e=read(candidate)['parts'][part];objects=load_objects(ROOT/e['file'],e['objects'])
 for obj in objects:
  if not obj.data.shape_keys:continue
  keys=obj.data.shape_keys.key_blocks
  if 'BlinkMid' not in keys:continue
  neutral=[v.co.copy() for v in keys['Basis'].data];delta=[(v.co-p)*.24 for v,p in zip(keys['BlinkMid'].data,neutral)]
  for key in keys:
   if key.name in ('Blink','BlinkMid'):continue
   for v,d in zip(key.data,delta):v.co+=d
  for v,p,d in zip(obj.data.vertices,neutral,delta):v.co=p+d
  obj.data.update();changes[obj['object_id']]={'maximum_displacement':max(d.length for d in delta),'vertices_moved':sum(d.length>1e-9 for d in delta),'blink_targets_exact':True}
 work=OUT/(part+'-work.blend');save_part(work,objects);candidate=Path(revise(candidate,part,VERSION,'publish',source=work))
assert changes
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,('head','eyes'));assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),body_positions_weights_unchanged=True,texture_contract='Inherited V26e five-image base-color contract, no material/UV changes.',neutral_lid_changes=changes)
write(OUT/'validation.json',report);write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={k.split('.')[0]:v for k,v in read(ROOT/'build/v026e/visual-roles.json').items()}
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
shutil.copytree(ROOT/'build/v026e/textures',OUT/'textures')
manifest=read(ROOT/'build/v026e/texture-manifest.json');manifest['glb_materials']=doc['materials'];manifest['material_roles']=read(OUT/'visual-roles.json');write(OUT/'texture-manifest.json',manifest)
write(OUT/'rf_c01_v026f.asset.json',{'schema':1,'id':'rf_c01_v026f','type':'character','source':'rf_c01-v026f-runtime.glb'})
print('V26f RELAXED LID PASS',changes,flush=True)
