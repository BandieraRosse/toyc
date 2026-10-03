"""Original modular tactical equipment, authored in the RF Humanoid bind space.

The generators describe sewn volumes and separately mounted hard parts. They
use only opaque materials and ordinary RFCHAR rigid weights; no game asset or
texture is copied. Pants modules end above the knee and never bridge a joint.
"""
import math

import bpy


class Equipment:
    def __init__(self, api, arm, scene, materials, prefix=''):
        self.api, self.arm, self.scene = api, arm, scene
        self.materials, self.prefix = materials, prefix

    def box(self, name, lo, hi, material='headgear', bone='RF_CHEST',
            slot='chest', bevel=.012):
        obj = self.api['box_mesh'](self.prefix + name, lo, hi,
            self.materials[material], self.arm, self.scene, bone)
        obj['rf_attachment_slot'] = slot
        # Bevels are real silhouette geometry, not normal-map decoration.
        if bevel:
            modifier = obj.modifiers.new('Sewn or machined edge', 'BEVEL')
            modifier.width = min(bevel, min(b-a for a, b in zip(lo, hi))*.35)
            modifier.segments = 1
            bpy.context.view_layer.objects.active = obj
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        return obj

    def rod(self, name, a, b, radius, material='webbing', bone='RF_CHEST',
            slot='chest'):
        obj = self.api['segment_loft'](self.prefix+name, a, b,
            [(0, radius, radius, 0), (1, radius, radius, 0)], 6,
            self.materials[material], self.arm, self.scene, bone)
        obj['rf_attachment_slot'] = slot
        return obj


def goggles(api, arm, scene, materials):
    kit = Equipment(api, arm, scene, materials, 'Eyewear_')
    before = {obj.name for obj in scene.objects}
    # A wraparound two-lens frame leaves the nose, cheek and brow readable.
    for side, sign in (('L', 1), ('R', -1)):
        x = .105*sign
        kit.box(side+'Seal', (x-.105, -.248, 1.785),
                (x+.105, -.191, 1.890), 'rubber', 'RF_HEAD', 'head', .022)
        kit.box(side+'Frame', (x-.095, -.265, 1.795),
                (x+.095, -.239, 1.880), 'headgear_light', 'RF_HEAD', 'head', .018)
        kit.box(side+'Lens', (x-.078, -.276, 1.810),
                (x+.078, -.262, 1.866), 'visor', 'RF_HEAD', 'head', .016)
        kit.box(side+'Catchlight', (x-.058, -.278, 1.853),
                (x+.018, -.275, 1.859), 'lens_glint', 'RF_HEAD', 'head', .002)
        kit.rod(side+'Temple', (.198*sign, -.212, 1.844),
                (.237*sign, .070, 1.847), .014, 'webbing', 'RF_HEAD', 'head')
        kit.box(side+'Buckle', (.235*sign-.016, .04, 1.823),
                (.235*sign+.016, .095, 1.866), 'metal', 'RF_HEAD', 'head', .007)
    kit.box('Bridge', (-.030, -.267, 1.838), (.030, -.238, 1.866),
            'rubber', 'RF_HEAD', 'head', .008)
    kit.box('RearStrap', (-.180, .201, 1.826), (.180, .227, 1.865),
            'webbing', 'RF_HEAD', 'head', .008)
    # Wrap the frame toward the temples instead of projecting a rectangular
    # visor in front of the face. Seal backs deliberately sink into the head.
    for obj in scene.objects:
        if obj.type != 'MESH' or obj.name in before:
            continue
        for vertex in obj.data.vertices:
            x, y = vertex.co.x, vertex.co.y
            if y < -.10:
                vertex.co.y += .035 + .055*(abs(x)/.21)**2
            vertex.co.x *= .93


