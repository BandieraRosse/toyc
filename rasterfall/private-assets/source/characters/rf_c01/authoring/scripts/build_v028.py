"""Lengthen the short upper/forearms in source space for a physical-size rifle.

Explicit body + arm-rest migration. Face/eyes/hair, hands, weights and topology
are preserved. This is not a runtime stretch or a weapon-scale compensation.
"""
import bpy,sys,copy,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part,part_record
from rf_parts.records import rig_record
from rf_parts import pipeline
VERSION='v028';OUT=ROOT/'build'/VERSION;OUT.mkdir(exist_ok=False)
base=read(ROOT/'assembly-eyes-v027.json');manifest=copy.deepcopy(base)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in [*base['parts'].values(),base['rig'],base['review'],base['interfaces']]}
bpy.ops.wm.read_factory_settings(use_empty=True)
entry=base['rig']
with bpy.data.libraries.load(str(ROOT/entry['file']),link=False) as (src,dst):
    dst.objects=[entry['object']];dst.actions=entry['actions']
arm=dst.objects[0];bpy.context.scene.collection.objects.link(arm)
for a in dst.actions:a.use_fake_user=True
arm.animation_data_create();arm.animation_data.action=bpy.data.actions[entry['review_action']]
upper=abs(arm.data.bones['RF_L_UPPER_ARM'].head_local.x)
wrist=abs(arm.data.bones['RF_L_HAND'].head_local.x)
def remap(x):
    a=abs(x);delta=max(0,min(a,wrist)-upper)*.25
    return x+(delta if x>=0 else -delta)
before={b.name:(tuple(b.head_local),tuple(b.tail_local)) for b in arm.data.bones}
bpy.context.view_layer.objects.active=arm;arm.select_set(True)
bpy.ops.object.mode_set(mode='EDIT')
changed=[]
for b in arm.data.edit_bones:
    if any(t in b.name for t in ('UPPER_ARM','FOREARM','HAND','FINGER','WEAPON','FOREGRIP')):
        b.head.x=remap(b.head.x);b.tail.x=remap(b.tail.x);changed.append(b.name)
bpy.ops.object.mode_set(mode='OBJECT')
for b in arm.data.bones:
    if b.name not in changed:assert before[b.name]==(tuple(b.head_local),tuple(b.tail_local)),b.name
rigpath=ROOT/'rig/v028.blend'
bpy.data.libraries.write(str(rigpath),{arm,*dst.actions},fake_user=True)
manifest['rig'].update(file='rig/v028.blend',sha256=sha(rigpath),record=rig_record(arm))
objects=load_objects(ROOT/base['parts']['body']['file'],base['parts']['body']['objects'])
count=0
for obj in objects:
    assert not obj.data.shape_keys
    for v in obj.data.vertices:
        names=[obj.vertex_groups[g.group].name for g in v.groups if g.weight>0]
        if any(any(t in n for t in ('UPPER_ARM','FOREARM','HAND','FINGER','SHOULDER')) for n in names):
            x=remap(v.co.x)
            if x!=v.co.x:count+=1;v.co.x=x
    obj.data.update();obj['part_revision']=VERSION
records=part_record(objects)
for oid,record in records.items():
    old=base['parts']['body']['records'][oid]
    for k in ('edges','loops','polygons','weights','materials','uv','shape_keys'):
        assert record[k]==old[k],k
part=ROOT/'parts/body/v028.blend';save_part(part,objects)
manifest['parts']['body'].update(file='parts/body/v028.blend',sha256=sha(part),revision=VERSION,records=records,
    provenance={'operation':'body-arm-length-and-rest-migration','previous_sha256':base['parts']['body']['sha256'],
                'upper_forearm_scale':1.25,'hand_scale':1.0,'script_sha256':sha(Path(__file__))})
candidate=ROOT/'assembly-body-v028.json';write(candidate,manifest)
blend=OUT/'rf_c01-v028.blend';pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate)
for key in manifest['parts']:
    if key!='body':assert manifest['parts'][key]==base['parts'][key]
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(allowed_changes=['body','arm_rest'],rig_actions_unchanged=False,
    source_actions_preserved=True,arm_rest_changed=changed,body_vertices_changed=count,
    original_arm_length_m=wrist-upper,new_arm_length_m=(wrist-upper)*1.25,
    head_eyes_hair_sources_unchanged=True)
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/'rf_c01-v028.glb'))
raw=(OUT/'rf_c01-v028.glb').read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={k.split('.')[0]:v for k,v in read(ROOT/'build/v027/visual-roles.json').items()}
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
write(OUT/'rf_c01_v028.asset.json',{'schema':1,'id':'rf_c01_v028','type':'character','source':'rf_c01-v028-runtime.glb'})
print('ARM REST MIGRATION PASS',report)
