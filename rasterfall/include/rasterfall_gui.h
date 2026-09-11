#ifndef RASTERFALL_GUI_H
#define RASTERFALL_GUI_H

#include "rf_core_input.h"
#include "toy_window.h"

#define RF_GUI_MAX_WINDOWS 8
#define RF_GUI_ICON_COUNT 3
#define RF_GUI_MOUSE_LEFT 0x110

struct rf_gui_rect { int x, y, width, height; };

struct rf_gui_window {
    int open;
    int x, y, width, height;
    int dragging;
    int drag_x, drag_y;
    char title[32];
    const char *text;
};

/* Screen-space presentation state. It owns no game, map, renderer, or asset
 * state; the context is deliberately small enough to copy per frame. */
struct rf_gui_context {
    int active;
    int cursor_x, cursor_y;
    int hovered_icon;
    int drag_window;
    struct rf_gui_window windows[RF_GUI_MAX_WINDOWS];
};

void rf_gui_init(struct rf_gui_context *gui);
void rf_gui_set_active(struct rf_gui_context *gui, int active);
int rf_gui_handle_input(struct rf_gui_context *gui,
                        const struct rf_input_frame *input,
                        int button_pressed, unsigned int button);
void rf_gui_render(struct toy_surface *surface,
                   const struct rf_gui_context *gui);
int rf_gui_logic_test(void);

#endif
