"""Generate the deterministic RFCHAR V1 reference character.

Run: blender --background --factory-startup --python this_file -- --output path.glb
"""
import argparse, sys, bpy, json, struct
from pathlib import Path
from mathutils import Vector

def args():
    p=argparse.ArgumentParser();p.add_argument("--output",required=True)
    return p.parse_args(sys.argv[sys.argv.index("--")+1:] if "--" in sys.argv else [])

def main():
    a=args();bpy.ops.object.select_all(action='SELECT');bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene;scene.unit_settings.system='METRIC';scene.unit_settings.scale_length=1.0
    bpy.ops.object.armature_add(enter_editmode=True,location=(0,0,0));arm=bpy.context.object;arm.name="RFCHAR_Armature"
    eb=arm.data.edit_bones;eb.remove(eb[0]); bones={}
    specs=[
      ("RF_ROOT",None,(0,0,0),(0,0,.10)),("RF_HIPS","RF_ROOT",(0,0,.90),(0,0,1.05)),
      ("RF_SPINE","RF_HIPS",(0,0,1.05),(0,0,1.25)),("RF_CHEST","RF_SPINE",(0,0,1.25),(0,0,1.43)),
      ("RF_UPPER_CHEST","RF_CHEST",(0,0,1.43),(0,0,1.52)),("RF_NECK","RF_UPPER_CHEST",(0,0,1.52),(0,0,1.62)),
      ("RF_HEAD","RF_NECK",(0,0,1.62),(0,0,1.82)),
      ("RF_L_SHOULDER","RF_UPPER_CHEST",(.10,0,1.50),(.22,0,1.50)),("RF_L_UPPER_ARM","RF_L_SHOULDER",(.22,0,1.50),(.52,0,1.50)),
      ("RF_L_FOREARM","RF_L_UPPER_ARM",(.52,0,1.50),(.78,0,1.50)),("RF_L_HAND","RF_L_FOREARM",(.78,0,1.50),(.92,0,1.50)),
      ("RF_R_SHOULDER","RF_UPPER_CHEST",(-.10,0,1.50),(-.22,0,1.50)),("RF_R_UPPER_ARM","RF_R_SHOULDER",(-.22,0,1.50),(-.52,0,1.50)),
      ("RF_R_FOREARM","RF_R_UPPER_ARM",(-.52,0,1.50),(-.78,0,1.50)),("RF_R_HAND","RF_R_FOREARM",(-.78,0,1.50),(-.92,0,1.50)),
      ("RF_L_UPPER_LEG","RF_HIPS",(.11,0,.90),(.11,0,.52)),("RF_L_LOWER_LEG","RF_L_UPPER_LEG",(.11,0,.52),(.11,0,.12)),
      ("RF_L_FOOT","RF_L_LOWER_LEG",(.11,0,.12),(.11,-.20,.08)),("RF_R_UPPER_LEG","RF_HIPS",(-.11,0,.90),(-.11,0,.52)),
      ("RF_R_LOWER_LEG","RF_R_UPPER_LEG",(-.11,0,.52),(-.11,0,.12)),("RF_R_FOOT","RF_R_LOWER_LEG",(-.11,0,.12),(-.11,-.20,.08))]
    for name,parent,head,tail in specs:
        b=eb.new(name);b.head=head;b.tail=tail;b.use_deform=True;bones[name]=b
        if parent:b.parent=bones[parent]
    for name,parent,head,tail in [("RF_ATTACH_WEAPON_R","RF_R_HAND",(-.88,-.03,1.48),(-.88,-.03,1.58)),
                                  ("RF_ATTACH_BACK","RF_CHEST",(0,.10,1.40),(0,.10,1.50))]:
        b=eb.new(name);b.head=head;b.tail=tail;b.parent=bones[parent];b.use_deform=False
    bpy.ops.object.mode_set(mode='OBJECT')
    red=bpy.data.materials.new("BodyRed");red.diffuse_color=(.55,.08,.06,1)
    blue=bpy.data.materials.new("BootBlue");blue.diffuse_color=(.04,.12,.55,1)
    def box(name,lo,hi,material,groups):
        x0,y0,z0=lo;x1,y1,z1=hi
        vs=[(x0,y0,z0),(x1,y0,z0),(x1,y1,z0),(x0,y1,z0),(x0,y0,z1),(x1,y0,z1),(x1,y1,z1),(x0,y1,z1)]
        fs=[(0,2,1),(0,3,2),(4,5,6),(4,6,7),(0,1,5),(0,5,4),(1,2,6),(1,6,5),(2,3,7),(2,7,6),(3,0,4),(3,4,7)]
        me=bpy.data.meshes.new(name);me.from_pydata(vs,[],fs);me.materials.append(material)
        uv=me.uv_layers.new(name="UVMap");
        for loop in me.loops: uv.data[loop.index].uv=((loop.vertex_index&1),((loop.vertex_index>>1)&1))
        ob=bpy.data.objects.new(name,me);scene.collection.objects.link(ob);ob.data.update()
        ob.parent=arm
        mod=ob.modifiers.new("RFCHAR Skin","ARMATURE");mod.object=arm
        for bone,indices,weight in groups:
            ob.vertex_groups.new(name=bone).add(indices,weight,'REPLACE')
        return ob
    body=box("BodyMesh",(-.20,-.10,.82),(.20,.10,1.48),red,[("RF_HIPS",[0,1,2,3],1),
             ("RF_HIPS",[4,5,6,7],.5),("RF_CHEST",[4,5,6,7],.5)])
    limb=box("RightArmMesh",(-.80,-.055,1.445),(-.22,.055,1.555),blue,
             [("RF_R_UPPER_ARM",[1,2,5,6],1),("RF_R_FOREARM",[0,3,4,7],.5),("RF_R_UPPER_ARM",[0,3,4,7],.5)])
    leg=box("LeftLegMesh",(.05,-.07,.10),(.17,.07,.90),blue,
            [("RF_L_LOWER_LEG",[0,1,2,3],1),("RF_L_UPPER_LEG",[4,5,6,7],1)])
    for ob in (arm,body,limb,leg):ob.select_set(True)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.export_scene.gltf(filepath=a.output,export_format='GLB',use_selection=True,
        export_skins=True,export_influence_nb=4,export_all_influences=True,export_morph=False,export_animations=False,
        export_yup=True,export_apply=False,export_armature_object_remove=True)
    # Blender omits glTF skin.skeleton when the common ancestor is obvious.
    # RFCHAR makes it explicit, so normalize this one exporter ambiguity.
    path=Path(a.output);raw=path.read_bytes();jn,jk=struct.unpack_from('<II',raw,12)
    doc=json.loads(raw[20:20+jn].rstrip(b' \0'));root=next(i for i,n in enumerate(doc['nodes']) if n.get('name')=='RF_ROOT')
    doc['skins'][0]['skeleton']=root;encoded=json.dumps(doc,separators=(',',':')).encode();encoded+=b' '*(-len(encoded)%4)
    bin_at=20+jn;bn,bk=struct.unpack_from('<II',raw,bin_at);blob=bytearray(raw[bin_at+8:bin_at+8+bn])
    # Preserve a known BDEF2 sample even on Blender builds configured to
    # export only the strongest influence.
    prim=doc['meshes'][1]['primitives'][0];ja=doc['accessors'][prim['attributes']['JOINTS_0']];wa=doc['accessors'][prim['attributes']['WEIGHTS_0']]
    jv=doc['bufferViews'][ja['bufferView']];wv=doc['bufferViews'][wa['bufferView']]
    j0=doc['skins'][0]['joints'].index(next(i for i,n in enumerate(doc['nodes']) if n.get('name')=='RF_R_UPPER_ARM'))
    j1=doc['skins'][0]['joints'].index(next(i for i,n in enumerate(doc['nodes']) if n.get('name')=='RF_R_FOREARM'))
    jf='B' if ja['componentType']==5121 else 'H';js=struct.calcsize(jf)*4;ws=16
    for i in range(min(3,ja['count'])):
        jo=jv.get('byteOffset',0)+ja.get('byteOffset',0)+i*jv.get('byteStride',js);wo=wv.get('byteOffset',0)+wa.get('byteOffset',0)+i*wv.get('byteStride',ws)
        struct.pack_into('<'+jf*4,blob,jo,j0,j1,0,0);struct.pack_into('<4f',blob,wo,.5,.5,0,0)
    total=12+8+len(encoded)+8+len(blob);path.write_bytes(b'glTF'+struct.pack('<II',2,total)+struct.pack('<II',len(encoded),jk)+encoded+struct.pack('<II',len(blob),bk)+blob)
    print("rfchar-fixture:",a.output)
if __name__=="__main__":main()
