#include "core.h"
#include "tlibc_everything.h"
#include "rf_core_host.h"

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer)
{
    if (!core || !input || !renderer) return -1;
    memset(core, 0, sizeof(*core));
    core->input = input;
    core->renderer = renderer;
    toy_input_init(core->input);
    toy_renderer_init(core->renderer);
    core->window = toy_window_open(title, width, height);
    if (!core->window) {
        toy_renderer_destroy(core->renderer);
        return -1;
    }
    /* Audio is optional, matching the existing Rasterfall startup policy. */
    if (toy_audio_open(&core->audio, 48000, 2) == 0)
        core->audio_ready = 1;
    return 0;
}

int rf_core_poll_events(struct rf_core *core)
{
    return rf_core_poll_events_timeout(core, 0);
}

int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms)
{
    if (!core || !core->window) return -1;
    toy_input_begin_frame(core->input);
    if (toy_window_poll(core->window, &core->events, timeout_ms) < 0) return -1;
    toy_input_apply(core->input, &core->events);
    return 0;
}

int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color)
{
    int ready;
    if (!core || !core->window) return -1;
    ready = toy_window_begin_frame(core->window, &core->surface);
    if (ready <= 0) return ready;
    if (toy_renderer_begin(core->renderer, &core->surface, clear_color) < 0)
        return -1;
    return ready;
}

int rf_core_end_frame(struct rf_core *core)
{
    if (!core || !core->window) return -1;
    return toy_window_present(core->window);
}

void rf_core_shutdown(struct rf_core *core)
{
    if (!core) return;
    if (core->audio_ready) toy_audio_close(&core->audio);
    if (core->window) toy_window_close(core->window);
    toy_renderer_destroy(core->renderer);
    memset(core, 0, sizeof(*core));
}

struct toy_window *rf_core_window(struct rf_core *core) { return core ? core->window : NULL; }
struct toy_window_events *rf_core_events(struct rf_core *core) { return core ? &core->events : NULL; }
struct toy_input *rf_core_input(struct rf_core *core) { return core ? core->input : NULL; }
struct toy_surface *rf_core_surface(struct rf_core *core) { return core ? &core->surface : NULL; }
struct toy_renderer *rf_core_renderer(struct rf_core *core) { return core ? core->renderer : NULL; }
struct toy_audio *rf_core_audio(struct rf_core *core) { return core ? &core->audio : NULL; }
int rf_core_audio_ready(const struct rf_core *core) { return core && core->audio_ready; }
int rf_core_set_pointer_lock(struct rf_core *core, int locked)
{
    return core && core->window ? toy_window_set_pointer_lock(core->window, locked) : -1;
}
int rf_core_move_window(struct rf_core *core, uint32_t serial)
{
    return core && core->window ? toy_window_move(core->window, serial) : -1;
}
void rf_core_reset_input(struct rf_core *core)
{
    if (!core || !core->input) return;
    memset(core->input, 0, sizeof(*core->input));
    toy_input_init(core->input);
}
