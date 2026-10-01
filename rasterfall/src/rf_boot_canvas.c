#include "core.h"
#include "tlibc_everything.h"
#include "rf_boot_canvas.h"
#include "fb_draw.h"
#include "fb_font.h"

/* One aspect-preserving canvas for geometry, glyphs and pointer hit tests. */
void boot_view(const struct toy_surface *s, int *w, int *h, int *x, int *y)
{
    *w = s->width;
    *h = *w * 720 / 1280;
    if (*h > s->height) { *h = s->height; *w = *h * 1280 / 720; }
    *x = (s->width - *w) / 2;
    *y = (s->height - *h) / 2;
}

void boot_box(struct toy_surface *s, int x, int y, int w, int h,
                     uint32_t fill, uint32_t edge)
{
    int vw, vh, ox, oy, right, bottom;
    if (!s || !s->pixels || w <= 0 || h <= 0) return;
    boot_view(s, &vw, &vh, &ox, &oy);
    right = ox + (x + w) * vw / 1280;
    bottom = oy + (y + h) * vh / 720;
    x = ox + x * vw / 1280;
    y = oy + y * vh / 720;
    /* Keep single-pixel rules and glyph strokes visible below native size. */
    if (right == x) ++right;
    if (bottom == y) ++bottom;
    w = right - x; h = bottom - y;
    if (x < 0 || y < 0 || w <= 0 || h <= 0 ||
        right > s->width || bottom > s->height) return;
    fb_fill_rect((unsigned char *)s->pixels, x, y, w, h, fill, s->stride);
    if (edge != fill)
        fb_draw_rect((unsigned char *)s->pixels, x, y, w, h, edge, s->stride);
}

void boot_type(struct toy_surface *s, int x, int y, const char *value,
                      uint32_t color, int scale, int max_cells)
{
    if (!s || !s->pixels || !value) return;
    /* Run-length glyph rows preserve the native bitmap face at every size. */
    for (int i = 0; value[i] && i < max_cells; ++i) {
        for (int row = 0; row < FB_FONT_H; ++row) {
            unsigned char bits = fb_font_glyph_row((unsigned char)value[i], row);
            for (int col = 0; col < FB_FONT_W;) {
                int first = col;
                if (!(bits & (128 >> col))) { ++col; continue; }
                while (col < FB_FONT_W && (bits & (128 >> col))) ++col;
                boot_box(s, x + (i * FB_FONT_W + first) * scale,
                         y + row * scale, (col - first) * scale, scale, color, color);
            }
        }
    }
}

void boot_text(struct toy_surface *s, int x, int y,
                      const char *value, uint32_t color)
{
    boot_type(s, x, y, value, color, 1, (1232 - x) / FB_FONT_W);
}

void boot_rule(struct toy_surface *s, int x, int y, int w, uint32_t color)
{
    boot_box(s, x, y, w, 1, color, color);
}

int boot_hit_rect(const struct toy_surface *s, const struct boot_rect *r, int px, int py)
{
    int vw, vh, ox, oy;
    if (!s || !r) return 0;
    boot_view(s, &vw, &vh, &ox, &oy);
    /* Match rasterized edges exactly, including fractional canvas scales. */
    return vw > 0 && vh > 0 &&
        px >= ox + r->x * vw / 1280 && px < ox + (r->x + r->w) * vw / 1280 &&
        py >= oy + r->y * vh / 720 && py < oy + (r->y + r->h) * vh / 720;
}

void rf_boot_canvas_destroy(struct rf_boot_canvas *c)
{
    if (!c) return;
    free(c->shown);
    free(c->source);
    free(c->target);
    memset(c, 0, sizeof(*c));
}

int rf_boot_canvas_seed(struct rf_boot_canvas *c, const uint32_t *pixels,
                        int width, int height, int page)
{
    size_t count, bytes;
    if (!c || !pixels || width <= 0 || height <= 0 ||
        (size_t)width > (size_t)-1 / (size_t)height) return -1;
    count = (size_t)width * height;
    if (count > (size_t)-1 / sizeof(uint32_t)) return -1;
    bytes = count * sizeof(uint32_t);
    rf_boot_canvas_destroy(c);
    c->shown = malloc(bytes);
    c->source = malloc(bytes);
    c->target = malloc(bytes);
    if (!c->shown || !c->source || !c->target) {
        rf_boot_canvas_destroy(c);
        return -1;
    }
    memcpy(c->shown, pixels, bytes);
    c->width = width;
    c->height = height;
    c->page = page;
    c->valid = 1;
    return 0;
}

