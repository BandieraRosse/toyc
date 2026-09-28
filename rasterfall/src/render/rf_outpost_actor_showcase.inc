/* Outpost actor fixtures are presentation values, never gameplay actors. */
static const int outpost_actor_actions[] = {
    TOY_GAME_ANIM_IDLE, TOY_GAME_ANIM_MOVE, TOY_GAME_ANIM_FIRE,
    TOY_GAME_ANIM_RELOAD, TOY_GAME_ANIM_DOWNED, TOY_GAME_ANIM_HIT,
    TOY_GAME_ANIM_DEATH, TOY_GAME_ANIM_REVIVE, TOY_GAME_ANIM_SHOVE,
    TOY_GAME_ANIM_MELEE, TOY_GAME_ANIM_THROW
};
#define OUTPOST_ACTOR_ACTION_COUNT 11
#define OUTPOST_ACTOR_WALK_COUNT 6
_Static_assert(sizeof(outpost_actor_actions)/sizeof(outpost_actor_actions[0]) ==
    OUTPOST_ACTOR_ACTION_COUNT,"actor action stations must match catalog");
#define OUTPOST_ACTOR_WALK_NORTH_Z -16600
#define OUTPOST_ACTOR_WALK_SOUTH_Z -22400
struct outpost_actor_walker {
    int family, level, x, z, direction;
};
static struct outpost_actor_walker outpost_actor_walkers[OUTPOST_ACTOR_WALK_COUNT];
static int outpost_actor_actions_visible, outpost_actor_walk_visible;
static uint64_t outpost_actor_actions_time_us;
static uint64_t outpost_actor_walk_time_us, outpost_actor_walk_step_us;

static int outpost_actor_locomotion_time(int actor_id)
{
    uint64_t time_us=actor_id>=10064 ? outpost_actor_walk_step_us :
        outpost_actor_actions_time_us;
    return (int)((time_us/1000)%1000000);
}

void rasterfall_render_set_outpost_actor_actions(int visible,uint64_t time_us)
{
    outpost_actor_actions_visible=visible;
    outpost_actor_actions_time_us=time_us;
}

void rasterfall_render_set_outpost_actor_walk(int visible,uint64_t time_us)
{
    int reset=!visible || !outpost_actor_walk_visible ||
        time_us<outpost_actor_walk_time_us ||
        time_us-outpost_actor_walk_time_us>1000000;
    if (reset) {
        outpost_actor_walk_step_us=time_us;
        for (int i=0;i<OUTPOST_ACTOR_WALK_COUNT;++i) {
            struct outpost_actor_walker *entry=&outpost_actor_walkers[i];
            entry->family=i/3;
            entry->level=i%3;
            entry->x=24600+i*1500;
            entry->z=OUTPOST_ACTOR_WALK_NORTH_Z;
            entry->direction=-1;
        }
    }
    outpost_actor_walk_visible=visible;
    outpost_actor_walk_time_us=time_us;
    if (!visible) return;
    while (time_us-outpost_actor_walk_step_us>=16000) {
        outpost_actor_walk_step_us+=16000;
        for (int i=0;i<OUTPOST_ACTOR_WALK_COUNT;++i) {
            struct outpost_actor_walker *entry=&outpost_actor_walkers[i];
            static const int speeds[]={TOY_CONFIG_AI_LEVEL_1_MOVE_SPEED,
                TOY_CONFIG_AI_LEVEL_2_MOVE_SPEED,TOY_CONFIG_AI_LEVEL_3_MOVE_SPEED};
            int next=entry->z+entry->direction*speeds[entry->level];
            if (next<OUTPOST_ACTOR_WALK_SOUTH_Z) {
                next=OUTPOST_ACTOR_WALK_SOUTH_Z+
                    (OUTPOST_ACTOR_WALK_SOUTH_Z-next);
                entry->direction=1;
            } else if (next>OUTPOST_ACTOR_WALK_NORTH_Z) {
                next=OUTPOST_ACTOR_WALK_NORTH_Z-
                    (next-OUTPOST_ACTOR_WALK_NORTH_Z);
                entry->direction=-1;
            }
            entry->z=next;
        }
    }
}

