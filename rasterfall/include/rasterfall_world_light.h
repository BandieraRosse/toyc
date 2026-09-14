#ifndef RASTERFALL_WORLD_LIGHT_H
#define RASTERFALL_WORLD_LIGHT_H

/* Presentation-only world lighting. No actor/material knowledge or fog.
 * V2: environment=256, visibility=0..256, weak contact=248..256.
 * Compose retains ambient and attenuates only the directional share. */
struct rasterfall_world_light {
    int environment_q8;
    int sun_visibility_q8;
    int contact_q8;
};

#define RASTERFALL_BAKED_LM_W 32
#define RASTERFALL_BAKED_LM_H 24
#define RF_WORLD_LIGHT_W 64
#define RF_WORLD_LIGHT_H 48
/* Canonical surface-to-sun direction, shared with existing form lighting. */
#define RASTERFALL_MODEL_LIGHT_X_Q15 (-13377)
#define RASTERFALL_MODEL_LIGHT_Y_Q15 26755
#define RASTERFALL_MODEL_LIGHT_Z_Q15 (-13377)

/* Owned by the world-light module; stored per render context. The bounds
 * snapshot belongs to this bake, so queries do not depend on a bound session.
 * Bake after loading/switching the world, before sampling. */
struct rasterfall_world_lighting {
    int minx, maxx, minz, maxz;
    /* Isolated Phase A/V1 cache for consumers deferred beyond Phase B.
     * It never feeds the V2 field or compose. */
    unsigned short environment[RASTERFALL_BAKED_LM_W * RASTERFALL_BAKED_LM_H];
    struct rasterfall_world_light field[RF_WORLD_LIGHT_W * RF_WORLD_LIGHT_H];
    int sample_y[RF_WORLD_LIGHT_W * RF_WORLD_LIGHT_H]; /* ground-relative RFU */
    int occluder_count;
    long long ray_tests;
};

struct toy_map;
struct rf_map_runtime;
void rasterfall_world_light_bake_v2(struct rasterfall_world_lighting *lighting,
                                  const struct rf_map_runtime *runtime);
struct rasterfall_world_light rasterfall_world_light_at_v1(
    const struct rasterfall_world_lighting *lighting, int x, int y, int z);
int rasterfall_world_light_v2_q8(struct rasterfall_world_light light);
int rasterfall_world_light_logic_test(void);
void rasterfall_world_light_bake(struct rasterfall_world_lighting *lighting,
                                const struct toy_map *map);
/* World position in RFU, including renderer-space Y (ground is -900).
 * V2 bilinear/clamped sample of a single ground-following XZ layer.
 * Query Y cannot distinguish overlapping floors; bake owns sample height. */
struct rasterfall_world_light rasterfall_world_light_at(
    const struct rasterfall_world_lighting *lighting, int x, int y, int z);
/* V1 compose, retained only for deferred consumers and its regression. */
int rasterfall_world_light_q8(struct rasterfall_world_light light);

#endif
