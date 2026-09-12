#ifndef RASTERFALL_APP_H
#define RASTERFALL_APP_H

#include "rasterfall_gui.h"
#include "rf_application_projection.h"

#define RF_APP_MAX 8

struct rf_app;
typedef void (*rf_app_update_fn)(struct rf_app *app,
                                 const struct rf_input_frame *input,
                                 int dt_ms);
typedef void (*rf_app_render_fn)(struct rf_app *app, struct toy_surface *surface,
                                 const struct rf_gui_window *window);

struct rf_app {
    int registered;
    int id;
    int icon;
    int open;
    int window_index;
    char name[32];
    const char *text;
    const struct rf_application_query_context *query;
    rf_app_update_fn update;
    rf_app_render_fn render;
};

struct rf_app_manager {
    struct rf_gui_context *gui;
    struct rf_app apps[RF_APP_MAX];
    int count;
};

void rf_app_manager_init(struct rf_app_manager *manager,
                         struct rf_gui_context *gui);
void rf_app_manager_set_query_context(
    struct rf_app_manager *manager,
    const struct rf_application_query_context *query);
int rf_app_manager_register(struct rf_app_manager *manager,
                            int id, int icon, const char *name,
                            const char *text, rf_app_update_fn update,
                            rf_app_render_fn render);
int rf_app_manager_register_defaults(struct rf_app_manager *manager);
int rf_app_manager_register_station(struct rf_app_manager *manager);
int rf_app_manager_open(struct rf_app_manager *manager, int id,
                        int screen_width, int screen_height);
int rf_app_manager_open_icon(struct rf_app_manager *manager, int icon,
                             int screen_width, int screen_height);
int rf_app_manager_close(struct rf_app_manager *manager, int id);
void rf_app_manager_update(struct rf_app_manager *manager,
                           const struct rf_input_frame *input, int dt_ms);
void rf_app_manager_render_window(struct rf_app_manager *manager,
                                  struct toy_surface *surface,
                                  const struct rf_gui_window *window);
const char *rf_app_manager_icon_name(const struct rf_app_manager *manager,
                                     int icon);
int rf_app_manager_logic_test(void);

#endif
