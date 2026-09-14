#ifndef RASTERFALL_MAP_COMPONENTS_H
#define RASTERFALL_MAP_COMPONENTS_H

/* Map-owned, RFU geometry contracts. No mesh loading or renderer/game types.
 * Local +X is length, +Y is up, +Z is depth; pivot is at the feet. */
#define RF_MAP_COMPONENT_MAX_PARTS 64
#define RF_MAP_COMPONENT_MAX_COLLISIONS 512
#define RF_MAP_WALL_HEIGHT 2150
#define RF_MAP_WALL_THICKNESS 124
#define RF_MAP_WALL_SPACING 4096

struct rf_map_component_box {
    const char *name;
    int min_x, max_x, min_y, max_y, min_z, max_z;
    unsigned int color;
    int walkable;
};

/* Unknown kind/invalid dimensions returns -1. Known decorative kinds return
 * zero parts. Collision envelopes are authored independently of mesh AABBs. */
int rf_map_component_collision_boxes(const char *kind, int length,
                                     struct rf_map_component_box *out);
/* Closed, adjacent colour bands, without coplanar overlays. Same dimensions
 * and buttress positions as the boundary_wall collision contract. */
int rf_map_wall_visual_boxes(int length, struct rf_map_component_box *out);
/* Exact cardinal rotations; other yaw values produce conservative AABBs.
 * Rejects overflowing transforms instead of wrapping map coordinates. */
int rf_map_component_transform(const struct rf_map_component_box *local,
                               int x, int y, int z, int yaw, int scale,
                               struct rf_map_component_box *world);

#endif
