#include "tlibc_everything.h"
#include "rf_game_lifecycle.h"

int rf_game_init(struct rf_game_runtime *runtime,
                 struct rasterfall_session *session,
                 const char *map_path)
{
    if (!runtime || !session || !map_path) return -1;
    memset(runtime, 0, sizeof(*runtime));
    runtime->session = session;
    if (rasterfall_session_load(session, map_path) < 0) {
        runtime->session = NULL;
        return -1;
    }
    rasterfall_effects_init(&runtime->effects);
    rasterfall_net_init(&runtime->net);
    rasterfall_net_discovery_init(&runtime->discovery);
    runtime->paused = 1;
    runtime->running = 1;
    runtime->initialized = 1;
    runtime->render_context.session = session;
    runtime->render_context.effects = &runtime->effects;
    runtime->render_context.net = &runtime->net;
    rasterfall_render_bind(&runtime->render_context);
    return 0;
}

int rf_game_update(struct rf_game_runtime *runtime,
                   const struct rasterfall_command *command,
                   int dt_ms)
{
    if (!runtime || !runtime->initialized || !runtime->session) return -1;
    if (dt_ms < 0) dt_ms = 0;
    if (dt_ms > 250) dt_ms = 250;
    if (!runtime->paused && command) {
        runtime->command = *command;
        if (runtime->net.mode == RASTERFALL_NET_CLIENT)
            rasterfall_session_step_client(runtime->session, &runtime->camera,
                                            &runtime->command, dt_ms);
        else
            rasterfall_session_step(runtime->session, &runtime->camera,
                                     &runtime->command, dt_ms);
    }
    rasterfall_effects_update(&runtime->effects, dt_ms);
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

void rf_game_shutdown(struct rf_game_runtime *runtime)
{
    if (!runtime || !runtime->initialized) return;
    rasterfall_net_discovery_close(&runtime->discovery);
    rasterfall_net_close(&runtime->net);
    if (runtime->session)
        rasterfall_session_unload(runtime->session);
    memset(runtime, 0, sizeof(*runtime));
}
