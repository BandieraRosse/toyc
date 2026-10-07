"""RF-C01 V23: frozen V22h assembly, body-only garment and silhouette revision.

No historical generator is executed. All source files remain immutable. Existing
UV islands/weights survive; added opaque details use the same five body materials.
Run with Blender background; the review/export phase is a separate operation.
"""
import bpy, bmesh, math, sys
from pathlib import Path
from mathutils import Vector
from mathutils.bvhtree import BVHTree
from mathutils.kdtree import KDTree

ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha
from rf_parts.revisions import revise
from rf_parts import pipeline

OUT=ROOT/'build/v023'
OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-hair_base-v022h.json'
manifest=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in manifest['parts'].values()}
protected[str(BASE)]=sha(BASE)
bpy.ops.wm.open_mainfile(filepath=str(ROOT/'build/v022h/rf_c01-v022h.blend'))
bpy.context.preferences.filepaths.save_version=0
body=next(o for o in bpy.context.scene.objects if o.get('part_id')=='body')
mesh=body.data
before_vertices=len(mesh.vertices)
before_triangles=sum(len(p.vertices)-2 for p in mesh.polygons)

adj=[[] for v in mesh.vertices]
for e in mesh.edges:
    a,b=e.vertices;adj[a].append(b);adj[b].append(a)
seen=set();components=[]
for v in mesh.vertices:
    if v.index in seen:continue
    stack=[v.index];seen.add(v.index);indices=[]
    while stack:
        i=stack.pop();indices.append(i)
        for j in adj[i]:
            if j not in seen:seen.add(j);stack.append(j)
    components.append(indices)
assert len(components)==43 and before_vertices==13501,'Unexpected frozen body topology'

def smooth(t):
    t=max(0,min(1,t));return t*t*(3-2*t)
def bell(t,center,width):return math.exp(-((t-center)/width)**2)
def bodyshape(p):
    x,y,z=p;a=abs(x)
    # Relax the straight waist; a continuous small fabric volume at hem and ribs.
    if a<.16:
        x*=1+.025*bell(z,1.09,.075)
        y=.008+(y-.008)*(1+.035*bell(z,1.19,.11))
        y+=.0015*math.sin((z-1.01)*71+abs(x)*23)*bell(z,1.04,.045)
    if a>.145:
        # Rounded sleeve cap and a shaped, slightly cinched rolled edge.
        lift=.007*bell(a,.206,.055)
        z=1.294+(z-1.294)*(1+.08*bell(a,.22,.06))
        z+=lift*smooth((z-1.294)/.025)
        y=.008+(y-.008)*(1-.06*bell(a,.38,.026))
        fold=.0018*math.sin((a-.31)*115)*bell(a,.36,.028)
        y+=fold*(1 if y>.008 else -1)
    return Vector((x,y,z))

for i in components[10]:mesh.vertices[i].co=bodyshape(mesh.vertices[i].co)
for ci in (24,25):
    for i in components[ci]:mesh.vertices[i].co=bodyshape(mesh.vertices[i].co)
for i in components[26]:
    p=mesh.vertices[i].co;x,y,z=p;s=1 if x>=0 else -1;cx=s*.09
    if z<.82:
        angle=math.atan2(y-.006,x-cx)
        r=.0028*math.sin((z-.47)*115+1.5*math.cos(angle))*bell(z,.47,.055)
        r+=.0022*math.sin((z-.253)*150+math.cos(angle))*bell(z,.256,.03)
        r+=.0015*math.sin((z-.756)*100+math.cos(angle))*bell(z,.768,.055)
        p.x+=r*math.cos(angle);p.y+=r*math.sin(angle)
        # A restrained outer thigh/calf taper, keeping joint centers unchanged.
        p.x=cx+(p.x-cx)*(1+.035*bell(z,.70,.10))
    if z>.84:p.y+=.0018*math.sin(abs(x)*70)*bell(z,.91,.065)