def respirator(api, arm, scene, materials):
    kit = Equipment(api, arm, scene, materials, 'Respirator_')
    api['vertical_loft']('RespiratorContouredSeal',
        [(1.647,-.143,.055,.045),(1.682,-.173,.100,.064),
         (1.738,-.177,.151,.070),(1.785,-.164,.132,.055),
         (1.811,-.177,.055,.049)],16,materials['mask'],arm,scene,
        [[('RF_HEAD',1)]]*5)
    kit.box('CenterValve',(-.035,-.257,1.684),(.035,-.237,1.746),
            'rubber','RF_HEAD','head',.014)
    for sign in (-1,1):
        a=(sign*.121,-.197,1.712)
        b=(sign*.159,-.249,1.712)
        api['segment_loft']('RespiratorFilter'+str(sign),a,b,
            [(0,.047,.044,0),(.22,.057,.053,0),(.85,.057,.053,0),(1,.050,.047,0)],
            12,materials['headgear'],arm,scene,'RF_HEAD')
        kit.rod('FilterRim'+str(sign),b,(sign*.167,-.260,1.712),
                .043,'rubber','RF_HEAD','head')
        kit.rod('LowerStrap'+str(sign),(.133*sign,-.128,1.698),
                (.218*sign,.078,1.770),.010,'webbing','RF_HEAD','head')


def helmet(api, arm, scene, materials):
    kit = Equipment(api, arm, scene, materials, 'Ballistic_')
    # A high-cut shell: brow low at the front, clearance above both ear cups.
    sides = 24
    rings = []
    for height, width, depth in ((1.930, .238, .213), (1.985, .248, .220),
                                  (2.050, .237, .213), (2.105, .204, .184),
                                  (2.145, .145, .137), (2.164, .070, .073)):
        points = []
        for i in range(sides):
            angle = 2*math.pi*i/sides
            side_cut = abs(math.cos(angle))**5
            z = height + (.042*side_cut if height < 1.96 else 0)
            points.append((width*math.cos(angle), .025+depth*math.sin(angle), z))
        rings.append(points)
    vertices = [p for ring in rings for p in ring]
    faces = []
    for ring in range(len(rings)-1):
        for i in range(sides):
            a, b = ring*sides+i, ring*sides+(i+1)%sides
            faces.append((a,b,b+sides,a+sides))
    faces.append(tuple(range((len(rings)-1)*sides, len(rings)*sides)))
    # Open helmet underside; the visible rim has its own thickness.
    mesh = bpy.data.meshes.new('BallisticShell')
    mesh.from_pydata(vertices, [], faces)
    mesh.materials.append(materials['headgear'])
    mesh.update()
    obj = bpy.data.objects.new('BallisticShell', mesh)
    scene.collection.objects.link(obj)
    obj.parent = arm
    obj['rf_attachment_slot'] = 'head'
    group = obj.vertex_groups.new(name='RF_HEAD')
    group.add(list(range(len(vertices))), 1, 'REPLACE')
    modifier = obj.modifiers.new('RFCHAR Skin', 'ARMATURE')
    modifier.object = arm
    for i in range(sides):
        kit.rod('Rim'+str(i), rings[0][i], rings[0][(i+1)%sides], .012,
                'rubber', 'RF_HEAD', 'head')
    for sign in (-1,1):
        x = .242*sign
        kit.box('SideRail'+str(sign), (x-.015, -.076, 1.958),
                (x+.015, .130, 2.006), 'rubber', 'RF_HEAD', 'head', .008)
        for n in range(4):
            kit.box('RailTooth'+str(sign)+str(n),
                (x-.019, -.059+n*.044, 1.967), (x+.019, -.037+n*.044, 1.996),
                'metal', 'RF_HEAD', 'head', .003)
        kit.box('EarCup'+str(sign), (.232*sign-.026, -.011, 1.780),
                (.232*sign+.026, .098, 1.933), 'rubber', 'RF_HEAD', 'head', .021)
        kit.box('EarPanel'+str(sign), (.258*sign-.012, .012, 1.812),
                (.258*sign+.012, .085, 1.903), 'headgear_light', 'RF_HEAD', 'head', .013)
        kit.rod('ChinStrap'+str(sign), (.204*sign, -.04, 1.935),
                (.124*sign, -.151, 1.690), .010, 'webbing', 'RF_HEAD', 'head')
        kit.box('CrownPatch'+str(sign), (.126*sign-.051, -.094, 2.117),
                (.126*sign+.051, .079, 2.133), 'headgear_light', 'RF_HEAD', 'head', .012)
    kit.box('NVGMountBase', (-.057,-.205,1.968), (.057,-.174,2.045),
            'rubber', 'RF_HEAD','head',.014)
    kit.box('NVGMount', (-.034,-.220,1.986), (.034,-.201,2.029),
            'metal', 'RF_HEAD','head',.007)
    kit.box('RearBattery', (-.088,.211,1.984), (.088,.245,2.042),
            'rubber','RF_HEAD','head',.012)


