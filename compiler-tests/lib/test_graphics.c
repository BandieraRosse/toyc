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
    static const unsigned char base_texel[3] = {100, 100, 100};
    static const unsigned char sphere_texel[3] = {10, 20, 30};
    static const unsigned char transparent_texel[4] = {255, 0, 0, 0};
    static const unsigned char translucent_texel[4] = {255, 0, 0, 128};
    static const unsigned char toon_texels[6] = {
        255, 255, 255, 128, 128, 128
    };
    static const unsigned char subtexture_texels[6] = {
        128, 128, 128, 255, 255, 255
    };
    static const unsigned char translucent_red[4] = {255, 0, 0, 128};
    static const unsigned char translucent_blue[4] = {0, 0, 255, 128};
    struct toy_texture_view texture = {texels, 2, 2, 12, 3, 0};
    struct toy_texture_view base_texture = {base_texel, 1, 1, 3, 3, 0};
    struct toy_texture_view sphere_texture = {sphere_texel, 1, 1, 3, 3, 0};
    struct toy_texture_view transparent_texture = {
        transparent_texel, 1, 1, 4, 4, 1
    };
    struct toy_texture_view translucent_texture = {
        translucent_texel, 1, 1, 4, 4, 1
    };
    struct toy_texture_view toon_texture = {toon_texels, 1, 2, 6, 3, 0};
    struct toy_texture_view subtexture = {subtexture_texels, 2, 1, 6, 3, 0};
    struct toy_texture_view red_texture = {translucent_red, 1, 1, 4, 4, 1};
    struct toy_texture_view blue_texture = {translucent_blue, 1, 1, 4, 4, 1};
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

    toy_renderer_begin(&renderer, &surface, 0);
    a.u = b.u = c.u = a.v = b.v = c.v = 0;
    a.u2 = b.u2 = c.u2 = a.v2 = b.v2 = c.v2 = 0;
    a.u_over_z = b.u_over_z = c.u_over_z = 0;
    a.v_over_z = b.v_over_z = c.v_over_z = 0;
    a.u2_over_z = b.u2_over_z = c.u2_over_z = 0;
    a.v2_over_z = b.v2_over_z = c.v2_over_z = 0;
    toy_renderer_triangle_textured_dual_lit(
        &renderer, &a, &b, &c, &base_texture, &sphere_texture,
        2, 0, 0, 256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x006E7882) return 7;

    toy_renderer_begin(&renderer, &surface, 0x123456);
    toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                                       &transparent_texture, 0, 0,
                                       256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x123456) return 8;

    toy_renderer_begin(&renderer, &surface, 0x102030);
    toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                                       &translucent_texture, 0, 0,
                                       256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x870F17) return 9;

    toy_renderer_begin(&renderer, &surface, 0);
    toy_renderer_triangle_textured_material_lit(
        &renderer, &a, &b, &c, &base_texture, 0, 0,
        &toon_texture, -1, 0, 255, 0, 0, 256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x323232) return 10;

    toy_renderer_begin(&renderer, &surface, 0);
    toy_renderer_triangle_textured_material_lit(
        &renderer, &a, &b, &c, &base_texture, &subtexture, 3,
        0, -1, 255, 255, 0, 0, 256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x323232) return 11;

    toy_renderer_begin(&renderer, &surface, 0x102030);
    toy_renderer_triangle_textured_material_lit(
        &renderer, &a, &b, &c, &base_texture, 0, 0,
        0, -1, 255, 128, 0, 0, 256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x3A424A) return 12;

    toy_renderer_begin(&renderer, &surface, 0);
    a.z = b.z = c.z = 10;
    a.inv_z = b.inv_z = c.inv_z = 104857;
    toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                                       &red_texture, 0, 0, 256, 0);
    a.z = b.z = c.z = 20;
    a.inv_z = b.inv_z = c.inv_z = 52428;
    toy_renderer_triangle_textured_lit(&renderer, &a, &b, &c,
                                       &blue_texture, 0, 0, 256, 0);
    toy_renderer_flush(&renderer);
    if (pixels[2 * 8 + 2] != 0x80003F) return 13;

    toy_renderer_destroy(&renderer);
    return 0;
}
