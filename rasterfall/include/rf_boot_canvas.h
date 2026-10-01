#ifndef RF_BOOT_CANVAS_H
#define RF_BOOT_CANVAS_H

#include "toy_window.h"

/* All boot pages use the same logical canvas, palette and bitmap primitives. */
#define BOOT_BG 0x080D12u
#define BOOT_PANEL 0x101C26u
#define BOOT_TEXT 0xDFE9EDu
#define BOOT_DIM 0x8296A5u
#define BOOT_CYAN 0x81E5D3u
#define BOOT_EDGE 0x293C49u
#define BOOT_AMBER 0xD9A955u
#define FIRMWARE_GREEN 0x8DDBA4u
#define FIRMWARE_BLUE 0x8EBCE8u

struct boot_rect { int x, y, w, h; };
#define RF_BOOT_SCAN_REGIONS 24
struct rf_boot_scan_region {
    struct boot_rect rect;
    int active;
    int64_t started_us;
};
struct rf_boot_canvas {
    /* Tight rows, independent of the window's potentially padded stride. */
    uint32_t *shown, *source, *target;
    int width, height, page, valid, sweeping;
    int region_count, previous_regions;
    int64_t started_us;
    struct rf_boot_scan_region regions[RF_BOOT_SCAN_REGIONS];
};

void boot_view(const struct toy_surface *s, int *w, int *h, int *x, int *y);
void boot_box(struct toy_surface *s, int x, int y, int w, int h,
              uint32_t fill, uint32_t edge);
void boot_type(struct toy_surface *s, int x, int y, const char *value,
               uint32_t color, int scale, int max_cells);
void boot_text(struct toy_surface *s, int x, int y, const char *value, uint32_t color);
void boot_rule(struct toy_surface *s, int x, int y, int w, uint32_t color);
void boot_chrome(struct toy_surface *s, const char *section, const char *input);
void boot_chip(struct toy_surface *s, int x, int y, int gpu);
int boot_hit_rect(const struct toy_surface *s, const struct boot_rect *r, int px, int py);

/* Zero initialize; begin before drawing, register optional scan regions in a
 * stable order, compose after drawing and before the owner's normal present.
 * immediate is for input, failure and synchronous task boundaries. No waits. */
void rf_boot_canvas_begin(struct rf_boot_canvas *canvas);
void rf_boot_canvas_region(struct rf_boot_canvas *canvas, int x, int y, int w, int h);
void rf_boot_canvas_compose(struct rf_boot_canvas *canvas, struct toy_surface *surface,
                            int page, int64_t now_us, int immediate);
int rf_boot_canvas_seed(struct rf_boot_canvas *canvas, const uint32_t *pixels,
                        int width, int height, int page);
void rf_boot_canvas_destroy(struct rf_boot_canvas *canvas);

#endif
