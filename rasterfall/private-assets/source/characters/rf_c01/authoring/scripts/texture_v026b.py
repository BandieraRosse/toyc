"""Original deterministic base-color painting, explicit UV0, embedded opaque PNG.

Texture RGB is authored in linear space, encoded once to sRGB. No normal,
roughness or opacity image is emitted. Paint marks are pigmentation/weave;
lighting remains in the renderer. All textures are generated from scratch.
"""
import bpy,math,struct,zlib
from pathlib import Path
import numpy as np

def smooth(x):
 x=np.clip(x,0,1);return x*x*(3-2*x)
def bell(x,c,w):return np.exp(-((x-c)/w)**2)
def png(path,rgb):
 rgb=np.clip(rgb,0,1)
 srgb=np.where(rgb<=.0031308,rgb*12.92,1.055*rgb**(1/2.4)-.055)
 rgba=np.concatenate((np.uint8(np.rint(srgb*255)),np.full((*rgb.shape[:2],1),255,dtype=np.uint8)),axis=2)
 # Image rows use top-left; Blender UV uses bottom-left.
 rgba=np.flipud(rgba);h,w=rgba.shape[:2]
 def chunk(kind,data):return struct.pack('>I',len(data))+kind+data+struct.pack('>I',zlib.crc32(kind+data)&0xffffffff)
 payload=b''.join(b'\x00'+row.tobytes() for row in rgba)
 path.write_bytes(b'\x89PNG\r\n\x1a\n'+chunk(b'IHDR',struct.pack('>2I5B',w,h,8,6,0,0,0))+chunk(b'sRGB',b'\x00')+chunk(b'IDAT',zlib.compress(payload,9))+chunk(b'IEND',b''))
def grid(size):
 a=(np.arange(size,dtype=np.float32)+.5)/size
 return np.meshgrid(a,a)
def create_textures(out):
 out.mkdir(exist_ok=False)
 # The face atlas is cylindrical, with the visible front centered at U=.5.
 u,v=grid(1024);rgb=np.zeros((1024,1024,3),np.float32);rgb[:]=(.68,.446,.337)
 front=bell(u,.5,.21)
 cheeks=(bell(u,.421,.041)+bell(u,.579,.041))*bell(v,.518,.053)
 ear=(bell(u,.255,.035)+bell(u,.745,.035))*bell(v,.558,.068)
 under=(bell(u,.43,.039)+bell(u,.57,.039))*bell(v,.585,.026)
 nose=bell(u,.5,.023)*bell(v,.50,.037)
 warm=np.clip(.40*cheeks+.25*ear+.14*under+.17*nose,0,1)
 rgb[...,0]+=warm*.042;rgb[...,1]-=warm*.036;rgb[...,2]-=warm*.020
 # Soft mouth color follows the lip volume; no directional light is baked.
 lip=bell(u,.5,.031)*bell(v,.424,.013)
 upper=bell(v,.429,.006);lower=bell(v,.418,.007)
 lipshade=np.stack((.575-.065*upper+.018*lower,.318-.046*upper+.018*lower,.277-.018*upper+.012*lower),axis=2)
 rgb=rgb*(1-lip[...,None]*.83)+lipshade*(lip[...,None]*.83)
 # Subtle hue variation breaks the flat mannequin color without visible pores.
 variation=.0017*np.sin(u*1193.4+v*795.7)*np.sin(v*1221.8-u*271.5)
 rgb+=variation[...,None];png(out/'rf_c01_face_basecolor.png',rgb)
 u,v=grid(512);rgb=np.zeros((512,512,3),np.float32);rgb[:]=(.66,.43,.32)
 warm=.018*bell(v,.46,.23)+.006*np.sin(u*math.tau)
 rgb[...,0]+=warm;rgb[...,1]-=warm*.15;rgb[...,2]-=warm*.1
 png(out/'rf_c01_skin_basecolor.png',rgb)
 u,v=grid(512);x=(u-.5)/.445;y=(v-.5)/.445;r=np.sqrt(x*x+y*y);a=np.arctan2(y,x)
 rgb=np.zeros((512,512,3),np.float32);rgb[:]=(.83,.81,.74)
 fibers=.5+.5*np.sin(a*53+np.sin(a*13)*1.8+r*18)
 gold=np.zeros_like(rgb);gold[:]=(.43,.238,.075)
 gold+=fibers[...,None]*np.array((.075,.051,.016),np.float32)
 gold+=bell(r,.63,.20)[...,None]*np.array((.075,.044,.016),np.float32)
 top=smooth((y+.08)/.85)*.38;gold*=1-top[...,None]
 outer=smooth((r-.80)/.18);gold=gold*(1-outer[...,None])+np.array((.030,.026,.024))*outer[...,None]
 pupil=1-smooth((r-.255)/.075);gold=gold*(1-pupil[...,None])+np.array((.012,.017,.020))*pupil[...,None]
 rgb=np.where((r<1)[...,None],gold,rgb);png(out/'rf_c01_eyes_basecolor.png',rgb)
 # Eight horizontal strand variants. A four-pixel inset keeps clamp filtering
 # inside each tile; strokes represent colored fiber bundles, not specular glints.
 u,v=grid(1024);tile=np.floor(u*8);s=u*8-tile
 broad=.5+.5*np.sin((s+.035*np.sin(v*5+tile))*math.tau*2+tile*.73)
 fibers=.5+.5*np.sin((s+.010*np.sin(v*8+tile))*math.tau*17+tile*1.11)
 fine=.5+.5*np.sin(s*math.tau*53+v*2.1+tile)
 variation=.80+.21*broad+.035*fibers+.009*fine
 variation*=.86+.16*smooth(v/.65)
 rgb=variation[...,None]*np.array((.040,.084,.102),np.float32)
 png(out/'rf_c01_hair_basecolor.png',rgb)
 # Four neutral tintable cloth/leather/gear tiles; exact material base colors
 # remain linear glTF factors, so garment palettes are still interchangeable.
 u,v=grid(1024);tx=np.floor(u*2);ty=np.floor(v*2);s=u*2-tx;t=v*2-ty
 fabric=(.5+.5*np.sin(s*math.tau*108+t*math.tau*32))*(.5+.5*np.sin(t*math.tau*112))
 weave=.952+.027*fabric
 seam=bell(s,.12,.007)+bell(s,.88,.007)
 stitch=(.5+.5*np.cos(t*math.tau*78))**12*(bell(s,.127,.003)+bell(s,.873,.003))
 weave-=seam*.085;weave+=stitch*.048
 leather=.957+.012*np.sin(s*641+t*921)*np.sin(s*137-t*277)
 gear=.962+.018*np.sin(s*math.tau*64)*np.sin(t*math.tau*64)
 value=np.where(ty<1,weave,np.where(tx<1,leather,gear))
 rgb=np.repeat(value[...,None],3,axis=2);png(out/'rf_c01_garment_basecolor.png',rgb)
 return {name:bpy.data.images.load(str(out/('rf_c01_'+name+'_basecolor.png')),check_existing=False) for name in ('face','skin','eyes','hair','garment')}
