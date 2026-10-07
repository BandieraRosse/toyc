"""Explicit hair topology migration: primary clumps, short overlays and fine tips.

Frozen V24c head/body retained exactly. Each stable hair object remains owned by
its original part; new watertight clumps carry metric UVs and RF_HEAD weights.
"""
import bpy,bmesh,math,sys
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part
from rf_parts.revisions import revise
from rf_parts import pipeline
VERSION=sys.argv[sys.argv.index('--')+1] if '--' in sys.argv else 'v025b'
OUT=ROOT/('build/'+VERSION);OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-body-v024c.json';base=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in base['parts'].values()}
protected[str(BASE)]=sha(BASE)
C=Vector((0,.015,1.541))
def smooth(t):
 t=max(0.,min(1.,t));return t*t*(3-2*t)
def bell(t,c,w):return math.exp(-((t-c)/w)**2)
def tree_for(objects):return [BVHTree.FromPolygons([v.co for v in o.data.vertices],[list(p.vertices) for p in o.data.polygons]) for o in objects]
def source(part):
 e=read(candidate)['parts'][part]
 return load_objects(ROOT/e['file'],e['objects'])
def replace_mesh(obj,clumps):
 vs=[];fs=[];uvs=[]
 for rows in clumps:
  start=len(vs);ny=len(rows);nx=len(rows[0])
  for side in range(2):
   for j,row in enumerate(rows):
    for k,(p,n,thickness) in enumerate(row):
     vs.append(p-n*thickness*side);uvs.append((k/(nx-1),j/(ny-1)))
  layer=ny*nx
  for side in range(2):
   off=start+layer*side
   for j in range(ny-1):
    for k in range(nx-1):
     a=off+j*nx+k;face=(a,a+1,a+nx+1,a+nx)
     fs.append(face if side==0 else tuple(reversed(face)))
  boundary=list(range(nx))+[j*nx+nx-1 for j in range(1,ny)]+[(ny-1)*nx+k for k in range(nx-2,-1,-1)]+[j*nx for j in range(ny-2,0,-1)]
  for i,a in enumerate(boundary):
   b=boundary[(i+1)%len(boundary)];fs.append((start+a,start+layer+a,start+layer+b,start+b))
 materials=list(obj.data.materials)
 mesh=bpy.data.meshes.new('V25 layered hair / '+obj.name)
 mesh.from_pydata(vs,[],fs)
 for mat in materials:mesh.materials.append(mat)
 for p in mesh.polygons:p.use_smooth=True
 uv=mesh.uv_layers.new(name='UVMap')
 for loop in mesh.loops:uv.data[loop.index].uv=uvs[loop.vertex_index]
 obj.data=mesh
 for vg in list(obj.vertex_groups):obj.vertex_groups.remove(vg)
 vg=obj.vertex_groups.new(name='RF_HEAD');vg.add(list(range(len(vs))),1,'REPLACE')
 bm=bmesh.new();bm.from_mesh(mesh);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(mesh);bm.free();mesh.update()
def envelope(tree,a,theta):
 d=Vector((math.sin(theta)*math.cos(a),math.sin(theta)*math.sin(a),math.cos(theta)))
 hit,n,face,dist=tree.ray_cast(C,d,.4)
 assert hit is not None,(a,theta)
 volume=.006+.004*bell(theta,.75,.70)
 return hit+d*volume,d
def back_clump(tree,index):
 # The alternating short overlays end above the long main locks. No uniform
 # circumferential row of equally-sized teeth remains at the haircut boundary.
 angles=[-.045,.245,.57,.88,1.17,1.50,1.84,2.15,2.48,2.80,3.10]
 ends=[1.99,1.58,2.18,1.90,1.69,2.22,2.08,1.70,2.19,1.78,1.98]
 widths=[.21,.145,.245,.21,.15,.27,.24,.16,.25,.16,.21]
 primary=index in (0,2,3,5,6,8,10)
 start=.17 if primary else .50
 rows=[]
 for j in range(31):
  t=j/30;theta=start+(ends[index]-start)*t
  a=angles[index]+(.31 if index<5 else -.22)*(1-t)**1.7
  a+=(.13 if index%2 else -.09)*math.sin(math.pi*t)**2
  taper=math.sqrt(max(0,1-smooth((t-.51)/.49)))
  width=widths[index]*(.63+.37*math.sin(math.pi*t))*taper+.0011
  row=[]
  for k in range(9):
   u=(k-4)/4;aa=a+width*u
   skew=.075*u*math.sin(math.pi*t)*(1 if index%2 else -1)
   p,n=envelope(tree,aa,theta+skew)
   emerge=smooth((t-.06)/.22)
   dome=max(0,1-u*u)**.7
   relief=-.0022*(1-emerge)+emerge*(.0015+.0035*dome)
   relief-=.0018*abs(u)**3*(1-smooth((t-.72)/.28))
   # Free tips curve away from the core instead of terminating as cut ribbons.
   flip=(.012 if index in (2,5,8) else .004)*smooth((t-.72)/.28)
   p+=n*(relief+flip)
   tangent=Vector((-n.y,n.x,0)).normalized()
   p+=tangent*((.009 if index%2 else -.006)*smooth((t-.65)/.35))
   if not primary:p.z+=.002*math.sin(math.pi*t)
   row.append((p,n,.0010*(.65+.35*math.sin(math.pi*t))))
  rows.append(row)
 return rows
