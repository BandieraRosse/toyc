#include "rf_gpu_scene_local.h"
#include <string.h>

void rf_gpu_scene_local_destroyed(struct rf_gpu_scene_local_source *s)
{
    if (s) {
        s->active = 0; s->epoch = 0;
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
        s->failed || s->active) return -1;
    if (s->next_epoch == UINT64_MAX) { s->failed=1; return -1; }
    s->epoch=++s->next_epoch; s->actor_id=a->actor_id; s->active=1;
    return 0;
}
int rf_gpu_scene_local_freeze(struct rf_gpu_scene_local_source *s,
    const struct toy_game *game, const struct camera *camera,
    uint32_t width, uint32_t height, int air_walls_enabled,
    struct rf_gpu_scene_local_frame *out)
{
    struct rf_gpu_scene_actor_input_v1 input[TOY_GAME_MAX_ACTORS];
    uint32_t ordinals[TOY_GAME_MAX_ACTORS];
    struct rf_gpu_scene_local_frame next;
    struct rf_gpu_scene_identity_tracker tracker;
    uint64_t lower_time;
    int lower_walk;
    const struct toy_game_actor *a = NULL;
    int slot=-1;
    if (!s || !game || !out || s->failed || s->frame_id==UINT64_MAX) return -1;
    memset(input,0,sizeof(input)); memset(ordinals,0,sizeof(ordinals));
    memset(&next,0,sizeof(next));
    lower_time=s->lower_time_ms; lower_walk=s->lower_walk;
    if (s->active) {
        for (int i=0;i<TOY_GAME_MAX_ACTORS;++i)
            if (game->actors[i].active && game->actors[i].actor_id==s->actor_id) {
                if (a) return -1;
                a=&game->actors[i]; slot=i;
            }
        /* Missing or changed sources require an explicit owner event. */
        if (!a || a->kind!=TOY_GAME_ACTOR_AI || a->current_slot<0 ||
            a->current_slot>=TOY_GAME_WEAPON_SLOTS || a->animation.time_ms<0) return -1;
        input[slot].source=RF_GPU_SCENE_ACTOR_GAME;
        input[slot].source_id=(uint32_t)a->actor_id;
        input[slot].source_epoch=s->epoch;
        input[slot].active=input[slot].visible=1;
        input[slot].x=a->x; input[slot].y=-900+a->ground_y+a->airborne_y;
        input[slot].z=a->z; input[slot].sy=a->sy; input[slot].cy=a->cy;
        input[slot].animation_id=a->animation.id;
        input[slot].animation_time_ms=a->animation.time_ms;
        input[slot].weapon=a->slots[a->current_slot].weapon;
        next.presentation.character_id=a->character_id;
        next.presentation.state=a->state; next.presentation.moving=a->moving;
        next.presentation.ground_y=a->ground_y; next.presentation.airborne_y=a->airborne_y;
        next.presentation.pitch_sy=a->pitch_sy; next.presentation.pitch_cy=a->pitch_cy;
        next.presentation.locomotion_blend_ms=a->locomotion_blend_ms;
        next.presentation.muzzle_flash_ms=a->muzzle_flash_ms;
        if (a->character_id!=s->clock_character) { lower_time=0; lower_walk=0; }
        if (a->animation.id==TOY_GAME_ANIM_MOVE) {
            int duration=toy_game_animation_info(TOY_GAME_ANIM_MOVE)->duration_ms;
            int delta=a->animation.time_ms;
            if (lower_walk && s->last_animation==TOY_GAME_ANIM_MOVE) {
                delta-=s->last_time_ms;
                if (delta<0) delta+=duration;
            }
            if (delta<0 || duration<=0 || delta>duration ||
                lower_time>UINT64_MAX-(uint64_t)delta) return -1;
            lower_time+=(uint64_t)delta; lower_walk=1;
        } else if (a->animation.id==TOY_GAME_ANIM_IDLE || a->animation.id==TOY_GAME_ANIM_NONE) {
            lower_time=0; lower_walk=0;
        }
        next.presentation.lower_time_ms=lower_time;
        next.presentation.lower_walk=lower_walk;
    }
    tracker=s->tracker;
    if (rf_gpu_scene_snapshot_build_v2(&tracker,input,ordinals,camera,
        s->frame_id+1,s->world_generation,s->world_generation,width,height,
        air_walls_enabled,NULL,0,NULL,0,&next.snapshot)<0) return -1;
    if (a) next.presentation.identity=next.snapshot.actors[0].identity;
    s->tracker=tracker; ++s->frame_id; *out=next;
    s->lower_time_ms=lower_time; s->lower_walk=lower_walk;
    if (a) {
        s->last_animation=a->animation.id; s->last_time_ms=a->animation.time_ms;
        s->clock_character=a->character_id;
    }
    return 0;
}
