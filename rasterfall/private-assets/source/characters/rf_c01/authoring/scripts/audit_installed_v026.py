"""Exact imported material/role/surface and companion-texture audit."""
import sys,json,struct,hashlib,subprocess
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
REPO=next(p for p in ROOT.parents if (p/'tools/assets/rfchar_import.py').exists())
version=sys.argv[1] if len(sys.argv)>1 else 'v026e';out=ROOT/('build/'+version);asset='rf_c01_'+version
mesh=REPO/('rasterfall/private-assets/models/'+asset+'.rmesh');textures=mesh.with_suffix('.textures')
raw=mesh.read_bytes();assert raw[:4]==b'RFM2'
version_number,nv,ni,scale=struct.unpack_from('<4I',raw,4);assert version_number==15 and scale==65536
primitive_count,material_count,primitive_offset,material_offset,skin_offset=struct.unpack_from('<5I',raw,44)
glb=out/('rf_c01-'+version+'-runtime.glb');g=glb.read_bytes();doc=json.loads(g[20:20+struct.unpack_from('<I',g,12)[0]])
assert material_count==len(doc['materials'])
roles=json.loads((out/'visual-roles.json').read_text(encoding='utf-8'));codes={'none':0,'face':1,'eyes':2,'hair':3,'skin':4,'clothing':5,'equipment':6}
audit=[];used=set()
for i,mat in enumerate(doc['materials']):
 offset=material_offset+i*40;slot=struct.unpack_from('<I',raw,offset+8)[0]
 assert raw[offset+36]==codes[roles[mat['name']]],mat['name']
 expected=mat['pbrMetallicRoughness'].get('baseColorTexture',{}).get('index',0xffffffff)
 assert slot==expected and (slot==0xffffffff or slot<8),(mat['name'],slot,expected)
 if slot!=0xffffffff:used.add(slot)
 audit.append({'name':mat['name'],'role':roles[mat['name']],'texture_slot':None if slot==0xffffffff else slot})
skn=struct.unpack_from('<8I',raw,skin_offset);assert skn[0]==0x314e4b53 and skn[2]==49
chr_offset=skin_offset+skn[1];chrh=struct.unpack_from('<8I',raw,chr_offset);assert chrh[0]==0x31524843 and chrh[4]==8
mat_offset=chr_offset+chrh[1];mh=struct.unpack_from('<8I',raw,mat_offset);assert mh[0]==0x3154414d and mh[3]==material_count
for i,mat in enumerate(doc['materials']):
 flags,rough,metal,reserved=struct.unpack_from('<IffI',raw,mat_offset+32+i*16);pbr=mat['pbrMetallicRoughness']
 assert flags==1 and reserved==0 and abs(rough-pbr.get('roughnessFactor',1))<1e-6 and abs(metal-pbr.get('metallicFactor',1))<1e-6
 audit[i].update(roughness=rough,metallic=metal)
digest=lambda p:hashlib.sha256(p.read_bytes()).hexdigest()
texture_rows=[]
for index in sorted(used):
 p=textures/('texture_%03d.ttex'%index);assert p.is_file();run=subprocess.run([str(REPO/'build-windows/toyasset.exe'),'validate',str(p)],capture_output=True,text=True,encoding='utf-8',errors='replace');assert run.returncode==0,run.stdout+run.stderr
 texture_rows.append({'slot':index,'file':str(p),'sha256':digest(p),'bytes':p.stat().st_size})
report={'mesh':str(mesh),'sha256':digest(mesh),'format_version':version_number,'vertices':nv,'triangles':ni//3,'bones':skn[2],'attachments':chrh[4],'material_count':material_count,'surface_audit':audit,'textures':texture_rows,'source':str(glb),'source_sha256':digest(glb),'scope':'Installed RMESH/TTEX exact source-material mapping and toyasset validation. Does not claim GPU acceptance.'}
(out/'installed-validation.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print('V26 INSTALLED MATERIAL/TEXTURE AUDIT PASS',asset,nv,ni//3,len(used),digest(mesh))
