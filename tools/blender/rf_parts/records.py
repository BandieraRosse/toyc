"""Semantic fingerprints: Blender file bytes are not stable across saves."""
import array
import hashlib
import json

import bpy


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':'),
                                     allow_nan=False).encode('utf-8')).hexdigest()


def buffer(items, field, width, kind='f'):
    values = array.array(kind, [0]) * (len(items) * width)
    if values:
        items.foreach_get(field, values)
    return hashlib.sha256(values.tobytes()).hexdigest()


def scalar(value):
    if value is None or isinstance(value, (str, bool, int, float)):
        return value
    if isinstance(value, bpy.types.Image):
        # Include packed image content, not an incidental datablock suffix.
        packed = value.packed_file
        return {'image': value.filepath, 'packed': hashlib.sha256(packed.data).hexdigest()
                if packed else None, 'color_space': value.colorspace_settings.name}
    if isinstance(value, bpy.types.ID):
        return value.name
    try:
        return [scalar(v) for v in value]
    except TypeError:
        return str(value)


def properties(obj, exclude=()):
    result = {}
    for prop in obj.bl_rna.properties:
        key = prop.identifier
        if key in {'rna_type', 'name', 'name_full', 'users', 'use_fake_user', 'use_extra_user',
                   'is_updated', 'is_updated_data', 'is_editable', 'is_library_indirect',
                   'is_missing', 'is_embedded_data', 'is_runtime_data', 'tag', 'session_uid',
                   'is_evaluated', 'preview', 'asset_data'} or key in exclude:
            continue
        if prop.type in {'BOOLEAN', 'INT', 'FLOAT', 'STRING', 'ENUM'}:
            result[key] = scalar(getattr(obj, key))
    return result


def material_record(mat):
    if mat is None:
        return None
    result = properties(mat)
    if mat.use_nodes:
        nodes = list(mat.node_tree.nodes)
        result['nodes'] = []
        for node in nodes:
            if node.type == 'GROUP':
                raise ValueError('Nested shader groups require an explicit fingerprint adapter')
            item = {'type': node.bl_idname,
                    'properties': properties(node, {'location', 'width', 'height', 'label',
                                                   'select', 'show_options', 'show_preview',
                                                   'show_texture', 'hide', 'dimensions'}),
                    'inputs': [(s.identifier, scalar(s.default_value))
                               for s in node.inputs if hasattr(s, 'default_value')]}
            if hasattr(node, 'image'):
                item['image'] = scalar(node.image)
            if hasattr(node, 'color_ramp'):
                item['ramp'] = [(e.position, list(e.color)) for e in node.color_ramp.elements]
            result['nodes'].append(item)
        result['links'] = sorted((nodes.index(link.from_node), link.from_socket.identifier,
                                  nodes.index(link.to_node), link.to_socket.identifier)
                                 for link in mat.node_tree.links)
    return result


def mesh_record(obj):
    mesh = obj.data
    mesh.update()
    result = {
        'vertices': buffer(mesh.vertices, 'co', 3),
        'edges': buffer(mesh.edges, 'vertices', 2, 'i'),
        'loops': buffer(mesh.loops, 'vertex_index', 1, 'i'),
        'polygons': [(p.loop_start, p.loop_total, p.material_index, p.use_smooth)
                     for p in mesh.polygons],
        'normals': [tuple(n.vector) for n in mesh.corner_normals],
        'uv': [(uv.name, buffer(uv.data, 'uv', 2)) for uv in mesh.uv_layers],
        'uv_active': mesh.uv_layers.active_index,
        'attributes': {},
        'weights': [[(obj.vertex_groups[g.group].name, g.weight) for g in v.groups]
                    for v in mesh.vertices],
        'materials': [material_record(m) for m in mesh.materials],
        'transform': [list(row) for row in obj.matrix_basis],
        'parent_inverse': [list(row) for row in obj.matrix_parent_inverse],
        'visibility': [obj.hide_render, obj.hide_viewport],
        'constraints': [properties(c) | {'type': c.type} for c in obj.constraints],
        'modifiers': [properties(m) | {'type': m.type} for m in obj.modifiers
                      if m.type != 'ARMATURE'],
        'shape_keys': {},
    }
    if obj.animation_data or mesh.animation_data or (mesh.shape_keys and mesh.shape_keys.animation_data):
        raise ValueError('Part animation/drivers require an explicit contract; keep motion in the rig')
    for attr in mesh.attributes:
        if attr.name in {'position', '.corner_vert', '.edge_verts', '.corner_edge'}:
            continue
        field, width = {'FLOAT': ('value', 1), 'INT': ('value', 1),
                        'BOOLEAN': ('value', 1), 'FLOAT_VECTOR': ('vector', 3),
                        'FLOAT2': ('vector', 2), 'FLOAT_COLOR': ('color', 4),
                        'BYTE_COLOR': ('color', 4)}.get(attr.data_type, (None, 0))
        if field:
            result['attributes'][attr.name] = [attr.domain, attr.data_type,
                buffer(attr.data, field, width, 'i' if attr.data_type in {'INT', 'BOOLEAN'} else 'f')]
        else:
            raise ValueError('Unsupported mesh attribute: ' + attr.data_type)
    if mesh.shape_keys:
        for key in mesh.shape_keys.key_blocks:
            result['shape_keys'][key.name] = {
                'positions': buffer(key.data, 'co', 3), 'value': key.value,
                'relative': key.relative_key.name, 'vertex_group': key.vertex_group,
                'slider': [key.slider_min, key.slider_max], 'mute': key.mute,
                'interpolation': key.interpolation}
    return {key: digest(value) for key, value in result.items()}


def action_record(action):
    curves = []
    for layer in action.layers:
        for strip in layer.strips:
            for slot in action.slots:
                bag = strip.channelbag(slot)
                if bag:
                    for curve in bag.fcurves:
                        curves.append([slot.identifier, curve.data_path, curve.array_index,
                                       curve.extrapolation,
                                       [properties(k) for k in curve.keyframe_points],
                                       [properties(m) for m in curve.modifiers]])
    return {'properties': properties(action), 'curves': curves}


def rig_record(arm):
    return digest({'bones': [(b.name, b.parent.name if b.parent else None,
                             [list(row) for row in b.matrix_local], list(b.head_local),
                             list(b.tail_local), b.use_deform, b.inherit_scale)
                            for b in arm.data.bones],
                   'transform': [list(row) for row in arm.matrix_basis],
                   'actions': {a.name: action_record(a) for a in bpy.data.actions}})


def scene_record(scene):
    return digest({'render': properties(scene.render, {'filepath'}),
                   'cycles': properties(scene.cycles),
                   'view': properties(scene.view_settings),
                   'display': properties(scene.display_settings),
                   'frame': scene.frame_current,
                   'world': material_record(scene.world) if scene.world else None,
                   'objects': {o.name: {'matrix': [list(r) for r in o.matrix_world],
                                       'data': properties(o.data),
                                       'mesh': mesh_record(o) if o.type == 'MESH' else None}
                               for o in scene.objects if o.type in {'CAMERA', 'LIGHT'}
                               or (o.type == 'MESH' and not o.get('part_id'))}})
