#ifndef RASTERFALL_RF_GAME_LIFECYCLE_H
#define RASTERFALL_RF_GAME_LIFECYCLE_H

#include "rasterfall_session.h"
#include "rasterfall_effects.h"
#include "rasterfall_net.h"
#include "rasterfall_net_transport.h"
#include "rasterfall_perf.h"
#include "rasterfall_console.h"
#include "rasterfall_audio.h"
#include "rasterfall_render.h"
#include "rf_core_input.h"

struct rasterfall_options;
struct rf_core;

struct rf_game_config {
    const struct rasterfall_options *options;
    const char *map_path;
};

/* Query-only runtime summary for future station/terminal consumers. */
struct rf_game_runtime_status {
    int initialized;
    int running;
    int paused;
    int session_active;
    int network_mode;
    int local_player_active;
    int local_player_state;
    int local_player_hp;
};

struct rf_game_runtime {
    /* Gameplay/session ownership stays below this object.  The runtime owns
     * the presentation and frame-loop state which used to be implicit in
     * rasterfall.c. */
    struct rasterfall_session *session;
    /* Borrowed Core context; Core owns the referenced services. */
    struct rf_core *core;
    struct rasterfall_render_context render_context;
    struct rasterfall_effects effects;
    struct rasterfall_net net;
    struct rasterfall_net_discovery discovery;
    struct rasterfall_audio audio;
    struct rasterfall_perf_stats stats;
    struct rasterfall_perf_stats stats_total;
    struct rasterfall_console console;
    struct camera camera;
    struct camera render_camera;
    struct rasterfall_command command;
    unsigned char pending_key_edges[TOY_INPUT_KEY_COUNT];
    int pointer_turn_pending;
    int pointer_pitch_pending;
    int fire_edge;
    int shove_edge;
    int lifecycle_paused;
    int lifecycle_running;
    int coordinate_axes;
    int display_fps;
    int fps_window_frames;
    int rendered_frames;
    int scene_pixels;
    int menu_selected;
    int menu_nav_ready;
    int managed_spectator;
    int managed_third_person;
    int input_event_count;
    unsigned int last_key;
    int last_key_pressed;
    int have_last_key;
    int pointer_lock_requested;
    int have_pointer_position;
    int last_pointer_x;
    int last_pointer_y;
    int64_t accumulator;
    int64_t last_time;
    int64_t previous_begin;
    int64_t frame_start;
    char host_address[16];
    char selected_address[64];
    uint64_t seed;
    int initialized;
};

int rf_game_init(struct rf_game_runtime *runtime,
                 struct rf_core *core,
                 struct rasterfall_session *session,
                 const char *map_path);
int rf_game_update(struct rf_game_runtime *runtime,
                   const struct rasterfall_command *command,
                   int dt_ms);
int rf_game_render(struct rf_game_runtime *runtime,
                   struct toy_renderer *renderer,
                   struct toy_surface *surface);
int rf_game_runtime_get_status(const struct rf_game_runtime *runtime,
                               struct rf_game_runtime_status *status);
void rf_game_shutdown(struct rf_game_runtime *runtime);
int rf_game_runtime_run(const struct rf_game_config *config);

#endif
