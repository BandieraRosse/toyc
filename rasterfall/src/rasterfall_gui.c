#include "rasterfall_gui.h"
#include "rasterfall_app.h"
#include "tlibc_everything.h"
#include "fb_font.h"
#include "fb_draw.h"
#include "string.h"
#include "rasterfall_hud.h"

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

static int hit_icon(const struct rf_gui_context *gui, int x, int y)
{
    int i;
    for (i = 0; gui && i < gui->icon_count; i++)
        if (inside(32 + i * 152, 72, 136, 78, x, y)) return i;
    return -1;
}

static void focus_window(struct rf_gui_context *gui, int index)
{
    int i;
    if (!gui || index < 0 || index >= RF_GUI_MAX_WINDOWS ||
        !gui->windows[index].open) return;
    for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) gui->windows[i].focused = 0;
    gui->windows[index].focused = 1;
    gui->windows[index].minimized = 0;
    gui->windows[index].z_order = gui->next_z_order++;
}

static int hit_window(const struct rf_gui_context *gui, int x, int y)
{
    int i, best = -1, best_z = -1;
    for (i = 0; gui && i < RF_GUI_MAX_WINDOWS; i++) {
        const struct rf_gui_window *w = &gui->windows[i];
        if (w->open && !w->minimized &&
            inside(w->x, w->y, w->width, w->height, x, y) &&
            w->z_order > best_z) { best = i; best_z = w->z_order; }
    }
    return best;
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
    w->open = 1; w->minimized = 0; w->maximized = 0;
    w->width = 480; w->height = 286;
    w->app_id = app_id;
    w->x = (screen_w - w->width) / 2 + slot * 18;
    w->y = (screen_h - w->height) / 2 + slot * 12;
    if (w->x + w->width > screen_w - 8) w->x = screen_w - w->width - 8;
    if (w->y + w->height > screen_h - 8) w->y = screen_h - w->height - 8;
    strncpy(w->title, title, sizeof(w->title) - 1);
    w->title[sizeof(w->title) - 1] = 0;
    w->restore_x = w->x; w->restore_y = w->y;
    w->restore_width = w->width; w->restore_height = w->height;
    w->z_order = gui->next_z_order++;
    focus_window(gui, slot);
    return slot;
}

void rf_gui_init(struct rf_gui_context *gui)
{ if (gui) { memset(gui, 0, sizeof(*gui)); gui->drag_window = -1; gui->hovered_icon = -1; gui->icon_count = RF_GUI_ICON_COUNT; gui->screen_width = 1024; gui->screen_height = 720; gui->next_z_order = 1; } }

void rf_gui_set_active(struct rf_gui_context *gui, int active)
{ if (gui) { gui->active = active != 0; if (!gui->active) gui->drag_window = -1; } }

void rf_gui_set_icon_count(struct rf_gui_context *gui, int count)
{ if (gui) { if (count < 0) count = 0; if (count > RF_GUI_ICON_COUNT) count = RF_GUI_ICON_COUNT; gui->icon_count = count; gui->hovered_icon = -1; } }

void rf_gui_set_screen_size(struct rf_gui_context *gui, int width, int height)
{ if (gui) { if (width > 0) gui->screen_width = width; if (height > 0) gui->screen_height = height; } }

void rf_gui_close_all_windows(struct rf_gui_context *gui)
{
    int i;
    if (!gui) return;
    for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) { gui->windows[i].open = 0; gui->windows[i].focused = 0; }
    gui->drag_window = -1;
}

void rf_gui_set_app_manager(struct rf_gui_context *gui,
                            struct rf_app_manager *manager)
{ if (gui) gui->app_manager = manager; }

