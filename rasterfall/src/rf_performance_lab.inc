/* Outpost performance lab: runtime-owned trial and read-only result UI. */
#define RF_PERF_LAB_SCENES 10
#define RF_PERF_LAB_POINTS 5
#define RF_PERF_LAB_POINT_US 6000000LL
#define RF_PERF_LAB_SAMPLES 65536
#define RF_PERF_LAB_WARMUP_US 2000000LL
#define RF_PERF_LAB_DURATION_US 10000000LL

struct rf_perf_lab_preparation {
    int64_t extract,pack,upload,batch,retire,skin,bytes,enemy_reused,skin_reused;
};
struct rf_perf_lab_point {
    int samples;
    int64_t sum,median_us,p95_us,p99_us;
    int64_t layer_us,actor_us,enemy_us,freeze_us;
};
struct rf_perf_lab_live_result {
    int ticks, legs, done, allies_min, allies_max, gunners_min, gunners_max;
    int combat_frames;
    int64_t step_sum, step_max, ai_us, enemy_us;
    int64_t combat_intervals[RF_PERF_LAB_SAMPLES];
    unsigned layer_queries, layer_nodes, ground_scans, body_scans, waiting;
};
static int rf_perf_live_start(struct rf_game_runtime *runtime);
static int rf_perf_live_return(struct rf_game_runtime *runtime);
static void rf_perf_live_camera(struct rf_game_runtime *runtime,int64_t now);
static void rf_perf_live_collect(struct rf_game_runtime *runtime,int64_t interval);
static struct rf_perf_lab_live_result rf_perf_live_result;
struct rf_perf_lab_result {
    int valid, scene, samples, spawned, min_alive, max_alive;
    int isolated, uncapped, gpu_samples, width, height, aborted;
    unsigned lights, shadow_maps, shadow_draws, present_mode;
    unsigned lights_min,lights_max,shadows_min,shadows_max;
    int64_t logic_us;
    char map[128];
    int64_t median_us, p95_us, p99_us, mean_us;
    int64_t enemy_us, geometry_us, upload_us, world_us, submit_us;
    double gpu_draw_ms;
    unsigned int draws;
    int background,points;
    unsigned lab_mask;
    int64_t layer_us,actor_us,freeze_us,source_us,record_us,sky_us;
    struct rf_perf_lab_preparation preparation;
    struct rf_perf_lab_point point[RF_PERF_LAB_POINTS];
    /* The full interval buffer stays in the live owner, outside the UI result. */
    int live_ticks, live_legs, live_done, combat_frames;
    int allies_min,allies_max,gunners_min,gunners_max;
    int64_t step_mean,step_max,ai_us,enemy_logic_us,combat_p50,combat_p95,combat_p99;
    unsigned layer_queries,layer_nodes,ground_scans,body_scans,waiting;
};
struct rf_perf_lab_state {
    int control_near, result_near, menu_open, result_open, selected, running;
    int spawned, min_alive, max_alive, samples;
    int isolated, uncapped, interference, gpu_samples, invalid, returning;
    int width, height, observe_x, observe_z;
    int64_t logic_sum;
    unsigned lights, shadow_maps, present_mode;
    unsigned lights_min,lights_max,shadows_min,shadows_max;
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
    int point,detail_samples,result_page,saved_isolated,saved_interference,presented;
    int previous_requested[RF_LAB_COUNT],previous_rpm;
    unsigned lab_mask;
    int64_t layer_sum,actor_sum,freeze_sum,source_sum,record_sum,sky_sum;
    struct rf_perf_lab_preparation preparation;
    struct rf_perf_lab_point points[RF_PERF_LAB_POINTS];
};
static struct rf_perf_lab_state rf_perf_lab;
static const char *rf_perf_lab_names[RF_PERF_LAB_SCENES] = {
    "基础负载", "64 名感染体", "24 个密集组件", "感染体与组件叠加",
    "园区巡检：当前展示", "园区巡检：全部展示",
    "实战基线", "枪手混战", "五人跨层通行", "边缘站点巡检"
};
static const char *rf_perf_lab_descriptions[RF_PERF_LAB_SCENES] = {
    "无额外敌人和组件，用作性能对照。",
    "固定 64 名感染体，观察敌人准备与绘制开销。",
    "集中布置 24 个静态组件，观察场景处理开销。",
    "64 名感染体与 24 个组件叠加，测试复合压力。",
    "保留当前展示状态，依次采样园区五个视角。",
    "开启全部展示，巡查五个视角，观察展示叠加负载。",
    "4 名友军对抗 16 名感染体，正常战斗、受伤与死亡。",
    "在实战基线中加入 4 名枪手，对比混战负载。",
    "5 人依次穿行地下层、二层、屋顶和一层，检查导航负载。",
    "采样正式边缘站点进攻阶段的入口、全景、各层与屋顶。"
};
static const char *rf_perf_lab_areas[RF_PERF_LAB_SCENES] = {
    "perf_low_area", "perf_low_area", "perf_low_area", "perf_low_area",
    "perf_low_area", "perf_low_area", "perf_low_area", "perf_low_area",
    "perf_low_area", "perf_low_area"
};
static const char *rf_perf_lab_point_names[RF_PERF_LAB_POINTS]={
    "PARK ENTRANCE","COMPUTER CLOSE","ELECTRONICS","LIGHTING","HALL AND SKY"
};
static int rf_perf_lab_live(void) { return rf_perf_lab.selected>=6; }
static int rf_perf_lab_panorama(void) {
    return rf_perf_lab.selected==4 || rf_perf_lab.selected==5 || rf_perf_lab.selected==9;
}
static const char *rf_perf_lab_point_name(int scene,int point) {
    static const char *frontier[]={"ENTRY","OVERVIEW","WORKSHOP 1F","WORKSHOP 2F","ROOF"};
    return scene==9?frontier[point]:rf_perf_lab_point_names[point];
}
static int64_t rf_perf_lab_duration(void) {
    if(rf_perf_lab.selected==8)return 120000000LL;
    if(rf_perf_lab.selected==6 || rf_perf_lab.selected==7)return 30000000LL;
    return rf_perf_lab_panorama()?RF_PERF_LAB_POINTS*RF_PERF_LAB_POINT_US:RF_PERF_LAB_DURATION_US;
}
static unsigned rf_perf_lab_mask(void) {
    unsigned mask=0;
    for (int i=0;i<RF_LAB_COUNT;++i) if (rf_labs.requested[i]) mask|=1u<<i;
    return mask;
}
static const char *rf_perf_lab_scope(int isolated,int background) {
    return isolated?"ISOLATED":background?"FULL SCENE":"OUTPOST";
}
/* Each stop has its own two-second warmup. Camera changes do not hide or
 * suspend other exhibits; the regular renderer decides visibility. */
