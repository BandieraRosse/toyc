#ifndef TOYC_TOY_RENDERER_H
#define TOYC_TOY_RENDERER_H

#include "toy_window.h"

struct toy_screen_vertex {
    int x;
    int y;
    int z;
};

struct toy_renderer {
    struct toy_surface surface;
    int *depth;
    size_t depth_size;
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

#endif