for ci in (28,31):
    for i in components[ci]:
        p=mesh.vertices[i].co;a=abs(p.x)
        p.y=.008+(p.y-.008)*(1+.055*bell(a,.446,.035)-.045*bell(a,.55,.025))
        p.z=1.294+(p.z-1.294)*(1+.045*bell(a,.46,.055))
for ci in (27,30):
    for i in components[ci]:
        p=mesh.vertices[i].co;s=1 if p.x>=0 else -1
        toe=smooth((-p.y-.064)/.07)
        # The upper toe transitions into a flatter toe box, rather than a dome.
        p.z-=.008*toe*bell(p.z,.085,.047)
        p.x=s*.09+(p.x-s*.09)*(1-.07*toe)
for ci in (29,32):
    for i in components[ci]:
        p=mesh.vertices[i].co
        p.z+=.0018*bell(abs(p.x),.642,.018)*smooth((p.z-1.294)/.015)
for ci in range(33,43):
    pts=[mesh.vertices[i].co.copy() for i in components[ci]]
    center=sum(pts,Vector())/len(pts)
    for i in components[ci]:
        p=mesh.vertices[i].co
        taper=1-.10*smooth((abs(p.x)-.678)/.029)
        p.y=center.y+(p.y-center.y)*taper
        p.z=center.z+(p.z-center.z)*taper

# Retire the two dense rectangular knee slabs and the ten pre-existing loose
# vertices. Preserve all unrelated data through BMesh instead of remapping UVs.
remove=set(components[2]+components[6])
remove.update(i for c in components if len(c)==1 for i in c)
bm=bmesh.new();bm.from_mesh(mesh);bm.verts.ensure_lookup_table()
bmesh.ops.delete(bm,geom=[bm.verts[i] for i in sorted(remove)],context='VERTS')
bm.to_mesh(mesh);bm.free();mesh.update()
surface=BVHTree.FromPolygons([v.co for v in mesh.vertices],[list(p.vertices) for p in mesh.polygons])
kd=KDTree(len(mesh.vertices))
for v in mesh.vertices:kd.insert(v.co,v.index)
kd.balance()
original_weights={v.index:[(body.vertex_groups[g.group].name,g.weight) for g in v.groups if g.weight>1e-6] for v in mesh.vertices}
material={}
for name in ('RF_Shirt','RF_Pants','RF_Boots','RF_Yoke','RF_ID'):
    material[name]=next(m for m in mesh.materials if name in m.name)
new=[];details=[]

def nearest_weights(p):return original_weights[kd.find(p)[1]]
def add(name,verts,faces,mat,weights=None,smooth_faces=True):
    data=bpy.data.meshes.new('V23 / '+name);data.from_pydata(verts,[],faces);data.materials.append(material[mat]);data.update()
    uv=data.uv_layers.new(name='UVMap')
    for p in data.polygons:
        p.use_smooth=smooth_faces
        for li in p.loop_indices:
            co=data.vertices[data.loops[li].vertex_index].co
            uv.data[li].uv=(co.x*.5+.5,co.z*.5)
    obj=bpy.data.objects.new('V23 / '+name,data);bpy.context.scene.collection.objects.link(obj)
    for i,v in enumerate(data.vertices):
        groups=weights(v.co) if weights else nearest_weights(v.co)
        for bone,w in groups:
            if w>1e-6:
                vg=obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)
                vg.add([i],w,'REPLACE')
    new.append(obj);details.append(dict(name=name,vertices=len(verts),triangles=sum(len(f)-2 for f in faces)))
    return obj

def front(x,z):
    hit,n,_,_=surface.ray_cast(Vector((x,-1,z)),Vector((0,1,0)),2)
    if hit is None:raise ValueError(('No front garment surface',x,z))
    return hit.y