int rf_gui_handle_input(struct rf_gui_context *gui,
                        const struct rf_input_frame *input,
                        int button_pressed, unsigned int button)
{
    int i, icon, window;
    if (!gui || !input || !gui->active) return 0;
    gui->cursor_x = input->pointer_x; gui->cursor_y = input->pointer_y;
    gui->hovered_icon = hit_icon(gui, gui->cursor_x, gui->cursor_y);
    if (gui->drag_window >= 0) {
        struct rf_gui_window *w = &gui->windows[gui->drag_window];
        if (input->mouse_buttons & 1) {
            w->x = gui->cursor_x - w->drag_x;
            w->y = gui->cursor_y - w->drag_y;
            if (w->x < 8) w->x = 8;
            if (w->y < 8) w->y = 8;
            if (w->x + w->width > gui->screen_width - 8)
                w->x = gui->screen_width - w->width - 8;
            if (w->y + w->height > gui->screen_height - 48)
                w->y = gui->screen_height - w->height - 48;
            w->restore_x = w->x; w->restore_y = w->y;
            return 1;
        }
        w->dragging = 0; gui->drag_window = -1;
    }
    if (!button_pressed || button != RF_GUI_MOUSE_LEFT) return 1;
    window = hit_window(gui, gui->cursor_x, gui->cursor_y);
    if (window >= 0) {
        struct rf_gui_window *w = &gui->windows[window];
        int button_x = w->x + w->width - 84;
        focus_window(gui, window);
        if (inside(button_x, w->y, 28, 28, gui->cursor_x, gui->cursor_y)) {
            w->minimized = 1; w->focused = 0; gui->drag_window = -1;
            return 1;
        }
        if (inside(button_x + 28, w->y, 28, 28, gui->cursor_x, gui->cursor_y)) {
            if (w->maximized) {
                w->x = w->restore_x; w->y = w->restore_y;
                w->width = w->restore_width; w->height = w->restore_height;
                w->maximized = 0;
            } else {
                w->restore_x = w->x; w->restore_y = w->y;
                w->restore_width = w->width; w->restore_height = w->height;
                w->x = 8; w->y = 8; w->width = gui->screen_width - 16;
                w->height = gui->screen_height - 64; w->maximized = 1;
            }
            return 1;
        }
        if (inside(button_x + 56, w->y, 28, 28, gui->cursor_x, gui->cursor_y)) {
            if (gui->app_manager) rf_app_manager_close(gui->app_manager, w->app_id);
            return 1;
        }
        if (inside(w->x, w->y, w->width - 84, 28, gui->cursor_x, gui->cursor_y) &&
            !w->maximized) {
            gui->drag_window = window; w->dragging = 1;
            w->drag_x = gui->cursor_x - w->x; w->drag_y = gui->cursor_y - w->y;
        }
        return 1;
    }
    /* The strip is a second launcher for running applications. */
    { int strip = 0;
    for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) {
        struct rf_gui_window *w = &gui->windows[i];
        int x = 104 + strip * 156;
        if (w->open && inside(x, gui->screen_height - 38, 144, 28,
                              gui->cursor_x, gui->cursor_y)) {
            focus_window(gui, i); return 1;
        }
        if (w->open) strip++;
    }
    }
    icon = gui->hovered_icon;
    if (icon >= 0 && gui->app_manager)
        return rf_app_manager_open_icon(gui->app_manager, icon,
                                        gui->screen_width, gui->screen_height) == 0;
    return 1;
}

static void draw_window(struct toy_surface *s, const struct rf_gui_window *w)
{
    unsigned int border = w->focused ? 0xF6C35B : 0x52687A;
    fill(s, w->x - 2, w->y - 2, w->width + 4, w->height + 4, border);
    fill(s, w->x, w->y, w->width, w->height, 0x172332);
    fill(s, w->x, w->y, w->width, 28, 0x30465A);
    text(s, w->x + 12, w->y + 6, w->title, 0xF6C35B);
    text(s, w->x + w->width - 78, w->y + 7, "_", 0xC4D0D9);
    text(s, w->x + w->width - 50, w->y + 7, w->maximized ? "<>" : "[]", 0xC4D0D9);
    text(s, w->x + w->width - 22, w->y + 7, "X", 0xC4D0D9);
}

