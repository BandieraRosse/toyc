#ifndef RASTERFALL_HUD_H
#define RASTERFALL_HUD_H

#include "toy_renderer.h"
#include "toy_game.h"
#include "toy_map.h"
#include "rasterfall_map.h"
#include "rasterfall_net.h"
#include "rasterfall_animation_composition.h"
#include "rasterfall_calibration.h"
#include "rf_player_ui.h"

struct rasterfall_hud_state {
    const struct rf_player_ui_state *player_ui;
    struct rf_player_ui_view player_ui_view;
    const struct toy_game *game;
    const struct toy_map *map;
    const struct toy_game_box *safe_rooms;
    const char *player_name;
    const struct rasterfall_interactable *interactables;
    int interactable_count;
    int highlighted;
    int air_walls_enabled;
    int manual_alarm_enabled;
    int manual_alarm_timer_ms;
    int ai_revive_active;
    int ai_revive_available;
    int ai_revive_progress_ms;
    const char *ai_revive_name;
    int player_revive_active;
    int player_revive_available;
    int player_revive_progress_ms;
    const char *player_revive_name;
    int horde_banner_ms;
    const char *interaction_banner;
    int interaction_banner_success;
    const struct rasterfall_net *net;
    const char *host_address;
    int host_port;
    int flag_count;
    int flag_near;
    int flag_carried;
    int flag_colors[8];
    int pose_debug_active, pose_debug_bone, pose_debug_axis, pose_debug_layer;
    const struct rasterfall_rifle_pose *rifle_pose, *hit_pose;
    const struct rasterfall_calibration_state *pose_editor;
};

#include "rasterfall_canvas.h"
#define RASTERFALL_HUD_WAIT_FOR_RESCUE "WAIT FOR RESCUE"
/* Authoritative raw panel/text geometry, clipped by the canvas at paint. */
struct rasterfall_hud_wait_panel {
    struct rf_ui_rect panel;
    int text_x,text_y;
};
void rasterfall_hud_wait_for_rescue_geometry(int width,int height,
    struct rasterfall_hud_wait_panel *out);
void rasterfall_hud_player_status(struct rasterfall_canvas *,const struct toy_game *,
    const char *player_name,int x,int y);
void rasterfall_hud_layout(struct rasterfall_canvas *,int,
    const struct rasterfall_hud_state *);
void rasterfall_hud_prompt_layout(struct rasterfall_canvas *,
    const struct rasterfall_hud_state *);
void rasterfall_hud_draw_interact_prompt(struct toy_renderer *renderer,
                                         const struct rasterfall_hud_state *state);
void rasterfall_hud_render(struct toy_surface *surface, int fps,
                           const struct rasterfall_hud_state *state);
int rasterfall_hud_dump_frame(const char *path, const struct toy_surface *surface);
int rasterfall_hud_dump_bmp(const char *path, const struct toy_surface *surface);
void rasterfall_hud_damage_flash(struct toy_surface *surface,
                                 const struct toy_game *game);

#endif
