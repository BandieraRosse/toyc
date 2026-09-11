"""Six original low-poly infected carriers; reuse RF Humanoid V2 RFCHAR export.

Blender --background --factory-startup --python this_file -- --family block
--type heavy --output path.glb. No imported third-party geometry or textures.
"""
import argparse
import sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent))
import generate_rasterfall_humanoid_v2 as rf
import bpy


def build(arm, scene, mats, family, kind):
    palette = {'skin': (.32, .40, .29), 'hair': (.07, .09, .065),
               'shirt': (.18, .26, .25), 'pants': (.12, .15, .17),
               'boots': (.055, .065, .065), 'headgear': (.19, .23, .21),
               'headgear_light': (.43, .39, .25), 'visor': (.43, .11, .085),
               'mask': (.70, .47, .20)}
    if kind == 'fast':
        palette['shirt'] = (.30, .24, .17)
        palette['skin'] = (.39, .43, .29)
    if kind == 'heavy':
        palette['shirt'] = (.19, .21, .16)
        palette['skin'] = (.37, .38, .25)
    for key, color in palette.items():
        mats[key].diffuse_color = (*color, 1)
    def box(name, lo, hi, mat='skin', bone='RF_CHEST'):
        return rf.box_mesh(name, lo, hi, mats[mat], arm, scene, bone)
    if family == 'humanoid':
        # Same anatomical source and weights, fewer radial facets for crowds.
        rf.SIDES_BODY = 8
        rf.SIDES_LIMB = 6
        rf.create_body(arm, scene, mats)
    else:
        box('Pelvis', (-.23,-.15,.79), (.23,.15,1.00), 'pants', 'RF_HIPS')
        torso = box('TornShirt', (-.25,-.17,1.00), (.25,.17,1.54), 'shirt')
        # Blend the lower shirt into the waist while its shoulders follow chest.
        # This keeps a flexible waist without subdividing the block silhouette.
        spine = torso.vertex_groups.new(name='RF_SPINE')
        for vertex in torso.data.vertices:
            if vertex.co.z < 1.1:
                torso.vertex_groups['RF_CHEST'].add([vertex.index], .25, 'REPLACE')
                spine.add([vertex.index], .75, 'REPLACE')
        box('Neck', (-.075,-.07,1.52), (.075,.07,1.72), 'skin', 'RF_NECK')
        box('BrokenJaw', (-.13,-.14,1.65), (.10,.10,1.82), 'skin','RF_HEAD')
        box('Skull', (-.145,-.135,1.79), (.145,.135,2.04), 'skin','RF_HEAD')
        box('HairRemnant', (-.15,.01,1.88), (.15,.145,2.07), 'hair','RF_HEAD')
        for side, sign in [('L',1),('R',-1)]:
            def limb(name, x0, x1, z0, z1, depth, mat, bone):
                box(name+side, (min(sign*x0,sign*x1),-depth,z0),
                    (max(sign*x0,sign*x1),depth,z1),mat,'RF_'+side+'_'+bone)
            limb('UpperArm',.27,.58,1.40,1.60,.10,'shirt','UPPER_ARM')
            limb('Forearm',.58,.83,1.415,1.585,.085,'skin','FOREARM')
            limb('Hand',.83,1.00,1.42,1.58,.075,'skin','HAND')
            limb('Thigh',.055,.245,.50,.88,.13,'pants','UPPER_LEG')
            limb('Shin',.07,.23,.12,.50,.10,'pants' if side=='R' else 'skin','LOWER_LEG')
            box('Boot'+side,(sign*.15-.105,-.26,.015),(sign*.15+.105,.12,.15),'boots','RF_'+side+'_FOOT')
    # Large asymmetric wounds, cloth remnants and raised infection growth.
    box('OpenRibWound',(-.26,-.186,1.13),(-.04,-.165,1.39),'visor')
    for n in range(3):
        box('ExposedRib'+str(n),(-.235,-.207,1.16+n*.066),(-.065,-.18,1.187+n*.066),'mask')
    box('JawLesion',(.055,-.153,1.70),(.155,-.132,1.82),'visor','RF_HEAD')
    box('TornHem',(.09,-.195,.93),(.225,-.15,1.08),'shirt','RF_HIPS')
    box('InfectedShoulder',(-.40,-.145,1.46),(-.24,.14,1.65),'visor','RF_R_UPPER_ARM')
    def growth(name, p0, p1, radius, bone='RF_CHEST'):
        rf.segment_loft(name,p0,p1,[(0,radius,radius,0),(.65,radius*.68,radius*.65,0),(1,.012,.012,0)],
                        4 if family=='block' else 5,mats['mask'],arm,scene,bone)
    growth('ShoulderSplinter',(-.30,.04,1.57),(-.43,.09,1.84),.08,'RF_R_UPPER_ARM')
    if kind == 'fast':
        # Narrow trunk and elongated reach; fingers form an unmistakable hook.
        for side, sign in [('L',1),('R',-1)]:
            for n in range(2):
                growth('Hook'+side+str(n),(sign*.94,-.045+n*.08,1.50),
                       (sign*1.16,-.12+n*.08,1.46),.035,'RF_'+side+'_HAND')
        for n in range(3):
            growth('SpinalFin'+str(n),(0,.16,1.21+n*.12),(0,.35,1.34+n*.12),.055)
    if kind == 'heavy':
        box('ResidualChestPlate',(-.30,-.25,1.14),(.12,-.19,1.53),'headgear')
        box('PlateStripe',(-.26,-.265,1.30),(-.18,-.25,1.49),'headgear_light')
        box('BrokenCollar',(-.24,-.23,1.52),(.20,.13,1.63),'headgear')
        box('BackArmor',(-.30,.13,1.10),(.30,.28,1.56),'headgear')
        box('ShoulderArmor',(.26,-.19,1.42),(.53,.19,1.66),'headgear','RF_L_UPPER_ARM')
        box('MutatedForearm',(-.88,-.18,1.33),(-.57,.17,1.66),'skin','RF_R_FOREARM')
        for n in range(3):
            growth('ArmorFusion'+str(n),(-.25+n*.14,.20,1.49),(-.40+n*.20,.29,1.86+n%2*.10),.12)
        growth('ArmGrowth',(-.69,.04,1.60),(-.76,.10,1.88),.105,'RF_R_FOREARM')
        box('KneeArmor',(.04,-.18,.43),(.27,-.115,.62),'headgear','RF_L_LOWER_LEG')
    # Rest-space proportion edits transform geometry AND bones/socket anchors.
    # This preserves inverse bind matrices; no render-time bind compensation.
    def deform(v):
        x,y,z = v
        if kind == 'fast':
            x = x*.74 if abs(x)<.28 else (1 if x>0 else -1)*(.28*.74+(abs(x)-.28)*1.20)
            y *= .78
            z *= 1.07
        elif kind == 'heavy':
            x *= 1.40
            y *= 1.38
            z *= 1.09
        return (x,y,z)
    for obj in scene.objects:
        if obj.type == 'MESH':
            for v in obj.data.vertices: v.co = deform(v.co)
            obj.data.update()
    bpy.context.view_layer.objects.active = arm
    bpy.ops.object.mode_set(mode='EDIT')
    for bone in arm.data.edit_bones:
        bone.head = deform(bone.head)
        bone.tail = deform(bone.tail)
    bpy.ops.object.mode_set(mode='OBJECT')


def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--family',choices=['block','humanoid'],required=True)
    parser.add_argument('--type',choices=['common','fast','heavy'],required=True)
    parser.add_argument('--output',required=True)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    Path(args.output).parent.mkdir(parents=True,exist_ok=True)
    original_body=rf.create_body
    # Reuse the existing exporter without duplicating its skin/primitive rules.
    def body(arm,scene,mats):
        rf.create_body=original_body
        build(arm,scene,mats,args.family,args.type)
    rf.create_body=body
    rf.arguments=lambda: argparse.Namespace(output=args.output,rigid_attachment=None,
                                           profession=None,headgear='bare')
    rf.main()

if __name__=='__main__': main()
