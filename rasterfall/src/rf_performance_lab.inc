/* Outpost performance lab: runtime-owned trial and read-only result UI. */
#define RF_PERF_LAB_SCENES 4
#define RF_PERF_LAB_SAMPLES 65536
#define RF_PERF_LAB_WARMUP_US 2000000LL
#define RF_PERF_LAB_DURATION_US 10000000LL

struct rf_perf_lab_result {
    int valid, scene, samples, spawned, min_alive, max_alive;
    int isolated, uncapped, gpu_samples, width, height, aborted;
    unsigned lights, shadow_maps, shadow_draws, present_mode;
    int64_t logic_us;
    char map[128];
    int64_t median_us, p95_us, p99_us, mean_us;
    int64_t enemy_us, geometry_us, upload_us, world_us, submit_us;
    double gpu_draw_ms;
    unsigned int draws;
};
struct rf_perf_lab_state {
    int control_near, result_near, menu_open, result_open, selected, running;
    int spawned, min_alive, max_alive, samples;
    int isolated, uncapped, interference, gpu_samples, invalid, returning;
    int width, height;
    int64_t logic_sum;
    unsigned lights, shadow_maps, present_mode;
    unsigned long long shadow_draws_sum;
    uint64_t previous_seed;
    struct toy_game_actor previous_player;
    int64_t started_us, interval_sum, enemy_sum, geometry_sum, upload_sum;
    int64_t world_sum, submit_sum, gpu_draw_ns_sum;
    unsigned long long draws_sum;
    int64_t intervals[RF_PERF_LAB_SAMPLES];
    struct camera previous_camera;
    uint64_t previous_rng;
    struct rf_perf_lab_result result;
};
static struct rf_perf_lab_state rf_perf_lab;
static const char *rf_perf_lab_names[RF_PERF_LAB_SCENES] = {
    "LOW PRESSURE", "64 ENEMIES", "DENSE COMPONENTS", "ENEMIES + COMPONENTS"
};
static const int rf_perf_lab_observe_z[RF_PERF_LAB_SCENES] = {
    900, -31500, -20700, -9900
};

static int rf_perf_lab_near_object(const struct rasterfall_session *session,
                                   const char *id, int x, int z)
{
    const struct rf_map_runtime_object *o = rf_map_runtime_find_object(
        &session->map_ops.runtime, id);
    return o && x >= o->x-850 && x <= o->x+850 &&
        z >= o->z-850 && z <= o->z+850;
}

static void rf_perf_lab_clear(void)
{
    memset(&rf_perf_lab.result, 0, sizeof(rf_perf_lab.result));
    rf_perf_lab.spawned = 0;
    rf_perf_lab.gpu_samples = rf_perf_lab.invalid = 0;
    rf_perf_lab.logic_sum = 0;
    rf_perf_lab.shadow_draws_sum = 0;
    rf_perf_lab.lights = rf_perf_lab.shadow_maps = 0;
    rf_perf_lab.samples = 0;
    rf_perf_lab.interval_sum = rf_perf_lab.enemy_sum = 0;
    rf_perf_lab.geometry_sum = rf_perf_lab.upload_sum = 0;
    rf_perf_lab.world_sum = rf_perf_lab.submit_sum = 0;
    rf_perf_lab.gpu_draw_ns_sum = 0;
    rf_perf_lab.draws_sum = 0;
    rf_perf_lab.min_alive = TOY_GAME_MAX_ENEMIES;
    rf_perf_lab.max_alive = 0;
}

