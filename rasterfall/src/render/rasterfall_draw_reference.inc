/* Draw/reference: producer resolves submeshes, reference alone traverses vertices and
 * triangles. This file shares the legacy integer frontend helpers; it is not
 * a Vulkan encoder. resource registry registry pins also protect the lowered RasterCmds. */
static void static_prop_draw_snapshot(struct toy_renderer *renderer,
    const struct camera *camera, const struct rasterfall_model_asset *model,
    const struct rasterfall_prop_instance *prop, int scale,
    struct rasterfall_draw_view *view, struct rasterfall_draw_instance *instance)
{
    memset(view, 0, sizeof(*view));
    memset(instance, 0, sizeof(*instance));
    view->camera = *camera;
    view->width = renderer->surface.width;
    view->height = renderer->surface.height;
    view->focal = view->width * 3 / 4;
    view->near_z = NEAR_Z;
    instance->mesh = model;
    instance->asset_id = prop->asset_id;
    instance->x = prop->x; instance->y = prop->y; instance->z = prop->z;
    instance->scale_milli = scale;
    instance->yaw_sin_q10 = active_gallery_sy;
    instance->yaw_cos_q10 = active_gallery_cy;
    instance->scene_light_q8 = active_scene_light_override_q8 >= 0 ?
        active_scene_light_override_q8 : 256;
    instance->form_lighting = active_model_form_lighting;
    instance->force_backface_culling = frontend_state()->force_model_backface_culling;
    instance->disable_material_light = active_disable_material_light;
}

static enum rasterfall_draw_reject static_prop_draw_resolve(
    const struct rasterfall_draw_instance *instance, unsigned int primitive,
    struct rasterfall_draw_item *draw)
{
    const struct rasterfall_model_asset *model = instance->mesh;
    const unsigned char *p = model->primitives + primitive * RASTERFALL_MODEL_PRIMITIVE_BYTES;
    unsigned int material = model_u32(p + 8);
    const unsigned char *m = material < model->material_count ?
        model->materials + material * model->material_bytes : NULL;
    memset(draw, 0, sizeof(*draw));
    draw->primitive = primitive;
    draw->first_index = model_u32(p);
    draw->index_count = model_u32(p + 4);
    if (draw->first_index > model->index_count ||
        draw->index_count > model->index_count - draw->first_index ||
        draw->index_count % 3) return RASTERFALL_DRAW_RANGE;
    draw->material.color = m ? model_u32(m) : RF_COLOR_UI_TEXT_MUTED;
    draw->material.double_sided = !instance->force_backface_culling &&
        (!m || model->format_version < 7 || (m[7] & 1));
    if (!m) return RASTERFALL_DRAW_ACCEPTED;
    if (model->format_version >= 4 && m[4] != 255)
        return RASTERFALL_DRAW_TRANSPARENT;
    /* Conservative V0 policy. Even disabled special features stay legacy,
     * so eligibility cannot change when a diagnostic scope later changes. */
    if ((model->format_version >= 3 && ((model_u32(m + 12) >> 16) & 3)) ||
        (model->format_version >= 5 && m[6]) ||
        (model->format_version >= 8 && (m[7] & 0x10) && model_u32(m + 20)) ||
        authored_visual_class(model, material) != RASTERFALL_VISUAL_DEFAULT)
        return RASTERFALL_DRAW_MATERIAL;
    if (model->format_version >= 9) {
        draw->material.ambient = instance->disable_material_light ? 0 : model_u32(m + 24);
        draw->material.specular = instance->disable_material_light ? 0 : model_u32(m + 28);
        draw->material.specular_power = (int)model_u32(m + 32);
    }
    if (model->textures.assets) {
        unsigned int texture = model_u32(m + 8);
        if (texture < model->textures.count && model->textures.assets[texture].data) {
            draw->material.texture = &model->textures.views[texture];
            if (draw->material.texture->has_transparency)
                return RASTERFALL_DRAW_TRANSPARENT;
        }
    }
    return RASTERFALL_DRAW_ACCEPTED;
}

static long long static_prop_draw_abs64(long long value)
{
    return value < 0 ? -value : value;
}

/* Match the integer indexed-draw shader's conservative coordinate gate
 * before committing this instance to a hardware Draw. Rejected instances
 * remain eligible for compute RasterCmds in the same strict GPU frame. */
