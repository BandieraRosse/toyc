/* Game -> cheap AI ABI adapter. No action scoring or mechanical policy here. */
struct ai_tactical_queries {
    const struct rf_tac_world *world;
    int team, paths_ready[RF_TAC_MAX_SQUAD];
    signed char shot[RF_TAC_MAX_SQUAD][RF_TAC_MAX_SQUAD];
    struct tac_paths paths[RF_TAC_MAX_SQUAD];
};
static struct rf_tac_vec ai_tac_point(struct rf_ai_point p)
{struct rf_tac_vec v={p.x,p.z};return v;}
static struct rf_ai_point ai_point(struct rf_tac_vec p)
{struct rf_ai_point v={p.x,p.y};return v;}
static int ai_tactical_query(void *opaque,const struct rf_ai_query *q,struct rf_ai_query_result *r)
{
    struct ai_tactical_queries *c=opaque;const struct rf_tac_world *w=c->world;
    if(q->member<0 || q->member>=w->squad_size)return RF_AI_UNKNOWN;
    const struct rf_tac_unit *u=&w->units[c->team*RF_TAC_MAX_SQUAD+q->member];
    if(!u->alive)return RF_AI_NO;
    if(q->kind==RF_AI_CONNECTION)
        return tac_move_clear(w->map,ai_tac_point(q->from),ai_tac_point(q->destination));
    if(q->kind==RF_AI_SHOT){
        if(q->enemy<0 || q->enemy>=w->squad_size)return RF_AI_UNKNOWN;
        const struct rf_tac_unit *v=&w->units[(1-c->team)*RF_TAC_MAX_SQUAD+q->enemy];
        if(!q->hypothetical && c->shot[q->member][q->enemy]>=0){r->distance=tac_distance(u->pos,v->pos);return c->shot[q->member][q->enemy];}
        const struct toy_game_actor *source=toy_game_actor_by_id_const(w->game,u->actor_id);
        const struct toy_game_actor *target=toy_game_actor_by_id_const(w->game,v->actor_id);
        if(!source || !target || !v->alive)return RF_AI_NO;
        struct toy_game_actor hypothetical=*source;
        struct rf_tac_vec from=q->hypothetical?ai_tac_point(q->from):u->pos;
        if(!tac_valid_pos(from))return RF_AI_NO;
        hypothetical.x=tac_x(from.x);hypothetical.z=tac_z(from.y);
        r->distance=tac_distance(from,v->pos);
        int answer=r->distance*512<=toy_game_weapon_info(toy_game_actor_current_weapon(source))->range &&
            toy_game_combat_visible(w->game,&hypothetical,target->x,
                target->ground_y+target->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU*64/100,target->z);
        if(!q->hypothetical)c->shot[q->member][q->enemy]=(signed char)answer;
        return answer;
    }
    if(q->kind==RF_AI_ROUTE){
        struct rf_tac_vec goal=ai_tac_point(q->destination);
        if(!tac_valid_pos(goal))return RF_AI_NO;
        if(!c->paths_ready[q->member]){tac_paths_build(w->map,u->pos,&c->paths[q->member]);c->paths_ready[q->member]=1;}
        const struct tac_paths *p=&c->paths[q->member];
        int node=rf_tac_nearest_node(w->map,goal);
        if(node<0 || p->distance[node]>=1e19f)return RF_AI_NO;
        /* Snap only when the exact requested point has no safe final segment. */
        if(!tac_move_clear(w->map,w->map->nodes[node].pos,goal))goal=w->map->nodes[node].pos;
        r->destination=ai_point(goal);
        r->distance=p->distance[node]+tac_distance(w->map->nodes[node].pos,goal);
        r->waypoint=r->destination;
        if(!tac_move_clear(w->map,u->pos,goal)){
            short route[RF_TAC_MAX_PATH];int count=tac_route_nodes(w->map,u->pos,p,node,route),found=0;
            for(int k=0;k<count && k<8;++k){
                struct rf_tac_vec point=w->map->nodes[route[k]].pos;
                if(!tac_move_clear(w->map,u->pos,point))break;
                if(tac_distance(u->pos,point)>0.35f){r->waypoint=ai_point(point);found=1;}
            }
            if(!found)return RF_AI_NO;
        }
        return RF_AI_YES;
    }
    return RF_AI_UNKNOWN;
}
static void ai_tactical_member(const struct rf_tac_world *w,const struct rf_tac_unit *u,struct rf_ai_member *m)
{
    const struct toy_game_actor *a=toy_game_actor_by_id_const(w->game,u->actor_id);
    m->identity.id=u->actor_id;m->identity.generation=u->actor_generation;
    m->alive=u->alive;m->position=ai_point(u->pos);
    m->previous.kind=u->explicit_actions?u->action.kind:RF_AI_HOLD;
    m->previous.member=m->identity;m->previous.target.id=-1;
    m->previous.destination=ai_point(u->destination);
    if(u->action.kind==RF_TAC_FIRE && u->action.target>=0 && u->action.target<w->squad_size){
        const struct rf_tac_unit *v=&w->units[(1-u->team)*RF_TAC_MAX_SQUAD+u->action.target];
        m->previous.target.id=v->actor_id;m->previous.target.generation=v->actor_generation;
        m->feedback=v->alive?RF_AI_RUNNING:RF_AI_TARGET_LOST;
    }else m->feedback=u->action.kind==RF_TAC_HOLD?RF_AI_IDLE:RF_AI_RUNNING;
    if(!a || a->combat_generation!=u->actor_generation){m->alive=0;return;}
    const struct toy_game_weapon_info *weapon=toy_game_weapon_info(toy_game_actor_current_weapon(a));
    m->health=a->hp;m->evasion=a->evasion.reserve_milli/1000.0f;
    m->ammo=a->slots[a->current_slot].mag;m->reserve=a->slots[a->current_slot].reserve;
    m->magazine=weapon->mag_size;m->range_m=weapon->range/512.0f;
    m->reload_ms=a->reload_timer_ms;m->cooldown_ms=a->fire_cooldown_ms;m->aim_ms=a->combat_aim_ms;
    m->facing.x=a->sy/1024.0f;m->facing.z=a->cy/1024.0f;
    m->shots=a->combat_stats.shots;m->hits=a->combat_stats.hits;m->damage_taken=a->combat_stats.damage_taken;
}
void rf_ai_host_destroy(struct rf_ai_host *h)
{rf_ai_instance_destroy(&h->instance);memset(h,0,sizeof(*h));}
int rf_ai_host_decide(struct rf_ai_host *h,const struct rf_tac_world *w,int team,
    const struct rf_tac_policy *policy,struct rf_tac_plan *out,
    struct rf_tac_observation *legacy,struct rf_tac_decision_trace *trace)
{
    if(!h || !w || !w->game || team<0 || team>1 || policy->solver!=RF_TAC_M0 || !rf_tac_policy_validate(policy))return 0;
    double start=rf_ai_clock_seconds();struct rf_ai_snapshot *s=&h->snapshot;
    memset(s,0,sizeof(*s));s->version=RF_AI_API_VERSION;s->team=team;s->count=s->enemy_count=w->squad_size;
    s->time_ms=w->time_ms;s->generation=tac_generation(w,team);s->map_generation=w->map->generation;
    s->navigation_count=w->map->node_count;s->navigation=w->map->ai_nodes;
    s->command=w->orders[team].kind;s->objective=ai_point(w->orders[team].target);
    s->radius=w->orders[team].radius;s->completed=w->orders[team].captured;s->capture_ms=w->orders[team].clear_ms;
    s->bounds_min.x=s->bounds_min.z=0.35f;s->bounds_max.x=63.65f;s->bounds_max.z=47.65f;
    int reset=h->generation!=s->generation;
    for(int i=0;i<s->count;++i){
        ai_tactical_member(w,&w->units[team*RF_TAC_MAX_SQUAD+i],&s->members[i]);
        ai_tactical_member(w,&w->units[(1-team)*RF_TAC_MAX_SQUAD+i],&s->enemies[i]);
        struct rf_ai_member *m=&s->members[i];
        if(reset || m->previous.kind!=RF_AI_MOVE)h->blocked_ms[i]=0;
        else if(tac_distance(ai_tac_point(m->position),ai_tac_point(h->previous_positions[i]))<0.03f)
            h->blocked_ms[i]+=w->time_ms-h->last_time_ms;
        else h->blocked_ms[i]=0;
        if(h->blocked_ms[i]>=800)m->feedback=RF_AI_BLOCKED;
        if(!reset && m->previous.kind==RF_AI_HOLD && h->plan.actions[i].kind==RF_AI_MOVE &&
            tac_distance(ai_tac_point(m->position),ai_tac_point(h->plan.actions[i].destination))<0.35f)m->feedback=RF_AI_ARRIVED;
        else if(!reset && m->previous.kind==RF_AI_HOLD && h->plan.actions[i].kind!=RF_AI_HOLD && !s->completed)
            m->feedback=RF_AI_REJECTED;
        h->previous_positions[i]=m->position;
    }
    h->last_time_ms=w->time_ms;h->generation=s->generation;
    const struct rf_ai_algorithm *algorithm=policy->algorithm;
    if(!h->instance.initialized || h->instance.algorithm!=algorithm || memcmp(&policy->ai,&h->instance.config,sizeof(policy->ai)))
        if(!rf_ai_instance_init(&h->instance,algorithm,&policy->ai))return 0;
    h->snapshot_seconds=rf_ai_clock_seconds()-start;
    struct ai_tactical_queries queries;memset(&queries,0,sizeof(queries));
    memset(queries.shot,-1,sizeof(queries.shot));queries.world=w;queries.team=team;
    if(!rf_ai_decide(&h->instance,s,policy->budget,ai_tactical_query,&queries,&h->plan,&h->stats))return 0;
    memset(out,0,sizeof(*out));out->explicit_actions=1;out->version=RF_TAC_VERSION;
    out->generation=s->generation;out->team=team;out->count=s->count;out->tick=w->tick;out->time_ms=w->time_ms;
    out->evaluations=h->stats.work;out->budget_exhausted=h->stats.exhausted;
    if(legacy){memset(legacy,0,sizeof(*legacy));legacy->generation=out->generation;
        legacy->team=team;legacy->count=s->count;legacy->tick=w->tick;legacy->time_ms=w->time_ms;legacy->order=w->orders[team];}
    if(trace)memset(trace,0,sizeof(*trace));
    for(int i=0;i<s->count;++i){
        const struct rf_ai_action *a=&h->plan.actions[i];
        out->actions[i].kind=a->kind;out->actions[i].candidate=-1;out->actions[i].target=-1;
        out->destinations[i]=ai_tac_point(a->destination);
        if(a->kind==RF_AI_FIRE)for(int j=0;j<s->enemy_count;++j)
            if(a->target.id==s->enemies[j].identity.id && a->target.generation==s->enemies[j].identity.generation)out->actions[i].target=j;
        if(legacy){tac_view(&w->units[team*RF_TAC_MAX_SQUAD+i],&legacy->friendly[i]);
            tac_view(&w->units[(1-team)*RF_TAC_MAX_SQUAD+i],&legacy->enemy[i]);}
        if(trace){trace->selected[i]=-1;trace->targets[i]=out->actions[i].target;}
    }
    return 1;
}
