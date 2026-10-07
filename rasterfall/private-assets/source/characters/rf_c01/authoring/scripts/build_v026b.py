"""RF-C01 V26 content: local face/hair sculpt, explicit UVs and base-color paint.

V25d rig, rest, sockets, actions and body positions/weights remain locked. Facial
geometry edits apply to Basis and every authored offline expression. Textures
are original procedural paint authored in texture_v026.py, never baked lighting.
"""
import bpy,math,sys,json,struct
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path[:0]=[str(REPO/'tools/blender'),str(ROOT/'scripts')]
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
import texture_v026b as tex
VERSION=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'v026b'
OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-head-v025d.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()}
protected[str(BASE)]=sha(BASE)
tex.create_textures(OUT/'textures')
C=Vector((0,.015,1.541))
def smooth(t):
 t=max(0.,min(1.,t));return t*t*(3-2*t)
def bell(x,c,w):return math.exp(-((x-c)/w)**2)
def face(p):
 x,y,z=p;front=smooth((-.024-y)/.030)
 # Slightly firmer cheek-to-jaw transition and supported chin plane.
 x*=1-.027*bell(z,1.450,.021)*smooth((abs(x)-.021)/.032)*front
 y-=.0015*bell(x,0,.023)*bell(z,1.438,.010)*front
 # Distinguish nasal root, small tip and alar support without a nose spike.
 y+=.0010*bell(x,0,.012)*bell(z,1.517,.009)*front
 y-=.0008*bell(abs(x),.009,.005)*bell(z,1.487,.006)*front
 # The upper/lower mouth planes transition into the surrounding cheek.
 y-=.0005*bell(x,0,.021)*bell(z,1.463,.011)*front
 return Vector((x,y,z))
def map_mesh(obj,fn):
 old=[v.co.copy() for v in obj.data.vertices]
 if obj.data.shape_keys:
  for key in obj.data.shape_keys.key_blocks:
   for v in key.data:v.co=fn(v.co.copy())
 for v,p in zip(obj.data.vertices,old):v.co=fn(p)
 obj.data.update()
def hair_sculpt(obj):
 part=obj['part_id'];old=[v.co.copy() for v in obj.data.vertices]
 if part=='hair_base':return
 idx=int(obj['object_id'].rsplit('_',1)[1])
 if part=='hair_back':
  assert len(old)==558
  for j in range(31):
   t=j/30;c=(old[j*9+4]+old[279+j*9+4])*.5
   radial=(c-C).normalized()
   # The upper section is a long flowing plane, not a bulb with two leaf edges.
   center_shift=radial*(-.0015*bell(t,.44,.27))
   lift=([.005,.003,.011,.004,.003,.013,.005,.003,.010,.003,.004][idx])*smooth((t-.55)/.45)
   for side in range(2):
    for k in range(9):
     n=side*279+j*9+k;offset=old[n]-c;u=(k-4)/4
     width=1-.12*bell(t,.52,.25)
     p=c+offset*width+center_shift+Vector((0,0,lift))
     # Weave one edge under the neighboring mass; it is no longer a closed
     # symmetrical raised panel throughout its entire length.
     p-=radial*(.0015*smooth((u+.25)/1.25)*math.sin(math.pi*t)**2)
     obj.data.vertices[n].co=p
 else:
  sections=[(0,33)]+([(594,21)] if len(old)==972 else [])
  for start,rows in sections:
   layer=rows*9
   for j in range(rows):
    t=j/(rows-1);c=(old[start+j*9+4]+old[start+layer+j*9+4])*.5
    delta=Vector((0,0,0))
    if start==0:
     if idx==0:delta.z+=.009*smooth((t-.59)/.41)
     if idx==4:
      delta.z+=.011*smooth((t-.52)/.48);delta.y+=.005*smooth((t-.60)/.40)
     if idx==3:delta.z+=.004*smooth((t-.55)/.45)
    for side in range(2):
     for k in range(9):
      n=start+side*layer+j*9+k;obj.data.vertices[n].co=old[n]+delta
 obj.data.update()
