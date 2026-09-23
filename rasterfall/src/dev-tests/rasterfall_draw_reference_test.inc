/* Compare meaningful fields rather than padding, unused flat UV values or
 * pointer addresses. Also compare every rendered pixel and signed depth. */
static int draw_test_texture_equal(const struct toy_texture_view *a,
                                  const struct toy_texture_view *b)
{
    if (!a || !b) return a == b;
    return a->width == b->width && a->height == b->height &&
        a->channels == b->channels && a->data_size == b->data_size &&
        a->has_transparency == b->has_transparency &&
        ((!a->data || !b->data) ? a->data == b->data :
         !memcmp(a->data, b->data, a->data_size));
}

static int draw_test_vertex_equal(const struct toy_screen_vertex *a,
    const struct toy_screen_vertex *b, int textured)
{
    if (a->x != b->x || a->y != b->y || a->z != b->z ||
        a->inv_z != b->inv_z || a->light != b->light || a->fog != b->fog) return 0;
    return !textured || (a->u == b->u && a->v == b->v &&
        a->u_over_z == b->u_over_z && a->v_over_z == b->v_over_z &&
        a->u2 == b->u2 && a->v2 == b->v2 &&
        a->u2_over_z == b->u2_over_z && a->v2_over_z == b->v2_over_z);
}

static int draw_test_command_equal(const struct toy_raster_cmd *a,
                                    const struct toy_raster_cmd *b)
{
#define DRAW_EQ(field) if (a->field != b->field) return 0
    DRAW_EQ(textured); DRAW_EQ(planar_vertex_lit); DRAW_EQ(repeat);
    DRAW_EQ(overlay); DRAW_EQ(color); DRAW_EQ(fallback); DRAW_EQ(light); DRAW_EQ(fog);
    DRAW_EQ(blend_mode); DRAW_EQ(toon_shared); DRAW_EQ(toon_level); DRAW_EQ(has_toon);
    DRAW_EQ(toon_multiplier); DRAW_EQ(material_features); DRAW_EQ(base_texture_valid);
    DRAW_EQ(base_texture_bilinear); DRAW_EQ(sphere_texture_valid); DRAW_EQ(material_alpha);
    DRAW_EQ(transparent_no_depth_write); DRAW_EQ(material_ambient); DRAW_EQ(material_specular);
    DRAW_EQ(material_specular_level); DRAW_EQ(material_add); DRAW_EQ(material_tint);
    DRAW_EQ(transparent); DRAW_EQ(edge); DRAW_EQ(area);
    DRAW_EQ(bbox_minx); DRAW_EQ(bbox_maxx); DRAW_EQ(bbox_miny); DRAW_EQ(bbox_maxy);
#undef DRAW_EQ
    return draw_test_texture_equal(a->texture, b->texture) &&
        draw_test_texture_equal(a->texture2, b->texture2) &&
        draw_test_texture_equal(a->texture3, b->texture3) &&
        draw_test_vertex_equal(&a->a, &b->a, a->textured) &&
        draw_test_vertex_equal(&a->b, &b->b, a->textured) &&
        draw_test_vertex_equal(&a->c, &b->c, a->textured);
}

struct actor_range_test_output {
    struct toy_raster_cmd commands[32];
    int count;
};

static int actor_range_test_consume(struct toy_renderer *r,
    const struct toy_raster_cmd *commands, int count, void *context)
{
    struct actor_range_test_output *out = context;
    (void)r;
    if (count > 32 - out->count) return -1;
    memcpy(out->commands + out->count, commands, count * sizeof(*commands));
    out->count += count;
    return 0;
}