void rf_boot_canvas_begin(struct rf_boot_canvas *c)
{
    if (c) c->region_count = 0;
}

void rf_boot_canvas_region(struct rf_boot_canvas *c, int x, int y, int w, int h)
{
    struct rf_boot_scan_region *r;
    if (!c || c->region_count == RF_BOOT_SCAN_REGIONS) return;
    r = &c->regions[c->region_count];
    if (c->region_count++ >= c->previous_regions ||
        r->rect.x != x || r->rect.y != y || r->rect.w != w || r->rect.h != h)
        r->active = 0;
    r->rect.x = x; r->rect.y = y; r->rect.w = w; r->rect.h = h;
}

static struct boot_rect scan_rect(const struct toy_surface *s, struct boot_rect r)
{
    int vw, vh, ox, oy, right, bottom;
    boot_view(s, &vw, &vh, &ox, &oy);
    right = ox + (r.x + r.w) * vw / 1280;
    bottom = oy + (r.y + r.h) * vh / 720;
    r.x = ox + r.x * vw / 1280;
    r.y = oy + r.y * vh / 720;
    if (r.x < 0) r.x = 0;
    if (r.y < 0) r.y = 0;
    if (right > s->width) right = s->width;
    if (bottom > s->height) bottom = s->height;
    r.w = right > r.x ? right - r.x : 0;
    r.h = bottom > r.y ? bottom - r.y : 0;
    return r;
}

static uint32_t *scan_row(struct toy_surface *s, int y)
{
    return (uint32_t *)((unsigned char *)s->pixels + (size_t)y * s->stride);
}

static int scan_changed(struct rf_boot_canvas *c, struct toy_surface *s, struct boot_rect r)
{
    for (int y = r.y; y < r.y + r.h; ++y)
        if (memcmp(scan_row(s, y) + r.x, c->target + (size_t)y * c->width + r.x,
                   (size_t)r.w * sizeof(uint32_t))) return 1;
    return 0;
}

static void scan_capture(struct rf_boot_canvas *c, struct boot_rect r)
{
    for (int y = r.y; y < r.y + r.h; ++y) {
        size_t offset = (size_t)y * c->width + r.x;
        memcpy(c->source + offset, c->shown + offset, (size_t)r.w * sizeof(uint32_t));
    }
}

static int scan_cut(int height, int64_t elapsed, int duration)
{
    if (elapsed <= 0) return 0;
    if (elapsed >= duration) return height;
    return (int)(elapsed * height / duration);
}

static void scan_apply(struct rf_boot_canvas *c, struct toy_surface *s,
                       struct boot_rect r, int cut, int beam)
{
    for (int y = r.y + cut; y < r.y + r.h; ++y)
        memcpy(scan_row(s, y) + r.x, c->source + (size_t)y * c->width + r.x,
               (size_t)r.w * sizeof(uint32_t));
    /* A single subtle line follows the actual replacement boundary. */
    if (beam && cut > 0 && cut < r.h) {
        uint32_t *row = scan_row(s, r.y + cut - 1) + r.x;
        for (int x = 0; x < r.w; ++x) {
            uint32_t v = row[x];
            unsigned int red = ((v >> 16) & 255) * 7 + 129;
            unsigned int green = ((v >> 8) & 255) * 7 + 229;
            unsigned int blue = (v & 255) * 7 + 211;
            row[x] = (v & 0xff000000u) | ((red / 8) << 16) | ((green / 8) << 8) | (blue / 8);
        }
    }
}

