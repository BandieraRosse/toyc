#include "rf_gpu_scene_local.h"
#include <string.h>
#include "tlibc_everything.h"
#include "rasterfall_world_light.h"

void rf_gpu_scene_local_destroyed(struct rf_gpu_scene_local_source *s)
{
    if (s) {
        s->active = 0; s->epoch = 0;
        s->actor_count=0;
        memset(s->actors,0,sizeof(s->actors));
        s->lower_time_ms=0; s->lower_walk=0;
        s->last_animation=0; s->last_time_ms=0; s->clock_character=0;
    }
}
int rf_gpu_scene_local_world(struct rf_gpu_scene_local_source *s)
{
    if (!s || s->failed) return -1;
    rf_gpu_scene_local_destroyed(s);
    if (s->world_generation == UINT64_MAX) { s->failed=1; return -1; }
    ++s->world_generation;
    return 0;
}
int rf_gpu_scene_local_created(struct rf_gpu_scene_local_source *s,
                               const struct toy_game_actor *a)
{
    if (!s || !a || !a->active || a->actor_id < 0 || !s->world_generation ||
        s->failed || s->actor_count>=TOY_GAME_MAX_ACTORS) return -1;
    for(uint32_t i=0;i<s->actor_count;++i)
        if (s->actors[i].actor_id==a->actor_id) return -1;
    if (s->next_epoch == UINT64_MAX) { s->failed=1; return -1; }
    struct rf_gpu_scene_local_actor_source *entry=&s->actors[s->actor_count++];
    entry->epoch=++s->next_epoch; entry->actor_id=a->actor_id;
    if (!s->active) {
        s->epoch=entry->epoch; s->actor_id=entry->actor_id; s->active=1;
    }
    return 0;
}
int rf_gpu_scene_local_freeze_world_lit(struct rf_gpu_scene_local_source *s,
    const struct toy_game *game, const struct camera *camera,
    uint32_t width, uint32_t height, int air_walls_enabled,
    const struct rf_gpu_scene_world_input_v2 *world,uint32_t world_count,
    const struct rasterfall_world_lighting *lighting,
    struct rf_gpu_scene_local_frame *out)
{
    struct rf_gpu_scene_actor_input_v1 input[TOY_GAME_MAX_ACTORS];
    uint32_t ordinals[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_local_frame next;
    struct rf_gpu_scene_identity_tracker tracker;
    struct rf_gpu_scene_local_actor_source states[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_local_presentation by_slot[TOY_GAME_MAX_ACTORS];
    if (!s || !game || !out || s->failed || s->frame_id==UINT64_MAX ||
        s->actor_count>TOY_GAME_MAX_ACTORS ||
        world_count>RF_GPU_SCENE_MAX_WORLD_V2 || (world_count && !world)) return -1;
    memset(input,0,sizeof(input)); memset(ordinals,0,sizeof(ordinals));
    memset(by_slot,0,sizeof(by_slot));
    memset(&next,0,sizeof(next));
    memcpy(states,s->actors,sizeof(states));
    if (s->active!=(s->actor_count!=0)) return -1;
    for(uint32_t source=0;source<s->actor_count;++source) {
        struct rf_gpu_scene_local_actor_source *state=&states[source];
        struct rf_gpu_scene_local_presentation *p;
        const struct toy_game_actor *a=NULL;
        int slot=-1;
        for (int i=0;i<TOY_GAME_MAX_ACTORS;++i)
            if (game->actors[i].active && game->actors[i].actor_id==state->actor_id) {
                if (a) return -1;
                a=&game->actors[i]; slot=i;
            }
        /* Missing or changed sources require an explicit owner event. */
        if (!a || a->kind!=TOY_GAME_ACTOR_AI || a->current_slot<0 ||
            a->current_slot>=TOY_GAME_WEAPON_SLOTS || a->animation.time_ms<0) return -1;
        input[slot].source=RF_GPU_SCENE_ACTOR_GAME;
        ordinals[slot]=world_count+(uint32_t)slot;
        input[slot].source_id=(uint32_t)a->actor_id;
        input[slot].source_epoch=state->epoch;
        input[slot].active=input[slot].visible=1;
        input[slot].x=a->x; input[slot].y=-900+a->ground_y+a->airborne_y;
        input[slot].z=a->z; input[slot].sy=a->sy; input[slot].cy=a->cy;
        input[slot].animation_id=a->animation.id;
        input[slot].animation_time_ms=a->animation.time_ms;
        input[slot].weapon=a->slots[a->current_slot].weapon;
        p=&by_slot[slot];
        p->character_id=a->character_id;
        p->state=a->state; p->moving=a->moving;
        p->ground_y=a->ground_y; p->airborne_y=a->airborne_y;
        p->pitch_sy=a->pitch_sy; p->pitch_cy=a->pitch_cy;
        p->locomotion_blend_ms=a->locomotion_blend_ms;
        p->muzzle_flash_ms=a->muzzle_flash_ms;
        p->scene_light_q8=lighting ?
            rasterfall_world_light_v2_q8(rasterfall_world_light_at(lighting,
                a->x,-900+a->ground_y,a->z)) : 256;
        if (a->character_id!=state->clock_character) {
            state->lower_time_ms=0; state->lower_walk=0;
        }
        if (a->animation.id==TOY_GAME_ANIM_MOVE) {
            int duration=toy_game_animation_info(TOY_GAME_ANIM_MOVE)->duration_ms;
            int delta=a->animation.time_ms;
            if (state->lower_walk && state->last_animation==TOY_GAME_ANIM_MOVE) {
                delta-=state->last_time_ms;
                if (delta<0) delta+=duration;
            }
            if (delta<0 || duration<=0 || delta>duration ||
                state->lower_time_ms>UINT64_MAX-(uint64_t)delta) return -1;
            state->lower_time_ms+=(uint64_t)delta; state->lower_walk=1;
        } else if (a->animation.id==TOY_GAME_ANIM_IDLE || a->animation.id==TOY_GAME_ANIM_NONE) {
            state->lower_time_ms=0; state->lower_walk=0;
        }
        p->lower_time_ms=state->lower_time_ms;
        p->lower_walk=state->lower_walk;
        state->last_animation=a->animation.id;
        state->last_time_ms=a->animation.time_ms;
        state->clock_character=a->character_id;
    }
    tracker=s->tracker;
    if (rf_gpu_scene_snapshot_build_v2(&tracker,input,ordinals,camera,
        s->frame_id+1,s->world_generation,s->world_generation,width,height,
        air_walls_enabled,world,world_count,NULL,0,&next.snapshot)<0) return -1;
    if (next.snapshot.actor_count!=s->actor_count) return -1;
    for(uint32_t i=0;i<next.snapshot.actor_count;++i) {
        int slot=(int)next.snapshot.actors[i].source_slot;
        if (slot<0 || slot>=TOY_GAME_MAX_ACTORS || !input[slot].active) return -1;
        next.presentations[i]=by_slot[slot];
        next.presentations[i].identity=next.snapshot.actors[i].identity;
    }
    if (next.snapshot.actor_count) next.presentation=next.presentations[0];
    s->tracker=tracker; ++s->frame_id; *out=next;
    memcpy(s->actors,states,sizeof(states));
    if (s->actor_count) {
        s->lower_time_ms=s->actors[0].lower_time_ms;
        s->lower_walk=s->actors[0].lower_walk;
        s->last_animation=s->actors[0].last_animation;
        s->last_time_ms=s->actors[0].last_time_ms;
        s->clock_character=s->actors[0].clock_character;
    }
    return 0;
}
int rf_gpu_scene_local_freeze_world(struct rf_gpu_scene_local_source *s,
    const struct toy_game *game, const struct camera *camera,
    uint32_t width, uint32_t height, int air_walls_enabled,
    const struct rf_gpu_scene_world_input_v2 *world,uint32_t world_count,
    struct rf_gpu_scene_local_frame *out)
{
    return rf_gpu_scene_local_freeze_world_lit(s,game,camera,width,height,
        air_walls_enabled,world,world_count,NULL,out);
}
int rf_gpu_scene_local_freeze(struct rf_gpu_scene_local_source *s,
    const struct toy_game *game, const struct camera *camera,
    uint32_t width, uint32_t height, int air_walls_enabled,
    struct rf_gpu_scene_local_frame *out)
{
    return rf_gpu_scene_local_freeze_world(s,game,camera,width,height,
        air_walls_enabled,NULL,0,out);
}
