/* Small software renderer shared by Toyc windowed games. */

#include "toy_renderer.h"
#include "core.h"
#include "string.h"
#include "tlibc_compat.h"

static long edge(const struct toy_screen_vertex *a,
                 const struct toy_screen_vertex *b, int px, int py)
{
    return (px - a->x) * (b->y - a->y) -
           (py - a->y) * (b->x - a->x);
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

void toy_renderer_init(struct toy_renderer *renderer)
{
    if (renderer) memset(renderer, 0, sizeof(struct toy_renderer));
}

void toy_renderer_destroy(struct toy_renderer *renderer)
{
    if (!renderer) return;
    if (renderer->depth) tlibc_free(renderer->depth);
    memset(renderer, 0, sizeof(struct toy_renderer));
}

int toy_renderer_begin(struct toy_renderer *renderer,
                       const struct toy_surface *surface, uint32_t clear_color)
{
    size_t required;
    if (!renderer || !surface || !surface->pixels ||
        surface->width <= 0 || surface->height <= 0) return -1;
    required = (size_t)surface->width * surface->height * sizeof(int);
    if (required != renderer->depth_size) {
        int *new_depth;
        if (renderer->depth) tlibc_free(renderer->depth);
        renderer->depth = NULL;
        renderer->depth_size = 0;
        new_depth = tlibc_malloc(required);
        if (!new_depth) return -1;
        renderer->depth = new_depth;
        renderer->depth_size = required;
    }
    /* Keep this self-host friendly: avoid a whole-structure assignment here. */
    renderer->surface.pixels = surface->pixels;
    renderer->surface.width = surface->width;
    renderer->surface.height = surface->height;
    renderer->surface.stride = surface->stride;
    for (int y = 0; y < surface->height; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (int x = 0; x < surface->width; x++) {
            row[x] = clear_color;
            renderer->depth[y * surface->width + x] = 0x7fffffff;
        }
    }
    return 0;
}

int toy_renderer_triangle(struct toy_renderer *renderer,
                          const struct toy_screen_vertex *a,
                          const struct toy_screen_vertex *b,
                          const struct toy_screen_vertex *c,
                          uint32_t color)
{
    struct toy_surface *surface;
    long area;
    int minx, maxx, miny, maxy, drawn = 0;
    if (!renderer || !renderer->depth || !a || !b || !c) return 0;
    surface = &renderer->surface;
    area = edge(a, b, c->x, c->y);
    if (area >= 0) return 0;
    minx = clampi(a->x < b->x ? (a->x < c->x ? a->x : c->x) :
                                   (b->x < c->x ? b->x : c->x),
                  0, surface->width - 1);
    maxx = clampi((a->x > b->x ? (a->x > c->x ? a->x : c->x) :
                                    (b->x > c->x ? b->x : c->x)) + 1,
                  0, surface->width - 1);
    miny = clampi(a->y < b->y ? (a->y < c->y ? a->y : c->y) :
                                   (b->y < c->y ? b->y : c->y),
                  0, surface->height - 1);
    maxy = clampi((a->y > b->y ? (a->y > c->y ? a->y : c->y) :
                                    (b->y > c->y ? b->y : c->y)) + 1,
                  0, surface->height - 1);
    for (int y = miny; y <= maxy; y++) {
        uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                     y * surface->stride);
        for (int x = minx; x <= maxx; x++) {
            long w0 = edge(b, c, x, y);
            long w1 = edge(c, a, x, y);
            long w2 = edge(a, b, x, y);
            if (w0 <= 0 && w1 <= 0 && w2 <= 0) {
                int z = (int)((w0 * a->z + w1 * b->z + w2 * c->z) / area);
                int at = y * surface->width + x;
                if (z < renderer->depth[at]) {
                    renderer->depth[at] = z;
                    row[x] = color;
                    drawn++;
                }
            }
        }
    }
    return drawn;
}
