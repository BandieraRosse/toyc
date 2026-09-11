#include "rasterfall_gui.h"
#include "rasterfall_app.h"
#include "tlibc_everything.h"
#include "fb_font.h"
#include "fb_draw.h"
#include "string.h"

static void fill(struct toy_surface *s, int x, int y, int w, int h,
                 unsigned int color)
{
    int xx, yy;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > s->width) w = s->width - x;
    if (y + h > s->height) h = s->height - y;
    if (w <= 0 || h <= 0) return;
    for (yy = y; yy < y + h; yy++)
        for (xx = x; xx < x + w; xx++)
            s->pixels[yy * (s->stride / 4) + xx] = color;
}

static int inside(int x, int y, int w, int h, int px, int py)
{ return px >= x && py >= y && px < x + w && py < y + h; }

static void text(struct toy_surface *s, int x, int y, const char *value,
                 unsigned int color)
{ fb_draw_string((unsigned char *)s->pixels, x, y, value, color, s->stride); }

static int hit_icon(int x, int y)
{
    int i;
    for (i = 0; i < RF_GUI_ICON_COUNT; i++)
        if (inside(32, 72 + i * 82, 190, 62, x, y)) return i;
    return -1;
}

int rf_gui_open_window(struct rf_gui_context *gui, int app_id,
                       const char *title,
                       int screen_w, int screen_h)
{
    int slot;
    struct rf_gui_window *w;
    if (!gui || !title) return -1;
    for (slot = 0; slot < RF_GUI_MAX_WINDOWS; slot++) if (!gui->windows[slot].open) break;
    if (slot >= RF_GUI_MAX_WINDOWS) return -1;
    w = &gui->windows[slot];
    w->open = 1; w->width = 390; w->height = 230;
    w->app_id = app_id;
    w->x = (screen_w - w->width) / 2 + slot * 18;
    w->y = (screen_h - w->height) / 2 + slot * 12;
    if (w->x + w->width > screen_w - 8) w->x = screen_w - w->width - 8;
    if (w->y + w->height > screen_h - 8) w->y = screen_h - w->height - 8;
    strncpy(w->title, title, sizeof(w->title) - 1);
    w->title[sizeof(w->title) - 1] = 0;
    return slot;
}

void rf_gui_init(struct rf_gui_context *gui)
{ if (gui) { memset(gui, 0, sizeof(*gui)); gui->drag_window = -1; gui->hovered_icon = -1; } }

void rf_gui_set_active(struct rf_gui_context *gui, int active)
{ if (gui) { gui->active = active != 0; if (!gui->active) gui->drag_window = -1; } }

void rf_gui_set_app_manager(struct rf_gui_context *gui,
                            struct rf_app_manager *manager)
{ if (gui) gui->app_manager = manager; }

int rf_gui_handle_input(struct rf_gui_context *gui,
                        const struct rf_input_frame *input,
                        int button_pressed, unsigned int button)
{
    int i, icon;
    if (!gui || !input || !gui->active) return 0;
    gui->cursor_x = input->pointer_x; gui->cursor_y = input->pointer_y;
    gui->hovered_icon = hit_icon(gui->cursor_x, gui->cursor_y);
    if (gui->drag_window >= 0) {
        struct rf_gui_window *w = &gui->windows[gui->drag_window];
        if (input->mouse_buttons & 1) {
            w->x = gui->cursor_x - w->drag_x;
            w->y = gui->cursor_y - w->drag_y;
            return 1;
        }
        w->dragging = 0; gui->drag_window = -1;
    }
    if (!button_pressed || button != RF_GUI_MOUSE_LEFT) return 1;
    for (i = RF_GUI_MAX_WINDOWS - 1; i >= 0; i--) {
        struct rf_gui_window *w = &gui->windows[i];
        if (!w->open || !inside(w->x, w->y, w->width, w->height,
                                 gui->cursor_x, gui->cursor_y)) continue;
        if (inside(w->x + w->width - 30, w->y, 30, 28,
                   gui->cursor_x, gui->cursor_y)) {
            w->open = 0;
            if (gui->app_manager) rf_app_manager_close(gui->app_manager, w->app_id);
            return 1;
        }
        if (inside(w->x, w->y, w->width, 28, gui->cursor_x, gui->cursor_y)) {
            gui->drag_window = i; w->dragging = 1;
            w->drag_x = gui->cursor_x - w->x; w->drag_y = gui->cursor_y - w->y;
        }
        return 1;
    }
    icon = gui->hovered_icon;
    if (icon >= 0 && gui->app_manager)
        return rf_app_manager_open_icon(gui->app_manager, icon, 1024, 720) == 0;
    return 1;
}

