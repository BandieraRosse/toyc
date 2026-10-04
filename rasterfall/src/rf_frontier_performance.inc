/* Optional ordinary-run observer. No gameplay writes, file polling, per-frame
 * output, GPU synchronization changes, or automatic exit. Rows are emitted at
 * shutdown; next begin-to-begin interval belongs to the preceding rendered row. */
#ifdef TOYC_WINDOWS
#define RF_FRONTIER_PERF_CAP 4096
struct rf_frontier_perf_row {
    uint64_t frame,world;
    unsigned mission;
    int phase,phase_ms,width,height,rts,invalid;
    int actors,actors_alive,infected,dying,guards,mission_enemies,periodic,pending;
    int64_t interval,active,cpu,logic,source,freeze,pose,overhead;
    struct rf_gpu_scene_world_gpu_probe_stats gpu;
};
static struct {
    int enabled,contaminated,count[2],dropped[2],warmup_frames;
    uint64_t world;
    unsigned mission;
    int64_t begin,cpu_begin,overhead;
    struct rf_frontier_perf_row pending,*rows[2];
} rf_frontier_perf;
static void rf_frontier_perf_init(int ordinary)
{
    memset(&rf_frontier_perf,0,sizeof(rf_frontier_perf));
    const char *enable=getenv("RF_FRONTIER_PHASE_PERF");
    if(!enable || strcmp(enable,"1"))return;
    rf_frontier_perf.contaminated=!ordinary;
    if(getenv("RF_FRONTIER_AUDIT") || getenv("RF_UI_AUDIT") ||
        getenv("RF_GPU_SCENE_PROFILE_LAYERS") || getenv("RF_GPU_PROFILE_TRIANGLE_UPDATE") ||
        getenv("RF_UI_CAPTURE_DIRECTORY") || getenv("VK_INSTANCE_LAYERS"))
        rf_frontier_perf.contaminated=1;
    for(int i=0;i<2;++i) {
        rf_frontier_perf.rows[i]=calloc(RF_FRONTIER_PERF_CAP,sizeof(*rf_frontier_perf.rows[i]));
        if(!rf_frontier_perf.rows[i]) {
            free(rf_frontier_perf.rows[0]);free(rf_frontier_perf.rows[1]);
            memset(&rf_frontier_perf,0,sizeof(rf_frontier_perf));
            __fprintf(2,"FRONTIER-PERF disabled allocation_failed=1\n");return;
        }
    }
    rf_frontier_perf.enabled=1;
}
static void rf_frontier_perf_begin(int64_t now)
{
    if(!rf_frontier_perf.enabled)return;
    int64_t start=rf_core_clock_now_us();
    struct rf_frontier_perf_row *p=&rf_frontier_perf.pending;
    if(p->frame) {
        int slot=p->phase==RF_FRONTIER_ASSAULT?0:1;
        p->interval=now-rf_frontier_perf.begin;
        if(p->interval<=0)p->invalid|=1;
        if(rf_frontier_perf.count[slot]<RF_FRONTIER_PERF_CAP)
            rf_frontier_perf.rows[slot][rf_frontier_perf.count[slot]++]=*p;
        else rf_frontier_perf.dropped[slot]++;
    }
    memset(p,0,sizeof(*p));
    rf_frontier_perf.begin=now;
    rf_frontier_perf.cpu_begin=(int64_t)toy_platform_thread_cpu_us();
    rf_frontier_perf.overhead+=rf_core_clock_now_us()-start;
}
static void rf_frontier_perf_end(uint64_t frame,const struct rf_game_runtime *r,
    const struct rf_gpu_scene_world_gpu_probe_stats *gpu,int width,int height,int capture,
    int64_t logic,int64_t source,int64_t freeze,int64_t pose)
{
    if(!rf_frontier_perf.enabled)return;
    int64_t start=rf_core_clock_now_us();
    const struct rasterfall_session *s=r->session;
    const struct toy_game *g=&s->game_state;
    const struct rf_frontier_mission *m=&s->frontier;
    if(s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)goto done;
    if(rf_frontier_perf.world!=s->scene_local.world_generation || rf_frontier_perf.mission!=m->mission_id) {
        rf_frontier_perf.world=s->scene_local.world_generation;
        rf_frontier_perf.mission=m->mission_id;rf_frontier_perf.warmup_frames=0;
    }
    if(++rf_frontier_perf.warmup_frames<=120)goto done;
    if(m->phase!=RF_FRONTIER_ASSAULT && m->phase!=RF_FRONTIER_COUNTERATTACK)goto done;
    struct rf_frontier_perf_row *p=&rf_frontier_perf.pending;
    p->frame=frame;p->world=s->scene_local.world_generation;p->mission=m->mission_id;
    p->phase=m->phase;p->phase_ms=m->phase_ms;p->width=width;p->height=height;p->rts=r->rts_active;
    p->active=start-rf_frontier_perf.begin;
    p->cpu=(int64_t)toy_platform_thread_cpu_us()-rf_frontier_perf.cpu_begin;
    p->logic=logic;p->source=source;p->freeze=freeze;p->pose=pose;p->gpu=*gpu;
    /* Flags: timing=1, pause=2, GPU=4, bridge=8, diagnostic=16,
     * capture=32, non-playing=64, gameplay driver=128. */
    if(r->lifecycle_paused)p->invalid|=2;
    if(!gpu->gpu_time_valid || gpu->gpu_draw_ms<=0)p->invalid|=4;
    if(gpu->bridge_transfers)p->invalid|=8;
    if(rf_frontier_perf.contaminated)p->invalid|=16;
    if(capture)p->invalid|=32;
    if(g->state!=TOY_GAME_PLAYING)p->invalid|=64;
    if(rf_weaver_diagnostic.enabled || rf_weaver_performance.mode ||
        rf_perf_lab.running || rf_combat_lab.running)p->invalid|=128;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)if(g->actors[i].active) {
        p->actors++;if(g->actors[i].state==TOY_GAME_ACTOR_ALIVE)p->actors_alive++;
    }
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i) {
        if(g->enemies[i].active==1)p->infected++;
        else if(g->enemies[i].active==2)p->dying++;
    }
    p->guards=m->guards_alive;p->mission_enemies=m->enemies_alive;
    p->periodic=m->periodic_alive;p->pending=m->pending_spawns;
    p->overhead=rf_core_clock_now_us()-start;
