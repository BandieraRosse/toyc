#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"

int rf_game_init(struct rf_game_runtime *runtime,
                 struct rf_core *core,
                 struct rasterfall_session *session,
                 const char *map_path)
{
    if (!runtime || !core || !session || !map_path) return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->core = core;
    runtime->session = session;
    if ((!strcmp(map_path, "rasterfall/assets/maps/rasterfall_legacy.map") ?
         rasterfall_session_load_legacy(session, map_path) :
         rasterfall_session_load(session, map_path)) < 0) {
        runtime->core = NULL;
        runtime->session = NULL;
        return -1;
    }
    rasterfall_effects_init(&runtime->effects);
    rasterfall_net_init(&runtime->net);
    rasterfall_net_discovery_init(&runtime->discovery);
    runtime->lifecycle_paused = 1;
    runtime->lifecycle_running = 1;
    runtime->initialized = 1;
    runtime->render_context.session = session;
    runtime->render_context.effects = &runtime->effects;
    runtime->render_context.net = &runtime->net;
    rasterfall_render_bind(&runtime->render_context);
    return 0;
}

int rf_game_render(struct rf_game_runtime *runtime,
                   struct toy_renderer *renderer,
                   struct toy_surface *surface)
{
    int pixels;
    (void)surface;
    if (!runtime || !runtime->initialized || !runtime->session || !renderer)
        return -1;
    pixels = rasterfall_render_scene(renderer, &runtime->render_camera);
    pixels += rasterfall_render_flags(renderer, &runtime->render_camera);
    pixels += rasterfall_render_enemies(renderer, &runtime->render_camera);
    pixels += rasterfall_render_ai_teammate(renderer, &runtime->render_camera);
    runtime->scene_pixels = pixels;
    return pixels;
}

int rf_game_runtime_get_status(const struct rf_game_runtime *runtime,
                               struct rf_game_runtime_status *status)
{
    const struct toy_game_actor *player;
    if (!status) return -1;
    memset(status, 0, sizeof(*status));
    if (!runtime) return -1;
    status->initialized = runtime->initialized;
    status->running = runtime->lifecycle_running;
    status->paused = runtime->lifecycle_paused;
    status->session_active = runtime->session != NULL;
    status->network_mode = runtime->net.mode;
    if (!runtime->session) return 0;
    player = rasterfall_session_local_player_const(runtime->session);
    if (!player) return 0;
    status->local_player_active = player->active;
    status->local_player_state = player->state;
    status->local_player_hp = player->hp;
    return 0;
}

void rf_game_shutdown(struct rf_game_runtime *runtime)
{
    if (!runtime || !runtime->initialized) return;
    rasterfall_net_discovery_close(&runtime->discovery);
    rasterfall_net_close(&runtime->net);
    if (runtime->session)
        rasterfall_session_unload(runtime->session);
    memset(runtime, 0, sizeof(*runtime));
}
