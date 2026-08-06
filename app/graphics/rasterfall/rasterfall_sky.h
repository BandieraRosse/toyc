#ifndef RASTERFALL_SKY_H
#define RASTERFALL_SKY_H

#include "toy_window.h"

/* Rasterfall 的相机使用 1024 定点单位保存水平偏航和垂直俯仰。 */
struct camera {
    int x, z, sy, cy, pitch_sy, pitch_cy;
};

void rasterfall_sky_draw(struct toy_surface *surface,
                         const struct camera *camera);

#endif
