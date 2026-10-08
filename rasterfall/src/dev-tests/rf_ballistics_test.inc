static void test_ballistics(void)
{
    struct toy_game *g=calloc(1,sizeof(*g));
    CHECK(g!=NULL,"ballistics Game allocation");if(!g)return;
    toy_game_init(g,921);g->external_director=1;
    struct toy_game_actor *source=toy_game_local_player_actor(g);
    int id=toy_game_add_ai(g,TOY_GAME_AI_LEVEL_2,0,10240,"TARGET");
    struct toy_game_actor *target=toy_game_actor_by_id(g,id);
    CHECK(target!=NULL,"ballistic target");if(!target){free(g);return;}
    source->x=source->z=source->ground_y=source->airborne_y=0;
    target->ground_y=target->airborne_y=0;target->faction=TOY_GAME_FACTION_HOSTILE;
    target->evasion.reserve_milli=0;target->controller_external=1;
    toy_game_actor_equip_weapon(g,source,TOY_GAME_WEAPON_AK);
    source->weapon_switch_timer_ms=0;source->pitch_cy=1024;
    source->pitch_sy=(RASTERFALL_HUMAN_HEIGHT_RFU*64/100-RASTERFALL_HUMAN_EYE_HEIGHT_RFU)/10;
    int hp=target->hp;
    toy_game_actor_fire(g,source,0,1024,0);
    CHECK(target->hp==hp && g->shot_serial==1 && g->shot_history[0].phase==TOY_GAME_SHOT_FIRED,
        "launch records one shot without future damage");
    toy_game_update_bullets(g,16);
    CHECK(target->hp==hp,"20m target cannot be hit during first rifle flight step");
    toy_game_update_bullets(g,16);
    CHECK(target->hp<hp && g->shot_serial==2 && g->shot_history[1].phase==TOY_GAME_SHOT_IMPACT,
        "arrival resolves real collision and publishes a separate impact");
    int after=target->hp;toy_game_update_bullets(g,200);
    CHECK(target->hp==after && g->shot_serial==2,"a bullet settles exactly once");
    source->fire_cooldown_ms=0;toy_game_actor_fire(g,source,0,1024,0);
    target->x=2000;toy_game_update_bullets(g,32);
    CHECK(target->hp==after,"target movement after launch can evade the shot");
    memset(g->bullets,0,sizeof(g->bullets));target->x=0;
    struct toy_map_primitive wall={0};wall.shape=TOY_MAP_PRIMITIVE_BOX;
    wall.flags=TOY_MAP_PRIMITIVE_COLLISION;wall.minx=-2000;wall.maxx=2000;
    wall.minz=6000;wall.maxz=6001;wall.surface_y0=wall.surface_y1=2000;
    g->primitives=&wall;g->primitive_count=1;
    source->fire_cooldown_ms=0;toy_game_actor_fire(g,source,0,1024,0);
    toy_game_update_bullets(g,100);
    const struct toy_game_shot_event *impact=&g->shot_history[(g->shot_serial-1)%TOY_GAME_SHOT_HISTORY];
    CHECK(target->hp==after && impact->rays[0].hit_world && impact->rays[0].ez<=6001,
        "swept flight cannot tunnel through a thin wall on a long step");
    g->primitive_count=0;source->fire_cooldown_ms=0;
    toy_game_actor_fire(g,source,0,1024,0);
    source->simulation_paused=1;struct toy_game_bullet saved=g->bullets[0];
    toy_game_update_bullets(g,32);
    CHECK(!memcmp(&saved,&g->bullets[0],sizeof(saved)),"experiment pause freezes in-flight bullets");
    source->simulation_paused=0;source->hp=0;source->state=TOY_GAME_ACTOR_DEAD;
    toy_game_update_bullets(g,32);
    CHECK(target->hp<after,"shooter death does not erase an already fired bullet");
    source->hp=120;source->state=TOY_GAME_ACTOR_ALIVE;source->fire_cooldown_ms=0;
    toy_game_actor_fire(g,source,0,1024,0);after=target->hp;++source->combat_generation;
    toy_game_update_bullets(g,32);
    CHECK(target->hp==after && !g->bullets[0].active,"source lifecycle reset retires old bullets");
    int previous=toy_game_weapon_damage_milli(TOY_GAME_WEAPON_SMG,0);
    for(int d=1;d<=200*512;d+=128){
        int damage=toy_game_weapon_damage_milli(TOY_GAME_WEAPON_SMG,d);
        CHECK(damage>0 && damage<=previous,"piecewise damage is positive and monotone");previous=damage;
    }
    int spread=toy_game_actor_current_spread(source);
    source->moving=1;source->weapon_spread_heat=100;source->evasion.animation_ms=500;
    memset(&source->skills,3,sizeof(source->skills));
    CHECK(toy_game_actor_current_spread(source)==spread,"rifle spread is fixed across motion, heat and skills");
    /* Cadence carries fixed-step overshoot; no reload hides the result. */
    memset(g->bullets,0,sizeof(g->bullets));source->slots[source->current_slot].mag=100;
    source->slots[source->current_slot].reserve=0;source->fire_cooldown_ms=0;
    source->fire_seq=0;source->reloading=0;
    for(int i=0;i<125;++i)toy_game_update_actor_weapon_held(g,source,NULL,0,1,1024,0,16,100);
    CHECK(source->fire_seq==(unsigned)((2000+toy_game_weapon_info(TOY_GAME_WEAPON_AK)->cooldown_ms-1)/
        toy_game_weapon_info(TOY_GAME_WEAPON_AK)->cooldown_ms),
        "rifle sustained cadence preserves mechanical rate over fixed steps");
    memset(g->bullets,0,sizeof(g->bullets));g->defer_actor_damage=1;
    unsigned serial=g->shot_serial;source->slots[source->current_slot].mag=100;
    for(int i=0;i<600;++i){source->fire_cooldown_ms=0;source->slots[source->current_slot].mag=100;
        toy_game_actor_fire(g,source,0,1024,0);}
    CHECK(!g->bullets[0].active && g->shot_serial==serial,
        "client prediction and replay cannot consume authoritative flight slots or publish hits");
    free(g);
}

