"""Prepare an FBX character and emit a temporary PMX for Rasterfall.

Run through Blender only.  PMX is an interchange detail: the checked-in
pipeline immediately converts it to RFM2/SKN1 and TTEX.
"""

import argparse
import math
import os
import re
import shutil
import struct
import sys

if os.environ.get("BLENDER_PYTHONPATH"):
    sys.path.insert(0, os.environ["BLENDER_PYTHONPATH"])

import bpy
from mathutils import Vector


SEMANTIC_NAMES = {
    "mixamorig:Hips": "腰",
    "mixamorig:Spine": "上半身",
    "mixamorig:Spine1": "上半身3",
    "mixamorig:Spine2": "上半身2",
    "mixamorig:Neck": "首",
    "mixamorig:Head": "頭",
    "mixamorig:LeftShoulder": "左肩",
    "mixamorig:LeftArm": "左腕",
    "mixamorig:LeftForeArm": "左ひじ",
    "mixamorig:LeftHand": "左手首",
    "mixamorig:RightShoulder": "右肩",
    "mixamorig:RightArm": "右腕",
    "mixamorig:RightForeArm": "右ひじ",
    "mixamorig:RightHand": "右手首",
    "mixamorig:LeftUpLeg": "左足",
    "mixamorig:LeftLeg": "左ひざ",
    "mixamorig:LeftFoot": "左足首",
    "mixamorig:RightUpLeg": "右足",
    "mixamorig:RightLeg": "右ひざ",
    "mixamorig:RightFoot": "右足首",
}


