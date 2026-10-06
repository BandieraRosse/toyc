/* Opt-in native whole-loop sampler. It observes the current scene without
 * suspending exhibits, replacing the world or changing the present policy. */
#ifdef TOYC_WINDOWS
#include "toy_platform.h"
static int rf_scene_perf_cpu_enabled;
/* Bounded, delayed reporting: no per-frame console IO. Begin-to-begin timing
 * is attached to the preceding frame's work, never the following frame. */
struct rf_scene_cost_sample {
    uint64_t frame,scene_frame,world;
    unsigned mission,scene_warmup;
    int phase,phase_ms,eligible;
    int64_t interval,active,cpu,logic,source,freeze,pose;
    struct rf_gpu_scene_world_gpu_probe_stats gpu;
};
static struct {
    int enabled,count,counter_scope,unknown_scope;
    uint64_t world,eligible_ended,eligible_ranked,excluded,nonpositive;
    unsigned mission,scene_warmup;
    int64_t began,previous_begin,cpu_begin;
    struct rf_scene_cost_sample pending,worst[16];
} rf_scene_cost;
static void rf_scene_cost_begin(int64_t now)
{
    if(!rf_scene_cost.enabled && !rf_scene_perf_cpu_enabled) return;
    if(rf_scene_cost.enabled && (rf_scene_cost.counter_scope?rf_scene_cost.pending.eligible:rf_scene_cost.pending.frame>120) && rf_scene_cost.previous_begin) {
        struct rf_scene_cost_sample value=rf_scene_cost.pending;
        value.interval=now-rf_scene_cost.previous_begin;
        if(rf_scene_cost.counter_scope && value.interval<=0) {
            rf_scene_cost.nonpositive++;goto finish_pending;
        }
        rf_scene_cost.eligible_ranked++;
        int at=rf_scene_cost.count;
        if(at==16) {
            at=0;
            for(int i=1;i<16;++i) if(rf_scene_cost.worst[i].interval<rf_scene_cost.worst[at].interval) at=i;
            if(value.interval<=rf_scene_cost.worst[at].interval) at=-1;
        } else rf_scene_cost.count++;
        if(at>=0) rf_scene_cost.worst[at]=value;
    }
finish_pending:
    memset(&rf_scene_cost.pending,0,sizeof(rf_scene_cost.pending));
    rf_scene_cost.previous_begin=rf_scene_cost.began=now;
    rf_scene_cost.cpu_begin=(int64_t)toy_platform_thread_cpu_us();
}
static void rf_scene_cost_end(uint64_t frame,int64_t logic,int64_t source,int64_t freeze,int64_t pose,
    const struct rf_gpu_scene_world_gpu_probe_stats *gpu,
    const struct rf_game_runtime *runtime,const struct rf_gpu_scene_enemy_frame_v1 *frozen)
{
    if(!rf_scene_cost.enabled) return;
    struct rf_scene_cost_sample *s=&rf_scene_cost.pending;
    s->frame=frame;s->active=rf_core_clock_now_us()-rf_scene_cost.began;
    s->cpu=(int64_t)toy_platform_thread_cpu_us()-rf_scene_cost.cpu_begin;
    s->logic=logic;s->source=source;s->freeze=freeze;s->pose=pose;s->gpu=*gpu;
    /* This is called only after a successful frozen Scene submission. Never
     * read new runtime state at the following begin to label this row. */
    s->scene_frame=frozen->frame_id;s->world=frozen->world_generation;
    const struct rasterfall_session *session=runtime->session;
    s->mission=session->frontier.mission_id;
    s->phase=session->frontier.phase;s->phase_ms=session->frontier.phase_ms;
    int frontier=session->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01 &&
        session->scene_local.world_generation==s->world &&
        session->scene_local.frame_id==s->scene_frame;
    if(frontier) {
        if(rf_scene_cost.world!=s->world || rf_scene_cost.mission!=s->mission) {
            rf_scene_cost.world=s->world;rf_scene_cost.mission=s->mission;
            rf_scene_cost.scene_warmup=0;
        }
        s->scene_warmup=++rf_scene_cost.scene_warmup;
    }
    s->eligible=rf_scene_cost.counter_scope?
        frontier && s->scene_warmup>120 && s->phase==RF_FRONTIER_COUNTERATTACK &&
        s->phase_ms>=16000 && !runtime->lifecycle_paused &&
        session->game_state.state==TOY_GAME_PLAYING && !gpu->bridge_transfers &&
        gpu->gpu_time_valid && gpu->gpu_query_frame==s->scene_frame &&
        gpu->gpu_draw_ms>=0.001 && gpu->gpu_draw_ms<1e12:frame>120;
    if(s->eligible)rf_scene_cost.eligible_ended++;else rf_scene_cost.excluded++;
}
static void rf_scene_cost_report(void)
{
    if(!rf_scene_cost.enabled) return;
    __printf("SCENE-SLOW-META schema=2 scope=%s unknown_scope=%d warmup=120 min_phase_ms=%d cap=16 ranking=interval eligible_ended=%llu eligible_ranked=%llu excluded=%llu nonpositive_interval=%llu selected=%d pending_last_frame_omitted=%d\n",
        rf_scene_cost.counter_scope?"frontier-counter":"global",rf_scene_cost.unknown_scope,
        rf_scene_cost.counter_scope?16000:0,
        (unsigned long long)rf_scene_cost.eligible_ended,(unsigned long long)rf_scene_cost.eligible_ranked,
        (unsigned long long)rf_scene_cost.excluded,(unsigned long long)rf_scene_cost.nonpositive,
        rf_scene_cost.count,rf_scene_cost.pending.frame!=0);
    for(int i=0;i<rf_scene_cost.count;++i) {
        const struct rf_scene_cost_sample *s=&rf_scene_cost.worst[i];
        const struct rf_gpu_scene_world_gpu_probe_stats *g=&s->gpu;
        __printf("SCENE-SLOW frame=%llu scene_frame=%llu world=%llu mission=%u phase=%d phase_ms=%d scene_warmup=%u main_query_frame=%llu interval_us=%lld active_us=%lld thread_cpu_us=%lld logic_us=%lld source_us=%lld freeze_us=%lld pose_us=%lld world_us=%lld actors_us=%lld enemy_us=%lld layers_us=%lld lighting_us=%lld extract_us=%lld clip_us=%lld pack_us=%lld upload_us=%lld batch_us=%lld skin_batch_us=%lld record_us=%lld acquire_us=%lld queue_us=%lld present_us=%lld retire_us=%lld gpu_us=%lld bytes=%llu enemy_reused=%u skin_reused=%u misc_us=%lld weaver_us=%lld aux_us=%lld aux_refresh_mask=%u aux_gpu_valid_mask=%u aux0_before=%llu aux0_after=%llu aux0_cpu_us=%lld aux0_gpu_us=%lld aux1_before=%llu aux1_after=%llu aux1_cpu_us=%lld aux1_gpu_us=%lld enemy_extract_us=%lld enemy_upload_us=%lld enemy_draw_us=%lld enemy_created=%u enemy_triangles=%u\n",
            (unsigned long long)s->frame,(unsigned long long)s->scene_frame,
            (unsigned long long)s->world,s->mission,s->phase,s->phase_ms,s->scene_warmup,
            (unsigned long long)g->gpu_query_frame,(long long)s->interval,(long long)s->active,(long long)s->cpu,
            (long long)s->logic,(long long)s->source,(long long)s->freeze,(long long)s->pose,
            (long long)g->world_prepare_us,(long long)g->actor_prepare_us,(long long)g->enemy_prepare_us,
            (long long)g->layer_prepare_us,(long long)g->lighting_prepare_us,(long long)g->layer_extract_us,(long long)g->layer_clip_us,
            (long long)g->layer_pack_us,(long long)g->layer_upload_us,(long long)g->layer_batch_us,
            (long long)g->actor_batch_us,(long long)g->record_us,(long long)g->acquire_us,
            (long long)g->queue_submit_us,(long long)g->present_us,(long long)g->retire_us,
            (long long)(g->gpu_draw_ms*1000),(unsigned long long)g->upload_bytes,
            g->enemy_geometry_reused,g->skin_reused,
            (long long)g->misc_prepare_us,(long long)g->weaver_prepare_us,(long long)g->aux_prepare_us,
            g->aux_refresh_mask,g->aux_gpu_valid_mask,
            (unsigned long long)g->aux_frames_before[0],(unsigned long long)g->aux_frames_after[0],
            (long long)g->aux_cpu_us[0],(long long)g->aux_gpu_us[0],
            (unsigned long long)g->aux_frames_before[1],(unsigned long long)g->aux_frames_after[1],
            (long long)g->aux_cpu_us[1],(long long)g->aux_gpu_us[1],
            (long long)g->geometry_extract_us,(long long)g->enemy_upload_us,
            (long long)g->enemy_draw_prepare_us,g->dynamic_created,g->enemy_triangles);
    }
}
static struct {
    int enabled,target,count,invalid,width,height;
    unsigned present_mode;
    int64_t interval[4096],gpu[4096],sky[4096],prepare[4096],cpu[4096];
    int64_t shadow[4096],main[4096];
    int64_t detail[9][4096];
    uint64_t light_counts[7];
    uint64_t draws,shadows,uploads,world_us,layers_us,actors_us,enemies_us,skin_us,weaver_us;
} rf_scene_perf;
static void rf_scene_perf_init(int allowed)
{
    memset(&rf_scene_perf,0,sizeof(rf_scene_perf));
    memset(&rf_scene_cost,0,sizeof(rf_scene_cost));
    rf_scene_perf_cpu_enabled=0;
    const char *slow=getenv("RF_GPU_SCENE_PROFILE_SLOW");
    rf_scene_cost.enabled=slow && !strcmp(slow,"1");
    if(rf_scene_cost.enabled) {
        const char *scope=getenv("RF_GPU_SCENE_PROFILE_SLOW_SCOPE");
        rf_scene_cost.counter_scope=scope && !strcmp(scope,"frontier-counter");
        rf_scene_cost.unknown_scope=scope && *scope && strcmp(scope,"global") &&
            !rf_scene_cost.counter_scope;
    }
    const char *value=getenv("RF_SCENE_PERF_FRAMES");
    int frames=value?atoi(value):0;
    if (allowed && frames>=120 && frames<=4096) {
        rf_scene_perf.enabled=1;rf_scene_perf.target=frames;
        rf_scene_perf_cpu_enabled=1;
    }
}
static int rf_scene_perf_compare(const void *a,const void *b)
{
    int64_t x=*(const int64_t *)a,y=*(const int64_t *)b;
    return x>y?1:x<y?-1:0;
}
static int rf_scene_perf_sample(int frame,int64_t interval,
    const struct rf_gpu_scene_world_gpu_probe_stats *s,int width,int height,int paused)
{
    if (!rf_scene_perf.enabled || frame<=120) return 0;
    int n=rf_scene_perf.count++;
    if (!n) {
        rf_scene_perf.width=width;rf_scene_perf.height=height;
        rf_scene_perf.present_mode=s->present_mode;
    }
    if (width!=rf_scene_perf.width || height!=rf_scene_perf.height ||
        s->present_mode!=rf_scene_perf.present_mode || !s->gpu_time_valid ||
        interval<=0 || paused || s->bridge_transfers) rf_scene_perf.invalid=1;
    if(s->light_profile)rf_scene_perf.invalid=1; /* Instrumented fragment shader. */
    for(unsigned i=0;i<9;++i)rf_scene_perf.detail[i][n]=(int64_t)(s->gpu_detail_ms[i]*1000);
    for(unsigned i=0;i<7;++i)rf_scene_perf.light_counts[i]+=s->light_counts[i];
    rf_scene_perf.interval[n]=interval;
    rf_scene_perf.gpu[n]=(int64_t)(s->gpu_draw_ms*1000);
    rf_scene_perf.sky[n]=(int64_t)(s->gpu_sky_ms*1000);
    rf_scene_perf.shadow[n]=(int64_t)(s->gpu_shadow_ms*1000);
    rf_scene_perf.main[n]=(int64_t)(s->gpu_main_ms*1000);
    rf_scene_perf.prepare[n]=s->prepare_us;
    rf_scene_perf.cpu[n]=(int64_t)toy_platform_thread_cpu_us()-rf_scene_cost.cpu_begin;
    rf_scene_perf.world_us+=s->world_prepare_us;rf_scene_perf.layers_us+=s->layer_prepare_us;
    rf_scene_perf.actors_us+=s->actor_prepare_us;rf_scene_perf.enemies_us+=s->enemy_prepare_us;
    rf_scene_perf.skin_us+=s->actor_batch_us;
    rf_scene_perf.weaver_us+=s->weaver_prepare_us;
    rf_scene_perf.draws+=s->native_draws;rf_scene_perf.shadows+=s->shadow_draws;
    rf_scene_perf.uploads+=s->upload_bytes;
    if (rf_scene_perf.count<rf_scene_perf.target) return 0;
    int count=rf_scene_perf.count;int64_t sum=0;
    for (int i=0;i<count;++i) sum+=rf_scene_perf.interval[i];
    qsort(rf_scene_perf.interval,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.gpu,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.sky,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.shadow,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.main,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.prepare,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.cpu,count,sizeof(int64_t),rf_scene_perf_compare);
    for(unsigned i=0;i<9;++i)qsort(rf_scene_perf.detail[i],count,sizeof(int64_t),rf_scene_perf_compare);
    __printf("SCENE-PERF valid=%d samples=%d warmup=120 extent=%dx%d present_mode=%u mean_us=%lld p50_us=%lld p95_us=%lld p99_us=%lld gpu_p50_us=%lld gpu_p95_us=%lld sky_p50_us=%lld sky_p95_us=%lld shadow_p50_us=%lld shadow_p95_us=%lld main_p50_us=%lld main_p95_us=%lld draws=%llu shadow_draws=%llu upload_bytes=%llu prepare_p50_us=%lld prepare_p95_us=%lld world_mean_us=%llu layers_mean_us=%llu actors_mean_us=%llu enemies_mean_us=%llu skin_mean_us=%llu weaver_mean_us=%llu\n",
        !rf_scene_perf.invalid,count,width,height,s->present_mode,(long long)(sum/count),
        (long long)rf_scene_perf.interval[count/2],(long long)rf_scene_perf.interval[(count*95+99)/100-1],
        (long long)rf_scene_perf.interval[(count*99+99)/100-1],
        (long long)rf_scene_perf.gpu[count/2],(long long)rf_scene_perf.gpu[(count*95+99)/100-1],
        (long long)rf_scene_perf.sky[count/2],(long long)rf_scene_perf.sky[(count*95+99)/100-1],
        (long long)rf_scene_perf.shadow[count/2],(long long)rf_scene_perf.shadow[(count*95+99)/100-1],
        (long long)rf_scene_perf.main[count/2],(long long)rf_scene_perf.main[(count*95+99)/100-1],
        (unsigned long long)(rf_scene_perf.draws/count),(unsigned long long)(rf_scene_perf.shadows/count),
        (unsigned long long)(rf_scene_perf.uploads/count),
        (long long)rf_scene_perf.prepare[count/2],
        (long long)rf_scene_perf.prepare[(count*95+99)/100-1],
        (unsigned long long)(rf_scene_perf.world_us/count),(unsigned long long)(rf_scene_perf.layers_us/count),
        (unsigned long long)(rf_scene_perf.actors_us/count),(unsigned long long)(rf_scene_perf.enemies_us/count),
        (unsigned long long)(rf_scene_perf.skin_us/count),(unsigned long long)(rf_scene_perf.weaver_us/count));
    __printf("SCENE-CPU samples=%d thread_cpu_p50_us=%lld thread_cpu_p95_us=%lld\n",count,
        (long long)rf_scene_perf.cpu[count/2],(long long)rf_scene_perf.cpu[(count*95+99)/100-1]);
    const char *stages[]={"tiles","world","transparent","viewmodel","post","hud","gi","receiver","skin"};
    __printf("SCENE-GPU-STAGES diagnostic=%u light_tiles=%u light_ablation=%u gpu_p99_us=%lld",s->light_profile,s->light_tiles,s->light_ablation,
        (long long)rf_scene_perf.gpu[(count*99+99)/100-1]);
    for(unsigned i=0;i<9;++i)__printf(" %s_p50_us=%lld %s_p95_us=%lld %s_p99_us=%lld",stages[i],
        (long long)rf_scene_perf.detail[i][count/2],stages[i],
        (long long)rf_scene_perf.detail[i][(count*95+99)/100-1],stages[i],
        (long long)rf_scene_perf.detail[i][(count*99+99)/100-1]);
    __printf("\n");
    if(s->light_profile) {
        const char *names[]={"shaded","candidates","roof","sun","local","visible","pcf"};
        __printf("SCENE-LIGHT-PROFILE diagnostic=1");
        for(unsigned i=0;i<7;++i)__printf(" %s_mean=%llu",names[i],
            (unsigned long long)(rf_scene_perf.light_counts[i]/count));
        __printf("\n");
    }
    return 1;
}
#endif
