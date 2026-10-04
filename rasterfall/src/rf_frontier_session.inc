/* Local action policy. Map records supply stable spatial bindings; session
 * owns the mission, device permissions and outcome. Presentation only reads. */
static const char *frontier_facility_ids[3] = {
    "frontier_workshop_terminal", "frontier_storage_terminal", "frontier_energy_terminal"
};

static unsigned frontier_identity(const char *id)
{
    unsigned hash=2166136261u;
    while(*id)hash=(hash^(unsigned char)*id++)*16777619u;
    return hash?hash:1;
}

static int frontier_bind_region(const struct rasterfall_session *s,
    const char *id,const char *kind,struct rf_frontier_point *point)
{
    const struct rf_map_runtime_region *r=rf_map_runtime_find_region(&s->map_ops.runtime,id);
    if(!r || strcmp(r->kind,kind)) {
        __fprintf(2,"FRONTIER binding missing/wrong kind: %s\n",id);return 0;
    }
    snprintf(point->id,sizeof(point->id),"%s",id);
    point->x=r->bounds.min_x+(r->bounds.max_x-r->bounds.min_x)/2;
    point->z=r->bounds.min_z+(r->bounds.max_z-r->bounds.min_z)/2;
    return 1;
}

static int session_frontier_bind(struct rasterfall_session *s)
{
    static const char *zones[]={"gate","storage","workshop","energy"};
    static const int weapons[]={
        TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_SMG,TOY_GAME_WEAPON_AK,
        TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_SMG,TOY_GAME_WEAPON_SHOTGUN,
        TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_SHOTGUN,
        TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_SMG,TOY_GAME_WEAPON_AK
    };
    struct rf_frontier_config *c=&s->frontier_config;
    struct rf_frontier_point rally;
    if(s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)return 1;
    memset(c,0,sizeof(*c));c->offline_authority=1;
    for(int i=0;i<12;++i) {
        char id[64];snprintf(id,sizeof(id),"frontier_guard_%s_%d",zones[i/3],i%3);
        if(!frontier_bind_region(s,id,"mission_guard",&c->guards[i].point))return 0;
        c->guards[i].weapon=weapons[i];c->guards[i].elite=i==8 || i==11;
        c->guards[i].guard_radius=i<3?6144:7168;
    }
    if(!frontier_bind_region(s,"frontier_spawn_north","mission_entry",&c->north) ||
       !frontier_bind_region(s,"frontier_spawn_east","mission_entry",&c->east) ||
       !frontier_bind_region(s,"frontier_spawn_west","mission_entry",&c->west) ||
       !frontier_bind_region(s,"frontier_rally_courtyard","mission_route",&rally))return 0;
    c->assault_x=rally.x;c->assault_z=rally.z;
    for(int i=0;i<3;++i) {
        const struct rf_map_runtime_object *o=rf_map_runtime_find_object(&s->map_ops.runtime,frontier_facility_ids[i]);
        if(!o){__fprintf(2,"FRONTIER facility missing: %s\n",frontier_facility_ids[i]);return 0;}
        snprintf(c->facilities[i].point.id,sizeof(c->facilities[i].point.id),"%s",o->id);
        c->facilities[i].point.x=o->x;c->facilities[i].point.z=o->z;
        c->facilities[i].identity=frontier_identity(o->id);
        c->facilities[i].generation=(unsigned)s->scene_local.world_generation;
        if(!c->facilities[i].generation)c->facilities[i].generation=1;
        c->facilities[i].interact_range=900;
    }
    if(!rf_map_runtime_find_object(&s->map_ops.runtime,"mesh_weaver")) {
        __fprintf(2,"FRONTIER machine missing: mesh_weaver\n");return 0;
    }
    return 1;
}

static void session_frontier_reset(struct rasterfall_session *s)
{
    memset(&s->frontier,0,sizeof(s->frontier));
    if(s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)return;
    s->game_state.external_director=1;
    s->game_state.to_spawn=0;
    struct toy_game_actor *p=toy_game_local_player_actor(&s->game_state);
    p->class_id=TOY_GAME_AI_LEVEL_2;
    toy_game_actor_reset_combat(p,1);
    toy_game_actor_equip_weapon(&s->game_state,p,TOY_GAME_WEAPON_AK);
    struct toy_mesh_weaver_supply supply=s->game_state.weaver.supply;
    supply.power_on=supply.cpu_on=supply.x1_on=0;
    supply.storage_bytes=0;
    toy_game_weaver_set_supply(&s->game_state,&supply);
    if(!session_frontier_bind(s) || !rf_frontier_mission_begin(&s->frontier,
        &s->game_state,&s->frontier_config,(unsigned)s->scene_local.world_generation)) {
        s->game_state.state=TOY_GAME_OVER;
        s->banner_text="站点任务初始化失败，请返回前哨站";s->banner_ms=10000;
        __fprintf(2,"FRONTIER initialization failed\n");return;
    }
    __printf("FRONTIER begin mission=%u guards=%d squad=5\n",s->frontier.mission_id,s->frontier.guards_alive);
}

