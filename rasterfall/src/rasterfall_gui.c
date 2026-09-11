#include "rasterfall_gui.h"
#include "tlibc_everything.h"
#include "fb_font.h"
#include "fb_draw.h"
#include "string.h"

static const char *icon_titles[RF_GUI_ICON_COUNT] = {
    "CORE STATUS", "PERSONNEL", "TERMINAL"
};
static const char *icon_text[RF_GUI_ICON_COUNT] = {
    "CORE RUNTIME\n\nwindow     READY\nrenderer   READY\nfilesystem READY\nclock      READY\naudio      READY\n\nPRESENTATION LAYER: GUI V0",
    "PERSONNEL\n\nSQUAD STATUS\n  LOCAL OPERATOR   READY\n  RESPONSE SQUAD   STANDBY\n  ASSAULT SQUAD    STANDBY\n\nNo world entity binding in GUI V0.",
    "TERMINAL\n\nRF TERMINAL FRONTEND\n\nUse the developer console for\ncommands and diagnostics.\n\nGUI V0 is display-only here."
};

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

static void open_window(struct rf_gui_context *gui, int icon,
                        int screen_w, int screen_h)
{
    struct rf_gui_window *w = &gui->windows[icon];
    w->open = 1; w->width = 390; w->height = 230;
    w->x = (screen_w - w->width) / 2 + icon * 18;
    w->y = (screen_h - w->height) / 2 + icon * 12;
    if (w->x + w->width > screen_w - 8) w->x = screen_w - w->width - 8;
    if (w->y + w->height > screen_h - 8) w->y = screen_h - w->height - 8;
    strncpy(w->title, icon_titles[icon], sizeof(w->title) - 1);
    w->title[sizeof(w->title) - 1] = 0;
    w->text = icon_text[icon];
}

void rf_gui_init(struct rf_gui_context *gui)
{ if (gui) { memset(gui, 0, sizeof(*gui)); gui->drag_window = -1; gui->hovered_icon = -1; } }

void rf_gui_set_active(struct rf_gui_context *gui, int active)
{ if (gui) { gui->active = active != 0; if (!gui->active) gui->drag_window = -1; } }

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
                   gui->cursor_x, gui->cursor_y)) { w->open = 0; return 1; }
        if (inside(w->x, w->y, w->width, 28, gui->cursor_x, gui->cursor_y)) {
            gui->drag_window = i; w->dragging = 1;
            w->drag_x = gui->cursor_x - w->x; w->drag_y = gui->cursor_y - w->y;
        }
        return 1;
    }
    icon = gui->hovered_icon;
    if (icon >= 0) { open_window(gui, icon, 1024, 720); return 1; }
    return 1;
}

static void draw_window(struct toy_surface *s, const struct rf_gui_window *w)
{
    int line = 0, x = w->x + 18, y = w->y + 44;
    const char *p = w->text, *start = p;
    fill(s, w->x - 2, w->y - 2, w->width + 4, w->height + 4, 0xD8B87A);
    fill(s, w->x, w->y, w->width, w->height, 0x172332);
    fill(s, w->x, w->y, w->width, 28, 0x30465A);
    text(s, w->x + 12, w->y + 6, w->title, 0xF6C35B);
    text(s, w->x + w->width - 20, w->y + 6, "X", 0xFF8A80);
    while (*p && y + line * FB_FONT_H < w->y + w->height - 12) {
        if (*p == '\n') {
            char row[96]; int n = (int)(p - start);
            if (n > (int)sizeof(row) - 1) n = sizeof(row) - 1;
            memcpy(row, start, n); row[n] = 0; text(s, x, y + line++ * FB_FONT_H, row, 0xD5E0EA);
            start = p + 1;
        }
        p++;
    }
    if (start != p && line < 12) { char row[96]; int n = (int)(p - start); if (n > 95) n = 95; memcpy(row, start, n); row[n] = 0; text(s, x, y + line * FB_FONT_H, row, 0xD5E0EA); }
}

void rf_gui_render(struct toy_surface *s, const struct rf_gui_context *gui)
{
    int i;
    if (!s || !gui || !gui->active) return;
    fill(s, 0, 0, s->width, s->height, 0x0B1420);
    text(s, 32, 24, "RF GUI DESKTOP  //  CORE PRESENTATION", 0xF6C35B);
    text(s, 32, 46, "F12 close    click icon open    drag title bar    X close", 0x91A5B8);
    for (i = 0; i < RF_GUI_ICON_COUNT; i++) {
        unsigned int border = i == gui->hovered_icon ? 0xF6C35B : 0x557087;
        fill(s, 30, 70 + i * 82, 194, 66, border);
        fill(s, 34, 74 + i * 82, 186, 58, 0x1D2E3E);
        fill(s, 45, 87 + i * 82, 30, 30, i == 0 ? 0x4DA3D9 : i == 1 ? 0x6BCB8B : 0xD88A32);
        text(s, 88, 94 + i * 82, icon_titles[i], 0xE9F0F5);
        text(s, 88, 112 + i * 82, "OPEN PANEL", 0x91A5B8);
    }
    for (i = 0; i < RF_GUI_MAX_WINDOWS; i++) if (gui->windows[i].open) draw_window(s, &gui->windows[i]);
    fill(s, gui->cursor_x, gui->cursor_y, 2, 14, 0xFFFFFF);
    fill(s, gui->cursor_x, gui->cursor_y, 10, 2, 0xFFFFFF);
}

int rf_gui_logic_test(void)
{
    struct rf_gui_context g; struct rf_input_frame in;
    memset(&in, 0, sizeof(in)); rf_gui_init(&g); rf_gui_set_active(&g, 1);
    in.pointer_x = 40; in.pointer_y = 80; in.mouse_buttons = 1;
    if (!rf_gui_handle_input(&g, &in, 1, RF_GUI_MOUSE_LEFT) || !g.windows[0].open) return 1;
    in.pointer_x = g.windows[0].x + 10; in.pointer_y = g.windows[0].y + 10;
    in.mouse_buttons = 1; rf_gui_handle_input(&g, &in, 1, RF_GUI_MOUSE_LEFT);
    in.pointer_x += 20; in.pointer_y += 20;
    rf_gui_handle_input(&g, &in, 0, 0);
    in.mouse_buttons = 0; rf_gui_handle_input(&g, &in, 0, 0);
    if (!g.windows[0].open || g.windows[0].x == (1024 - 390) / 2) return 2;
    return 0;
}
