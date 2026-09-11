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

int rf_application_projection_logic_test(void);

#endif
