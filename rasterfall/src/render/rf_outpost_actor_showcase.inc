/* Outpost actor fixtures are presentation values, never gameplay actors. */
static const int outpost_actor_actions[] = {
    TOY_GAME_ANIM_IDLE, TOY_GAME_ANIM_MOVE, TOY_GAME_ANIM_FIRE,
    TOY_GAME_ANIM_RELOAD, TOY_GAME_ANIM_DOWNED, TOY_GAME_ANIM_HIT,
    TOY_GAME_ANIM_DEATH, TOY_GAME_ANIM_REVIVE, TOY_GAME_ANIM_SHOVE,
    TOY_GAME_ANIM_MELEE, TOY_GAME_ANIM_THROW
};
#define OUTPOST_ACTOR_ACTION_COUNT 11
static const struct {
    int character_id, action;
} outpost_humanoid_stations[] = {
    {RASTERFALL_CHARACTER_RF_RIFLEMAN, TOY_GAME_ANIM_IDLE},
    {RASTERFALL_CHARACTER_RF_RIFLEMAN, TOY_GAME_ANIM_MOVE},
    {RASTERFALL_CHARACTER_RF_RIFLEMAN, TOY_GAME_ANIM_FIRE},
    {RASTERFALL_CHARACTER_SQUAD_B_BREACHER, TOY_GAME_ANIM_IDLE},
    {RASTERFALL_CHARACTER_SQUAD_A_RECON, TOY_GAME_ANIM_MOVE},
    {RASTERFALL_CHARACTER_SQUAD_A_MEDIC, TOY_GAME_ANIM_IDLE},
    {RASTERFALL_CHARACTER_SQUAD_A_ENGINEER, TOY_GAME_ANIM_IDLE},
    {RASTERFALL_CHARACTER_SQUAD_B_HEAVY, TOY_GAME_ANIM_MOVE},
    {RASTERFALL_CHARACTER_GUNNER, TOY_GAME_ANIM_MOVE},
    {RASTERFALL_CHARACTER_GUNNER_ELITE, TOY_GAME_ANIM_FIRE}
};
#define OUTPOST_HUMANOID_STATION_COUNT \
    (sizeof(outpost_humanoid_stations)/sizeof(outpost_humanoid_stations[0]))
#define OUTPOST_ACTOR_WALK_COUNT 6
_Static_assert(sizeof(outpost_actor_actions)/sizeof(outpost_actor_actions[0]) ==
    OUTPOST_ACTOR_ACTION_COUNT,"actor action stations must match catalog");
#define OUTPOST_ACTOR_WALK_NORTH_Z 2856
#define OUTPOST_ACTOR_WALK_SOUTH_Z -2944
struct outpost_actor_walker {
    int family, level, x, z, direction;
};
static struct outpost_actor_walker outpost_actor_walkers[OUTPOST_ACTOR_WALK_COUNT];
static int outpost_actor_actions_visible, outpost_actor_walk_visible;
static uint64_t outpost_actor_actions_time_us;
static int outpost_model_lab_visible;
static int outpost_model_lab_status;
static uint64_t outpost_model_lab_time_us, outpost_model_lab_start_us;
static struct rasterfall_rifle_history outpost_actor_rifle_history[70],outpost_model_rifle_history;
static struct rasterfall_rifle_pose_input outpost_actor_rifle[70],outpost_model_rifle;
#include "rf_outpost_rifle_cycle.inc"

void rasterfall_render_set_outpost_model_lab(int visible,uint64_t time_us)
{
    if(!visible || !outpost_model_lab_visible || time_us<outpost_model_lab_start_us)
        memset(&outpost_model_rifle_history,0,sizeof(outpost_model_rifle_history));
    if (!visible && outpost_model_lab_visible && !outpost_rifle_cycle_visible)
        rf_gpu_scene_pose_body_release();
    if (!visible || !outpost_model_lab_visible) outpost_model_lab_status=0;
    if (!outpost_model_lab_visible || time_us<outpost_model_lab_start_us)
        outpost_model_lab_start_us=time_us;
    outpost_model_lab_visible=visible;
    outpost_model_lab_time_us=time_us-outpost_model_lab_start_us;
    if(visible) {
        struct toy_game_actor actor={0};
        actor.active=1;actor.actor_id=10200;actor.kind=TOY_GAME_ACTOR_AI;
        actor.state=TOY_GAME_ACTOR_ALIVE;actor.combat_target.kind=-1;actor.pitch_cy=1024;
        actor.slots[0].weapon=TOY_GAME_WEAPON_AK;actor.animation.id=TOY_GAME_ANIM_IDLE;
        rasterfall_rifle_sample(&actor,(unsigned)(outpost_model_lab_time_us/1000),
            &outpost_model_rifle_history,&outpost_model_rifle);
    }
}

int rasterfall_render_outpost_model_lab_status(void)
{ return outpost_model_lab_status; }
static uint64_t outpost_actor_walk_time_us, outpost_actor_walk_step_us;