def arguments():
    tail = sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else []
    parser = argparse.ArgumentParser()
    parser.add_argument("--input", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--target-triangles", type=int, default=5000)
    return parser.parse_args(tail)


def pmx_text(value):
    raw = value.encode("utf-8")
    return struct.pack("<i", len(raw)) + raw


def pmx_index(value):
    return struct.pack("<i", value)


def pmx_space(v):
    # Blender is Z-up/-Y-forward; PMX/Rasterfall character assets are Y-up.
    return Vector((v.x, v.z, -v.y))


def safe_name(name):
    name = re.sub(r"[^A-Za-z0-9_.-]+", "_", name)
    return name.strip("._") or "texture"


def base_image(material):
    if not material or not material.use_nodes or not material.node_tree:
        return None
    principled = next((n for n in material.node_tree.nodes
                       if n.type == "BSDF_PRINCIPLED"), None)
    if principled:
        color = principled.inputs.get("Base Color")
        if color and color.is_linked:
            node = color.links[0].from_node
            if node.type == "TEX_IMAGE" and node.image:
                return node.image
    return next((n.image for n in material.node_tree.nodes
                 if n.type == "TEX_IMAGE" and n.image), None)


def material_color(material):
    if not material:
        return (1.0, 1.0, 1.0, 1.0)
    c = material.diffuse_color
    return tuple(float(x) for x in c)


def ordered_bones(armature):
    pending = list(armature.data.bones)
    result = []
    while pending:
        progress = False
        for bone in pending[:]:
            if bone.parent is None or bone.parent in result:
                result.append(bone)
                pending.remove(bone)
                progress = True
        if not progress:
            raise RuntimeError("armature contains a cyclic/unresolved hierarchy")
    return result


def prepare_mesh(mesh_obj, target_triangles):
    bpy.context.view_layer.objects.active = mesh_obj
    mesh_obj.select_set(True)
    if mesh_obj.data.shape_keys:
        mesh_obj.shape_key_clear()
    mesh = mesh_obj.data
    material_order = {material.name: index
                      for index, material in enumerate(mesh.materials)}
    mesh.calc_loop_triangles()
    source_triangles = len(mesh.loop_triangles)
    if source_triangles > target_triangles:
        # Decimate material regions independently so the total stays fixed
        # while roughly 450 triangles move from broad body surfaces to hair
        # and the dress/skirt silhouette.
        bpy.ops.object.mode_set(mode="EDIT")
        bpy.ops.mesh.select_all(action="SELECT")
        bpy.ops.mesh.separate(type="MATERIAL")
        bpy.ops.object.mode_set(mode="OBJECT")
        parts = [o for o in bpy.context.selected_objects if o.type == "MESH"]
        facts = []
        for part in parts:
            bpy.context.view_layer.objects.active = part
            bpy.ops.object.material_slot_remove_unused()
            part.data.calc_loop_triangles()
            count = len(part.data.loop_triangles)
            name = part.data.materials[0].name.lower() if part.data.materials else ""
            if "hair" in name or "tops" in name:
                weight = 1.50
            elif "face" in name or "eye" in name:
                weight = 1.00
            else:
                weight = 0.60
            facts.append([part, count, count * weight, 0])
        facts.sort(key=lambda f: material_order.get(
            f[0].data.materials[0].name if f[0].data.materials else "", 999))
        parts = [f[0] for f in facts]
        total_weight = sum(f[2] for f in facts)
        assigned = 0
        allocation_target = target_triangles + 5
        for fact in facts:
            exact = allocation_target * fact[2] / total_weight
            fact[3] = max(1, int(exact))
            assigned += fact[3]
        for fact in sorted(facts, key=lambda f: f[2], reverse=True):
            if assigned >= allocation_target:
                break
            fact[3] += 1
            assigned += 1
        for part, count, _, target in facts:
            if count <= target:
                continue
            bpy.context.view_layer.objects.active = part
            modifier = part.modifiers.new("Rasterfall LOD0", "DECIMATE")
            modifier.decimate_type = "COLLAPSE"
            modifier.ratio = target / count
            modifier.use_collapse_triangulate = True
            bpy.ops.object.modifier_apply(modifier=modifier.name)
        mesh_obj = parts[0]
        for part in parts[1:]:
            bpy.ops.object.select_all(action="DESELECT")
            mesh_obj.select_set(True)
            part.select_set(True)
            bpy.context.view_layer.objects.active = mesh_obj
            bpy.ops.object.join()
    modifier = mesh_obj.modifiers.new("Rasterfall Triangulate", "TRIANGULATE")
    modifier.keep_custom_normals = False
    bpy.ops.object.modifier_apply(modifier=modifier.name)
    mesh = mesh_obj.data
    sharp = mesh.attributes.get("sharp_edge")
    if sharp:
        mesh.attributes.remove(sharp)
    if mesh.has_custom_normals:
        mesh.normals_split_custom_set([(0.0, 0.0, 0.0)] * len(mesh.loops))
    for polygon in mesh.polygons:
        polygon.use_smooth = True
    bpy.ops.object.mode_set(mode="EDIT")
    bpy.ops.mesh.select_all(action="SELECT")
    bpy.ops.mesh.normals_make_consistent(inside=False)
    bpy.ops.object.mode_set(mode="OBJECT")
    mesh.calc_loop_triangles()
    return mesh_obj, source_triangles, len(mesh.loop_triangles)


def guessed_texture(material, source_dir):
    name = material.name.lower() if material else ""
    rules = (
        ("hairback", "Hair2_Tex.png"), ("hair", "Hair1_Tex.png"),
        ("tops", "Clothes_Tex.png"), ("shoes", "Shoes_Tex.png"),
        ("facemouth", "Mouth_tex.png"), ("eyeiris", "Eyes_Tex.png"),
        ("eyehighlight", "EyeHighlights_tex.png"),
        ("eyewhite", "EyeWhite_Tex.png"), ("facebrow", "Eyebrows_tex.png"),
        ("faceeyeline", "Eyelashes_Tex.png"), ("face", "Face_Tex.png"),
        ("body", "Body_Tex.png"),
    )
    for token, filename in rules:
        if token in name:
            path = os.path.join(source_dir, "Textures", filename)
            return path if os.path.isfile(path) else ""
    return ""


def copy_textures(materials, output_dir, source_dir):
    texture_dir = os.path.join(output_dir, "textures")
    os.makedirs(texture_dir, exist_ok=True)
    paths = []
    by_source = {}
    material_texture = []
    for index, material in enumerate(materials):
        image = base_image(material)
        source = bpy.path.abspath(image.filepath) if image else ""
        source = os.path.realpath(source) if source else ""
        if not source or not os.path.isfile(source):
            source = guessed_texture(material, source_dir)
        if not source or not os.path.isfile(source):
            material_texture.append(-1)
            continue
        if source not in by_source:
            extension = os.path.splitext(source)[1].lower() or ".png"
            image_name = image.name if image else os.path.splitext(os.path.basename(source))[0]
            filename = "%03d_%s%s" % (len(paths), safe_name(image_name), extension)
            destination = os.path.join(texture_dir, filename)
            material_name = material.name.lower() if material else ""
            max_size = 1024 if any(token in material_name for token in
                                   ("body", "tops", "face_00")) else 512
            if extension == ".png":
                loaded = bpy.data.images.load(source, check_existing=False)
                width, height = loaded.size
                if max(width, height) > max_size:
                    scale = max_size / max(width, height)
                    loaded.scale(max(1, round(width * scale)),
                                 max(1, round(height * scale)))
                loaded.filepath_raw = destination
                loaded.file_format = "PNG"
                loaded.save()
                bpy.data.images.remove(loaded)
            else:
                shutil.copy2(source, destination)
            by_source[source] = len(paths)
            paths.append("textures/" + filename)
        material_texture.append(by_source[source])
    return paths, material_texture


def main():
    args = arguments()
    source = os.path.abspath(args.input)
    output = os.path.abspath(args.output)
    output_dir = os.path.dirname(output)
    os.makedirs(output_dir, exist_ok=True)

    bpy.ops.wm.read_factory_settings(use_empty=True)
    bpy.ops.import_scene.fbx(filepath=source, use_anim=True,
                             automatic_bone_orientation=False)
    meshes = [o for o in bpy.context.scene.objects if o.type == "MESH"]
    armatures = [o for o in bpy.context.scene.objects if o.type == "ARMATURE"]
    if len(meshes) != 1 or len(armatures) != 1:
        raise RuntimeError("expected one mesh and one armature, got %d/%d" %
                           (len(meshes), len(armatures)))
    mesh_obj, armature = meshes[0], armatures[0]
    source_shapes = (len(mesh_obj.data.shape_keys.key_blocks) - 1
                     if mesh_obj.data.shape_keys else 0)
    mesh_obj, source_triangles, final_triangles = prepare_mesh(
        mesh_obj, args.target_triangles)

    bones = ordered_bones(armature)
    bone_index = {bone.name: i for i, bone in enumerate(bones)}
    exported_bone_names = [SEMANTIC_NAMES.get(b.name, b.name) for b in bones]
    required = set(SEMANTIC_NAMES)
    missing = sorted(required - set(bone_index))
    if missing:
        raise RuntimeError("missing required Mixamo bones: " + ", ".join(missing))

    materials = list(mesh_obj.data.materials)
    if not materials:
        materials = [None]
    if len(materials) > 32:
        raise RuntimeError("RFM2 supports at most 32 material groups")
    texture_paths, material_texture = copy_textures(
        materials, output_dir, os.path.dirname(source))

    mesh = mesh_obj.data
    mesh.calc_loop_triangles()
    world = mesh_obj.matrix_world
    normal_world = world.to_3x3().inverted().transposed()
    uv_layer = mesh.uv_layers.active
    vertices = []
    vertex_lookup = {}
    material_indices = [[] for _ in materials]
    discarded_total = 0.0
    discarded_max = 0.0
    influenced_over_two = 0

    for tri in mesh.loop_triangles:
        material = min(max(tri.material_index, 0), len(materials) - 1)
        for loop_index in tri.loops:
            loop = mesh.loops[loop_index]
            vertex = mesh.vertices[loop.vertex_index]
            normal = normal_world @ mesh.corner_normals[loop_index].vector
            normal.normalize()
            uv = uv_layer.data[loop_index].uv if uv_layer else Vector((0.0, 0.0))
            key = (loop.vertex_index,
                   round(normal.x, 6), round(normal.y, 6), round(normal.z, 6),
                   round(uv.x, 6), round(uv.y, 6))
            runtime_index = vertex_lookup.get(key)
            if runtime_index is None:
                weights = []
                for group in vertex.groups:
                    if group.weight <= 0.0:
                        continue
                    group_name = mesh_obj.vertex_groups[group.group].name
                    if group_name in bone_index:
                        weights.append((group.weight, bone_index[group_name]))
                weights.sort(reverse=True)
                if not weights:
                    weights = [(1.0, 0)]
                if len(weights) > 2:
                    influenced_over_two += 1
                    discarded = sum(w for w, _ in weights[2:])
                    discarded_total += discarded
                    discarded_max = max(discarded_max, discarded)
                weights = weights[:2]
                total = sum(w for w, _ in weights)
                weights = [(w / total, b) for w, b in weights]
                if len(weights) == 1 or weights[1][0] < 1.0 / 65535.0:
                    weights = [(1.0, weights[0][1])]
                position = pmx_space(world @ vertex.co)
                pmx_normal = pmx_space(normal)
                vertices.append((position, pmx_normal, (uv.x, 1.0 - uv.y), weights))
                runtime_index = len(vertices) - 1
                vertex_lookup[key] = runtime_index
            material_indices[material].append(runtime_index)

    armature_world = armature.matrix_world
    bone_positions = [pmx_space(armature_world @ bone.head_local) for bone in bones]

    with open(output, "wb") as out:
        out.write(b"PMX ")
        out.write(struct.pack("<fB8B", 2.0, 8, 1, 0, 4, 4, 4, 4, 4, 4))
        for text in ("Rasterfall Maid", "Rasterfall Maid", "Generated by Blender", "Generated by Blender"):
            out.write(pmx_text(text))
        out.write(struct.pack("<i", len(vertices)))
        for position, normal, uv, weights in vertices:
            out.write(struct.pack("<3f3f2f", *position, *normal, *uv))
            if len(weights) == 1:
                out.write(struct.pack("<B", 0) + pmx_index(weights[0][1]))
            else:
                out.write(struct.pack("<B", 1) + pmx_index(weights[0][1]) +
                          pmx_index(weights[1][1]) + struct.pack("<f", weights[0][0]))
            out.write(struct.pack("<f", 1.0))
        all_indices = [i for group in material_indices for i in group]
        out.write(struct.pack("<i", len(all_indices)))
        for index in all_indices:
            out.write(pmx_index(index))
        out.write(struct.pack("<i", len(texture_paths)))
        for path in texture_paths:
            out.write(pmx_text(path))
        out.write(struct.pack("<i", len(materials)))
        for index, material in enumerate(materials):
            name = material.name if material else "default"
            role = name.lower()
            r, g, b, a = material_color(material)
            face_or_skin = "face" in role or "skin" in role or "eye" in role
            body_skin = "body" in role and "skin" in role
            flags = 0x01
            edge_size = 0.0
            # Alpha-mapped hair cards cannot use the current untextured edge
            # shell: it fills transparent texels and creates internal lines.
            ambient = (0.25, 0.18, 0.16) if body_skin else \
                (0.95, 0.88, 0.82) if face_or_skin else (
                r * 0.35, g * 0.35, b * 0.35)
            specular = (0.0, 0.0, 0.0) if face_or_skin else (0.08, 0.08, 0.08)
            specular_power = 0.0 if face_or_skin else 8.0
            out.write(pmx_text(name) + pmx_text(name))
            out.write(struct.pack("<4f3ff3fB4ff", r, g, b, a,
                                  *specular, specular_power,
                                  *ambient, flags,
                                  0.0, 0.0, 0.0, 1.0, edge_size))
            out.write(pmx_index(material_texture[index]))
            out.write(pmx_index(-1))
            out.write(struct.pack("<BB", 0, 0))
            out.write(pmx_index(-1))
            visual_role = "FACE" if "face" in role else \
                "EYES" if "eye" in role else \
                "HAIR" if "hair" in role else \
                "SKIN" if "skin" in role or "body" in role else \
                "CLOTHING" if any(token in role for token in
                                  ("cloth", "tops", "shoes")) else "NONE"
            out.write(pmx_text("Rasterfall role=" + visual_role))
            out.write(struct.pack("<i", len(material_indices[index])))
        out.write(struct.pack("<i", len(bones)))
        for index, bone in enumerate(bones):
            name = exported_bone_names[index]
            parent = bone_index[bone.parent.name] if bone.parent else -1
            children = [bone_index[c.name] for c in bone.children]
            tail = (bone_positions[children[0]] - bone_positions[index]
                    if children else pmx_space(armature_world.to_3x3() @
                                               (bone.tail_local - bone.head_local)))
            out.write(pmx_text(name) + pmx_text(bone.name))
            out.write(struct.pack("<3f", *bone_positions[index]))
            out.write(pmx_index(parent))
            out.write(struct.pack("<iH3f", 0, 0x0002, *tail))
        # Morphs, display frames, rigid bodies and joints.
        out.write(struct.pack("<4i", 0, 0, 0, 0))

    average_discarded = discarded_total / influenced_over_two if influenced_over_two else 0.0
    print("rasterfall-blender: source_triangles=%d output_triangles=%d runtime_vertices=%d materials=%d bones=%d shape_keys_removed=%d" %
          (source_triangles, final_triangles, len(vertices), len(materials),
           len(bones), source_shapes))
    print("rasterfall-blender: weights_over_two=%d discarded_weight_average=%.6f discarded_weight_max=%.6f normalized_top_two=yes" %
          (influenced_over_two, average_discarded, discarded_max))
    print("rasterfall-blender: wrote", output)


if __name__ == "__main__":
    main()