def image_for(images,name):
 image=images[name];image.colorspace_settings.name='sRGB';image.pack();return image
def material(name,role,image,factor=(1,1,1),roughness=.6,metallic=0):
 m=bpy.data.materials.new(name);m.use_nodes=True;m['export_id']=name;m['visual_role']=role
 m.diffuse_color=(*factor,1)
 n=m.node_tree.nodes;n.clear();output=n.new('ShaderNodeOutputMaterial');bsdf=n.new('ShaderNodeBsdfPrincipled')
 bsdf.inputs['Base Color'].default_value=(*factor,1);bsdf.inputs['Roughness'].default_value=roughness;bsdf.inputs['Metallic'].default_value=metallic
 if image:
  tex=n.new('ShaderNodeTexImage');tex.image=image;tex.interpolation='Linear';tex.extension='EXTEND'
  if factor==(1,1,1):m.node_tree.links.new(tex.outputs['Color'],bsdf.inputs['Base Color'])
  else:
   # Exporter recognizes this standard multiply as baseColorFactor.
   mix=n.new('ShaderNodeMix');mix.data_type='RGBA';mix.blend_type='MULTIPLY';mix.inputs[0].default_value=1;mix.inputs[7].default_value=(*factor,1)
   m.node_tree.links.new(tex.outputs['Color'],mix.inputs[6]);m.node_tree.links.new(mix.outputs[2],bsdf.inputs['Base Color'])
 m.node_tree.links.new(bsdf.outputs[0],output.inputs['Surface']);return m
def uv_write(obj,mapper):
 mesh=obj.data
 while len(mesh.uv_layers):mesh.uv_layers.remove(mesh.uv_layers[0])
 uv=mesh.uv_layers.new(name='UVMap')
 for polygon in mesh.polygons:
  coordinates=[mapper(mesh.vertices[mesh.loops[i].vertex_index].co,polygon) for i in polygon.loop_indices]
  for i,p in zip(polygon.loop_indices,coordinates):uv.data[i].uv=(max(0,min(1,p[0])),max(0,min(1,p[1])))
def cylindrical(p,axis,center,start,end):
 if axis==2:u=.5+math.atan2(p.x-center[0],-(p.y-center[1]))/math.tau;v=(p.z-start)/(end-start)
 else:u=.5+math.atan2(p.z-center[2],-(p.y-center[1]))/math.tau;v=(abs(p.x)-start)/(end-start)
 return u,max(0,min(1,v))
def fix_wrap(obj):
 mesh=obj.data;uv=mesh.uv_layers.active
 for p in mesh.polygons:
  us=[uv.data[i].uv.x for i in p.loop_indices]
  if max(us)-min(us)>.5:
   for i in p.loop_indices:
    if uv.data[i].uv.x<.25:uv.data[i].uv.x=1