static void rf_perf_lab_camera(struct rf_game_runtime *runtime,int64_t now)
{
    if(rf_perf_lab_live()) {rf_perf_live_camera(runtime,now);return;}
    struct camera *c=&runtime->camera;
    struct toy_game_actor *p=toy_game_local_player_actor(&runtime->session->game_state);
    c->x=rf_perf_lab.observe_x;c->z=rf_perf_lab.observe_z;
    c->sy=1024;c->cy=0;c->pitch_sy=0;c->pitch_cy=1024;
    if (rf_perf_lab_panorama()) {
        int point=(int)((now-rf_perf_lab.started_us)/RF_PERF_LAB_POINT_US);
        if(point<0) point=0;
        if(point>=RF_PERF_LAB_POINTS) point=RF_PERF_LAB_POINTS-1;
        rf_perf_lab.point=point;c->y=-350;
        if(point==0) {
            c->x=0;c->z=-26*512;c->sy=0;c->cy=-1024;
            c->pitch_sy=100;c->pitch_cy=1019;
        } else if(point==1) rf_lab_computer_camera(runtime->session,c,"lab-computer-side");
        else if(point==2) rf_electronics_camera(runtime->session,c,"electronics-lab");
        else if(point==3) {
            rf_lab_camera_position(runtime->session,c,"rf_light_lab_area",0,6000);
            c->sy=0;c->cy=-1024;c->pitch_sy=100;c->pitch_cy=1019;
        } else {
            c->x=0;c->z=-26*512;c->sy=0;c->cy=1024;
            c->pitch_sy=512;c->pitch_cy=887;
        }
    }
    if(p) {p->x=c->x;p->z=c->z;p->sy=c->sy;p->cy=c->cy;}
}

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
    rf_perf_lab.point=rf_perf_lab.detail_samples=rf_perf_lab.result_page=0;
    rf_perf_lab.presented=0;
    rf_perf_lab.layer_sum=rf_perf_lab.actor_sum=rf_perf_lab.freeze_sum=0;
    memset(&rf_perf_lab.preparation,0,sizeof(rf_perf_lab.preparation));
    rf_perf_lab.source_sum=rf_perf_lab.record_sum=rf_perf_lab.sky_sum=0;
    memset(rf_perf_lab.points,0,sizeof(rf_perf_lab.points));
}

