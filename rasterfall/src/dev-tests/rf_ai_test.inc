/* Public ABI tests use a deliberately different algorithm and no Game pointer. */
static int ai_test_resets,ai_test_destroys;
static void ai_test_reset(void *state){(void)state;++ai_test_resets;}
static void ai_test_destroy(void *state){(void)state;++ai_test_destroys;}
static void ai_test_decide(void *state,const struct rf_ai_config *c,const struct rf_ai_snapshot *s,
    const struct rf_ai_services *api,struct rf_ai_plan *p)
{
    (void)c;++*(int *)state;
    struct rf_ai_query q={0};struct rf_ai_query_result r;q.kind=RF_AI_ROUTE;q.member=0;
    CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN,"denied query stays unknown");
    CHECK(!api->spend(api->context,1),"query overhead consumes shared budget");
    CHECK(api->remaining(api->context)>=0,"query denial never overruns budget");
    p->actions[0].kind=RF_AI_MOVE;p->actions[0].destination.x=s->objective.x+0.125f;
    p->actions[0].destination.z=s->objective.z+0.375f;
    p->actions[1].kind=RF_AI_FIRE;p->actions[1].target.id=99999;
}
static void test_ai_abi(void)
{
    struct rf_ai_algorithm algorithm={"test-arbitrary",1,sizeof(int),0,NULL,ai_test_reset,ai_test_decide,ai_test_destroy};
    ai_test_resets=ai_test_destroys=0;
    struct rf_ai_instance instance={0};struct rf_ai_snapshot s={0};struct rf_ai_plan p;
    struct rf_ai_stats stats;
    s.version=RF_AI_API_VERSION;s.count=2;s.generation=1;s.bounds_max.x=s.bounds_max.z=64;
    s.objective.x=20;s.objective.z=20;
    for(int i=0;i<2;++i){s.members[i].alive=1;s.members[i].identity.id=i+1;s.members[i].identity.generation=1;}
    CHECK(rf_ai_instance_init(&instance,&algorithm,NULL),"independent compiled algorithm initializes");
    CHECK(rf_ai_decide(&instance,&s,1,NULL,NULL,&p,&stats),"independent algorithm runs");
    CHECK(p.actions[0].kind==RF_AI_MOVE && p.actions[0].destination.x==20.125f,
        "algorithm positions are not restricted to host candidate slots");
    CHECK(p.actions[1].kind==RF_AI_HOLD && stats.invalid_actions==1,"bad identity falls back per member");
    CHECK(stats.work<=1 && stats.exhausted && stats.unknown==1,"queries share total work limit");
    CHECK(rf_ai_decide(&instance,&s,0,NULL,NULL,&p,&stats) && p.count==2 &&
        p.actions[0].kind==RF_AI_HOLD && p.actions[1].kind==RF_AI_HOLD,"zero budget produces a complete plan");
    CHECK(*(int *)instance.state.bytes==1,"zero budget never invokes algorithm");
    ++s.generation;CHECK(rf_ai_decide(&instance,&s,1,NULL,NULL,&p,&stats),"command generation resets memory");
    CHECK(*(int *)instance.state.bytes==1,"algorithm state cannot leak across commands");
    rf_ai_instance_destroy(&instance);
    CHECK(ai_test_resets==2 && ai_test_destroys==2,"command reset releases owned algorithm state before reinitialization");
    const struct rf_ai_algorithm *m=rf_ai_algorithm_find("mechanical-v3");struct rf_ai_config c;
    CHECK(m!=NULL,"mechanical algorithm registered");rf_ai_config_default(m,&c);c.parameters[0]=2;
    CHECK(!rf_ai_instance_init(&instance,m,&c),"algorithm parameter schema rejects invalid tuning");
    struct rf_tac_policy loaded;
    CHECK(rf_tac_policy_load("rasterfall/config/ai/mechanical-v3.cfg",&loaded) && loaded.solver==RF_TAC_M0 &&
        loaded.algorithm==m && loaded.ai.version==m->version,"versioned per-algorithm configuration loads");
}
static void test_ai_game(struct rf_tac_map *map)
{
    struct rf_tac_world w={0};struct rf_tac_policy policy;struct rf_ai_host h[2]={0};
    struct rf_tac_plan p[2];struct rf_tac_observation obs;
    rf_tac_policy_default(&policy,RF_TAC_M0);
    CHECK(rf_tac_world_init(&w,map,1337,4,RF_TW_RIFLE,60000),"M0 Game init");if(!w.game)return;
    rf_tac_command(&w,0,RF_TAC_ATTACK,map->objective,map->objective_radius);
    rf_tac_command(&w,1,RF_TAC_DEFEND,map->objective,map->objective_radius);
    for(int t=0;t<2;++t)CHECK(rf_ai_host_decide(&h[t],&w,t,&policy,&p[t],&obs,NULL),"M0 cheap decision");
    CHECK(obs.candidate_count[0]==0 && h[0].snapshot.navigation_count>0,"cheap snapshot has shared nav but no pre-scored candidates");
    CHECK(h[0].stats.work<=policy.budget && h[0].stats.queries>0,"M0 accounts navigation and visibility queries");
    int moving=0;for(int i=0;i<4;++i)moving+=p[0].actions[i].kind==RF_TAC_MOVE;
    CHECK(moving==4,"initial attack assigns movement to every member");
    struct rf_tac_plan stale=p[0];rf_tac_command(&w,0,RF_TAC_ATTACK,map->objective,map->objective_radius);
    CHECK(!rf_tac_apply(&w,&stale),"old command plan rejected");
    /* Literal HOLD cannot reload an empty weapon, even when Game normally would. */
    CHECK(rf_ai_host_decide(&h[0],&w,0,&policy,&p[0],NULL,NULL),"fresh plan after command");
    p[0].actions[0].kind=RF_TAC_MOVE;p[0].destinations[0].x=-10;
    CHECK(rf_tac_apply(&w,&p[0]) && w.units[0].action.kind==RF_TAC_HOLD,
        "invalid modern movement falls back without aborting the entire match");
    for(int i=0;i<4;++i)p[0].actions[i].kind=RF_TAC_HOLD;
    CHECK(rf_tac_apply(&w,&p[0]),"literal HOLD accepted");
    struct toy_game_actor *a=toy_game_actor_by_id(w.game,w.units[0].actor_id);
    a->slots[a->current_slot].mag=0;rf_tac_step(&w);
    CHECK(!a->reloading && !a->combat_stats.shots,"HOLD neither fires nor auto reloads");
    CHECK(rf_ai_host_decide(&h[0],&w,0,&policy,&p[0],NULL,NULL) && p[0].actions[0].kind==RF_TAC_RELOAD,
        "M0 explicitly chooses empty-magazine reload");
    CHECK(rf_tac_apply(&w,&p[0]),"explicit reload accepted");rf_tac_step(&w);
    CHECK(a->reloading,"Game executes requested reload");
    /* Isolate objective semantics from combat: no enemies, actual Game movement. */
    for(int i=0;i<4;++i){struct toy_game_actor *enemy=toy_game_actor_by_id(w.game,w.units[RF_TAC_MAX_SQUAD+i].actor_id);enemy->active=0;}
    rf_tac_sync(&w);w.continuous_commands=1;
    int valid=1;
    for(int tick=0;tick<3500 && !w.orders[0].captured;++tick){
        if(w.time_ms%RF_TAC_THINK_MS<RF_TAC_DT_MS){
            valid&=rf_ai_host_decide(&h[0],&w,0,&policy,&p[0],NULL,NULL);
            valid&=rf_tac_apply(&w,&p[0]);
        }
        rf_tac_step(&w);
    }
    CHECK(valid,"M0 replans and validates arbitrary destinations throughout attack");
    CHECK(w.orders[0].captured && w.orders[0].kind==RF_TAC_ATTACK && !w.finished,"attack completes without rewriting command to defense");
    rf_tac_command(&w,0,RF_TAC_DEFEND,map->objective,map->objective_radius);
    for(int tick=0;tick<150;++tick)rf_tac_step(&w);
    CHECK(!w.orders[0].captured && !w.finished,"defense remains active after enemies disappear");
    for(int t=0;t<2;++t)rf_ai_host_destroy(&h[t]);
    rf_tac_world_destroy(&w);
    CHECK(rf_tac_world_init(&w,map,1337,6,RF_TW_RIFLE,60000),"six-member M0 init");
    if(!w.game)return;
    rf_tac_command(&w,0,RF_TAC_ATTACK,map->objective,map->objective_radius);
    CHECK(rf_ai_host_decide(&h[0],&w,0,&policy,&p[0],NULL,NULL),"six-member budgeted decision");
    moving=0;for(int i=0;i<6;++i)moving+=p[0].actions[i].kind==RF_TAC_MOVE;
    CHECK(moving==6 && h[0].stats.work<=policy.budget,"default budget serves all six members");
    rf_ai_host_destroy(&h[0]);rf_tac_world_destroy(&w);
}

