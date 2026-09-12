#ifndef RASTERFALL_WORLD_CONTENT_H
#define RASTERFALL_WORLD_CONTENT_H

enum rasterfall_world_id {
    RASTERFALL_WORLD_OUTPOST = 0,
    RASTERFALL_WORLD_CAMPAIGN_01 = 1
};

#define RASTERFALL_CONTENT_MAX_FORMATIONS 4
#define RASTERFALL_CONTENT_MAX_MEMBERS 8
#define RASTERFALL_CONTENT_MAX_ACTORS 32
#define RASTERFALL_CONTENT_MAX_TERMINALS 8
#define RASTERFALL_CONTENT_MAX_FLAGS 8
#define RASTERFALL_CONTENT_MAX_FIXTURES 16
#define RASTERFALL_CONTENT_ID_SIZE 64
#define RASTERFALL_CONTENT_KIND_SIZE 32
#define RASTERFALL_CONTENT_ERROR_SIZE 160

struct rasterfall_content_actor {
    char id[RASTERFALL_CONTENT_ID_SIZE];
    char name[RASTERFALL_CONTENT_ID_SIZE];
    char character[RASTERFALL_CONTENT_KIND_SIZE];
    int x, y, z, yaw;
    int line;
};

struct rasterfall_content_terminal {
    char id[RASTERFALL_CONTENT_ID_SIZE];
    char kind[RASTERFALL_CONTENT_KIND_SIZE];
    int x, y, z, yaw;
    int line;
};

struct rasterfall_content_flag {
    char id[RASTERFALL_CONTENT_ID_SIZE];
    int x, y, z;
    int line;
};

struct rasterfall_content_fixture {
    char id[RASTERFALL_CONTENT_ID_SIZE];
    char kind[RASTERFALL_CONTENT_KIND_SIZE];
    int x, y, z, yaw;
    int line;
};

struct rasterfall_content_formation {
    char id[RASTERFALL_CONTENT_ID_SIZE];
    int member_count;
    char member_ids[RASTERFALL_CONTENT_MAX_MEMBERS][RASTERFALL_CONTENT_ID_SIZE];
};

/* Game-owned policy for the current world.  Runtime instances remain in the
 * session/game state; this object decides which content is built. */
struct rasterfall_world_content {
    char source[128];
    int loaded;
    int error_line;
    char error[RASTERFALL_CONTENT_ERROR_SIZE];
    int actor_count;
    int terminal_count;
    int flag_definition_count;
    int fixture_count;
    struct rasterfall_content_actor actors[RASTERFALL_CONTENT_MAX_ACTORS];
    struct rasterfall_content_terminal terminals[RASTERFALL_CONTENT_MAX_TERMINALS];
    struct rasterfall_content_flag flag_definitions[RASTERFALL_CONTENT_MAX_FLAGS];
    struct rasterfall_content_fixture fixtures[RASTERFALL_CONTENT_MAX_FIXTURES];
    int spawn_null;
    int spawn_terminals;
    int spawn_campaign_roster;
    int spawn_maid_squad;
    int spawn_campaign_support;
    int campaign_flags_enabled;
    int model_gallery_enabled;
    int character_test_strip_enabled;
    int campaign_fixture_enabled;
    int formation_count;
    struct rasterfall_content_formation formations[
        RASTERFALL_CONTENT_MAX_FORMATIONS];
};

/* Game-owned V1 world definition.  RF Core never consumes these paths. */
const char *rasterfall_world_map_path(enum rasterfall_world_id world);
const char *rasterfall_world_content_path(enum rasterfall_world_id world);

void rasterfall_world_content_build(struct rasterfall_world_content *content,
                                    enum rasterfall_world_id world);
int rasterfall_world_content_load(struct rasterfall_world_content *content,
                                  enum rasterfall_world_id world,
                                  const char *path);
void rasterfall_world_content_clear(struct rasterfall_world_content *content);

#endif