static void outpost_actor_fixture(struct toy_game_actor *actor,int family,
    int level,int action,int x,int z,int direction,uint64_t time_us,int id)
{
    const struct toy_game_animation_info *info=toy_game_animation_info(action);
    memset(actor,0,sizeof(*actor));
    actor->active=1;actor->actor_id=10000+id;
    actor->kind=TOY_GAME_ACTOR_AI;actor->state=TOY_GAME_ACTOR_ALIVE;
    actor->character_id=family ? RASTERFALL_CHARACTER_RF_RIFLEMAN :
        RASTERFALL_CHARACTER_NONE;
    actor->class_id=TOY_GAME_AI_LEVEL_1+level;
    actor->x=x;actor->z=z;actor->cy=direction*1024;
    actor->current_slot=0;
    actor->slots[0].weapon=family ? TOY_GAME_WEAPON_AK :
        level==0 ? TOY_GAME_WEAPON_PISTOL :
        level==1 ? TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_AK;
    actor->animation.id=action;
    actor->animation.time_ms=info->duration_ms>0 ?
        (int)((time_us/1000)%(uint64_t)info->duration_ms) : 0;
    actor->moving=action==TOY_GAME_ANIM_MOVE;
}

static int render_outpost_actor_lab(struct toy_renderer *renderer,
    const struct camera *camera)
{
    int pixels=0;
    if (!active_session || active_session->world_id!=RASTERFALL_WORLD_OUTPOST)
        return 0;
    for (int family=0;family<2;++family) {
        if (!outpost_actor_actions_visible) break;
        for (int i=0;i<OUTPOST_ACTOR_ACTION_COUNT;++i) {
            struct toy_game_actor actor;
            int x=12100+(i%6)*1650;
            int z=-16300-(i/6)*2100-family*4200;
            int color=RF_COLOR_AI_RIFLE;
            if (family && i>=3) continue; /* Authored Humanoid actions absent. */
            outpost_actor_fixture(&actor,family,1,outpost_actor_actions[i],
                x,z,1,outpost_actor_actions_time_us,family*32+i);
            if (family) {
                int result=render_modular_ai_teammate(renderer,camera,&actor,
                    TOY_GAME_MAX_ACTORS,NULL);
                if (result>0) pixels+=result;
            } else {
                struct rasterfall_character_profile profile=
                    *rasterfall_character_profile(RASTERFALL_CHARACTER_NONE);
                struct rasterfall_procedural_humanoid_state state={
                    actor.x,actor.z,0,actor.sy,actor.cy,actor.slots[0].weapon,
                    0,actor.animation.id==TOY_GAME_ANIM_DOWNED,
                    actor.animation.id,actor.animation.time_ms,
                    RASTERFALL_PROFESSION_NONE};
                profile.body_color=color;
                profile.leg_color=0x25354A;
                pixels+=rasterfall_render_procedural_humanoid(renderer,camera,
                    &state,&profile);
            }
        }
    }
    if (outpost_actor_walk_visible) for (int i=0;i<OUTPOST_ACTOR_WALK_COUNT;++i) {
        const struct outpost_actor_walker *entry=&outpost_actor_walkers[i];
        struct toy_game_actor actor;
        outpost_actor_fixture(&actor,entry->family,entry->level,
            TOY_GAME_ANIM_MOVE,entry->x,entry->z,entry->direction,
            outpost_actor_walk_step_us,64+i);
        if (entry->family) {
            int result=render_modular_ai_teammate(renderer,camera,&actor,
                TOY_GAME_MAX_ACTORS,NULL);
            if (result>0) pixels+=result;
        } else {
            struct rasterfall_character_profile profile=
                *rasterfall_character_profile(RASTERFALL_CHARACTER_NONE);
            struct rasterfall_procedural_humanoid_state state={
                actor.x,actor.z,0,actor.sy,actor.cy,actor.slots[0].weapon,
                0,0,actor.animation.id,actor.animation.time_ms,
                RASTERFALL_PROFESSION_NONE};
            profile.body_color=entry->level==2 ? RF_COLOR_AI_HEAVY :
                entry->level==1 ? RF_COLOR_AI_RIFLE : RF_COLOR_AI_BASIC;
            profile.leg_color=0x25354A;
            pixels+=rasterfall_render_procedural_humanoid(renderer,camera,
                &state,&profile);
        }
    }
    return pixels;
}
