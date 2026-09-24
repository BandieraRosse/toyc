/* Independent normal Scene enemy source. This file is included after
 * render_enemies so all pure pose and projection helpers are available. */
struct scene_independent_enemy_history {
    struct rasterfall_infected_motion infected;
    int valid, type, x, z, phase, elapsed, hit_ms;
    uint64_t hit_mask, moved_us, sampled_us;
};
static struct scene_independent_enemy_history
    scene_independent_enemy_history[TOY_GAME_MAX_ENEMIES];
static uint64_t scene_independent_enemy_generation;
static uint64_t scene_independent_enemy_frame;
static int scene_independent_enemy_family;
static const struct rasterfall_session *scene_independent_enemy_session;

static struct enemy_rig_input scene_independent_enemy_rig_input(
    const struct toy_game_enemy *e,const struct enemy_rig_profile *profile,
    struct scene_independent_enemy_history *history,uint64_t now_us)
{
    struct enemy_rig_input input={0};
    int64_t dx=(int64_t)e->x-history->x;
    int64_t dz=(int64_t)e->z-history->z;
    int64_t distance=(dx<0?-dx:dx)+(dz<0?-dz:dz);
    input.type=e->type;
    input.attack_ms=input.recovery_ms=input.impact_age_ms=-1;
    input.tongue_active=e->special_target_active;
    if (e->active!=1 || !profile) {
        history->valid=0;
        return input;
    }
    if (!history->valid || history->type!=e->type || distance>1200 ||
        now_us<history->sampled_us) {
        history->phase=0;
        history->hit_ms=-10000;
        history->hit_mask=e->ability.charge_hit_actor_mask;
        history->moved_us=0;
        history->elapsed=e->charge_elapsed_ms;
    } else if (distance>0) {
        history->phase=(history->phase+(int)distance)%profile->stride;
        history->moved_us=now_us;
    }
    if (!e->charge_active || e->ability.special_timer_ms>0 ||
        e->charge_elapsed_ms<history->elapsed)
        history->hit_ms=-10000;
    if (e->charge_active &&
        (e->ability.charge_hit_actor_mask & ~history->hit_mask))
        history->hit_ms=e->charge_elapsed_ms;
    history->hit_mask=e->ability.charge_hit_actor_mask;
    history->elapsed=e->charge_elapsed_ms;
    history->valid=1;
    history->sampled_us=now_us;
    history->type=e->type;
    history->x=e->x;history->z=e->z;
    input.stride_phase=history->phase*1024/profile->stride;
    input.moving=history->moved_us && now_us>=history->moved_us &&
        now_us-history->moved_us<100000;
    if (e->charge_active && history->hit_ms>=0)
        input.impact_age_ms=e->charge_elapsed_ms-history->hit_ms;
    if (e->type==TOY_GAME_ENEMY_CHARGER) {
        if (e->charge_active && e->ability.special_timer_ms>0)
            input.windup_q10=1024-e->ability.special_timer_ms*1024/
                TOY_GAME_CHARGER_WINDUP_MS;
        else if (e->charge_active) input.charging=1;
        else if (e->ability.special_timer_ms>0)
            input.recovery_ms=TOY_GAME_CHARGER_COOLDOWN_MS-
                e->ability.special_timer_ms;
    } else if (e->type==TOY_GAME_ENEMY_TANK) {
        if (e->charge_active) input.attack_ms=e->charge_elapsed_ms;
        else if (e->ability.special_timer_ms>0)
            input.recovery_ms=TOY_CONFIG_TANK_COOLDOWN_MS-
                e->ability.special_timer_ms;
    }
    return input;
}

static void scene_independent_enemy_target(const struct toy_game_enemy *e,
    struct rf_gpu_scene_enemy_item_v1 *item)
{
    const struct toy_game_actor *target;
    if (active_net && active_net->mode==RASTERFALL_NET_CLIENT &&
        e->special_target_kind==0 &&
        game.actors[TOY_GAME_PLAYER_ACTOR_INDEX].active)
        target=&game.actors[TOY_GAME_PLAYER_ACTOR_INDEX];
    else if (e->special_target_kind==1 && e->special_target_index>=0 &&
        e->special_target_index<TOY_GAME_MAX_ACTORS &&
        game.actors[e->special_target_index].active)
        target=&game.actors[e->special_target_index];
    else target=toy_game_local_player_actor_const(&game);
    item->target_x=target->x;item->target_y=target->airborne_y;
    item->target_z=target->z;
}

