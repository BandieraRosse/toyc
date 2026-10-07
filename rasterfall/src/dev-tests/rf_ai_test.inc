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
    rf_tac_command(&w,0,RF_TAC_ATTACK,map->objective,map->objective_radius);policy.budget=128;
    CHECK(rf_ai_host_decide(&h[0],&w,0,&policy,&p[0],NULL,NULL),"six-member budgeted decision");
    moving=0;for(int i=0;i<6;++i)moving+=p[0].actions[i].kind==RF_TAC_MOVE;
    CHECK(moving==6 && h[0].stats.work<=128,"default budget serves all six members");
    rf_ai_host_destroy(&h[0]);rf_tac_world_destroy(&w);
}
