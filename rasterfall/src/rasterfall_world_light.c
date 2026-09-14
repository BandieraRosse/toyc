#include "rasterfall_world_light.h"
#include "toy_map.h"
#include "string.h"
#include "math.h"
#include "rasterfall_map_runtime.h"

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

struct rasterfall_world_light rasterfall_world_light_at_v1(
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

/* Slab intersection in bake only. t is in RFU/Q15 direction units;
 * renderer ground -900 is a common translation, so use ground-relative Y. */
static int sun_blocked(const struct rf_map_runtime_collision *c, int x, int y, int z)
{
    int lo[3] = {c->bounds.min_x, c->base_y, c->bounds.min_z};
    int hi[3] = {c->bounds.max_x, c->height, c->bounds.max_z};
    int p[3] = {x, y, z};
    int dir[3] = {RASTERFALL_MODEL_LIGHT_X_Q15,
                  RASTERFALL_MODEL_LIGHT_Y_Q15, RASTERFALL_MODEL_LIGHT_Z_Q15};
    double enter = 0, leave = 1e30;
    int i;
    for (i = 0; i < 3; i++) {
        double a = ((double)lo[i] - p[i]) / dir[i];
        double b = ((double)hi[i] - p[i]) / dir[i];
        if (a > b) { double swap = a; a = b; b = swap; }
        if (a > enter) enter = a;
        if (b < leave) leave = b;
        if (enter > leave) return 0;
    }
    return leave > 0;
}

static int occludes(const struct rf_map_runtime_collision *c)
{
    return c->collision && !strcmp(c->shape, "box") && c->height > c->base_y &&
           (!c->has_role || strncmp(c->role, "air_gate", 8));
}

static int surface_y(const struct rf_map_runtime *runtime, int x, int z)
{
    int i, y = 0;
    for (i = 0; i < rf_map_runtime_surface_count(runtime); i++) {
        const struct rf_map_runtime_surface *s = rf_map_runtime_surface_at(runtime, i);
        int h = s->height;
        if (x < s->bounds.min_x || x > s->bounds.max_x ||
            z < s->bounds.min_z || z > s->bounds.max_z) continue;
        /* Roofs and utility air-gate tops must not replace the ground beneath.
         * One XZ layer represents authored floors, ramps and ordinary platforms. */
        if (strcmp(s->kind, "ground") && strcmp(s->kind, "floor") &&
            strcmp(s->kind, "ramp") && strcmp(s->kind, "platform")) continue;
        if (!strncmp(s->collision_id, "platform_air_gate", 17)) continue;
        if (!strcmp(s->kind, "ramp") && s->has_height2) {
            int along_z = s->has_axis && !strcmp(s->axis, "z");
            int start = along_z ? s->bounds.min_z : s->bounds.min_x;
            int end = along_z ? s->bounds.max_z : s->bounds.max_x;
            int at = along_z ? z : x;
            if (end > start) h += (int)((long long)(s->height2 - h) * (at - start) / (end - start));
        }
        if (h > y) y = h;
    }
    return y;
}

void rasterfall_world_light_bake_v2(struct rasterfall_world_lighting *lighting,
                                  const struct rf_map_runtime *runtime)
{
    const struct rf_map_runtime_world *world = rf_map_runtime_world_info(runtime);
    int ix, iz, i;
    if (world) {
        lighting->minx = world->bounds.min_x; lighting->maxx = world->bounds.max_x;
        lighting->minz = world->bounds.min_z; lighting->maxz = world->bounds.max_z;
    }
    lighting->occluder_count = 0;
    lighting->ray_tests = 0;
    for (i = 0; i < rf_map_runtime_collision_count(runtime); i++)
        if (occludes(rf_map_runtime_collision_at(runtime, i))) lighting->occluder_count++;
    for (iz = 0; iz < RF_WORLD_LIGHT_H; iz++)
    for (ix = 0; ix < RF_WORLD_LIGHT_W; ix++) {
        int index = iz * RF_WORLD_LIGHT_W + ix;
        int x = lighting->minx + (int)((long long)(lighting->maxx - lighting->minx) * ix / (RF_WORLD_LIGHT_W - 1));
        int z = lighting->minz + (int)((long long)(lighting->maxz - lighting->minz) * iz / (RF_WORLD_LIGHT_H - 1));
        int y = surface_y(runtime, x, z), contact = 256, visible = 256;
        for (i = 0; i < rf_map_runtime_collision_count(runtime); i++) {
            const struct rf_map_runtime_collision *c = rf_map_runtime_collision_at(runtime, i);
            int dx, dz, dist;
            if (!occludes(c)) continue;
            lighting->ray_tests++;
            if (sun_blocked(c, x, y + 32, z)) visible = 0;
            dx = x < c->bounds.min_x ? c->bounds.min_x - x : x > c->bounds.max_x ? x - c->bounds.max_x : 0;
            dz = z < c->bounds.min_z ? c->bounds.min_z - z : z > c->bounds.max_z ? z - c->bounds.max_z : 0;
            dist = dx > dz ? dx : dz;
            /* No stacking, no contact beneath elevated beams; at most 3%. */
            if (c->base_y <= y + 64 && c->height > y + 64 && dist < 320) {
                int q = 256 - (320 - dist) * 8 / 320;
                if (q < contact) contact = q;
            }
        }
        lighting->sample_y[index] = y;
        lighting->field[index].environment_q8 = 256;
        lighting->field[index].sun_visibility_q8 = visible;
        lighting->field[index].contact_q8 = contact;
    }
}

static int field_coord(int at, int min, int max, int count)
{
    if (at <= min || max <= min) return 0;
    if (at >= max) return (count - 1) * 256;
    return (int)(((long long)at - min) * (count - 1) * 256 / ((long long)max - min));
}

static int bilerp(int a, int b, int c, int d, int fx, int fz)
{
    return (int)(((long long)a * (256 - fx) * (256 - fz) +
                  (long long)b * fx * (256 - fz) +
                  (long long)c * (256 - fx) * fz +
                  (long long)d * fx * fz + 32768) / 65536);
}

struct rasterfall_world_light rasterfall_world_light_at(
    const struct rasterfall_world_lighting *lighting, int x, int y, int z)
{
    struct rasterfall_world_light light;
    int qx = field_coord(x, lighting->minx, lighting->maxx, RF_WORLD_LIGHT_W);
    int qz = field_coord(z, lighting->minz, lighting->maxz, RF_WORLD_LIGHT_H);
    int ix = qx / 256, iz = qz / 256;
    int nx = ix + (ix < RF_WORLD_LIGHT_W - 1), nz = iz + (iz < RF_WORLD_LIGHT_H - 1);
    const struct rasterfall_world_light *a = lighting->field + iz * RF_WORLD_LIGHT_W + ix;
    const struct rasterfall_world_light *b = lighting->field + iz * RF_WORLD_LIGHT_W + nx;
    const struct rasterfall_world_light *c = lighting->field + nz * RF_WORLD_LIGHT_W + ix;
    const struct rasterfall_world_light *d = lighting->field + nz * RF_WORLD_LIGHT_W + nx;
    (void)y; /* One ground-following XZ layer, not a volume. */
#define INTERPOLATE(component) light.component = bilerp(a->component, b->component, c->component, d->component, qx % 256, qz % 256)
    INTERPOLATE(environment_q8);
    INTERPOLATE(sun_visibility_q8);
    INTERPOLATE(contact_q8);
#undef INTERPOLATE
    return light;
}

int rasterfall_world_light_v2_q8(struct rasterfall_world_light light)
{
    /* Ambient survives obstruction; visibility attenuates only the sun share. */
    return light.environment_q8 * (192 + light.sun_visibility_q8 * 64 / 256) / 256 * light.contact_q8 / 256;
}

int rasterfall_world_light_logic_test(void)
{
    static struct rasterfall_world_lighting field;
    struct rf_map_runtime_collision c = {0};
    struct rasterfall_world_light q;
    struct rf_map_runtime runtime = {0};
    int i;
    c.bounds.min_x = -600; c.bounds.max_x = -400;
    c.bounds.min_z = -600; c.bounds.max_z = -400;
    c.base_y = 0; c.height = 1400;
    if (!sun_blocked(&c, 0, 32, 0)) return 1;
    if (sun_blocked(&c, 0, 32, 2000)) return 2;
    /* Same XZ beam: a high beam misses; lower beam actually intersects. */
    c.base_y = 2000; c.height = 2400;
    if (sun_blocked(&c, 0, 32, 0)) return 3;
    c.base_y = 800; c.height = 1400;
    if (!sun_blocked(&c, 0, 32, 0)) return 4;
    c.base_y = 0; c.height = 1400;
    if (sun_blocked(&c, 0, 1500, 0)) return 5;
    field.minx = 0; field.maxx = (RF_WORLD_LIGHT_W - 1) * 256;
    field.minz = 0; field.maxz = (RF_WORLD_LIGHT_H - 1) * 256;
    for (i = 0; i < RF_WORLD_LIGHT_W * RF_WORLD_LIGHT_H; i++) {
        field.field[i].environment_q8 = 256;
        field.field[i].sun_visibility_q8 = 256;
        field.field[i].contact_q8 = 256;
    }
    field.field[0].sun_visibility_q8 = 0;
    field.field[1].sun_visibility_q8 = 64;
    field.field[RF_WORLD_LIGHT_W].sun_visibility_q8 = 128;
    field.field[RF_WORLD_LIGHT_W+1].sun_visibility_q8 = 256;
    if (rasterfall_world_light_at(&field,0,0,0).sun_visibility_q8 != 0 ||
        rasterfall_world_light_at(&field,256,0,0).sun_visibility_q8 != 64 ||
        rasterfall_world_light_at(&field,0,0,256).sun_visibility_q8 != 128 ||
        rasterfall_world_light_at(&field,256,0,256).sun_visibility_q8 != 256) return 6;
    q = rasterfall_world_light_at(&field,128,0,128);
    if (q.sun_visibility_q8 != 112 || q.environment_q8 != 256 || q.contact_q8 != 256) return 7;
    field.field[0].environment_q8 = 192;
    field.field[1].environment_q8 = 208;
    field.field[RF_WORLD_LIGHT_W].environment_q8 = 224;
    field.field[0].contact_q8 = 248;
    field.field[1].contact_q8 = 252;
    q = rasterfall_world_light_at(&field,128,0,128);
    if (q.environment_q8 != 220 || q.sun_visibility_q8 != 112 || q.contact_q8 != 253) return 7;
    if (rasterfall_world_light_at(&field,-200000,0,-200000).sun_visibility_q8 != 0 ||
        rasterfall_world_light_at(&field,200000,0,200000).sun_visibility_q8 != 256 ||
        rasterfall_world_light_at(&field,field.maxx,0,128).sun_visibility_q8 != 256) return 8;
    q.environment_q8 = q.contact_q8 = 256;
    q.sun_visibility_q8 = 0;
    if (rasterfall_world_light_v2_q8(q) != 192) return 9;
    q.sun_visibility_q8 = 256;
    if (rasterfall_world_light_v2_q8(q) != 256) return 10;
    /* Exercise authored slope heights and roof exclusion in actual Runtime Map. */
    if (rf_map_runtime_load(&runtime, "rasterfall/assets/maps/rasterfall.map") < 0) return 11;
    i = surface_y(&runtime, -20000, -10000);
    if (i != 2186 || surface_y(&runtime, 0, -4700) != 0 ||
        surface_y(&runtime, 0, -10000) != 500) {
        rf_map_runtime_unload(&runtime); return 12;
    }
    rasterfall_world_light_bake_v2(&field, &runtime);
    rf_map_runtime_unload(&runtime);
    if (field.occluder_count <= 0 || field.ray_tests !=
        (long long)RF_WORLD_LIGHT_W * RF_WORLD_LIGHT_H * field.occluder_count) return 13;
    for (i = 0; i < RF_WORLD_LIGHT_W * RF_WORLD_LIGHT_H; i++) {
        if (field.field[i].environment_q8 != 256 || field.field[i].contact_q8 < 248 ||
            field.field[i].contact_q8 > 256 ||
            (field.field[i].sun_visibility_q8 != 0 && field.field[i].sun_visibility_q8 != 256)) return 14;
    }
    return 0;
}
