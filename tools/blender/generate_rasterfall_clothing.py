"""Generate separate canonical RFCHAR jacket/trousers skin resources.

Garments keep the shared Humanoid skeleton, weights and bind space. The source
surfaces are tailored from its known fit, then given their own silhouette,
seams, cuffs and pockets. UV0 is metric projected and ready for base textures.
"""
import argparse
import math
import sys
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import bpy
import generate_rasterfall_humanoid_v2 as humanoid
from rf_tactical_textures import apply_textiles, patch_export_materials


def assign_uv(obj):
    """Six-plane cloth projection, independent of triangle order."""
    mesh = obj.data
    uv = mesh.uv_layers.new(name='UVMap')
    for polygon in mesh.polygons:
        axis = max(range(3), key=lambda a: abs(polygon.normal[a]))
        axes = ((1, 2), (0, 2), (0, 1))[axis]
        for loop in polygon.loop_indices:
            co = mesh.vertices[mesh.loops[loop].vertex_index].co
            # Metre-scaled textile repeats, deliberately shared between pieces.
            uv.data[loop].uv = ((co[axes[0]] + 1.2) / 2.4,
                (co[axes[1]]+(0 if axes[1]==2 else 1.2))/2.4)


def surface(name, rows, material, arm, scene):
    """A longitudinal cloth detail with matching adjacent-bone weights."""
    vertices, faces = [], []
    for z, x, y, half_width, weights in rows:
        vertices.extend(((x-half_width,y,z),(x+half_width,y,z)))
    for row in range(len(rows)-1):
        n = row*2
        faces.append((n,n+1,n+3,n+2))
    mesh=bpy.data.meshes.new(name)
    mesh.from_pydata(vertices,[],faces)
    mesh.materials.append(material)
    mesh.update()
    obj=bpy.data.objects.new(name,mesh)
    scene.collection.objects.link(obj)
    obj.parent=arm
    modifier=obj.modifiers.new('RFCHAR Skin','ARMATURE')
    modifier.object=arm
    for i,row in enumerate(rows):
        for bone,weight in row[4]:
            if weight <= .001: continue
            group=obj.vertex_groups.get(bone) or obj.vertex_groups.new(name=bone)
            group.add([2*i,2*i+1],weight,'REPLACE')
    return obj