def outer_thigh(api, arm, scene, materials, side):
    sign = 1 if side == 'l' else -1
    bone, slot = 'RF_'+side.upper()+'_UPPER_LEG', 'hip-'+side
    kit = Equipment(api, arm, scene, materials, 'Cargo_'+side+'_')
    # A partial, open inner seam avoids welding the legs at the crotch. This
    # rigid piece stops at mid-thigh; the body owns articulated knee cloth.
    ring_count, sides = 5, 14
    vertices = []
    for z, width, depth in ((.60,.133,.150),(.65,.154,.168),(.75,.164,.178),
                            (.85,.157,.176),(.91,.149,.168)):
        for i in range(sides):
            angle = -math.pi*.67 + (math.pi*1.34)*i/(sides-1)
            vertices.append((sign*(.15+width*math.cos(angle)),
                             depth*math.sin(angle), z))
    faces = []
    for ring in range(ring_count-1):
        for i in range(sides-1):
            a = ring*sides+i
            face = (a,a+1,a+1+sides,a+sides)
            faces.append(face if sign > 0 else tuple(reversed(face)))
    mesh = bpy.data.meshes.new('CargoShell_'+side)
    mesh.from_pydata(vertices,[],faces)
    mesh.materials.append(materials['cloth'])
    mesh.update()
    obj = bpy.data.objects.new('CargoShell_'+side, mesh)
    scene.collection.objects.link(obj)
    obj.parent = arm
    obj['rf_attachment_slot'] = slot
    group = obj.vertex_groups.new(name=bone)
    group.add(list(range(len(vertices))),1,'REPLACE')
    modifier = obj.modifiers.new('RFCHAR Skin','ARMATURE')
    modifier.object = arm
    # Side cargo pocket has a gusset, flap and two closures. It protrudes
    # from the garment and remains visible in side and RTS views.
    x = sign*.307
    kit.box('Pocket', (x-.035,-.077,.648),(x+.035,.093,.826),
            'cloth',bone,slot,.023)
    kit.box('Flap', (x-.043,-.082,.799),(x+.043,.101,.841),
            'headgear_light',bone,slot,.012)
    for y in (-.042,.061):
        kit.box('Closure'+str(y),(x-.047,y-.012,.784),(x+.047,y+.012,.821),
                'webbing',bone,slot,.006)
    kit.box('FrontReinforcement',(sign*.178-.071,-.183,.635),
            (sign*.178+.071,-.156,.766),'rubber',bone,slot,.017)
    kit.box('Hem',(sign*.178-.075,-.175,.612),
            (sign*.178+.075,-.149,.633),'webbing',bone,slot,.007)


