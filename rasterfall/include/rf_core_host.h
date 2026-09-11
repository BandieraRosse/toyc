#ifndef RASTERFALL_RF_CORE_HOST_H
#define RASTERFALL_RF_CORE_HOST_H

#include "toy_audio.h"
#include "toy_input.h"
#include "toy_renderer.h"
#include "toy_window.h"

/* Internal V0 host.  The game may borrow the objects through the accessors,
 * but does not own their lifetime. */
struct rf_core {
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input *input;
    struct toy_surface surface;
    struct toy_renderer *renderer;
    struct toy_audio audio;
    int audio_ready;
};

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer);
int rf_core_poll_events(struct rf_core *core);
int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms);
int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color);
int rf_core_end_frame(struct rf_core *core);
void rf_core_shutdown(struct rf_core *core);

struct toy_window *rf_core_window(struct rf_core *core);
struct toy_window_events *rf_core_events(struct rf_core *core);
struct toy_input *rf_core_input(struct rf_core *core);
struct toy_surface *rf_core_surface(struct rf_core *core);
struct toy_renderer *rf_core_renderer(struct rf_core *core);
struct toy_audio *rf_core_audio(struct rf_core *core);
int rf_core_audio_ready(const struct rf_core *core);
int rf_core_set_pointer_lock(struct rf_core *core, int locked);
int rf_core_move_window(struct rf_core *core, uint32_t serial);
void rf_core_reset_input(struct rf_core *core);

#endif
