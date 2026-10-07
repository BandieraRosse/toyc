"""Normalize identical GLB texture aliases without changing image or geometry.

The source export remains untouched; a separate -runtime.glb is generated for
the narrow eight-slot RFM2 surface consumer. Identity is (source, sampler).
"""
import json,struct,hashlib,sys,copy
from pathlib import Path
ROOT=Path(__file__).resolve().parents[1]
version=sys.argv[1] if len(sys.argv)>1 else 'v026e';out=ROOT/('build/'+version)
source=out/('rf_c01-'+version+'.glb');target=out/('rf_c01-'+version+'-runtime.glb')
assert not target.exists(),target
raw=source.read_bytes();chunks=[];at=12
while at<len(raw):
 n,kind=struct.unpack_from('<II',raw,at);chunks.append((kind,raw[at+8:at+8+n]));at+=8+n
assert [k for k,b in chunks]==[0x4e4f534a,0x004e4942]
doc=json.loads(chunks[0][1]);old=copy.deepcopy(doc);slots=[];by_key={};mapping={}
for i,texture in enumerate(doc.get('textures',[])):
 assert set(texture)<=set(('source','sampler','name')),texture
 key=(texture['source'],texture.get('sampler'))
 if key not in by_key:
  by_key[key]=len(slots);slots.append(copy.deepcopy(texture))
 mapping[i]=by_key[key]
assert len(slots)<=8
for material in doc.get('materials',[]):
 assert not any(k in material for k in ('normalTexture','occlusionTexture','emissiveTexture'))
 pbr=material['pbrMetallicRoughness'];assert 'metallicRoughnessTexture' not in pbr
 if 'baseColorTexture' in pbr:pbr['baseColorTexture']['index']=mapping[pbr['baseColorTexture']['index']]
doc['textures']=slots
# Validate semantic resolution per material instead of merely counting slots.
for a,b in zip(old['materials'],doc['materials']):
 ta=a['pbrMetallicRoughness'].get('baseColorTexture');tb=b['pbrMetallicRoughness'].get('baseColorTexture')
 if ta:
  oldtex=old['textures'][ta['index']];newtex=doc['textures'][tb['index']]
  assert (oldtex['source'],oldtex.get('sampler'))==(newtex['source'],newtex.get('sampler'))
for key in old:
 if key not in ('textures','materials'):assert old[key]==doc[key],key
encoded=json.dumps(doc,ensure_ascii=True,separators=(',',':')).encode('utf-8');encoded+=b' '*((-len(encoded))%4)
body=struct.pack('<II',len(encoded),0x4e4f534a)+encoded+struct.pack('<II',len(chunks[1][1]),0x004e4942)+chunks[1][1]
target.write_bytes(b'glTF'+struct.pack('<II',2,12+len(body))+body)
digest=lambda b:hashlib.sha256(b).hexdigest()
report={'source':str(source),'source_sha256':digest(raw),'normalized':str(target),'normalized_sha256':digest(target.read_bytes()),'textures_before':len(old['textures']),'texture_slots':len(slots),'images':len(doc['images']),'mapping':mapping,'key':'source image + sampler','binary_unchanged_sha256':digest(chunks[1][1]),'material_resolution_unchanged':True,'all_other_json_fields_unchanged':True}
(out/'texture-normalization.json').write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
print(json.dumps(report,indent=2))
