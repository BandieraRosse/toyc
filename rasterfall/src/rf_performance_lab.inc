/* Outpost performance lab: runtime-owned trial and read-only result UI. */
#define RF_PERF_LAB_SCENES 4
#define RF_PERF_LAB_SAMPLES 2048
#define RF_PERF_LAB_WARMUP_US 2000000LL
#define RF_PERF_LAB_DURATION_US 10000000LL

struct rf_perf_lab_result {
    int valid, scene, samples, spawned, min_alive, max_alive;
    int64_t median_us, p95_us, p99_us, mean_us;
    int64_t enemy_us, geometry_us, upload_us, world_us, submit_us;
    double gpu_draw_ms;
    unsigned int draws;
};
struct rf_perf_lab_state {
    int control_near, result_near, menu_open, result_open, selected, running;
    int spawned, min_alive, max_alive, samples;
    int64_t started_us, interval_sum, enemy_sum, geometry_sum, upload_sum;
    int64_t world_sum, submit_sum, gpu_draw_ns_sum;
    unsigned long long draws_sum;
    int64_t intervals[RF_PERF_LAB_SAMPLES];
    struct camera previous_camera;
    int previous_player_x, previous_player_z;
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
    rf_perf_lab.samples = 0;
    rf_perf_lab.interval_sum = rf_perf_lab.enemy_sum = 0;
    rf_perf_lab.geometry_sum = rf_perf_lab.upload_sum = 0;
    rf_perf_lab.world_sum = rf_perf_lab.submit_sum = 0;
    rf_perf_lab.gpu_draw_ns_sum = 0;
    rf_perf_lab.draws_sum = 0;
    rf_perf_lab.min_alive = TOY_GAME_MAX_ENEMIES;
    rf_perf_lab.max_alive = 0;
}

static int rf_perf_lab_start(struct rasterfall_session *session,
                             struct camera *camera, int64_t now)
{
    struct toy_game_box spawn_box = {
        .minx=36600, .maxx=47800, .minz=-35100, .maxz=-27900
    };
    struct toy_game *test_game = &session->game_state;
    struct toy_game_actor *player = toy_game_local_player_actor(test_game);
    int scene = rf_perf_lab.selected;
    if (scene < 0 || scene >= RF_PERF_LAB_SCENES || !player ||
        test_game->state != TOY_GAME_PLAYING || test_game->enemies_alive) return -1;
    rf_perf_lab_clear();
    rf_perf_lab.previous_camera = *camera;
    rf_perf_lab.previous_player_x = player->x;
    rf_perf_lab.previous_player_z = player->z;
    rf_perf_lab.previous_rng = test_game->rng;
    test_game->rng = 0x504552464c414231ULL;
    if (scene == 3) {
        spawn_box.minz = -13500;
        spawn_box.maxz = -6300;
    }
    camera->x = 34200; camera->z = rf_perf_lab_observe_z[scene];
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
        if (rf_perf_lab.spawned != 64) {
            rasterfall_session_dev_killall(session);
            for (int i=0; i<TOY_GAME_MAX_ENEMIES; ++i)
                if (test_game->enemies[i].active == 2) test_game->enemies[i].active = 0;
            test_game->rng = rf_perf_lab.previous_rng;
            player->x = rf_perf_lab.previous_player_x;
            player->z = rf_perf_lab.previous_player_z;
            *camera = rf_perf_lab.previous_camera;
            return -1;
        }
    }
    rf_perf_lab.started_us = now;
    rf_perf_lab.running = 1;
    rf_perf_lab.menu_open = 0;
    rf_perf_lab.result_open = 0;
    return 0;
}

static int rf_perf_lab_compare_intervals(const void *a, const void *b)
{
    int64_t x = *(const int64_t *)a, y = *(const int64_t *)b;
    return (x > y) - (x < y);
}

