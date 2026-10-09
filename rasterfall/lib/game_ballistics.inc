/* Rifle/SMG authoritative flight. Units: RFU, milliseconds, direction /1024.
 * Other firearms retain their existing instantaneous settlement. */
const struct toy_game_ballistics *toy_game_weapon_ballistics(int weapon)
{
    static const struct toy_game_ballistics rifle = {720, {30,45,65,90}, {1000,750,450,250}};
    static const struct toy_game_ballistics smg = {400, {15,25,40,60}, {1000,750,400,200}};
    return weapon == TOY_GAME_WEAPON_AK ? &rifle : weapon == TOY_GAME_WEAPON_SMG ? &smg : NULL;
}

int toy_game_weapon_damage_milli(int weapon, int distance_rfu)
{
    const struct toy_game_ballistics *p = toy_game_weapon_ballistics(weapon);
    int i, multiplier = 1000;
    if (p) {
        multiplier = p->damage_permille[3];
        if (distance_rfu <= p->distance_m[0]*512) multiplier = p->damage_permille[0];
        else for (i=1;i<4;i++) if (distance_rfu <= p->distance_m[i]*512) {
            multiplier = p->damage_permille[i-1] + (int)((long long)
                (p->damage_permille[i]-p->damage_permille[i-1]) *
                (distance_rfu-p->distance_m[i-1]*512) /
                ((p->distance_m[i]-p->distance_m[i-1])*512));
            break;
        }
    }
    return toy_game_weapon_info(weapon)->damage * multiplier;
}

static struct toy_game_shot_event *combat_shot_event(struct toy_game *g)
{
    struct toy_game_shot_event *e=&g->shot_history[g->shot_serial%TOY_GAME_SHOT_HISTORY];
    memset(e,0,sizeof(*e));e->serial=++g->shot_serial;e->time_ms=g->combat_time_ms;
    return e;
}

static int combat_bullet_free(const struct toy_game *g)
{
    int i;
    for(i=0;i<TOY_GAME_MAX_BULLETS;i++)if(!g->bullets[i].active)return i;
    return -1;
}

/* Called after begin_fire, with the sampled direction only. No future target
 * or collision is reserved at launch; lifetime belongs to the Game copy. */
int toy_game_actor_launch_bullet(struct toy_game *g, struct toy_game_actor *a)
{
    int index=combat_bullet_free(g), weapon=toy_game_actor_current_weapon(a), length;
    struct toy_game_bullet *b;
    struct toy_game_ray *r=&a->rays[0];
    struct toy_game_shot_event *e;
    struct toy_game_capabilities caps;
    if(index<0 || !toy_game_weapon_ballistics(weapon))return 0;
    b=&g->bullets[index];memset(b,0,sizeof(*b));b->active=1;
    b->source_id=a->actor_id;b->source_generation=a->combat_generation;
    b->fire_sequence=a->fire_seq;b->faction=a->faction;b->weapon=weapon;
    b->sx=b->x=a->x;b->sz=b->z=a->z;
    b->sy=b->y=a->ground_y+a->airborne_y+RASTERFALL_HUMAN_EYE_HEIGHT_RFU;
    b->dx=r->sy;b->dz=r->cy;
    b->dy=r->vy+(a->pitch_cy>0?(int)((long long)a->pitch_sy*1024/a->pitch_cy):0);
    length=isqrt((long long)b->dx*b->dx+(long long)b->dy*b->dy+(long long)b->dz*b->dz);
    if(length<1)length=1;
    b->dx=(int)((long long)b->dx*1024/length);
    b->dy=(int)((long long)b->dy*1024/length);
    b->dz=(int)((long long)b->dz*1024/length);
    toy_game_actor_capabilities(a,weapon,&caps);b->weakpoint_percent=caps.weakpoint_percent;
    r->actor_index=r->enemy_index=-1;r->damage=r->health_damage=r->evasion_spent_milli=0;
    r->hit_enemy=r->hit_world=0;r->ex=b->x;r->ez=b->z;r->hit_y=b->y;
    /* Clients predict ammo/muzzle only. Host results drive visual flight,
     * so replay cannot create duplicate bullets or consume a finite pool. */
    if(g->defer_actor_damage){b->active=0;return 1;}
    e=combat_shot_event(g);e->phase=TOY_GAME_SHOT_FIRED;
    e->source_id=b->source_id;e->source_generation=b->source_generation;
    e->fire_sequence=b->fire_sequence;e->weapon=weapon;
    e->x=b->sx;e->y=b->sy;e->z=b->sz;e->ray_count=1;e->rays[0]=*r;
    e->rays[0].sy=b->dx;e->rays[0].cy=b->dz;e->rays[0].vy=b->dy;
    return 1;
}

/* Swept segment against a finite vertical body box, including its near face.
 * Long steps cannot tunnel through walls or bodies. Targets use current fixed
 * step positions; no future movement or aim-leading enters this query. */
static int combat_bullet_body(const struct toy_game_bullet *b,int x,int y,int z,
    int height,int radius,int limit)
{
    long long lo=0,hi=(long long)limit*(1<<20);
    if(!combat_ray_slab(b->x,b->dx,x-radius,x+radius,&lo,&hi) ||
       !combat_ray_slab(b->z,b->dz,z-radius,z+radius,&lo,&hi) ||
       !combat_ray_slab(b->y,b->dy,y,y+height,&lo,&hi))return -1;
    return (int)(lo/(1<<20));
}