done:
    rf_frontier_perf.overhead+=rf_core_clock_now_us()-start;
}
static void rf_frontier_perf_report(void)
{
    if(!rf_frontier_perf.enabled)return;
    __printf("FRONTIER-PERF-META schema=1 warmup=120 cap_per_phase=%d assault_samples=%d counter_samples=%d assault_dropped=%d counter_dropped=%d collector_us=%lld pending_last_frame_omitted=%d\n",
        RF_FRONTIER_PERF_CAP,rf_frontier_perf.count[0],rf_frontier_perf.count[1],
        rf_frontier_perf.dropped[0],rf_frontier_perf.dropped[1],
        (long long)rf_frontier_perf.overhead,rf_frontier_perf.pending.frame!=0);
    for(int slot=0;slot<2;++slot) {
        for(int i=0;i<rf_frontier_perf.count[slot];++i) {
            const struct rf_frontier_perf_row *p=&rf_frontier_perf.rows[slot][i];
            const struct rf_gpu_scene_world_gpu_probe_stats *g=&p->gpu;
            __printf("FRONTIER-PERF-SAMPLE frame=%llu world=%llu mission=%u phase=%d phase_ms=%d invalid=%d width=%d height=%d present_mode=%u rts=%d actors=%d actors_alive=%d infected=%d dying=%d guards=%d mission_enemies=%d periodic=%d pending=%d interval_us=%lld active_us=%lld thread_cpu_us=%lld gpu_us=%lld sky_us=%lld logic_us=%lld source_us=%lld freeze_us=%lld pose_us=%lld prepare_us=%lld world_us=%lld actors_us=%lld enemies_us=%lld layers_us=%lld skin_us=%lld record_us=%lld acquire_us=%lld queue_us=%lld present_us=%lld retire_us=%lld bytes=%llu draws=%u shadows=%u collector_us=%lld\n",
                (unsigned long long)p->frame,(unsigned long long)p->world,p->mission,p->phase,p->phase_ms,p->invalid,
                p->width,p->height,g->present_mode,p->rts,p->actors,p->actors_alive,p->infected,p->dying,
                p->guards,p->mission_enemies,p->periodic,p->pending,
                (long long)p->interval,(long long)p->active,(long long)p->cpu,
                (long long)(g->gpu_draw_ms*1000),(long long)(g->gpu_sky_ms*1000),
                (long long)p->logic,(long long)p->source,(long long)p->freeze,(long long)p->pose,
                (long long)g->prepare_us,(long long)g->world_prepare_us,(long long)g->actor_prepare_us,
                (long long)g->enemy_prepare_us,(long long)g->layer_prepare_us,(long long)g->actor_batch_us,
                (long long)g->record_us,(long long)g->acquire_us,(long long)g->queue_submit_us,
                (long long)g->present_us,(long long)g->retire_us,(unsigned long long)g->upload_bytes,
                g->native_draws,g->shadow_draws,(long long)p->overhead);
        }
        free(rf_frontier_perf.rows[slot]);rf_frontier_perf.rows[slot]=NULL;
    }
    rf_frontier_perf.enabled=0;
}
#endif
