"""RF-C01 V24: locked-source mature anime sculpt and tailored shoulders.

Explicit seven-part/interface migration. Older assemblies are read-only. Hair
keeps the original topology, UVs and weights and becomes an editable frozen
source for this sculpt revision; the old procedural recipes remain available.
"""
import bpy,bmesh,math,sys,copy
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'v024c'
OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-body-v023a.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()}
protected[str(BASE)]=sha(BASE)
def smooth(t):
 t=max(0.,min(1.,t));return t*t*(3-2*t)
def bell(t,c,w):return math.exp(-((t-c)/w)**2)
def headmap(p):
 x,y,z=p;ax=abs(x);front=smooth((.017-y)/.060)
 # Anatomical jaw taper with a supported chin, rather than a rectangular jaw.
 x*=1-.085*bell(z,1.468,.029)*smooth((ax-.023)/.035)*front
 y-=.0016*bell(ax,.043,.021)*bell(z,1.500,.020)*front
 # Keep the existing nasal bridge; define the tip/alar transition gently.
 y-=.0010*bell(ax,.009,.004)*bell(z,1.496,.006)*front
 y+=.0009*bell(x,0,.006)*bell(z,1.504,.007)*front
 # Lift the orbital opening coherently, including every eye component/key.
 orbit=bell(ax,.036,.029)*bell(z,1.534,.019)*smooth((-.018-y)/.025)
 z+=(z-1.534)*.095*orbit
 # A shorter neck keeps the adult proportions while reducing the doll look.
 z-=.009*smooth((z-1.348)/.080)
 # Round the crown slightly, maintaining the occiput/neck separation.
 z+=.003*bell(x,0,.064)*bell(y,.018,.078)*smooth((z-1.586)/.045)
 return Vector((x,y,z))
def map_mesh(obj,fn):
 old=[v.co.copy() for v in obj.data.vertices]
 if obj.data.shape_keys:
  for key in obj.data.shape_keys.key_blocks:
   for v in key.data:v.co=fn(v.co.copy())
 for v,p in zip(obj.data.vertices,old):v.co=fn(p)
 obj.data.update()
def color(mat,rgb,roughness=None):
 mat.diffuse_color=(*rgb,1)
 for node in mat.node_tree.nodes:
  if node.type=='BSDF_PRINCIPLED':
   node.inputs['Base Color'].default_value=(*rgb,1)
   if roughness is not None:node.inputs['Roughness'].default_value=roughness
def components(mesh):
 adj=[[] for v in mesh.vertices]
 for e in mesh.edges:
  a,b=e.vertices;adj[a].append(b);adj[b].append(a)
 seen=set();groups=[]
 for v in mesh.vertices:
  if v.index in seen:continue
  todo=[v.index];seen.add(v.index);indices=[]
  while todo:
   i=todo.pop();indices.append(i)
   for j in adj[i]:
    if j not in seen:seen.add(j);todo.append(j)
  groups.append(indices)
 return groups
def edit_body(obj):
 m=obj.data;groups=components(m)
 assert len(groups)==71 and len(m.vertices)==13075
 # Retire the dark circular piping which read as detached shoulder armor.
 remove=set(groups[69]+groups[70])
 bm=bmesh.new();bm.from_mesh(m);bm.verts.ensure_lookup_table()
 bmesh.ops.delete(bm,geom=[bm.verts[i] for i in remove],context='VERTS')
 bm.to_mesh(m);bm.free()
 for v in m.vertices:
  p=v.co;a=abs(p.x)
  if .11<a<.35 and p.z>1.19:
   cap=bell(a,.204,.051)
   p.z-=.010*cap*smooth((p.z-1.287)/.047)
   p.y=.008+(p.y-.008)*(1-.075*cap)
   # Sleeve fabric has a little room below the deltoid and across the biceps.
   fabric=.025*bell(a,.294,.046)
   p.y=.008+(p.y-.008)*(1+fabric)
   p.z=1.294+(p.z-1.294)*(1+fabric)
  # A modestly broader upper torso supports the neck and sleeve transition.
  if a<.15 and 1.24<p.z<1.35:
   p.z+=.003*bell(a,.10,.05)*bell(p.z,1.32,.026)
 for mat in m.materials:
  if 'RF_Yoke' in mat.name:color(mat,(.49,.555,.55),.76)
 m.update()
