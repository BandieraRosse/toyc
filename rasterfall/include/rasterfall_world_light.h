#ifndef RASTERFALL_WORLD_LIGHT_H
#define RASTERFALL_WORLD_LIGHT_H

/* Presentation-only world lighting. No actor/material knowledge or fog.
 * 256 is unity; V1 environment may be overbright (up to 286).
 * V1 proximity darkening and the east boost remain baked into environment;
 * sun visibility and contact are neutral until their data is separated. */
struct rasterfall_world_light {
    int environment_q8;
    int sun_visibility_q8;
    int contact_q8;
};

#define RASTERFALL_BAKED_LM_W 32
#define RASTERFALL_BAKED_LM_H 24

/* Owned by the world-light module; stored per render context. The bounds
 * snapshot belongs to this bake, so queries do not depend on a bound session.
 * Bake after loading/switching the world, before sampling. */
struct rasterfall_world_lighting {
    int minx, maxx, minz, maxz;
    unsigned short environment[RASTERFALL_BAKED_LM_W * RASTERFALL_BAKED_LM_H];
};

struct toy_map;
void rasterfall_world_light_bake(struct rasterfall_world_lighting *lighting,
                                const struct toy_map *map);
/* World position in RFU, including renderer-space Y (ground is -900).
 * Phase A intentionally ignores Y and retains V1 nearest-cell sampling. */
struct rasterfall_world_light rasterfall_world_light_at(
    const struct rasterfall_world_lighting *lighting, int x, int y, int z);
/* Environment * visibility / 256, then * contact / 256. Presentation applies
 * form and material policy afterwards, preserving its existing rounding. */
int rasterfall_world_light_q8(struct rasterfall_world_light light);

#endif