static int normal_test_query(void *context,const struct rf_ai_query *q,struct rf_ai_query_result *r,
    const struct rf_ai_query_budget *budget)
{
    (void)context;
    if(!budget->spend(budget->context,2))return RF_AI_UNKNOWN;
    if(q->kind==RF_AI_SHOT)return RF_AI_YES;
    if(q->kind==RF_AI_EXPOSURE){
        r->sample_count=r->samples_completed=q->sample_count;
        for(int i=0;i<q->sample_count;++i){r->samples[i].position=q->route->points[0];
            r->samples[i].tested_enemies=r->samples[i].visible_enemies=3;}
        return RF_AI_YES;
    }
    return RF_AI_NO; /* This fixture has no legal relocation. */
}
static int normal_maneuver_query(void *context,const struct rf_ai_query *q,struct rf_ai_query_result *r,
    const struct rf_ai_query_budget *budget)
{
    const struct rf_ai_snapshot *s=context;
    if(!budget->spend(budget->context,2))return RF_AI_UNKNOWN;
    if(q->kind==RF_AI_SHOT)return q->from.x<9?RF_AI_NO:RF_AI_YES;
    if(q->kind==RF_AI_ROUTE){
        r->route.count=2;r->route.points[0]=s->members[q->member].position;
        r->route.points[1]=q->destination;r->waypoint=r->destination=q->destination;
        float x=q->destination.x-r->route.points[0].x,z=q->destination.z-r->route.points[0].z;
        r->distance=sqrtf(x*x+z*z);return RF_AI_YES;
    }
    if(q->kind==RF_AI_EXPOSURE){
        r->sample_count=r->samples_completed=q->sample_count;
        for(int i=0;i<q->sample_count;++i){
            float f=(float)i/(q->sample_count-1);
            r->samples[i].position.x=q->route->points[0].x+(q->route->points[1].x-q->route->points[0].x)*f;
            r->samples[i].position.z=q->route->points[0].z+(q->route->points[1].z-q->route->points[0].z)*f;
            r->samples[i].tested_enemies=1;
            r->samples[i].visible_enemies=r->samples[i].position.x<9?0:1;
        }
        return RF_AI_YES;
    }
    return RF_AI_NO;
}
static void test_normal_ai(void)
{
    struct rf_ai_snapshot s={0};struct rf_ai_plan p;struct rf_ai_stats stats;
    struct rf_ai_instance instance={0};
    s.version=RF_AI_API_VERSION;s.generation=1;s.count=2;s.enemy_count=2;
    s.command=RF_AI_DEFEND;s.objective.x=s.objective.z=10;s.radius=4;
    s.bounds_max.x=s.bounds_max.z=64;
    for(int i=0;i<2;++i){
        struct rf_ai_member *u=&s.members[i];u->alive=1;u->identity.id=i;u->identity.generation=1;
        u->position=s.objective;u->ammo=u->magazine=30;u->reserve=90;
        u->health=u->max_health=120;u->evasion=u->max_evasion=45;u->range_m=100;
        u->shot_damage=16;u->fire_interval_ms=100;u->turn_degrees=360;u->facing.x=1;
        s.enemies[i]=*u;s.enemies[i].identity.id=i+10;s.enemies[i].position.x=20+i;
        s.enemies[i].facing.x=-1;
    }
    s.enemies[1].health=8;s.enemies[1].evasion=0;
    CHECK(rf_ai_algorithm_find("easy")==rf_ai_algorithm_find("mechanical-v3"),"easy keeps the existing mechanical algorithm");
    CHECK(rf_ai_instance_init(&instance,rf_ai_algorithm_find("normal"),NULL),"normal initializes through public ABI");
    CHECK(rf_ai_decide(&instance,&s,RF_AI_DEFAULT_BUDGET,normal_test_query,NULL,&p,&stats),"normal legal decision");
    CHECK(p.actions[0].kind==RF_AI_FIRE && p.actions[0].target.id==11 &&
        p.actions[1].kind==RF_AI_FIRE && p.actions[1].target.id==10,"normal finishes vulnerable target without assigning the whole squad");
    s.members[0].ammo=s.members[1].ammo=2;
    CHECK(rf_ai_decide(&instance,&s,RF_AI_DEFAULT_BUDGET,normal_test_query,NULL,&p,&stats),"normal reload decision");
    CHECK((p.actions[0].kind==RF_AI_RELOAD)+(p.actions[1].kind==RF_AI_RELOAD)==1,
        "exposed squad staggers optional reloads while a teammate can fire");
    CHECK(stats.work<=RF_AI_DEFAULT_BUDGET && !stats.invalid_actions,"normal respects budget and action validation");
    s.members[0].ammo=0;
    CHECK(rf_ai_decide(&instance,&s,RF_AI_DEFAULT_BUDGET,normal_test_query,NULL,&p,&stats) &&
        p.actions[0].kind==RF_AI_RELOAD,"empty weapon reloads before considering a move");
    CHECK(rf_ai_decide(&instance,&s,0,normal_test_query,NULL,&p,&stats) &&
        p.actions[0].kind==RF_AI_HOLD && p.actions[1].kind==RF_AI_HOLD,"normal zero budget remains safe HOLD");
    s.count=s.enemy_count=1;s.radius=8;s.members[0].ammo=30;
    s.members[0].health=20;s.members[0].evasion=0;s.enemies[0].shot_damage=100;
    CHECK(rf_ai_decide(&instance,&s,RF_AI_DEFAULT_BUDGET,normal_maneuver_query,&s,&p,&stats) &&
        p.actions[0].kind==RF_AI_MOVE && p.actions[0].destination.x<9,
        "pressured normal actor can leave a firing lane for reachable cover");
    rf_ai_instance_destroy(&instance);
}
