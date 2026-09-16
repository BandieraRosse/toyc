/* Fixed world submission, no simulation/present/pacing or screenshot IO.
 * Ablations are diagnostic-only and never select V1 lighting. */
static unsigned long long world_benchmark_hash(const void *data, size_t size)
{
    const unsigned char *p = data;
    unsigned long long hash = 1469598103934665603ULL;
    while (size--) { hash ^= *p++; hash *= 1099511628211ULL; }
    return hash;
}

int rasterfall_render_world_benchmark(int iterations)
{
    static const int counts[] = {0, 10, 30, 60};
    static const char *modes[] = {"normal", "generic-planar",
        "constant-world", "flat-planar", "no-planar-v2", "legacy-enemies",
        "no-actors"};
    struct toy_game *saved, *fixture;
    struct toy_renderer renderer;
    struct toy_surface surface;
    struct camera camera;
    uint32_t *ab_pixels = NULL;
    int *ab_depth = NULL;
    size_t pixel_count = 1280U * 720U;
    int saved_family = enemy_visual_family, result = 1;
    if (!active_session || iterations < 1 || iterations > 1000) return 1;
    saved = tlibc_malloc(sizeof(game));
    if (!saved) return 1;
    *saved = game;
    fixture=tlibc_malloc(sizeof(game));
    if(!fixture) { tlibc_free(saved); return 1; }
    memset(&surface, 0, sizeof(surface));
    surface.width=1280; surface.height=720; surface.stride=1280*4;
    surface.pixels=tlibc_malloc((size_t)surface.stride*surface.height);
    ab_pixels=tlibc_malloc(pixel_count*sizeof(*ab_pixels));
    ab_depth=tlibc_malloc(pixel_count*sizeof(*ab_depth));
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer,0);
    if (!surface.pixels || !ab_pixels || !ab_depth) goto cleanup;
    __printf("WORLD-PERF size=1280x720 iterations=%d actors=normal-map family=%d excludes=logic,begin,IO,present,pacing\n",iterations,saved_family);
    for (int view=0; view<2; view++) for (int n=0; n<4; n++) {
        game=*saved;
        memset(game.enemies,0,sizeof(game.enemies));
        memset(&camera,0,sizeof(camera));
        camera.z=-3400; camera.y=-350; camera.cy=camera.pitch_cy=1024;
        /* Same crowd seen near and mid; all entries use normal visibility. */
        if (view) camera.z=-8400;
        for (int e=0;e<counts[n] && e<TOY_GAME_MAX_ENEMIES;e++) {
            struct toy_game_enemy *enemy=&game.enemies[e];
            enemy->active=1; enemy->hp=100;
            enemy->type=e%10==8 ? TOY_GAME_ENEMY_PURSUIT_FAST :
                e%10==9 ? TOY_GAME_ENEMY_PURSUIT_HEAVY : TOY_GAME_ENEMY_PURSUIT_COMMON;
            enemy->x=(e%10-5)*600+300; enemy->z=-1800+(e/10)*800;
            enemy->ground_y=0; /* Flat spawn approach fixture. */
            enemy->dir_z=-1024;
        }
        *fixture=game;
        long logic=0;
        for(int tick=0;tick<iterations+2;tick++) {
            game=*fixture; game.state=TOY_GAME_PLAYING;
            long start=render_monotonic_us();
            toy_game_update_world(&game,16);
            if(tick>=2) logic+=render_monotonic_us()-start;
        }
        game=*fixture;
        __printf("WORLD-LOGIC view=%s enemies=%d world_tick_us=%ld dt_ms=16 snapshot_reset=outside-timer excludes=session,network,effects\n",view ? "mid" : "near",counts[n],logic/iterations);
        for (int mode=0;mode<7;mode++) {
            long frame=0,scene=0,enemies=0,actors=0,raster=0,sky=0,map=0,props=0,private=0;
            long sort=0,classify=0,enemy_skin=0,enemy_vertex=0,enemy_body=0,enemy_bones=0;
            long planar_us=0,tex_us=0;
            unsigned long commands=0,enemy_commands=0,actor_commands=0,bbox=0,inside=0,tex=0;
            unsigned long planar_tris=0,planar_constant_tris=0,planar_px=0,fallback_cmds=0;
            diagnostic_generic_planar=mode==1;
            diagnostic_constant_world=mode==2;
            diagnostic_flat_planar=mode==3;
            diagnostic_no_planar_v2=mode==4;
            rasterfall_render_set_enemy_visual_family(mode==5 ? RASTERFALL_ENEMY_VISUAL_LEGACY : saved_family);
            for (int repeat=0;repeat<iterations+2;repeat++) {
                if(toy_renderer_begin(&renderer,&surface,0x151922)<0) goto cleanup;
                long start=render_monotonic_us();
                int local=rasterfall_render_begin_dynamic_lighting();
                rasterfall_render_scene(&renderer,&camera);
                rasterfall_render_flags(&renderer,&camera);
                long scene_end=render_monotonic_us();
                int before=renderer.cmd_count;
                struct rasterfall_model_setup_timing timing_before=rasterfall_render_frontend_current(&renderer)->timing;
                rasterfall_render_enemies(&renderer,&camera);
                struct rasterfall_model_setup_timing timing_after=rasterfall_render_frontend_current(&renderer)->timing;
                int ec=renderer.cmd_count-before;
                long enemy_end=render_monotonic_us();
                before=renderer.cmd_count;
                if(mode!=6) rasterfall_render_ai_teammate(&renderer,&camera);
                int ac=renderer.cmd_count-before;
                rasterfall_render_end_dynamic_lighting();
                long actor_end=render_monotonic_us();
                int cmd=renderer.cmd_count;
                if(toy_renderer_flush(&renderer)<0) goto cleanup;
                long rt=render_monotonic_us()-actor_end;
                unsigned long bp=renderer.last_bbox_px,ip=renderer.last_inside_px,tp=renderer.last_tex_tris;
                unsigned long pp=renderer.last_planar_px,pt=renderer.last_planar_tris;
                unsigned long pct=renderer.last_planar_constant_tris;
                unsigned long fc=renderer.last_texture_fallback_cmds;
                long pu=renderer.last_planar_us,tu=renderer.last_tex_us;
                long st=renderer.last_sort_us,cl=renderer.last_classify_us;
                rasterfall_viewmodel_render(&renderer,&game,active_effects,local);
                cmd+=renderer.cmd_count;
                long rs=render_monotonic_us();
                if(toy_renderer_flush(&renderer)<0) goto cleanup;
                rt+=render_monotonic_us()-rs;
                long frame_elapsed=render_monotonic_us()-start;
                if (repeat == 2 && mode == 0) {
                    memcpy(ab_pixels, surface.pixels,
                           pixel_count * sizeof(*ab_pixels));
                    memcpy(ab_depth, renderer.depth,
                           pixel_count * sizeof(*ab_depth));
                } else if (repeat == 2 && mode == 1) {
                    unsigned long diff_px=0,diff_depth=0;
                    int max_rgb=0,max_depth=0;
                    uint32_t *actual=surface.pixels;
                    for (size_t at=0;at<pixel_count;at++) {
                        uint32_t old=ab_pixels[at],now=actual[at];
                        if (old != now) {
                            int dr=abs((int)((old>>16)&255)-(int)((now>>16)&255));
                            int dg=abs((int)((old>>8)&255)-(int)((now>>8)&255));
                            int db=abs((int)(old&255)-(int)(now&255));
                            int error=dr>dg ? (dr>db?dr:db) : (dg>db?dg:db);
                            diff_px++;
                            if(error>max_rgb) max_rgb=error;
                        }
                        if(ab_depth[at]!=renderer.depth[at]) {
                            int error=abs(ab_depth[at]-renderer.depth[at]);
                            diff_depth++;
                            if(error>max_depth) max_depth=error;
                        }
                    }
                    __printf("WORLD-PLANAR-AB view=%s enemies=%d pixels=%lu differing_pixels=%lu max_rgb_error=%d framebuffer_fast_hash=%016llx framebuffer_generic_hash=%016llx differing_depth=%lu max_depth_error=%d depth_fast_hash=%016llx depth_generic_hash=%016llx\n",
                        view?"mid":"near",counts[n],(unsigned long)pixel_count,
                        diff_px,max_rgb,
                        world_benchmark_hash(ab_pixels,pixel_count*sizeof(*ab_pixels)),
                        world_benchmark_hash(actual,pixel_count*sizeof(*actual)),
                        diff_depth,max_depth,
                        world_benchmark_hash(ab_depth,pixel_count*sizeof(*ab_depth)),
                        world_benchmark_hash(renderer.depth,pixel_count*sizeof(*ab_depth)));
                }
                if(repeat>=2) {
                    frame+=frame_elapsed; scene+=scene_end-start;
                    enemy_skin+=timing_after.skinning_us-timing_before.skinning_us;
                    enemy_vertex+=timing_after.vertex_cache_us-timing_before.vertex_cache_us;
                    enemy_body+=timing_after.body_triangles_us-timing_before.body_triangles_us;
                    enemy_bones+=timing_after.bone_hierarchy_us-timing_before.bone_hierarchy_us;
                    enemies+=enemy_end-scene_end; actors+=actor_end-enemy_end; raster+=rt;
                    sky+=scene_stats.sky_floor_us; map+=scene_stats.map_us;
                    props+=scene_stats.gallery_us; private+=scene_stats.private_model_us;
                    commands+=cmd; enemy_commands+=ec; actor_commands+=ac;
                    bbox+=bp; inside+=ip; tex+=tp; sort+=st; classify+=cl;
                    planar_tris+=pt; planar_constant_tris+=pct;
                    planar_px+=pp; fallback_cmds+=fc;
                    planar_us+=pu; tex_us+=tu;
                }
            }
            __printf("WORLD-PERF view=%s enemies=%d mode=%s frame_us=%ld scene_us=%ld sky_floor_us=%ld map_us=%ld props_gallery_us=%ld private_us=%ld enemy_us=%ld enemy_skin_us=%ld enemy_vertex_us=%ld enemy_body_us=%ld enemy_bones_us=%ld actor_us=%ld raster_us=%ld sort_us=%ld classify_us=%ld commands=%lu enemy_commands=%lu actor_commands=%lu bbox_px=%lu inside_px=%lu planar_tris=%lu planar_constant_tris=%lu planar_px=%lu planar_us=%ld tex_tris=%lu texture_fallback_cmds=%lu tex_us=%ld queries=%d workers=%d\n",
                view ? "mid" : "near",counts[n],modes[mode],frame/iterations,scene/iterations,
                sky/iterations,map/iterations,props/iterations,private/iterations,enemies/iterations,enemy_skin/iterations,enemy_vertex/iterations,
                enemy_body/iterations,enemy_bones/iterations,actors/iterations,raster/iterations,sort/iterations,classify/iterations,commands/iterations,
                enemy_commands/iterations,actor_commands/iterations,bbox/iterations,inside/iterations,
                planar_tris/iterations,planar_constant_tris/iterations,
                planar_px/iterations,planar_us/iterations,
                tex/iterations,fallback_cmds/iterations,tex_us/iterations,
                dynamic_world_light_queries,renderer.worker_count);
        }
    }
    result=0;
