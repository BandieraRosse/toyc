#ifndef RF_PLAYER_COMMANDS_H
#define RF_PLAYER_COMMANDS_H
#include "tlibc_types.h"
#include "toy_mesh_weaver.h"
#include "rasterfall_console.h"

struct rf_game_runtime;
struct rasterfall_session;
enum rf_player_operation {
    RF_PLAYER_SET_MODE, RF_PLAYER_SET_SCALE, RF_PLAYER_SET_OPACITY,
    RF_PLAYER_SET_HINTS, RF_PLAYER_SET_CROSSHAIR,
    RF_PLAYER_WEAVER_SELECT, RF_PLAYER_WEAVER_START, RF_PLAYER_WEAVER_POWER,
    RF_PLAYER_WEAVER_CPU, RF_PLAYER_WEAVER_X1, RF_PLAYER_WEAVER_COLLECT,
    RF_PLAYER_RTS_SELECT, RF_PLAYER_RTS_MOVE, RF_PLAYER_RTS_STOP,
    RF_PLAYER_RTS_FOLLOW, RF_PLAYER_VIEW, RF_PLAYER_DIALOG_ANSWER,
    RF_PLAYER_DIALOG_COLLAPSE, RF_PLAYER_DIALOG_CLOSE,
    RF_PLAYER_STORY_REPLAY, RF_PLAYER_STORY_RESET
};
enum rf_player_result_code {
    RF_PLAYER_OK, RF_PLAYER_UNAVAILABLE, RF_PLAYER_PERMISSION, RF_PLAYER_RANGE,
    RF_PLAYER_STALE, RF_PLAYER_INVALID, RF_PLAYER_BUSY, RF_PLAYER_RESOURCE,
    RF_PLAYER_CONFIRM_REPLACE
};
struct rf_player_request {
    /* COLLECT confirmation binds value to the weapon the user approved
     * replacing (-1 empty); all device requests carry world/job generations. */
    int operation, value, x, z, confirmed;
    uint64_t world_generation;
    unsigned serial, session_revision, node_revision;
};
struct rf_player_result { int code, affected; char message[192]; };
#define RF_PLAYER_NOTICE_CAP 16
struct rf_player_notice { unsigned serial; int kind, count; char text[160]; };
struct rf_player_controls {
    int selected_blueprint, requested_view, settings_dirty, persistence_error;
    int hints, crosshair, details, preview_yaw, notice_count, notice_ms;
    uint64_t observed_world;
    unsigned observed_job, observed_collected;
    int observed_phase, observed_pause;
    struct rf_player_result last;
    struct rf_player_notice notices[RF_PLAYER_NOTICE_CAP];
};
struct rf_player_weaver_query {
    uint64_t world_generation;
    unsigned serial;
    int available, near, selected, phase, reason, can_start, can_collect;
    int weapon, replace_weapon, rounds, power_on, cpu_on, x1_on; /* replacement -1=empty */
    double progress, estimate_seconds, remaining_seconds, expected_kj, available_kj;
    double used_kj, compute;
    struct toy_mesh_weaver_cost cost;
    char name[64], id[32], status[96];
};
void rf_player_controls_init(struct rf_player_controls *state);
int rf_player_weaver_near(const struct rasterfall_session *session,int x,int z,int facing);
void rf_player_weaver_query(const struct rf_game_runtime *runtime,struct rf_player_weaver_query *out);
struct rf_player_result rf_player_execute(struct rf_game_runtime *runtime,
    const struct rf_player_request *request,enum rf_command_permission_level permission);
int rf_player_terminal_command(const struct rf_command_context *context,
    struct rf_command_output *output,int argc,char **argv);
void rf_player_controls_observe(struct rf_game_runtime *runtime,int dt_ms);
int rf_player_settings_load(struct rf_game_runtime *runtime,const char *path);
int rf_player_settings_save(struct rf_game_runtime *runtime,const char *path);
int rf_player_commands_logic_test(void);
#endif
