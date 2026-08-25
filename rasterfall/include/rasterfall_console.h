#ifndef RASTERFALL_CONSOLE_H
#define RASTERFALL_CONSOLE_H
#include "toy_input.h"
#include "toy_renderer.h"
#include "rasterfall_calibration.h"

enum rasterfall_console_log_level {
    RASTERFALL_CONSOLE_INFO,
    RASTERFALL_CONSOLE_WARNING,
    RASTERFALL_CONSOLE_ERROR,
    RASTERFALL_CONSOLE_COMMAND
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
void rasterfall_console_init(struct rasterfall_console *console);
void rasterfall_console_log(struct rasterfall_console *console,
                            enum rasterfall_console_log_level level,
                            const char *message);
int rasterfall_console_handle_input(struct rasterfall_console *console,
                                    struct toy_input *input, unsigned char *pending);
void rasterfall_console_draw(struct toy_surface *surface,
                             const struct rasterfall_console *console);
int rasterfall_console_logic_test(void);
#endif
