#ifndef RASTERFALL_CONSOLE_H
#define RASTERFALL_CONSOLE_H
#include "rf_core_input.h"
#include "toy_renderer.h"
#include "rasterfall_calibration.h"

enum rasterfall_console_log_level {
    RASTERFALL_CONSOLE_INFO,
    RASTERFALL_CONSOLE_WARNING,
    RASTERFALL_CONSOLE_ERROR,
    RASTERFALL_CONSOLE_COMMAND
};

struct rasterfall_console;
struct rf_core;
struct rf_game_runtime;
struct rf_command_context {
    struct rf_core *core;
    struct rf_game_runtime *game_runtime;
    void *command_state;
};
enum rf_command_output_level {
    RF_COMMAND_OUTPUT_NORMAL,
    RF_COMMAND_OUTPUT_INFO,
    RF_COMMAND_OUTPUT_ERROR
};
#define RF_COMMAND_OUTPUT_MAX_LINES 16
struct rf_command_output_line {
    enum rf_command_output_level level;
    char text[192];
};
struct rf_command_output {
    struct rf_command_output_line lines[RF_COMMAND_OUTPUT_MAX_LINES];
    unsigned int count;
};
typedef int (*rasterfall_console_command_handler)(
    const struct rf_command_context *context,
    struct rf_command_output *output, int argc, char **argv);

struct rasterfall_console_command {
    const char *name;
    rasterfall_console_command_handler handler;
    const char *description;
    const char *permission;
};

struct rasterfall_console_log_line {
    char text[192];
    unsigned int color;
};

struct rasterfall_console {
    int open, history_cursor, was_paused;
    char line[160];
    char history[8][160];
    struct rasterfall_console_log_line output[64];
    int output_count;
    int killall_requested;
    int give_requested;
    int pose_hud_request;
    int close_requested;
    struct rasterfall_calibration_state calibration;
};
const struct rasterfall_console_command *rasterfall_console_commands(
    unsigned int *count);
void rf_command_output_init(struct rf_command_output *output);
void rf_command_output_write(struct rf_command_output *output,
                             enum rf_command_output_level level,
                             const char *text);
void rasterfall_console_init(struct rasterfall_console *console);
void rasterfall_console_log(struct rasterfall_console *console,
                            enum rasterfall_console_log_level level,
                            const char *message);
int rasterfall_console_handle_input(struct rasterfall_console *console,
                                    struct rf_input_frame *input, unsigned char *pending);
int rasterfall_console_handle_input_context(
    struct rasterfall_console *console, struct rf_input_frame *input,
    unsigned char *pending, const struct rf_command_context *context);
void rasterfall_console_draw(struct toy_surface *surface,
                             const struct rasterfall_console *console);
int rasterfall_console_logic_test(void);
#endif