static int actor_range_logic_test(void)
{
    struct rasterfall_ai_submission_stats saved = ai_submission_stats;
    int failed = 0;
    /* Both sides of the old begin, equality, and repeated flushes. Compare
     * actual commands against an independent expected stream, including a
     * preceding actor's offscreen command that this scope must not touch. */
    for (int flush_mode=0;flush_mode<2;++flush_mode)
    for (int tail = 1; tail <= 7; tail += 2) {
        struct toy_renderer r;
        struct toy_raster_cmd commands[32], expected[32], cmd;
        struct actor_range_test_output out;
        struct ai_actor_command_scope scope;
        int expected_count = 0;
        memset(&r, 0, sizeof(r)); memset(&out, 0, sizeof(out));
        memset(&cmd, 0, sizeof(cmd));
        r.cmds = commands; r.cmd_cap = 32;
        r.surface.width = r.surface.height = 96;
        r.command_consumer = actor_range_test_consume;
        r.command_consumer_context = &out;
        for (int n = 0; n < 3; n++) {
            cmd.color = 100 + n; cmd.a.x = cmd.b.x = cmd.c.x = -10;
            commands[r.cmd_count++] = cmd;
            expected[expected_count++] = cmd;
        }
        ai_actor_command_scope_begin(&r, &scope);
        for (int chunk = 0; chunk < 3; chunk++) {
            for (int n = 0; n < tail; n++) {
                cmd.color = 200 + chunk * 16 + n;
                cmd.a.x = cmd.b.x = cmd.c.x = n % 2 ? -10 : 10;
                commands[r.cmd_count++] = cmd;
                if (!(n % 2)) expected[expected_count++] = cmd;
            }
            if (flush_mode && chunk != 2 && toy_renderer_flush(&r) < 0) failed = 1;
        }
        ai_actor_command_scope_end(&r, &scope);
        if (toy_renderer_flush(&r) < 0 || out.count != expected_count ||
            r.command_filter || r.command_filter_context ||
            scope.generated != (unsigned long)(3 * tail)) failed = 1;
        for (int n = 0; n < out.count && n < expected_count; n++)
            if (!draw_test_command_equal(&out.commands[n], &expected[n])) failed = 1;
    }
    ai_submission_stats = saved;
    __printf("actor command flush scope: %s\n", failed ? "FAIL" : "PASS");
    return failed;
}

/* Exercise real mesh packing and the production placement, then independently
 * decode the Draw transform against authored world coordinates. Zero tolerance
 * on world/view/screen coordinates; hardware edge coverage is a separate test. */
static int persistent_map_projection_test(void)
{
    static const struct vec3 quads[3][4] = {
        {{-900,-900,3200},{900,-900,3200},{900,500,5600},{-900,500,5600}},
        {{-4200,-900,4200},{-1400,-900,4200},{-1400,2700,4200},{-4200,2700,4200}},
        {{1800,300,3900},{4600,300,3900},{4600,300,5900},{1800,300,5900}}
    };
    int failed = 0, checked = 0;
    for (int shape=0; shape<3; ++shape) {
        struct persistent_map_mesh_build build = {0};
        struct rasterfall_model_asset *model;
        struct rasterfall_draw_instance instance;
        struct rasterfall_resource_handle handle = {0};
        if (persistent_map_mesh_push(&build, quads[shape], 0x596775) < 0) return 1;
        model = persistent_map_mesh_finish(&build);
        tlibc_free(build.patches);
        if (!model) return 1;
        persistent_map_instance(model, handle, &instance);
        for (int angle=0; angle<3; ++angle) {
            struct camera camera = {0};
            struct toy_surface surface = {0};
            camera.y = -350; camera.cy = angle ? 949 : 1024;
            camera.sy = angle == 1 ? -384 : angle == 2 ? 384 : 0;
            camera.pitch_cy = angle == 2 ? 989 : 1024;
            camera.pitch_sy = angle == 2 ? 265 : 0;
            surface.width=1280; surface.height=720;
            for (int k=0; k<4; ++k) {
                const unsigned char *v=model->vertices+k*model->vertex_bytes;
                struct vec3 decoded, a, b;
                struct toy_screen_vertex sa, sb;
                decoded.x=(int)model_u32(v)+(int)model_u32(model->materials+24);
                decoded.y=(int)model_u32(v+4)-model->min_y+instance.y;
                decoded.z=(int)model_u32(v+8)+(int)model_u32(model->materials+28);
                world_to_view(&camera, &quads[shape][k], &a);
                world_to_view(&camera, &decoded, &b);
                project_vertex(&surface,&a,&sa); project_vertex(&surface,&b,&sb);
                if (memcmp(&decoded,&quads[shape][k],sizeof(decoded)) ||
                    memcmp(&a,&b,sizeof(a)) || sa.x!=sb.x || sa.y!=sb.y || sa.inv_z!=sb.inv_z) failed=1;
                if (!angle) __printf("MAP-PROJECTION shape=%d vertex=%d world=(%d,%d,%d) cpu=(%d,%d) draw=(%d,%d)\n",
                    shape,k,decoded.x,decoded.y,decoded.z,sa.x,sa.y,sb.x,sb.y);
                ++checked;
            }
        }
        rasterfall_model_unload(model); tlibc_free(model);
    }
    __printf("persistent map projection: %s vertices=%d\n",failed?"FAIL":"PASS",checked);
    return failed;
}