static int rf_perf_lab_start(struct rf_game_runtime *runtime)
{
    struct rasterfall_session *session = runtime->session;
    struct camera *camera = &runtime->camera;
    struct toy_game_box spawn_box = {
        .minx=36600, .maxx=47800, .minz=-35100, .maxz=-27900
    };
    struct toy_game *test_game = &session->game_state;
    struct toy_game_actor *player = toy_game_local_player_actor(test_game);
    int scene = rf_perf_lab.selected;
    if (scene < 0 || scene >= RF_PERF_LAB_SCENES || !player ||
        test_game->state != TOY_GAME_PLAYING || test_game->enemies_alive ||
        session->world_id != RASTERFALL_WORLD_OUTPOST) return -1;
    rf_perf_lab_clear();
    rf_perf_lab.previous_camera = *camera;
    rf_perf_lab.previous_rng = test_game->rng;
    rf_perf_lab.previous_seed = session->seed;
    rf_perf_lab.previous_player = *player;
    if (rf_perf_lab.isolated) {
        enum rasterfall_world_id world = scene >= 2 ?
            RASTERFALL_WORLD_PERF_COMPONENTS : RASTERFALL_WORLD_PERF_EMPTY;
        if (rf_game_request_world(runtime, world) < 0) {
            session->seed = rf_perf_lab.previous_seed;
            if (rf_game_request_world(runtime, RASTERFALL_WORLD_OUTPOST) == 0) {
                *camera = rf_perf_lab.previous_camera;
                *toy_game_local_player_actor(&session->game_state) = rf_perf_lab.previous_player;
            } else return -2;
            return -1;
        }
        rasterfall_effects_init(active_effects);
        test_game = &session->game_state;
        player = toy_game_local_player_actor(test_game);
        const struct rf_map_runtime_region *spawn = rf_map_runtime_find_region(
            &session->map_ops.runtime,"perf_spawn");
        if (!spawn) goto failed;
        spawn_box.minx=spawn->bounds.min_x;spawn_box.maxx=spawn->bounds.max_x;
        spawn_box.minz=spawn->bounds.min_z;spawn_box.maxz=spawn->bounds.max_z;
    }
    test_game->rng = 0x504552464c414231ULL;
    if (!rf_perf_lab.isolated && scene == 3) {
        spawn_box.minz = -13500;
        spawn_box.maxz = -6300;
    }
    camera->x = rf_perf_lab.isolated ? session->level.start_x : 34200;
    camera->z = rf_perf_lab.isolated ? session->level.start_z : rf_perf_lab_observe_z[scene];
    camera->sy = 1024; camera->cy = 0;
    camera->pitch_sy = 0; camera->pitch_cy = 1024;
    player->x = camera->x; player->z = camera->z;
    player->sy = camera->sy; player->cy = camera->cy;
    if (scene == 1 || scene == 3) {
        rf_perf_lab.spawned += toy_game_spawn_horde_type(test_game,
            TOY_GAME_ENEMY_PURSUIT_COMMON, 48, 48, &spawn_box, 1, 0);
        rf_perf_lab.spawned += toy_game_spawn_horde_type(test_game,
            TOY_GAME_ENEMY_PURSUIT_FAST, 8, 8, &spawn_box, 1, 0);
        rf_perf_lab.spawned += toy_game_spawn_horde_type(test_game,
            TOY_GAME_ENEMY_PURSUIT_HEAVY, 8, 8, &spawn_box, 1, 0);
        if (rf_perf_lab.spawned != 64) goto failed;
    }
    rf_perf_lab.started_us = rf_core_clock_now_us();
    rf_perf_lab.running = 1;
    rf_perf_lab.menu_open = 0;
    rf_perf_lab.result_open = 0;
    return 0;
failed:
    if (rf_perf_lab.isolated) {
        session->seed = rf_perf_lab.previous_seed;
        if (rf_game_request_world(runtime, RASTERFALL_WORLD_OUTPOST) < 0) return -2;
    } else {
        rasterfall_session_dev_killall(session);
        for (int i=0; i<TOY_GAME_MAX_ENEMIES; ++i) test_game->enemies[i].active=0;
    }
    session->game_state.rng = rf_perf_lab.previous_rng;
    *toy_game_local_player_actor(&session->game_state) = rf_perf_lab.previous_player;
    *camera = rf_perf_lab.previous_camera;
    return -1;
}