static int rf_perf_lab_start(struct rf_game_runtime *runtime)
{
    struct rasterfall_session *session = runtime->session;
    struct camera *camera = &runtime->camera;
    struct toy_game_box spawn_box = {0};
    struct toy_game *test_game = &session->game_state;
    struct toy_game_actor *player = toy_game_local_player_actor(test_game);
    int scene = rf_perf_lab.selected;
    if(scene==4 || scene==5 || scene>=8)return -1;
    if (scene < 0 || scene >= RF_PERF_LAB_SCENES || !player ||
        test_game->state != TOY_GAME_PLAYING || test_game->enemies_alive ||
        session->world_id != RASTERFALL_WORLD_OUTPOST) return -1;
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
        &session->map_ops.runtime,rf_perf_lab_areas[scene]);
    if (!area || strcmp(area->kind,"experiment") ||
        area->bounds.max_x-area->bounds.min_x<=2300 ||
        area->bounds.max_z-area->bounds.min_z<=1800) return -1;
    int observe_x=area->bounds.min_x-1300,observe_z=area->origin_z;
    spawn_box.minx=area->bounds.min_x+1100;spawn_box.maxx=area->bounds.max_x-1200;
    spawn_box.minz=area->bounds.min_z+900;spawn_box.maxz=area->bounds.max_z-900;
    rf_perf_lab_clear();
    rf_perf_lab.previous_camera = *camera;
    rf_perf_lab.previous_rng = test_game->rng;
    rf_perf_lab.previous_seed = session->seed;
    rf_perf_lab.previous_player = *player;
    rf_perf_lab.saved_isolated=rf_perf_lab.isolated;
    rf_perf_lab.saved_interference=rf_perf_lab.interference;
    memcpy(rf_perf_lab.previous_requested,rf_labs.requested,sizeof(rf_labs.requested));
    rf_perf_lab.previous_rpm=rf_labs.fan_rpm;
    /* One physical field; content is a transaction owned by the trial. */
    rf_perf_lab.isolated=0;rf_perf_lab.interference=1;
    if(scene==2 || scene==3) {
        if(rasterfall_experiment_replace(session,3,NULL,0,
            "rasterfall/assets/maps/experiment_components.map")<0)return -1;
    }
    if(rf_perf_lab_live()) {
        rf_perf_lab.isolated=0;rf_perf_lab.interference=1;
        rf_perf_lab.menu_open=0;
        if(rf_perf_live_start(runtime)<0) {
            if(rf_perf_live_return(runtime)<0)return -2;
            rf_perf_lab.isolated=rf_perf_lab.saved_isolated;
            rf_perf_lab.interference=rf_perf_lab.saved_interference;
            return -1;
        }
        rf_perf_lab.lab_mask=rf_perf_lab_mask();
        rf_perf_lab.started_us=rf_core_clock_now_us();
        rf_perf_lab.running=1;rf_perf_lab.result_open=0;
        rf_perf_lab_camera(runtime,rf_perf_lab.started_us);
        return 0;
    }
    if(rf_perf_lab_panorama()) {rf_perf_lab.isolated=0;rf_perf_lab.interference=1;}
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
    camera->x = rf_perf_lab.isolated ? session->level.start_x : observe_x;
    camera->z = rf_perf_lab.isolated ? session->level.start_z : observe_z;
    rf_perf_lab.observe_x=camera->x;rf_perf_lab.observe_z=camera->z;
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
    if(scene==5) {
        for(int i=0;i<RF_LAB_COUNT;++i) rf_labs.requested[i]=1;
        rf_labs.fan_rpm=600;
    }
    rf_perf_lab.lab_mask=rf_perf_lab_mask();
    rf_perf_lab_camera(runtime,rf_perf_lab.started_us);
    rf_perf_lab.running = 1;
    rf_perf_lab.menu_open = 0;
    rf_perf_lab.result_open = 0;
    return 0;
