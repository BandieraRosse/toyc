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
    if (rasterfall_session_load(session, map_path) < 0) {
        runtime->core = NULL;
        runtime->session = NULL;
        return -1;
    }
    rasterfall_effects_init(&runtime->effects);
    rf_gui_init(&runtime->gui);
    rf_app_manager_init(&runtime->app_manager, &runtime->gui);
    rf_gui_set_app_manager(&runtime->gui, &runtime->app_manager);
    if (rf_app_manager_register_defaults(&runtime->app_manager) < 0) {
        runtime->core = NULL;
        runtime->session = NULL;
        return -1;
    }
    rf_application_query_init(&runtime->application_query, core, runtime, NULL);
    rf_app_manager_set_query_context(&runtime->app_manager,
                                     &runtime->application_query);
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
