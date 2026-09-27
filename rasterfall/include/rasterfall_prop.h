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
    RASTERFALL_PROP_ASSET_SHORT_WALL = 4,
    RASTERFALL_PROP_ASSET_RAILING = 5,
    RASTERFALL_PROP_ASSET_VENT_UNIT = 6,
    RASTERFALL_PROP_ASSET_WORKBENCH = 7,
    RASTERFALL_PROP_ASSET_AMMO_CONTAINER = 8,
    RASTERFALL_PROP_ASSET_INDUSTRIAL_PILLAR = 9,
    RASTERFALL_PROP_ASSET_PIPE_MODULE = 10,
    RASTERFALL_PROP_ASSET_POWER_UNIT = 11,
    RASTERFALL_PROP_ASSET_GATE_FRAME = 12,
    RASTERFALL_PROP_ASSET_CONTROL_CABINET = 13,
    RASTERFALL_PROP_ASSET_ARCH_BEAM = 14,
    RASTERFALL_PROP_ASSET_ARCH_SUPPORT = 15,
    RASTERFALL_PROP_ASSET_ARCH_WALL = 16,
    RASTERFALL_PROP_ASSET_ARCH_DOORWAY = 17,
    RASTERFALL_PROP_ASSET_ARCH_PIPE_STRAIGHT = 18,
    RASTERFALL_PROP_ASSET_ARCH_PIPE_ELBOW = 19,
    RASTERFALL_PROP_ASSET_ARCH_PIPE_TEE = 20,
    RASTERFALL_PROP_ASSET_ARCH_SERVICE_PANEL = 21,
    RASTERFALL_PROP_ASSET_ARCH_CABLE_TRAY = 22,
    RASTERFALL_PROP_ASSET_ARCH_FLOOR_HATCH = 23,
    RASTERFALL_PROP_ASSET_CAMPUS_WALL_PLAIN = 24,
    RASTERFALL_PROP_ASSET_CAMPUS_WALL_WINDOW = 25,
    RASTERFALL_PROP_ASSET_CAMPUS_WINDOW_STRIP = 26,
    RASTERFALL_PROP_ASSET_CAMPUS_ENTRANCE = 27,
    RASTERFALL_PROP_ASSET_CAMPUS_ROOF_EDGE = 28,
    RASTERFALL_PROP_ASSET_CAMPUS_COLUMN = 29,
    RASTERFALL_PROP_ASSET_CAMPUS_STAIR_SHORT = 30,
    RASTERFALL_PROP_ASSET_CAMPUS_STAIR_LONG = 31,
    RASTERFALL_PROP_ASSET_CAMPUS_RETAINING_WALL = 32,
    RASTERFALL_PROP_ASSET_CAMPUS_CURB = 33,
    RASTERFALL_PROP_ASSET_CAMPUS_SIDEWALK = 34,
    RASTERFALL_PROP_ASSET_CAMPUS_TREE_PROXY = 35,
    RASTERFALL_PROP_ASSET_BOUNDARY_WALL = 36,
    RASTERFALL_PROP_ASSET_FACILITY_DESK = 37,
    RASTERFALL_PROP_ASSET_FACILITY_CHAIR = 38,
    RASTERFALL_PROP_ASSET_FACILITY_MONITOR = 39,
    RASTERFALL_PROP_ASSET_FACILITY_COMMAND_TABLE = 40,
    RASTERFALL_PROP_ASSET_FACILITY_LOW_CABINET = 41,
    RASTERFALL_PROP_ASSET_FACILITY_BENCH = 42,
    RASTERFALL_PROP_ASSET_FACILITY_TERMINAL = 43,
    RASTERFALL_PROP_ASSET_HOST_RACK_FRAME = 44,
    RASTERFALL_PROP_ASSET_HOST_BLANK_PANEL = 45,
    RASTERFALL_PROP_ASSET_HOST_CPU_MODULE = 46,
    RASTERFALL_PROP_ASSET_HOST_MEMORY_MODULE = 47,
    RASTERFALL_PROP_ASSET_HOST_RACK_FAN_PANEL = 48,
    RASTERFALL_PROP_ASSET_HOST_POWER_BUNDLE = 49,
    RASTERFALL_PROP_ASSET_HOST_DATA_BUNDLE = 50,
    RASTERFALL_PROP_ASSET_HOST_CPU_HEADER = 51,
    RASTERFALL_PROP_ASSET_HOST_MEMORY_HEADER = 52,
    RASTERFALL_PROP_ASSET_COUNT = 52
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
    /* Default AABB in RFU. Temporary campus visual assets use all zeros:
     * collision_dimensions rejects them; no implicit gameplay collision. */
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
    int length; /* boundary_wall RFU; Host uses 100 + rack*10 + bay */
};

const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_profile(int id);
const struct rasterfall_prop_asset_profile *
rasterfall_prop_asset_by_name(const char *name);
int rasterfall_prop_render_scale(const struct rasterfall_prop_asset_profile *profile,
                                 int instance_scale_milli);
int rasterfall_prop_collision_dimensions(
    const struct rasterfall_prop_asset_profile *profile,
    int yaw_degrees, int instance_scale_milli,
    struct rasterfall_prop_dimensions *out);
/* Host modules use length=1..6 as the visual bay index; zero previews the asset. */
#define RASTERFALL_HOST_SLOT_COUNT 6
#define RASTERFALL_HOST_RACK_COUNT 4
#define RASTERFALL_HOST_MEMORY_SLOT_MIB 4096ULL
int rasterfall_host_bay_asset(int asset, int bay, int active_slots);
/* Explicit visual override, -1 restores the six-slot design preview. */
void rasterfall_host_set_active_slots(int cpu_slots, int memory_slots);
/* Diagnostic fixed time; -1 restores the caller's live presentation time. */
void rasterfall_host_set_capture_time(int time_ms);
void rasterfall_host_update(unsigned time_ms);
int rasterfall_host_object_active(const char *id);
typedef int (*rasterfall_host_quad_fn)(void *, const int points[4][3], unsigned color);
int rasterfall_host_activity(const struct rasterfall_prop_instance *instance,
    unsigned time_ms, rasterfall_host_quad_fn emit, void *context);
int rasterfall_prop_presented_asset(int asset, int bay);
int rasterfall_prop_asset_logic_test(void);

#endif
