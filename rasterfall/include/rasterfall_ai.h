#ifndef RASTERFALL_AI_H
#define RASTERFALL_AI_H

#include "toy_game.h"

/* Controller ownership is deliberately separate from toy_game_actor.kind:
 * both ordinary teammates and a future managed player are game actors, but
 * they may use different policies while sharing the same executor. */
enum rasterfall_ai_controller {
    RASTERFALL_AI_CONTROLLER_TEAMMATE = 1,
    RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER = 2
};

enum rasterfall_ai_policy {
    RASTERFALL_AI_POLICY_IDLE = 0
};

struct rasterfall_ai_agent {
    int active;
    int controller;
    int actor_index;
    int think_interval_ms;
    int think_elapsed_ms;
    int policy;
    struct toy_game_ai_observation observation;
    struct toy_game_ai_decision decision;
};

struct rasterfall_ai_registry {
    struct rasterfall_ai_agent agents[TOY_GAME_MAX_ACTORS];
    int count;
};

void rasterfall_ai_registry_init(struct rasterfall_ai_registry *registry);
int rasterfall_ai_registry_add(struct rasterfall_ai_registry *registry,
                               int actor_index, int controller,
                               int think_interval_ms,
                               int policy);
void rasterfall_ai_registry_remove(struct rasterfall_ai_registry *registry,
                                   int actor_index);
void rasterfall_ai_registry_sync(struct rasterfall_ai_registry *registry,
                                 const struct toy_game *game);
void rasterfall_ai_registry_think(struct rasterfall_ai_registry *registry,
                                  const struct toy_game *game, int dt_ms);

#endif
