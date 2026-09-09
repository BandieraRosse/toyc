"""Generate the Rasterfall RF Humanoid Art Acceptance Character V1.

This is deliberately a source generator rather than a hand-edited blend file.  It
creates the canonical RFCHAR V1 armature, a small set of low-poly anatomical
meshes, and the stable attachment bones.  Source space is Blender metric, Z-up,
and -Y forward; the glTF exporter performs the standard Y-up conversion.

Run:
  blender --background --factory-startup --python this_file -- \
    --output tmp/rf_humanoid_acceptance.glb
"""
import argparse, json, struct, sys, site
from pathlib import Path
# Blender's embedded Python intentionally disables the user site directory.
# Enable it when present so a normal user-level numpy install is reusable by
# the stock glTF exporter; this is a no-op on machines with system numpy.
try:
    site.addsitedir(site.getusersitepackages())
except (AttributeError, OSError):
    pass
import bpy
from mathutils import Vector


def arguments():
    p = argparse.ArgumentParser()
    p.add_argument("--output", required=True)
    return p.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def ring_mesh(name, rings, sides, material, weights, arm, scene, front_cut=False):
    """Make a capped ring mesh. rings are (center, basis_a, basis_b, radius_a, radius_b)."""
    verts = []
    for center, ba, bb, ra, rb in rings:
        for s in range(sides):
            a = 6.28318530718 * s / sides
            verts.append(tuple(Vector(center) + Vector(ba) * (ra * __import__('math').cos(a)) +
                               Vector(bb) * (rb * __import__('math').sin(a))))
    faces = []
    for r in range(len(rings) - 1):
        for s in range(sides):
            n = (s + 1) % sides
            a, b = r * sides + s, r * sides + n
            c, d = (r + 1) * sides + n, (r + 1) * sides + s
            for face in ((a, b, c), (a, c, d)):
                if not front_cut or sum(verts[i][1] for i in face) / 3.0 >= -0.015:
                    faces.append(face)
    if not front_cut:
        faces.append(tuple(range(sides - 1, -1, -1)))
    last = (len(rings) - 1) * sides
    faces.append(tuple(last + s for s in range(sides)))
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(verts, [], faces); mesh.update()
    mesh.materials.append(material)
    ob = bpy.data.objects.new(name, mesh); scene.collection.objects.link(ob)
    ob.parent = arm
    mod = ob.modifiers.new("RFCHAR Skin", "ARMATURE"); mod.object = arm
    for bone, indices, value in weights:
        ob.vertex_groups.new(name=bone).add(indices, value, 'REPLACE')
    return ob


def segment(name, p0, p1, r0, r1, material, bone_a, bone_b, arm, scene, sides=8):
    p0, p1 = Vector(p0), Vector(p1); axis = (p1 - p0).normalized()
    helper = Vector((0, 1, 0)) if abs(axis.y) < .8 else Vector((0, 0, 1))
    ba = axis.cross(helper).normalized(); bb = axis.cross(ba).normalized()
    ts = (0.0, .20, .80, 1.0)
    rings = [(p0.lerp(p1, t), ba, bb, r0 + (r1-r0)*t, r0 + (r1-r0)*t) for t in ts]
    indices_a, indices_b = [], []
    for ri, t in enumerate(ts):
        for s in range(sides):
            i = ri * sides + s
            # A broad two-bone transition at the joint keeps the low-poly silhouette.
            b = max(0.0, min(1.0, (t - .45) / .30))
            if b <= .001: indices_a.append(i)
            elif b >= .999: indices_b.append(i)
            else:
                indices_a.append(i); indices_b.append(i)
    if bone_a == bone_b:
        return ring_mesh(name, rings, sides, material,
                         [(bone_a, range(len(rings) * sides), 1.0)], arm, scene)
    weights = [(bone_a, indices_a, 1.0)]
    if indices_b:
        # Set the transition vertices a second time below with per-vertex values.
        weights = []
    ob = ring_mesh(name, rings, sides, material, weights, arm, scene)
    if indices_b:
        ga = ob.vertex_groups.new(name=bone_a); gb = ob.vertex_groups.new(name=bone_b)
        for ri, t in enumerate(ts):
            b = max(0.0, min(1.0, (t - .45) / .30))
            for s in range(sides):
                i = ri*sides+s
                ga.add([i], 1.0-b, 'REPLACE')
                if b > .001: gb.add([i], b, 'REPLACE')
    return ob