def edit_eyes(obj):
 name=obj.name
 if 'iris' in name:
  side=1 if name.startswith('L') else -1
  def iris(p):
   x,y,z=p;cx=side*.036;x=cx+(x-cx)*1.11;z=1.534+(z-1.534)*1.07
   # Reproject the pigment to the frozen ellipsoidal eye surface.
   h=max(.03,1-((x-cx)/.025)**2-((z-1.534)/.018)**2)
   y=-.0385-.01405*math.sqrt(h)-.00027
   return Vector((x,y,z))
  map_mesh(obj,iris)
 elif 'eyebrow' in name:
  # Keep the graceful arch, but give the brow a clear mid-distance silhouette.
  old=[v.co.copy() for v in obj.data.vertices]
  zs=[p.z for p in old];center=sum(zs)/len(zs)
  map_mesh(obj,lambda p:Vector((p.x,p.y-.0003,center+(p.z-center)*1.20-.0007)))
 elif 'upper eyelash' in name or 'outer lash' in name:
  map_mesh(obj,lambda p:p+Vector((0,-.00035,.00035)))
 map_mesh(obj,headmap)
def edit_hair(obj):
 part=obj['part_id'];old=[v.co.copy() for v in obj.data.vertices]
 C=Vector((0,.015,1.55))
 if part=='hair_base':
  for v,p in zip(obj.data.vertices,old):
   d=(p-C).normalized();a=math.atan2(d.y,d.x)
   # Broad continuous crown flow, not a separate oversized helmet shell.
   w=smooth((p.z-1.54)/.07)*smooth((d.y+.3)/.8)
   q=p+d*(.0013*math.sin(5*a+.8)*w)
   v.co=headmap(q)
 else:
  assert len(old)==450
  idx=int(obj['object_id'].rsplit('_',1)[1]);front=part=='hair_front'
  centers=[(old[j*9+4]+old[225+j*9+4])*.5 for j in range(25)]
  for j,c in enumerate(centers):
   t=j/24;rad=(c-C).normalized();tangent=Vector((-rad.y,rad.x,0)).normalized()
   if front:
    primary=idx in (0,3,4)
    root=.0055*bell(t,.25,.29)
    outward=.0025*math.sin(math.pi*t)**2
    shift=Vector((.0035*(1-t)**2,-root,0))
    shift+=rad*outward
    # Secondary swept fringe ends at three different heights above the brows.
    if idx in (1,2):shift.z-=.004* smooth((t-.38)/.62)
    if idx==0:shift.x-=.0035*smooth((t-.64)/.36)
    if idx==4:shift.x+=.003*smooth((t-.63)/.37)
    width=1.03 if primary else .93
   else:
    primary=idx in (0,2,4,6,8,10)
    # Staggered overlaps and subtly flipped ends break the bob helmet outline.
    outward=(.0038 if primary else .0010)*math.sin(math.pi*t)**2
    outward+=(.007 if idx in (2,6,8) else .003)*smooth((t-.73)/.27)
    shift=rad*outward+tangent*((.004 if idx%2 else -.003)*smooth((t-.25)/.75))
    shift.z+=(.007 if not primary else -.003)*smooth((t-.68)/.32)
    width=1.05 if primary else .87
   for side in range(2):
    for k in range(9):
     n=side*225+j*9+k;u=(k-4)/4
     offset=old[n]-c
     # A wide central plane with a fine bevel, narrowing near the tip.
     q=c+offset*width+shift
     ridge=(.0023 if front else .0028)*math.sin(math.pi*t)**2*(1-u*u)
     q+=rad*ridge
     obj.data.vertices[n].co=headmap(q)
 for mat in obj.data.materials:
  if 'Hair' in mat.name:color(mat,(.014,.038,.050),.54)
 obj.data.update()