def profession(api, arm, scene, materials, name):
    kit = Equipment(api,arm,scene,materials,name+'_')
    heavy = name in ('breacher','heavy','gunner-elite')
    light = name == 'recon'
    width = .25 if heavy else .205 if not light else .173
    bottom = 1.08 if heavy else 1.14
    top = 1.48 if not light else 1.40
    front = -.257 if heavy else -.236
    # Carrier perimeter, inset ballistic plate, raised binding and three
    # separate magazine pouches communicate construction at medium distance.
    kit.box('Carrier',(-width,front-.018,bottom),(width,-.138,top),bevel=.045)
    kit.box('PlatePocket',(-width*.81,front-.040,bottom+.042),
            (width*.81,front-.020,top-.025),'headgear_light',bevel=.035)
    kit.box('UpperPatch',(-width*.51,front-.050,top-.105),
            (width*.51,front-.037,top-.057),'webbing',bevel=.008)
    for sign in (-1,1):
        x = sign*width*.71
        kit.box('ShoulderPad'+str(sign),(x-.030,-.151,top-.015),
                (x+.030,.173,1.527),'webbing',bevel=.014)
        kit.box('QuickRelease'+str(sign),(x-.034,front-.029,top-.051),
                (x+.034,front-.002,top-.006),'metal',bevel=.008)
        kit.box('Cummerbund'+str(sign),(sign*.229-.046,-.116,1.155),
                (sign*.229+.046,.150,1.273),'webbing',bevel=.018)
        for row in range(2):
            kit.box('SideWeb'+str(sign)+str(row),(sign*.270-.014,-.106,1.172+row*.052),
                    (sign*.270+.014,.118,1.188+row*.052),'headgear_light',bevel=.004)
    for n in range(2 if light else 3):
        x = (n-(.5 if light else 1))*.117
        z = bottom-.032
        kit.box('Pouch'+str(n),(x-.048,front-.100,z),
                (x+.048,front-.030,z+.163),bevel=.015)
        kit.box('PouchFlap'+str(n),(x-.047,front-.112,z+.120),
                (x+.047,front-.094,z+.166),'headgear_light',bevel=.008)
        kit.box('PouchRetention'+str(n),(x-.010,front-.115,z+.023),
                (x+.010,front-.103,z+.121),'webbing',bevel=.003)
    for row in range(2):
        for column in range(4):
            x = -.148+column*.077
            kit.box('Molle'+str(row)+str(column),(x,front-.052,top-.164-row*.035),
                    (x+.061,front-.041,top-.151-row*.035),'webbing',bevel=.003)
    kit.box('Radio',(.213,-.052,1.282),(.266,.036,1.438),'rubber',bevel=.012)
    kit.rod('RadioAntenna',(.240,.012,1.420),(.240,.014,1.618),.008,'rubber')
    if heavy:
        kit.box('Abdomen',(-.168,-.220,.948),(.168,-.153,1.083),bevel=.027)
        for sign in (-1,1):
            kit.box('Collar'+str(sign),(sign*.123-.039,-.119,1.460),
                    (sign*.123+.039,.105,1.574),'headgear_light',bevel=.021)
    # The eight existing identities retain different pack silhouettes.
    pack_width = .275 if name in ('heavy','medic') else .226 if heavy else .178
    if light: pack_width = .115
    pack_depth = .45 if name in ('heavy','medic') else .369
    pack_bottom, pack_top = (1.04,1.57) if name != 'rifleman' else (1.13,1.49)
    kit.box('BackPack',(-pack_width,.165,pack_bottom),
            (pack_width,pack_depth,pack_top),slot='back',bevel=.047)
    kit.box('PackPanel',(-pack_width*.82,pack_depth-.002,pack_bottom+.056),
            (pack_width*.82,pack_depth+.032,pack_top-.08),
            'headgear_light',slot='back',bevel=.029)
    kit.box('TopFlap',(-pack_width*.89,.224,pack_top-.005),
            (pack_width*.89,pack_depth-.014,pack_top+.030),'webbing',slot='back',bevel=.016)
    for sign in (-1,1):
        x = sign*pack_width*.60
        kit.box('Compression'+str(sign),(x-.013,pack_depth+.025,pack_bottom+.033),
                (x+.013,pack_depth+.041,pack_top-.046),'webbing',slot='back',bevel=.005)
        kit.box('PackBuckle'+str(sign),(x-.023,pack_depth+.038,pack_bottom+.119),
                (x+.023,pack_depth+.055,pack_bottom+.158),'metal',slot='back',bevel=.006)
    if name == 'medic':
        kit.box('MedicalChestPatch',(-.062,front-.060,1.323),(.062,front-.050,1.432),
                'headgear_light',bevel=.010)
    if name == 'engineer':
        kit.box('ToolShaft',(-.252,.24,1.34),(-.209,.289,1.73),
                'metal',slot='back',bevel=.008)
        kit.box('ToolHead',(-.333,.222,1.652),(-.114,.309,1.760),
                'headgear_light',slot='back',bevel=.019)
        kit.box('HipToolbox',(.275,-.06,.765),(.407,.145,.982),
                'headgear_light','RF_L_UPPER_LEG','hip-l',.022)
    if name == 'heavy':
        for sign,side in ((1,'l'),(-1,'r')):
            x = .333*sign
            kit.box('HipAmmo'+side,(x-.069,-.058,.792),(x+.069,.143,.979),
                    'headgear','RF_'+side.upper()+'_UPPER_LEG','hip-'+side,.021)