def solid_z(name, z0, z1, wx0, wy0, wx1, wy1, material, bone_a, bone_b, arm, scene):
    rings = []
    for z, x, y in ((z0, wx0, wy0), (z0+.35*(z1-z0), wx0, wy0),
                    (z0+.70*(z1-z0), wx1, wy1), (z1, wx1, wy1)):
        rings.append(((0, 0, z), (1, 0, 0), (0, 1, 0), x, y))
    ob = ring_mesh(name, rings, 8, material, [], arm, scene)
    ga = ob.vertex_groups.new(name=bone_a); gb = ob.vertex_groups.new(name=bone_b)
    count = 8
    for i in range(len(rings)*count):
        t = (i // count) / (len(rings)-1); b = max(0.0, min(1.0, (t-.35)/.35))
        ga.add([i], 1-b, 'REPLACE')
        if b > .001: gb.add([i], b, 'REPLACE')
    return ob


def main():
    a = arguments(); bpy.ops.object.select_all(action='SELECT'); bpy.ops.object.delete(use_global=False)
    scene = bpy.context.scene; scene.unit_settings.system = 'METRIC'; scene.unit_settings.scale_length = 1.0
    bpy.ops.object.armature_add(enter_editmode=True, location=(0, 0, 0))
    arm = bpy.context.object; arm.name = 'RFCHAR_Armature'; eb = arm.data.edit_bones; eb.remove(eb[0]); bones = {}
    specs = [
      ('RF_ROOT',None,(0,0,0),(0,0,.10)), ('RF_HIPS','RF_ROOT',(0,0,.88),(0,0,1.02)),
      ('RF_SPINE','RF_HIPS',(0,0,1.02),(0,0,1.18)), ('RF_CHEST','RF_SPINE',(0,0,1.18),(0,0,1.42)),
      ('RF_UPPER_CHEST','RF_CHEST',(0,0,1.42),(0,0,1.54)), ('RF_NECK','RF_UPPER_CHEST',(0,0,1.54),(0,0,1.66)),
      ('RF_HEAD','RF_NECK',(0,0,1.66),(0,0,1.88)),
      ('RF_L_SHOULDER','RF_UPPER_CHEST',(.12,0,1.50),(.25,0,1.50)), ('RF_L_UPPER_ARM','RF_L_SHOULDER',(.25,0,1.50),(.54,0,1.50)),
      ('RF_L_FOREARM','RF_L_UPPER_ARM',(.54,0,1.50),(.79,0,1.50)), ('RF_L_HAND','RF_L_FOREARM',(.79,0,1.50),(.96,0,1.50)),
      ('RF_R_SHOULDER','RF_UPPER_CHEST',(-.12,0,1.50),(-.25,0,1.50)), ('RF_R_UPPER_ARM','RF_R_SHOULDER',(-.25,0,1.50),(-.54,0,1.50)),
      ('RF_R_FOREARM','RF_R_UPPER_ARM',(-.54,0,1.50),(-.79,0,1.50)), ('RF_R_HAND','RF_R_FOREARM',(-.79,0,1.50),(-.96,0,1.50)),
      ('RF_L_UPPER_LEG','RF_HIPS',(.13,0,.88),(.13,0,.51)), ('RF_L_LOWER_LEG','RF_L_UPPER_LEG',(.13,0,.51),(.13,0,.12)),
      ('RF_L_FOOT','RF_L_LOWER_LEG',(.13,0,.12),(.13,-.20,.07)), ('RF_R_UPPER_LEG','RF_HIPS',(-.13,0,.88),(-.13,0,.51)),
      ('RF_R_LOWER_LEG','RF_R_UPPER_LEG',(-.13,0,.51),(-.13,0,.12)), ('RF_R_FOOT','RF_R_LOWER_LEG',(-.13,0,.12),(-.13,-.20,.07))]
    for name, parent, head, tail in specs:
        b=eb.new(name); b.head=head; b.tail=tail; b.use_deform=True; bones[name]=b
        if parent: b.parent=bones[parent]
    attach = {'WEAPON_R':('RF_R_HAND',(-.91,-.10,1.48)), 'WEAPON_L':('RF_L_HAND',(.91,-.10,1.48)),
              'FOREGRIP':('RF_L_HAND',(.83,-.10,1.48)), 'BACK':('RF_CHEST',(0,.16,1.35)),
              'CHEST':('RF_CHEST',(0,-.20,1.34)), 'HEAD':('RF_HEAD',(0,0,1.88)),
              'HIP_L':('RF_L_UPPER_LEG',(.18,0,.85)), 'HIP_R':('RF_R_UPPER_LEG',(-.18,0,.85))}
    for aid,(parent,head) in attach.items():
        b=eb.new('RF_ATTACH_'+aid); b.head=head; b.tail=(head[0],head[1],head[2]+.08); b.parent=bones[parent]; b.use_deform=False
    bpy.ops.object.mode_set(mode='OBJECT')
    skin=bpy.data.materials.new('RF_Skin'); skin.diffuse_color=(.48,.25,.16,1)
    hair=bpy.data.materials.new('RF_Hair'); hair.diffuse_color=(.035,.045,.055,1)
    mainmat=bpy.data.materials.new('RF_ClothingMain'); mainmat.diffuse_color=(.16,.22,.24,1)
    secondary=bpy.data.materials.new('RF_ClothingSecondary'); secondary.diffuse_color=(.30,.34,.30,1)
    boot=bpy.data.materials.new('RF_Boots'); boot.diffuse_color=(.055,.07,.075,1)
    # Anatomical torso: pelvis, visibly pinched waist, broad chest.
    solid_z('Pelvis',.70,1.00,.27,.18,.24,.17,secondary,'RF_HIPS','RF_SPINE',arm,scene)
    solid_z('Waist',.96,1.18,.24,.17,.21,.15,mainmat,'RF_SPINE','RF_CHEST',arm,scene)
    solid_z('Chest',1.14,1.54,.22,.16,.34,.21,mainmat,'RF_CHEST','RF_UPPER_CHEST',arm,scene)
    ring_mesh('Neck', [((0,0,1.52),(1,0,0),(0,1,0),.105,.10),((0,0,1.68),(1,0,0),(0,1,0),.12,.11)], 8, skin, [('RF_NECK',range(16),1)], arm, scene)
    # Skull rings: jaw -> cheek/temple -> cranium -> crown.
    ring_mesh('Skull', [((0,0,1.64),(1,0,0),(0,1,0),.18,.14),((0,0,1.72),(1,0,0),(0,1,0),.27,.19),
                        ((0,0,1.84),(1,0,0),(0,1,0),.30,.22),((0,0,1.96),(1,0,0),(0,1,0),.22,.17)], 8, skin, [], arm, scene)
    ob=bpy.data.objects['Skull']; g=ob.vertex_groups.new(name='RF_HEAD'); g.add(range(len(ob.data.vertices)),1,'REPLACE')
    # Keep the forehead lighter than a full helmet, while retaining a broad,
    # slightly irregular crown and rear/side mass in the silhouette.
    ring_mesh('HairShell', [((0,.02,1.79),(1,0,0),(0,1,0),.285,.19),((0,.025,1.93),(1,0,0),(0,1,0),.27,.18),
                            ((0,.02,2.00),(1,0,0),(0,1,0),.20,.13)], 8, hair, [('RF_HEAD',range(24),1)], arm, scene,
               front_cut=True)
    # Arms, hands, legs, feet. Each joint transition is deliberately BDEF2.
    for side,sgn in (('L',1),('R',-1)):
        segment(side+'UpperArm',(.25*sgn,0,1.50),(.54*sgn,0,1.50),.115,.095,mainmat,'RF_'+side+'_UPPER_ARM','RF_'+side+'_FOREARM',arm,scene)
        segment(side+'Forearm',(.54*sgn,0,1.50),(.79*sgn,0,1.50),.095,.082,secondary,'RF_'+side+'_FOREARM','RF_'+side+'_HAND',arm,scene)
        segment(side+'Hand',(.79*sgn,0,1.50),(.98*sgn,0,1.50),.095,.085,skin,'RF_'+side+'_HAND','RF_'+side+'_HAND',arm,scene)
        segment(side+'Thigh',(.13*sgn,0,.87),(.13*sgn,0,.51),.145,.115,mainmat,'RF_'+side+'_UPPER_LEG','RF_'+side+'_LOWER_LEG',arm,scene)
        segment(side+'Calf',(.13*sgn,0,.51),(.13*sgn,0,.12),.115,.085,secondary,'RF_'+side+'_LOWER_LEG','RF_'+side+'_FOOT',arm,scene)
        segment(side+'Foot',(.13*sgn,0,.12),(.13*sgn,-.22,.07),.105,.13,boot,'RF_'+side+'_FOOT','RF_'+side+'_FOOT',arm,scene)
    # Apply no object transforms: all geometry is authored in armature space.
    for ob in list(scene.objects): ob.select_set(True)
    bpy.context.view_layer.objects.active=arm
    bpy.ops.export_scene.gltf(filepath=a.output,export_format='GLB',use_selection=True,export_skins=True,
        export_influence_nb=4,export_all_influences=True,export_morph=False,export_animations=False,
        export_yup=True,export_apply=False,export_armature_object_remove=True)
    # Blender 4.x can omit the explicit skin skeleton node. RFCHAR requires it.
    path=Path(a.output); raw=path.read_bytes(); jn,jk=struct.unpack_from('<II',raw,12)
    doc=json.loads(raw[20:20+jn].rstrip(b' \0')); root=next(i for i,n in enumerate(doc['nodes']) if n.get('name')=='RF_ROOT')
    doc['skins'][0]['skeleton']=root; encoded=json.dumps(doc,separators=(',',':')).encode(); encoded+=b' '*(-len(encoded)%4)
    bin_at=20+jn; bn,bk=struct.unpack_from('<II',raw,bin_at); blob=raw[bin_at+8:bin_at+8+bn]
    total=12+8+len(encoded)+8+len(blob); path.write_bytes(b'glTF'+struct.pack('<II',2,total)+struct.pack('<II',len(encoded),jk)+encoded+struct.pack('<II',len(blob),bk)+blob)
    print('rfchar-humanoid:', a.output)


if __name__ == '__main__': main()
