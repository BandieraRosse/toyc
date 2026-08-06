#ifndef RASTERFALL_SKY_H
#define RASTERFALL_SKY_H

#include "toy_window.h"
#include "rasterfall_camera.h"

void rasterfall_sky_draw(struct toy_surface *surface,
                         const struct camera *camera);

#endif