static void rf_perf_lab_finish(struct rasterfall_session *session,
                               struct camera *camera)
{
    struct toy_game *test_game = &session->game_state;
    struct toy_game_actor *player = toy_game_local_player_actor(test_game);
    struct rf_perf_lab_result *r = &rf_perf_lab.result;
    int n = rf_perf_lab.samples;
    if (!rf_perf_lab.running) return;
    rf_perf_lab.running = 0;
    if (n > 0) {
        qsort(rf_perf_lab.intervals, (size_t)n, sizeof(rf_perf_lab.intervals[0]),
              rf_perf_lab_compare_intervals);
        r->valid = rf_perf_lab.spawned == 0 ||
            rf_perf_lab.min_alive == rf_perf_lab.spawned ? 1 : 2;
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
        r->gpu_draw_ms = (double)rf_perf_lab.gpu_draw_ns_sum/(double)n/1000000.0;
        r->draws = (unsigned int)(rf_perf_lab.draws_sum/(unsigned long long)n);
    }
    rasterfall_session_dev_killall(session);
    for (int i=0; i<TOY_GAME_MAX_ENEMIES; ++i)
        if (test_game->enemies[i].active == 2) {
            test_game->enemies[i].active = 0;
            test_game->enemies[i].dying_ms = 0;
        }
    test_game->rng = rf_perf_lab.previous_rng;
    if (player) {
        player->x = rf_perf_lab.previous_player_x;
        player->z = rf_perf_lab.previous_player_z;
    }
    *camera = rf_perf_lab.previous_camera;
    rf_perf_lab.menu_open = 0;
}

static void rf_perf_lab_sample(int64_t now, int64_t interval_us,
    int64_t enemy_us, int64_t geometry_us, int64_t upload_us,
    int64_t world_us, int64_t submit_us, double gpu_draw_ms,
    unsigned int draws, int alive)
{
    int n = rf_perf_lab.samples;
    if (!rf_perf_lab.running || now-rf_perf_lab.started_us < RF_PERF_LAB_WARMUP_US ||
        interval_us <= 0 || n >= RF_PERF_LAB_SAMPLES) return;
    rf_perf_lab.intervals[n] = interval_us;
    rf_perf_lab.samples = n+1;
    rf_perf_lab.interval_sum += interval_us;
    rf_perf_lab.enemy_sum += enemy_us;
    rf_perf_lab.geometry_sum += geometry_us;
    rf_perf_lab.upload_sum += upload_us;
    rf_perf_lab.world_sum += world_us;
    rf_perf_lab.submit_sum += submit_us;
    rf_perf_lab.gpu_draw_ns_sum += (int64_t)(gpu_draw_ms*1000000.0);
    rf_perf_lab.draws_sum += draws;
    if (alive < rf_perf_lab.min_alive) rf_perf_lab.min_alive = alive;
    if (alive > rf_perf_lab.max_alive) rf_perf_lab.max_alive = alive;
}

static void rf_perf_lab_draw(struct rasterfall_canvas *c, int64_t now)
{
    char line[128];
    int x = (c->width-700)/2, y = (c->height-430)/2;
    const struct rf_perf_lab_result *r = &rf_perf_lab.result;
    if (rf_perf_lab.running) {
        int remain = (int)((RF_PERF_LAB_DURATION_US-(now-rf_perf_lab.started_us)+999999)/1000000);
        if (remain < 0) remain = 0;
        snprintf(line,sizeof(line),"PERFORMANCE TEST  %s  %ds",rf_perf_lab_names[rf_perf_lab.selected],remain);
        rasterfall_canvas_text(c, 30, 35, line, 0xC7F2EE);
        return;
    }
    rasterfall_canvas_rect(c,x,y,700,430,0x172530,230);
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
                rasterfall_canvas_text(c,x+25,y+315,"WORKLOAD CHANGED: RESULT INVALID",0xFFBE8A);
            snprintf(line,sizeof(line),"ENEMY %.2f   GEOMETRY %.2f   UPLOAD %.2f ms",
                r->enemy_us/1000.0,r->geometry_us/1000.0,r->upload_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+215,line,0xFFFFFF);
            snprintf(line,sizeof(line),"WORLD %.2f   SUBMIT/WAIT %.2f   GPU %.2f ms",
                r->world_us/1000.0,r->submit_us/1000.0,r->gpu_draw_ms);
            rasterfall_canvas_text(c,x+25,y+250,line,0xFFFFFF);
            rasterfall_canvas_text(c,x+25,y+285,"GPU TIME OVERLAPS CPU; STAGES DO NOT SUM",0x9FB7C5);
        }
        rasterfall_canvas_text(c,x+25,y+380,"ESC  CLOSE",0xC7F2EE);
    } else {
        for (int i=0;i<RF_PERF_LAB_SCENES;++i) {
            snprintf(line,sizeof(line),"%c  %d  %s",i==rf_perf_lab.selected?'>':' ',i+1,
                rf_perf_lab_names[i]);
            rasterfall_canvas_text(c,x+40,y+85+i*55,line,
                i==rf_perf_lab.selected?0xC7F2EE:0xFFFFFF);
        }
        rasterfall_canvas_text(c,x+25,y+365,"1-4 SELECT   ENTER START   ESC CLOSE",0xC7F2EE);
    }
}