void rf_boot_canvas_compose(struct rf_boot_canvas *c, struct toy_surface *s,
                            int page, int64_t now, int immediate)
{
    struct boot_rect full;
    int changed_page, cut = 0;
    size_t count, bytes;
    if (!c || !s || !s->pixels || s->width <= 0 || s->height <= 0 ||
        s->width > s->stride / (int)sizeof(uint32_t)) return;
    full.x = full.y = 0; full.w = s->width; full.h = s->height;
    count = (size_t)s->width * s->height;
    if (count > (size_t)-1 / sizeof(uint32_t)) return;
    bytes = count * sizeof(uint32_t);
    if (c->width != s->width || c->height != s->height) {
        /* Resize invalidates pixel history. Show the correctly scaled frame. */
        rf_boot_canvas_destroy(c);
        c->shown = malloc(bytes); c->source = malloc(bytes); c->target = malloc(bytes);
        if (!c->shown || !c->source || !c->target) {
            rf_boot_canvas_destroy(c);
            return; /* Allocation failure affects animation only. */
        }
        c->width = s->width; c->height = s->height;
    }
    changed_page = c->valid && c->page != page;
    if (changed_page) {
        scan_capture(c, full);
        c->started_us = now;
        c->sweeping = !immediate;
    }
    if (immediate || !c->valid) c->sweeping = 0;
    if (c->sweeping) {
        cut = scan_cut(s->height, now - c->started_us, 240000);
        if (cut == s->height) c->sweeping = 0;
    }
    for (int i = 0; i < c->region_count; ++i) {
        struct rf_boot_scan_region *r = &c->regions[i];
        struct boot_rect pixels = scan_rect(s, r->rect);
        if (!c->valid || changed_page || c->sweeping || immediate)
            r->active = 0;
        else if (!r->active && scan_changed(c, s, pixels)) {
            scan_capture(c, pixels);
            r->active = 1; r->started_us = now;
        }
    }
    /* Save desired pixels before composition; otherwise animation itself would
     * be mistaken for a new content change. Updates never queue or restart. */
    for (int y = 0; y < s->height; ++y)
        memcpy(c->target + (size_t)y * c->width, scan_row(s, y),
               (size_t)s->width * sizeof(uint32_t));
    if (c->sweeping) scan_apply(c, s, full, cut, 1);
    else for (int i = 0; i < c->region_count; ++i) {
        struct rf_boot_scan_region *r = &c->regions[i];
        struct boot_rect pixels = scan_rect(s, r->rect);
        if (!r->active) continue;
        cut = scan_cut(pixels.h, now - r->started_us, 80000);
        if (cut == pixels.h) r->active = 0;
        else scan_apply(c, s, pixels, cut, 0);
    }
    for (int y = 0; y < s->height; ++y)
        memcpy(c->shown + (size_t)y * c->width, scan_row(s, y),
               (size_t)s->width * sizeof(uint32_t));
    c->valid = 1; c->page = page; c->previous_regions = c->region_count;
}
void boot_chrome(struct toy_surface *s, const char *section, const char *input)
{
    boot_box(s, 48, 32, 30, 24, BOOT_CYAN, BOOT_CYAN);
    boot_text(s, 55, 36, "RF", BOOT_BG);
    boot_text(s, 94, 36, "RASTERFALL", BOOT_TEXT);
    boot_text(s, 254, 36, section, BOOT_DIM);
    boot_text(s, 1016, 36, input, BOOT_DIM);
    boot_rule(s, 48, 76, 1184, BOOT_EDGE);
    boot_rule(s, 48, 650, 1184, BOOT_EDGE);
    boot_text(s, 48, 673, "RF CORE  /  BOOT ENVIRONMENT", BOOT_DIM);
}

/* Static circuit illustration: a renderer symbol, never a device telemetry view. */
void boot_chip(struct toy_surface *s, int x, int y, int gpu)
{
    for (int i = 0; i < 7; ++i) {
        int n = 16 + i * 20;
        boot_box(s, x - 20, y + n, 20, 4, BOOT_EDGE, BOOT_EDGE);
        boot_box(s, x + 160, y + n, 20, 4, BOOT_EDGE, BOOT_EDGE);
        boot_box(s, x + n, y - 20, 4, 20, BOOT_EDGE, BOOT_EDGE);
        boot_box(s, x + n, y + 160, 4, 20, BOOT_EDGE, BOOT_EDGE);
    }
    boot_box(s, x, y, 160, 160, BOOT_BG, BOOT_CYAN);
    boot_box(s, x + 12, y + 12, 136, 136, BOOT_PANEL, BOOT_EDGE);
    if (gpu) {
        for (int row = 0; row < 3; ++row)
            for (int col = 0; col < 4; ++col)
                boot_box(s, x + 28 + col * 28, y + 30 + row * 32,
                         20, 24, 0x21463Fu, BOOT_CYAN);
    } else {
        boot_box(s, x + 38, y + 38, 84, 84, BOOT_BG, BOOT_CYAN);
        boot_type(s, x + 56, y + 64, "CPU", BOOT_TEXT, 2, 3);
    }
}
