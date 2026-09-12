#ifndef RASTERFALL_GUI_H
#define RASTERFALL_GUI_H

#include "rf_core_input.h"
#include "toy_window.h"

#define RF_GUI_MAX_WINDOWS 8
#define RF_GUI_ICON_COUNT 2
#define RF_GUI_MOUSE_LEFT 0x110

struct rf_gui_rect { int x, y, width, height; };
struct rf_app_manager;

struct rf_gui_window {
    /* Desktop/window-manager state.  Application content never owns these. */
    int open;                 /* running */
    int minimized;
    int maximized;
    int focused;
    int z_order;
    int x, y, width, height;
    int restore_x, restore_y, restore_width, restore_height;
    int dragging;
    int app_id;
    int drag_x, drag_y;
    char title[32];
};

/* Screen-space presentation state. It owns no game, map, renderer, or asset
 * state; the context is deliberately small enough to copy per frame. */
struct rf_gui_context {
    int active;
    int icon_count;
    int cursor_x, cursor_y;
    int hovered_icon;
    int drag_window;
    int screen_width, screen_height;
    int next_z_order;
    struct rf_app_manager *app_manager;
    struct rf_gui_window windows[RF_GUI_MAX_WINDOWS];
};

void rf_gui_init(struct rf_gui_context *gui);
void rf_gui_set_active(struct rf_gui_context *gui, int active);
void rf_gui_set_icon_count(struct rf_gui_context *gui, int count);
void rf_gui_set_screen_size(struct rf_gui_context *gui, int width, int height);
void rf_gui_close_all_windows(struct rf_gui_context *gui);
void rf_gui_set_app_manager(struct rf_gui_context *gui,
                            struct rf_app_manager *manager);
int rf_gui_open_window(struct rf_gui_context *gui, int app_id,
                       const char *title,
                       int screen_width, int screen_height);
int rf_gui_handle_input(struct rf_gui_context *gui,
                        const struct rf_input_frame *input,
                        int button_pressed, unsigned int button);
void rf_gui_render(struct toy_surface *surface,
                   const struct rf_gui_context *gui);
int rf_gui_logic_test(void);
int rf_gui_visual_capture(const char *output);

#endif
