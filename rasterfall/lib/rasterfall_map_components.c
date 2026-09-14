#include "rasterfall_map_components.h"
#include "string.h"
#include "math.h"

struct collision_profile {
    const char *kind;
    int width, height, depth;
    int walkable;
};

static const struct collision_profile solids[] = {
    {"crate", 614, 512, 512, 1},
    {"barrier", 1229, 512, 410, 1},
    /* Lamp envelope follows the grounded stem/base, not the light arm. */
    {"lamp_post", 184, 1434, 184, 0},
    {"short_wall", 1229, 717, 256, 1},
    {"railing", 1229, 563, 154, 0},
    {"vent_unit", 717, 614, 461, 1},
    {"workbench", 922, 461, 410, 1},
    {"ammo_container", 461, 307, 256, 1},
    {"industrial_pillar", 410, 1434, 410, 0},
    {"pipe_module", 819, 717, 410, 0},
    {"power_unit", 2458, 1331, 1229, 1},
    {"control_cabinet", 614, 922, 307, 0},
    {"arch_wall", 2048, 2150, 124, 0}
};

static void box(struct rf_map_component_box *b, const char *name,
                int x0, int x1, int y0, int y1, int z0, int z1,
                unsigned int color, int walkable)
{
    b->name = name;
    b->min_x = x0; b->max_x = x1;
    b->min_y = y0; b->max_y = y1;
    b->min_z = z0; b->max_z = z1;
    b->color = color; b->walkable = walkable;
}

static int wall_parts(int length, struct rf_map_component_box *out, int visual)
{
    int start, end, at, n = 0;
    if (!out || length < 256 || length > 200000) return -1;
    start = -length / 2; end = start + length;
    if (visual) {
        box(out + n++, "foot", start, end, 0, 230, -62, 62, 0x505D66, 0);
        box(out + n++, "face", start, end, 230, 2088, -62, 62, 0x77838A, 0);
        box(out + n++, "cap", start, end, 2088, RF_MAP_WALL_HEIGHT,
            -62, 62, 0x596770, 0);
    } else {
        box(out + n++, "slab", start, end, 0, RF_MAP_WALL_HEIGHT,
            -62, 62, 0, 0);
    }
    /* Buttresses touch the slab; never overlay a second complete wall face.
     * Half-spacing at ends leaves room for a clean corner or doorway. */
    for (at = start + RF_MAP_WALL_SPACING / 2;
         at + 128 < end; at += RF_MAP_WALL_SPACING) {
        if (n + 2 > RF_MAP_COMPONENT_MAX_PARTS) return -1;
        if (!visual) {
            /* Slab fills the gap between the two ribs: their union here is
             * one exact box, saving a collision query for every buttress. */
            box(out + n++, "rib", at - 92, at + 92, 0, 2088,
                -108, 108, 0, 0);
            continue;
        }
        box(out + n++, "rib_front", at - 92, at + 92, 0, 2088,
            62, 108, 0x626F77, 0);
        box(out + n++, "rib_back", at - 92, at + 92, 0, 2088,
            -108, -62, 0x626F77, 0);
    }
    return n;
}

int rf_map_wall_visual_boxes(int length, struct rf_map_component_box *out)
{
    return wall_parts(length, out, 1);
}

