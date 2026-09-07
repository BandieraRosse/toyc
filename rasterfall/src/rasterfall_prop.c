#include "rasterfall_prop.h"
#include "string.h"

/* GLB remains metres, glb2rmesh stores 232 units per metre, and the world
 * uses 512 RFU per metre.  The conversion is deliberately kept here in the
 * asset profile rather than entering map syntax or gameplay state. */
#define RASTERFALL_PROP_RENDER_SCALE_MILLI \
    ((RASTERFALL_RFU_PER_METER * 1000 + 116) / 232)

static const struct rasterfall_prop_asset_profile prop_assets[] = {
    { RASTERFALL_PROP_ASSET_CRATE, "crate",
      "rasterfall/assets/models/rf_crate.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 614, 512, 512 } },
    { RASTERFALL_PROP_ASSET_BARRIER, "barrier",
      "rasterfall/assets/models/rf_barrier.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 1229, 410, 512 } },
    { RASTERFALL_PROP_ASSET_LAMP_POST, "lamp_post",
      "rasterfall/assets/models/rf_lamp_post.rmesh",
      RASTERFALL_PROP_RENDER_SCALE_MILLI, { 410, 410, 1638 } }
};

static const struct rasterfall_prop_asset_profile *find_id(int id)
{
    int i;
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++)
        if (prop_assets[i].id == id) return &prop_assets[i];
    return 0;
}

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_profile(int id)
{
    return find_id(id);
}

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_by_name(const char *name)
{
    int i;
    if (!name) return 0;
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++)
        if (!strcmp(prop_assets[i].name, name)) return &prop_assets[i];
    return 0;
}

int rasterfall_prop_render_scale(
    const struct rasterfall_prop_asset_profile *profile, int instance_scale_milli)
{
    if (!profile || instance_scale_milli <= 0) return 0;
    return (int)((long long)profile->render_scale_milli *
                 instance_scale_milli / 1000);
}

int rasterfall_prop_asset_logic_test(void)
{
    int i;
    const struct rasterfall_prop_asset_profile *crate;
    const struct rasterfall_prop_asset_profile *barrier;
    const struct rasterfall_prop_asset_profile *lamp;
    for (i = 0; i < RASTERFALL_PROP_ASSET_COUNT; i++) {
        const struct rasterfall_prop_asset_profile *asset = prop_assets + i;
        if (asset->id != i + 1 || !asset->name || !asset->model_path ||
            asset->render_scale_milli != RASTERFALL_PROP_RENDER_SCALE_MILLI ||
            asset->collision_size.x <= 0 ||
            asset->collision_size.y <= 0 ||
            asset->collision_size.z <= 0)
            return 1;
    }
    crate = rasterfall_prop_asset_profile(RASTERFALL_PROP_ASSET_CRATE);
    barrier = rasterfall_prop_asset_by_name("barrier");
    lamp = rasterfall_prop_asset_by_name("lamp_post");
    if (!crate || !barrier || !lamp || crate->id != RASTERFALL_PROP_ASSET_CRATE ||
        barrier->id != RASTERFALL_PROP_ASSET_BARRIER ||
        lamp->id != RASTERFALL_PROP_ASSET_LAMP_POST)
        return 2;
    if (rasterfall_prop_asset_profile(0) ||
        rasterfall_prop_asset_by_name("missing") ||
        rasterfall_prop_asset_by_name(0))
        return 3;
    if (crate->render_scale_milli != 2207 ||
        crate->collision_size.x != RASTERFALL_RFU_FROM_MM(1200) ||
        crate->collision_size.y != RASTERFALL_RFU_FROM_MM(1000) ||
        crate->collision_size.z != RASTERFALL_RFU_FROM_MM(1000))
        return 4;
    if (rasterfall_prop_render_scale(crate, 1000) != 2207 ||
        rasterfall_prop_render_scale(crate, 2000) != 4414 ||
        rasterfall_prop_render_scale(crate, 0) != 0)
        return 5;
    return 0;
}
