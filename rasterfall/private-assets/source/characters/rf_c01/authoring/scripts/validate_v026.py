"""Validate saved sources, opaque base-color contract and offline deformation.

This does not claim runtime facial animation or general inter-part collision
freedom. Blender pose samples are diagnostics; native review belongs to root.
"""
import bpy,bmesh,math,sys,json,struct
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha
from rf_parts import pipeline
VERSION=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'v026c'
OUT=ROOT/('build/'+VERSION);build=read(OUT/'validation.json')
manifest=Path(build['candidate']);baseline=ROOT/('assembly-'+VERSION+'-interface-baseline.json')
report=pipeline.verify(OUT/('rf_c01-'+VERSION+'.blend'),manifest,baseline,tuple(read(manifest)['parts']))
objects=[o for o in bpy.context.scene.objects if o.get('part_id')]
meshes={};materials={}
for obj in objects:
 bm=bmesh.new();bm.from_mesh(obj.data);areas=[f.calc_area() for f in bm.faces]
 assert all(math.isfinite(v) and v>1e-15 for v in areas),(obj.name,min(areas))
 assert all(v.link_faces for v in bm.verts),obj.name
 assert all(len(e.link_faces)<=2 for e in bm.edges),obj.name
 meshes[obj['object_id']]={'vertices':len(bm.verts),'triangles':sum(len(f.verts)-2 for f in bm.faces),'minimum_face_area':min(areas),'boundary_edges':sum(e.is_boundary for e in bm.edges)}
 if obj.get('part_id') in ('hair_front','hair_back'):assert not any(e.is_boundary for e in bm.edges),obj.name
 bm.free()
 for mat in obj.data.materials:
  if any(n.type=='TEX_IMAGE' for n in mat.node_tree.nodes):
   assert obj.data.uv_layers.active is not None,obj.name
   assert all(math.isfinite(c) and -.00001<=c<=1.00001 for v in obj.data.uv_layers.active.data for c in v.uv),obj.name
   for node in mat.node_tree.nodes:
    if node.type=='TEX_IMAGE':
     assert node.extension=='EXTEND' and node.interpolation=='Linear',mat.name
     assert node.image.packed_file and node.image.colorspace_settings.name=='sRGB',mat.name
  materials[mat['export_id']]=mat['visual_role']
assert materials['RF_C01_Face']=='face' and materials['RF_C01_BodySkin']=='skin'
report['mesh_audit']=meshes
s=bpy.context.scene;arm=next(o for o in s.objects if o.type=='ARMATURE')
samples=[]
for name in ('idle','walk','aim'):
 arm.animation_data.action=bpy.data.actions['RF_C01_V05_'+name];a,b=arm.animation_data.action.frame_range
 for i in range(17):
  frame=a+(b-a)*i/16;s.frame_set(int(frame),subframe=frame-int(frame));minimum=1
  for obj in objects:
   e=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());m=e.to_mesh();m.calc_loop_triangles()
   assert all(math.isfinite(c) for v in m.vertices for c in v.co),obj.name
   area=min(t.area for t in m.loop_triangles);assert area>1e-16,(obj.name,name,frame,area)
   minimum=min(minimum,area);e.to_mesh_clear()
  samples.append({'action':name,'frame':frame,'minimum_triangle_area':minimum})
report['action_samples']=samples
expressions=[]
for name in ('Blink','MouthOpen','BrowDown','SmileL'):
 for value in (0,.5,1):
  affected=0
  for obj in objects:
   if not obj.data.shape_keys:continue
   keys=obj.data.shape_keys.key_blocks
   for key in keys:key.value=0
   if name not in keys:continue
   if value==.5 and name+'Mid' in keys:keys[name+'Mid'].value=1
   else:keys[name].value=value
   e=obj.evaluated_get(bpy.context.evaluated_depsgraph_get());m=e.to_mesh()
   assert all(math.isfinite(c) for v in m.vertices for c in v.co)
   affected+=1;e.to_mesh_clear()
  expressions.append({'name':name,'value':value,'parts_with_keys':affected})
report['expression_samples']=expressions
assert all(sha(Path(p))==h for p,h in build['protected_sources'].items())
v25=read(ROOT/'assembly-head-v025d.json');current=read(manifest)
for field in ('vertices','weights','edges','loops'):
 assert current['parts']['body']['records']['body/rf_c01_v07_body'][field]==v25['parts']['body']['records']['body/rf_c01_v07_body'][field],field
assert current['rig']==v25['rig']
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();n=struct.unpack_from('<I',raw,12)[0];doc=json.loads(raw[20:20+n]);binary=raw[28+n:]
assert len(doc['images'])<=8 and len(doc['skins'])==1
images=[]
for item in doc['images']:
 assert item['mimeType']=='image/png' and 'bufferView' in item,item
 view=doc['bufferViews'][item['bufferView']];blob=binary[view.get('byteOffset',0):view.get('byteOffset',0)+view['byteLength']]
 assert blob[:8]==b'\x89PNG\r\n\x1a\n'
 w,h=struct.unpack_from('>II',blob,16);assert 0<w<=1024 and 0<h<=1024
 images.append({'name':item.get('name'),'width':w,'height':h,'bytes':len(blob)})
roles=read(OUT/'visual-roles.json');assert set(roles)=={m['name'] for m in doc['materials']}
for mat in doc['materials']:
 assert mat.get('alphaMode','OPAQUE')=='OPAQUE'
 pbr=mat['pbrMetallicRoughness'];assert pbr.get('baseColorFactor',[1,1,1,1])[3]==1
 assert not any(k in mat for k in ('normalTexture','occlusionTexture','emissiveTexture'))
 assert 'metallicRoughnessTexture' not in pbr
 if mat['name'].startswith('RF_C01_Garment_'):assert 'baseColorFactor' in pbr
 if 'baseColorTexture' in pbr:assert pbr['baseColorTexture'].get('texCoord',0)==0
for sampler in doc['samplers']:
 assert sampler['wrapS']==33071 and sampler['wrapT']==33071
 assert sampler['magFilter']==9729 and sampler['minFilter']==9987
report['texture_contract']={'images':images,'materials':len(doc['materials']),'all_roles_exact':True,'samplers':doc['samplers'],'face_body_separate':True,'uv0_range':[0,1],'glb_sha256':sha(OUT/('rf_c01-'+VERSION+'.glb'))}
report['scope']='Topology, UV0, exact image/material contract, protected rig/body data, authored finite/nondegenerate skeletal samples, and finite offline expressions. Not full collision testing or runtime facial animation.'
write(OUT/'deformation-validation.json',report)
print('V26 VALIDATION PASS',VERSION,len(samples),len(expressions),len(images),flush=True)