static int outpost_actor_locomotion_time(int actor_id)
{
    uint64_t time_us=actor_id>=10064 ? outpost_actor_walk_step_us :
        outpost_actor_actions_time_us;
    return (int)((time_us/1000)%1000000);
}

void rasterfall_render_set_outpost_actor_actions(int visible,uint64_t time_us)
{
    if(!visible || !outpost_actor_actions_visible || time_us<outpost_actor_actions_time_us)
        memset(outpost_actor_rifle_history,0,64*sizeof(outpost_actor_rifle_history[0]));
    outpost_actor_actions_visible=visible;
    outpost_actor_actions_time_us=time_us;
}

void rasterfall_render_set_outpost_actor_walk(int visible,uint64_t time_us)
{
    int reset=!visible || !outpost_actor_walk_visible ||
        time_us<outpost_actor_walk_time_us ||
        time_us-outpost_actor_walk_time_us>1000000;
    if (reset) {
        memset(outpost_actor_rifle_history+64,0,6*sizeof(outpost_actor_rifle_history[0]));
        outpost_actor_walk_step_us=time_us;
        for (int i=0;i<OUTPOST_ACTOR_WALK_COUNT;++i) {
            struct outpost_actor_walker *entry=&outpost_actor_walkers[i];
            entry->family=i/3;
            entry->level=i%3;
            entry->x=-4072+i*1500;
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
            struct toy_game_actor sample={0};
            sample.class_id=entry->level;
            int step=toy_game_actor_move_step(&sample,TOY_CONFIG_AI_RETURN_SPEED);
            int next=entry->z+entry->direction*step;
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
    if (family && id>=32 && id<32+(int)OUTPOST_HUMANOID_STATION_COUNT)
        actor->character_id=outpost_humanoid_stations[id-32].character_id;
    actor->class_id=TOY_GAME_AI_LEVEL_1+level;
    rasterfall_render_lab_point(id>=64 ? "actor_walk_lab_area" : "actor_actions_lab_area",
        x,z,&actor->x,&actor->z);
    actor->cy=direction*1024;
    actor->pitch_cy=1024;
    actor->combat_target.kind=-1;
    actor->current_slot=0;
    actor->slots[0].weapon=family || id<64 ? TOY_GAME_WEAPON_AK :
        level==0 ? TOY_GAME_WEAPON_PISTOL :
        level==1 ? TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_AK;
    actor->animation.id=action;
    actor->animation.time_ms=info->duration_ms>0 ?
        (int)((time_us/1000)%(uint64_t)info->duration_ms) : 0;
    actor->moving=action==TOY_GAME_ANIM_MOVE;
}

static const struct rasterfall_rifle_pose_input *outpost_actor_rifle_input(const struct toy_game_actor *actor)
{
    int index=actor->actor_id-10000;
    uint64_t time_us=index>=64?outpost_actor_walk_step_us:outpost_actor_actions_time_us;
    rasterfall_rifle_sample(actor,(unsigned)(time_us/1000),
        &outpost_actor_rifle_history[index],&outpost_actor_rifle[index]);
    return &outpost_actor_rifle[index];
}

static void outpost_actor_block_rifle(const struct toy_game_actor *actor,
    struct rasterfall_procedural_humanoid_state *state)
{
    state->rifle=*outpost_actor_rifle_input(actor);
    state->moving=actor->moving;
    state->locomotion_time_ms=(int)(outpost_actor_locomotion_time(actor->actor_id)%60000);
    state->rifle_valid=state->weapon==TOY_GAME_WEAPON_AK &&
        (actor->animation.id==TOY_GAME_ANIM_IDLE || actor->animation.id==TOY_GAME_ANIM_MOVE ||
         actor->animation.id==TOY_GAME_ANIM_FIRE);
    state->muzzle_flash=actor->animation.id==TOY_GAME_ANIM_FIRE && actor->animation.time_ms<65;
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
            int x=-4284+(i%6)*1650;
            int z=3156-(i/6)*2100-family*4200;
            int color=RF_COLOR_AI_RIFLE;
            if (family && i>=(int)OUTPOST_HUMANOID_STATION_COUNT) continue;
            outpost_actor_fixture(&actor,family,1,family ?
                outpost_humanoid_stations[i].action : outpost_actor_actions[i],
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
                    RASTERFALL_PROFESSION_NONE,0,0,0,0,0,{0}};
                outpost_actor_block_rifle(&actor,&state);
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
                RASTERFALL_PROFESSION_NONE,0,0,0,0,0,{0}};
            outpost_actor_block_rifle(&actor,&state);
            profile.body_color=entry->level==2 ? RF_COLOR_AI_HEAVY :
                entry->level==1 ? RF_COLOR_AI_RIFLE : RF_COLOR_AI_BASIC;
            profile.leg_color=0x25354A;
            pixels+=rasterfall_render_procedural_humanoid(renderer,camera,
                &state,&profile);
        }
    }
    return pixels;
}
