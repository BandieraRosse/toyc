#include "rasterfall_sky.h"
#include "rasterfall_colors.h"

static void sky_fill_rect(struct toy_surface *surface, int x, int y,
                          int width, int height, uint32_t color)
{
    int yy, xx;
    uint32_t *row;
    if (!surface || !surface->pixels || width <= 0 || height <= 0) return;
    for (yy = y; yy < y + height; yy++) {
        if (yy < 0 || yy >= surface->height) continue;
        row = (uint32_t *)((unsigned char *)surface->pixels + yy * surface->stride);
        for (xx = x; xx < x + width; xx++)
            if (xx >= 0 && xx < surface->width) row[xx] = color;
    }
}

static uint32_t sky_mix_color(uint32_t from, uint32_t to, int num, int den)
{
    unsigned long fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF;
    unsigned long fb = from & 0xFF;
    unsigned long tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF;
    unsigned long tb = to & 0xFF;
    return (uint32_t)(((fr * num + tr * (den - num)) / den) << 16 |
                      ((fg * num + tg * (den - num)) / den) << 8 |
                      ((fb * num + tb * (den - num)) / den));
}

static int project_sky_dir(const struct camera *camera,
                           const struct toy_surface *surface,
                           int dx, int dy, int dz, int *sx, int *sy)
{
    int vx = (dx * camera->cy - dz * camera->sy) / 1024;
    int vz0 = (dx * camera->sy + dz * camera->cy) / 1024;
    int vy2 = (dy * camera->pitch_cy - vz0 * camera->pitch_sy) / 1024;
    int vz2 = (dy * camera->pitch_sy + vz0 * camera->pitch_cy) / 1024;
    int focal = surface->width * 3 / 4;
    if (vz2 <= 64) return 0;
    *sx = surface->width / 2 + vx * focal / vz2;
    *sy = surface->height / 2 - vy2 * focal / vz2;
    return 1;
}

static void draw_sky_cloud(struct toy_surface *surface, int x, int y, int scale)
{
    int unit = scale / 4;
    if (unit < 2) unit = 2;
    sky_fill_rect(surface, x - unit * 6, y, unit * 12, unit * 3, 0xEAF7FF);
    sky_fill_rect(surface, x - unit * 3, y - unit * 2, unit * 6, unit * 2,
                  0xFFFFFF);
    sky_fill_rect(surface, x - unit * 7, y + unit * 2, unit * 14, unit,
                  0xB9D9EC);
    sky_fill_rect(surface, x - unit * 5, y - unit, unit * 2, unit, 0xFFFFFF);
    sky_fill_rect(surface, x + unit * 3, y - unit, unit * 2, unit, 0xFFFFFF);
}

static void draw_sky_features(struct toy_surface *surface,
                              const struct camera *camera)
{
    static const int cloud_dir[3][3] = {
        {340, 248, -934}, {-872, 172, -512}, {-488, 380, -816},
    };
    static const int cloud_scale[3] = {16, 12, 10};
    int cx, cy, i;
    for (i = 0; i < 3; i++) {
        if (!project_sky_dir(camera, surface, cloud_dir[i][0], cloud_dir[i][1],
                             cloud_dir[i][2], &cx, &cy))
            continue;
        draw_sky_cloud(surface, cx, cy, cloud_scale[i]);
    }
}

void rasterfall_sky_draw(struct toy_surface *surface,
                         const struct camera *camera)
{
    int focal = surface->width * 3 / 4;
    int horizon = surface->height / 2;
    int pitch_cy = camera->pitch_cy;
    int sky_bottom, ground_top;
    if (pitch_cy < 0) pitch_cy = -pitch_cy;
    if (pitch_cy >= 64) {
        long long offset = (long long)focal * camera->pitch_sy / camera->pitch_cy;
        if (offset > 2LL * surface->height) offset = 2LL * surface->height;
        if (offset < -2LL * surface->height) offset = -2LL * surface->height;
        horizon += (int)offset;
    } else if (camera->pitch_sy > 0) {
        horizon = -surface->height;
    } else {
        horizon = surface->height * 2;
    }
    sky_bottom = horizon < surface->height ? horizon : surface->height;
    ground_top = horizon > 0 ? horizon : 0;
    if (sky_bottom > 0) {
        int band_h = sky_bottom / 8 + 1;
        int y;
        for (y = 0; y < sky_bottom; y += band_h) {
            int band = y / band_h;
            sky_fill_rect(surface, 0, y, surface->width, band_h,
                          sky_mix_color(0x3B82C4, 0xB9E3FF, 7 - band, 8));
        }
    }
    if (ground_top < surface->height)
        sky_fill_rect(surface, 0, ground_top, surface->width,
                      surface->height - ground_top, 0x0F1218);
    draw_sky_features(surface, camera);
}
