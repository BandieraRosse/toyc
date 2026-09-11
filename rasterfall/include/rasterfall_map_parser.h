#ifndef RASTERFALL_MAP_PARSER_H
#define RASTERFALL_MAP_PARSER_H

#include "tlibc_types.h"

#define RASTERFALL_MAP_IR_MAX_REGIONS 64
#define RASTERFALL_MAP_IR_MAX_COLLISIONS 128
#define RASTERFALL_MAP_IR_MAX_SURFACES 128
#define RASTERFALL_MAP_IR_MAX_RENDERS 128
#define RASTERFALL_MAP_IR_MAX_INTERACTIONS 64
#define RASTERFALL_MAP_IR_MAX_ACTOR_SPAWNS 32
#define RASTERFALL_MAP_IR_MAX_PICKUPS 48
#define RASTERFALL_MAP_IR_MAX_OBJECTS 128
#define RASTERFALL_MAP_IR_MAX_ATTRIBUTES 8
#define RASTERFALL_MAP_IR_ID_SIZE 64
#define RASTERFALL_MAP_IR_KIND_SIZE 32
#define RASTERFALL_MAP_IR_VALUE_SIZE 96
#define RASTERFALL_MAP_ERROR_SIZE 192

struct rasterfall_map_ir_attribute {
    char key[RASTERFALL_MAP_IR_KIND_SIZE];
    char value[RASTERFALL_MAP_IR_VALUE_SIZE];
};

struct rasterfall_map_ir_bounds { int min_x, max_x, min_z, max_z; };

struct rasterfall_map_ir_world {
    struct rasterfall_map_ir_bounds bounds;
    int room_limit;
    int has_room_limit;
};

struct rasterfall_map_ir_region {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char kind[RASTERFALL_MAP_IR_KIND_SIZE];
    struct rasterfall_map_ir_bounds bounds;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_collision {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char shape[RASTERFALL_MAP_IR_KIND_SIZE];
    struct rasterfall_map_ir_bounds bounds;
    int height;
    int height2;
    int has_height2;
    int collision;
    int visible;
    int walkable;
    int blocks_airborne;
    char color[RASTERFALL_MAP_IR_VALUE_SIZE];
    int has_color;
    char role[RASTERFALL_MAP_IR_KIND_SIZE];
    int has_role;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_surface {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char kind[RASTERFALL_MAP_IR_KIND_SIZE];
    struct rasterfall_map_ir_bounds bounds;
    int height;
    int height2;
    int has_height2;
    char axis[RASTERFALL_MAP_IR_KIND_SIZE];
    int has_axis;
    char material[RASTERFALL_MAP_IR_VALUE_SIZE];
    int has_material;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_render {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char kind[RASTERFALL_MAP_IR_KIND_SIZE];
    struct rasterfall_map_ir_bounds bounds;
    int x, y, z;
    int has_position;
    char asset[RASTERFALL_MAP_IR_VALUE_SIZE];
    int has_asset;
    char color[RASTERFALL_MAP_IR_VALUE_SIZE];
    int has_color;
    int height;
    int has_height;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_interaction {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char action[RASTERFALL_MAP_IR_KIND_SIZE];
    int x, y, z;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_actor_spawn {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char class_name[RASTERFALL_MAP_IR_KIND_SIZE];
    char weapon[RASTERFALL_MAP_IR_KIND_SIZE];
    int base_id;
    int x, y, z;
    int downed;
    int has_weapon;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_pickup {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char kind[RASTERFALL_MAP_IR_KIND_SIZE];
    int x, y, z;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir_object {
    char id[RASTERFALL_MAP_IR_ID_SIZE];
    char kind[RASTERFALL_MAP_IR_KIND_SIZE];
    int x, y, z;
    int yaw;
    int scale;
    struct rasterfall_map_ir_attribute attributes[RASTERFALL_MAP_IR_MAX_ATTRIBUTES];
    int attribute_count;
    int line;
};

struct rasterfall_map_ir {
    int version;
    char units[RASTERFALL_MAP_IR_KIND_SIZE];
    int has_map;
    struct rasterfall_map_ir_world world;
    int has_world;
    struct rasterfall_map_ir_region regions[RASTERFALL_MAP_IR_MAX_REGIONS];
    int region_count;
    struct rasterfall_map_ir_collision collisions[RASTERFALL_MAP_IR_MAX_COLLISIONS];
    int collision_count;
    struct rasterfall_map_ir_surface surfaces[RASTERFALL_MAP_IR_MAX_SURFACES];
    int surface_count;
    struct rasterfall_map_ir_render renders[RASTERFALL_MAP_IR_MAX_RENDERS];
    int render_count;
    struct rasterfall_map_ir_interaction interactions[RASTERFALL_MAP_IR_MAX_INTERACTIONS];
    int interaction_count;
    struct rasterfall_map_ir_actor_spawn actor_spawns[RASTERFALL_MAP_IR_MAX_ACTOR_SPAWNS];
    int actor_spawn_count;
    struct rasterfall_map_ir_pickup pickups[RASTERFALL_MAP_IR_MAX_PICKUPS];
    int pickup_count;
    struct rasterfall_map_ir_object objects[RASTERFALL_MAP_IR_MAX_OBJECTS];
    int object_count;
    int error_line;
    char error[RASTERFALL_MAP_ERROR_SIZE];
};

int rasterfall_map_ir_parse_file(const char *path, struct rasterfall_map_ir *ir);

#endif