def ribbon(name,points,width,mat='RF_Pants',offset=.0013):
    # Front-facing seam with a small bevel, sampled on the already edited body.
    vs=[];fs=[]
    for i,(x,z) in enumerate(points):
        a=Vector(points[max(0,i-1)]);b=Vector(points[min(len(points)-1,i+1)])
        d=(b-a).normalized();normal=Vector((-d.y,d.x))*width*.5
        for t in (-1,0,1):
            px=x+normal.x*t;pz=z+normal.y*t
            vs.append((px,front(px,pz)-offset*(1 if t else 1.45),pz))
        if i:
            for j in range(2):
                a=(i-1)*3+j;fs.append((a,a+1,a+4,a+3))
    return add(name,vs,fs,mat)

def box(name,center,size,mat,bone,bevel=.002):
    bpy.ops.mesh.primitive_cube_add(size=1,location=center)
    obj=bpy.context.object;obj.name='V23 / '+name;obj.dimensions=size
    bpy.ops.object.transform_apply(location=False,rotation=False,scale=True)
    mod=obj.modifiers.new('Soft garment edges','BEVEL');mod.width=bevel;mod.segments=1
    bpy.ops.object.modifier_apply(modifier=mod.name)
    bpy.ops.object.transform_apply(location=True,rotation=False,scale=False)
    obj.data.materials.append(material[mat]);group=obj.vertex_groups.new(name=bone)
    group.add(list(range(len(obj.data.vertices))),1,'REPLACE')
    new.append(obj);details.append(dict(name=name,vertices=len(obj.data.vertices),triangles=sum(len(p.vertices)-2 for p in obj.data.polygons)))
    return obj

def ring_band(name,axis,center,radii,length,mat,weights,sides=20):
    vs=[];fs=[]
    for t,scale in [(-.5,.97),(-.30,1.02),(.30,1.02),(.5,.97)]:
        for j in range(sides):
            a=math.tau*j/sides;p=Vector(center)
            p[axis]+=length*t
            p[(axis+1)%3]+=radii[0]*math.cos(a)*scale
            p[(axis+2)%3]+=radii[1]*math.sin(a)*scale
            vs.append(p)
    for r in range(3):
        for j in range(sides):
            a=r*sides+j;b=r*sides+(j+1)%sides;fs.append((a,b,b+sides,a+sides))
    return add(name,vs,fs,mat,weights)

