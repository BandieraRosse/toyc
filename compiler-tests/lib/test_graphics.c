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
    static const unsigned char texels[12] = {
        255, 0, 0,  0, 255, 0,
        0, 0, 255, 255, 255, 255
    };
    struct toy_texture_view texture = {texels, 2, 2, 12};
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
    a.inv_z = b.inv_z = c.inv_z = 104857;
    near_drawn = toy_renderer_triangle(&renderer, &a, &b, &c, 0xAABBCC);
    if (near_drawn != 0 || renderer.submitted_triangles != 1) return 3;
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0xAABBCC) return 3;

    a.z = 20; b.z = 20; c.z = 20;
    a.inv_z = b.inv_z = c.inv_z = 52428;
    far_drawn = toy_renderer_triangle(&renderer, &a, &b, &c, 0x445566);
    if (far_drawn != 0) return 4;
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0xAABBCC) return 4;

    toy_renderer_begin(&renderer, &surface, 0);
    a.x = 1; a.y = 1; a.z = 10; a.u = 0; a.v = 0;
    b.x = 6; b.y = 1; b.z = 10; b.u = 2 * 65536; b.v = 0;
    c.x = 1; c.y = 6; c.z = 10; c.u = 0; c.v = 2 * 65536;
    a.inv_z = b.inv_z = c.inv_z = 104857;
    a.u_over_z = (long)a.u * a.inv_z;
    b.u_over_z = (long)b.u * b.inv_z;
    c.u_over_z = (long)c.u * c.inv_z;
    a.v_over_z = (long)a.v * a.inv_z;
    b.v_over_z = (long)b.v * b.inv_z;
    c.v_over_z = (long)c.v * c.inv_z;
    toy_renderer_triangle_textured(&renderer, &a, &b, &c,
                                   &texture, 1, 0x123456);
    if (renderer.textured_triangles != 1 || renderer.submitted_triangles != 1)
        return 6;

    toy_renderer_destroy(&renderer);
    return 0;
}
