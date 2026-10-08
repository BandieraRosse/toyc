/* Game-backed scenario ownership. Unit records below are read-only physical
 * projections plus policy intent; no projection is copied back into Game. */
static int tac_map_geometry(struct rf_tac_map *m)
{
    m->geometry=calloc(1,sizeof(*m->geometry));
    if(!m->geometry) return 0;
    toy_game_init(m->geometry,1);
    struct toy_map_primitive *p=&m->primitives[0];
    p->shape=TOY_MAP_PRIMITIVE_FLAT;p->minx=tac_x(-2);p->maxx=tac_x(RF_TAC_WIDTH_M+2);
    p->minz=tac_z(-8);p->maxz=tac_z(RF_TAC_HEIGHT_M+2);p->flags=TOY_MAP_PRIMITIVE_WALKABLE;
    m->primitive_count=1;
    for(int i=0;i<m->cover_count;++i) {
        const struct rf_tac_cover *c=&m->covers[i];p=&m->primitives[m->primitive_count++];
        p->shape=TOY_MAP_PRIMITIVE_BOX;p->flags=TOY_MAP_PRIMITIVE_COLLISION;
        p->minx=tac_x(c->x0);p->maxx=tac_x(c->x1);
        p->minz=tac_z(c->y0);p->maxz=tac_z(c->y1);
        p->surface_y0=p->surface_y1=(int)lroundf((c->height==RF_TAC_LOW?1.1f:2.2f)*512);
    }
    toy_game_set_primitives(m->geometry,m->primitives,m->primitive_count,65000);
    return 1;
}

void rf_tac_map_destroy(struct rf_tac_map *m)
{ if(m) {free(m->geometry);m->geometry=NULL;} }

static unsigned tac_geometry_hash(const struct toy_game *g)
{
    unsigned h=2166136261u;
    for(int i=0;i<g->primitive_count;++i) {
        const struct toy_map_primitive *p=&g->primitives[i];
        const int values[]={p->shape,p->minx,p->maxx,p->minz,p->maxz,p->base_y,
            p->surface_y0,p->surface_y1,p->ramp_thickness,(int)p->flags};
        for(unsigned j=0;j<sizeof(values)/sizeof(values[0]);++j)h=tac_hash_word(h,(unsigned)values[j]);
    }
    return h;
}

int rf_tac_map_bind(struct rf_tac_map *m,const struct toy_game *g)
{
    if(!m || !g || !m->geometry) return 0;
    memcpy(m->geometry,g,sizeof(*g));m->geometry->update_profile=NULL;
    /* Copy collision facts: the graph's lifetime does not borrow session storage. */
    if(g->primitive_count>TOY_GAME_MAX_PRIMITIVES) return 0;
    m->primitive_count=g->primitive_count;
    memcpy(m->primitives,g->primitives,g->primitive_count*sizeof(*g->primitives));
    m->geometry->primitives=m->primitives;
    tac_map_bake(m);
    m->generation=g->navigation_generation ^ m->seed ^ 0x52465432u;
    m->content_hash=tac_geometry_hash(m->geometry);
    return 1;
}

void rf_tac_sync(struct rf_tac_world *w)
{
    if(!w || !w->game) return;
    w->rng=(unsigned)w->game->rng;
    for(int i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct rf_tac_unit *u=&w->units[i];
        const struct toy_game_actor *a=toy_game_actor_by_id_const(w->game,u->actor_id);
        if(!a || a->combat_generation!=u->actor_generation) {u->alive=0;continue;}
        u->alive=a->state==TOY_GAME_ACTOR_ALIVE && a->hp>0;
        u->pos.x=a->x/512.0f+RF_TAC_WIDTH_M/2;u->pos.y=a->z/512.0f+RF_TAC_HEIGHT_M/2;
        u->hp=a->hp;u->evasion=a->evasion.reserve_milli/1000.0f;
        u->recovery_ms=a->evasion.pressure_ms;
        u->ammo=a->slots[a->current_slot].mag;u->reload_ms=a->reload_timer_ms;
        u->cooldown_ms=a->fire_cooldown_ms;u->recoil_milli_mrad=a->weapon_spread_heat;
        u->shots=a->combat_stats.shots;u->hits=a->combat_stats.hits;
        u->damage=a->combat_stats.health_damage;
        u->absorbed=a->combat_stats.evasion_spent_milli/1000.0f;
        /* Advisory route progress follows actual body motion. */
        while(u->nav_cursor<u->nav_count &&
            tac_distance(u->pos,w->map->nodes[u->nav_nodes[u->nav_cursor]].pos)<0.5f) ++u->nav_cursor;
    }
}

