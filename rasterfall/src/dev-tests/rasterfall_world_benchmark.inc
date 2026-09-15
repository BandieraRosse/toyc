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
