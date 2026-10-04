/* Opt-in native whole-loop sampler. It observes the current scene without
 * suspending exhibits, replacing the world or changing the present policy. */
#ifdef TOYC_WINDOWS
#include "toy_platform.h"
static int rf_scene_perf_cpu_enabled;
/* Bounded, delayed reporting: no per-frame console IO. Begin-to-begin timing
 * is attached to the preceding frame's work, never the following frame. */
struct rf_scene_cost_sample {
    uint64_t frame;
    int64_t interval,active,cpu,logic,source,freeze,pose;
    struct rf_gpu_scene_world_gpu_probe_stats gpu;
};
static struct {
    int enabled,count;
    int64_t began,previous_begin,cpu_begin;
    struct rf_scene_cost_sample pending,worst[16];
} rf_scene_cost;
static void rf_scene_cost_begin(int64_t now)
{
    if(!rf_scene_cost.enabled && !rf_scene_perf_cpu_enabled) return;
    if(rf_scene_cost.enabled && rf_scene_cost.pending.frame>120 && rf_scene_cost.previous_begin) {
        struct rf_scene_cost_sample value=rf_scene_cost.pending;
        value.interval=now-rf_scene_cost.previous_begin;
        int at=rf_scene_cost.count;
        if(at==16) {
            at=0;
            for(int i=1;i<16;++i) if(rf_scene_cost.worst[i].interval<rf_scene_cost.worst[at].interval) at=i;
            if(value.interval<=rf_scene_cost.worst[at].interval) at=-1;
        } else rf_scene_cost.count++;
        if(at>=0) rf_scene_cost.worst[at]=value;
    }
    memset(&rf_scene_cost.pending,0,sizeof(rf_scene_cost.pending));
    rf_scene_cost.previous_begin=rf_scene_cost.began=now;
    rf_scene_cost.cpu_begin=(int64_t)toy_platform_thread_cpu_us();
}
static void rf_scene_cost_end(uint64_t frame,int64_t logic,int64_t source,int64_t freeze,int64_t pose,
    const struct rf_gpu_scene_world_gpu_probe_stats *gpu)
{
    if(!rf_scene_cost.enabled) return;
    struct rf_scene_cost_sample *s=&rf_scene_cost.pending;
    s->frame=frame;s->active=rf_core_clock_now_us()-rf_scene_cost.began;
    s->cpu=(int64_t)toy_platform_thread_cpu_us()-rf_scene_cost.cpu_begin;
    s->logic=logic;s->source=source;s->freeze=freeze;s->pose=pose;s->gpu=*gpu;
}
static void rf_scene_cost_report(void)
{
    if(!rf_scene_cost.enabled) return;
    for(int i=0;i<rf_scene_cost.count;++i) {
        const struct rf_scene_cost_sample *s=&rf_scene_cost.worst[i];
        const struct rf_gpu_scene_world_gpu_probe_stats *g=&s->gpu;
        __printf("SCENE-SLOW frame=%llu interval_us=%lld active_us=%lld thread_cpu_us=%lld logic_us=%lld source_us=%lld freeze_us=%lld pose_us=%lld world_us=%lld actors_us=%lld enemy_us=%lld layers_us=%lld extract_us=%lld clip_us=%lld pack_us=%lld upload_us=%lld batch_us=%lld skin_batch_us=%lld record_us=%lld acquire_us=%lld queue_us=%lld present_us=%lld retire_us=%lld gpu_us=%lld bytes=%llu enemy_reused=%u skin_reused=%u\n",
            (unsigned long long)s->frame,(long long)s->interval,(long long)s->active,(long long)s->cpu,
            (long long)s->logic,(long long)s->source,(long long)s->freeze,(long long)s->pose,
            (long long)g->world_prepare_us,(long long)g->actor_prepare_us,(long long)g->enemy_prepare_us,
            (long long)g->layer_prepare_us,(long long)g->layer_extract_us,(long long)g->layer_clip_us,
            (long long)g->layer_pack_us,(long long)g->layer_upload_us,(long long)g->layer_batch_us,
            (long long)g->actor_batch_us,(long long)g->record_us,(long long)g->acquire_us,
            (long long)g->queue_submit_us,(long long)g->present_us,(long long)g->retire_us,
            (long long)(g->gpu_draw_ms*1000),(unsigned long long)g->upload_bytes,
            g->enemy_geometry_reused,g->skin_reused);
    }
}
static struct {
    int enabled,target,count,invalid,width,height;
    unsigned present_mode;
    int64_t interval[4096],gpu[4096],sky[4096],prepare[4096],cpu[4096];
    uint64_t draws,shadows,uploads,world_us,layers_us,actors_us,enemies_us,skin_us,weaver_us;
} rf_scene_perf;
static void rf_scene_perf_init(int allowed)
{
    memset(&rf_scene_perf,0,sizeof(rf_scene_perf));
    memset(&rf_scene_cost,0,sizeof(rf_scene_cost));
    rf_scene_perf_cpu_enabled=0;
    const char *slow=getenv("RF_GPU_SCENE_PROFILE_SLOW");
    rf_scene_cost.enabled=slow && !strcmp(slow,"1");
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
    rf_scene_perf.interval[n]=interval;
    rf_scene_perf.gpu[n]=(int64_t)(s->gpu_draw_ms*1000);
    rf_scene_perf.sky[n]=(int64_t)(s->gpu_sky_ms*1000);
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
    qsort(rf_scene_perf.prepare,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.cpu,count,sizeof(int64_t),rf_scene_perf_compare);
    __printf("SCENE-PERF valid=%d samples=%d warmup=120 extent=%dx%d present_mode=%u mean_us=%lld p50_us=%lld p95_us=%lld p99_us=%lld gpu_p50_us=%lld gpu_p95_us=%lld sky_p50_us=%lld sky_p95_us=%lld draws=%llu shadow_draws=%llu upload_bytes=%llu prepare_p50_us=%lld prepare_p95_us=%lld world_mean_us=%llu layers_mean_us=%llu actors_mean_us=%llu enemies_mean_us=%llu skin_mean_us=%llu weaver_mean_us=%llu\n",
        !rf_scene_perf.invalid,count,width,height,s->present_mode,(long long)(sum/count),
        (long long)rf_scene_perf.interval[count/2],(long long)rf_scene_perf.interval[(count*95+99)/100-1],
        (long long)rf_scene_perf.interval[(count*99+99)/100-1],
        (long long)rf_scene_perf.gpu[count/2],(long long)rf_scene_perf.gpu[(count*95+99)/100-1],
        (long long)rf_scene_perf.sky[count/2],(long long)rf_scene_perf.sky[(count*95+99)/100-1],
        (unsigned long long)(rf_scene_perf.draws/count),(unsigned long long)(rf_scene_perf.shadows/count),
        (unsigned long long)(rf_scene_perf.uploads/count),
        (long long)rf_scene_perf.prepare[count/2],
        (long long)rf_scene_perf.prepare[(count*95+99)/100-1],
        (unsigned long long)(rf_scene_perf.world_us/count),(unsigned long long)(rf_scene_perf.layers_us/count),
        (unsigned long long)(rf_scene_perf.actors_us/count),(unsigned long long)(rf_scene_perf.enemies_us/count),
        (unsigned long long)(rf_scene_perf.skin_us/count),(unsigned long long)(rf_scene_perf.weaver_us/count));
    __printf("SCENE-CPU samples=%d thread_cpu_p50_us=%lld thread_cpu_p95_us=%lld\n",count,
        (long long)rf_scene_perf.cpu[count/2],(long long)rf_scene_perf.cpu[(count*95+99)/100-1]);
    return 1;
}
#endif
