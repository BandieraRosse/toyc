/* Game -> cheap AI ABI adapter. No action scoring or mechanical policy here. */
struct ai_tactical_queries {
    const struct rf_tac_world *world;
    int team, paths_ready[RF_TAC_MAX_SQUAD];
    signed char shot[RF_TAC_MAX_SQUAD][RF_TAC_MAX_SQUAD];
    struct tac_paths paths[RF_TAC_MAX_SQUAD];
    struct rf_tac_vec origins[RF_TAC_MAX_SQUAD];
};
static struct rf_tac_vec ai_tac_point(struct rf_ai_point p)
{struct rf_tac_vec v={p.x,p.z};return v;}
static struct rf_ai_point ai_point(struct rf_tac_vec p)
{struct rf_ai_point v={p.x,p.y};return v;}
static int ai_connection(const struct rf_tac_map *m,struct rf_tac_vec a,struct rf_tac_vec b,
    struct rf_ai_query_result *r,const struct rf_ai_query_budget *budget)
{
    if(!budget->spend(budget->context,2))return RF_AI_UNKNOWN;
    ++r->connection_tests;return tac_move_clear(m,a,b);
}
/* Charge bounded batches of distance comparisons and each geometric test. */
static int ai_nearest(const struct rf_tac_map *m,struct rf_tac_vec point,int *node,
    struct rf_ai_query_result *r,const struct rf_ai_query_budget *budget)
{
    float best=1e20f;*node=-1;
    for(int i=0;i<m->node_count;++i){
        if(i%32==0 && !budget->spend(budget->context,1))return RF_AI_UNKNOWN;
        float d=tac_distance(point,m->nodes[i].pos);
        if(d>=best)continue;
        int clear=ai_connection(m,point,m->nodes[i].pos,r,budget);
        if(clear==RF_AI_UNKNOWN)return clear;
        if(clear){*node=i;best=d;}
    }
    return *node>=0?RF_AI_YES:RF_AI_NO;
}
static int ai_route_query(struct ai_tactical_queries *c,const struct rf_ai_query *q,
    struct rf_ai_query_result *r,const struct rf_ai_query_budget *budget)
{
    const struct rf_tac_world *w=c->world;const struct rf_tac_map *m=w->map;
    struct rf_tac_vec start=q->hypothetical?ai_tac_point(q->from):w->units[c->team*RF_TAC_MAX_SQUAD+q->member].pos;
    struct rf_tac_vec goal=ai_tac_point(q->destination);
    struct rf_ai_route route={0};
    if(!tac_valid_pos(start) || !tac_valid_pos(goal))return RF_AI_NO;
    int direct=ai_connection(m,start,goal,r,budget);
    if(direct==RF_AI_UNKNOWN)return direct;
    route.points[route.count++]=ai_point(start);
    if(!direct){
        struct tac_paths *p=&c->paths[q->member];int target;
        int answer=ai_nearest(m,goal,&target,r,budget);
        if(answer!=RF_AI_YES)return answer;
        if(!c->paths_ready[q->member] || start.x!=c->origins[q->member].x || start.y!=c->origins[q->member].y){
            int source;answer=ai_nearest(m,start,&source,r,budget);
            if(answer!=RF_AI_YES)return answer;
            if(!budget->spend(budget->context,(m->node_count+31)/32))return RF_AI_UNKNOWN;
            memset(p,0,sizeof(*p));p->source=source;
            for(int i=0;i<m->node_count;++i){p->distance[i]=1e20f;p->previous[i]=-1;p->position[i]=-1;}
            p->distance[source]=tac_distance(start,m->nodes[source].pos);
            p->heap[0]=(short)source;p->position[source]=0;p->count=1;
            c->origins[q->member]=start;c->paths_ready[q->member]=1;
        }else ++r->cache_hits;
        /* Preserve the frontier if a query runs out; only this decision owns it. */
        while(p->position[target]!=-2 && p->count){
            if(!budget->spend(budget->context,1))return RF_AI_UNKNOWN;
            int a=tac_heap_pop(p);++r->expanded_nodes;
            for(int k=0;k<m->neighbor_count[a];++k){
                int b=m->neighbors[a][k];
                if(p->position[b]==-2)continue;
                float d=p->distance[a]+tac_distance(m->nodes[a].pos,m->nodes[b].pos);
                if(d>=p->distance[b])continue;
                p->distance[b]=d;p->previous[b]=(short)a;
                if(p->position[b]<0){p->position[b]=(short)p->count;p->heap[p->count++]=(short)b;}
                tac_heap_up(p,p->position[b]);
            }
        }
        if(p->position[target]!=-2)return RF_AI_NO;
        short reverse[RF_AI_MAX_ROUTE_POINTS-2];int count=0,node=target;
        while(node>=0){
            if(count>=RF_AI_MAX_ROUTE_POINTS-2)return RF_AI_UNKNOWN;
            if(!budget->spend(budget->context,1))return RF_AI_UNKNOWN;
            reverse[count++]=(short)node;node=p->previous[node];
        }
        for(int k=count-1;k>=0;--k)route.points[route.count++]=ai_point(m->nodes[reverse[k]].pos);
    }
    route.points[route.count++]=ai_point(goal);
    /* Skip an initial graph centre only after a charged movement validation. */
    int waypoint=1;
    for(int i=2;!direct && i<route.count && i<9;++i){
        int clear=ai_connection(m,start,ai_tac_point(route.points[i]),r,budget);
        if(clear==RF_AI_UNKNOWN)return clear;
        if(!clear)break;
        waypoint=i;
    }
    if(waypoint>1){
        for(int i=waypoint;i<route.count;++i)route.points[1+i-waypoint]=route.points[i];
        route.count-=waypoint-1;
    }
    route.generation=tac_generation(w,c->team);route.map_generation=m->generation;
    route.time_ms=w->time_ms;route.team=c->team;route.member=q->member;
    r->route=route;r->destination=ai_point(goal);r->waypoint=route.points[1];
    for(int i=1;i<route.count;++i)r->distance+=tac_distance(ai_tac_point(route.points[i-1]),ai_tac_point(route.points[i]));
    return RF_AI_YES;
}
static int ai_exposure_query(struct ai_tactical_queries *c,const struct rf_ai_query *q,
    struct rf_ai_query_result *r,const struct rf_ai_query_budget *budget)
{
    const struct rf_tac_world *w=c->world;const struct rf_ai_route *route=q->route;
    if(!route || route->count<2 || route->count>RF_AI_MAX_ROUTE_POINTS ||
       route->generation!=tac_generation(w,c->team) || route->map_generation!=w->map->generation ||
       route->time_ms!=w->time_ms || route->team!=c->team || route->member!=q->member ||
       q->sample_count<2 || q->sample_count>RF_AI_MAX_EXPOSURE_SAMPLES ||
       q->first_sample<0 || q->first_sample>=q->sample_count)return RF_AI_UNKNOWN;
    r->next_sample=q->first_sample;
    float arc[RF_AI_MAX_ROUTE_POINTS]={0};
    for(int i=0;i<route->count;++i){
        if(!budget->spend(budget->context,1))return RF_AI_UNKNOWN;
        if(!tac_valid_pos(ai_tac_point(route->points[i])))return RF_AI_UNKNOWN;
        if(i)arc[i]=arc[i-1]+tac_distance(ai_tac_point(route->points[i-1]),ai_tac_point(route->points[i]));
    }
    const struct rf_tac_unit *u=&w->units[c->team*RF_TAC_MAX_SQUAD+q->member];
    const struct toy_game_actor *target=toy_game_actor_by_id_const(w->game,u->actor_id);
    if(!target || target->combat_generation!=u->actor_generation)return RF_AI_UNKNOWN;
    r->distance=arc[route->count-1];r->next_sample=q->first_sample;
    int segment=1;
    for(int i=q->first_sample;i<q->sample_count;++i){
        if(!budget->spend(budget->context,1))return RF_AI_UNKNOWN;
        struct rf_ai_exposure_sample *sample=&r->samples[r->sample_count++];
        sample->distance=r->distance*i/(q->sample_count-1);
        while(segment<route->count-1 && arc[segment]<sample->distance)++segment;
        float span=arc[segment]-arc[segment-1];
        float f=span>0?(sample->distance-arc[segment-1])/span:0;
        sample->position.x=route->points[segment-1].x+(route->points[segment].x-route->points[segment-1].x)*f;
        sample->position.z=route->points[segment-1].z+(route->points[segment].z-route->points[segment-1].z)*f;
        for(int e=0;e<w->squad_size;++e){
            if(!budget->spend(budget->context,2))return RF_AI_UNKNOWN;
            const struct rf_tac_unit *enemy=&w->units[(1-c->team)*RF_TAC_MAX_SQUAD+e];
            const struct toy_game_actor *source=toy_game_actor_by_id_const(w->game,enemy->actor_id);
            if(enemy->alive && (!source || source->combat_generation!=enemy->actor_generation))return RF_AI_UNKNOWN;
            sample->tested_enemies|=1u<<e;
            if(!enemy->alive)continue;
            float distance=tac_distance(enemy->pos,ai_tac_point(sample->position));
            if(distance*512>toy_game_weapon_info(toy_game_actor_current_weapon(source))->range)continue;
            ++r->visibility_tests;
            if(toy_game_combat_visible(w->game,source,tac_x(sample->position.x),
                target->ground_y+target->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU*64/100,tac_z(sample->position.z)))
                sample->visible_enemies|=1u<<e;
        }
        ++r->samples_completed;r->next_sample=i+1;
    }
    return RF_AI_YES;
}
static int ai_tactical_query(void *opaque,const struct rf_ai_query *q,struct rf_ai_query_result *r,
    const struct rf_ai_query_budget *budget)
{
    struct ai_tactical_queries *c=opaque;const struct rf_tac_world *w=c->world;
    if(q->member<0 || q->member>=w->squad_size)return RF_AI_UNKNOWN;
    const struct rf_tac_unit *u=&w->units[c->team*RF_TAC_MAX_SQUAD+q->member];
    if(!u->alive)return RF_AI_NO;
    if(q->kind==RF_AI_CONNECTION)
        return ai_connection(w->map,ai_tac_point(q->from),ai_tac_point(q->destination),r,budget);
    if(q->kind==RF_AI_ROUTE)return ai_route_query(c,q,r,budget);
    if(q->kind==RF_AI_EXPOSURE)return ai_exposure_query(c,q,r,budget);
    if(q->kind==RF_AI_SHOT){
        if(q->enemy<0 || q->enemy>=w->squad_size)return RF_AI_UNKNOWN;
        const struct rf_tac_unit *v=&w->units[(1-c->team)*RF_TAC_MAX_SQUAD+q->enemy];
        if(!q->hypothetical && c->shot[q->member][q->enemy]>=0){++r->cache_hits;r->distance=tac_distance(u->pos,v->pos);return c->shot[q->member][q->enemy];}
        if(!budget->spend(budget->context,2))return RF_AI_UNKNOWN;
        const struct toy_game_actor *source=toy_game_actor_by_id_const(w->game,u->actor_id);
        const struct toy_game_actor *target=toy_game_actor_by_id_const(w->game,v->actor_id);
        if(!source || !target || !v->alive)return RF_AI_NO;
        if(source->combat_generation!=u->actor_generation || target->combat_generation!=v->actor_generation)return RF_AI_UNKNOWN;
        struct toy_game_actor hypothetical=*source;
        struct rf_tac_vec from=q->hypothetical?ai_tac_point(q->from):u->pos;
        if(!tac_valid_pos(from))return RF_AI_NO;
        hypothetical.x=tac_x(from.x);hypothetical.z=tac_z(from.y);
        r->distance=tac_distance(from,v->pos);
        int answer=0;
        if(r->distance*512<=toy_game_weapon_info(toy_game_actor_current_weapon(source))->range){
            ++r->visibility_tests;answer=toy_game_combat_visible(w->game,&hypothetical,target->x,
                target->ground_y+target->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU*64/100,target->z);
        }
        if(!q->hypothetical)c->shot[q->member][q->enemy]=(signed char)answer;
        return answer;
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
