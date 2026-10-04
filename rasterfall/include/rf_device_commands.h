#ifndef RF_DEVICE_COMMANDS_H
#define RF_DEVICE_COMMANDS_H
#include "rf_player_commands.h"

#define RF_DEVICE_FEATURE_CAP 16
#define RF_DEVICE_MAP_CAP 8
enum rf_device_operation { RF_DEVICE_RENDER_SET, RF_DEVICE_TABLE_SELECT, RF_DEVICE_TABLE_DEPLOY };
enum rf_device_support { RF_DEVICE_FORMAL, RF_DEVICE_EXPERIMENTAL,
    RF_DEVICE_UNSUPPORTED, RF_DEVICE_UNIMPLEMENTED };
struct rf_device_feature {
    const char *id, *name;
    int support, enabled, adjustable;
};
struct rf_device_map { const char *id, *name; int world, available; };
struct rf_device_query {
    int bound, scene_backend, feature_count, map_count, selected_map;
    int table_present, table_near, deployable, pending_world;
    uint64_t world_generation;
    char reason[192];
    struct rf_device_feature features[RF_DEVICE_FEATURE_CAP];
    struct rf_device_map maps[RF_DEVICE_MAP_CAP];
};
struct rf_device_request { int operation, index, value; uint64_t world_generation; };
/* Presentation owners supply catalog/state and bounded setters. They never
 * load worlds. The command service validates and queues a deployment; runtime
 * consumes it once, revalidates, and invokes the normal session load path. */
struct rf_device_service {
    void *context;
    void (*query)(void *context, struct rf_device_query *out);
    int (*apply)(void *context, int operation, int index, int value);
    int pending_world;
    uint64_t pending_generation;
    struct rf_player_result last;
};
void rf_device_service_init(struct rf_device_service *service);
void rf_device_query(const struct rf_game_runtime *runtime, struct rf_device_query *out);
struct rf_player_result rf_device_execute(struct rf_game_runtime *runtime,
    const struct rf_device_request *request, enum rf_command_permission_level permission);
/* Returns a world ID once, or -1 when absent/rejected. Check service.last for
 * a rejected queued request. This must run before a world mutation boundary. */
int rf_device_take_deploy(struct rf_game_runtime *runtime);
int rf_device_terminal_command(const struct rf_command_context *context,
    struct rf_command_output *output, int argc, char **argv);
int rf_device_commands_logic_test(void);
#endif
