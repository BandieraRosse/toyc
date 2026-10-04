#ifndef RF_PLAYER_UI_H
#define RF_PLAYER_UI_H

#include "rf_minimap.h"
#include "rf_rts.h"

struct rf_input_bindings;
struct rf_story;

#define RF_UI_SUBTITLE_CAP 4
#define RF_UI_SUBTITLE_FADE_MS 220
struct rf_ui_subtitle {
    int node_id, choice, remaining_ms, speaking;
    unsigned revision;
};

enum rf_player_ui_mode {
    RF_PLAYER_UI_PLAYER, RF_PLAYER_UI_TERMINAL, RF_PLAYER_UI_EXPERIMENT,
    RF_PLAYER_UI_LEGACY, RF_PLAYER_UI_MODE_COUNT
};
struct rf_ui_rect { int x, y, w, h; };
enum rf_player_ui_hit { RF_PLAYER_UI_HIT_NONE, RF_PLAYER_UI_HIT_STOP,
    RF_PLAYER_UI_HIT_FOLLOW, RF_PLAYER_UI_HIT_FPS, RF_PLAYER_UI_HIT_DOCK,
    RF_PLAYER_UI_HIT_PAGE_PREV, RF_PLAYER_UI_HIT_PAGE_NEXT,
    RF_PLAYER_UI_HIT_GROUP_BASE=20, RF_PLAYER_UI_HIT_UNIT_BASE=40 };
struct rf_ui_theme {
    unsigned int panel, panel_raised, border, text, muted, accent;
    unsigned int success, warning, danger, selected;
    int panel_alpha;
};
/* All dimensions are 720p design pixels, independent of gameplay data.
 * A future theme/editor can replace this descriptor without changing HUD code. */
struct rf_ui_layout_config {
    int margin, gap, padding, map_size;
    int vital_width, vital_height, weapon_width, weapon_height;
    int rts_height, objective_width, phase_width, resource_width;
};
struct rf_ui_layout {
    int scale_milli, text_scale_milli, margin, gap, padding;
    struct rf_ui_rect map, objective, vitals, weapon, phase, resources;
    struct rf_ui_rect selection, commands, hints, dock_toggle;
    struct rf_ui_rect groups, portrait, video;
};
struct rf_player_ui_state {
    int mode, scale_percent, rts_collapsed;
    struct rf_ui_theme theme;
    struct rf_ui_layout_config layout;
    struct rf_minimap_state minimap;
    struct rf_ui_subtitle subtitles[RF_UI_SUBTITLE_CAP];
    int subtitle_count;
    unsigned subtitle_history_revision;
};
/* Synchronous borrowed projection; never retained by an asynchronous GPU slot. */
struct rf_player_ui_view {
    const struct rf_input_bindings *bindings;
    const char *region_name, *objective_title, *objective_detail;
    char mission_title[128], mission_detail[128];
    int rts_active, rts_selected, rts_follow_player, comms_visible, hints, modal, phase_visible;
    int rts_move_active, rts_move_x, rts_move_z;
    int camera_x, camera_z, camera_sy, camera_cy;
    int map_sight_count, map_sight_x[4], map_sight_z[4];
    int objective_active, objective_x, objective_y, objective_z;
    const char *interaction_title, *interaction_action;
    int interaction_distance_rfu;
    const char *command_feedback;
    const struct rf_rts_state *rts;
    const struct camera *rts_camera;
};
struct rasterfall_hud_state;
void rf_player_ui_init(struct rf_player_ui_state *state);
void rf_player_subtitles_update(struct rf_player_ui_state *state,
    const struct rf_story *story,int dt_ms);
void rf_player_ui_prepare(struct rf_player_ui_state *state,
                          const struct toy_map *map, const struct toy_game *game,
                          unsigned int map_generation);
const char *rf_player_ui_mode_name(int mode);
void rf_ui_layout_resolve(struct rf_ui_layout *out,
                          const struct rf_player_ui_state *state,
                          int width, int height, int rts_active);
void rf_player_ui_map_view(struct rf_minimap_view *out,
                           const struct rf_player_ui_state *state,
                           const struct rf_player_ui_view *view,
                           const struct toy_game_actor *player,int width,int height);
int rf_ui_rect_contains(struct rf_ui_rect rect, int x, int y);
int rf_player_ui_hit_test(const struct rf_player_ui_state *state,
                          int width,int height,int rts_active,int x,int y);
int rf_player_ui_rts_hit(const struct rf_ui_layout *layout,const struct rf_rts_state *rts,
    const struct toy_game *game,int x,int y);
int rf_player_ui_rts_page_size(const struct rf_ui_layout *layout);
void rf_ui_panel(struct rasterfall_canvas *canvas, struct rf_ui_rect rect,
                 const struct rf_ui_theme *theme, int selected);
void rf_ui_button(struct rasterfall_canvas *canvas, struct rf_ui_rect rect,
                  const struct rf_ui_theme *theme, const char *label,
                  int scale_milli, int selected, int enabled);
void rf_ui_window(struct rasterfall_canvas *canvas, struct rf_ui_rect rect,
                  const struct rf_ui_theme *theme, const char *title,
                  int scale_milli);
void rf_ui_text(struct rasterfall_canvas *canvas, struct rf_ui_rect rect,
                const char *text, unsigned int color, int scale_milli,
                int max_lines);
void rf_player_ui_layout(struct rasterfall_canvas *canvas,
                         const struct rasterfall_hud_state *hud,int fps);
void rf_player_ui_prompt_layout(struct rasterfall_canvas *canvas,
                                const struct rasterfall_hud_state *hud);
int rf_player_ui_logic_test(void);

#endif
