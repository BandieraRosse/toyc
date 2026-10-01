#ifndef RASTERFALL_MOTION_PRESENTATION_H
#define RASTERFALL_MOTION_PRESENTATION_H

#include "toy_game.h"

/* Completed simulation states.  Only visual transforms are retained; game
 * rules, collision and network authority continue to use toy_game directly. */
struct rasterfall_motion_point {
    int active, identity, aux_identity;
    int x, z, ground_y, airborne_y;
    int sy, cy;
    int time_ms;
};

struct rasterfall_motion_snapshot {
    struct rasterfall_motion_point actors[TOY_GAME_MAX_ACTORS];
    struct rasterfall_motion_point enemies[TOY_GAME_MAX_ENEMIES];
    struct rasterfall_motion_point projectiles[TOY_GAME_MAX_PROJECTILES];
};

static inline void rasterfall_motion_capture(
    struct rasterfall_motion_snapshot *out, const struct toy_game *game)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; ++i) {
        const struct toy_game_actor *actor = &game->actors[i];
        struct rasterfall_motion_point *point = &out->actors[i];
        point->active = actor->active;
        point->identity = actor->actor_id;
        point->aux_identity = actor->animation.id;
        point->x = actor->x; point->z = actor->z;
        point->ground_y = actor->ground_y;
        point->airborne_y = actor->airborne_y;
        point->sy = actor->sy; point->cy = actor->cy;
        point->time_ms = actor->animation.time_ms;
    }
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; ++i) {
        const struct toy_game_enemy *enemy = &game->enemies[i];
        struct rasterfall_motion_point *point = &out->enemies[i];
        point->active = enemy->active;
        point->identity = enemy->type;
        point->x = enemy->x; point->z = enemy->z;
        point->ground_y = enemy->ground_y;
        point->airborne_y = enemy->airborne_y;
        point->sy = enemy->dir_x; point->cy = enemy->dir_z;
        point->time_ms = enemy->dying_ms;
    }
    for (i = 0; i < TOY_GAME_MAX_PROJECTILES; ++i) {
        const struct toy_game_projectile *projectile = &game->projectiles[i];
        struct rasterfall_motion_point *point = &out->projectiles[i];
        point->active = projectile->active;
        point->identity = projectile->kind;
        point->aux_identity = projectile->owner_actor_id;
        point->x = projectile->x; point->z = projectile->z;
        point->ground_y = projectile->y;
        point->time_ms = projectile->age_ms;
    }
}

#endif
