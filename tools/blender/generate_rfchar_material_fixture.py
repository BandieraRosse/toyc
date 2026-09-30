"""Generate an original RFCHAR material fixture and optional Blender reference preview.

Run from the repository root with Blender 5.2:
  blender --background --factory-startup --python-exit-code 1 \
    --python tools/blender/generate_rfchar_material_fixture.py -- \
    --output tmp/rfchar-material/fixture.glb \
    --preview tmp/rfchar-material/blender-reference.png

The preview is a Blender reference, not a Rasterfall GPU acceptance image.
"""

import argparse
import importlib.util
import json
import struct
import sys
from pathlib import Path

import bpy
from mathutils import Vector


def arguments():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, default=Path("tmp/rfchar-material/fixture.glb"))
    parser.add_argument("--preview", type=Path, help="Optional Blender reference PNG")
    return parser.parse_args(sys.argv[sys.argv.index("--") + 1:] if "--" in sys.argv else [])


def read_glb(path):
    raw = path.read_bytes()
    if raw[:4] != b"glTF" or struct.unpack_from("<I", raw, 4)[0] != 2:
        raise RuntimeError("Blender did not write a GLB v2 file")
    length, kind = struct.unpack_from("<II", raw, 12)
    if kind != 0x4E4F534A:
        raise RuntimeError("GLB has no JSON chunk")
    return json.loads(raw[20:20 + length])


def normalize_glb(path):
    """Write explicit contract defaults that Blender legitimately leaves implicit."""
    raw = path.read_bytes()
    old_json_length = struct.unpack_from("<I", raw, 12)[0]
    doc = read_glb(path)
    root = next(i for i, node in enumerate(doc["nodes"]) if node.get("name") == "RF_ROOT")
    doc["skins"][0]["skeleton"] = root
    for mat in doc["materials"]:
        if mat.get("alphaMode") == "MASK":
            mat["alphaCutoff"] = 0.5
    encoded = json.dumps(doc, separators=(",", ":"), ensure_ascii=True).encode("ascii")
    encoded += b" " * (-len(encoded) % 4)
    binary_offset = 20 + old_json_length
    binary = raw[binary_offset:]
    path.write_bytes(b"glTF" + struct.pack("<II", 2, 20 + len(encoded) + len(binary))
                     + struct.pack("<II", len(encoded), 0x4E4F534A) + encoded + binary)


def verify_glb(path):
    doc = read_glb(path)
    materials = {m.get("extras", {}).get("rf_material", {}).get("id"): m
                 for m in doc.get("materials", [])}
    expected = {"rf_fixture_body", "rf_fixture_mask", "rf_fixture_double",
                "rf_fixture_face", "rf_fixture_toon"}
    if set(materials) != expected:
        raise RuntimeError("GLB material extras missing or unexpected: " + repr(set(materials)))
    for material_id, material in materials.items():
        meta = material["extras"]["rf_material"]
        if not isinstance(meta, dict) or meta.get("version") != 1:
            raise RuntimeError("Invalid rf_material: " + material_id)
    if materials["rf_fixture_mask"].get("alphaMode") != "MASK":
        raise RuntimeError("Blender failed to export alphaMode=MASK")
    if abs(materials["rf_fixture_mask"].get("alphaCutoff", 0) - 0.5) > 1e-6:
        raise RuntimeError("Blender failed to export alphaCutoff=0.5")
    if not materials["rf_fixture_double"].get("doubleSided"):
        raise RuntimeError("Blender failed to export doubleSided=true")
    if not all(materials[key].get("pbrMetallicRoughness", {}).get("baseColorTexture")
               for key in ("rf_fixture_body", "rf_fixture_mask")):
        raise RuntimeError("GLB is missing a baseColorTexture")
    if not any(image.get("mimeType") == "image/png" for image in doc.get("images", [])):
        raise RuntimeError("GLB is missing an embedded RGBA PNG")
    if doc.get("extensionsUsed") or doc.get("extensionsRequired"):
        raise RuntimeError("Fixture unexpectedly requires a glTF extension")
    sampler_profile = {"magFilter": 9729, "minFilter": 9987,
                       "wrapS": 33071, "wrapT": 33071}
    if not doc.get("samplers") or any(sampler != sampler_profile
                                      for sampler in doc["samplers"]):
        raise RuntimeError("GLB sampler does not match the RF material profile")
    textured = {doc["materials"].index(materials[key]) for key in
                ("rf_fixture_body", "rf_fixture_mask")}
    for mesh in doc.get("meshes", []):
        for primitive in mesh.get("primitives", []):
            if primitive.get("material") in textured and "TEXCOORD_0" not in primitive["attributes"]:
                raise RuntimeError("Textured primitive has no UV0")
    print("material fixture verified:", ", ".join(sorted(expected)))