for s,side in ((1,'L'),(-1,'R')):
    base='RF_'+side+'_'
    # Curved hexagonal knee shield, narrow at each end, with an inset face.
    contour=[(-.018,-.049),(.018,-.049),(.035,-.030),(.035,.029),(.020,.049),(-.020,.049),(-.035,.029),(-.035,-.030)]
    vs=[];fs=[]
    for scale,depth in [(1,.0015),(.88,.0048),(.68,.0055)]:
        for dx,dz in contour:
            x=s*.09+dx*scale;z=.491+dz*scale
            vs.append((x,front(x,z)-depth,z))
    for r in range(2):
        for j in range(8):
            a=r*8+j;b=r*8+(j+1)%8;fs.append((a,b,b+8,a+8))
    vs.append((s*.09,front(s*.09,.491)-.0058,.491))
    for j in range(8):fs.append((16+j,16+(j+1)%8,24))
    add(side+' shaped knee shield',vs,fs,'RF_Shirt')
    ribbon(side+' knee articulation',[(s*.09+u,.478+.005*abs(u)/.025) for u in (-.025,-.0125,0,.0125,.025)],.0022,'RF_Pants',.006)

    # Tailored front seams and a low-profile chest pocket below the pale yoke.
    ribbon(side+' jacket side seam',[(s*(.092-.012*math.sin(t*math.pi)),1.045+.172*t) for t in [i/10 for i in range(11)]],.0024)
    ribbon(side+' chest pocket welt',[(s*(.042+.075*t),1.219-.017*t) for t in [i/8 for i in range(9)]],.006)
    ribbon(side+' hip pocket opening',[(s*(.07+.068*t),.916-.044*t) for t in [i/8 for i in range(9)]],.004,'RF_Boots')
    # The old utility pocket remains its inner body; add a flap and seam.
    box(side+' cargo flap',(s*.157,.014,.806),(.007,.078,.023),'RF_Pants',base+'UPPER_LEG',.003)
    box(side+' cargo tab',(s*.162,-.002,.785),(.005,.014,.026),'RF_Yoke',base+'UPPER_LEG',.002)
    box(side+' belt loop',(s*.073,-.064,.990),(.012,.009,.032),'RF_Pants','RF_HIPS',.002)
    ring_band(side+' sleeve hem',0,(s*.389,.008,1.294),(.0405,.0387),.020,'RF_Pants',nearest_weights)
    ring_band(side+' glove cuff',0,(s*.585,.006,1.294),(.0257,.0208),.014,'RF_Shirt',lambda p,b=base:[(b+'HAND',1)])
    # Fine knuckle ridges are anchored to the hand, not the finger joints.
    for i,y in enumerate((-.017,-.002,.013,.026)):
        box(side+' knuckle '+str(i),(s*.644,y,1.311),(.014,.010,.004),'RF_Pants',base+'HAND',.0015)
    ring_band(side+' boot collar',2,(s*.09,.005,.217),(.041,.044),.019,'RF_Pants',nearest_weights)
    # Toe and ankle panels follow the same foot/lower-leg binding as the boot.
    ribbon(side+' boot toe seam',[(s*.09+u,.061+.003*(1-(u/.039)**2)) for u in [(-.039+.078*i/12) for i in range(13)]],.002,'RF_Pants',.0014)
    for z in (.122,.143,.164):
        ribbon(side+' boot instep '+str(z),[(s*.09+u,z) for u in (-.020,-.010,0,.010,.020)],.003,'RF_Pants',.0015)

# Soft shoulder seam follows the edited sleeve; material boundaries remain
# opaque, so no new textures, shaders, draw primitives or alpha passes are needed.
for s,side in ((1,'L'),(-1,'R')):
    vs=[];fs=[];x=s*.233
    for r,dx in enumerate((-.0012,0,.0012)):
        for j in range(17):
            a=math.tau*j/16;p=bodyshape(Vector((x+dx,.008+.0565*math.cos(a),1.294+.0515*math.sin(a))))
            p.y=.008+(p.y-.008)*1.012;p.z=1.294+(p.z-1.294)*1.012;vs.append(p)
    for r in range(2):
        for j in range(16):a=r*17+j;fs.append((a,a+1,a+18,a+17))
    add(side+' shoulder seam',vs,fs,'RF_Pants')

bpy.ops.object.select_all(action='DESELECT');body.select_set(True)
for obj in new:obj.select_set(True)
bpy.context.view_layer.objects.active=body
bpy.ops.object.join()
bm=bmesh.new();bm.from_mesh(body.data);bmesh.ops.recalc_face_normals(bm,faces=bm.faces);bm.to_mesh(body.data);bm.free()
body.data.update()
after_vertices=len(body.data.vertices);after_triangles=sum(len(p.vertices)-2 for p in body.data.polygons)
assert after_triangles<before_triangles,(before_triangles,after_triangles)
work=OUT/'body-work.blend'
bpy.ops.wm.save_as_mainfile(filepath=str(work))
candidate=Path(revise(BASE,'body','v023','publish',source=work))
blend=OUT/'rf_c01-v023.blend'
pipeline.assemble(candidate,blend)
report=pipeline.verify(blend,candidate,BASE,('body',))
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(before_body_vertices=before_vertices,after_body_vertices=after_vertices,
    before_body_triangles=before_triangles,after_body_triangles=after_triangles,
    added_details=details,protected_sources=protected,candidate=str(candidate),
    head_hair_eyes_mouth_unchanged=True,material_count_unchanged=True)
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blend,OUT/'rf_c01-v023.glb'))
print('V23 BODY BUILD PASS',before_triangles,after_triangles,flush=True)