def support_roots(obj,locks):
 # The scalp is a continuous support for the visible outer locks. Fill beneath
 # their roots instead of clipping whole strands into an unrelated bald shell.
 C=headmap(Vector((0,.015,1.55)))
 trees=[BVHTree.FromPolygons([v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons]) for o in locks]
 points=[v.co.copy() for v in obj.data.vertices];offset=[]
 for p in points:
  d=(p-C).normalized();radius=(p-C).length;target=radius
  for tree in trees:
   hit,n,face,dist=tree.ray_cast(C+d*.4,-d,.4)
   if hit is not None:target=max(target,(hit-C).length-.00018)
  offset.append(max(0,target-radius)*smooth((p.z-1.567)/.070))
 adj=[set() for p in points]
 for e in obj.data.edges:
  a,b=e.vertices;adj[a].add(b);adj[b].add(a)
 for _ in range(3):
  offset=[max(v,sum(offset[j] for j in adj[i])/len(adj[i])) for i,v in enumerate(offset)]
 for v,p,amount in zip(obj.data.vertices,points,offset):v.co=p+(p-C).normalized()*amount
 obj.data.update()

interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':list(base['parts']),'reason':'Mature anime orbital/jaw/crown refinement; neck shortening applied coherently to head, facial parts and hair. Tailored shoulder silhouette; same skeleton and sockets.'}
interface['refinement_v024']={'neck_shortening_m':.009,'crown_lift_m':.003,'orbital_opening_strength':.095,'recipe':'scripts/build_v024c.py','recipe_sha256':sha(Path(__file__))}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface)
base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
for part in ('hair_base','hair_back'):
 entry=base['parts'][part];entry['mode']='editable_mesh'
 entry['source_migration']={'from_mode':'procedural','retained_frozen_source':entry['file'],'reason':'Explicit frozen-mesh sculpt, retained topology/UV/weights. Historical recipe is not the next revision source.'}
 for key in ('generator','parameters','parameters_sha256'):entry.pop(key,None)
candidate=ROOT/('assembly-'+VERSION+'-seed.json');write(candidate,base)
stats={}
for part in ('head','eyes','mouth','hair_front','hair_back','hair_base','body'):
 entry=read(candidate)['parts'][part]
 bpy.ops.wm.read_factory_settings(use_empty=True)
 objects=load_objects(ROOT/entry['file'],entry['objects'])
 references=[]
 if part=='hair_base':
  for pid in ('hair_front','hair_back'):
   ref=read(candidate)['parts'][pid]
   references+=load_objects(ROOT/ref['file'],ref['objects'])
 for obj in objects:
  if part=='body':edit_body(obj)
  elif part=='eyes':edit_eyes(obj)
  elif part.startswith('hair'):edit_hair(obj)
  else:map_mesh(obj,headmap)
  if part=='head':
   for mat in obj.data.materials:
    if 'upper lip' in mat.name:color(mat,(.50,.265,.230),.72)
    elif 'lower lip' in mat.name:color(mat,(.59,.345,.287),.69)
  stats[obj['object_id']]={'vertices':len(obj.data.vertices),'triangles':sum(len(p.vertices)-2 for p in obj.data.polygons)}
  if part=='hair_base':support_roots(obj,references)
 for obj in references:bpy.data.objects.remove(obj,do_unlink=True)
 work=OUT/(part+'-work.blend');save_part(work,objects)
 candidate=Path(revise(candidate,part,VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,tuple(base['parts']))
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),stats=stats,interface_migration=interface['migration'])
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
print('V24 BUILD PASS',VERSION,flush=True)