def create_garment(arm, scene, materials, garment):
    humanoid.create_body(arm,scene,materials)
    keep = ('Torso','ShirtCollar','LSleeve','RSleeve') if garment=='jacket' else (
        'Pelvis','LTrouser','RTrouser')
    for obj in list(scene.objects):
        if obj.type=='MESH' and obj.name not in keep:
            bpy.data.objects.remove(obj,do_unlink=True)
    cloth=materials['cloth']
    trim=materials['webbing']
    accent=materials['headgear_light']
    humanoid.set_material_color(cloth,(.155,.170,.123) if garment=='jacket' else (.125,.140,.104))
    humanoid.set_material_color(accent,(.20,.22,.17))
    for obj in scene.objects:
        if obj.type!='MESH': continue
        obj.data.materials.clear()
        obj.data.materials.append(cloth)
        for vertex in obj.data.vertices:
            co=vertex.co
            if obj.name in ('Torso','ShirtCollar','Pelvis'):
                co.x*=1.095
                co.y*=1.12
                if obj.name=='Torso' and co.z<1.075:
                    co.z-=.055*(1.075-co.z)/.115
                if obj.name=='ShirtCollar':
                    co.z+=.018
            elif obj.name.endswith('Sleeve'):
                co.y=.003+(co.y-.003)*1.17
                co.z=1.50+(co.z-1.50)*1.17
            else:
                sign=1 if obj.name.startswith('L') else -1
                co.x=.15*sign+(co.x-.15*sign)*1.16
                co.y*=1.17
                # Small authored cloth folds retain a continuous knee bridge.
                co.y+=.004*math.sin(co.z*54)
        obj.data.update()
    if garment=='jacket':
        surface('FrontZip',[(.91,0,-.210,.012,[('RF_HIPS',.55),('RF_SPINE',.45)]),
            (1.075,0,-.179,.012,[('RF_SPINE',1)]),
            (1.25,0,-.231,.012,[('RF_SPINE',.65),('RF_CHEST',.35)]),
            (1.435,0,-.226,.012,[('RF_CHEST',.45),('RF_UPPER_CHEST',.55)]),
            (1.565,0,-.133,.012,[('RF_UPPER_CHEST',1)])],trim,arm,scene)
        for sign,side in ((1,'L'),(-1,'R')):
            # Upper-arm pocket sits on the sleeve's outward/top surface.
            humanoid.box_mesh(side+'SleevePocket',
                (sign*.40-.073,-.050,1.604),(sign*.40+.073,.065,1.627),
                accent,arm,scene,'RF_'+side+'_UPPER_ARM')
            humanoid.box_mesh(side+'SleevePocketFlap',
                (sign*.40-.073,-.055,1.625),(sign*.40+.073,-.035,1.635),
                trim,arm,scene,'RF_'+side+'_UPPER_ARM')
        # A bound collar and hem are separate cloth volumes, not painted lines.
        humanoid.vertical_loft('JacketHem',[(.897,-.003,.292,.211),
            (.923,-.003,.292,.211)],16,trim,arm,scene,
            [[('RF_HIPS',.55),('RF_SPINE',.45)]]*2)
    else:
        for side,sign in (('L',1),('R',-1)):
            prefix='RF_'+side+'_'
            surface(side+'KneeReinforcement',[
                (.432,.15*sign,-.132,.080,[(prefix+'LOWER_LEG',1)]),
                (.505,.15*sign,-.150,.090,[(prefix+'UPPER_LEG',.40),(prefix+'LOWER_LEG',.60)]),
                (.560,.15*sign,-.135,.087,[(prefix+'UPPER_LEG',.80),(prefix+'LOWER_LEG',.20)])],
                accent,arm,scene)
            x=sign*.295
            humanoid.box_mesh(side+'CargoPocket',(x-.035,-.078,.632),
                (x+.035,.090,.797),cloth,arm,scene,prefix+'UPPER_LEG')
            humanoid.box_mesh(side+'CargoPocketFlap',(x-.041,-.084,.774),
                (x+.041,.096,.809),trim,arm,scene,prefix+'UPPER_LEG')
        humanoid.vertical_loft('TrouserWaist',[(.960,0,.285,.187),
            (.995,0,.273,.171)],16,trim,arm,scene,
            [[('RF_HIPS',.35),('RF_SPINE',.65)]]*2)
    for obj in scene.objects:
        if obj.type=='MESH': assign_uv(obj)
    apply_textiles(scene,materials,('cloth','webbing','headgear_light'))


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--garment',required=True,choices=('jacket','trousers'))
    parser.add_argument('--output',required=True,type=Path)
    args=parser.parse_args(sys.argv[sys.argv.index('--')+1:])
    bpy.ops.object.select_all(action='SELECT')
    bpy.ops.object.delete(use_global=False)
    scene=bpy.context.scene
    scene.unit_settings.system='METRIC'
    scene.unit_settings.scale_length=1
    arm=humanoid.make_armature(scene)
    create_garment(arm,scene,humanoid.create_materials(),args.garment)
    bpy.ops.object.select_all(action='DESELECT')
    meshes=[obj for obj in scene.objects if obj.type=='MESH']
    for obj in meshes: obj.select_set(True)
    bpy.context.view_layer.objects.active=meshes[0]
    bpy.ops.object.join()
    arm.select_set(True)
    args.output.parent.mkdir(parents=True,exist_ok=True)
    bpy.ops.export_scene.gltf(filepath=str(args.output),export_format='GLB',
        use_selection=True,export_skins=True,export_influence_nb=4,
        export_all_influences=True,export_morph=False,export_animations=False,
        export_yup=True,export_apply=False,export_armature_object_remove=True)
    humanoid.patch_glb_skeleton(args.output)
    patch_export_materials(args.output)
    print('RF clothing:',args.garment,args.output)


if __name__=='__main__':
    main()