failed:
    if(session->experiments && session->experiments->groups[3].active)
        rasterfall_experiment_replace(session,3,NULL,0,NULL);
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
        int offset=0;
        r->points=rf_perf_lab_panorama()?RF_PERF_LAB_POINTS:0;
        for(int i=0;i<r->points;++i) {
            struct rf_perf_lab_point *p=&rf_perf_lab.points[i];
            int k=p->samples;
            if(!k) {rf_perf_lab.invalid=1;continue;}
            qsort(rf_perf_lab.intervals+offset,(size_t)k,sizeof(int64_t),rf_perf_lab_compare_intervals);
            p->median_us=rf_perf_lab.intervals[offset+k/2];
            p->p95_us=rf_perf_lab.intervals[offset+(k*95+99)/100-1];
            p->p99_us=rf_perf_lab.intervals[offset+(k*99+99)/100-1];
            p->layer_us/=k;p->actor_us/=k;p->enemy_us/=k;p->freeze_us/=k;
            r->point[i]=*p;offset+=k;
        }
        qsort(rf_perf_lab.intervals, (size_t)n, sizeof(rf_perf_lab.intervals[0]),
              rf_perf_lab_compare_intervals);
        r->valid = !aborted && !rf_perf_lab.invalid &&
            (rf_perf_lab_live() ? (rf_perf_lab.selected!=8 || rf_perf_live_result.done) :
            (rf_perf_lab.min_alive == rf_perf_lab.spawned &&
            rf_perf_lab.max_alive == rf_perf_lab.spawned)) ? 1 : 2;
        r->isolated = rf_perf_lab.isolated;r->uncapped = rf_perf_lab.uncapped;
        r->aborted = aborted;r->gpu_samples = rf_perf_lab.gpu_samples;
        r->width=rf_perf_lab.width;r->height=rf_perf_lab.height;
        r->lights=rf_perf_lab.lights;r->shadow_maps=rf_perf_lab.shadow_maps;
        r->lights_min=rf_perf_lab.lights_min;r->lights_max=rf_perf_lab.lights_max;
        r->shadows_min=rf_perf_lab.shadows_min;r->shadows_max=rf_perf_lab.shadows_max;
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
        r->background=rf_perf_lab.interference;r->lab_mask=rf_perf_lab.lab_mask;
        r->layer_us=rf_perf_lab.layer_sum/n;r->actor_us=rf_perf_lab.actor_sum/n;
        r->freeze_us=rf_perf_lab.freeze_sum/n;r->source_us=rf_perf_lab.source_sum/n;
        r->record_us=rf_perf_lab.record_sum/n;r->sky_us=rf_perf_lab.sky_sum/n;
        r->preparation=rf_perf_lab.preparation;
        if(rf_perf_lab_live()) {
            struct rf_perf_lab_live_result *l=&rf_perf_live_result;
            r->live_ticks=l->ticks;r->live_legs=l->legs;r->live_done=l->done;
            r->allies_min=l->allies_min;r->allies_max=l->allies_max;
            r->gunners_min=l->gunners_min;r->gunners_max=l->gunners_max;
            r->step_mean=l->ticks?l->step_sum/l->ticks:0;r->step_max=l->step_max;
            r->ai_us=l->ticks?l->ai_us/l->ticks:0;r->enemy_logic_us=l->ticks?l->enemy_us/l->ticks:0;
            r->layer_queries=l->layer_queries;r->layer_nodes=l->layer_nodes;
            r->ground_scans=l->ground_scans;r->body_scans=l->body_scans;r->waiting=l->waiting;
            int k=l->combat_frames;r->combat_frames=k;
            if(k) {
                qsort(l->combat_intervals,(size_t)k,sizeof(int64_t),rf_perf_lab_compare_intervals);
                r->combat_p50=l->combat_intervals[k/2];
                r->combat_p95=l->combat_intervals[(k*95+99)/100-1];
                r->combat_p99=l->combat_intervals[(k*99+99)/100-1];
            }
        }
    }
    if(rf_perf_lab_live()) {
        if(rf_perf_live_return(runtime)<0)return -2;
        rf_perf_lab.isolated=rf_perf_lab.saved_isolated;
        rf_perf_lab.interference=rf_perf_lab.saved_interference;
        rf_perf_lab.menu_open=0;
        return 0;
    }
    if(session->experiments && session->experiments->groups[3].active &&
        rasterfall_experiment_replace(session,3,NULL,0,NULL)<0)return -2;
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
    if(rf_perf_lab_panorama()) {
        memcpy(rf_labs.requested,rf_perf_lab.previous_requested,sizeof(rf_labs.requested));
        rf_labs.fan_rpm=rf_perf_lab.previous_rpm;
    }
    rf_perf_lab.isolated=rf_perf_lab.saved_isolated;
    rf_perf_lab.interference=rf_perf_lab.saved_interference;
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
    if(rf_perf_lab.running && !rf_perf_lab.presented) {
        /* Creating an experiment can synchronously prewarm GPU resources.
         * Start the steady trial clock after that first successful present. */
        rf_perf_lab.presented=1;rf_perf_lab.started_us=now;
        return;
    }
    if (!rf_perf_lab.running || now-rf_perf_lab.started_us < RF_PERF_LAB_WARMUP_US ||
        interval_us <= 0) return;
    if(rf_perf_lab_panorama()) {
        int64_t elapsed=now-rf_perf_lab.started_us;
        if(elapsed/RF_PERF_LAB_POINT_US!=rf_perf_lab.point ||
            elapsed%RF_PERF_LAB_POINT_US<RF_PERF_LAB_WARMUP_US) return;
    }
    if(rf_perf_lab.interference && rf_perf_lab_mask()!=rf_perf_lab.lab_mask) rf_perf_lab.invalid=1;
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
        rf_perf_lab.lights_min=rf_perf_lab.lights_max=lights;
        rf_perf_lab.shadows_min=rf_perf_lab.shadows_max=shadow_maps;
    } else if (width!=rf_perf_lab.width || height!=rf_perf_lab.height ||
               present_mode!=rf_perf_lab.present_mode || (!rf_perf_lab.interference && lights!=rf_perf_lab.lights) ||
               (!rf_perf_lab.interference && shadow_maps!=rf_perf_lab.shadow_maps)) rf_perf_lab.invalid=1;
    if (rf_perf_lab.isolated && lights) rf_perf_lab.invalid=1;
    if(lights<rf_perf_lab.lights_min) rf_perf_lab.lights_min=lights;
    if(lights>rf_perf_lab.lights_max) rf_perf_lab.lights_max=lights;
    if(shadow_maps<rf_perf_lab.shadows_min) rf_perf_lab.shadows_min=shadow_maps;
    if(shadow_maps>rf_perf_lab.shadows_max) rf_perf_lab.shadows_max=shadow_maps;
    rf_perf_lab.draws_sum += draws;
    if (alive < rf_perf_lab.min_alive) rf_perf_lab.min_alive = alive;
    if (alive > rf_perf_lab.max_alive) rf_perf_lab.max_alive = alive;
}

