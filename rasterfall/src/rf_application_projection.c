#include "rf_application_projection.h"
#include "tlibc_everything.h"

void rf_application_query_init(
    struct rf_application_query_context *context,
    const struct rf_core *core,
    const struct rf_game_runtime *game_runtime,
    const struct rf_command_context *command)
{
    if (!context) return;
    memset(context, 0, sizeof(*context));
    context->core = core;
    context->game_runtime = game_runtime;
    context->command = command;
}

int rf_application_query_core_status(
    const struct rf_application_query_context *context,
    struct rf_core_status *status)
{
    if (!context || !context->core) return -1;
    return rf_core_get_status(context->core, status);
}

int rf_application_query_game_status(
    const struct rf_application_query_context *context,
    struct rf_game_runtime_status *status)
{
    if (!context || !context->game_runtime) return -1;
    return rf_game_runtime_get_status(context->game_runtime, status);
}

int rf_application_projection_logic_test(void)
{
    struct rf_application_query_context context;
    rf_application_query_init(&context, NULL, NULL, NULL);
    if (context.core || context.game_runtime || context.command) return 1;
    if (rf_application_query_core_status(&context, NULL) == 0) return 2;
    if (rf_application_query_game_status(&context, NULL) == 0) return 3;
    return 0;
}
