#ifndef TOYC_TOY_RENDERER_H
#define TOYC_TOY_RENDERER_H

#include "toy_window.h"

struct toy_screen_vertex {
    int x;
    int y;
    int z;
    /* Optional Q16.16 UV and perspective terms. Pure-color callers may leave
     * these zero; textured callers use the *_over_z fields. */
    int u;
    int v;
    long inv_z;
    long u_over_z;
    long v_over_z;
};

struct toy_texture_view {
    const unsigned char *data;
    uint32_t width;
    uint32_t height;
    uint32_t data_size;
};

struct toy_renderer {
    struct toy_surface surface;
    int *depth;
    size_t depth_size;
    unsigned long textured_pixels;
    unsigned long textured_triangles;
    unsigned long texture_fallback_pixels;
};

void toy_renderer_init(struct toy_renderer *renderer);
void toy_renderer_destroy(struct toy_renderer *renderer);
int toy_renderer_begin(struct toy_renderer *renderer,
                       const struct toy_surface *surface, uint32_t clear_color);
int toy_renderer_triangle(struct toy_renderer *renderer,
                          const struct toy_screen_vertex *a,
                          const struct toy_screen_vertex *b,
                          const struct toy_screen_vertex *c,
                          uint32_t color);
int toy_renderer_triangle_textured(struct toy_renderer *renderer,
                                   const struct toy_screen_vertex *a,
                                   const struct toy_screen_vertex *b,
                                   const struct toy_screen_vertex *c,
                                   const struct toy_texture_view *texture,
                                   int repeat, uint32_t fallback_color);

#endif
