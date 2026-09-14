#include "rasterfall_world_light.h"
#include "toy_map.h"
#include "string.h"
#include "math.h"

void rasterfall_world_light_bake(struct rasterfall_world_lighting *lighting,
                                const struct toy_map *map)
{
    int x, z, i;
    lighting->minx = map->minx;
    lighting->maxx = map->maxx;
    lighting->minz = map->minz;
    lighting->maxz = map->maxz;
    for (z = 0; z < RASTERFALL_BAKED_LM_H; z++)
    for (x = 0; x < RASTERFALL_BAKED_LM_W; x++) {
        int wx = map->minx + (map->maxx - map->minx) * (x * 2 + 1) /
                 (RASTERFALL_BAKED_LM_W * 2);
        int wz = map->minz + (map->maxz - map->minz) * (z * 2 + 1) /
                 (RASTERFALL_BAKED_LM_H * 2);
        int light = 270 + (wx - map->minx) * 4 /
                    (map->maxx - map->minx ? map->maxx - map->minx : 1);
        for (i = 0; i < map->primitive_count; i++) {
            const struct toy_map_primitive *b = &map->primitives[i];
            int dx = wx < b->minx ? b->minx - wx : wx > b->maxx ? wx - b->maxx : 0;
            int dz = wz < b->minz ? b->minz - wz : wz > b->maxz ? wz - b->maxz : 0;
            int dist = dx > dz ? dx : dz;
            if (b->shape == TOY_MAP_PRIMITIVE_BOX &&
                strncmp(b->role, "air_gate", 8) && dist < 900)
                light -= (900 - dist) * 24 / 900;
        }
        /* V1's fixed east-corner scalar boost, preserved verbatim. */
        {
            int ldx = wx - 4000, ldz = wz - 160;
            int ldist = (int)isqrt((long long)ldx * ldx +
                                   (long long)ldz * ldz);
            if (ldist < 2600)
                light += (2600 - ldist) * 26 / 2600;
        }
        if (light < 150) light = 150;
        if (light > 286) light = 286;
        lighting->environment[z * RASTERFALL_BAKED_LM_W + x] =
            (unsigned short)light;
    }
}

struct rasterfall_world_light rasterfall_world_light_at(
    const struct rasterfall_world_lighting *lighting, int x, int y, int z)
{
    struct rasterfall_world_light light;
    int ix = (x - lighting->minx) * RASTERFALL_BAKED_LM_W /
             (lighting->maxx - lighting->minx ? lighting->maxx - lighting->minx : 1);
    int iz = (z - lighting->minz) * RASTERFALL_BAKED_LM_H /
             (lighting->maxz - lighting->minz ? lighting->maxz - lighting->minz : 1);
    (void)y;
    if (ix < 0) ix = 0;
    if (ix >= RASTERFALL_BAKED_LM_W) ix = RASTERFALL_BAKED_LM_W - 1;
    if (iz < 0) iz = 0;
    if (iz >= RASTERFALL_BAKED_LM_H) iz = RASTERFALL_BAKED_LM_H - 1;
    light.environment_q8 = lighting->environment[iz * RASTERFALL_BAKED_LM_W + ix];
    light.sun_visibility_q8 = 256;
    light.contact_q8 = 256;
    return light;
}

int rasterfall_world_light_q8(struct rasterfall_world_light light)
{
    return (int)((long long)light.environment_q8 * light.sun_visibility_q8 / 256 *
                 light.contact_q8 / 256);
}
