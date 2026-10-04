#ifndef RF_RTS_H
#define RF_RTS_H
#include "toy_game.h"
#include "rasterfall_camera.h"

#define RF_RTS_GROUPS 10
/* Membership covers the entire actor pool; there is no squad-size limit.
 * A reused slot must never inherit a selection or a control group. */
struct rf_rts_member { int active, actor_id; unsigned generation; };
struct rf_rts_state {
    uint64_t world_generation;
    struct rf_rts_member selected[TOY_GAME_MAX_ACTORS];
    struct rf_rts_member groups[RF_RTS_GROUPS][TOY_GAME_MAX_ACTORS];
    int primary, hovered, page, active_group;
    int drag_active, drag_started, drag_x, drag_y, pointer_x, pointer_y;
    int drag_add, video_requested, video_live, video_state;
    int video_actor_id;unsigned video_generation;
};
int rf_rts_selectable(const struct toy_game *game,int index);
int rf_rts_member_valid(const struct rf_rts_member *member,const struct toy_game *game,int index);
void rf_rts_sync(struct rf_rts_state *state,const struct toy_game *game,uint64_t world);
void rf_rts_clear(struct rf_rts_state *state);
void rf_rts_select(struct rf_rts_state *state,const struct toy_game *game,int index,int add);
int rf_rts_count(const struct rf_rts_state *state,const struct toy_game *game);
int rf_rts_nth(const struct rf_rts_state *state,const struct toy_game *game,int ordinal);
int rf_rts_group_count(const struct rf_rts_state *state,const struct toy_game *game,int group);
void rf_rts_group(struct rf_rts_state *state,const struct toy_game *game,int group,int assign);
int rf_rts_project(const struct camera *camera,int width,int height,int x,int y,int z,int *sx,int *sy);
int rf_rts_pick(const struct toy_game *game,const struct camera *camera,int width,int height,int x,int y);
void rf_rts_box(struct rf_rts_state *state,const struct toy_game *game,const struct camera *camera,
    int width,int height,int x0,int y0,int x1,int y1,int add);
int rf_rts_logic_test(void);

#define RF_RTS_PORTRAIT_W 96
#define RF_RTS_PORTRAIT_H 128
void rf_rts_portraits_prepare(const struct rf_rts_state *state,const struct toy_game *game);
const unsigned char *rf_rts_portrait(const struct toy_game_actor *actor);
#endif