static int static_prop_draw_numeric_eligible(
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance, int integer_depth)
{
    const struct rasterfall_model_asset *model = instance ? instance->mesh : NULL;
    long long bx, by, bz, rx, rz, x, y, z;
    long long cx, cy, cz, axial, depth, ar, dr, min_depth, vx, vz, vy;
    if (!view || !model || instance->scale_milli < 1 ||
        instance->scale_milli > 8000 || view->near_z != 64 ||
        view->focal != view->width * 3 / 4) return 0;
    bx = static_prop_draw_abs64(model->min_x);
    if (static_prop_draw_abs64(model->max_x) > bx) bx = static_prop_draw_abs64(model->max_x);
    by = static_prop_draw_abs64(model->min_y);
    if (static_prop_draw_abs64(model->max_y) > by) by = static_prop_draw_abs64(model->max_y);
    bz = static_prop_draw_abs64(model->min_z);
    if (static_prop_draw_abs64(model->max_z) > bz) bz = static_prop_draw_abs64(model->max_z);
    rx = bx * static_prop_draw_abs64(instance->yaw_cos_q10) +
         bz * static_prop_draw_abs64(instance->yaw_sin_q10);
    rz = bz * static_prop_draw_abs64(instance->yaw_cos_q10) +
         bx * static_prop_draw_abs64(instance->yaw_sin_q10);
    if (rx > INT_MAX || rz > INT_MAX) return 0;
    x = rx / 1024; z = rz / 1024;
    y = by + static_prop_draw_abs64(model->min_y);
    if (x * instance->scale_milli > INT_MAX ||
        y * instance->scale_milli > INT_MAX ||
        z * instance->scale_milli > INT_MAX) return 0;
    x = x * instance->scale_milli / 1000;
    y = y * instance->scale_milli / 1000;
    z = z * instance->scale_milli / 1000;
    if (!integer_depth) {
        /* Scene hardware clipping does not use the integer rasterizer's
         * projected-coordinate envelope. Its vertex transform still uses
         * int32 arithmetic, so prove every camera-transform product/sum. */
        x += static_prop_draw_abs64((long long)instance->x-view->camera.x);
        y += static_prop_draw_abs64((long long)instance->y-view->camera.y);
        z += static_prop_draw_abs64((long long)instance->z-view->camera.z);
        vx = x*static_prop_draw_abs64(view->camera.cy) +
             z*static_prop_draw_abs64(view->camera.sy);
        vz = x*static_prop_draw_abs64(view->camera.sy) +
             z*static_prop_draw_abs64(view->camera.cy);
        if (vx>INT_MAX || vz>INT_MAX) return 0;
        vz /= 1024;
        vy = y*static_prop_draw_abs64(view->camera.pitch_cy) +
             vz*static_prop_draw_abs64(view->camera.pitch_sy);
        depth = y*static_prop_draw_abs64(view->camera.pitch_sy) +
                vz*static_prop_draw_abs64(view->camera.pitch_cy);
        return vy<=INT_MAX && depth<=INT_MAX;
    }
    cx = (long long)instance->x - view->camera.x;
    cy = (long long)instance->y - view->camera.y;
    cz = (long long)instance->z - view->camera.z;
    axial = (cx * view->camera.sy + cz * view->camera.cy) / 1024;
    depth = (cy * view->camera.pitch_sy + axial * view->camera.pitch_cy) / 1024;
    ar = (x * static_prop_draw_abs64(view->camera.sy) +
          z * static_prop_draw_abs64(view->camera.cy) + 1023) / 1024;
    dr = (y * static_prop_draw_abs64(view->camera.pitch_sy) +
          ar * static_prop_draw_abs64(view->camera.pitch_cy) + 1023) / 1024;
    min_depth = depth - dr - 4;
    if (min_depth < view->near_z) min_depth = view->near_z;
    x += static_prop_draw_abs64(cx); y += static_prop_draw_abs64(cy);
    z += static_prop_draw_abs64(cz);
    vx = (x * static_prop_draw_abs64(view->camera.cy) +
          z * static_prop_draw_abs64(view->camera.sy)) / 1024;
    vz = (x * static_prop_draw_abs64(view->camera.sy) +
          z * static_prop_draw_abs64(view->camera.cy)) / 1024;
    vy = (y * static_prop_draw_abs64(view->camera.pitch_cy) +
          vz * static_prop_draw_abs64(view->camera.pitch_sy)) / 1024;
    return vx * view->focal / min_depth + view->width / 2 <= 16384 &&
           vy * view->focal / min_depth + view->height / 2 <= 16384;
}