/* A query consumer exercises the public contract without accessing Game. */
static struct rf_ai_route ai_test_route;
static struct rf_ai_query_result ai_test_exposure,ai_test_partial;
static int ai_query_test_mode,ai_query_test_cap;
static void ai_query_test_decide(void *state,const struct rf_ai_config *config,const struct rf_ai_snapshot *s,
    const struct rf_ai_services *api,struct rf_ai_plan *plan)
{
    (void)state;(void)config;(void)plan;
    struct rf_ai_query q={0};struct rf_ai_query_result r;
    if(ai_query_test_mode==1){
        q.kind=RF_AI_EXPOSURE;q.route=&ai_test_route;q.sample_count=8;
        CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && !r.sample_count,"stale route cannot be evaluated");return;
    }
    if(ai_query_test_mode==3){
        q.kind=RF_AI_EXPOSURE;q.route=&ai_test_route;q.sample_count=8;q.first_sample=3;
        CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && r.limited && !r.samples_completed &&
            api->remaining(api->context)==0,"total budget stops exposure without a false completed result");
        CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && !r.work && r.next_sample==3,
            "denial at entry preserves the requested resume position");return;
    }
    q.kind=RF_AI_ROUTE;q.destination=s->objective;q.max_work=2;
    CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && r.limited && !r.route.count && r.work<=2,
        "local work cap stops geometry before execution and publishes no incomplete route");
    int remaining=api->remaining(api->context);
    CHECK(remaining>0,"query cap preserves budget for other work");
    if(ai_query_test_mode==2){
        q.max_work=ai_query_test_cap;
        CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && r.limited && r.expanded_nodes>0,
            "bounded search stops with a reusable frontier");
    }
    q.max_work=0;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES,"complete geometric route available");
    if(r.answer!=RF_AI_YES)return;
    if(ai_query_test_mode==2)CHECK(r.cache_hits>0,"partial source tree resumes within decision");
    else ai_query_test_cap=r.work-r.expanded_nodes/2;
    ai_test_route=r.route;
    CHECK(r.route.count>=2 && r.route.points[0].x==s->members[0].position.x &&
        r.route.points[r.route.count-1].x==s->objective.x,"route contains origin and exact destination");
    CHECK(r.waypoint.x==r.route.points[1].x && r.waypoint.z==r.route.points[1].z,
        "immediate movement follows the reported polyline");
    float length=r.distance;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && !memcmp(&r.route,&ai_test_route,sizeof(ai_test_route)) &&
        r.distance==length && r.expanded_nodes==0,"same-source cache preserves full route and length");
    float sum=0;
    for(int i=1;i<ai_test_route.count;++i){
        q.kind=RF_AI_CONNECTION;q.from=ai_test_route.points[i-1];q.destination=ai_test_route.points[i];
        CHECK(api->query(api->context,&q,&r)==RF_AI_YES,"every reported segment is actually traversable");
        float x=q.from.x-q.destination.x,z=q.from.z-q.destination.z;sum+=sqrtf(x*x+z*z);
    }
    CHECK(fabsf(sum-length)<0.001f,"distance measures the returned route including connectors");
    q.kind=RF_AI_EXPOSURE;q.route=&ai_test_route;q.sample_count=8;
    CHECK(api->query(api->context,&q,&ai_test_exposure)==RF_AI_YES && ai_test_exposure.samples_completed==8 &&
        ai_test_exposure.next_sample==8,"coarse exposure completes requested samples");
    CHECK(ai_test_exposure.samples[0].distance==0 && ai_test_exposure.samples[7].distance==length,
        "exposure includes both route ends");
    q.max_work=1+ai_test_route.count+1+2*s->enemy_count+1+2;
    CHECK(api->query(api->context,&q,&ai_test_partial)==RF_AI_UNKNOWN && ai_test_partial.limited &&
        ai_test_partial.samples_completed==1 && ai_test_partial.next_sample==1 && ai_test_partial.sample_count==2,
        "partial exposure identifies completed prefix and unfinished sample");
    CHECK(ai_test_partial.samples[1].tested_enemies==1,
        "untested enemies are distinguishable from occluded enemies");
    q.first_sample=ai_test_partial.next_sample;q.max_work=0;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && r.samples_completed==7 && r.next_sample==8,
        "exposure resumes at first unfinished sample");
    CHECK(!memcmp(r.samples,&ai_test_exposure.samples[1],7*sizeof(r.samples[0])),
        "resumed exposure agrees with uninterrupted sampling");
    q.first_sample=0;q.sample_count=32;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && r.samples_completed==32,"medium exposure fidelity");
    q.sample_count=RF_AI_MAX_EXPOSURE_SAMPLES;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && r.samples_completed==q.sample_count,"fine exposure fidelity");
    q.sample_count++;
    CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN && !r.sample_count,"oversized exposure rejected");
    q.sample_count=8;unsigned generation=ai_test_route.map_generation;ai_test_route.map_generation++;
    CHECK(api->query(api->context,&q,&r)==RF_AI_UNKNOWN,"map generation invalidates route values");
    ai_test_route.map_generation=generation;
    q.kind=RF_AI_ROUTE;q.hypothetical=1;q.from=s->objective;q.destination=s->members[0].position;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && r.route.points[0].x==s->objective.x,
        "waypoint chaining can route from a hypothetical origin");
    q.hypothetical=0;q.destination=s->objective;
    CHECK(api->query(api->context,&q,&r)==RF_AI_YES && !memcmp(&r.route,&ai_test_route,sizeof(ai_test_route)),
        "changing source invalidates tree without contaminating the actual-origin route");
}
static void test_ai_queries(struct rf_tac_map *map)
{
    struct rf_tac_world w={0};struct rf_ai_host h={0};struct rf_tac_policy policy;struct rf_tac_plan plan;
    const struct rf_ai_algorithm algorithm={"query-contract",1,0,0,NULL,NULL,ai_query_test_decide,NULL};
    CHECK(rf_tac_world_init(&w,map,42,4,RF_TW_RIFLE,60000),"query world initializes");if(!w.game)return;
    for(int e=0;e<w.squad_size;++e){
        struct toy_game_actor *enemy=toy_game_actor_by_id(w.game,w.units[RF_TAC_MAX_SQUAD+e].actor_id);
        toy_game_actor_equip_weapon(w.game,enemy,TOY_GAME_WEAPON_SMG);
    }
    rf_tac_sync(&w);
    rf_tac_command(&w,0,RF_TAC_ATTACK,map->objective,map->objective_radius);
    rf_tac_policy_default(&policy,RF_TAC_M0);policy.algorithm=&algorithm;rf_ai_config_default(&algorithm,&policy.ai);
    policy.budget=100000;ai_query_test_mode=0;
    unsigned hash=rf_tac_hash(&w);unsigned long long rng=w.game->rng;
    CHECK(rf_ai_host_decide(&h,&w,0,&policy,&plan,NULL,NULL),"public queries run through Game adapter");
    CHECK(hash==rf_tac_hash(&w) && rng==w.game->rng,"geometry and exposure queries never mutate Game or RNG");
    CHECK(h.stats.query_work==h.stats.work && h.stats.limited_queries==2 && !h.stats.exhausted &&
        h.stats.expanded_nodes>0 && h.stats.visibility_tests>0,"typed work counters distinguish local limits from total exhaustion");
    struct rf_ai_query_result first=ai_test_exposure;
    struct rf_ai_stats stats=h.stats;
    CHECK(rf_ai_host_decide(&h,&w,0,&policy,&plan,NULL,NULL) && h.stats.work==stats.work &&
        h.stats.expanded_nodes==stats.expanded_nodes && !memcmp(&first,&ai_test_exposure,sizeof(first)),
        "fresh decision owns fresh cache and produces deterministic work and exposure");
    /* Verify incoming direction using actual enemy weapons and official rays. */
    const struct toy_game_actor *target=toy_game_actor_by_id_const(w.game,w.units[0].actor_id);
    for(int i=0;i<first.sample_count;++i){
        const struct rf_ai_exposure_sample *sample=&first.samples[i];unsigned visible=0;
        for(int e=0;e<w.squad_size;++e){
            const struct rf_tac_unit *u=&w.units[RF_TAC_MAX_SQUAD+e];
            const struct toy_game_actor *source=toy_game_actor_by_id_const(w.game,u->actor_id);
            float dx=u->pos.x-sample->position.x,dz=u->pos.y-sample->position.z;
            if(sqrtf(dx*dx+dz*dz)*512<=toy_game_weapon_info(toy_game_actor_current_weapon(source))->range &&
               toy_game_combat_visible(w.game,source,(int)lroundf((sample->position.x-32)*512),
                target->ground_y+target->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU*64/100,
                (int)lroundf((sample->position.z-24)*512)))visible|=1u<<e;
        }
        CHECK(sample->tested_enemies==15 && sample->visible_enemies==visible,"exposure agrees with enemy-to-route official visibility");
    }
    ai_query_test_mode=2;
    CHECK(rf_ai_host_decide(&h,&w,0,&policy,&plan,NULL,NULL),"interrupted route search resumes");
    ai_query_test_mode=3;policy.budget=5;
    CHECK(rf_ai_host_decide(&h,&w,0,&policy,&plan,NULL,NULL) && h.stats.work<=5 && h.stats.exhausted,
        "host reports total budget exhaustion independently of per-query caps");
    policy.budget=100000;
    ai_query_test_mode=1;++w.time_ms;
    CHECK(rf_ai_host_decide(&h,&w,0,&policy,&plan,NULL,NULL),"next snapshot rejects retained route");
    rf_ai_host_destroy(&h);rf_tac_world_destroy(&w);
}
