#include "tlibc_everything.h"
#include "string.h"
#include "rasterfall_ai.h"

static int registry_find(const struct rasterfall_ai_registry *registry,
                         int actor_index)
{
    int i;
    if (!registry) return -1;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (registry->agents[i].active &&
            registry->agents[i].actor_index == actor_index) return i;
    return -1;
}

void rasterfall_ai_registry_init(struct rasterfall_ai_registry *registry)
{
    if (!registry) return;
    memset(registry, 0, sizeof(*registry));
}

int rasterfall_ai_registry_add(struct rasterfall_ai_registry *registry,
                               int actor_index, int controller,
                               int think_interval_ms,
                               int policy)
{
    int i;
    if (!registry || actor_index < TOY_GAME_PLAYER_ACTOR_INDEX ||
        actor_index >= TOY_GAME_MAX_ACTORS ||
        (controller != RASTERFALL_AI_CONTROLLER_TEAMMATE &&
         controller != RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER) ||
        (actor_index == TOY_GAME_PLAYER_ACTOR_INDEX &&
         controller != RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER)) return -1;
    i = registry_find(registry, actor_index);
    if (i < 0) {
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
            if (!registry->agents[i].active) break;
        if (i >= TOY_GAME_MAX_ACTORS) return -1;
        registry->count++;
    }
    registry->agents[i].active = 1;
    registry->agents[i].controller = controller;
    registry->agents[i].actor_index = actor_index;
    registry->agents[i].think_interval_ms = think_interval_ms > 0 ?
        think_interval_ms : 100;
    registry->agents[i].think_elapsed_ms = 0;
    registry->agents[i].policy = policy;
    toy_game_ai_decision_clear(&registry->agents[i].decision);
    return i;
}

void rasterfall_ai_registry_remove(struct rasterfall_ai_registry *registry,
                                   int actor_index)
{
    int i = registry_find(registry, actor_index);
    if (i < 0) return;
    memset(&registry->agents[i], 0, sizeof(registry->agents[i]));
    if (registry->count > 0) registry->count--;
}

void rasterfall_ai_registry_sync(struct rasterfall_ai_registry *registry,
                                 const struct toy_game *game)
{
    int i;
    if (!registry || !game) return;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game->actors[i];
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->developer_only) {
            rasterfall_ai_registry_remove(registry, i);
            continue;
        }
        if (registry_find(registry, i) < 0)
            rasterfall_ai_registry_add(registry, i,
                RASTERFALL_AI_CONTROLLER_TEAMMATE, 100,
                RASTERFALL_AI_POLICY_IDLE);
    }
}

void rasterfall_ai_registry_think(struct rasterfall_ai_registry *registry,
                                  const struct toy_game *game, int dt_ms)
{
    int i;
    if (!registry || !game || dt_ms < 0) return;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct rasterfall_ai_agent *agent = &registry->agents[i];
        if (!agent->active) continue;
        agent->think_elapsed_ms += dt_ms;
        if (agent->think_elapsed_ms < agent->think_interval_ms) continue;
        agent->think_elapsed_ms %= agent->think_interval_ms;
        if (!toy_game_ai_observe(game, agent->actor_index,
                                 &agent->observation)) {
            agent->active = 0;
            if (registry->count > 0) registry->count--;
            continue;
        }
        toy_game_ai_decision_clear(&agent->decision);
        /* Policy dispatch is intentionally centralized.  Only the no-op
         * policy exists until a concrete single-player strategy is added. */
        switch (agent->policy) {
        case RASTERFALL_AI_POLICY_IDLE:
        default:
            toy_game_ai_decision_clear(&agent->decision);
            break;
        }
    }
}