int rasterfall_session_frontier_near(const struct rasterfall_session *s,int facing)
{
    if(!s || s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01 ||
       s->frontier.phase==RF_FRONTIER_INACTIVE || s->frontier.phase==RF_FRONTIER_FAILED)return -1;
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&s->game_state);
    if(!p || !p->active || p->state!=TOY_GAME_ACTOR_ALIVE || p->control_disabled)return -1;
    int best=-1;long long best_distance=0;
    for(int i=0;i<3;++i) {
        const struct rf_map_runtime_object *o=rf_map_runtime_find_object(&s->map_ops.runtime,frontier_facility_ids[i]);
        if(!o || abs(p->ground_y+p->airborne_y-o->y)>512)continue;
        long long dx=(long long)o->x-p->x,dz=(long long)o->z-p->z,d2=dx*dx+dz*dz;
        long long dot=dx*p->sy+dz*p->cy;
        int range=s->frontier.config.facilities[i].interact_range;
        if(d2>(long long)range*range || (facing && d2>128LL*128 &&
            (dot<=0 || dot*dot<d2*1024LL*1024/4)))continue;
        if(best<0 || d2<best_distance){best=i;best_distance=d2;}
    }
    return best;
}

int rasterfall_session_frontier_capture(struct rasterfall_session *s,
    int facility,unsigned identity,unsigned generation)
{
    if(!s || facility<0 || facility>=3 ||
       rasterfall_session_frontier_near(s,0)!=facility)return RF_FRONTIER_CAPTURE_INVALID;
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&s->game_state);
    int result=rf_frontier_mission_capture(&s->frontier,&s->game_state,
        frontier_facility_ids[facility],identity,generation,p->actor_id,p->combat_generation);
    if(result==RF_FRONTIER_CAPTURE_APPLIED) {
        struct toy_mesh_weaver_supply supply=s->game_state.weaver.supply;
        if(facility==1)supply.cpu_on=1;
        if(facility==1)supply.storage_bytes=64ULL*1024*1024;
        if(facility==2)supply.power_on=supply.x1_on=1;
        toy_game_weaver_set_supply(&s->game_state,&supply);
        if(facility==0)s->game_state.weaver.enabled=1;
        __printf("FRONTIER captured id=%s mission=%u\n",frontier_facility_ids[facility],s->frontier.mission_id);
    }
    return result;
}

int rasterfall_session_weaver_set_supply(struct rasterfall_session *s,
    const struct toy_mesh_weaver_supply *supply)
{
    if(!s || !supply)return 0;
    if(s->world_id==RASTERFALL_WORLD_FRONTIER_STATION_01 &&
       ((supply->power_on && !s->frontier.captured[2]) ||
        (supply->cpu_on && !s->frontier.captured[1]) ||
        (supply->storage_bytes && !s->frontier.captured[1]) ||
        (supply->x1_on && !s->frontier.captured[2])))return 0;
    toy_game_weaver_set_supply(&s->game_state,supply);return 1;
}

static void session_frontier_step(struct rasterfall_session *s,int dt)
{
    if(s->world_id!=RASTERFALL_WORLD_FRONTIER_STATION_01)return;
    if(s->frontier.phase==RF_FRONTIER_SECURED || s->frontier.phase==RF_FRONTIER_FAILED)return;
    int capable=0;
    const struct toy_game_actor *p=toy_game_local_player_actor_const(&s->game_state);
    capable=p && p->active && p->state==TOY_GAME_ACTOR_ALIVE;
    for(int n=0;n<5;++n) {
        int i=s->frontier_squad_indices[n];
        if(i>=0 && i<TOY_GAME_MAX_ACTORS && s->game_state.actors[i].active &&
            s->game_state.actors[i].actor_id==s->frontier_squad_actor_ids[n] &&
            s->game_state.actors[i].combat_generation==s->frontier_squad_generations[n] &&
            s->game_state.actors[i].faction==TOY_GAME_FACTION_ALLIED &&
            s->game_state.actors[i].state==TOY_GAME_ACTOR_ALIVE)capable++;
    }
    if(!capable || s->game_state.state==TOY_GAME_OVER) {
        rf_frontier_mission_fail(&s->frontier);s->game_state.state=TOY_GAME_OVER;return;
    }
    int before=s->frontier.phase;
    rf_frontier_mission_step(&s->frontier,&s->game_state,dt);
    if(before!=s->frontier.phase)__printf("FRONTIER phase=%s alive=%d pending=%d captured=%d%d%d\n",
        rf_frontier_mission_phase_name(s->frontier.phase),s->frontier.enemies_alive,
        s->frontier.pending_spawns,s->frontier.captured[0],s->frontier.captured[1],s->frontier.captured[2]);
}