int rf_map_component_collision_boxes(const char *kind, int length,
                                     struct rf_map_component_box *out)
{
    int i;
    if (!kind || !out) return -1;
    if (!strcmp(kind, "boundary_wall")) return wall_parts(length, out, 0);
    for (i = 0; i < (int)(sizeof(solids) / sizeof(solids[0])); i++) {
        const struct collision_profile *p = solids + i;
        if (strcmp(kind, p->kind)) continue;
        box(out, "body", -p->width / 2, (p->width + 1) / 2,
            0, p->height, -p->depth / 2, (p->depth + 1) / 2, 0, p->walkable);
        return 1;
    }
    if (!strcmp(kind, "gate_frame") || !strcmp(kind, "arch_doorway")) {
        int gate = !strcmp(kind, "gate_frame");
        int inside = gate ? 1219 : 1229, half_depth = gate ? 205 : 69;
        box(out, "left", -1536, -inside, 0, 2150,
            -half_depth, half_depth, 0, 0);
        box(out + 1, "right", inside, 1536, 0, 2150,
            -half_depth, half_depth, 0, 0);
        box(out + 2, "lintel", -inside, inside, gate ? 1900 : 1843,
            2150, -half_depth, half_depth, 0, 0);
        return 3;
    }
    if (!strcmp(kind, "arch_support")) {
        box(out, "left", -614, -410, 0, 1331, -205, 205, 0, 0);
        box(out + 1, "right", 410, 614, 0, 1331, -205, 205, 0, 0);
        box(out + 2, "lintel", -614, 614, 1311, 1434, -154, 154, 0, 0);
        return 3;
    }
    if (!strcmp(kind, "arch_beam")) {
        box(out, "beam", -1536, 1536, 0, 307, -128, 128, 0, 1); return 1;
    }
    if (!strcmp(kind, "arch_pipe_straight")) {
        box(out, "pipe", -512, 512, 0, 317, -159, 159, 0, 0); return 1;
    }
    if (!strcmp(kind, "arch_pipe_elbow") || !strcmp(kind, "arch_pipe_tee")) {
        int tee = !strcmp(kind, "arch_pipe_tee");
        /* Source shifts the bottom-centred fitting by .345 m; Blender +Y
         * becomes GLB/runtime -Z. Preserve the actual branch location. */
        box(out, "main", tee ? -512 : -335, tee ? 512 : 336,
            0, 317, 18, 336, 0, 0);
        box(out + 1, "branch", tee ? -159 : 18, tee ? 159 : 336,
            0, 317, -335, 336, 0, 0); return 2;
    }
    if (!strcmp(kind, "arch_service_panel")) {
        box(out, "panel", -410, 410, 0, 614, -41, 41, 0, 0); return 1;
    }
    if (!strcmp(kind, "arch_cable_tray")) {
        box(out, "tray", -1024, 1024, 0, 164, -62, 62, 0, 0); return 1;
    }
    if (!strcmp(kind, "arch_floor_hatch")) {
        box(out, "hatch", -410, 410, 0, 20, -307, 307, 0, 1); return 1;
    }
    return -1;
}

int rf_map_component_transform(const struct rf_map_component_box *local,
                               int x, int y, int z, int yaw, int scale,
                               struct rf_map_component_box *world)
{
    double s, c, lo_x = 0, hi_x = 0, lo_z = 0, hi_z = 0;
    long long y0, y1;
    int i;
    if (!local || !world || scale <= 0 || scale > 100000) return -1;
    yaw %= 360; if (yaw < 0) yaw += 360;
    if (yaw % 90 == 0) {
        s = yaw == 90 ? 1 : yaw == 270 ? -1 : 0;
        c = yaw == 0 ? 1 : yaw == 180 ? -1 : 0;
    } else {
        s = sin(yaw * 3.141592653589793 / 180.0);
        c = cos(yaw * 3.141592653589793 / 180.0);
    }
    for (i = 0; i < 4; i++) {
        double lx = (i & 1) ? local->max_x : local->min_x;
        double lz = (i & 2) ? local->max_z : local->min_z;
        double wx = x + (lx * c + lz * s) * scale / 1000;
        double wz = z + (lz * c - lx * s) * scale / 1000;
        if (!i || wx < lo_x) lo_x = wx;
        if (!i || wx > hi_x) hi_x = wx;
        if (!i || wz < lo_z) lo_z = wz;
        if (!i || wz > hi_z) hi_z = wz;
    }
    y0 = y + (long long)local->min_y * scale / 1000;
    y1 = y + (long long)local->max_y * scale / 1000;
    if (lo_x < -1000000 || hi_x > 1000000 || lo_z < -1000000 || hi_z > 1000000 ||
        y0 < 0 || y1 > 1000000) return -1;
    *world = *local;
    world->min_x = (int)lo_x; world->max_x = (int)hi_x;
    world->min_z = (int)lo_z; world->max_z = (int)hi_z;
    if (lo_x < world->min_x) world->min_x--;
    if (hi_x > world->max_x) world->max_x++;
    if (lo_z < world->min_z) world->min_z--;
    if (hi_z > world->max_z) world->max_z++;
    world->min_y = (int)y0; world->max_y = (int)y1;
    return 0;
}
