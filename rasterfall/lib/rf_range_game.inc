/* Range is a scenario and event recorder, never a second weapon simulator. */
static struct toy_game_actor *range_actor(struct rf_range_lab *r,int id,unsigned generation)
{
    struct toy_game_actor *a=toy_game_actor_by_id(r->game,id);
    return a && a->combat_generation==generation?a:NULL;
}
static void range_target_reset(struct toy_game_actor *a)
{
    a->state=TOY_GAME_ACTOR_ALIVE;
    toy_game_actor_set_combat_template(a,TOY_GAME_COMBAT_STANDARD);
    a->fire_enabled=0;a->controller_external=1;a->intent_target_id=-1;
    a->current_slot=0;a->slots[0].weapon=-1;
    toy_game_actor_set_animation(a,TOY_GAME_ANIM_IDLE);
}
static int range_spawn(struct rf_range_lab *r)
{
    struct toy_game *g=r->game;
    r->base_primitives=g->primitives;r->base_primitive_count=g->primitive_count;
    if(g->primitive_count+RF_RANGE_LANES*2>TOY_GAME_MAX_PRIMITIVES)return 0;
    memcpy(r->primitives,g->primitives,g->primitive_count*sizeof(*g->primitives));
    r->primitive_count=g->primitive_count;
    for(int s=0;s<2;++s) {
        int x=(r->lane-4)*1536+(s?0:640);
        struct toy_game_actor *a=s && !r->owns_game?toy_game_local_player_actor(g):NULL;
        if(!a) a=toy_game_actor_by_id(g,toy_game_add_ai(g,TOY_GAME_AI_LEVEL_2,x,0,s?"PLAYER TEST":"RANGE AI"));
        if(!a)return 0;
        r->shooter_ids[s]=a->actor_id;r->shooter_generations[s]=a->combat_generation;
        if(!s || r->owns_game) {
            a->controller_external=1;a->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
            a->x=x;a->z=0;a->sy=0;a->cy=1024;a->pitch_cy=1024;
        }
        toy_game_actor_set_combat_template(a,TOY_GAME_COMBAT_PLAYER);
        toy_game_actor_equip_weapon(g,a,r->weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG);
        a->slots[a->current_slot].reserve=TOY_GAME_AMMO_INFINITE;
        for(int l=0;l<RF_RANGE_LANES;++l) {
            int tx=(l-4)*1536+(s?0:640),tz=rf_range_distances[l]*512;
            int id=toy_game_add_ai(g,TOY_GAME_AI_LEVEL_2,tx,tz,"RANGE TARGET");
            struct toy_game_actor *target=toy_game_actor_by_id(g,id);
            if(!target)return 0;
            target->faction=TOY_GAME_FACTION_HOSTILE;
            target->character_id=RASTERFALL_CHARACTER_GUNNER;target->cy=-1024;
            range_target_reset(target);
            r->target_ids[s][l]=id;r->target_generations[s][l]=target->combat_generation;
            r->trial_owner[s][l]=-1;
            if(r->target) {
                struct toy_map_primitive *p=&r->primitives[r->primitive_count++];
                memset(p,0,sizeof(*p));p->shape=TOY_MAP_PRIMITIVE_BOX;
                p->minx=tx-240;p->maxx=tx+240;p->minz=tz-300;p->maxz=tz-240;
                p->surface_y0=p->surface_y1=RASTERFALL_HUMAN_HEIGHT_RFU*(r->target==2?84:50)/100;
                p->flags=TOY_MAP_PRIMITIVE_COLLISION;strcpy(p->role,"range_cover");
            }
        }
    }
    g->external_director=1;g->to_spawn=0;
    g->rng=r->rng[0]?r->rng[0]:1;
    toy_game_set_primitives(g,r->primitives,r->primitive_count,65000);
    r->event_cursor=g->shot_serial;
    return 1;
}
void rf_range_destroy(struct rf_range_lab *r)
{
    if(!r || !r->game)return;
    if(r->owns_game)free(r->game);
    else {
        for(int s=0;s<2;++s) {
            struct toy_game_actor *a=range_actor(r,r->shooter_ids[s],r->shooter_generations[s]);
            if(a && s==0)memset(a,0,sizeof(*a));
            for(int l=0;l<RF_RANGE_LANES;++l) {
                a=range_actor(r,r->target_ids[s][l],r->target_generations[s][l]);
                if(a)memset(a,0,sizeof(*a));
            }
        }
        toy_game_set_primitives(r->game,r->base_primitives,r->base_primitive_count,65000);
    }
    r->game=NULL;r->owns_game=0;
}
void rf_range_reset(struct rf_range_lab *r,int weapon,int mode,int lane,int target,unsigned seed)
{
    struct toy_game *bound=r->game && !r->owns_game?r->game:NULL;
    rf_range_destroy(r);memset(r,0,sizeof(*r));
    r->weapon=weapon;r->mode=mode;r->lane=lane;r->target=target;r->rng[0]=seed;
    if(bound) {r->game=bound;range_spawn(r);return;}
    r->game=calloc(1,sizeof(*r->game));if(!r->game)return;
    r->owns_game=1;toy_game_init(r->game,seed?seed:1);
    memset(r->game->actors,0,sizeof(r->game->actors));r->game->base_actor_index=-1;
    /* Owned runs use the same floor/body collision and distances as hosted runs. */
    r->primitives[0].shape=TOY_MAP_PRIMITIVE_FLAT;
    r->primitives[0].minx=-8192;r->primitives[0].maxx=8192;
    r->primitives[0].minz=-4096;r->primitives[0].maxz=53248;
    r->primitives[0].flags=TOY_MAP_PRIMITIVE_WALKABLE;
    r->game->primitives=r->primitives;r->game->primitive_count=1;
    range_spawn(r);
}
int rf_range_bind(struct rf_range_lab *r,struct toy_game *game)
{
    if(!r || !game)return 0;
    rf_range_destroy(r);r->game=game;r->owns_game=0;
    return range_spawn(r);
}
void rf_range_prepare(struct rf_range_lab *r,int dt)
{
    if(!r || !r->game)return;
    struct toy_game_actor *a=range_actor(r,r->shooter_ids[0],r->shooter_generations[0]);
    if(a) {
        a->simulation_paused=!r->running;
        if(r->running)r->wait_ms=r->wait_ms>dt?r->wait_ms-dt:0;
        toy_game_actor_set_intent(a,0,0,0,0,r->target_ids[0][r->lane],
            r->target_generations[0][r->lane],r->running && !r->wait_ms,0);
        a->intent_aim_height_percent=r->target==2?92:64;
    }
    for(int l=0;l<RF_RANGE_LANES;++l) {
        a=range_actor(r,r->target_ids[0][l],r->target_generations[0][l]);
        if(a)a->simulation_paused=!r->running;
    }
}
void rf_range_collect(struct rf_range_lab *r,int dt)
{
    if(!r || !r->game)return;
    struct toy_game *g=r->game;
    for(int s=0;s<2;++s) for(int l=0;l<RF_RANGE_LANES;++l) {
        if((s || r->running) && r->trial_owner[s][l]!=-1)r->trial_elapsed[s][l]+=dt;
        if((s || r->running) && r->stats[s][l].shots)r->stats[s][l].elapsed_ms+=dt;
    }
    if(g->shot_serial-r->event_cursor>TOY_GAME_SHOT_HISTORY) {
        r->missed_events+=(int)(g->shot_serial-r->event_cursor-TOY_GAME_SHOT_HISTORY);
        r->event_cursor=g->shot_serial-TOY_GAME_SHOT_HISTORY;
    }
    while(r->event_cursor!=g->shot_serial) {
        const struct toy_game_shot_event *e=&g->shot_history[r->event_cursor++%TOY_GAME_SHOT_HISTORY];
        int shooter=-1;
        for(int s=0;s<2;++s)if(e->source_id==r->shooter_ids[s] && e->source_generation==r->shooter_generations[s])shooter=s;
        if(shooter<0)continue;
        int lane=r->lane,target_side=shooter;float best=1e30f;
        const struct toy_game_ray *ray=&e->rays[0];
        /* Associate misses by the actual shot direction, not camera selection. */
        for(int l=0;l<RF_RANGE_LANES;++l) {
            float dx=(l-4)*1536+(shooter?0:640)-e->x,dz=rf_range_distances[l]*512-e->z;
            float cross=dx*ray->cy-dz*ray->sy,along=dx*ray->sy+dz*ray->cy;
            float score=along>0?cross*cross/(dx*dx+dz*dz+1):1e30f;
            if(score<best){best=score;lane=l;}
        }
        int hit=0,heads=0,damage=0,absorbed=0,killed=0;
        for(int p=0;p<e->ray_count;++p) {
            const struct toy_game_ray *v=&e->rays[p];
            if(v->actor_index<0 || v->actor_index>=TOY_GAME_MAX_ACTORS)continue;
            const struct toy_game_actor *target=&g->actors[v->actor_index];
            for(int s=0;s<2;++s)for(int l=0;l<RF_RANGE_LANES;++l)
                if(target->actor_id==r->target_ids[s][l] && v->actor_generation==r->target_generations[s][l]) {
                    lane=l;target_side=s;hit=1;heads+=v->weakpoint;
                    damage+=v->health_damage;absorbed+=v->evasion_spent_milli;
                    killed|=(e->killed_rays & (1u<<p))!=0;
                }
        }
        struct rf_range_stats *stats=&r->stats[shooter][lane];
        stats->shots++;stats->hits+=hit;stats->heads+=heads;stats->damage+=damage;stats->absorbed+=absorbed/1000.0f;
        float dx=(lane-4)*1536+(target_side?0:640)-e->x,dz=rf_range_distances[lane]*512-e->z;
        float distance=sqrtf(dx*dx+dz*dz)/512;
        stats->distance_total+=distance;
        if(stats->shots==1 || distance<stats->distance_min)stats->distance_min=distance;
        if(distance>stats->distance_max)stats->distance_max=distance;
        int *owner=&r->trial_owner[target_side][lane];
        if(*owner==-1)*owner=shooter;else if(*owner!=shooter)*owner=2;
        struct toy_game_actor *target=range_actor(r,r->target_ids[target_side][lane],r->target_generations[target_side][lane]);
        if(target && killed) {
            /* Mixed-shooter kills are not independent shooter TTK samples. */
            if(*owner==shooter) {
                stats->kills++;stats->last_ttk_ms=r->trial_elapsed[target_side][lane];
                stats->ttk_total_ms+=stats->last_ttk_ms;
            }
        }
        if(!shooter) {
            ++r->pattern_shots;
            r->wait_ms=r->mode==RF_TW_SINGLE?450:r->mode==RF_TW_BURST && r->pattern_shots%3==0?450:0;
        }
        struct rf_range_mark *mark=&r->marks[r->mark_count++%RF_RANGE_MARKS];
        mark->x=(ray->ex-((lane-4)*1536+(target_side?0:640)))/512.0f;
        mark->y=(ray->hit_y-RASTERFALL_HUMAN_HEIGHT_RFU/2)/512.0f;
        mark->lane=lane;mark->shooter=shooter;mark->hit=hit;
    }
    for(int s=0;s<2;++s) {
        struct toy_game_actor *a=range_actor(r,r->shooter_ids[s],r->shooter_generations[s]);
        if(a) {r->weapons[s].ammo=a->slots[a->current_slot].mag;r->weapons[s].cooldown_ms=a->fire_cooldown_ms;
            r->weapons[s].reload_remaining_ms=a->reload_timer_ms;}
        for(int l=0;l<RF_RANGE_LANES;++l) {
            a=range_actor(r,r->target_ids[s][l],r->target_generations[s][l]);
            if(a && a->hp<=0) {
                range_target_reset(a);r->trial_elapsed[s][l]=0;r->trial_owner[s][l]=-1;
            }
            if(a){r->stats[s][l].hp=a->hp;r->stats[s][l].risk=a->evasion.reserve_milli/1000.0f;}
            r->stats[s][l].trial_ms=r->trial_elapsed[s][l];
        }
    }
}
void rf_range_step(struct rf_range_lab *r,int dt)
{
    if(!r || !r->game)return;
    rf_range_prepare(r,dt);r->game->event_count=0;
    toy_game_update_world(r->game,dt);rf_range_collect(r,dt);
}
int rf_range_fire_distance(struct rf_range_lab *r,int s,int lane,float ax,float ay,float distance)
{
    if(!r || !r->game || s<0 || s>1 || lane<0 || lane>=RF_RANGE_LANES || !isfinite(distance) || distance<=0)return 0;
    struct toy_game_actor *a=range_actor(r,r->shooter_ids[s],r->shooter_generations[s]);
    struct toy_game_actor *target=range_actor(r,r->target_ids[s][lane],r->target_generations[s][lane]);
    if(!a || !target)return 0;
    a->x=target->x;a->z=target->z-(int)(distance*512);a->sy=(int)(ax/distance*1024);a->cy=1024;
    a->pitch_sy=(int)((target->ground_y+RASTERFALL_HUMAN_HEIGHT_RFU/2+ay*512-
        a->ground_y-RASTERFALL_HUMAN_EYE_HEIGHT_RFU)/distance*2);a->pitch_cy=1024;
    unsigned before=a->fire_seq;toy_game_actor_fire(r->game,a,a->sy,a->cy,100);
    rf_range_collect(r,0);return a->fire_seq!=before;
}
int rf_range_fire(struct rf_range_lab *r,int s,int lane,float ax,float ay)
{return rf_range_fire_distance(r,s,lane,ax,ay,lane>=0 && lane<RF_RANGE_LANES?rf_range_distances[lane]:0);}
