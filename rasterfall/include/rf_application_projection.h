#ifndef RASTERFALL_APPLICATION_PROJECTION_H
#define RASTERFALL_APPLICATION_PROJECTION_H

#include "rf_core_host.h"
#include "rf_game_lifecycle.h"

/* Application data is obtained through snapshots, never through a raw Core,
 * session, or toy_game structure.  The command context is borrowed only for
 * later read-only/command projection consumers. */
struct rf_application_query_context {
    const struct rf_core *core;
    const struct rf_game_runtime *game_runtime;
    const struct rf_command_context *command;
};

enum rf_application_data_source {
    RF_APPLICATION_DATA_CORE_STATUS,
    RF_APPLICATION_DATA_GAME_RUNTIME_QUERY
};

#define RF_PERSONNEL_MAX 16
#define RF_PERSONNEL_TEXT_MAX 32

struct rf_personnel_record {
    int person_id;
    char display_name[RF_PERSONNEL_TEXT_MAX];
    char role[RF_PERSONNEL_TEXT_MAX];
    char department[RF_PERSONNEL_TEXT_MAX];
    char readiness[RF_PERSONNEL_TEXT_MAX];
    char health_state[RF_PERSONNEL_TEXT_MAX];
    char assignment[RF_PERSONNEL_TEXT_MAX];
};

/* Application-owned value snapshot. It contains no gameplay pointers and is
 * safe for GUI and Terminal consumers to read independently. */
struct rf_personnel_snapshot {
    int available;
    int count;
    struct rf_personnel_record people[RF_PERSONNEL_MAX];
};

void rf_application_query_init(
    struct rf_application_query_context *context,
    const struct rf_core *core,
    const struct rf_game_runtime *game_runtime,
    const struct rf_command_context *command);

int rf_application_query_core_status(
    const struct rf_application_query_context *context,
    struct rf_core_status *status);
int rf_application_query_game_status(
    const struct rf_application_query_context *context,
    struct rf_game_runtime_status *status);

int rf_application_project_personnel(
    const struct rf_application_query_context *context,
    struct rf_personnel_snapshot *snapshot);

int rf_application_projection_logic_test(void);

#endif