def image_atlas(path):
    image = bpy.data.images.new("RF Original RGBA Atlas", width=32, height=32, alpha=True)
    pixels = []
    for y in range(32):
        for x in range(32):
            checker = ((x // 4) + (y // 4)) % 2
            alpha = 0.0 if x < 16 and checker else 1.0
            rgb = (0.88, 0.22, 0.12) if x < 16 else (0.08, 0.46, 0.82)
            pixels.extend((*rgb, alpha))
    image.pixels.foreach_set(pixels)
    image.file_format = "PNG"
    image.filepath_raw = str(path)
    image.save()
    image.pack()
    return image


def material(name, material_id, color, extras=None, texture=None, mask=False, double=False):
    mat = bpy.data.materials.new(name)
    mat.use_nodes = True
    mat.use_backface_culling = not double
    mat["rf_material"] = {"version": 1, "id": material_id, **(extras or {})}
    nodes = mat.node_tree.nodes
    shader = nodes.get("Principled BSDF")
    shader.inputs["Base Color"].default_value = (*color, 1.0)
    shader.inputs["Metallic"].default_value = 0.0
    shader.inputs["Roughness"].default_value = 0.8
    if texture:
        node = nodes.new("ShaderNodeTexImage")
        node.image = texture
        node.interpolation = "Linear"
        node.extension = "EXTEND"
        mat.node_tree.links.new(node.outputs["Color"], shader.inputs["Base Color"])
        if mask:
            clip = nodes.new("ShaderNodeMath")
            clip.operation = "GREATER_THAN"
            clip.inputs[1].default_value = 0.5
            mat.node_tree.links.new(node.outputs["Alpha"], clip.inputs[0])
            mat.node_tree.links.new(clip.outputs[0], shader.inputs["Alpha"])
    return mat


def panel(name, vertices, mat, armature, bone):
    mesh = bpy.data.meshes.new(name)
    mesh.from_pydata(vertices, [], [(0, 1, 2), (0, 2, 3)])
    mesh.materials.append(mat)
    uv = mesh.uv_layers.new(name="UVMap")
    coords = ((0, 0), (1, 0), (1, 1), (0, 1))
    for polygon in mesh.polygons:
        for loop_index in polygon.loop_indices:
            uv.data[loop_index].uv = coords[mesh.loops[loop_index].vertex_index]
    obj = bpy.data.objects.new(name, mesh)
    bpy.context.scene.collection.objects.link(obj)
    obj.parent = armature
    group = obj.vertex_groups.new(name=bone)
    group.add([0, 1, 2, 3], 1.0, "REPLACE")
    modifier = obj.modifiers.new("RFCHAR Skin", "ARMATURE")
    modifier.object = armature
    return obj


def reference_preview(path, armature):
    scene = bpy.context.scene
    bpy.ops.object.camera_add(location=(2.9, -4.3, 2.4))
    camera = bpy.context.object
    camera.name = "Blender Reference Camera"
    camera.rotation_euler = (Vector((0, 0, 1.05)) - camera.location).to_track_quat("-Z", "Y").to_euler()
    camera.data.type = "ORTHO"
    camera.data.ortho_scale = 2.8
    scene.camera = camera
    for name, location, power, size in (
        ("Reference Key", (1.5, -2.0, 3.0), 450, 3.0),
        ("Reference Fill", (-2.0, -1.0, 1.5), 220, 2.0),
    ):
        bpy.ops.object.light_add(type="AREA", location=location)
        light = bpy.context.object
        light.name = name
        light.data.energy = power
        light.data.shape = "DISK"
        light.data.size = size
        light.rotation_euler = (Vector((0, 0, 1.0)) - light.location).to_track_quat("-Z", "Y").to_euler()
    scene.render.engine = "BLENDER_EEVEE"
    scene.render.resolution_x = 768
    scene.render.resolution_y = 768
    scene.render.resolution_percentage = 100
    scene.render.image_settings.file_format = "PNG"
    scene.render.filepath = str(path)
    scene.world.color = (0.15, 0.15, 0.15)
    bpy.ops.render.render(write_still=True)
    print("Blender reference preview:", path)


def main():
    args = arguments()
    output = args.output.resolve()
    output.parent.mkdir(parents=True, exist_ok=True)
    if args.preview:
        args.preview = args.preview.resolve()
        args.preview.parent.mkdir(parents=True, exist_ok=True)

    # Run the established public fixture producer to obtain the exact canonical
    # armature, sockets, and baseline skinned body. Keep its intermediate in tmp.
    fixture_path = output.with_name(output.stem + "-canonical-base.glb")
    source = Path(__file__).with_name("generate_rfchar_fixture.py")
    spec = importlib.util.spec_from_file_location("rfchar_fixture", source)
    fixture = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(fixture)
    old_argv = sys.argv
    try:
        sys.argv = [str(source), "--", "--output", str(fixture_path)]
        fixture.main()
    finally:
        sys.argv = old_argv
    fixture_path.unlink()

    armature = bpy.data.objects["RFCHAR_Armature"]
    atlas = image_atlas(output.with_name(output.stem + "-rgba.png"))
    body = material("RF Body UV", "rf_fixture_body", (1, 1, 1), texture=atlas)
    mask = material("RF Mask Cutout", "rf_fixture_mask", (1, 1, 1), texture=atlas, mask=True)
    double = material("RF Double Sided", "rf_fixture_double", (0.12, 0.76, 0.55), double=True,
                      extras={"outline": {"width_px": 2.0, "color": [0.02, 0.02, 0.04]}})
    face = material("RF Face Local Light", "rf_fixture_face", (0.94, 0.66, 0.47),
                    extras={"face_light": {"role": "RF_HEAD", "normal": [0, 0, 1], "strength": 0.7},
                            "toon": {"threshold": 0.5, "softness": 0.08,
                                     "shade_color": [0.65, 0.60, 0.60]}})
    toon = material("RF Toon Outline", "rf_fixture_toon", (0.68, 0.23, 0.82),
                    extras={"toon": {"threshold": 0.55, "softness": 0.06,
                                     "shade_color": [0.38, 0.32, 0.44]},
                            "outline": {"width_px": 3.0, "color": [0.03, 0.02, 0.06]}})
    for object_name, mat in (("BodyMesh", body), ("RightArmMesh", toon), ("LeftLegMesh", double)):
        bpy.data.objects[object_name].data.materials.clear()
        bpy.data.objects[object_name].data.materials.append(mat)
    # Front and rear views expose the alpha cutout and back-face setting.
    panel("Mask Card", [(-.18, -.125, 1.05), (.18, -.125, 1.05),
                        (.18, -.125, 1.37), (-.18, -.125, 1.37)], mask, armature, "RF_CHEST")
    panel("Double Sided Card", [(.25, .01, 1.03), (.55, .01, 1.03),
                                (.55, .01, 1.35), (.25, .01, 1.35)], double, armature, "RF_CHEST")
    panel("Face Card", [(-.12, -.08, 1.65), (.12, -.08, 1.65),
                        (.12, -.08, 1.84), (-.12, -.08, 1.84)], face, armature, "RF_HEAD")

    bpy.ops.object.select_all(action="DESELECT")
    for obj in bpy.context.scene.objects:
        if obj == armature or (obj.type == "MESH" and obj.parent == armature):
            obj.select_set(True)
    bpy.context.view_layer.objects.active = armature
    bpy.ops.export_scene.gltf(filepath=str(output), export_format="GLB", use_selection=True,
        export_skins=True, export_influence_nb=4, export_all_influences=True,
        export_morph=False, export_animations=False, export_extras=True,
        export_yup=True, export_apply=False, export_armature_object_remove=True)
    normalize_glb(output)
    verify_glb(output)
    if args.preview:
        reference_preview(args.preview, armature)
    print("rfchar material fixture:", output)


if __name__ == "__main__":
    main()