int rasterfall_render_draw_reference_logic_test(void)
{
    struct toy_renderer r[2];
    struct rasterfall_frontend_state states[2];
    struct toy_surface surfaces[2];
    uint32_t pixels[2][96 * 96];
    /* Six non-planar vertices; both windings and a near-plane intersection. */
    static const int positions[6][3] = {
        {-70, 0, -80}, {0, 110, 80}, {70, 0, 80},
        {-55, 20, 40}, {55, 20, 40}, {0, 95, 40}
    };
    uint32_t vertices[6 * 6] = {0};
    uint32_t indices[12] = {0, 1, 2, 0, 2, 1, 3, 4, 5, 3, 5, 4};
    uint32_t primitives[8] = {0, 6, 0, 0, 6, 6, 1, 0};
    unsigned char materials[80] = {0};
    unsigned char texels[12] = {230, 40, 80, 20, 200, 90, 30, 50, 210, 190, 150, 25};
    struct toy_texture_asset texture_asset;
    struct toy_texture_view texture_view;
    struct rasterfall_model_asset *model = tlibc_malloc(sizeof(*model));
    struct rasterfall_scene_stats saved_stats = scene_stats;
    struct rasterfall_model_render_stats saved_model_stats = model_render_stats;
    int saved_collect = collect_model_render_stats;
    struct camera camera;
    int saved_gallery = active_gallery_lighting, saved_scene = active_scene_light_override_q8;
    int saved_v2 = active_world_light_v2, saved_floor = active_floor_submission;
    int saved_form = active_model_form_lighting, saved_material = active_disable_material_light;
    int result = actor_range_logic_test() | persistent_map_projection_test(), tested = 0, commands = 0, touched = 0;
    if (!model) return 1;
    memset(model, 0, sizeof(*model));
    memset(states, 0, sizeof(states)); memset(surfaces, 0, sizeof(surfaces));
    memset(&camera, 0, sizeof(camera));
    memset(&texture_asset, 0, sizeof(texture_asset));
    memset(&texture_view, 0, sizeof(texture_view));
    camera.cy = camera.pitch_cy = 1024;
    texture_asset.data = texels;
    texture_view.data = texels; texture_view.width = texture_view.height = 2;
    texture_view.channels = 3; texture_view.data_size = sizeof(texels);
    model->vertices = (const unsigned char *)vertices;
    model->indices = (const unsigned char *)indices;
    model->primitives = (const unsigned char *)primitives;
    model->materials = materials; model->material_count = model->primitive_count = 2;
    model->vertex_count = 6; model->index_count = 12;
    model->vertex_bytes = 24; model->material_bytes = 40;
    model->format_version = 9; model->position_scale = 1000;
    model->min_x = -70; model->max_x = 70;
    model->min_y = 0; model->max_y = 110;
    model->min_z = -80; model->max_z = 80;
    model->textures.assets = &texture_asset; model->textures.views = &texture_view;
    model->textures.count = 1;
    for (int i = 0; i < 6; ++i) {
        unsigned char *v = (unsigned char *)vertices + i * 24;
        short n[3] = {(short)(i * 2100 - 5000), 27000, -10000};
        unsigned short uv[2] = {(unsigned short)(i * 11000), (unsigned short)(i * 13000)};
        memcpy(v, positions[i], 12); memcpy(v + 12, n, 6); memcpy(v + 18, uv, 4);
    }
    for (int i = 0; i < 2; ++i) {
        uint32_t color = i ? 0x00d08040U : 0x0060a0d0U;
        uint32_t none = 0xffffffffU;
        memcpy(materials + i * 40, &color, 4);
        materials[i * 40 + 4] = 255; materials[i * 40 + 7] = 1;
        memcpy(materials + i * 40 + 8, &none, 4);
        toy_renderer_init(&r[i]); toy_renderer_set_frame_budget(&r[i], 0);
        r[i].preserve_command_order = 1;
        frontend_set_override(&r[i], &states[i]);
        states[i].toon_shared = -1; states[i].material_alpha = 255;
        states[i].material_double_sided = 1;
        surfaces[i].width = surfaces[i].height = 96; surfaces[i].stride = 96 * 4;
        surfaces[i].pixels = pixels[i];
    }
    active_gallery_lighting = active_model_form_lighting = 1;
    active_disable_material_light = active_world_light_v2 = active_floor_submission = 0;
    collect_model_render_stats = 1;
    memset(&model_render_stats, 0, sizeof(model_render_stats));
    /* Flat, textured, missing texture, material-light, old v2 sidedness;
     * two overlapping instances per frame, signed yaw and non-unit scale. */
    for (int mode = 0; mode < 5 && !result; ++mode)
    for (int near = 0; near < 2 && !result; ++near)
    for (int yaw = 0; yaw < 3 && !result; ++yaw)
    for (int cull = 0; cull < 2 && !result; ++cull) {
        unsigned int texture_index = mode ? 0 : 0xffffffffU;
        uint32_t ambient = mode == 3 ? 0x00040201U : 0;
        uint32_t specular = mode == 3 ? 0x00070503U : 0;
        uint32_t power = 2049;
        model->format_version = mode == 4 ? 2 : 9;
        texture_asset.data = mode == 2 ? NULL : texels;
        memcpy(materials + 8, &texture_index, 4);
        memcpy(materials + 24, &ambient, 4); memcpy(materials + 28, &specular, 4);
        memcpy(materials + 32, &power, 4);
        for (int lane = 0; lane < 2; ++lane)
            if (toy_renderer_begin(&r[lane], &surfaces[lane], 0xff203040U) < 0) result = 2;
        for (int instance_id = 0; instance_id < 2 && !result; ++instance_id) {
            struct rasterfall_prop_instance prop;
            struct rasterfall_draw_view view;
            struct rasterfall_draw_instance instance;
            int sy = yaw == 0 ? 0 : yaw == 1 ? -723 : 1024;
            int cy = yaw == 0 ? 1024 : yaw == 1 ? 723 : 0;
            memset(&prop, 0, sizeof(prop));
            prop.x = instance_id * 17; prop.y = -50;
            prop.z = near ? 105 : 350;
            prop.scale_milli = instance_id ? 1375 : 800;
            active_scene_light_override_q8 = instance_id ? 197 : 256;
            for (int lane = 0; lane < 2; ++lane) {
                states[lane].gallery_facing = 1;
                states[lane].gallery_sy = sy; states[lane].gallery_cy = cy;
                states[lane].force_model_backface_culling = cull;
            }
            static_prop_draw_snapshot(&r[1], &camera, model, &prop, prop.scale_milli,
                                      &view, &instance);
            if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_ACCEPTED) {
                result = 3; break;
            }
            render_gallery_model(&r[0], &camera, model, prop.x, prop.y, prop.z, prop.scale_milli);
            /* Poison the ambient scope after capture. Lowering must use the
             * frozen values and restore all caller state on return. */
            states[1].gallery_sy = 100; states[1].gallery_cy = -900;
            states[1].rigid_transform_enabled = 1;
            active_scene_light_override_q8 = 11; active_gallery_lighting = 0;
            active_model_form_lighting = 0;
            if (static_prop_draw_reference(&r[1], &view, &instance) < 0 ||
                states[1].gallery_sy != 100 || !states[1].rigid_transform_enabled ||
                active_scene_light_override_q8 != 11 || active_gallery_lighting ||
                active_model_form_lighting) result = 4;
            states[1].rigid_transform_enabled = 0;
            active_gallery_lighting = active_model_form_lighting = 1;
        }
        if (r[0].cmd_count != r[1].cmd_count || r[0].cmd_overflow || r[1].cmd_overflow) result = 5;
        for (int i = 0; i < r[0].cmd_count && !result; ++i)
            if (!draw_test_command_equal(&r[0].cmds[i], &r[1].cmds[i])) result = 6;
        commands += r[0].cmd_count;
        if (toy_renderer_flush(&r[0]) < 0 || toy_renderer_flush(&r[1]) < 0) result = 7;
        if (memcmp(pixels[0], pixels[1], sizeof(pixels[0])) ||
            memcmp(r[0].depth, r[1].depth, 96 * 96 * sizeof(*r[0].depth))) result = 8;
        for (int i = 0; i < 96 * 96; ++i) if (r[0].depth[i]) ++touched;
        ++tested;
    }
    /* Reject a later submesh before any new-path command can be emitted. */
    if (!result) {
        struct rasterfall_draw_instance instance;
        struct rasterfall_draw_view view;
        memset(&view, 0, sizeof(view));
        memset(&instance, 0, sizeof(instance)); instance.mesh = model;
        view.camera = camera; view.width = view.height = 96;
        view.focal = 72; view.near_z = 64;
        instance.scale_milli = 1000;
        instance.yaw_cos_q10 = 1024;
        model->format_version = 9; texture_asset.data = texels;
        materials[44] = 128;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_TRANSPARENT) result = 9;
        materials[44] = 255; texture_view.has_transparency = 1;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_TRANSPARENT) result = 10;
        texture_view.has_transparency = 0; materials[6] = 2;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_MATERIAL) result = 11;
        materials[6] = 0; model->skinning_enabled = 1;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_DEFORMATION) result = 12;
        model->skinning_enabled = 0; primitives[5] = 9;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_RANGE) result = 13;
        primitives[5] = 6; instance.x = 100000; instance.z = 64;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_NUMERIC) result = 18;
        instance.x = instance.z = 0; r[1].recording_edge = 1;
        if (static_prop_draw_preflight(&r[1], &view, &instance) != RASTERFALL_DRAW_SCOPE ||
            r[1].cmd_count != 0) result = 15;
        r[1].recording_edge = 0;
        /* Reject stale registry identity before touching borrowed backing. */
        instance.mesh_handle.slot = RASTERFALL_RESOURCE_CAPACITY;
        instance.mesh_handle.generation = 1;
        if (static_prop_draw_reference(&r[1], &view, &instance) != -1 ||
            r[1].cmd_count != 0) result = 17;
    }
    if (!result && (!commands || !touched)) result = 14;
    if (!result && (!model_render_stats.body.near_clipped_triangles ||
        !model_render_stats.body.backface_culled_triangles ||
        scene_stats.static_draw_items - saved_stats.static_draw_items != (unsigned long)tested * 4 ||
        scene_stats.static_draw_lowered_triangles - saved_stats.static_draw_lowered_triangles !=
            (unsigned long)tested * 8)) result = 16;
    for (int lane = 0; lane < 2; ++lane) {
        toy_renderer_destroy(&r[lane]);
        if (states[lane].vertex_cache) tlibc_free(states[lane].vertex_cache);
    }
    tlibc_free(model); scene_stats = saved_stats;
    active_gallery_lighting = saved_gallery; active_scene_light_override_q8 = saved_scene;
    active_world_light_v2 = saved_v2; active_floor_submission = saved_floor;
    active_model_form_lighting = saved_form; active_disable_material_light = saved_material;
    model_render_stats = saved_model_stats; collect_model_render_stats = saved_collect;
    __printf("rasterfall: Draw/reference fixtures=%d commands=%d covered=%d result=%d\n",
             tested, commands, touched, result);
    return result;
}
