#ifndef RF_PLAYER_PANELS_H
#define RF_PLAYER_PANELS_H

#include "rf_player_ui.h"

struct rf_game_runtime;
enum rf_player_weaver_hit {
    RF_WEAVER_HIT_NONE, RF_WEAVER_HIT_START, RF_WEAVER_HIT_COLLECT,
    RF_WEAVER_HIT_POWER, RF_WEAVER_HIT_CPU, RF_WEAVER_HIT_X1,
    RF_WEAVER_HIT_DETAILS, RF_WEAVER_HIT_CLOSE, RF_WEAVER_HIT_MINIMIZE,
    RF_WEAVER_HIT_TERMINAL, RF_WEAVER_HIT_ROTATE_LEFT, RF_WEAVER_HIT_ROTATE_RIGHT,
    RF_WEAVER_HIT_COUNT, RF_WEAVER_HIT_BLUEPRINT_BASE=100
};
#define RF_PLAYER_BLUEPRINT_ROWS 16
struct rf_player_weaver_rects {
    struct rf_ui_rect window, catalog, preview, conditions, status;
    struct rf_ui_rect buttons[RF_WEAVER_HIT_COUNT];
    struct rf_ui_rect blueprints[RF_PLAYER_BLUEPRINT_ROWS];
    int scale_milli, padding;
};
void rf_player_weaver_layout(struct rf_player_weaver_rects *out,int width,int height,
                             const struct rf_player_ui_state *ui);
int rf_player_weaver_hit(const struct rf_player_weaver_rects *rects,int x,int y);
void rf_player_weaver_draw(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime,
                           int focus,int pointer_x,int pointer_y,int confirm_replace);
void rf_player_weaver_card(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime);

enum rf_player_comms_hit { RF_COMMS_HIT_NONE, RF_COMMS_HIT_COLLAPSE,
    RF_COMMS_HIT_CLOSE, RF_COMMS_HIT_HISTORY, RF_COMMS_HIT_CONTINUE,
    RF_COMMS_HIT_CHOICE_BASE=100 };
struct rf_player_comms_rects {
    struct rf_ui_rect window, video, text, footer, choices[3];
    struct rf_ui_rect collapse, close, history;
    int scale_milli, padding;
};
/* video is a transparent hole: exactly this rectangle must be passed to the
 * GPU camera compositor. HUD adds borders but never paints over a live image. */
void rf_player_comms_layout(struct rf_player_comms_rects *out,int width,int height,
                            const struct rf_player_ui_state *ui,int compact,int collapsed);
int rf_player_comms_hit(const struct rf_player_comms_rects *rects,int x,int y,int choice_count);
void rf_player_comms_draw(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime,
                          const struct rf_input_bindings *bindings);
void rf_player_terminal_draw(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime,
                             const struct rf_input_bindings *bindings);
void rf_player_notice_draw(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime);

enum rf_player_device_kind { RF_PLAYER_DEVICE_RENDER, RF_PLAYER_DEVICE_TABLE };
enum rf_player_device_hit {
    RF_DEVICE_HIT_NONE=0, RF_DEVICE_HIT_FEATURE_BASE=1,
    RF_DEVICE_HIT_MAP_BASE=100, RF_DEVICE_HIT_DEPLOY=200,
    RF_DEVICE_HIT_CLOSE, RF_DEVICE_HIT_TERMINAL
};
#define RF_PLAYER_DEVICE_FEATURE_ROWS 16
#define RF_PLAYER_DEVICE_MAP_ROWS 8
struct rf_player_device_rects {
    struct rf_ui_rect window, content, status, close, terminal, deploy;
    struct rf_ui_rect features[RF_PLAYER_DEVICE_FEATURE_ROWS];
    struct rf_ui_rect maps[RF_PLAYER_DEVICE_MAP_ROWS];
    int scale_milli, padding, kind;
};
/* Query/draw/hit contain no commands. The runtime checks the query's actual
 * count/support and executes through the shared device service. */
void rf_player_device_layout(struct rf_player_device_rects *out,int width,int height,
                             const struct rf_player_ui_state *ui,int kind);
int rf_player_device_hit(const struct rf_player_device_rects *rects,int x,int y);
void rf_player_device_draw(struct rasterfall_canvas *canvas,const struct rf_game_runtime *runtime,
                           int kind,int focus);

#endif
