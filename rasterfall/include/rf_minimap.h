#ifndef RF_MINIMAP_H
#define RF_MINIMAP_H

#include "toy_map.h"
#include "rasterfall_canvas.h"

#define RF_MINIMAP_MAX_TERRAIN TOY_MAP_MAX_DRAW
#define RF_MINIMAP_MAX_MARKERS (TOY_GAME_MAX_ACTORS + 64)

enum rf_minimap_marker_kind {
    RF_MINIMAP_PLAYER, RF_MINIMAP_ALLY, RF_MINIMAP_OBJECTIVE, RF_MINIMAP_DEVICE
};
/* Only explicitly known friendly/objective/device data enter this service.
 * Hostile positions are intentionally absent until a perception owner exists. */
struct rf_minimap_marker {
    unsigned int id;
    int kind, x, y, z, sy, cy;
    char label[64];
};
struct rf_minimap_terrain {
    int minx, maxx, minz, maxz, kind;
};
struct rf_minimap_state {
    unsigned int generation, revision;
    const struct toy_map *source;
    int minx, maxx, minz, maxz, ready;
    int terrain_count, marker_count;
    struct rf_minimap_terrain terrain[RF_MINIMAP_MAX_TERRAIN];
    struct rf_minimap_marker markers[RF_MINIMAP_MAX_MARKERS];
};
struct rf_minimap_view {
    int x, y, width, height;
    int center_x, center_z, span; /* span is horizontal world extent */
    int rotation_sy, rotation_cy; /* 1024 unit rotation; north up = 0,1024 */
    int reference_y, layer_threshold;
};
void rf_minimap_init(struct rf_minimap_state *state);
void rf_minimap_invalidate(struct rf_minimap_state *state);
void rf_minimap_prepare(struct rf_minimap_state *state,
                        const struct toy_map *map, unsigned int generation);
int rf_minimap_marker_set(struct rf_minimap_state *state,
                          const struct rf_minimap_marker *marker);
void rf_minimap_marker_remove(struct rf_minimap_state *state, unsigned int id);
void rf_minimap_collect_allies(struct rf_minimap_state *state,
                               const struct toy_game *game);
int rf_minimap_world_to_screen(const struct rf_minimap_view *view,
                               int wx, int wz, int *sx, int *sy);
int rf_minimap_screen_to_world(const struct rf_minimap_view *view,
                               int sx, int sy, int *wx, int *wz);
int rf_minimap_height_hint(const struct rf_minimap_view *view, int world_y);
void rf_minimap_layout(struct rasterfall_canvas *canvas,
                       const struct rf_minimap_state *state,
                       const struct rf_minimap_view *view,
                       unsigned int terrain_color, unsigned int accent);
int rf_minimap_logic_test(void);

#endif