void toy_game_update_bullets(struct toy_game *g,int dt_ms)
{
    int i,j;
    if(!g || dt_ms<=0 || g->state!=TOY_GAME_PLAYING)return;
    for(i=0;i<TOY_GAME_MAX_BULLETS;i++) {
        struct toy_game_bullet *b=&g->bullets[i];
        struct toy_game_actor *source;
        struct toy_game_ray ray={0};
        const struct toy_game_weapon_info *w;
        int travel,best,actor_hit=-1,enemy_hit=-1,world_hit,expired,damage_milli;
        long long advance;
        if(!b->active)continue;
        source=toy_game_actor_by_id(g,b->source_id);
        /* Reset/unload invalidates a generation. Death alone does not erase
         * already fired bullets, and a pause freezes that source's flight. */
        if(!source || source->combat_generation!=b->source_generation){b->active=0;continue;}
        if(source->simulation_paused)continue;
        w=toy_game_weapon_info(b->weapon);
        advance=(long long)toy_game_weapon_ballistics(b->weapon)->speed_mps*512*dt_ms+b->remainder;
        travel=(int)(advance/1000);b->remainder=(int)(advance%1000);
        if(travel>w->range-b->distance)travel=w->range-b->distance;
        best=combat_world_distance(g,b->x,b->y,b->z,b->dx,b->dy,b->dz,travel+1);
        world_hit=best<=travel;if(!world_hit)best=travel;
        for(j=0;j<TOY_GAME_MAX_ACTORS+TOY_GAME_MAX_ENEMIES;j++) {
            int x,y,z,height,t,radius=TOY_GAME_HIT_RADIUS,core=0;
            if(j<TOY_GAME_MAX_ACTORS) {
                const struct toy_game_actor *a=&g->actors[j];
                if(!a->active || a->hp<=0 || a->state!=TOY_GAME_ACTOR_ALIVE ||
                    a->simulation_paused || a->developer_only || a->faction==b->faction)continue;
                x=a->x;y=a->ground_y+a->airborne_y;z=a->z;height=RASTERFALL_HUMAN_HEIGHT_RFU;
                if(a->core_hit_radius>0){radius=a->core_hit_radius;height=a->core_hit_height;core=1;}
            } else {
                const struct toy_game_enemy *e=&g->enemies[j-TOY_GAME_MAX_ACTORS];
                if(e->active!=1 || e->hp<=0)continue;
                x=e->x;y=e->ground_y+e->airborne_y;z=e->z;
                height=e->type==TOY_GAME_ENEMY_TANK?1600:e->type==TOY_GAME_ENEMY_SMOKER?1250:1000;
            }
            t=combat_bullet_body(b,x,y,z,height,radius,best);
            if(t<0 || (t==best && !core && (world_hit || actor_hit>=0 || enemy_hit>=0)))continue;
            best=t;actor_hit=j<TOY_GAME_MAX_ACTORS?j:-1;
            enemy_hit=j>=TOY_GAME_MAX_ACTORS?j-TOY_GAME_MAX_ACTORS:-1;world_hit=0;
        }
        b->distance+=best;b->age_ms+=dt_ms;
        b->x=b->sx+(int)((long long)b->dx*b->distance/1024);
        b->y=b->sy+(int)((long long)b->dy*b->distance/1024);
        b->z=b->sz+(int)((long long)b->dz*b->distance/1024);
        expired=b->distance>=w->range || b->age_ms>=2000;
        if(!world_hit && actor_hit<0 && enemy_hit<0 && !expired)continue;
        ray.ex=b->x;ray.ez=b->z;ray.hit_y=b->y;ray.sy=b->dx;ray.cy=b->dz;ray.vy=b->dy;
        ray.actor_index=actor_hit;ray.enemy_index=enemy_hit;ray.hit_world=world_hit;
        ray.hit_enemy=actor_hit>=0 || enemy_hit>=0;
        damage_milli=toy_game_weapon_damage_milli(b->weapon,b->distance);
        ray.damage=ray.hit_enemy?(damage_milli+500)/1000:0;
        if(actor_hit>=0) {
            struct toy_game_actor *a=&g->actors[actor_hit];
            struct toy_game_attack attack={0};struct toy_game_damage_result result={0};
            ray.actor_generation=a->combat_generation;
            ray.weakpoint=!a->base_core && b->y>=a->ground_y+a->airborne_y+RASTERFALL_HUMAN_HEIGHT_RFU*84/100;
            attack.kind=TOY_GAME_ATTACK_BULLET;attack.source_x=b->sx;attack.source_z=b->sz;
            attack.base_damage_milli=damage_milli;
            attack.health_damage_milli=damage_milli*(ray.weakpoint?b->weakpoint_percent:100)/100;
            source->combat_stats.hits++;
            if(!g->defer_actor_damage)result=toy_game_damage_actor(g,source,a,&attack);
            ray.health_damage=result.health_damage;ray.evasion_spent_milli=result.evasion_spent_milli;
            if(result.killed)ray.hit_enemy=2;
        } else if(enemy_hit>=0) {
            int before=g->enemies[enemy_hit].hp;
            source->combat_stats.hits++;
            toy_game_apply_reported_hit(g,source,enemy_hit,ray.damage);
            ray.health_damage=before-g->enemies[enemy_hit].hp;
            source->combat_stats.health_damage+=ray.health_damage;
            source->combat_stats.overkill+=ray.damage-ray.health_damage;
        }
        {
            struct toy_game_shot_event *e=combat_shot_event(g);
            e->phase=TOY_GAME_SHOT_IMPACT;e->source_id=b->source_id;e->source_generation=b->source_generation;
            e->fire_sequence=b->fire_sequence;e->weapon=b->weapon;e->x=b->sx;e->y=b->sy;e->z=b->sz;
            e->ray_count=1;e->killed_rays=ray.hit_enemy==2;ray.hit_enemy=ray.hit_enemy!=0;e->rays[0]=ray;
        }
        b->active=0;
    }
}