static enum rasterfall_draw_reject static_prop_draw_preflight(
    struct toy_renderer *renderer, const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance)
{
    const struct rasterfall_model_asset *model = instance->mesh;
    unsigned int i;
    if (active_rigid_transform_enabled || active_infected_model ||
        active_character_palette_override || active_world_light_v2 ||
        floor_submission || renderer->recording_edge ||
        gallery_model_has_texture(model))
        return RASTERFALL_DRAW_SCOPE;
    if (model->skinning_enabled || model->bone_count || model->has_character_contract ||
        character_perceptual_model(model) != CHARACTER_PERCEPTUAL_NONE)
        return RASTERFALL_DRAW_DEFORMATION;
    if (!static_prop_draw_numeric_eligible(view, instance, 1))
        return RASTERFALL_DRAW_NUMERIC;
    for (i = 0; i < model->primitive_count; ++i) {
        struct rasterfall_draw_item draw;
        enum rasterfall_draw_reject result = static_prop_draw_resolve(instance, i, &draw);
        if (result != RASTERFALL_DRAW_ACCEPTED) return result;
    }
    return RASTERFALL_DRAW_ACCEPTED;
}

static int static_prop_draw_lower_item(struct toy_renderer *renderer,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    const struct rasterfall_draw_item *draw)
{
    /* The first submitted submesh prepares the instance once. Keep even
     * vertex preparation behind the consumer boundary: a future hardware
     * consumer must receive this same Draw before doing any CPU geometry. */
    if (draw->primitive == 0) {
        long phase = render_monotonic_us();
        if (prepare_gallery_vertex_cache(renderer, instance->mesh, &view->camera,
                instance->x, instance->y, instance->z, instance->scale_milli,1) < 0)
            return -1;
        skin_trace_capture(renderer, instance->mesh);
        model_setup_timing.vertex_cache_us += render_monotonic_us() - phase;
    }
    active_texture_view = draw->material.texture;
    active_material_alpha = 255;
    active_material_double_sided = draw->material.double_sided;
    active_material_ambient = draw->material.ambient;
    active_material_specular = draw->material.specular;
    active_material_specular_power = draw->material.specular_power;
    active_material_tint = 0xffffffU;
    active_sphere_texture = active_toon_texture = NULL;
    active_sphere_mode = 0; active_toon_shared = -1;
    active_face_material = active_skin_material = 0;
    active_material_lighting_min_q8 = 0;
    active_material_lighting_max_q8 = 256;
    toy_renderer_set_base_texture_bilinear(renderer, 0);
    active_model_triangle_stats = collect_model_render_stats ? &model_render_stats.body : NULL;
    scene_stats.static_draw_items++;
    scene_stats.static_draw_triangles += draw->index_count / 3;
    scene_stats.static_draw_lowered_triangles += draw->index_count / 3;
    return lower_gallery_triangles(renderer, &view->camera, instance->mesh,
        instance->x, instance->y, instance->z, instance->scale_milli,
        instance->mesh->indices + draw->first_index * 4, 0, draw->index_count,
        0, draw->material.texture != NULL, draw->material.color);
}

/* Normal producer entry for an ordered mixed frame. The caller installs a
 * Core Raster consumer before binding mixed_frame; flushing establishes the
 * exact Raster-before-Draw boundary without preparing mesh vertices. */
static int static_prop_draw_mixed(struct toy_renderer *renderer,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance,
    struct rf_core_mixed_frame *frame)
{
    unsigned int i;
    long phase;
    if (!frame || !instance->mesh ||
        renderer->command_consumer != rf_core_mixed_world_consume ||
        renderer->command_consumer_context != frame) {
        __fprintf(2, "mixed static prop: WORLD consumer mismatch\n");
        return -1;
    }
    if (!gallery_model_visible(&renderer->surface, &view->camera, instance->mesh,
            instance->x, instance->y, instance->z, instance->scale_milli, 1,
            instance->yaw_sin_q10, instance->yaw_cos_q10)) {
        scene_stats.models_tested++;
        scene_stats.models_culled++;
        scene_stats.model_triangles_culled += instance->mesh->index_count / 3;
        return 0;
    }
    if (rf_core_mixed_require_draws(frame, instance->mesh->primitive_count) < 0) {
        __fprintf(2, "mixed static prop: Draw requirement failed\n");
        return -1;
    }
    phase = render_monotonic_us();
    /* Consecutive Draw instances have no intervening RasterCmds. An empty
     * flush still performs watchdog/timing bookkeeping, so only establish a
     * mixed ordering boundary when there is actually a Raster prefix. */
    if (renderer->cmd_count > 0 && toy_renderer_flush(renderer) < 0) {
        __fprintf(2, "mixed static prop: preceding raster flush failed\n");
        return -1;
    }
    scene_stats.static_draw_flush_us += render_monotonic_us() - phase;
    scene_stats.models_tested++;
    phase = render_monotonic_us();
    for (i = 0; i < instance->mesh->primitive_count; ++i) {
        struct rasterfall_draw_item draw;
        if (static_prop_draw_resolve(instance, i, &draw) != RASTERFALL_DRAW_ACCEPTED ||
            rf_core_mixed_draw(frame, view, instance, &draw) < 0) {
            __fprintf(2, "mixed static prop: Draw %u rejected\n", i);
            return -1;
        }
        scene_stats.static_draw_items++;
        scene_stats.static_draw_triangles += draw.index_count / 3;
    }
    scene_stats.static_draw_submit_us += render_monotonic_us() - phase;
    return 0;
}