static void draw_window(struct toy_surface *s, const struct rf_gui_window *w)
{
    fill(s, w->x - 2, w->y - 2, w->width + 4, w->height + 4, 0xD8B87A);
    fill(s, w->x, w->y, w->width, w->height, 0x172332);
    fill(s, w->x, w->y, w->width, 28, 0x30465A);
    text(s, w->x + 12, w->y + 6, w->title, 0xF6C35B);
    text(s, w->x + w->width - 20, w->y + 6, "X", 0xFF8A80);
}

void rf_gui_render(struct toy_surface *s, const struct rf_gui_context *gui)
{
    int i;
    if (!s || !gui || !gui->active) return;
    fill(s, 0, 0, s->width, s->height, 0x0B1420);
    text(s, 32, 24, "RF GUI DESKTOP  //  CORE PRESENTATION", 0xF6C35B);
    text(s, 32, 46, "F12 close    click icon open    drag title bar    X close", 0x91A5B8);
    for (i = 0; i < RF_GUI_ICON_COUNT; i++) {
        const char *name = gui->app_manager ? rf_app_manager_icon_name(gui->app_manager, i) : "APPLICATION";
        unsigned int border = i == gui->hovered_icon ? 0xF6C35B : 0x557087;
        fill(s, 30, 70 + i * 82, 194, 66, border);
        fill(s, 34, 74 + i * 82, 186, 58, 0x1D2E3E);
        fill(s, 45, 87 + i * 82, 30, 30, i == 0 ? 0x4DA3D9 : i == 1 ? 0x6BCB8B : 0xD88A32);
        text(s, 88, 94 + i * 82, name, 0xE9F0F5);
        text(s, 88, 112 + i * 82, "OPEN PANEL", 0x91A5B8);
    }
    for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) if (gui->windows[i].open) {
        draw_window(s, &gui->windows[i]);
        if (gui->app_manager)
            rf_app_manager_render_window(gui->app_manager, s, &gui->windows[i]);
    }
    fill(s, gui->cursor_x, gui->cursor_y, 2, 14, 0xFFFFFF);
    fill(s, gui->cursor_x, gui->cursor_y, 10, 2, 0xFFFFFF);
}

int rf_gui_logic_test(void)
{
    struct rf_gui_context g; struct rf_app_manager m; struct rf_input_frame in;
    memset(&in, 0, sizeof(in)); rf_gui_init(&g); rf_gui_set_active(&g, 1);
    rf_app_manager_init(&m, &g); if (rf_app_manager_register_defaults(&m) < 0) return 1;
    rf_gui_set_app_manager(&g, &m);
    in.pointer_x = 40; in.pointer_y = 80; in.mouse_buttons = 1;
    if (!rf_gui_handle_input(&g, &in, 1, RF_GUI_MOUSE_LEFT) || !g.windows[0].open) return 2;
    in.pointer_x = g.windows[0].x + 10; in.pointer_y = g.windows[0].y + 10;
    in.mouse_buttons = 1; rf_gui_handle_input(&g, &in, 1, RF_GUI_MOUSE_LEFT);
    in.pointer_x += 20; in.pointer_y += 20;
    rf_gui_handle_input(&g, &in, 0, 0);
    in.mouse_buttons = 0; rf_gui_handle_input(&g, &in, 0, 0);
    if (!g.windows[0].open || g.windows[0].x == (1024 - 390) / 2) return 3;
    return 0;
}
