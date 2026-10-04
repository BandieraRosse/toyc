/* A real-clock workload controller for the existing normal native sampler.
 * The absent case uses an explicit generated map with the machine removed.
 * Active starts a real job after the normal cold-resource warmup. */
static struct {
    int mode, started, invalid, samples;
    double minimum_progress, maximum_progress;
} rf_weaver_performance;

static int rf_weaver_perf_initial(int allowed)
{
    const char *mode=rf_weaver_environment("RF_WEAVER_PERF_MODE");
    memset(&rf_weaver_performance,0,sizeof(rf_weaver_performance));
    rf_weaver_performance.minimum_progress=1;
    if(!allowed || !mode || !mode[0])return 0;
    if(!strcmp(mode,"absent"))rf_weaver_performance.mode=1;
    else if(!strcmp(mode,"idle"))rf_weaver_performance.mode=2;
    else if(!strcmp(mode,"active"))rf_weaver_performance.mode=3;
    else return -1;
    return 0;
}
static void rf_weaver_perf_before_frame(struct rasterfall_session *s,int rendered_frames)
{
    if(!rf_weaver_performance.mode)return;
    struct toy_mesh_weaver *w=&s->game_state.weaver;
    if(rf_weaver_performance.mode==3 && !rf_weaver_performance.started && rendered_frames>=60) {
        rf_weaver_performance.started=1;
        const struct rf_weaver_blueprint *b=NULL;
        for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
            if(rf_weaver_blueprints[i].geometry.weapon==TOY_GAME_WEAPON_AK)b=&rf_weaver_blueprints[i];
        if(!b || rasterfall_session_weaver_start(s,&b->geometry))rf_weaver_performance.invalid=1;
        else {
            /* Cross calibration before measurement through the normal rule;
             * the resource ledger accounts for this one-second preroll. */
            toy_game_weaver_update(&s->game_state,1000);
            rasterfall_session_weaver_sync(s);
        }
    }
    if(rendered_frames<120)return;
    rf_weaver_performance.samples++;
    if((rf_weaver_performance.mode==1 && w->enabled) ||
       (rf_weaver_performance.mode==2 && (!w->enabled || w->phase!=TOY_WEAVER_IDLE)) ||
       (rf_weaver_performance.mode==3 && (!w->enabled || w->phase!=TOY_WEAVER_WEAVING || w->pause_reason)))
        rf_weaver_performance.invalid=1;
    if(w->progress<rf_weaver_performance.minimum_progress)rf_weaver_performance.minimum_progress=w->progress;
    if(w->progress>rf_weaver_performance.maximum_progress)rf_weaver_performance.maximum_progress=w->progress;
}
static void rf_weaver_perf_report(const struct rasterfall_session *s)
{
    if(!rf_weaver_performance.mode)return;
    const struct toy_mesh_weaver *w=&s->game_state.weaver;
    __printf("MESH-WEAVER-PERF mode=%s valid=%d frames=%d min_progress=%.9f max_progress=%.9f elapsed_ms=%.6f energy_kj=%.9f produced=%u collected=%u\n",
        rf_weaver_performance.mode==1?"absent":rf_weaver_performance.mode==2?"idle":"active",
        !rf_weaver_performance.invalid && rf_weaver_performance.samples>0,
        rf_weaver_performance.samples,rf_weaver_performance.minimum_progress,
        rf_weaver_performance.maximum_progress,w->elapsed_ms,w->supply.energy_kj,
        w->produced_count,w->collected_count);
}
