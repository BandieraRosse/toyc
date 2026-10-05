/* Live pressure trials only configure worlds and ordinary RTS orders. The
 * normal runtime/session fixed step owns AI, damage, movement and death. */
static struct {
    struct toy_game_update_profile profile;
    int route_index[5], goal_x[5],goal_y,goal_z[5], ordered;
    unsigned world_generation;
} rf_perf_live;

static int rf_perf_live_start(struct rf_game_runtime *r)
{
    struct rasterfall_session *s=r->session;
    memset(&rf_perf_live,0,sizeof(rf_perf_live));
    memset(&rf_perf_live_result,0,sizeof(rf_perf_live_result));
    rf_perf_live_result.allies_min=rf_perf_live_result.gunners_min=TOY_GAME_MAX_ACTORS;
    enum rasterfall_world_id world=rf_perf_lab.selected==9?
        RASTERFALL_WORLD_FRONTIER_STATION_01:RASTERFALL_WORLD_OUTPOST;
    s->seed=0x504552464c414231ULL;
    if(rf_game_request_world(r,world)<0)return -1;
    if(world==RASTERFALL_WORLD_OUTPOST) {
        memcpy(rf_labs.requested,rf_perf_lab.previous_requested,sizeof(rf_labs.requested));
        rf_labs.fan_rpm=rf_perf_lab.previous_rpm;
    }
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
    } else if(rf_perf_lab.selected==8) {
        struct toy_game_actor *p=toy_game_local_player_actor(g);
        rf_perf_live.route_index[0]=TOY_GAME_PLAYER_ACTOR_INDEX;
        for(int i=1;i<5;++i) {
            int id=toy_game_add_ai(g,TOY_GAME_AI_LEVEL_1,p->x+i*420,p->z,"STOREY PROBE");
            struct toy_game_actor *a=toy_game_actor_by_id(g,id);
            if(!a)return -1;
            a->flag_index=-1;a->deployment_x=a->x;a->deployment_z=a->z;
            rf_perf_live.route_index[i]=(int)(a-g->actors);
        }
        r->rts_active=1;r->rts_follow_player=1;r->rts_camera_distance=12000;
        rasterfall_session_set_rts(s,1);
    } else {
        /* Formal map and mission content: do not fake counterattack or use
         * renderer-only actor copies to inflate the workload. */
        r->rts_active=0;r->rts_follow_player=0;
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
    if(rf_game_request_world(r,RASTERFALL_WORLD_OUTPOST)<0)return -1;
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
    if(rf_perf_lab.selected==6 || rf_perf_lab.selected==7) {
        /* Observation moves the camera, never a combatant's body. */
        r->camera.x=rf_perf_lab.observe_x;r->camera.z=rf_perf_lab.observe_z;
        r->camera.y=RASTERFALL_STANDING_CAMERA_Y;
        r->camera.sy=1024;r->camera.cy=0;r->camera.pitch_sy=0;r->camera.pitch_cy=1024;
        return;
    }
    if(rf_perf_lab.selected==8)return; /* Ordinary RTS follow camera. */
    int point=(int)((now-rf_perf_lab.started_us)/RF_PERF_LAB_POINT_US);
    if(point<0)point=0;
    if(point>=RF_PERF_LAB_POINTS)point=RF_PERF_LAB_POINTS-1;
    if(point==rf_perf_lab.point && rf_perf_live.ordered)return;
    rf_perf_lab.point=point;rf_perf_live.ordered=1;
    struct rasterfall_session *s=r->session;
    rf_rts_sync(&r->rts,&s->game_state,s->scene_local.world_generation);
    rf_rts_floors_load(&r->rts,&s->map_ops.runtime,s->scene_local.world_generation);
    rf_rts_floor_focus(r,-1,0);
    r->rts_active=point!=0;r->rts_follow_player=0;
    rasterfall_session_set_rts(s,r->rts_active);
    if(point==0) {
        r->camera.x=s->level.start_x;r->camera.z=s->level.start_z;
        r->camera.y=RASTERFALL_STANDING_CAMERA_Y;r->camera.sy=0;r->camera.cy=1024;
        r->camera.pitch_sy=0;r->camera.pitch_cy=1024;
    } else if(point==1) {
        const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
            &s->map_ops.runtime,"frontier_station_bounds");
        if(!area) {rf_perf_lab.invalid=1;return;}
        r->rts_camera_x=area->origin_x;r->rts_camera_z=area->origin_z;
        r->rts_camera_distance=33000;
    } else {
        r->rts_camera_distance=16000;
        int found=0;
        for(int i=0;i<r->rts.floor_count;++i)if(r->rts.floors[i].floor_level==point-1) {
            rf_rts_floor_focus(r,i,1);found=1;break;
        }
        if(!found)rf_perf_lab.invalid=1;
        r->rts.cutaway_amount=point<4?1:0;
    }
}

