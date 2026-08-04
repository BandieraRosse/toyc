/* test_graphics.c — 软件光栅器纯内存回归测试
 * EXPECT: 0
 */

#include "toy_renderer.h"

int main(void)
{
    uint32_t pixels[64];
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct toy_screen_vertex a, b, c;
    int near_drawn, far_drawn;

    surface.pixels = pixels;
    surface.width = 8;
    surface.height = 8;
    surface.stride = 8 * sizeof(uint32_t);
    toy_renderer_init(&renderer);
    if (toy_renderer_begin(&renderer, &surface, 0x112233) < 0) return 1;
    for (int i = 0; i < 64; i++)
        if (pixels[i] != 0x112233) return 2;

    a.x = 1; a.y = 1; a.z = 10;
    b.x = 6; b.y = 1; b.z = 10;
    c.x = 1; c.y = 6; c.z = 10;
    near_drawn = toy_renderer_triangle(&renderer, &a, &b, &c, 0xAABBCC);
    if (near_drawn <= 0 || pixels[2 * 8 + 2] != 0xAABBCC) return 3;

    a.z = 20; b.z = 20; c.z = 20;
    far_drawn = toy_renderer_triangle(&renderer, &a, &b, &c, 0x445566);
    if (far_drawn != 0 || pixels[2 * 8 + 2] != 0xAABBCC) return 4;

    toy_renderer_destroy(&renderer);
    return 0;
}