void rf_gui_render(struct toy_surface *s, const struct rf_gui_context *gui)
{
    int i;
    if (!s || !gui || !gui->active) return;
    int z, max_z;
    rf_gui_set_screen_size((struct rf_gui_context *)gui, s->width, s->height);
    fill(s, 0, 0, s->width, s->height, 0x0B1420);
    fill(s, 0, 0, s->width, 48, 0x101D2A);
    text(s, 24, 14, "RF DESKTOP  //  WORKSTATION", 0xF6C35B);
    text(s, 24, 32, "ESC  CLOSE DESKTOP", 0x8195A7);
    for (i = 0; i < gui->icon_count; i++) {
        const char *name = gui->app_manager ? rf_app_manager_icon_name(gui->app_manager, i) : "APPLICATION";
        unsigned int border = i == gui->hovered_icon ? 0xF6C35B : 0x557087;
        fill(s, 32 + i * 152, 72, 136, 78, border);
        fill(s, 34 + i * 152, 74, 132, 74, 0x1D2E3E);
        fill(s, 48 + i * 152, 88, 32, 32, i == 0 ? 0x4DA3D9 : 0xD88A32);
        text(s, 48 + i * 152, 124, name, 0xE9F0F5);
    }
    max_z = 0;
    for (z = 0; z < RF_GUI_MAX_WINDOWS; z++) if (gui->windows[z].z_order > max_z) max_z = gui->windows[z].z_order;
    for (z = 1; z <= max_z; z++) for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) {
        if (gui->windows[i].open && !gui->windows[i].minimized && gui->windows[i].z_order == z) {
            draw_window(s, &gui->windows[i]);
            if (gui->app_manager) rf_app_manager_render_window(gui->app_manager, s, &gui->windows[i]);
        }
    }
    fill(s, 0, s->height - 46, s->width, 46, 0x101D2A);
    text(s, 24, s->height - 34, "RUNNING", 0x8195A7);
    { int strip = 0;
      for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) if (gui->windows[i].open) {
        unsigned int c = gui->windows[i].focused ? 0xF6C35B : 0x718394;
        fill(s, 104 + strip * 156, s->height - 38, 144, 28, c);
        fill(s, 106 + strip * 156, s->height - 36, 140, 24, gui->windows[i].minimized ? 0x23313D : 0x1D2E3E);
        text(s, 114 + strip * 156, s->height - 30, gui->windows[i].title, c); strip++;
      }
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
    in.pointer_x = g.windows[0].x + g.windows[0].width - 18;
    in.pointer_y = g.windows[0].y + 12;
    in.mouse_buttons = 1;
    rf_gui_handle_input(&g, &in, 1, RF_GUI_MOUSE_LEFT);
    if (g.windows[0].open || m.apps[0].open) return 4;
    return 0;
}

int rf_gui_visual_capture(const char *output)
{
    struct toy_surface surface;
    struct rf_gui_context gui;
    struct rf_app_manager manager;
    int result = -1;
    memset(&surface, 0, sizeof(surface));
    surface.width = 1024; surface.height = 720;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!surface.pixels || !output || !*output) goto done;
    rf_gui_init(&gui); rf_gui_set_active(&gui, 1);
    rf_app_manager_init(&manager, &gui);
    if (rf_app_manager_register_defaults(&manager) < 0) goto done;
    rf_gui_set_app_manager(&gui, &manager);
    rf_gui_render(&surface, &gui);
    result = rasterfall_hud_dump_bmp(output, &surface);
    if (result == 0)
        __printf("rasterfall: visual capture scenario=desktop-v1 size=%dx%d format=BMP output=%s\n",
                 surface.width, surface.height, output);
done:
    tlibc_free(surface.pixels);
    return result;
}
