static void effect_bullet_impact(struct rasterfall_effects *fx,const struct toy_game_shot_event *shot)
{
    const struct toy_game_ray *ray=&shot->rays[0];
    struct rasterfall_effect_event e={0};
    int dx,dy,dz,length,tail;
    if(shot->phase!=TOY_GAME_SHOT_IMPACT)return;
    dx=ray->ex-shot->x;dy=ray->hit_y-shot->y;dz=ray->ez-shot->z;
    length=(int)sqrt((double)dx*dx+(double)dy*dy+(double)dz*dz);tail=length<2048?length:2048;
    e.type=RASTERFALL_EFFECT_EVENT_TRACER;e.flags=RASTERFALL_EFFECT_EVENT_DEPTH_TEST;
    e.weapon=shot->weapon;e.source_id=shot->source_id;e.sequence=shot->fire_sequence;e.life_ms=48;
    e.ex=ray->ex;e.ey=RASTERFALL_WORLD_GROUND_Y+ray->hit_y;e.ez=ray->ez;
    e.sx=e.ex-(length?(int)((long long)dx*tail/length):0);
    e.sy=e.ey-(length?(int)((long long)dy*tail/length):0);
    e.sz=e.ez-(length?(int)((long long)dz*tail/length):0);
    rasterfall_effects_consume(fx,&e);
    e.x=e.ex;e.y=e.ey;e.z=e.ez;e.target_id=ray->actor_index>=0?-1:ray->enemy_index;
    e.dir_sy=ray->sy;e.dir_cy=ray->cy;e.damage=ray->health_damage;
    if(ray->hit_world || (ray->hit_enemy && ray->health_damage>0)){
        e.type=ray->hit_world?RASTERFALL_EFFECT_EVENT_BULLET_IMPACT:RASTERFALL_EFFECT_EVENT_ENTITY_HIT;
        rasterfall_effects_consume(fx,&e);
    }
}

/* Remote trajectories are derived visuals, never client Game damage state. */
void rasterfall_effects_remote_shot(struct rasterfall_effects *fx,const struct toy_game_shot_event *shot)
{
    if(!fx || !shot || !toy_game_weapon_ballistics(shot->weapon))return;
    if(shot->phase==TOY_GAME_SHOT_IMPACT){
        for(int i=0;i<64;++i){
            struct rasterfall_effect_flight *b=&fx->remote_flights[i];
            if(b->source==(unsigned)shot->source_id && b->generation==shot->source_generation &&
               b->sequence==shot->fire_sequence)b->active=0;
        }
        effect_bullet_impact(fx,shot);return;
    }
    if(shot->phase!=TOY_GAME_SHOT_FIRED)return;
    struct rasterfall_effect_flight *b=&fx->remote_flights[fx->remote_flight_next++%64];
    memset(b,0,sizeof(*b));b->active=1;b->source=shot->source_id;b->generation=shot->source_generation;
    b->sequence=shot->fire_sequence;b->weapon=shot->weapon;
    b->x=shot->x;b->y=shot->y;b->z=shot->z;
    b->dx=shot->rays[0].sy;b->dy=shot->rays[0].vy;b->dz=shot->rays[0].cy;
}
static void update_remote_flights(struct rasterfall_effects *fx,int dt_ms)
{
    if(dt_ms<=0)return;
    for(int i=0;i<64;++i){
        struct rasterfall_effect_flight *b=&fx->remote_flights[i];
        struct rasterfall_effect_event e={0};
        if(!b->active)continue;
        b->age_ms+=dt_ms;
        int distance=toy_game_weapon_ballistics(b->weapon)->speed_mps*512*b->age_ms/1000;
        if(distance>toy_game_weapon_info(b->weapon)->range || b->age_ms>700){b->active=0;continue;}
        int tail=distance<2048?distance:2048;
        b->distance=distance;e.type=RASTERFALL_EFFECT_EVENT_TRACER;e.flags=RASTERFALL_EFFECT_EVENT_DEPTH_TEST;
        e.weapon=b->weapon;e.source_id=b->source;e.sequence=b->sequence;e.life_ms=48;
        e.ex=b->x+(int)((long long)b->dx*distance/1024);
        e.ey=RASTERFALL_WORLD_GROUND_Y+b->y+(int)((long long)b->dy*distance/1024);
        e.ez=b->z+(int)((long long)b->dz*distance/1024);
        e.sx=e.ex-b->dx*tail/1024;e.sy=e.ey-b->dy*tail/1024;e.sz=e.ez-b->dz*tail/1024;
        rasterfall_effects_consume(fx,&e);
    }
}

/* Camera-independent short flight segments, read from authoritative Game.
 * Emission only once per rule step; neither FPS nor RTS selects another style. */
void rasterfall_effects_sync_bullets(struct rasterfall_effects *fx,const struct toy_game *g)
{
    int i;
    if(!fx || !g)return;
    if(g->shot_serial<fx->bullet_event_cursor){fx->bullet_event_cursor=0;fx->bullet_time_ms=-1;}
    if(g->shot_serial-fx->bullet_event_cursor>TOY_GAME_SHOT_HISTORY)
        fx->bullet_event_cursor=g->shot_serial-TOY_GAME_SHOT_HISTORY;
    if(fx->bullet_time_ms!=g->combat_time_ms){
        fx->bullet_time_ms=g->combat_time_ms;
        for(i=0;i<TOY_GAME_MAX_BULLETS;i++){
            const struct toy_game_bullet *b=&g->bullets[i];
            struct rasterfall_effect_event e={0};int tail;
            if(!b->active || !b->distance)continue;
            tail=b->distance<2048?b->distance:2048;
            e.type=RASTERFALL_EFFECT_EVENT_TRACER;e.flags=RASTERFALL_EFFECT_EVENT_DEPTH_TEST;
            e.weapon=b->weapon;e.source_id=b->source_id;e.sequence=b->fire_sequence;e.life_ms=48;
            e.ex=b->x;e.ey=RASTERFALL_WORLD_GROUND_Y+b->y;e.ez=b->z;
            e.sx=e.ex-b->dx*tail/1024;e.sy=e.ey-b->dy*tail/1024;e.sz=e.ez-b->dz*tail/1024;
            rasterfall_effects_consume(fx,&e);
        }
    }
    while(fx->bullet_event_cursor!=g->shot_serial){
        const struct toy_game_shot_event *shot=&g->shot_history[fx->bullet_event_cursor++%TOY_GAME_SHOT_HISTORY];
        effect_bullet_impact(fx,shot);
    }
}