static void rf_perf_live_before_step(struct rf_game_runtime *r)
{
    if(rf_perf_lab.selected!=8 || rf_perf_live.ordered || rf_perf_live_result.done)return;
    struct rasterfall_session *s=r->session;
    struct toy_game *g=&s->game_state;
    const char *slabs[]={"outpost_basement_surface","outpost_2f_hall_surface","outpost_roof_hall_surface"};
    int leg=rf_perf_live_result.legs,tx=s->level.start_x,tz=s->level.start_z,ty=0;
    if(leg<3) {
        const struct rf_map_runtime_surface *f=rf_map_runtime_find_surface(&s->map_ops.runtime,slabs[leg]);
        if(!f){rf_perf_lab.invalid=1;return;}
        tx=(f->bounds.min_x+f->bounds.max_x)/2;
        tz=(f->bounds.min_z+f->bounds.max_z)/2;ty=f->height;
    }
    static const int offsets[5][2]={{0,0},{-600,-600},{600,-600},{-600,600},{600,600}};
    static const int return_offsets[5]={0,-600,600,-1200,1200};
    rf_perf_live.goal_y=ty;
    for(int i=0;i<5;++i) {
        int index=rf_perf_live.route_index[i];struct toy_game_actor *a=&g->actors[index];
        /* The entry apron is shallow: spread along its width so every full
         * actor footprint has authored support, just as a real RTS click. */
        rf_perf_live.goal_x[i]=tx+(leg==3?return_offsets[i]:offsets[i][0]);
        rf_perf_live.goal_z[i]=tz+(leg==3?0:offsets[i][1]);
        if(!rasterfall_session_rts_order_actor_height(s,index,a->actor_id,a->combat_generation,
            rf_perf_live.goal_x[i],ty,rf_perf_live.goal_z[i],0,1))rf_perf_lab.invalid=1;
    }
    rf_perf_live.ordered=1;
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
    if(rf_perf_lab.selected!=8 || !rf_perf_live.ordered || l->done)return;
    struct toy_game *g=&r->session->game_state;
    int arrived=0;
    for(int i=0;i<5;++i) {
        struct toy_game_actor *a=&g->actors[rf_perf_live.route_index[i]];
        long long dx=(long long)a->x-rf_perf_live.goal_x[i],dz=(long long)a->z-rf_perf_live.goal_z[i];
        if(a->active && a->state==TOY_GAME_ACTOR_ALIVE && dx*dx+dz*dz<=250LL*250 &&
            abs(a->ground_y-rf_perf_live.goal_y)<=2)arrived++;
    }
    if(arrived==5) {
        l->legs++;rf_perf_live.ordered=0;
        __printf("PERF-LAB route leg=%d ticks=%d step_max_us=%lld\n",l->legs,l->ticks,(long long)l->step_max);
        if(l->legs==4)l->done=1;
    }
}

static void rf_perf_live_dump_route(struct rf_game_runtime *r)
{
    struct toy_game *g=&r->session->game_state;
    for(int i=0;i<5;++i) {
        const struct toy_game_actor *a=&g->actors[rf_perf_live.route_index[i]];
        __printf("PERF-LAB route-pending actor=%d position=%d,%d,%d goal=%d,%d,%d nav=%d cursor=%d moving=%d\n",
            a->actor_id,a->x,a->ground_y,a->z,rf_perf_live.goal_x[i],rf_perf_live.goal_y,
            rf_perf_live.goal_z[i],a->nav_layer_count,a->nav_layer_cursor,a->moving);
    }
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
