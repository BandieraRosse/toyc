struct actor_benchmark_fixture {
    struct rasterfall_model_asset model;
    struct rasterfall_vmd_clip vmd;
    struct rasterfall_animation_clip clip;
    struct rasterfall_animation_track tracks[RASTERFALL_VMD_MAX_BONES];
    struct toy_renderer commands;
    struct rasterfall_frontend_state state;
    long wall_us;
    int worker_id;
};
struct actor_benchmark_dispatch {
    struct actor_benchmark_fixture *actors;
    const struct camera *camera;
    const struct toy_surface *surface;
    int *depth;
    int time_ms;
};
static void actor_benchmark_job(int worker_id, int actor, void *opaque)
{
    static const int heights[5] = {1736, 1736, 1736, 1736, 1736};
    static const int positions[5] = {-1800, -900, 0, 900, 1800};
    struct actor_benchmark_dispatch *dispatch = opaque;
    struct actor_benchmark_fixture *job = &dispatch->actors[actor];
    long start = render_monotonic_us();
    frontend_bind_worker(worker_id, &job->state);
    __memset(&job->state.timing, 0, sizeof(job->state.timing));
    job->commands.surface = *dispatch->surface;
    job->commands.depth = dispatch->depth;
    job->commands.cmd_count = 0;
    job->commands.cmd_overflow = 0;
    job->commands.submitted_triangles = 0;
    job->commands.submitted_vertices = 0;
    job->commands.textured_triangles = 0;
    job->commands.textured_pixels = 0;
    job->commands.texture_fallback_pixels = 0;
    {
        long animation_start = render_monotonic_us();
        rasterfall_model_sample_clip(&job->model, &job->clip,
                                     dispatch->time_ms % job->clip.duration_ms);
        job->state.timing.animation_sample_us =
            render_monotonic_us() - animation_start;
    }
    render_gallery_model(&job->commands, dispatch->camera, &job->model,
        positions[actor], -900, 1000,
        character_model_scale(&job->model, heights[actor]));
    job->wall_us = render_monotonic_us() - start;
    job->worker_id = worker_id;
    frontend_unbind_worker(worker_id);
}
/* Fixed five-actor workload used to compare compiler optimization levels and
 * renderer architecture.  Keep assets, camera, animation times and pixels
 * identical between runs: this is a measurement fixture, not a quality knob. */
