#ifndef RASTERFALL_HUD_H
#define RASTERFALL_HUD_H

#include "toy_renderer.h"
#include "toy_game.h"
#include "toy_map.h"
#include "rasterfall_map.h"
#include "rasterfall_net.h"

struct rasterfall_hud_state {
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
    int horde_banner_ms;
    const char *interaction_banner;
    const struct rasterfall_net *net;
    const char *host_address;
    int host_port;
};

void rasterfall_hud_draw_interact_prompt(struct toy_renderer *renderer,
                                         const struct rasterfall_hud_state *state);
void rasterfall_hud_render(struct toy_surface *surface, int fps,
                           const struct rasterfall_hud_state *state);
void rasterfall_hud_dump_frame(const char *path, const struct toy_surface *surface);
void rasterfall_hud_damage_flash(struct toy_surface *surface,
                                 const struct toy_game *game);

#endif
