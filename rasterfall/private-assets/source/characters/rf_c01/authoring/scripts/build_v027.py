"""Replace intersecting eye layers with one opaque textured eyeball surface."""
import bpy, sys, json, struct, copy
from pathlib import Path
import numpy as np
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/blender/rf_parts').exists())
sys.path.insert(0,str(REPO/'tools/blender'))
from rf_parts.storage import read,write,sha,load_objects,save_part,part_record
from rf_parts import pipeline
sys.path.insert(0,str(ROOT/'scripts'))
from texture_v026b import material, png
VERSION='v027'; OUT=ROOT/'build'/VERSION; OUT.mkdir(exist_ok=False)
BASE=ROOT/'assembly-eyes-v026f.json'; manifest=read(BASE)
protected={str(ROOT/e['file']):sha(ROOT/e['file']) for e in manifest['parts'].values()}
bpy.ops.wm.read_factory_settings(use_empty=True)
entry=manifest['parts']['eyes']; objects=load_objects(ROOT/entry['file'],entry['objects'])
iris=next(o for o in objects if 'iris_shaded' in o['object_id'])
source_image=next(n.image for n in iris.data.materials[0].node_tree.nodes if n.type=='TEX_IMAGE')
source=np.array(source_image.pixels[:],dtype=np.float32).reshape(source_image.size[1],source_image.size[0],4)[...,:3]
# The atlas includes both sclera and iris. Its iris projection matches the
# previous footprint, but every pixel now belongs to the SAME depth surface.
n=1024
u,v=np.meshgrid((np.arange(n)+.5)/n,(np.arange(n)+.5)/n)
x=(u-.5)*.052; z=(v-.5)*.040
iu=x/.022404613+.5; iv=z/.02486360+.5
sx=np.clip(iu*(source.shape[1]-1),0,source.shape[1]-1)
sy=np.clip(iv*(source.shape[0]-1),0,source.shape[0]-1)
ix=sx.astype(int); iy=sy.astype(int); fx=(sx-ix)[...,None]; fy=(sy-iy)[...,None]
jx=np.minimum(ix+1,source.shape[1]-1); jy=np.minimum(iy+1,source.shape[0]-1)
color=(source[iy,ix]*(1-fx)+source[iy,jx]*fx)*(1-fy)+(source[jy,ix]*(1-fx)+source[jy,jx]*fx)*fy
r=np.sqrt(((iu-.5)*2)**2+((iv-.5)*2)**2)
white=np.zeros((n,n,3),dtype=np.float32)+(.87,.90,.88)
blend=np.clip((1-r)/.016,0,1)[...,None]
rgb=white*(1-blend)+color*blend
# One small authored catchlight in the same pigment surface; no floating mesh.
spot=np.clip((1-((x+.003)/.0013)**2-((z-.0033)/.0014)**2)*5,0,1)[...,None]
rgb=rgb*(1-spot)+np.array((.98,.99,1.0))*spot
textures=OUT/'textures';textures.mkdir()
png(textures/'rf_c01_eye_surface.png',rgb)
image=bpy.data.images.load(str(textures/'rf_c01_eye_surface.png'));image.colorspace_settings.name='sRGB';image.pack()
mat=material('RF_C01_EyeSurface','eyes',image,roughness=.42)
removed=[]; unchanged={}; eyeballs=[]
for obj in list(objects):
    oid=obj['object_id']
    if 'iris_shaded' in oid or 'catchlight' in oid:
        removed.append(oid);objects.remove(obj);bpy.data.objects.remove(obj,do_unlink=True);continue
    if 'eyeball' not in oid:
        unchanged[oid]=entry['records'][oid];continue
    # Keep the exact eyeball shape/weights/expressions; only surface/UV changes.
    cx=sum((min(v.co.x for v in obj.data.vertices),max(v.co.x for v in obj.data.vertices)))/2
    cz=1.525
    obj.data.materials.clear();obj.data.materials.append(mat)
    uv=obj.data.uv_layers.active or obj.data.uv_layers.new(name='UVMap')
    for poly in obj.data.polygons:
        front=sum(obj.data.vertices[i].co.y for i in poly.vertices)/len(poly.vertices)<-.038
        for li in poly.loop_indices:
            co=obj.data.vertices[obj.data.loops[li].vertex_index].co
            uv.data[li].uv=(max(0,min(1,(co.x-cx)/.052+.5)),max(0,min(1,(co.z-cz)/.040+.5))) if front else (.005,.005)
    eyeballs.append(oid)
for obj in objects:obj['part_revision']=VERSION
records=part_record(objects)
for oid,before in unchanged.items(): assert records[oid]==before,oid
output=ROOT/'parts/eyes'/f'{VERSION}.blend';save_part(output,objects)
names=[o.name for o in objects]
bpy.ops.wm.read_factory_settings(use_empty=True)
assert part_record(load_objects(output,names))==records
manifest['parts']['eyes'].update(file=f'parts/eyes/{VERSION}.blend',sha256=sha(output),revision=VERSION,objects=names,records=records,
    provenance={'operation':'explicit-eye-surface-inventory-migration','previous_sha256':entry['sha256'],'removed_objects':removed,'replacement_surfaces':eyeballs,'script_sha256':sha(Path(__file__))})
candidate=ROOT/f'assembly-eyes-{VERSION}.json';write(candidate,manifest)
blendfile=OUT/f'rf_c01-{VERSION}.blend';pipeline.assemble(candidate,blendfile)
report=pipeline.verify(blendfile,candidate,BASE,('eyes',))
assert all(sha(Path(p))==h for p,h in protected.items())
report.update(removed_overlapping_layers=removed,single_eye_surfaces=eyeballs,eyeball_geometry_unchanged=True)
write(OUT/'validation.json',report)
write(OUT/'export-validation.json',pipeline.export(blendfile,OUT/f'rf_c01-{VERSION}.glb'))
raw=(OUT/f'rf_c01-{VERSION}.glb').read_bytes();doc=json.loads(raw[20:20+struct.unpack_from('<I',raw,12)[0]])
roles={k.split('.')[0]:v for k,v in read(ROOT/'build/v026f/visual-roles.json').items()};roles['RF_C01_EyeSurface']='eyes'
write(OUT/'visual-roles.json',{m['name']:roles[m['name'].split('.')[0]] for m in doc['materials']})
write(OUT/f'rf_c01_{VERSION}.asset.json',{'schema':1,'id':f'rf_c01_{VERSION}','type':'character','source':f'rf_c01-{VERSION}-runtime.glb'})
print('SINGLE EYE SURFACE PASS',report)