int rasterfall_render_actor_benchmark(int iterations, int frontend_workers,
                                      int raster_workers)
{
    static const char *paths[5] = {
        "rasterfall/private-assets/models/eula.rmesh",
        "rasterfall/private-assets/models/maid.rmesh",
        "rasterfall/private-assets/models/maid.rmesh",
        "rasterfall/private-assets/models/maid.rmesh",
        "rasterfall/private-assets/models/maid.rmesh"
    };
    static const char vmd_path[] =
        "rasterfall/private-assets/animations/walk04_loop5.vmd";
    struct actor_benchmark_fixture *actors = 0;
    struct actor_benchmark_dispatch dispatch;
    struct toy_renderer renderer;
    struct toy_surface surface;
    struct camera camera;
    uint32_t *pixels = 0;
    uint64_t hash = 1469598103934665603ULL;
    long animation_us = 0, skeleton_us = 0, skin_us = 0;
    long vertex_us = 0, triangle_us = 0, material_us = 0;
    long body_triangle_us = 0, edge_triangle_us = 0;
    long merge_us = 0, raster_us = 0, frame_us = 0;
    unsigned long triangles = 0;
    long actor_us[5] = {0,0,0,0,0};
    long actor_animation[5] = {0,0,0,0,0};
    long actor_skin[5] = {0,0,0,0,0};
    long actor_vertex[5] = {0,0,0,0,0};
    long actor_triangle[5] = {0,0,0,0,0};
    long worker_us[8] = {0,0,0,0,0,0,0,0};
    long frontend_wall_us = 0;
    long triangle_wall_us = 0;
    const struct toy_renderer *merge_sources[5];
    int actor_worker[5] = {0,0,0,0,0};
    int frame, actor, loaded = 0, result = 1;
    if (iterations < 1) iterations = 1;
    if (iterations > 1000) iterations = 1000;
    actors = tlibc_malloc(sizeof(*actors) * 5);
    if (!actors) goto done;
    __memset(actors, 0, sizeof(*actors) * 5);
    pixels = tlibc_malloc((size_t)1280 * 720 * sizeof(*pixels));
    if (!pixels) goto done;
    __memset(&camera, 0, sizeof(camera));
    camera.y = 0; camera.z = -5000; camera.cy = 1024;
    camera.pitch_cy = 1024;
    for (actor = 0; actor < 5; actor++) {
        int center, groove;
        if (rasterfall_model_load(&actors[actor].model, paths[actor]) < 0)
            goto done;
        loaded++;
        if (
            rasterfall_vmd_load(&actors[actor].vmd, vmd_path) < 0 ||
            rasterfall_vmd_map_model(&actors[actor].vmd,
                                     &actors[actor].model) <= 0 ||
            rasterfall_vmd_build_animation(&actors[actor].vmd,
                &actors[actor].clip, actors[actor].tracks,
                RASTERFALL_VMD_MAX_BONES) < 0) goto done;
        center = rasterfall_model_find_bone(&actors[actor].model, "センター");
        groove = rasterfall_model_find_bone(&actors[actor].model, "グルーブ");
        rasterfall_model_bind_root_motion(&actors[actor].model, center, groove);
        toy_renderer_init(&actors[actor].commands);
        actors[actor].state.toon_shared = -1;
        actors[actor].state.toon_level = 255;
        actors[actor].state.material_alpha = 255;
        actors[actor].state.material_double_sided = 1;
        actors[actor].state.texture_view = active_wall_texture;
    }
    surface.pixels = pixels; surface.width = 1280; surface.height = 720;
    surface.stride = 1280 * (int)sizeof(*pixels);
    toy_renderer_init(&renderer);
    toy_renderer_set_worker_count(&renderer, raster_workers);
    active_gallery_lighting = 1;
    dispatch.actors = actors; dispatch.camera = &camera;
    dispatch.surface = &surface; dispatch.time_ms = 0;
    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0)
        goto destroy;
    dispatch.depth = renderer.depth;
    if (
        toy_renderer_parallel_for(&renderer, 5, frontend_workers,
                                  actor_benchmark_job, &dispatch) < 0)
        goto destroy;
    for (actor = 0; actor < 5; actor++)
        merge_sources[actor] = &actors[actor].commands;
    if (toy_renderer_merge_command_batch(&renderer, merge_sources, 5, 5) < 0)
        goto destroy;
    toy_renderer_flush(&renderer);
    for (frame = 0; frame < iterations; frame++) {
        long start = render_monotonic_us(), raster_start;
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) goto destroy;
        dispatch.depth = renderer.depth;
        dispatch.time_ms = frame * 17;
        {
            long frontend_start = render_monotonic_us();
            if (toy_renderer_parallel_for(&renderer, 5, frontend_workers,
                                          actor_benchmark_job,
                                          &dispatch) < 0) goto destroy;
            frontend_wall_us += render_monotonic_us() - frontend_start;
        }
        {
            long merge_start = render_monotonic_us();
            if (toy_renderer_merge_command_batch(
                    &renderer, merge_sources, 5, 5) < 0)
                goto destroy;
            merge_us += render_monotonic_us() - merge_start;
        }
        for (actor = 0; actor < 5; actor++) {
            struct rasterfall_model_setup_timing *timing =
                &actors[actor].state.timing;
            animation_us += timing->animation_sample_us;
            skeleton_us += timing->bone_hierarchy_us;
            skin_us += timing->skinning_us;
            vertex_us += timing->vertex_cache_us;
            material_us += timing->material_us;
            body_triangle_us += timing->body_triangles_us;
            edge_triangle_us += timing->edge_triangles_us;
            triangle_us += timing->material_us + timing->body_triangles_us +
                timing->edge_triangles_us;
            actor_us[actor] += actors[actor].wall_us;
            actor_animation[actor] += timing->animation_sample_us;
            actor_skin[actor] += timing->skinning_us;
            actor_vertex[actor] += timing->vertex_cache_us;
            actor_triangle[actor] += timing->material_us +
                timing->body_triangles_us + timing->edge_triangles_us;
            actor_worker[actor] = actors[actor].worker_id;
            worker_us[actors[actor].worker_id] += actors[actor].wall_us;
        }
        {
            long worker_triangle[8] = {0,0,0,0,0,0,0,0};
            long maximum = 0;
            for (actor = 0; actor < 5; actor++) {
                struct rasterfall_model_setup_timing *timing =
                    &actors[actor].state.timing;
                worker_triangle[actors[actor].worker_id] +=
                    timing->material_us + timing->body_triangles_us +
                    timing->edge_triangles_us;
            }
            for (actor = 0; actor < 8; actor++)
                if (worker_triangle[actor] > maximum)
                    maximum = worker_triangle[actor];
            triangle_wall_us += maximum;
        }
        raster_start = render_monotonic_us();
        toy_renderer_flush(&renderer);
        raster_us += render_monotonic_us() - raster_start;
        triangles += renderer.submitted_triangles;
        frame_us += render_monotonic_us() - start;
        for (int y = 0; y < surface.height; y++) {
            const uint32_t *row = (const uint32_t *)
                ((const unsigned char *)surface.pixels + y * surface.stride);
            for (int x = 0; x < surface.width; x++) {
                hash ^= row[x] & 0xffffffU;
                hash *= 1099511628211ULL;
            }
        }
    }
    __printf("rasterfall: actor benchmark actors=5 size=1280x720 frames=%d detected_cpus=%d frontend_workers=%d raster_workers=%d hash=%016llx frame_us=%ld frontend_wall_us=%ld merge_us=%ld animation_us=%ld skeleton_us=%ld skin_us=%ld vertex_transform_us=%ld triangle_setup_us=%ld triangle_setup_cpu_us=%ld material_us=%ld body_triangle_us=%ld edge_triangle_us=%ld raster_us=%ld triangles=%lu\n",
        iterations, renderer.detected_cpu_count, frontend_workers,
        renderer.worker_count, hash,
        frame_us / iterations, frontend_wall_us / iterations,
        merge_us / iterations,
        animation_us / iterations,
        skeleton_us / iterations, skin_us / iterations, vertex_us / iterations,
        triangle_wall_us / iterations, triangle_us / iterations,
        material_us / iterations,
        body_triangle_us / iterations, edge_triangle_us / iterations,
        raster_us / iterations,
        triangles / (unsigned long)iterations);
    for (actor = 0; actor < 5; actor++)
        __printf("rasterfall: actor benchmark job=%d last_worker=%d job_wall_us=%ld animation_us=%ld skin_us=%ld vertex_us=%ld triangle_us=%ld commands=%d triangles=%lu\n",
                 actor, actor_worker[actor], actor_us[actor] / iterations,
                 actor_animation[actor] / iterations,
                 actor_skin[actor] / iterations,
                 actor_vertex[actor] / iterations,
                 actor_triangle[actor] / iterations,
                 actors[actor].commands.cmd_count,
                 actors[actor].commands.submitted_triangles);
    for (actor = 0; actor < renderer.worker_count; actor++)
        __printf("rasterfall: actor benchmark frontend_worker=%d active_us=%ld\n",
                 actor, worker_us[actor] / iterations);
    result = 0;
destroy:
    active_gallery_lighting = 0;
    toy_renderer_destroy(&renderer);
done:
    if (actors) {
        for (actor = 0; actor < loaded; actor++) {
            /* Recording-only renderer borrows the destination depth pointer
             * solely to satisfy triangle API validation; it never owns it. */
            actors[actor].commands.depth = 0;
            toy_renderer_destroy(&actors[actor].commands);
            if (actors[actor].state.vertex_cache)
                tlibc_free(actors[actor].state.vertex_cache);
            rasterfall_vmd_unload(&actors[actor].vmd);
            rasterfall_model_unload(&actors[actor].model);
        }
        tlibc_free(actors);
    }
    if (pixels) tlibc_free(pixels);
    return result;
}