cleanup:
    diagnostic_constant_world=diagnostic_flat_planar=diagnostic_no_planar_v2=0;
    diagnostic_generic_planar=0;
    rasterfall_render_end_dynamic_lighting();
    rasterfall_render_set_enemy_visual_family(saved_family);
    game=*saved;
    toy_renderer_destroy(&renderer);
    tlibc_free(ab_depth); tlibc_free(ab_pixels); tlibc_free(surface.pixels);
    tlibc_free(fixture); tlibc_free(saved);
    return result;
}

struct gpu_world_capture {
    struct toy_raster_cmd *selected;
    int selected_count, selected_capacity, failed;
    unsigned long total, flat_opaque, planar_solid, textured, textured_supported, vertex_lit;
    unsigned long transparent, overlay, edge, other;
    unsigned long long selected_bbox_pixels;
    const struct toy_texture_view *texture_views[64];
    unsigned long long texture_bytes;
    unsigned long long largest_texture_bytes;
    unsigned int texture_count, nearest_count, bilinear_count;
    unsigned int repeat_count, clamp_count, texture_rgb_count, texture_rgba_count;
    unsigned int texture_material_count, texture_invalid_count;
    unsigned int texture_vertex_light_count, texture_vertex_fog_count;
    unsigned long long max_abs_u_over_z, max_abs_v_over_z;
    long classify_us;
};