static void rf_perf_lab_sample_detail(int64_t layers,int64_t actors,int64_t freeze,
    int64_t source,int64_t record,int64_t sky,int64_t enemies,
    const struct rf_gpu_scene_world_gpu_probe_stats *gpu)
{
    if(!rf_perf_lab.running || rf_perf_lab.detail_samples==rf_perf_lab.samples) return;
    rf_perf_lab.detail_samples=rf_perf_lab.samples;
    rf_perf_lab.layer_sum+=layers;rf_perf_lab.actor_sum+=actors;rf_perf_lab.freeze_sum+=freeze;
    rf_perf_lab.source_sum+=source;rf_perf_lab.record_sum+=record;rf_perf_lab.sky_sum+=sky;
    if(gpu) {
        struct rf_perf_lab_preparation *p=&rf_perf_lab.preparation;
        p->extract+=gpu->layer_extract_us;p->pack+=gpu->layer_pack_us;
        p->upload+=gpu->layer_upload_us;p->batch+=gpu->layer_batch_us;
        p->retire+=gpu->retire_us;p->skin+=gpu->actor_batch_us;p->bytes+=(int64_t)gpu->upload_bytes;
        p->enemy_reused+=gpu->enemy_geometry_reused;p->skin_reused+=gpu->skin_reused;
    }
    if(rf_perf_lab_panorama()) {
        struct rf_perf_lab_point *p=&rf_perf_lab.points[rf_perf_lab.point];
        p->samples++;p->sum+=rf_perf_lab.intervals[rf_perf_lab.samples-1];
        p->layer_us+=layers;p->actor_us+=actors;p->enemy_us+=enemies;p->freeze_us+=freeze;
    }
}