int rf_gpu_scene_enemy_collect_independent(const struct camera *camera,
    uint64_t now_us,uint64_t world_generation)
{
    static const int roll_sin[8]={0,392,724,946,1024,946,724,392};
    static const int roll_cos[8]={1024,946,724,392,0,-392,-724,-946};
    struct scene_independent_enemy_history next[TOY_GAME_MAX_ENEMIES];
    static struct rf_gpu_scene_enemy_frame_v1 frame;
    if (!camera || !active_session || !active_effects || !active_world_lighting ||
        !scene_enemy_capture_active || !world_generation ||
        scene_enemy_capture.world_generation!=world_generation ||
        (scene_independent_enemy_frame==scene_enemy_capture.frame_id &&
         scene_independent_enemy_generation==world_generation))
        return -1;
    if (scene_independent_enemy_generation!=world_generation ||
        scene_independent_enemy_session!=active_session ||
        scene_independent_enemy_family!=enemy_visual_family)
        memset(next,0,sizeof(next));
    else memcpy(next,scene_independent_enemy_history,sizeof(next));
    frame=scene_enemy_capture;
    frame.vertex_lighting=!diagnostic_flat_planar && !diagnostic_constant_world;
    frame.lighting=*active_world_lighting;
    for (int i=0;i<TOY_GAME_MAX_ENEMIES;++i) {
        const struct toy_game_enemy *e=&game.enemies[i];
        const struct toy_game_enemy_info *info;
        const struct enemy_rig_profile *profile;
        const struct rasterfall_effect_instance *feedback;
        struct rf_gpu_scene_enemy_item_v1 *item;
        struct toy_game_enemy presentation;
        struct vec3 center,view;
        int family,scale=1000,lift=0,alpha=255;
        unsigned color=0,tint;
        struct enemy_rig_input rig_input;
        if (!e->active) { memset(&next[i],0,sizeof(next[i]));continue; }
        family=enemy_visual_family_for_enemy(e->type,i);
        profile=enemy_rig_profile(e->type);
        rig_input=scene_independent_enemy_rig_input(e,profile,&next[i],now_us);
        center=(struct vec3){e->x,0,e->z};
        world_to_view(camera,&center,&view);
        if (view.z>(family!=RASTERFALL_ENEMY_VISUAL_LEGACY ?
                    56000:ENEMY_RENDER_DISTANCE)) { frame.culled++;continue; }
        if (frame.count>=TOY_GAME_MAX_ENEMIES) return -1;
        item=&frame.items[frame.count];
        memset(item,0,sizeof(*item));
        presentation=*e;
        info=toy_game_enemy_info_or_null(e->type);
        if (!info) { frame.deferred++;continue; }
        if (e->active==2) {
            int style=effects.enemy_death_style[i];
            if (style==RASTERFALL_ENEMY_DEATH_STYLE_LEGACY ||
                style==RASTERFALL_ENEMY_DEATH_STYLE_NONE) {
                scale=e->dying_ms*1000/TOY_GAME_DYING_MS;
                color=0x5A1A1A;
            } else {
                int elapsed=TOY_GAME_DYING_MS-e->dying_ms;
                int motion=elapsed*elapsed/TOY_GAME_DYING_MS;
                int hit_x=effects.enemy_hit_dir_x[i];
                int hit_z=effects.enemy_hit_dir_z[i];
                int side=((i*37+e->type*19)&127)-64;
                int roll_step=elapsed*(3+((i+e->type)&3))/TOY_GAME_DYING_MS;
                if (!hit_x && !hit_z) { hit_x=e->dir_x;hit_z=e->dir_z; }
                presentation.x+=hit_x*motion/1024-hit_z*side*elapsed/
                    (1024*TOY_GAME_DYING_MS/2);
                presentation.z+=hit_z*motion/1024+hit_x*side*elapsed/
                    (1024*TOY_GAME_DYING_MS/2);
                scale=e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY?1350:1000;
                color=info->color;
                alpha=e->dying_ms>=RASTERFALL_ENEMY_DEATH_FADE_MS?255:
                    e->dying_ms*255/RASTERFALL_ENEMY_DEATH_FADE_MS;
                lift=4*430*elapsed*(TOY_GAME_DYING_MS-elapsed)/
                    (TOY_GAME_DYING_MS*TOY_GAME_DYING_MS);
                item->transformed=1;
                item->pivot_x=presentation.x;
                item->pivot_y=-300+lift;
                item->pivot_z=presentation.z;
                item->roll_sin=roll_sin[roll_step&7];
                item->roll_cos=roll_cos[roll_step&7];
            }
        } else {
            color=info->color;
            if (e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY) scale=1350;
            if (info->ability==TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE)
                color=RF_COLOR_ENEMY_SMOKER;
            if (info->ability==TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH) {
                color=e->charge_active?0xB06A36:RF_COLOR_ENEMY_CHARGER;
                scale=1180;
            }
            if (info->ability==TOY_GAME_ENEMY_ABILITY_TANK_SWEEP) {
                color=RF_COLOR_ENEMY_TANK;scale=1600;
            }
            tint=enemy_feedback_color(i);
            if (tint) color=tint;
            else if (e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY)
                color=RF_COLOR_ENEMY_PURSUIT_HEAVY;
            else if (e->type==TOY_GAME_ENEMY_PURSUIT_FAST)
                color=RF_COLOR_ENEMY_PURSUIT_FAST;
            feedback=enemy_feedback_instance(i);
            if (feedback && (feedback->dir_x || feedback->dir_z)) {
                int offset=18+feedback->size/3;
                if (offset>70) offset=70;
                presentation.x+=feedback->dir_x*offset/1024;
                presentation.z+=feedback->dir_z*offset/1024;
            }
        }
        item->source_slot=(unsigned)i;item->type=e->type;
        item->x=presentation.x;item->z=presentation.z;
        item->dir_x=presentation.dir_x;item->dir_z=presentation.dir_z;
        if (!item->dir_x && !item->dir_z) item->dir_z=1024;
        item->lift=lift+presentation.ground_y+presentation.airborne_y;
        if (item->transformed) item->pivot_y=-300+item->lift;
        item->feedback=enemy_feedback_color(i);
        item->scene_light_q8=rasterfall_world_light_v2_q8(
            rasterfall_world_light_at(active_world_lighting,e->x,
                -900+e->ground_y,e->z));
        item->double_sided=1;
        item->transparent=alpha<255;
        item->alpha=alpha;
        if (item->transparent) frame.transparent++;
        item->squash=e->active==2 && !item->transformed?scale:1000;
        if (!item->transformed && e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY)
            item->squash=e->active==2?scale:1000;
        /* Blob shadow uses ground lift, before airborne height is added. */
        if (!item->transformed) {
            item->shadow=1;
            item->shadow_y=-886+lift+presentation.ground_y;
            item->shadow_rx=230*scale/1000;
            item->shadow_rz=150*scale/1000;
            if (item->shadow_rx<18) item->shadow_rx=18;
            if (item->shadow_rz<12) item->shadow_rz=12;
        }
        if (profile) {
            enemy_rig_sample(profile,&rig_input,&item->pose);
            if (info->ability==TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE &&
                e->special_target_active) {
                struct enemy_rig_final final;
                struct vec3 mouth={0,15,125};
                enemy_rig_finalize(profile,&item->pose,&final);
                mouth=enemy_rig_final_point(profile,&final,ER_HEAD,mouth);
                item->tongue=1;
                item->mouth_x=item->x+(item->dir_z*mouth.x+
                    item->dir_x*mouth.z)/1024;
                item->mouth_y=mouth.y+item->lift;
                item->mouth_z=item->z+(-item->dir_x*mouth.x+
                    item->dir_z*mouth.z)/1024;
                scene_independent_enemy_target(e,item);
            }
        } else if (family!=RASTERFALL_ENEMY_VISUAL_LEGACY) {
            struct rasterfall_infected_sample sample;
            int recipe=-1;
            if (rasterfall_infected_sample_motion(&next[i].infected,e->active,
                    e->x,e->z,now_us,&sample)<0) return -1;
            next[i].infected=sample.next;
            for (int j=0;j<6;++j)
                if (enemy_visual_recipes[j].family==family &&
                    enemy_visual_recipes[j].type==e->type) { recipe=j;break; }
            if (recipe>=0) {
                item->infected_recipe=recipe+1;
                item->infected_swing=sample.swing;
            } else {
                item->legacy_kind=e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY ||
                    !(i&1)?1:2;
                item->feedback=color;
                item->squash=scale;
            }
        } else {
            item->legacy_kind=e->type==TOY_GAME_ENEMY_PURSUIT_HEAVY ||
                !(i&1)?1:2;
            item->feedback=color;
            item->squash=scale;
        }
        frame.count++;
    }
    scene_enemy_capture=frame;
    memcpy(scene_independent_enemy_history,next,sizeof(next));
    scene_independent_enemy_generation=world_generation;
    scene_independent_enemy_family=enemy_visual_family;
    scene_independent_enemy_session=active_session;
    scene_independent_enemy_frame=frame.frame_id;
    return 0;
}
