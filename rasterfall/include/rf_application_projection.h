#ifndef RASTERFALL_APPLICATION_PROJECTION_H
#define RASTERFALL_APPLICATION_PROJECTION_H

struct rf_core;
struct rf_core_status;
struct rf_game_runtime;
struct rf_game_runtime_status;

/* Borrowed for the duration of a query call. It does not transfer ownership
 * and must not be retained after its source runtime ends. */
struct rf_application_query_context {
    const struct rf_core *core;
    const struct rf_game_runtime *game_runtime;
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

/* Application-owned value snapshot. The projector clears and fills the
 * caller-provided object on every call; it contains no gameplay pointers and
 * remains independently readable after the call returns. */
struct rf_personnel_snapshot {
    int available;
    int count;
    struct rf_personnel_record people[RF_PERSONNEL_MAX];
};

void rf_application_query_init(
    struct rf_application_query_context *context,
    const struct rf_core *core,
    const struct rf_game_runtime *game_runtime);

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