static void rf_perf_lab_draw(struct rasterfall_canvas *c, int64_t now)
{
    char line[256];
    int x = (c->width-850)/2, y = (c->height-590)/2;
    const struct rf_perf_lab_result *r = &rf_perf_lab.result;
    if (rf_perf_lab.running) {
        int remain = (int)((rf_perf_lab_duration()-(now-rf_perf_lab.started_us)+999999)/1000000);
        if (remain < 0) remain = 0;
        snprintf(line,sizeof(line),"性能实验  %s  剩余 %d 秒",rf_perf_lab_names[rf_perf_lab.selected],remain);
        rasterfall_canvas_text(c, 30, 35, line, 0xC7F2EE);
        if(rf_perf_lab_panorama()) {
            snprintf(line,sizeof(line),"%d/5  %s",rf_perf_lab.point+1,rf_perf_lab_point_name(rf_perf_lab.selected,rf_perf_lab.point));
            rasterfall_canvas_text(c,30,60,line,0xFFFFFF);
        }
        return;
    }
    rasterfall_canvas_rect(c,x,y,850,590,0x172530,230);
    rasterfall_canvas_text(c,x+25,y+22,rf_perf_lab.result_open ?
        "性能实验结果" : "性能实验",0xC7F2EE);
    if (rf_perf_lab.result_open) {
        if(r->valid && rf_perf_lab.result_page && r->scene>=6 && !r->points) {
            snprintf(line,sizeof(line),"LIVE ALLIES %d..%d  GUNNERS %d..%d  INFECTED %d..%d",
                r->allies_min,r->allies_max,r->gunners_min,r->gunners_max,r->min_alive,r->max_alive);
            rasterfall_canvas_text(c,x+25,y+80,line,0xFFFFFF);
            snprintf(line,sizeof(line),"ACTIVE COMBAT %d FRAMES  P50/P95/P99 %.2f / %.2f / %.2f ms",
                r->combat_frames,r->combat_p50/1000.0,r->combat_p95/1000.0,r->combat_p99/1000.0);
            rasterfall_canvas_text(c,x+25,y+130,line,0xFFFFFF);
            snprintf(line,sizeof(line),"FIXED STEPS %d  AVG/MAX %.3f / %.3f ms",r->live_ticks,
                r->step_mean/1000.0,r->step_max/1000.0);
            rasterfall_canvas_text(c,x+25,y+180,line,0xFFFFFF);
            snprintf(line,sizeof(line),"AI %.3f  INFECTED LOGIC %.3f ms / STEP",r->ai_us/1000.0,r->enemy_logic_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+230,line,0xFFFFFF);
            snprintf(line,sizeof(line),"LAYER QUERIES %u  NODES %u",
                r->layer_queries,r->layer_nodes);
            rasterfall_canvas_text(c,x+25,y+280,line,0xFFFFFF);
            snprintf(line,sizeof(line),"GROUND SCANS %u  BODY SCANS %u  NAV WAIT %u",
                r->ground_scans,r->body_scans,r->waiting);
            rasterfall_canvas_text(c,x+25,y+330,line,0xFFFFFF);
            rasterfall_canvas_text(c,x+25,y+395,"STEPS INCLUDE WARMUP; COMBAT FRAMES USE LIVE COUNTS",0x9FB7C5);
            if(r->valid==2)rasterfall_canvas_text(c,x+25,y+450,"ABORTED OR INCOMPLETE: INVALID",0xFFBE8A);
            rasterfall_canvas_text(c,x+25,y+550,"F2 OVERVIEW   ESC CLOSE",0xC7F2EE);
            return;
        }
        if(r->valid && rf_perf_lab.result_page && r->points) {
            rasterfall_canvas_text(c,x+25,y+66,"VIEW                    MEDIAN / P95 / P99 ms",0x9FB7C5);
            for(int i=0;i<r->points;++i) {
                const struct rf_perf_lab_point *p=&r->point[i];
                snprintf(line,sizeof(line),"%s   %.2f / %.2f / %.2f",rf_perf_lab_point_name(r->scene,i),
                    p->median_us/1000.0,p->p95_us/1000.0,p->p99_us/1000.0);
                rasterfall_canvas_text(c,x+25,y+110+i*77,line,0xFFFFFF);
                snprintf(line,sizeof(line),"LAYERS %.2f  ACTORS %.2f  ENEMY %.2f  FREEZE %.2f",
                    p->layer_us/1000.0,p->actor_us/1000.0,p->enemy_us/1000.0,p->freeze_us/1000.0);
                rasterfall_canvas_text(c,x+25,y+135+i*77,line,0x9FB7C5);
            }
            rasterfall_canvas_text(c,x+25,y+550,"F2 OVERVIEW   ESC CLOSE",0xC7F2EE);
            return;
        }
        if (!r->valid) rasterfall_canvas_text(c,x+25,y+80,"NO TEST RESULT",0xFFBE8A);
        else {
            snprintf(line,sizeof(line),"实验：%s",rf_perf_lab_names[r->scene]);
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
            snprintf(line,sizeof(line),"LAYERS %.2f   ACTORS %.2f   FREEZE %.2f ms",
                r->layer_us/1000.0,r->actor_us/1000.0,r->freeze_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+285,line,0xFFFFFF);
        }
        if (r->valid) {
            snprintf(line,sizeof(line),"%s  %s  %dx%d  PRESENT %u",
                rf_perf_lab_scope(r->isolated,r->background),r->uncapped?"UNCAPPED":"120 FPS CAP",
                r->width,r->height,r->present_mode);
            rasterfall_canvas_text(c,x+25,y+325,line,0xC7F2EE);
            snprintf(line,sizeof(line),"LOGIC %.2f  LIGHTS %u..%u  SHADOWS %u..%u / %u",
                r->logic_us/1000.0,r->lights_min,r->lights_max,r->shadows_min,r->shadows_max,r->shadow_draws);
            rasterfall_canvas_text(c,x+25,y+360,line,0xFFFFFF);
            snprintf(line,sizeof(line),"SOURCE %.2f  RECORD %.2f  SKY %.2f ms",
                r->source_us/1000.0,r->record_us/1000.0,r->sky_us/1000.0);
            rasterfall_canvas_text(c,x+25,y+395,line,0x9FB7C5);
            rasterfall_canvas_text(c,x+25,y+465,"GPU TIME OVERLAPS CPU; STAGES DO NOT SUM",0x9FB7C5);
            snprintf(line,sizeof(line),"GPU SAMPLES %d / %d   EXHIBITS %02X",r->gpu_samples,r->samples,r->lab_mask);
            rasterfall_canvas_text(c,x+25,y+500,line,0x9FB7C5);
            if(r->scene>=6 && r->valid==1) {
                snprintf(line,sizeof(line),"LIVE ALLIES %d..%d  GUNNERS %d..%d",
                    r->allies_min,r->allies_max,r->gunners_min,r->gunners_max);
                rasterfall_canvas_text(c,x+25,y+430,line,0xC7F2EE);
            }
        }
        rasterfall_canvas_text(c,x+25,y+550,r->points?"F2 VIEW BREAKDOWN   ESC CLOSE":
            r->scene>=6?"F2 LIVE DETAIL   ESC CLOSE":"ESC CLOSE",0xC7F2EE);
    } else {
        int first=rf_perf_lab.selected<6?0:6,last=first?RF_PERF_LAB_SCENES:6;
        rasterfall_canvas_text(c,x+350,y+22,first?"实战与地图实验  2/2":"基础与园区实验  1/2",0x9FB7C5);
        for (int i=first;i<last;++i) {
            snprintf(line,sizeof(line),"%c  %d  %s",i==rf_perf_lab.selected?'>':' ',i==9?0:i+1,
                rf_perf_lab_names[i]);
            rasterfall_canvas_text(c,x+40,y+75+(i-first)*43,line,
                i==rf_perf_lab.selected?0xC7F2EE:0xFFFFFF);
            rasterfall_canvas_text(c,x+96,y+95+(i-first)*43,rf_perf_lab_descriptions[i],0x9FB7C5);
        }
        snprintf(line,sizeof(line),"F2 场景范围：%s    T 帧率：%s",
            rf_perf_lab_panorama() || rf_perf_lab_live()?"完整场景":
                rf_perf_lab.isolated?"隔离场地":rf_perf_lab.interference?"完整场景":"前哨站背景",
            rf_perf_lab.uncapped?"不限帧":"上限 120 帧");
        rasterfall_canvas_text(c,x+25,y+365,line,0xC7F2EE);
        rasterfall_canvas_text(c,x+25,y+410,rf_perf_lab_live()?
            rf_perf_lab.selected==8?"五人完成四段通行后结束，最长 120 秒；Esc 取消。":
                rf_perf_lab.selected==9?"五个视角共 30 秒，每段预热 2 秒、采样 4 秒；Esc 取消。":
                "实战持续 30 秒，预热 2 秒后采样；Esc 取消。":rf_perf_lab_panorama()?
            "五个视角共 30 秒，每段预热 2 秒、采样 4 秒；Esc 取消。":
            "实验持续 10 秒，预热 2 秒后采样；Esc 取消。",0x9FB7C5);
        rasterfall_canvas_text(c,x+25,y+455,rf_perf_lab_live()?
            "结束或取消后重建前哨站，恢复玩家、视角和展示设置。":rf_perf_lab_panorama() || rf_perf_lab.interference?
            "保留完整背景，展示与灯光继续运行。":
            "暂停非测试展示与动态灯光。",0x9FB7C5);
        rasterfall_canvas_text(c,x+25,y+550,"上下键或数字 1-9/0 选择   Enter 开始   Esc 关闭",0xC7F2EE);
    }
}