static void gpu_world_observe(const struct toy_raster_cmd *commands, int count,
                              void *opaque)
{
    struct gpu_world_capture *capture = opaque;
    long start = render_monotonic_us();
    for (int i = 0; i < count; ++i) {
        const struct toy_raster_cmd *cmd = &commands[i];
        int texture_supported = cmd->textured && cmd->base_texture_valid &&
            !cmd->base_texture_bilinear && cmd->texture &&
            !cmd->texture->has_transparency && !cmd->material_features &&
            !cmd->has_toon && !cmd->texture2 && !cmd->texture3 &&
            !cmd->material_add && cmd->material_tint == 0x00ffffffU &&
            cmd->light >= 0 && cmd->fog >= 0 &&
            cmd->a.u_over_z >= -2147483647L-1 && cmd->a.u_over_z <= 2147483647L &&
            cmd->a.v_over_z >= -2147483647L-1 && cmd->a.v_over_z <= 2147483647L &&
            cmd->b.u_over_z >= -2147483647L-1 && cmd->b.u_over_z <= 2147483647L &&
            cmd->b.v_over_z >= -2147483647L-1 && cmd->b.v_over_z <= 2147483647L &&
            cmd->c.u_over_z >= -2147483647L-1 && cmd->c.u_over_z <= 2147483647L &&
            cmd->c.v_over_z >= -2147483647L-1 && cmd->c.v_over_z <= 2147483647L;
        int supported = (!cmd->textured || texture_supported) &&
            !cmd->transparent && !cmd->overlay && !cmd->edge &&
            cmd->material_alpha == 255 && cmd->area < 0;
        capture->total++;
        if (cmd->edge) capture->edge++;
        else if (cmd->overlay) capture->overlay++;
        else if (cmd->transparent || cmd->material_alpha != 255)
            capture->transparent++;
        else if (cmd->textured) { capture->textured++; if (texture_supported) capture->textured_supported++; }
        else if (cmd->planar_vertex_lit) {
            capture->planar_solid++;
            capture->vertex_lit++;
        } else if (supported) capture->flat_opaque++;
        else capture->other++;
        if (cmd->textured && !cmd->transparent && !cmd->overlay && !cmd->edge &&
            cmd->material_alpha == 255) {
            unsigned int found = 0, t;
            unsigned long long bytes = cmd->texture ? cmd->texture->data_size : 0;
            if (cmd->base_texture_bilinear) capture->bilinear_count++;
            else capture->nearest_count++;
            if (cmd->repeat) capture->repeat_count++;
            else capture->clamp_count++;
            if (!cmd->base_texture_valid) capture->texture_invalid_count++;
            if (cmd->material_features || cmd->has_toon || cmd->texture2 ||
                cmd->texture3 || cmd->material_add ||
                cmd->material_tint != 0x00ffffffU)
                capture->texture_material_count++;
            if (cmd->light < 0) capture->texture_vertex_light_count++;
            if (cmd->fog < 0) capture->texture_vertex_fog_count++;
            {
                const struct toy_screen_vertex *vertices[3] = {
                    &cmd->a, &cmd->b, &cmd->c
                };
                for (t = 0; t < 3; ++t) {
                    unsigned long long au = vertices[t]->u_over_z < 0 ?
                        (unsigned long long)(-(vertices[t]->u_over_z + 1)) + 1 :
                        (unsigned long long)vertices[t]->u_over_z;
                    unsigned long long av = vertices[t]->v_over_z < 0 ?
                        (unsigned long long)(-(vertices[t]->v_over_z + 1)) + 1 :
                        (unsigned long long)vertices[t]->v_over_z;
                    if (au > capture->max_abs_u_over_z)
                        capture->max_abs_u_over_z = au;
                    if (av > capture->max_abs_v_over_z)
                        capture->max_abs_v_over_z = av;
                }
            }
            if (cmd->texture && cmd->texture->channels == 4)
                capture->texture_rgba_count++;
            else if (cmd->texture) capture->texture_rgb_count++;
            for (t = 0; t < capture->texture_count; ++t)
                if (capture->texture_views[t] == cmd->texture) found = 1;
            if (!found && cmd->texture && capture->texture_count < 64) {
                capture->texture_views[capture->texture_count++] = cmd->texture;
                capture->texture_bytes += bytes;
                if (bytes > capture->largest_texture_bytes)
                    capture->largest_texture_bytes = bytes;
            }
        }
        if (!supported) continue;
        if (capture->selected_count == capture->selected_capacity) {
            int new_capacity = capture->selected_capacity ?
                capture->selected_capacity * 2 : 1024;
            struct toy_raster_cmd *grown = tlibc_malloc(
                (size_t)new_capacity * sizeof(*grown));
            if (!grown) { capture->failed = 1; break; }
            if (capture->selected_count)
                memcpy(grown, capture->selected,
                    (size_t)capture->selected_count * sizeof(*grown));
            tlibc_free(capture->selected);
            capture->selected = grown;
            capture->selected_capacity = new_capacity;
        }
        capture->selected[capture->selected_count++] = *cmd;
        capture->selected_bbox_pixels +=
            (unsigned long long)(cmd->bbox_maxx - cmd->bbox_minx + 1) *
            (unsigned long long)(cmd->bbox_maxy - cmd->bbox_miny + 1);
    }
    capture->classify_us += render_monotonic_us() - start;
}