def front_clumps(obj):
 old=[v.co.copy() for v in obj.data.vertices];idx=int(obj['object_id'].rsplit('_',1)[1])
 centers=[(old[j*9+4]+old[225+j*9+4])*.5 for j in range(25)]
 vectors=[old[j*9+8]-old[j*9] for j in range(25)]
 def sample(items,t):
  f=t*(len(items)-1);i=min(int(f),len(items)-2);s=f-i
  p0=items[max(0,i-1)];p1=items[i];p2=items[i+1];p3=items[min(len(items)-1,i+2)]
  return ((2*p1)+(-p0+p2)*s+(2*p0-5*p1+4*p2-p3)*s*s+(-p0+3*p1-3*p2+p3)*s*s*s)*.5
 result=[]
 for secondary in range(2 if idx in (0,3) else 1):
  rows=[]
  for j in range(33 if not secondary else 21):
   t=j/(32 if not secondary else 20)
   q=.36+.59*t if secondary else t
   c=sample(centers,q);cross=sample(vectors,q);width=cross.length*.5
   cross.normalize();n=(c-C).normalized()
   # Primary asymmetric swept masses; small offshoots have distinct directions.
   width*=([1.02,.86,.66,.83,.77][idx] if not secondary else .30)
   if secondary:
    c+=cross*(.006*(1-t)-.009*t)+n*(-.002+.0055*smooth(t/.32))
    c.z+=.008*smooth(t)
    width*=1-smooth((t-.45)/.55)
   else:
    c-=n*(.0045*(1-smooth(t/.26)))
    if idx in (0,4):c.z+=.005*smooth((t-.70)/.30)
    if idx==3:c.x+=.004*smooth((t-.60)/.40)
   width=max(.00010,width)
   row=[]
   for k in range(9):
    u=(k-4)/4;ridge=.0030*math.sin(math.pi*t)**1.2*(1-u*u)
    p=c+cross*(u*width)+n*ridge
    row.append((p,n,.0009))
   rows.append(row)
  result.append(rows)
 return result

interface=read(ROOT/base['interfaces']['file']);interface['revision']=VERSION
interface['migration']={'previous_file':base['interfaces']['file'],'previous_sha256':base['interfaces']['sha256'],'affected_parts':['hair_front','hair_base','hair_back'],'reason':'Explicit hair topology migration: swept primary clumps, alternating short overlays, fine offshoots. Head, face, neck seam, rig, body remain V24c.'}
interface['hair_v025']={'recipe':'scripts/build_v025b.py','sha256':sha(Path(__file__)),'skull_reference':{k:base['parts']['head'][k] for k in ('file','sha256','objects')},'objects_preserved':True,'topology_uv_weights_rebuilt':True}
ip=ROOT/('interfaces-'+VERSION+'.json');write(ip,interface)
base['interfaces']={'file':ip.name,'sha256':sha(ip)}
bridge=ROOT/('assembly-'+VERSION+'-interface-baseline.json');write(bridge,base)
candidate=ROOT/('assembly-'+VERSION+'-seed.json');write(candidate,base)
stats={}
for part in ('hair_front','hair_back','hair_base'):
 bpy.ops.wm.read_factory_settings(use_empty=True)
 objects=source(part);head=source('head');skin=next(o for o in head if o.get('expression_role')=='head');tree=tree_for([skin])[0]
 refs=[]
 if part=='hair_base':refs=source('hair_front')+source('hair_back');trees=tree_for(refs)
 for obj in objects:
  before=len(obj.data.vertices)
  if part=='hair_front':replace_mesh(obj,front_clumps(obj))
  elif part=='hair_back':replace_mesh(obj,[back_clump(tree,int(obj['object_id'].rsplit('_',1)[1]))])
  else:
   points=[]
   for v in obj.data.vertices:
    d=(v.co-C).normalized();a=math.atan2(d.y,d.x);theta=math.acos(max(-1,min(1,d.z)))
    theta-=.08*smooth((theta-1.45)/.40)*smooth((d.y+.12)/.30)
    p,n=envelope(tree,a,theta);target=(p-C).length
    root=smooth((p.z-1.585)/.058)
    for tr in trees:
     hit,normal,face,dist=tr.ray_cast(C+n*.4,-n,.4)
     if hit is not None:target=max(target,(hit-C).length-.0003)
    p+=n*(target-(p-C).length)*root
    points.append(p)
   for v,p in zip(obj.data.vertices,points):v.co=p
   obj.data.update()
  stats[obj['object_id']]={'before_vertices':before,'vertices':len(obj.data.vertices),'triangles':sum(len(p.vertices)-2 for p in obj.data.polygons)}
 for obj in head+refs:bpy.data.objects.remove(obj,do_unlink=True)
 work=OUT/(part+'-work.blend');save_part(work,objects)
 candidate=Path(revise(candidate,part,VERSION,'publish',source=work))
blend=OUT/('rf_c01-'+VERSION+'.blend');pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,bridge,('hair_front','hair_base','hair_back'))
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(protected_sources=protected,candidate=str(candidate),stats=stats,interface_migration=interface['migration'])
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/('rf_c01-'+VERSION+'.glb')))
print('V25 HAIR MIGRATION PASS',VERSION,flush=True)
