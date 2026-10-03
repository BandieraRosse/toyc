#ifndef RF_MESH_WEAVER_PRESENTATION_H
#define RF_MESH_WEAVER_PRESENTATION_H

/* Immutable presentation value. Resource accounting and output ownership live
 * in Game/session. Positions are RFU in the same WORLD depth domain. */
struct rf_mesh_weaver_frame {
    int present, x, y, z;
    int phase, pause_reason, weapon, powered, output_slot;
    unsigned serial, time_ms, collected_count;
    double progress, phase_ms, phase_duration_ms;
    int tray_pose_override;
    double tray_extension;
};

/* Runtime-owned visual state, never Game or renderer-local history. Update
 * once per presented frame, then publish the same immutable frame to CPU/GPU. */
struct rf_weaver_presentation_state {
    unsigned long long world_generation, last_time_us;
    int valid, x, y, z, paused, returning;
    unsigned serial, collected_count;
    double extension, velocity;
    double return_from, return_velocity, return_elapsed_ms, return_duration_ms;
};
void rf_weaver_presentation_reset(struct rf_weaver_presentation_state *);
void rf_weaver_presentation_update(struct rf_weaver_presentation_state *,
    struct rf_mesh_weaver_frame *, unsigned long long world_generation,
    unsigned long long time_us, int paused);

struct rf_mesh_weaver_gpu;

#define RF_WEAVER_LOCAL_UNITS 8192
#define RF_WEAVER_MAX_MATERIALS 24
#define RF_WEAVER_MAX_BONES 42
#define RF_WEAVER_ACTIVE_BEHIND .025
#define RF_WEAVER_ACTIVE_AHEAD .018
/* A lit stroke spans .40/16=.025 progress, inside the unchanged .043-wide
 * active band. Longer unlit handoffs keep the default rig below 180 deg/s;
 * independent head offsets maintain coverage without rapid pair switching. */
#define RF_WEAVER_STROKE_BINS 16
#define RF_WEAVER_STROKE_MARGIN .30
#define RF_WEAVER_STROKE_HEAD_OFFSET .125

struct rf_weaver_vertex { int position[3], normal[3]; };
struct rf_weaver_face {
    struct rf_weaver_vertex vertex[3];
    unsigned material, source;
    double threshold;
};
struct rf_weaver_material { unsigned color; float roughness, metallic; };
struct rf_weaver_growth { double threshold; unsigned face; };
/* Immutable weapon-local scan schedule. Each lit stroke stays in one real
 * triangle; head retargeting between strokes takes place with the beam off. */
struct rf_weaver_stroke { unsigned face; int active; double barycentric[2][3]; };
struct rf_weaver_ray_cache;
struct rf_weaver_mesh {
    struct rf_weaver_face *faces;
    struct rf_weaver_growth *growth;
    struct rf_weaver_stroke *strokes;
    struct rf_weaver_ray_cache *rays;
    unsigned count, material_count;
    struct rf_weaver_material materials[RF_WEAVER_MAX_MATERIALS];
    int minimum[3], maximum[3];
};
struct rf_weaver_transform { double rotation[9], position[3]; };
/* All transforms and beam endpoints are machine-local, at 8192 units/metre.
 * Bones: tray 0; yokes 1..8; cores 9..16; petals 17..40; gun 41. */
struct rf_weaver_pose {
    struct rf_weaver_transform bones[RF_WEAVER_MAX_BONES];
    double beam_start[8][3], beam_end[8][3];
    unsigned beam_face[8]; /* UINT_MAX when no real surface is being traced. */
    unsigned beam_mask;
    double opening, reveal;
};
void rf_weaver_pose_sample(const struct rf_mesh_weaver_frame *,
    const struct rf_weaver_mesh *weapon, struct rf_weaver_pose *);

/* The immutable cache retains real triangle topology. A weapon is converted by
 * the same physical-size adapter as held weapons; machine components preserve
 * their authored origin and units (weapon=-1). No costs are inferred here. */
int rf_weaver_mesh_load(struct rf_weaver_mesh *, const char *path, int weapon);
void rf_weaver_mesh_free(struct rf_weaver_mesh *);
void rf_weaver_transform_identity(struct rf_weaver_transform *);
void rf_weaver_transform_point(const struct rf_weaver_transform *,
    const double source[3], double output[3]);
void rf_weaver_transform_multiply(struct rf_weaver_transform *out,
    const struct rf_weaver_transform *a, const struct rf_weaver_transform *b);
void rf_weaver_transform_axis(struct rf_weaver_transform *, int axis, double radians);

#endif
