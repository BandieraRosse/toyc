#include "rasterfall_app.h"
#include "fb_font.h"
#include "tlibc_everything.h"
#include "string.h"

enum { RF_APP_CORE_STATUS = 1, RF_APP_PERSONNEL = 2, RF_APP_TERMINAL = 3 };

static void render_text(struct rf_app *app, struct toy_surface *surface,
                        const struct rf_gui_window *window);

static void render_personnel(struct rf_app *app, struct toy_surface *surface,
                             const struct rf_gui_window *window)
{
    struct rf_personnel_snapshot snapshot;
    char text[96];
    int i, y = window->y + 44;
    if (!app->query || rf_application_project_personnel(app->query, &snapshot) < 0) {
        app->text = "PERSONNEL\n\nPERSONNEL DATA UNAVAILABLE";
        render_text(app, surface, window);
        return;
    }
    if (!snapshot.available) {
        app->text = "PERSONNEL\n\nNO ACTIVE SESSION";
        render_text(app, surface, window);
        return;
    }
    fb_draw_string((unsigned char *)surface->pixels, window->x + 18, y,
                   "PERSONNEL", 0xD5E0EA, surface->stride);
    for (i = 0; i < snapshot.count && i < 7; i++) {
        const struct rf_personnel_record *p = &snapshot.people[i];
        snprintf(text, sizeof(text), "%s  %s  %s", p->display_name,
                 p->health_state, p->assignment);
        fb_draw_string((unsigned char *)surface->pixels, window->x + 18,
                       y + (i + 2) * FB_FONT_H, text, 0xD5E0EA,
                       surface->stride);
        snprintf(text, sizeof(text), "  %s / %s / %s", p->role,
                 p->department, p->readiness);
        fb_draw_string((unsigned char *)surface->pixels, window->x + 18,
                       y + (i + 3) * FB_FONT_H, text, 0xAABBCB,
                       surface->stride);
    }
}

static void render_text(struct rf_app *app, struct toy_surface *surface,
                        const struct rf_gui_window *window)
{
    const char *p = app->text, *start = p;
    int line = 0, y = window->y + 44, x = window->x + 18;
    while (*p && y + line * FB_FONT_H < window->y + window->height - 12) {
        if (*p == '\n') {
            char row[96]; int n = (int)(p - start);
            if (n > 95) n = 95;
            memcpy(row, start, n); row[n] = 0;
            fb_draw_string((unsigned char *)surface->pixels, x,
                           y + line++ * FB_FONT_H, row, 0xD5E0EA,
                           surface->stride);
            start = p + 1;
        }
        p++;
    }
    if (start != p && line < 12) {
        char row[96]; int n = (int)(p - start);
        if (n > 95) n = 95;
        memcpy(row, start, n); row[n] = 0;
        fb_draw_string((unsigned char *)surface->pixels, x,
                       y + line * FB_FONT_H, row, 0xD5E0EA,
                       surface->stride);
    }
}

static struct rf_app *find_id(struct rf_app_manager *m, int id)
{ int i; for (i = 0; i < m->count; i++) if (m->apps[i].id == id) return &m->apps[i]; return NULL; }

void rf_app_manager_init(struct rf_app_manager *m, struct rf_gui_context *gui)
{ if (m) { memset(m, 0, sizeof(*m)); m->gui = gui; } }

void rf_app_manager_set_query_context(
    struct rf_app_manager *m,
    const struct rf_application_query_context *query)
{
    int i;
    if (!m) return;
    for (i = 0; i < m->count; i++) m->apps[i].query = query;
}

int rf_app_manager_register(struct rf_app_manager *m, int id, int icon,
                            const char *name, const char *text,
                            rf_app_update_fn update, rf_app_render_fn render)
{
    struct rf_app *a;
    if (!m || m->count >= RF_APP_MAX || find_id(m, id) || !name || !text) return -1;
    a = &m->apps[m->count++]; memset(a, 0, sizeof(*a));
    a->registered = 1; a->id = id; a->icon = icon; a->window_index = -1;
    strncpy(a->name, name, sizeof(a->name) - 1); a->text = text;
    a->update = update; a->render = render ? render : render_text;
    return 0;
}

int rf_app_manager_register_defaults(struct rf_app_manager *m)
{
    if (!m) return -1;
    if (rf_app_manager_register(m, RF_APP_CORE_STATUS, 0, "CORE STATUS",
        "CORE RUNTIME\n\nwindow     READY\nrenderer   READY\nfilesystem READY\nclock      READY\naudio      READY\n\nAPPLICATION MODEL: V0", NULL, NULL) < 0) return -1;
    if (rf_app_manager_register(m, RF_APP_PERSONNEL, 1, "PERSONNEL",
        "PERSONNEL DATA UNAVAILABLE", NULL, render_personnel) < 0) return -1;
    if (rf_app_manager_register(m, RF_APP_TERMINAL, 2, "TERMINAL",
        "TERMINAL\n\nRF TERMINAL FRONTEND\n\nUse the developer console for\ncommands and diagnostics.\n\nApplication is display-only here.", NULL, NULL) < 0) return -1;
    return 0;
}

int rf_app_manager_open(struct rf_app_manager *m, int id, int sw, int sh)
{
    struct rf_app *a; int slot;
    if (!m || !m->gui || !(a = find_id(m, id))) return -1;
    if (a->open) return 0;
    slot = rf_gui_open_window(m->gui, a->id, a->name, a->text, sw, sh);
    if (slot < 0) return -1;
    a->open = 1; a->window_index = slot; return 0;
}

int rf_app_manager_open_icon(struct rf_app_manager *m, int icon, int sw, int sh)
{ int i; if (!m) return -1; for (i = 0; i < m->count; i++) if (m->apps[i].icon == icon) return rf_app_manager_open(m, m->apps[i].id, sw, sh); return -1; }

int rf_app_manager_close(struct rf_app_manager *m, int id)
{ struct rf_app *a = m ? find_id(m, id) : NULL; if (!a) return -1; if (a->window_index >= 0 && a->window_index < RF_GUI_MAX_WINDOWS) m->gui->windows[a->window_index].open = 0; a->open = 0; a->window_index = -1; return 0; }

void rf_app_manager_update(struct rf_app_manager *m,
                           const struct rf_input_frame *input, int dt_ms)
{ int i; if (!m) return; for (i = 0; i < m->count; i++) if (m->apps[i].open && m->apps[i].update) m->apps[i].update(&m->apps[i], input, dt_ms); }

void rf_app_manager_render_window(struct rf_app_manager *m,
                                  struct toy_surface *surface,
                                  const struct rf_gui_window *window)
{ int i; if (!m || !window) return; for (i = 0; i < m->count; i++) if (m->apps[i].id == window->app_id && m->apps[i].render) { m->apps[i].render(&m->apps[i], surface, window); return; } }

const char *rf_app_manager_icon_name(const struct rf_app_manager *m, int icon)
{ int i; if (!m) return ""; for (i = 0; i < m->count; i++) if (m->apps[i].icon == icon) return m->apps[i].name; return ""; }

int rf_app_manager_logic_test(void)
{
    struct rf_gui_context g; struct rf_app_manager m;
    rf_gui_init(&g); rf_app_manager_init(&m, &g);
    if (rf_app_manager_register_defaults(&m) < 0 || m.count != 3) return 1;
    if (rf_app_manager_open(&m, RF_APP_CORE_STATUS, 1024, 720) < 0 || !m.apps[0].open) return 2;
    if (rf_app_manager_close(&m, RF_APP_CORE_STATUS) < 0 || m.apps[0].open) return 3;
    return 0;
}
