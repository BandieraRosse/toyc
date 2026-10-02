/* Opt-in native whole-loop sampler. It observes the current scene without
 * suspending exhibits, replacing the world or changing the present policy. */
#ifdef TOYC_WINDOWS
static struct {
    int enabled,target,count,invalid,width,height;
    unsigned present_mode;
    int64_t interval[4096],gpu[4096],sky[4096];
    uint64_t draws,shadows,uploads;
} rf_scene_perf;
static void rf_scene_perf_init(int allowed)
{
    memset(&rf_scene_perf,0,sizeof(rf_scene_perf));
    const char *value=getenv("RF_SCENE_PERF_FRAMES");
    int frames=value?atoi(value):0;
    if (allowed && frames>=120 && frames<=4096) {
        rf_scene_perf.enabled=1;rf_scene_perf.target=frames;
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
    rf_scene_perf.draws+=s->native_draws;rf_scene_perf.shadows+=s->shadow_draws;
    rf_scene_perf.uploads+=s->upload_bytes;
    if (rf_scene_perf.count<rf_scene_perf.target) return 0;
    int count=rf_scene_perf.count;int64_t sum=0;
    for (int i=0;i<count;++i) sum+=rf_scene_perf.interval[i];
    qsort(rf_scene_perf.interval,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.gpu,count,sizeof(int64_t),rf_scene_perf_compare);
    qsort(rf_scene_perf.sky,count,sizeof(int64_t),rf_scene_perf_compare);
    __printf("SCENE-PERF valid=%d samples=%d warmup=120 extent=%dx%d present_mode=%u mean_us=%lld p50_us=%lld p95_us=%lld p99_us=%lld gpu_p50_us=%lld gpu_p95_us=%lld sky_p50_us=%lld sky_p95_us=%lld draws=%llu shadow_draws=%llu upload_bytes=%llu\n",
        !rf_scene_perf.invalid,count,width,height,s->present_mode,(long long)(sum/count),
        (long long)rf_scene_perf.interval[count/2],(long long)rf_scene_perf.interval[(count*95+99)/100-1],
        (long long)rf_scene_perf.interval[(count*99+99)/100-1],
        (long long)rf_scene_perf.gpu[count/2],(long long)rf_scene_perf.gpu[(count*95+99)/100-1],
        (long long)rf_scene_perf.sky[count/2],(long long)rf_scene_perf.sky[(count*95+99)/100-1],
        (unsigned long long)(rf_scene_perf.draws/count),(unsigned long long)(rf_scene_perf.shadows/count),
        (unsigned long long)(rf_scene_perf.uploads/count));
    return 1;
}
#endif
