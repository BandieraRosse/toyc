/* Combat experiments own their participants and results, never the world.
 * All attacks, movement and timers use the regular fixed-step Game API. */
#define RF_COMBAT_PRESETS 21
#define RF_COMBAT_MEMBERS 32
#define RF_COMBAT_RESULTS 64
#define RF_COMBAT_DT_MS 16
#ifndef RF_COMBAT_BUILD_COMMIT
#define RF_COMBAT_BUILD_COMMIT "source-build-unrecorded"
#endif
struct rf_combat_member {
    int actor_id, enemy_slot, side, initial_hp, dead, reloads, was_reloading;
    unsigned int generation;
};
struct rf_combat_result {
    int preset,seed,repeat,observed,status,elapsed_ms,ttk_ms,first_damage_ms,exhausted_ms;
    int shots,hits,pellets,health_damage,damage_taken,evasion_spent,reloads,exhaustions;
    int armor_absorbed,overkill,evasion_events,side_shots[2],side_hits[2],side_damage[2],side_hp[2];
    int allied_down,hostile_down,infected_down,remaining_hp,remaining_evasion,crossed;
    int ticks,frames; int64_t logic_us,ai_us,enemy_us,frame_us,frame_max_us,frame_median_us,frame_p95_us;
    unsigned nav_queries,ground_queries;
};
static struct {
    int selected,observe,running,menu_open,result_open,control_near,result_near,suppress_fire;
    int origin_x,origin_z,elapsed_ms,member_count,flag_index,old_flag_count;
    int player_owned,subject_id,seed,repeat,result_count,error,suite,previous_game_state;
    unsigned int subject_generation;
    int first_damage_ms,exhausted_ms,crossed,ttk_ms;
    uint64_t previous_rng;
    struct toy_game_actor previous_player;
    struct camera previous_camera;
    struct rf_combat_member members[RF_COMBAT_MEMBERS];
    struct rf_combat_result current,results[RF_COMBAT_RESULTS];
    int64_t intervals[8192];
    const char *output;
    char commit[64];
    struct toy_game_update_profile profile;
    struct toy_game_update_profile *previous_profile;
} rf_combat_lab;
static int rf_combat_write(void);
static int rf_combat_modal(void) {return rf_combat_lab.menu_open || rf_combat_lab.result_open;}
static const char *rf_combat_names[RF_COMBAT_PRESETS]={
    "WEAPON / AK", "WEAPON / SMG", "WEAPON / AWP", "WEAPON / SHOTGUN", "WEAPON / PISTOL",
    "DUEL / STANDARD", "DUEL / STANDARD MIRROR", "DUEL / ELITE", "DUEL / ELITE MIRROR",
    "DUEL / MOBILE", "DUEL / MOBILE MIRROR", "CROSS / NO EVASION", "CROSS / MEDIUM EVASION",
    "CROSS / HIGH EVASION", "MELEE / SINGLE / NONE", "MELEE / SINGLE / MEDIUM",
    "MELEE / FOUR / NONE", "MELEE / FOUR / MEDIUM", "MIXED / 3 V 3 + 4 INFECTED",
    "SCALE / 4 ALLIES + 16 INFECTED", "SCALE / BASELINE + 4 GUNNERS"
};
static struct toy_game_actor *rf_combat_actor(struct toy_game *g,int id)
{ return toy_game_actor_by_id(g,id); }
static struct toy_game_actor *rf_combat_owned_actor(struct toy_game *g,const struct rf_combat_member *m)
{
    struct toy_game_actor *a=rf_combat_actor(g,m->actor_id);
    return a && a->combat_generation==m->generation?a:NULL;
}
static struct toy_game_actor *rf_combat_subject(struct toy_game *g)
{
    struct toy_game_actor *a=rf_combat_actor(g,rf_combat_lab.subject_id);
    return a && a->combat_generation==rf_combat_lab.subject_generation?a:NULL;
}
static int rf_combat_actor_add(struct toy_game *g,int preset,int hostile,int x,int z,const char *name)
{
    if(rf_combat_lab.member_count>=RF_COMBAT_MEMBERS) return -1;
    int id=hostile?toy_game_add_gunner(g,preset==TOY_GAME_COMBAT_ELITE,x,z,name):
        toy_game_add_ai(g,TOY_GAME_AI_LEVEL_1,x,z,name);
    struct toy_game_actor *a=rf_combat_actor(g,id);
    if(!a) return -1;
    a->class_id=TOY_GAME_AI_LEVEL_1;
    toy_game_actor_set_combat_template(a,preset);
    a->faction=hostile?TOY_GAME_FACTION_HOSTILE:TOY_GAME_FACTION_ALLIED;
    if(!hostile)a->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
    toy_game_actor_equip_weapon(g,a,TOY_GAME_WEAPON_AK);toy_game_actor_refill_ammo(g,a);
    a->flag_index=-1;a->deployment_x=x;a->deployment_z=z;
    a->sy=x<rf_combat_lab.origin_x?1024:-1024;a->cy=0;a->pitch_sy=0;a->pitch_cy=1024;
    toy_game_actor_reset_combat(a,1);
    struct rf_combat_member *m=&rf_combat_lab.members[rf_combat_lab.member_count++];
    memset(m,0,sizeof(*m));m->actor_id=id;m->enemy_slot=-1;m->side=hostile;m->initial_hp=a->hp;
    m->generation=a->combat_generation;
    if(!hostile && rf_combat_lab.flag_index>=0) a->flag_index=rf_combat_lab.flag_index;
    return id;
}
static int rf_combat_enemy_add(struct toy_game *g,int x,int z,int fixed)
{
    unsigned char occupied[TOY_GAME_MAX_ENEMIES];
    if(rf_combat_lab.member_count>=RF_COMBAT_MEMBERS)return -1;
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i)occupied[i]=g->enemies[i].active!=0;
    struct toy_game_box box={x-40,x+40,z-40,z+40,0,0};
    if(toy_game_spawn_horde_type(g,TOY_GAME_ENEMY_PURSUIT_COMMON,1,1,&box,1,0)!=1)return -1;
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i)if(!occupied[i] && g->enemies[i].active) {
        struct toy_game_enemy *e=&g->enemies[i];
        if(fixed){e->speed=0;e->hp=240;}
        struct rf_combat_member *m=&rf_combat_lab.members[rf_combat_lab.member_count++];
        memset(m,0,sizeof(*m));m->actor_id=-1;m->enemy_slot=i;m->side=2;m->initial_hp=e->hp;
        return i;
    }
    return -1;
}
static void rf_combat_cleanup(struct rf_game_runtime *runtime)
{
    struct rasterfall_session *s=runtime->session;
    struct toy_game *g=&s->game_state;
    for(int i=0;i<rf_combat_lab.member_count;++i) {
        struct rf_combat_member *m=&rf_combat_lab.members[i];
        struct toy_game_actor *owned=m->enemy_slot<0?rf_combat_owned_actor(g,m):NULL;
        if(m->enemy_slot<0 && !owned)continue;
        if(owned)g->money-=owned->combat_stats.money_earned;
        if(m->enemy_slot>=0) {
            struct toy_game_enemy *e=&g->enemies[m->enemy_slot];
            if(e->active==1 && e->hp>0 && g->enemies_alive>0)g->enemies_alive--;
            memset(e,0,sizeof(*e));
        } else if(m->actor_id!=rf_combat_lab.previous_player.actor_id) {
            memset(owned,0,sizeof(*owned));
        }
        if(m->enemy_slot>=0)continue;
        for(int j=0;j<TOY_GAME_MAX_PROJECTILES;++j)
            if(g->projectiles[j].active && g->projectiles[j].owner_actor_id==m->actor_id)
                g->projectiles[j].active=0;
        for(int j=0;j<TOY_CONFIG_MAX_BURN_ZONES;++j)
            if(g->burn_zones[j].active && g->burn_zones[j].owner_actor_id==m->actor_id)
                g->burn_zones[j].active=0;
    }
    if(rf_combat_lab.flag_index>=0) {
        memset(&s->flags[rf_combat_lab.flag_index],0,sizeof(s->flags[0]));
        while(s->flag_count>rf_combat_lab.old_flag_count && !s->flags[s->flag_count-1].active)s->flag_count--;
        if(s->carried_flag==rf_combat_lab.flag_index)s->carried_flag=-1;
        if(s->assignment_flag==rf_combat_lab.flag_index)s->assignment_flag=-1;
    }
    *toy_game_local_player_actor(g)=rf_combat_lab.previous_player;
    runtime->camera=rf_combat_lab.previous_camera;
    g->rng=rf_combat_lab.previous_rng;g->update_profile=rf_combat_lab.previous_profile;
    g->state=rf_combat_lab.previous_game_state;
    rasterfall_session_set_rts(s,0);runtime->rts_active=0;runtime->rts_selected=-1;
    rf_combat_lab.member_count=0;rf_combat_lab.running=0;rf_combat_lab.flag_index=-1;
}
static int rf_combat_start(struct rf_game_runtime *runtime,int preset,int observe,int seed,int repeat)
{
    struct rasterfall_session *s=runtime->session;
    struct toy_game *g=&s->game_state;
    struct toy_game_actor *p=toy_game_local_player_actor(g);
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(&s->map_ops.runtime,"perf_low_area");
    if(rf_combat_lab.running || rf_perf_lab.running || rf_perf_lab.menu_open || !area ||
       preset<0 || preset>=RF_COMBAT_PRESETS || s->world_id!=RASTERFALL_WORLD_OUTPOST ||
       runtime->net.mode!=RASTERFALL_NET_OFF || !p || p->state!=TOY_GAME_ACTOR_ALIVE ||
       p->special_control!=TOY_GAME_SPECIAL_CONTROL_NONE || p->control_disabled)return -1;
    /* Do not take ownership of strangers already using the field. */
    for(int i=1;i<TOY_GAME_MAX_ACTORS;++i)if(g->actors[i].active &&
        g->actors[i].x>=area->bounds.min_x && g->actors[i].x<=area->bounds.max_x &&
        g->actors[i].z>=area->bounds.min_z && g->actors[i].z<=area->bounds.max_z)return -1;
    for(int i=0;i<TOY_GAME_MAX_ENEMIES;++i)if(g->enemies[i].active &&
        g->enemies[i].x>=area->bounds.min_x && g->enemies[i].x<=area->bounds.max_x &&
        g->enemies[i].z>=area->bounds.min_z && g->enemies[i].z<=area->bounds.max_z)return -1;
    rf_combat_lab.selected=preset;rf_combat_lab.observe=observe;
    rf_combat_lab.seed=seed;rf_combat_lab.repeat=repeat;
    rf_combat_lab.previous_player=*p;rf_combat_lab.previous_camera=runtime->camera;
    rf_combat_lab.previous_rng=g->rng;rf_combat_lab.previous_profile=g->update_profile;
    rf_combat_lab.previous_game_state=g->state;
    rf_combat_lab.origin_x=area->origin_x;rf_combat_lab.origin_z=area->origin_z;
    rf_combat_lab.elapsed_ms=rf_combat_lab.member_count=rf_combat_lab.crossed=0;
    rf_combat_lab.first_damage_ms=rf_combat_lab.exhausted_ms=rf_combat_lab.ttk_ms=-1;
    memset(&rf_combat_lab.current,0,sizeof(rf_combat_lab.current));
    memset(&rf_combat_lab.profile,0,sizeof(rf_combat_lab.profile));
    rf_combat_lab.profile.clock_us=rf_core_clock_now_us;
    g->update_profile=&rf_combat_lab.profile;
    rf_combat_lab.flag_index=-1;rf_combat_lab.old_flag_count=s->flag_count;
    if(s->flag_count<RASTERFALL_MAX_FLAGS) {
        int fi=s->flag_count++;rf_combat_lab.flag_index=fi;
        memset(&s->flags[fi],0,sizeof(s->flags[fi]));s->flags[fi].active=1;
        s->flags[fi].x=area->origin_x-2500;s->flags[fi].z=area->origin_z;
        s->flags[fi].color=0x57BCD5;s->flags[fi].facing=1;strcpy(s->flags[fi].label,"LAB");
        s->flags[fi].carrier_id=-1;
        /* Same compact square as the standard session flag.  These offsets
         * apply only when commanded; preset spawn/deployment stays intact. */
        static const int offsets[4][2]={{420,420},{-420,420},{-420,-420},{420,-420}};
        memcpy(s->flags[fi].slot_offsets,offsets,sizeof(offsets));
    }
    g->rng=(uint64_t)seed;
    int ox=area->origin_x,oz=area->origin_z;
    p->airborne_ms=p->airborne_y=p->vertical_velocity=0;p->air_x=p->air_z=0;
    p->knockback_x=p->knockback_z=p->knockback_cooldown_ms=0;
    p->moving=p->nav_active=p->hit_test_dummy=p->animation_demo=0;
    p->ground_y=0;memset(&p->animation,0,sizeof(p->animation));
    p->developer_only=1;p->x=area->bounds.min_x-1300;p->z=oz;
    p->sy=1024;p->cy=0;p->pitch_sy=0;p->pitch_cy=1024;
    runtime->camera.x=p->x;runtime->camera.z=p->z;runtime->camera.sy=1024;runtime->camera.cy=0;
    runtime->camera.pitch_sy=0;runtime->camera.pitch_cy=1024;
    rf_combat_lab.player_owned=!observe;rf_combat_lab.subject_id=-1;
    int template_id=TOY_GAME_COMBAT_STANDARD;
    if(preset==7 || preset==8)template_id=TOY_GAME_COMBAT_ELITE;
    if(preset==9 || preset==10)template_id=TOY_GAME_COMBAT_MOBILE;
    if(preset==12 || preset==15 || preset==17)template_id=TOY_GAME_COMBAT_ELITE;
    if(preset==13 || preset==18 || preset>=19)template_id=TOY_GAME_COMBAT_PLAYER;
    int sx=ox-2300,sz=oz,mirror=(preset==6 || preset==8 || preset==10);
    if(preset>=11 && preset<=13){sx=ox-2300;sz=oz-1700;}
    if(preset>=14 && preset<=17)sx=ox;
    if(!observe) {
        p->developer_only=0;p->x=sx;p->z=sz;p->faction=mirror?TOY_GAME_FACTION_HOSTILE:TOY_GAME_FACTION_ALLIED;
        p->class_id=TOY_GAME_AI_LEVEL_1;
        toy_game_actor_set_combat_template(p,template_id);toy_game_actor_reset_combat(p,1);
        toy_game_actor_equip_weapon(g,p,TOY_GAME_WEAPON_AK);toy_game_actor_refill_ammo(g,p);
        rf_combat_lab.subject_id=p->actor_id;
        struct rf_combat_member *m=&rf_combat_lab.members[rf_combat_lab.member_count++];
        memset(m,0,sizeof(*m));m->actor_id=p->actor_id;m->enemy_slot=-1;m->side=mirror;m->initial_hp=p->hp;
        m->generation=p->combat_generation;
        runtime->camera.x=sx;runtime->camera.z=sz;runtime->camera.sy=1024;
    } else rf_combat_lab.subject_id=rf_combat_actor_add(g,template_id,mirror,sx,sz,"LAB SUBJECT");
    struct toy_game_actor *subject=rf_combat_actor(g,rf_combat_lab.subject_id);
    if(!subject)goto failed;
    rf_combat_lab.subject_generation=subject->combat_generation;
    if(preset>=11 && preset<=17) {
        /* Effective tier 3 for every non-evasion skill, across levels.
         * Only evasion differs: none / tier 4 (65) / tier 5 (90). */
        int high=preset==13,none=preset==11 || preset==14 || preset==16;
        subject->class_id=high?TOY_GAME_AI_LEVEL_2:TOY_GAME_AI_LEVEL_1;
        for(int skill=0;skill<TOY_GAME_SKILL_COUNT;++skill)subject->skills.mastery[skill]=high?0:2;
        subject->skills.mastery[TOY_GAME_SKILL_EVASION]=none?0:high?2:3;
        struct toy_game_capabilities caps;
        toy_game_actor_capabilities(subject,toy_game_actor_current_weapon(subject),&caps);
        subject->hp=subject->max_hp=caps.max_hp;
    }
    if(preset<5) {
        static const int weapons[]={TOY_GAME_WEAPON_AK,TOY_GAME_WEAPON_SMG,TOY_GAME_WEAPON_AWP,TOY_GAME_WEAPON_SHOTGUN,TOY_GAME_WEAPON_PISTOL};
        toy_game_actor_equip_weapon(g,subject,weapons[preset]);toy_game_actor_refill_ammo(g,subject);
        for(int slot=0;slot<TOY_GAME_WEAPON_SLOTS;++slot)
            if(subject->slots[slot].weapon==weapons[preset])toy_game_actor_switch_weapon(g,subject,slot);
        subject->hit_test_dummy=observe;
        if(rf_combat_enemy_add(g,sx+4096,sz,1)<0)goto failed;
    } else if(preset<11) {
        if(rf_combat_actor_add(g,template_id,!mirror,ox+2300,oz,"LAB OPPONENT")<0)goto failed;
    } else if(preset<14) {
        subject->hit_test_dummy=observe;subject->fire_enabled=0;
        for(int i=0;i<2;++i) {
            int id=rf_combat_actor_add(g,TOY_GAME_COMBAT_STANDARD,1,ox-1600+3200*i,oz+1500,"LAB CROSSFIRE");
            struct toy_game_actor *a=rf_combat_actor(g,id);if(!a)goto failed;a->hit_test_dummy=1;
        }
    } else if(preset<18) {
        subject->hit_test_dummy=observe;subject->fire_enabled=0;
        int count=preset>=16?4:1;
        for(int i=0;i<count;++i)if(rf_combat_enemy_add(g,ox+(i==0?500:i==1?-500:0),oz+(i==2?500:i==3?-500:0),0)<0)goto failed;
    } else {
        int allies=preset==18?3:4,gunners=preset==18?3:preset==20?4:0,infected=preset==18?4:16;
        for(int i=1;i<allies;++i)if(rf_combat_actor_add(g,TOY_GAME_COMBAT_STANDARD,0,ox-2600,oz-1800+i*900,"LAB ALLY")<0)goto failed;
        for(int i=0;i<gunners;++i)if(rf_combat_actor_add(g,i==0?TOY_GAME_COMBAT_ELITE:TOY_GAME_COMBAT_GUNNER,1,ox+2600,oz-1500+i*900,"LAB GUNNER")<0)goto failed;
        for(int i=0;i<infected;++i)if(rf_combat_enemy_add(g,ox-900+(i%4)*600,oz+1000+(i/4)*450,0)<0)goto failed;
    }
    /* Snapshots always start from capability-derived full health/reserve. */
    for(int i=0;i<rf_combat_lab.member_count;++i)if(rf_combat_lab.members[i].enemy_slot<0) {
        struct toy_game_actor *a=rf_combat_actor(g,rf_combat_lab.members[i].actor_id);
        if(a){toy_game_actor_reset_combat(a,1);rf_combat_lab.members[i].initial_hp=a->hp;}
    }
    rf_combat_lab.running=1;rf_combat_lab.menu_open=rf_combat_lab.result_open=0;
    rasterfall_session_set_rts(s,0);runtime->rts_active=0;runtime->rts_selected=-1;
    return 0;
failed:
    rf_combat_cleanup(runtime);return -1;
}
static void rf_combat_aim_fire(struct toy_game *g,struct toy_game_actor *a,int x,int z,int dt)
{
    long long dx=x-a->x,dz=z-a->z;int distance=isqrt(dx*dx+dz*dz);
    if(distance<=0)return;
    struct toy_game_actor_command c;memset(&c,0,sizeof(c));c.switch_slot=-1;
    c.aim_active=1;c.aim_sy=(int)(dx*1024/distance);c.aim_cy=(int)(dz*1024/distance);
    a->pitch_sy=(RASTERFALL_HUMAN_HEIGHT_RFU*64/100-RASTERFALL_HUMAN_EYE_HEIGHT_RFU)*1024/distance;
    a->pitch_cy=1024;
    c.fire_pressed=1;c.fire_held=1;
    if(a->slots[a->current_slot].mag==0)c.reload=1;
    toy_game_execute_actor_command(g,a,&c,dt,100);
}
static void rf_combat_before_step(struct rf_game_runtime *runtime,int dt)
{
    if(!rf_combat_lab.running)return;
    struct toy_game *g=&runtime->session->game_state;
    struct toy_game_actor *subject=rf_combat_subject(g);
    if(!subject || subject->state!=TOY_GAME_ACTOR_ALIVE)return;
    int preset=rf_combat_lab.selected;
    if(preset<5 && rf_combat_lab.observe)
        rf_combat_aim_fire(g,subject,rf_combat_lab.origin_x+1796,rf_combat_lab.origin_z,dt);
    if(preset>=11 && preset<=13)for(int i=0;i<rf_combat_lab.member_count;++i) {
        struct rf_combat_member *m=&rf_combat_lab.members[i];
        if(m->side==1 && m->enemy_slot<0) {
            struct toy_game_actor *a=rf_combat_owned_actor(g,m);
            struct toy_game_capabilities caps;
            if(a)toy_game_actor_capabilities(a,toy_game_actor_current_weapon(a),&caps);
            if(a && a->state==TOY_GAME_ACTOR_ALIVE && rf_combat_lab.elapsed_ms>=caps.aim_ms)
                rf_combat_aim_fire(g,a,subject->x,subject->z,dt);
        }
    }
    if(rf_combat_lab.observe && ((preset>=11 && preset<=13) ||
        (preset>=14 && preset<=17 && rf_combat_lab.elapsed_ms>=10000))) {
        struct toy_game_actor_command c;memset(&c,0,sizeof(c));c.switch_slot=-1;
        c.move_target_active=1;c.move_x=rf_combat_lab.origin_x+(preset<14?2400:4700);c.move_z=subject->z;
        c.move_speed=toy_game_actor_move_step(subject,TOY_CONFIG_AI_RETURN_SPEED);
        toy_game_execute_actor_command(g,subject,&c,dt,100);
    }
}
static void rf_combat_collect(struct rf_game_runtime *runtime)
{
    struct toy_game *g=&runtime->session->game_state;
    struct rf_combat_result *r=&rf_combat_lab.current;
    r->preset=rf_combat_lab.selected;r->seed=rf_combat_lab.seed;r->repeat=rf_combat_lab.repeat;
    r->observed=rf_combat_lab.observe;r->elapsed_ms=rf_combat_lab.elapsed_ms;
    r->shots=r->hits=r->pellets=r->health_damage=r->damage_taken=r->evasion_spent=0;
    r->reloads=r->exhaustions=r->allied_down=r->hostile_down=r->infected_down=0;
    r->armor_absorbed=r->overkill=r->evasion_events=0;
    memset(r->side_shots,0,sizeof(r->side_shots));memset(r->side_hits,0,sizeof(r->side_hits));
    memset(r->side_damage,0,sizeof(r->side_damage));memset(r->side_hp,0,sizeof(r->side_hp));
    for(int i=0;i<rf_combat_lab.member_count;++i) {
        struct rf_combat_member *m=&rf_combat_lab.members[i];
        if(m->enemy_slot>=0) {r->infected_down+=m->dead;continue;}
        struct toy_game_actor *a=rf_combat_owned_actor(g,m);if(!a)continue;
        r->shots+=a->combat_stats.shots;r->hits+=a->combat_stats.hits;r->pellets+=a->combat_stats.pellets;
        r->health_damage+=a->combat_stats.health_damage;r->damage_taken+=a->combat_stats.damage_taken;
        r->evasion_spent+=a->combat_stats.evasion_spent_milli;r->exhaustions+=a->combat_stats.evasion_exhaustions;
        r->armor_absorbed+=a->combat_stats.armor_absorbed;r->overkill+=a->combat_stats.overkill;
        r->evasion_events+=a->combat_stats.evasion_events;
        r->side_shots[m->side]+=a->combat_stats.shots;r->side_hits[m->side]+=a->combat_stats.hits;
        r->side_damage[m->side]+=a->combat_stats.health_damage;r->side_hp[m->side]+=a->hp;
        r->reloads+=m->reloads;
        if(m->side)r->hostile_down+=a->state!=TOY_GAME_ACTOR_ALIVE;
        else r->allied_down+=a->state!=TOY_GAME_ACTOR_ALIVE;
    }
    struct toy_game_actor *subject=rf_combat_subject(g);
    r->remaining_hp=subject?subject->hp:0;r->remaining_evasion=subject?subject->evasion.reserve_milli:0;
    r->ttk_ms=rf_combat_lab.ttk_ms;r->first_damage_ms=rf_combat_lab.first_damage_ms;
    r->exhausted_ms=rf_combat_lab.exhausted_ms;r->crossed=rf_combat_lab.crossed;
    r->ai_us=rf_combat_lab.profile.teammate_us;r->enemy_us=rf_combat_lab.profile.enemy_us;
    r->nav_queries=rf_combat_lab.profile.nav_queries;r->ground_queries=rf_combat_lab.profile.ground_queries;
    if(r->frames>0) {
        qsort(rf_combat_lab.intervals,(unsigned)r->frames,sizeof(rf_combat_lab.intervals[0]),rf_perf_lab_compare_intervals);
        r->frame_median_us=rf_combat_lab.intervals[r->frames/2];
        r->frame_p95_us=rf_combat_lab.intervals[(r->frames-1)*95/100];
    }
}
static void rf_combat_finish(struct rf_game_runtime *runtime,int status)
{
    if(!rf_combat_lab.running)return;
    rf_combat_collect(runtime);rf_combat_lab.current.status=status;
    if(rf_combat_lab.result_count==RF_COMBAT_RESULTS) {
        memmove(&rf_combat_lab.results[0],&rf_combat_lab.results[1],
            (RF_COMBAT_RESULTS-1)*sizeof(rf_combat_lab.results[0]));rf_combat_lab.result_count--;
    }
    if(rf_combat_lab.result_count<RF_COMBAT_RESULTS)
        rf_combat_lab.results[rf_combat_lab.result_count++]=rf_combat_lab.current;
    rf_combat_cleanup(runtime);rf_combat_lab.result_open=1;
    if(!rf_combat_lab.suite)rf_combat_lab.error=rf_combat_write()<0;
}
static void rf_combat_after_step(struct rf_game_runtime *runtime,int dt,int64_t elapsed_us)
{
    if(!rf_combat_lab.running)return;
    struct toy_game *g=&runtime->session->game_state;
    rf_combat_lab.elapsed_ms+=dt;rf_combat_lab.current.ticks++;
    rf_combat_lab.current.logic_us+=elapsed_us;
    int alive[3]={0,0,0};
    for(int i=0;i<rf_combat_lab.member_count;++i) {
        struct rf_combat_member *m=&rf_combat_lab.members[i];
        if(m->enemy_slot>=0) {
            struct toy_game_enemy *e=&g->enemies[m->enemy_slot];
            if(e->active==1 && e->hp>0)alive[2]++;
            else {
                m->dead=1;e->active=2;
                /* Reserve the slot at the final valid death-animation phase.
                 * Extending beyond DYING_MS violates the render pose range. */
                if(e->dying_ms<=RF_COMBAT_DT_MS)e->dying_ms=RF_COMBAT_DT_MS+1;
            }
        } else {
            struct toy_game_actor *a=rf_combat_owned_actor(g,m);if(!a)continue;
            if(a->state==TOY_GAME_ACTOR_ALIVE)alive[m->side]++;
            if(a->reloading && !m->was_reloading)m->reloads++;
            m->was_reloading=a->reloading;
        }
    }
    struct toy_game_actor *subject=rf_combat_subject(g);
    if(subject && subject->combat_stats.damage_taken>0 && rf_combat_lab.first_damage_ms<0)
        rf_combat_lab.first_damage_ms=rf_combat_lab.elapsed_ms;
    if(subject && subject->combat_stats.evasion_exhaustions>0 && rf_combat_lab.exhausted_ms<0)
        rf_combat_lab.exhausted_ms=rf_combat_lab.elapsed_ms;
    int preset=rf_combat_lab.selected,finished=0;
    if(preset<5 && !alive[2]){rf_combat_lab.ttk_ms=rf_combat_lab.elapsed_ms;finished=1;}
    else if(preset>=5 && preset<11 && (!alive[0] || !alive[1])){rf_combat_lab.ttk_ms=rf_combat_lab.elapsed_ms;finished=1;}
    else if(preset>=11 && preset<18) {
        if(!subject || subject->state!=TOY_GAME_ACTOR_ALIVE)finished=1;
        else if(subject->x>=rf_combat_lab.origin_x+(preset<14?1900:4200)){rf_combat_lab.crossed=1;finished=1;}
    } else if(preset==18 && (!alive[0] || (!alive[1] && !alive[2])))finished=1;
    if(finished)rf_combat_finish(runtime,1);
    else if(rf_combat_lab.elapsed_ms>=30000)rf_combat_finish(runtime,2);
}
static int rf_combat_write(void)
{
    int fd=__creat(rf_combat_lab.output?rf_combat_lab.output:"combat-lab.csv",0644);
    if(fd<0)return -1;
    const char *header="commit,build,preset,configuration,seed,dt_ms,repeat,control,status,elapsed_ms,ttk_ms,first_damage_ms,exhausted_ms,shots,pellets,hits,health_damage,damage_taken,evasion_spent_milli,reloads,exhaustions,allied_down,hostile_down,infected_down,remaining_hp,remaining_evasion_milli,crossed,ticks,logic_us,ai_us,enemy_us,nav_queries,ground_queries,frames,frame_mean_us,frame_max_us,frame_median_us,frame_p95_us,armor_absorbed,overkill,evasion_events,allied_shots,hostile_shots,allied_hits,hostile_hits,allied_damage,hostile_damage,allied_hp,hostile_hp\n";
    int length=(int)strlen(header),failed=__write(fd,header,length)!=length;
    for(int i=0;i<rf_combat_lab.result_count && !failed;++i) {
        const struct rf_combat_result *r=&rf_combat_lab.results[i];char line[1024];
        length=snprintf(line,sizeof(line),"%s,%s %s,%d,%s,%d,%d,%d,%s,%s,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%lld,%lld,%lld,%u,%u,%d,%lld,%lld,%lld,%lld,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d,%d\n",
            rf_combat_lab.commit,__DATE__,__TIME__,r->preset,rf_combat_names[r->preset],r->seed,RF_COMBAT_DT_MS,r->repeat,
            r->observed?"scripted":"player",r->status==1?"complete":r->status==2?"timeout":"aborted",
            r->elapsed_ms,r->ttk_ms,r->first_damage_ms,r->exhausted_ms,r->shots,r->pellets,r->hits,r->health_damage,r->damage_taken,
            r->evasion_spent,r->reloads,r->exhaustions,r->allied_down,r->hostile_down,r->infected_down,r->remaining_hp,
            r->remaining_evasion,r->crossed,r->ticks,(long long)r->logic_us,(long long)r->ai_us,(long long)r->enemy_us,
            r->nav_queries,r->ground_queries,r->frames,(long long)(r->frames?r->frame_us/r->frames:0),(long long)r->frame_max_us,
            (long long)r->frame_median_us,(long long)r->frame_p95_us,r->armor_absorbed,r->overkill,r->evasion_events,
            r->side_shots[0],r->side_shots[1],r->side_hits[0],r->side_hits[1],r->side_damage[0],r->side_damage[1],r->side_hp[0],r->side_hp[1]);
        failed=length<=0 || length>=(int)sizeof(line) || __write(fd,line,length)!=length;
    }
    __close(fd);return failed?-1:0;
}
static void rf_combat_init(const struct rasterfall_options *o)
{
    memset(&rf_combat_lab,0,sizeof(rf_combat_lab));rf_combat_lab.flag_index=-1;
    rf_combat_lab.output=o->combat_lab_output;rf_combat_lab.seed=o->combat_lab_seed;
    rf_combat_lab.suite=o->combat_lab_suite;
    rf_combat_lab.observe=o->combat_lab_observe;
    const char *commit=getenv("RF_COMBAT_COMMIT");
    if(!commit || !*commit)commit=RF_COMBAT_BUILD_COMMIT;
    if(!*commit)commit="source-build-unrecorded";
    snprintf(rf_combat_lab.commit,sizeof(rf_combat_lab.commit),"%s",commit);
}
static void rf_combat_draw(struct rasterfall_canvas *c, int rts_active)
{
    char line[160];int x=(c->width-820)/2,y=(c->height-500)/2;
    if(rf_combat_lab.running) {
        int status_y=rts_active?105:35;
        snprintf(line,sizeof(line),"COMBAT %s  %.1fs  %s",rf_combat_names[rf_combat_lab.selected],
            rf_combat_lab.elapsed_ms/1000.0,rf_combat_lab.observe?"SCRIPTED":"FPS / M RTS");
        rasterfall_canvas_text(c,25,status_y,line,0xA1DFEE);
        rasterfall_canvas_text(c,25,status_y+23,"T STOP / RESULTS / RESET   ESC PAUSE",0xB5C1CB);
        return;
    }
    rasterfall_canvas_rect(c,x,y,820,500,0x172530,235);
    rasterfall_canvas_text(c,x+25,y+20,rf_combat_lab.result_open?"COMBAT RESULTS":"COMBAT LAB V0",0xA1DFEE);
    if(rf_combat_lab.result_open) {
        const struct rf_combat_result *r=&rf_combat_lab.current;
        if(!rf_combat_lab.result_count)rasterfall_canvas_text(c,x+25,y+85,"NO RESULT",0xFFBE8A);
        else {
            snprintf(line,sizeof(line),"%s / %s",rf_combat_names[r->preset],r->status==1?"COMPLETE":r->status==2?"TIMEOUT / NO TTK":"ABORTED");
            rasterfall_canvas_text(c,x+25,y+70,line,0xFFFFFF);
            snprintf(line,sizeof(line),"SEED %d  STEP %dms  REPEAT %d  %s",r->seed,RF_COMBAT_DT_MS,r->repeat,r->observed?"SCRIPTED":"PLAYER");
            rasterfall_canvas_text(c,x+25,y+108,line,0xB5C1CB);
            char ttk[32];if(r->ttk_ms<0)strcpy(ttk,"N/A");else snprintf(ttk,sizeof(ttk),"%.2fs",r->ttk_ms/1000.0);
            snprintf(line,sizeof(line),"TIME %.2fs  TTK %s  HP %d  EVA %.1f",r->elapsed_ms/1000.0,ttk,r->remaining_hp,r->remaining_evasion/1000.0);
            rasterfall_canvas_text(c,x+25,y+146,line,0xFFFFFF);
            snprintf(line,sizeof(line),"SHOTS %d  HITS %d / %d PELLETS  RELOADS %d",r->shots,r->hits,r->pellets,r->reloads);
            rasterfall_canvas_text(c,x+25,y+184,line,0xFFFFFF);
            snprintf(line,sizeof(line),"HP OUT/IN %d/%d  EVA %.1f  EMPTY %d",r->health_damage,r->damage_taken,r->evasion_spent/1000.0,r->exhaustions);
            rasterfall_canvas_text(c,x+25,y+222,line,0xFFFFFF);
            snprintf(line,sizeof(line),"DOWN ALLY %d / HOSTILE %d / INFECTED %d  CROSSED %s",r->allied_down,r->hostile_down,r->infected_down,r->crossed?"YES":"NO");
            rasterfall_canvas_text(c,x+25,y+260,line,0xFFFFFF);
            snprintf(line,sizeof(line),"FIRST HP %dms  EVA EMPTY %dms (-1 = NOT REACHED)",r->first_damage_ms,r->exhausted_ms);
            rasterfall_canvas_text(c,x+25,y+298,line,0xB5C1CB);
            rasterfall_canvas_text(c,x+25,y+352,rf_combat_lab.error?"CSV WRITE FAILED - CHECK OUTPUT PATH":"RESULTS SAVED TO COMBAT LAB CSV",rf_combat_lab.error?0xFFBE8A:0xA1DFEE);
        }
        rasterfall_canvas_text(c,x+25,y+450,"ENTER SAME PRESET / RESET   F2 CONFIGURE   ESC CLOSE",0xA1DFEE);
    } else {
        int first=rf_combat_lab.selected-3;if(first<0)first=0;if(first>RF_COMBAT_PRESETS-7)first=RF_COMBAT_PRESETS-7;
        for(int i=first;i<first+7;++i) {
            snprintf(line,sizeof(line),"%c %02d  %s",i==rf_combat_lab.selected?'>':' ',i,rf_combat_names[i]);
            rasterfall_canvas_text(c,x+30,y+70+(i-first)*35,line,i==rf_combat_lab.selected?0xA1DFEE:0xFFFFFF);
        }
        snprintf(line,sizeof(line),"F2 CONTROL: %s   SEED %d",rf_combat_lab.observe?"SCRIPTED / OBSERVE":"PLAYER / FPS + RTS",rf_combat_lab.seed);
        rasterfall_canvas_text(c,x+25,y+345,line,0xA1DFEE);
        rasterfall_canvas_text(c,x+25,y+382,"SAME FIELD / 30s LIMIT / ONLY LAB OBJECTS RESET",0xB5C1CB);
        rasterfall_canvas_text(c,x+25,y+415,"MELEE: HOLD 10s THEN ESCAPE EAST; CROSS: EAST FINISH",0xB5C1CB);
        rasterfall_canvas_text(c,x+25,y+450,"UP/DOWN CONFIG   ENTER START   ESC CLOSE",0xA1DFEE);
    }
}
