#ifndef TOYC_TOY_MAP_H
#define TOYC_TOY_MAP_H

#include "toy_game.h"

#define TOY_MAP_MAX_BOXES 64
#define TOY_MAP_MAX_ZONES 16
#define TOY_MAP_MAX_DRAW 128
#define TOY_MAP_MAX_PICKUPS 8
#define TOY_MAP_TEXT_SIZE 64

enum toy_map_draw_type {
    TOY_MAP_DRAW_FLOOR,
    TOY_MAP_DRAW_BORDER,
    TOY_MAP_DRAW_WALL,
    TOY_MAP_DRAW_LABEL,
    TOY_MAP_DRAW_MODEL,
    TOY_MAP_DRAW_TEXTURE
};

struct toy_map_box { int minx, maxx, minz, maxz, height; unsigned int color; int air; };
struct toy_map_zone { struct toy_game_box box; unsigned int color; };

/* 可交互拾取物：smg/shotgun（主武器）、ammo（弹药盒）与 button（墙上
 * 召唤按钮），x/z/y 为锚点坐标 */
enum toy_map_pickup_kind {
    TOY_MAP_PICKUP_SMG,
    TOY_MAP_PICKUP_SHOTGUN,
    TOY_MAP_PICKUP_AMMO,
    TOY_MAP_PICKUP_BUTTON,
    TOY_MAP_PICKUP_AIR_BUTTON,
    TOY_MAP_PICKUP_ALARM_BUTTON
};
struct toy_map_pickup { int kind; int x, z, y; };
struct toy_map_draw {
    int type;
    int a, b, c, d, e, f;
    unsigned int color;
    int texture_u, texture_v;
    int style;
    char text[TOY_MAP_TEXT_SIZE];
};

struct toy_map {
    int minx, maxx, minz, maxz, room_limit;
    int start_x, start_z;
    struct toy_map_box boxes[TOY_MAP_MAX_BOXES];
    int box_count;
    struct toy_game_box safe_rooms[TOY_MAP_MAX_ZONES];
    int safe_count;
    struct toy_map_zone spawn_zones[TOY_MAP_MAX_ZONES];
    int spawn_count;
    struct toy_game_box alarm_zone;
    int has_alarm;
    struct toy_map_draw draw[TOY_MAP_MAX_DRAW];
    int draw_count;
    struct toy_map_pickup pickups[TOY_MAP_MAX_PICKUPS];
    int pickup_count;
    char *blob;
};

int toy_map_load(const char *path, struct toy_map *map);
void toy_map_unload(struct toy_map *map);

#endif