static int static_prop_draw_reference(struct toy_renderer *renderer,
    const struct rasterfall_draw_view *view,
    const struct rasterfall_draw_instance *instance)
{
    struct rasterfall_frontend_state saved = *frontend_state();
    int saved_gallery = active_gallery_lighting;
    int saved_scene = active_scene_light_override_q8;
    int saved_form = active_model_form_lighting;
    int saved_v2 = active_world_light_v2, saved_floor = floor_submission;
    int saved_infected = active_infected_model;
    int saved_bilinear = renderer->recording_base_texture_bilinear;
    int drawn = 0, overflow = renderer->cmd_overflow;
    unsigned int i;
    long start, phase;
    if (!instance->mesh) return -1;
    if (instance->mesh_handle.generation &&
        rasterfall_resources_resolve(rasterfall_render_resources(),
            instance->mesh_handle) != instance->mesh) return -1;
    /* Resizing between snapshot and consumption is not legal in Draw V0. */
    if (view->width != renderer->surface.width || view->height != renderer->surface.height ||
        view->focal != view->width * 3 / 4 || view->near_z != NEAR_Z) return -1;
    scene_stats.models_tested++;
    if (!gallery_model_visible(&renderer->surface, &view->camera, instance->mesh,
            instance->x, instance->y, instance->z, instance->scale_milli, 1,
            instance->yaw_sin_q10, instance->yaw_cos_q10)) {
        scene_stats.models_culled++;
        scene_stats.model_triangles_culled += instance->mesh->index_count / 3;
        return 0;
    }
    start = render_monotonic_us();
    active_gallery_facing = 1;
    active_gallery_sy = instance->yaw_sin_q10;
    active_gallery_cy = instance->yaw_cos_q10;
    active_rigid_transform_enabled = 0;
    active_gallery_lighting = 1;
    active_scene_light_override_q8 = instance->scene_light_q8;
    active_model_form_lighting = instance->form_lighting;
    active_world_light_v2 = floor_submission = active_infected_model = 0;
    for (i = 0; i < instance->mesh->primitive_count; ++i) {
        struct rasterfall_draw_item draw;
        int lowered;
        long vertex_before;
        phase = render_monotonic_us();
        /* Preflight already resolved every material before any submission.
         * Draw/reference borrows immutable backing for this synchronous loop. */
        if (static_prop_draw_resolve(instance, i, &draw) != RASTERFALL_DRAW_ACCEPTED) {
            drawn = -1; break;
        }
        model_setup_timing.material_us += render_monotonic_us() - phase;
        phase = render_monotonic_us();
        vertex_before = model_setup_timing.vertex_cache_us;
        lowered = static_prop_draw_lower_item(renderer, view, instance, &draw);
        model_setup_timing.body_triangles_us += render_monotonic_us() - phase -
            (model_setup_timing.vertex_cache_us - vertex_before);
        if (lowered < 0) { drawn = -1; break; }
        drawn += lowered;
    }
    model_setup_timing.total_us += render_monotonic_us() - start;
    if (collect_model_render_stats)
        model_render_stats.command_overflow += renderer->cmd_overflow - overflow;
    /* Cache allocation and diagnostics are reference-owned, not scoped
     * material state. Retain them when restoring the caller's frontend. */
    saved.vertex_cache = gallery_vertex_cache;
    saved.vertex_cache_capacity = gallery_vertex_cache_capacity;
    saved.skinned_model = frontend_state()->skinned_model;
    saved.skinned_vertices_valid = frontend_state()->skinned_vertices_valid;
    saved.timing = model_setup_timing;
    *frontend_state() = saved;
    active_gallery_lighting = saved_gallery;
    active_scene_light_override_q8 = saved_scene;
    active_model_form_lighting = saved_form;
    active_world_light_v2 = saved_v2; floor_submission = saved_floor;
    active_infected_model = saved_infected;
    toy_renderer_set_base_texture_bilinear(renderer, saved_bilinear);
    return drawn;
}