static int rf_perf_lab_compare_intervals(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static int rf_perf_lab_finish(struct rf_game_runtime *runtime, int aborted)
{
    struct rasterfall_session *session = runtime->session;
    struct camera *camera = &runtime->camera;
    struct toy_game *test_game = &session->game_state;
    struct toy_game_actor *player = toy_game_local_player_actor(test_game);
    struct rf_perf_lab_result *r = &rf_perf_lab.result;
    int n = rf_perf_lab.samples;
    if (!rf_perf_lab.running) return 0;
    rf_perf_lab.running = 0;
    rf_perf_lab.returning = 1;
    if (!n) { r->valid=2;r->aborted=aborted;r->scene=rf_perf_lab.selected; }
    if (n > 0) {
        qsort(rf_perf_lab.intervals, (size_t)n, sizeof(rf_perf_lab.intervals[0]),
              rf_perf_lab_compare_intervals);
        r->valid = !aborted && !rf_perf_lab.invalid &&
            rf_perf_lab.min_alive == rf_perf_lab.spawned &&
            rf_perf_lab.max_alive == rf_perf_lab.spawned ? 1 : 2;
        r->isolated = rf_perf_lab.isolated;r->uncapped = rf_perf_lab.uncapped;
        r->aborted = aborted;r->gpu_samples = rf_perf_lab.gpu_samples;
        r->width=rf_perf_lab.width;r->height=rf_perf_lab.height;
        r->lights=rf_perf_lab.lights;r->shadow_maps=rf_perf_lab.shadow_maps;
        r->shadow_draws=(unsigned)(rf_perf_lab.shadow_draws_sum/(unsigned)n);
        r->present_mode=rf_perf_lab.present_mode;
        r->logic_us=rf_perf_lab.logic_sum/n;
        snprintf(r->map,sizeof(r->map),"%s",rasterfall_world_map_path(session->world_id));
        r->scene = rf_perf_lab.selected;
        r->samples = n;
        r->spawned = rf_perf_lab.spawned;
        r->min_alive = rf_perf_lab.min_alive;
        r->max_alive = rf_perf_lab.max_alive;
        r->median_us = rf_perf_lab.intervals[n/2];
        r->p95_us = rf_perf_lab.intervals[((n*95+99)/100)-1];
        r->p99_us = rf_perf_lab.intervals[((n*99+99)/100)-1];
        r->mean_us = rf_perf_lab.interval_sum/n;
        r->enemy_us = rf_perf_lab.enemy_sum/n;
        r->geometry_us = rf_perf_lab.geometry_sum/n;
        r->upload_us = rf_perf_lab.upload_sum/n;
        r->world_us = rf_perf_lab.world_sum/n;
        r->submit_us = rf_perf_lab.submit_sum/n;
        r->gpu_draw_ms = rf_perf_lab.gpu_samples ?
            (double)rf_perf_lab.gpu_draw_ns_sum/rf_perf_lab.gpu_samples/1000000.0 : 0;
        r->draws = (unsigned int)(rf_perf_lab.draws_sum/(unsigned long long)n);
    }
    if (rf_perf_lab.isolated) {
        session->seed = rf_perf_lab.previous_seed;
        if (rf_game_request_world(runtime, RASTERFALL_WORLD_OUTPOST) < 0) return -2;
        rasterfall_effects_init(active_effects);
        test_game = &session->game_state;
        player = toy_game_local_player_actor(test_game);
    }
    rasterfall_session_dev_killall(session);
    for (int i=0; i<TOY_GAME_MAX_ENEMIES; ++i)
        if (test_game->enemies[i].active == 2) {
            test_game->enemies[i].active = 0;
            test_game->enemies[i].dying_ms = 0;
        }
    test_game->rng = rf_perf_lab.previous_rng;
    if (player) *player = rf_perf_lab.previous_player;
    *camera = rf_perf_lab.previous_camera;
    rf_perf_lab.menu_open = 0;
    return 0;
}

static void rf_perf_lab_sample(int64_t now, int64_t interval_us,
    int64_t enemy_us, int64_t geometry_us, int64_t upload_us,
    int64_t world_us, int64_t submit_us, double gpu_draw_ms,
    unsigned int draws, int alive, int64_t logic_us, int gpu_valid,
    unsigned lights, unsigned shadow_maps, unsigned shadow_draws,
    unsigned present_mode, int width, int height)
{
    int n = rf_perf_lab.samples;
    if (!rf_perf_lab.running || now-rf_perf_lab.started_us < RF_PERF_LAB_WARMUP_US ||
        interval_us <= 0) return;
    if (n >= RF_PERF_LAB_SAMPLES) { rf_perf_lab.invalid=1;return; }
    rf_perf_lab.intervals[n] = interval_us;
    rf_perf_lab.samples = n+1;
    rf_perf_lab.interval_sum += interval_us;
    rf_perf_lab.enemy_sum += enemy_us;
    rf_perf_lab.geometry_sum += geometry_us;
    rf_perf_lab.upload_sum += upload_us;
    rf_perf_lab.world_sum += world_us;
    rf_perf_lab.submit_sum += submit_us;
    if (gpu_valid) {
        rf_perf_lab.gpu_draw_ns_sum += (int64_t)(gpu_draw_ms*1000000.0);
        rf_perf_lab.gpu_samples++;
    }
    rf_perf_lab.logic_sum += logic_us;
    rf_perf_lab.shadow_draws_sum += shadow_draws;
    if (!n) {
        rf_perf_lab.width=width;rf_perf_lab.height=height;
        rf_perf_lab.present_mode=present_mode;
        rf_perf_lab.lights=lights;rf_perf_lab.shadow_maps=shadow_maps;
    } else if (width!=rf_perf_lab.width || height!=rf_perf_lab.height ||
               present_mode!=rf_perf_lab.present_mode || lights!=rf_perf_lab.lights ||
               shadow_maps!=rf_perf_lab.shadow_maps) rf_perf_lab.invalid=1;
    if (rf_perf_lab.isolated && lights) rf_perf_lab.invalid=1;
    rf_perf_lab.draws_sum += draws;
    if (alive < rf_perf_lab.min_alive) rf_perf_lab.min_alive = alive;
    if (alive > rf_perf_lab.max_alive) rf_perf_lab.max_alive = alive;
}

static void rf_perf_lab_draw(struct rasterfall_canvas *c, int64_t now)
{
    char line[128];
    int x = (c->width-760)/2, y = (c->height-510)/2;
    const struct rf_perf_lab_result *r = &rf_perf_lab.result;
    if (rf_perf_lab.running) {
        int remain = (int)((RF_PERF_LAB_DURATION_US-(now-rf_perf_lab.started_us)+999999)/1000000);
        if (remain < 0) remain = 0;
        snprintf(line,sizeof(line),"PERFORMANCE TEST  %s  %ds",rf_perf_lab_names[rf_perf_lab.selected],remain);
        rasterfall_canvas_text(c, 30, 35, line, 0xC7F2EE);
        return;
    }
    rasterfall_canvas_rect(c,x,y,760,510,0x172530,230);
    rasterfall_canvas_text(c,x+25,y+22,rf_perf_lab.result_open ?
        "PERFORMANCE RESULTS" : "PERFORMANCE TEST",0xC7F2EE);
    if (rf_perf_lab.result_open) {
        if (!r->valid) rasterfall_canvas_text(c,x+25,y+80,"NO TEST RESULT",0xFFBE8A);
        else {
            snprintf(line,sizeof(line),"SCENE  %s",rf_perf_lab_names[r->scene]);
            rasterfall_canvas_text(c,x+25,y+72,line,0xFFFFFF);
            snprintf(line,sizeof(line),"FRAMES %d   FPS %.1f   INTERVAL %.2f ms",
                r->samples,r->mean_us ? 1000000.0/r->mean_us : 0.0,r->mean_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+110,line,0xFFFFFF);
            snprintf(line,sizeof(line),"MEDIAN %.2f   P95 %.2f   P99 %.2f ms",
                r->median_us/1000.0,r->p95_us/1000.0,r->p99_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+145,line,0xFFFFFF);
            snprintf(line,sizeof(line),"ENEMIES %d   ALIVE %d..%d   DRAWS %u",
                r->spawned,r->min_alive,r->max_alive,r->draws);
            rasterfall_canvas_text(c,x+25,y+180,line,0xFFFFFF);
            if (r->valid == 2)
                rasterfall_canvas_text(c,x+25,y+425,"ABORTED OR WORKLOAD/CONFIG CHANGED: INVALID",0xFFBE8A);
            snprintf(line,sizeof(line),"ENEMY %.2f   GEOMETRY %.2f   UPLOAD %.2f ms",
                r->enemy_us/1000.0,r->geometry_us/1000.0,r->upload_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+215,line,0xFFFFFF);
            snprintf(line,sizeof(line),"WORLD %.2f   SUBMIT/WAIT %.2f   GPU %.2f ms",
                r->world_us/1000.0,r->submit_us/1000.0,r->gpu_draw_ms);
            rasterfall_canvas_text(c,x+25,y+250,line,0xFFFFFF);
            rasterfall_canvas_text(c,x+25,y+285,"GPU TIME OVERLAPS CPU; STAGES DO NOT SUM",0x9FB7C5);
        }
        if (r->valid) {
            snprintf(line,sizeof(line),"%s  %s  %dx%d  PRESENT %u",
                r->isolated?"ISOLATED":"OUTPOST",r->uncapped?"UNCAPPED":"120 FPS CAP",
                r->width,r->height,r->present_mode);
            rasterfall_canvas_text(c,x+25,y+325,line,0xC7F2EE);
            snprintf(line,sizeof(line),"LOGIC %.2f ms  LIGHTS %u  SHADOW MAPS %u / DRAWS %u",
                r->logic_us/1000.0,r->lights,r->shadow_maps,r->shadow_draws);
            rasterfall_canvas_text(c,x+25,y+360,line,0xFFFFFF);
            snprintf(line,sizeof(line),"GPU VALID SAMPLES %d / %d",r->gpu_samples,r->samples);
            rasterfall_canvas_text(c,x+25,y+395,line,0x9FB7C5);
        }
        rasterfall_canvas_text(c,x+25,y+465,"ESC  CLOSE",0xC7F2EE);
    } else {
        for (int i=0;i<RF_PERF_LAB_SCENES;++i) {
            snprintf(line,sizeof(line),"%c  %d  %s",i==rf_perf_lab.selected?'>':' ',i+1,
                rf_perf_lab_names[i]);
            rasterfall_canvas_text(c,x+40,y+85+i*55,line,
                i==rf_perf_lab.selected?0xC7F2EE:0xFFFFFF);
        }
        snprintf(line,sizeof(line),"F2 SCOPE: %s    T TIMING: %s",
            rf_perf_lab.isolated?"ISOLATED":"OUTPOST",rf_perf_lab.uncapped?"UNCAPPED":"120 FPS CAP");
        rasterfall_canvas_text(c,x+25,y+340,line,0xC7F2EE);
        rasterfall_canvas_text(c,x+25,y+385,"10s TRIAL / 2s WARMUP   ESC DURING TEST: ABORT",0x9FB7C5);
        rasterfall_canvas_text(c,x+25,y+465,"1-4 SELECT   ENTER START   ESC CLOSE",0xC7F2EE);
    }
}
