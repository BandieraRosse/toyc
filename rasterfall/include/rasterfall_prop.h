#ifndef RASTERFALL_PROP_H
#define RASTERFALL_PROP_H

#include "rasterfall_units.h"
#include "tlibc_types.h"

/* Static prop asset identities are presentation/content IDs.  They are not
 * map record values, gameplay IDs, or network fields.  Keep assigned values
 * stable when appending new assets. */
enum rasterfall_prop_asset_id {
    RASTERFALL_PROP_ASSET_CRATE = 1,
    RASTERFALL_PROP_ASSET_BARRIER = 2,
    RASTERFALL_PROP_ASSET_LAMP_POST = 3,
    RASTERFALL_PROP_ASSET_COUNT = 3
};

struct rasterfall_prop_dimensions {
    int x;
    int y;
    int z;
};

struct rasterfall_prop_asset_profile {
    int id;
    const char *name;
    const char *model_path;
    /* milli-scale from RMESH units into RFU at the presentation edge. */
    int render_scale_milli;
    /* Default AABB dimensions in RFU; not consumed by gameplay yet. */
    struct rasterfall_prop_dimensions collision_size;
};

/* Presentation-only world instance. x/y/z are RFU; y is the ground/pivot
 * height. scale_milli is an instance multiplier, with 1000 as the default. */
struct rasterfall_prop_instance {
    int asset_id;
    int x;
    int y;
    int z;
    int yaw_degrees;
    int scale_milli;
};

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_profile(int id);
const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_by_name(const char *name);
int rasterfall_prop_asset_logic_test(void);

#endif