static int tac_spawn(struct rf_tac_world *w)
{
    for(int team=0;team<2;++team) for(int i=0;i<w->squad_size;++i) {
        struct rf_tac_unit *u=&w->units[team*RF_TAC_MAX_SQUAD+i];
        struct rf_tac_vec p=w->map->spawn[team];
        p.x+=(i%2)*1.2f;p.y+=(i-(w->squad_size-1)*0.5f)*1.6f;
        int id=toy_game_add_ai(w->game,TOY_GAME_AI_LEVEL_2,tac_x(p.x),tac_z(p.y),team?"BRAVO":"ALPHA");
        struct toy_game_actor *a=toy_game_actor_by_id(w->game,id);
        if(!a) return 0;
        a->faction=team?TOY_GAME_FACTION_HOSTILE:TOY_GAME_FACTION_ALLIED;
        a->character_id=team?RASTERFALL_CHARACTER_GUNNER_ELITE:RASTERFALL_CHARACTER_RF_RIFLEMAN;
        a->controller_external=1;a->intent_target_id=-1;a->pitch_cy=1024;
        a->sy=team?-1024:1024;a->cy=0;
        toy_game_actor_set_combat_template(a,TOY_GAME_COMBAT_STANDARD);
        toy_game_actor_equip_weapon(w->game,a,u->weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG);
        toy_game_update_actor_ground(w->game,(int)(a-w->game->actors));
        u->id=team*RF_TAC_MAX_SQUAD+i;u->team=team;u->actor_id=id;
        u->actor_generation=a->combat_generation;u->destination=p;
        u->action.kind=RF_TAC_HOLD;u->action.target=-1;u->last_shot_target=-1;
    }
    /* Spawn-time equipment RNG must not depend on unrelated roster allocation. */
    w->game->rng=w->seed?w->seed:1;
    rf_tac_sync(w);return 1;
}

int rf_tac_world_init(struct rf_tac_world *w,const struct rf_tac_map *map,
    unsigned seed,int squad,int weapon,int duration)
{
    if(!w || !map || !map->geometry || squad<1 || squad>RF_TAC_MAX_SQUAD ||
        weapon<0 || weapon>=RF_TW_KIND_COUNT || duration<RF_TAC_DT_MS) return 0;
    memset(w,0,sizeof(*w));w->game=malloc(sizeof(*w->game));if(!w->game)return 0;
    memcpy(w->game,map->geometry,sizeof(*w->game));w->owns_game=1;
    memset(w->game->actors,0,sizeof(w->game->actors));
    memset(w->game->enemies,0,sizeof(w->game->enemies));
    w->game->base_actor_index=-1;w->game->external_director=1;w->game->to_spawn=0;
    w->game->state=TOY_GAME_PLAYING;w->game->update_profile=NULL;
    w->map=map;w->seed=seed;w->squad_size=squad;w->max_time_ms=duration;w->winner=-1;
    for(int t=0;t<2;++t) {
        w->orders[t].kind=RF_TAC_DEFEND;w->orders[t].target=map->spawn[t];
        w->orders[t].radius=map->objective_radius;
        for(int i=0;i<squad;++i)w->units[t*RF_TAC_MAX_SQUAD+i].weapon=weapon;
    }
    if(!tac_spawn(w)) {rf_tac_world_destroy(w);return 0;}
    return 1;
}

void rf_tac_world_destroy(struct rf_tac_world *w)
{
    if(!w || !w->game)return;
    if(w->owns_game) free(w->game);
    else for(int i=0;i<RF_TAC_MAX_UNITS;++i) {
        struct toy_game_actor *a=toy_game_actor_by_id(w->game,w->units[i].actor_id);
        if(a && a->combat_generation==w->units[i].actor_generation)memset(a,0,sizeof(*a));
    }
    w->game=NULL;w->owns_game=0;
}

int rf_tac_world_bind(struct rf_tac_world *w,struct toy_game *game)
{
    if(!w || !game)return 0;
    rf_tac_world_destroy(w);w->game=game;w->owns_game=0;
    game->external_director=1;game->to_spawn=0;
    return tac_spawn(w);
}

int rf_tac_world_clone(struct rf_tac_world *out,const struct rf_tac_world *source)
{
    if(!out || !source || !source->game)return 0;
    *out=*source;out->game=malloc(sizeof(*out->game));out->owns_game=1;
    if(!out->game)return 0;
    memcpy(out->game,source->game,sizeof(*out->game));out->game->update_profile=NULL;
    return 1;
}
