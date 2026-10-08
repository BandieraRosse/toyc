/* Live pressure trials only configure worlds and ordinary RTS orders. The
 * normal runtime/session fixed step owns AI, damage, movement and death. */
static struct {
    struct toy_game_update_profile profile;
    unsigned world_generation;
} rf_perf_live;

static int rf_perf_live_start(struct rf_game_runtime *r)
{
    struct rasterfall_session *s=r->session;
    memset(&rf_perf_live,0,sizeof(rf_perf_live));
    memset(&rf_perf_live_result,0,sizeof(rf_perf_live_result));
    rf_perf_live_result.allies_min=rf_perf_live_result.gunners_min=TOY_GAME_MAX_ACTORS;
    if(rf_perf_lab.selected!=6 && rf_perf_lab.selected!=7)return -1;
    memcpy(rf_labs.requested,rf_perf_lab.previous_requested,sizeof(rf_labs.requested));
    rf_labs.fan_rpm=rf_perf_lab.previous_rpm;
    rasterfall_effects_init(active_effects);
    struct toy_game *g=&s->game_state;
    rf_perf_live.world_generation=s->scene_local.world_generation;
    if(rf_perf_lab.selected==6 || rf_perf_lab.selected==7) {
        /* Reuse the combat field's exact placement/equipment presets, then
         * relinquish its death-slot reservation and result/timer machinery. */
        if(rf_combat_start(r,rf_perf_lab.selected==6?19:20,1,1337,0)<0)return -1;
        rf_combat_lab.running=0;
        rf_perf_lab.spawned=16;
        rf_perf_lab.observe_x=r->camera.x;rf_perf_lab.observe_z=r->camera.z;
    }
    rf_perf_live.profile.clock_us=rf_core_clock_now_us;
    g->update_profile=&rf_perf_live.profile;
    return 0;
}

static int rf_perf_live_return(struct rf_game_runtime *r)
{
    if(rf_perf_lab.selected==6 || rf_perf_lab.selected==7) {
        if(rf_combat_lab.member_count)rf_combat_cleanup(r);
        rf_combat_lab.menu_open=rf_combat_lab.result_open=0;
    }
    r->session->game_state.update_profile=NULL;
    r->session->seed=rf_perf_lab.previous_seed;
    rasterfall_effects_init(active_effects);
    *toy_game_local_player_actor(&r->session->game_state)=rf_perf_lab.previous_player;
    r->session->game_state.rng=rf_perf_lab.previous_rng;
    r->camera=rf_perf_lab.previous_camera;
    memcpy(rf_labs.requested,rf_perf_lab.previous_requested,sizeof(rf_labs.requested));
    rf_labs.fan_rpm=rf_perf_lab.previous_rpm;
    rasterfall_session_set_rts(r->session,0);
    r->rts_active=r->rts_follow_player=0;r->rts_selected=-1;
    return 0;
}

static void rf_perf_live_camera(struct rf_game_runtime *r,int64_t now)
{
    (void)now;
    r->camera.x=rf_perf_lab.observe_x;r->camera.z=rf_perf_lab.observe_z;
    r->camera.y=RASTERFALL_STANDING_CAMERA_Y;
    r->camera.sy=1024;r->camera.cy=0;r->camera.pitch_sy=0;r->camera.pitch_cy=1024;
}



static void rf_perf_live_after_step(struct rf_game_runtime *r,int64_t elapsed)
{
    struct rf_perf_lab_live_result *l=&rf_perf_live_result;
    struct toy_game_update_profile *p=&rf_perf_live.profile;
    l->ticks++;l->step_sum+=elapsed;if(elapsed>l->step_max)l->step_max=elapsed;
    l->ai_us=p->teammate_us;l->enemy_us=p->enemy_us;
    l->layer_queries=p->actor_layer_queries;l->layer_nodes=p->actor_layer_nodes;
    l->ground_scans=p->ground_scans;l->body_scans=p->body_scans;
    l->waiting=p->nav_group_waiting+p->flow_waiting;
    if(r->session->scene_local.world_generation!=rf_perf_live.world_generation)rf_perf_lab.invalid=1;
}



static void rf_perf_live_collect(struct rf_game_runtime *r,int64_t interval)
{
    struct rf_perf_lab_live_result *l=&rf_perf_live_result;
    struct toy_game *g=&r->session->game_state;
    int allies=0,gunners=0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *a=&g->actors[i];
        if(!a->active || a->state!=TOY_GAME_ACTOR_ALIVE || a->developer_only)continue;
        if(rf_perf_lab.selected==6 || rf_perf_lab.selected==7) {
            int owned=0;
            for(int j=0;j<rf_combat_lab.member_count;++j)
                if(rf_combat_lab.members[j].enemy_slot<0 && rf_combat_owned_actor(g,&rf_combat_lab.members[j])==a)owned=1;
            if(!owned)continue;
        }
        if(a->faction==TOY_GAME_FACTION_HOSTILE)gunners++;else allies++;
    }
    if(allies<l->allies_min)l->allies_min=allies;
    if(allies>l->allies_max)l->allies_max=allies;
    if(gunners<l->gunners_min)l->gunners_min=gunners;
    if(gunners>l->gunners_max)l->gunners_max=gunners;
    /* Separate active combat from the quiet tail; do not respawn, heal or
     * reserve a death pose just to hold an artificial constant population. */
    if(allies && (gunners || g->enemies_alive) && l->combat_frames<RF_PERF_LAB_SAMPLES)
        l->combat_intervals[l->combat_frames++]=interval;
}
