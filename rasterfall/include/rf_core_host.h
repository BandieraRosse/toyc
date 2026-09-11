#ifndef RASTERFALL_RF_CORE_HOST_H
#define RASTERFALL_RF_CORE_HOST_H

#include "toy_audio.h"
#include "toy_input.h"
#include "toy_renderer.h"
#include "toy_window.h"
#include "rf_core_filesystem.h"
#include "rf_core_input.h"

/* The single V0 Core context.  The game may borrow the objects through the
 * accessors, but does not own their lifetime. */
struct rf_core {
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input *input;
    struct toy_surface surface;
    struct toy_renderer *renderer;
    struct rf_core_filesystem filesystem;
    struct toy_audio audio;
    int audio_ready;
    int exit_requested;
    int initialized;
};

/* Public, read-only snapshot of Core availability.  The snapshot deliberately
 * contains no service pointers or implementation-owned objects. */
#define RF_CORE_VERSION "0.2"
#define RF_CORE_BUILD "RF Core Runtime V0.2"

struct rf_core_status {
    const char *version;
    const char *build;
    int initialized;
    int window_ready;
    int renderer_ready;
    int filesystem_ready;
    int audio_ready;
    int clock_ready;
};

/* Compatibility name for the future public context spelling.  This is an
 * alias, not a second ownership container. */
typedef struct rf_core rf_core_context;

struct rf_core_config {
    const char *title;
    int width;
    int height;
    struct toy_input *input;
    struct toy_renderer *renderer;
};

int rf_core_init(struct rf_core *core, const char *title, int width, int height,
                 struct toy_input *input, struct toy_renderer *renderer);
int rf_core_init_config(struct rf_core *core,
                        const struct rf_core_config *config);
int rf_core_init_headless(struct rf_core *core, struct toy_input *input,
                          struct toy_renderer *renderer);
int rf_core_poll_events(struct rf_core *core);
int rf_core_poll_events_timeout(struct rf_core *core, int timeout_ms);
int64_t rf_core_begin_tick(struct rf_core *core);
int rf_core_should_exit(const struct rf_core *core);
int rf_core_begin_frame(struct rf_core *core, uint32_t clear_color);
/* Core-owned submission point for layered rendering within one frame. */
int rf_core_flush(struct rf_core *core);
int rf_core_end_frame(struct rf_core *core);
void rf_core_shutdown(struct rf_core *core);
int64_t rf_core_time_us(struct rf_core *core);
/* Clock service entry for pre-host diagnostics that have no Core instance. */
int64_t rf_core_clock_now_us(void);
int rf_core_get_input_frame(const struct rf_core *core,
                            struct rf_input_frame *frame);
int rf_core_get_status(const struct rf_core *core,
                       struct rf_core_status *status);

struct toy_window *rf_core_window(struct rf_core *core);
struct toy_window_events *rf_core_events(struct rf_core *core);
struct toy_input *rf_core_input(struct rf_core *core);
struct toy_surface *rf_core_surface(struct rf_core *core);
struct toy_renderer *rf_core_renderer(struct rf_core *core);
struct rf_core_filesystem *rf_core_filesystem_service(struct rf_core *core);
struct toy_audio *rf_core_audio(struct rf_core *core);
int rf_core_audio_ready(const struct rf_core *core);
int rf_core_set_pointer_lock(struct rf_core *core, int locked);
int rf_core_move_window(struct rf_core *core, uint32_t serial);
void rf_core_reset_input(struct rf_core *core);

#endif