int rasterfall_render_gpu_world_capture(const char *view, int enemy_count,
                                        const char *output_path)
{
    struct gpu_world_capture capture;
    struct toy_renderer renderer, packed_renderer;
    struct toy_surface surface;
    struct camera camera;
    struct toy_game *saved = NULL;
    unsigned char *stream = NULL;
    struct rf_gpu_texture_resources_v1 resources;
    uint32_t texture_count = 0;
    size_t texture_bytes = 0;
    size_t stream_capacity, written = 0;
    long frontend_start, frontend_us, pack_start, pack_us;
    int fd = -1, result = 1;
    const char *failure = "invalid arguments";
    if (!active_session || !view || !output_path ||
        (strcmp(view, "near") && strcmp(view, "mid")) ||
        (enemy_count != 0 && enemy_count != 30)) return 1;
    memset(&capture, 0, sizeof(capture));
    memset(&renderer, 0, sizeof(renderer));
    memset(&packed_renderer, 0, sizeof(packed_renderer));
    memset(&surface, 0, sizeof(surface));
    memset(&camera, 0, sizeof(camera));
    memset(&resources, 0, sizeof(resources));
    failure = "allocate game snapshot";
    saved = tlibc_malloc(sizeof(game));
    if (!saved) goto cleanup;
    *saved = game;
    memset(game.enemies, 0, sizeof(game.enemies));
    surface.width = 1280; surface.height = 720; surface.stride = 1280 * 4;
    surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!surface.pixels) { failure = "allocate surface"; goto cleanup; }
    camera.z = !strcmp(view, "mid") ? -8400 : -3400;
    camera.y = -350; camera.cy = camera.pitch_cy = 1024;
    for (int e = 0; e < enemy_count; ++e) {
        struct toy_game_enemy *enemy = &game.enemies[e];
        enemy->active = 1; enemy->hp = 100;
        enemy->type = e % 10 == 8 ? TOY_GAME_ENEMY_PURSUIT_FAST :
            e % 10 == 9 ? TOY_GAME_ENEMY_PURSUIT_HEAVY :
                           TOY_GAME_ENEMY_PURSUIT_COMMON;
        enemy->x = (e % 10 - 5) * 600 + 300;
        enemy->z = -1800 + (e / 10) * 800;
        enemy->dir_z = -1024;
    }
    game.state = TOY_GAME_PLAYING;
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
    /* Warm lazy model/animation caches before measuring or observing the
     * deterministic frame.  This mirrors the existing world benchmark's
     * warmup and keeps asset IO out of frontend recording time. */
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) goto cleanup;
    {
        int warm_local = rasterfall_render_begin_dynamic_lighting();
        rasterfall_render_scene(&renderer, &camera);
        rasterfall_render_flags(&renderer, &camera);
        rasterfall_render_enemies(&renderer, &camera);
        rasterfall_render_ai_teammate(&renderer, &camera);
        rasterfall_render_end_dynamic_lighting();
        if (toy_renderer_flush(&renderer) < 0) goto cleanup;
        rasterfall_viewmodel_render(&renderer, &game, active_effects,
                                    warm_local);
        if (toy_renderer_flush(&renderer) < 0) goto cleanup;
    }
    failure = "renderer begin";
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) goto cleanup;
    toy_renderer_set_command_observer(&renderer, gpu_world_observe, &capture);
    frontend_us = 0;
    frontend_start = render_monotonic_us();
    {
        int local = rasterfall_render_begin_dynamic_lighting();
        rasterfall_render_scene(&renderer, &camera);
        rasterfall_render_flags(&renderer, &camera);
        rasterfall_render_enemies(&renderer, &camera);
        rasterfall_render_ai_teammate(&renderer, &camera);
        rasterfall_render_end_dynamic_lighting();
        frontend_us += render_monotonic_us() - frontend_start;
        failure = "world flush";
        if (toy_renderer_flush(&renderer) < 0) goto cleanup;
        frontend_start = render_monotonic_us();
        rasterfall_viewmodel_render(&renderer, &game, active_effects, local);
        frontend_us += render_monotonic_us() - frontend_start;
        failure = "viewmodel flush";
        if (toy_renderer_flush(&renderer) < 0) goto cleanup;
    }
    if (capture.failed) { failure = "grow selected commands"; goto cleanup; }
    packed_renderer.surface = surface;
    packed_renderer.cmds = capture.selected;
    packed_renderer.cmd_count = capture.selected_count;
    stream_capacity = rf_gpu_raster_stream_size_v1(
        (uint32_t)capture.selected_count + 2U);
    stream = tlibc_malloc(stream_capacity);
    if (!stream) { failure = "allocate ABI stream"; goto cleanup; }
    pack_start = render_monotonic_us();
    if (rf_gpu_raster_measure_textures_toy_v1(&packed_renderer,
            &texture_count, &texture_bytes) != RF_GPU_RASTER_PACK_OK) {
        failure = "measure Texture V1 resources"; goto cleanup;
    }
    if (texture_count) {
        resources.descs = tlibc_malloc((size_t)texture_count * sizeof(*resources.descs));
        resources.texels = tlibc_malloc(texture_bytes);
        if (!resources.descs || !resources.texels) {
            failure = "allocate Texture V1 resources"; goto cleanup;
        }
        resources.desc_capacity = texture_count;
        resources.texel_capacity = texture_bytes;
    }
    if (rf_gpu_raster_pack_toy_textured_v1(&packed_renderer, 0x151922, 0,
            stream, stream_capacity, &written, &resources) != RF_GPU_RASTER_PACK_OK) {
        failure = "GPU-4 pack selected stream"; goto cleanup;
    }
    pack_us = render_monotonic_us() - pack_start;
    fd = __openat(AT_FDCWD, output_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (fd < 0) { failure = "open ABI stream"; goto cleanup; }
    {
        size_t offset = 0;
        while (offset < written) {
            long n = __write(fd, stream + offset, written - offset);
            if (n <= 0) { failure = "write ABI stream"; goto cleanup; }
            offset += (size_t)n;
        }
    }
    __close(fd); fd = -1;
    if (resources.desc_count) {
        char texture_path[512];
        uint32_t texture_header[4] = {0x31544652U, resources.desc_count,
            sizeof(struct rf_gpu_texture_desc_v1), (uint32_t)resources.texel_size};
        snprintf(texture_path, sizeof(texture_path), "%s.textures", output_path);
        fd = __openat(AT_FDCWD, texture_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
        if (fd < 0 || __write(fd, texture_header, sizeof(texture_header)) != sizeof(texture_header) ||
            __write(fd, resources.descs, (size_t)resources.desc_count * sizeof(*resources.descs)) !=
                (long)((size_t)resources.desc_count * sizeof(*resources.descs)) ||
            __write(fd, resources.texels, resources.texel_size) != (long)resources.texel_size) {
            failure = "write Texture V1 resources"; goto cleanup;
        }
        __close(fd); fd = -1;
    }
    __printf("GPU-WORLD partial-world-diagnostic view=%s enemies=%d size=1280x720 output=%s\n", view, enemy_count, output_path);
    __printf("GPU-WORLD commands total=%lu flat_supported=%lu vertex_lit_supported=%lu combined_supported=%d unsupported=%lu supported_percent=%lu.%02lu clear=2 unsupported_textured=%lu unsupported_transparent=%lu unsupported_overlay=%lu unsupported_edge=%lu other=%lu\n",
        capture.total + 2UL, capture.flat_opaque + 2UL,
        capture.vertex_lit,
        capture.selected_count + 2,
        capture.total - (unsigned long)capture.selected_count,
        (unsigned long)(capture.selected_count + 2) * 100UL /
            (capture.total + 2UL),
        ((unsigned long)(capture.selected_count + 2) * 10000UL /
            (capture.total + 2UL)) % 100UL,
        capture.textured - capture.textured_supported, capture.transparent, capture.overlay,
        capture.edge, capture.other);
    __printf("GPU-WORLD timing frontend_recording_us=%ld classification_us=%ld pack_us=%ld selected_bbox_pixels=%llu stream_bytes=%lu\n",
        frontend_us, capture.classify_us, pack_us,
        capture.selected_bbox_pixels, (unsigned long)written);
    __printf("GPU-WORLD textures commands=%lu unique=%u bytes=%llu largest=%llu nearest=%u bilinear=%u repeat=%u clamp=%u rgb_commands=%u rgba_commands=%u material_commands=%u invalid=%u vertex_light=%u vertex_fog=%u max_abs_uoz=%llu max_abs_voz=%llu\n",
        capture.textured, capture.texture_count, capture.texture_bytes,
        capture.largest_texture_bytes, capture.nearest_count,
        capture.bilinear_count, capture.repeat_count, capture.clamp_count,
        capture.texture_rgb_count, capture.texture_rgba_count,
        capture.texture_material_count, capture.texture_invalid_count,
        capture.texture_vertex_light_count, capture.texture_vertex_fog_count,
        capture.max_abs_u_over_z, capture.max_abs_v_over_z);
    result = 0;
cleanup:
    if (result) __fprintf(2, "GPU-WORLD capture failed: %s\n", failure);
    if (fd >= 0) __close(fd);
    if (saved) { game = *saved; tlibc_free(saved); }
    toy_renderer_set_command_observer(&renderer, NULL, NULL);
    toy_renderer_destroy(&renderer);
    tlibc_free(surface.pixels);
    tlibc_free(stream);
    tlibc_free(capture.selected);
    tlibc_free(resources.descs);
    tlibc_free(resources.texels);
    return result;
}