def support_roots(obj,locks):
 trees=[BVHTree.FromPolygons([v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons]) for o in locks]
 pts=[v.co.copy() for v in obj.data.vertices];offset=[]
 for p in pts:
  d=(p-C).normalized();r=(p-C).length;target=r
  for tree in trees:
   hit,n,face,dist=tree.ray_cast(C+d*.4,-d,.4)
   if hit is not None:target=max(target,(hit-C).length-.00028)
  # Front hairline remains the existing cut; posterior roots have a broader
  # common support envelope to avoid individual rounded leaf shoulders.
  onset=1.537 if d.y>0 else 1.593
  offset.append((target-r)*smooth((p.z-onset)/.058))
 adj=[set() for p in pts]
 for e in obj.data.edges:
  a,b=e.vertices;adj[a].add(b);adj[b].add(a)
 for _ in range(3):offset=[max(v,.8*sum(offset[j] for j in adj[i])/len(adj[i])) for i,v in enumerate(offset)]
 for v,p,o in zip(obj.data.vertices,pts,offset):v.co=p+(p-C).normalized()*o
 obj.data.update()
def face_uv(obj):
 tex.uv_write(obj,lambda p,poly:(.5+math.atan2(p.x,-(p.y-.015))/math.tau,(p.z-1.32)/.34));tex.fix_wrap(obj)
def paint(part,objects,images):
 images={name:tex.image_for(images,name) for name in images}
 cache={}
 def mat(name,role,image=None,factor=(1,1,1),rough=.6,metal=0):
  if name not in cache:cache[name]=tex.material(name,role,images[image] if image else None,factor,rough,metal)
  return cache[name]
 for obj in objects:
  mesh=obj.data;original=list(mesh.materials)
  if part=='head':
   mesh.materials.clear();mesh.materials.append(mat('RF_C01_Face','face','face',rough=.65))
   for p in mesh.polygons:p.material_index=0
   face_uv(obj)
  elif part=='eyes':
   name=obj.name
   if 'iris' in name:
    pts=[v.co for v in mesh.vertices];cx=(min(p.x for p in pts)+max(p.x for p in pts))*.5;cz=(min(p.z for p in pts)+max(p.z for p in pts))*.5
    rx=max(abs(p.x-cx) for p in pts);rz=max(abs(p.z-cz) for p in pts)
    tex.uv_write(obj,lambda p,poly:(.5+.445*(p.x-cx)/rx,.5+.445*(p.z-cz)/rz))
    replacement=mat('RF_C01_Iris','eyes','eyes',rough=.24)
   elif 'eyeball' in name or 'catchlight' in name:replacement=mat('RF_C01_EyeWhite','eyes',factor=(.83,.81,.74),rough=.27)
   elif 'lower lid' in name:
    replacement=mat('RF_C01_Lid','face','face',rough=.66);face_uv(obj)
   else:replacement=mat('RF_C01_BrowLash','eyes',factor=(.019,.029,.033),rough=.70)
   mesh.materials.clear();mesh.materials.append(replacement)
   for p in mesh.polygons:p.material_index=0
  elif part=='mouth':
   mesh.materials.clear();mesh.materials.append(mat('RF_C01_Mouth','eyes',factor=(.035,.012,.015),rough=.8))
   for p in mesh.polygons:p.material_index=0
  elif part.startswith('hair'):
   mesh.materials.clear();mesh.materials.append(mat('RF_C01_Hair','hair','hair',rough=.44))
   for p in mesh.polygons:p.material_index=0
   if part=='hair_base':
    tex.uv_write(obj,lambda p,poly:(.5+math.atan2(p.x,-(p.y-.015))/math.tau,math.acos(max(-1,min(1,(p-C).normalized().z)))/2.0));tex.fix_wrap(obj)
   else:
    idx=int(obj['object_id'].rsplit('_',1)[1]);tile=(idx+(3 if part=='hair_front' else 0))%8
    uv=mesh.uv_layers.active
    for l in uv.data:
     u,v=l.uv;l.uv=((tile+(4/128)+(120/128)*u)/8,8/1024+(1008/1024)*v)
  elif part=='body':
   tile_by_slot={}
   for i,source in enumerate(original):
    id=source.get('export_id','');factor=tuple(source.diffuse_color[:3])
    if 'Skin' in id:
     replacement=mat('RF_C01_BodySkin','skin','skin',rough=.66);tile_by_slot[i]=None
    else:
     if 'RF_Shirt' in id or 'RF_Yoke' in id:tile=(0,0);rough=.84;metal=0;role='clothing'
     elif 'RF_Pants' in id:tile=(1,0);rough=.87;metal=0;role='clothing'
     elif 'RF_Boots' in id:tile=(0,1);rough=.48;metal=0;role='equipment'
     else:tile=(1,1);rough=.47;metal=.12;role='equipment'
     replacement=mat('RF_C01_Garment_'+id.replace(' / ','_').replace(' ','_'),role,'garment',factor,rough,metal);tile_by_slot[i]=tile
    mesh.materials[i]=replacement
   def body_uv(p,poly):
    tile=tile_by_slot[poly.material_index]
    if tile is None:
     u,v=tex.cylindrical(p,0,(0,.008,1.294),.35,.59);return u,v
    if abs(poly.center.x)>.16 and poly.center.z>1.2:u,v=tex.cylindrical(p,0,(0,.008,1.294),.15,.71)
    elif poly.center.z<.95:
     side=1 if poly.center.x>=0 else -1;u,v=tex.cylindrical(p,2,(side*.09,.008,0),.0,.99)
    else:u,v=tex.cylindrical(p,2,(0,.008,0),.96,1.38)
    return (tile[0]+.01+.98*u)*.5,(tile[1]+.01+.98*v)*.5
   tex.uv_write(obj,body_uv)
  mesh.update()
 return {m.name:m['visual_role'] for m in cache.values()}

interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['head','mouth','hair_front','hair_back','hair_base'],'reason':'Local nasal, mouth and jaw refinement, posterior hair support and shorter ear-framing tips. Eye positions, skull extrema, neck seam and skeleton unchanged.'}
interface['content_v026']={'script':'scripts/build_v026b.py','sha256':sha(Path(__file__)),'texture_script':'scripts/texture_v026b.py','texture_sha256':sha(Path(tex.__file__)),'body_positions_weights_preserved':True}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface)
base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
candidate=ROOT/('assembly-'+VERSION+'-seed.json');write(candidate,base)
all_roles={}
for part in ('head','eyes','mouth','hair_front','hair_back','hair_base','body'):
 bpy.ops.wm.read_factory_settings(use_empty=True)
 entry=read(candidate)['parts'][part];objects=load_objects(ROOT/entry['file'],entry['objects'])
 refs=[]
 if part=='hair_base':
  for pid in ('hair_front','hair_back'):
   e=read(candidate)['parts'][pid];refs+=load_objects(ROOT/e['file'],e['objects'])
 for obj in objects:
  if part in ('head','mouth'):map_mesh(obj,face)
  elif part.startswith('hair'):hair_sculpt(obj)
  if part=='hair_base':support_roots(obj,refs)
 for obj in refs:bpy.data.objects.remove(obj,do_unlink=True)
 images={name:bpy.data.images.load(str(OUT/('textures/rf_c01_'+name+'_basecolor.png')),check_existing=False) for name in ('face','skin','eyes','hair','garment')}
 all_roles.update(paint(part,objects,images))
 work=OUT/(part+'-work.blend');save_part(work,objects)
 candidate=Path(revise(candidate,part,VERSION,'publish',source=work))
 # Correctly scope historical conversion metadata for future continuation.
 doc=read(candidate)
 if part in ('hair_base','hair_back'):
  doc['parts'][part]['source_migration']['reason']='V24c procedural-to-editable conversion retained topology/UV/weights only at that step. V25b rebuilt front/back topology and weights; V26 edits frozen mesh/UV/materials. Continue from this locked source, never rerun historical hair recipes.'
  write(candidate,doc)
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,tuple(base['parts']))
assert all(sha(Path(p))==h for p,h in protected.items())
current=read(candidate);old=read(BASE)
for field in ('vertices','weights','edges','loops'):
 assert current['parts']['body']['records']['body/rf_c01_v07_body'][field]==old['parts']['body']['records']['body/rf_c01_v07_body'][field],field
report.update(protected_sources=protected,candidate=str(candidate),body_positions_weights_unchanged=True,texture_contract='PNG embedded, sRGB, opaque, UV0, clamp, baseColor only; roughness/metallic constants')
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
raw=(OUT/('rf_c01-'+VERSION+'.glb')).read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={}
for material in doc['materials']:
 name=material['name'];canonical=name.split('.')[0]
 assert canonical in all_roles,(name,canonical,all_roles)
 roles[name]=all_roles[canonical]
 if name.startswith('RF_C01_Garment_'):
  assert 'baseColorFactor' in material['pbrMetallicRoughness'],name
assert len(doc.get('images',[]))<=8
write(OUT/'visual-roles.json',roles)
write(OUT/'texture-manifest.json',{'images':{p.name:{'sha256':sha(p),'bytes':p.stat().st_size} for p in (OUT/'textures').glob('*.png')},'glb_materials':doc['materials'],'glb_samplers':doc.get('samplers',[]),'material_roles':roles})
print('V26 CONTENT BUILD PASS',VERSION,len(doc['materials']),len(doc.get('images',[])),flush=True)
