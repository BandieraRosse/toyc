#ifndef TOYC_RASTERFALL_MODEL_H
#define TOYC_RASTERFALL_MODEL_H

#include "toy_assets.h"
#include "toy_renderer.h"
#include "rasterfall_animation.h"
#include "rasterfall_humanoid.h"
#include "rasterfall_humanoid_basis.h"

struct rasterfall_glb_rotation_clip;
struct rasterfall_glb_rotation_reference;
struct rasterfall_vmd_clip;

/*
 * RFM2 is the deliberately small runtime mesh format produced by
 * the offline model converters.  All integers are little-endian.  The file
 * layout is:
 *
 *   header[64]                 magic/version/counts/scale/bounds/offsets
 *   primitive_count * 16       first index/count/material index
 *   material_count * 16/24/40  base/sphere/toon material data; v8 adds edge
 *                              data, v9 adds ambient/specular parameters
 *   vertex_count * 24/32/36 bytes x,y,z: int32; nx,ny,nz: int16; u,v: uint16;
 *                              v6 adds signed Q16 additional u,v at 24..31;
 *                              v10 adds unsigned Q16 edge scale at 32..35
 *   index_count * 4 bytes      uint32 triangle indices
 *   optional SKN1 section      v11 bone hierarchy + BDEF1/BDEF2 records;
 *                              v12 appends an IK2 metadata section;
 *                              v13 appends grant parent/ratio to bone records
 *
 * Header byte 60 is the SKN1 offset in v11.  SKN1 has a 32-byte header
 * (total bytes, bone count/stride, vertex count/stride, name-table bytes),
 * followed by v11/v12 24-byte bones (parent, flags, absolute rest position,
 * name offset), or v13 32-byte bones with grant parent (int32, -1 sentinel)
 * and grant ratio (float32) at bytes 24..31; then 8-byte vertex weights
 * (bone0, bone1, Q16 weight0, BDEF type), and a NUL-terminated UTF-8 name
 * table. Earlier versions end after indices.
 *
 * Positions are Rasterfall world units.  Normals are signed Q15 and UVs are
 * unsigned Q16 (the exporter clamps UVs to [0, 1]). Multiple glTF primitives
 * are concatenated into one RFM2 mesh; their material assignments are stored
 * and rendered by Rasterfall's material path. Material bytes 8..11 hold a
 * texture-table index (0xffffffff means no texture). Version 4 stores PMX
 * material alpha in byte 4 (0..255). Version 5 stores the toon index in byte
 * 5 and its kind in byte 6 (0 none, 1 texture-table, 2 shared). Version 7
 * stores PMX material drawing flags in byte 7 (bit 0 means double-sided).
 * Bytes 12..15 contain a packed sphere-table index in bits 0..15 and PMX sphere mode in bits 16..17.
 * Texture files are kept beside the mesh by the offline importer.
 */
#define RASTERFALL_MODEL_MAGIC 0x324d4652U /* "RFM2" in little-endian */
#define RASTERFALL_MODEL_VERSION 13
#define RASTERFALL_MODEL_VERTEX_BYTES 24
#define RASTERFALL_MODEL_VERTEX_BYTES_ADDITIONAL_UV 32
#define RASTERFALL_MODEL_VERTEX_BYTES_EDGE_SCALE 36
#define RASTERFALL_MODEL_HEADER_BYTES 64
#define RASTERFALL_MODEL_PRIMITIVE_BYTES 16
#define RASTERFALL_MODEL_MATERIAL_BYTES_LEGACY 16
#define RASTERFALL_MODEL_MATERIAL_BYTES_EDGE 24
#define RASTERFALL_MODEL_MATERIAL_BYTES 40
#define RASTERFALL_MODEL_SKIN_MAGIC 0x314e4b53U /* "SKN1" */
#define RASTERFALL_MODEL_SKIN_HEADER_BYTES 32
#define RASTERFALL_MODEL_BONE_BYTES_LEGACY 24
#define RASTERFALL_MODEL_BONE_BYTES 32
#define RASTERFALL_MODEL_SKIN_VERTEX_BYTES 8
#define RASTERFALL_MODEL_IK_MAGIC 0x324b4932U /* "2IK2" */
#define RASTERFALL_MODEL_IK_HEADER_BYTES 24
#define RASTERFALL_MODEL_IK_RECORD_BYTES 24
#define RASTERFALL_MODEL_IK_LINK_BYTES 32
#define RASTERFALL_MODEL_MAX_BONES 4096
#define RASTERFALL_MODEL_MAX_BONE_DEPTH 512

enum rasterfall_model_pose {
    RASTERFALL_MODEL_POSE_BIND,
    RASTERFALL_MODEL_POSE_RIGHT_ARM,
    RASTERFALL_MODEL_POSE_ARMS,
    RASTERFALL_MODEL_POSE_BODY_TURN
};

struct rasterfall_model_bone {
    int parent;
    int rest_x, rest_y, rest_z;
    unsigned int flags;
    const char *name;
    int rotate_x, rotate_y, rotate_z;
    int animation_x, animation_y, animation_z;
    int grant_parent;
    float grant_ratio;
    int grant_rotation_enabled;
    int grant_translation_enabled;
};

struct rasterfall_model_bone_transform {
    double rotation[9];
    double position[3];
};

struct rasterfall_model_ik_link {
    int bone;
    int limited;
    float lower[3];
    float upper[3];
};

struct rasterfall_model_ik {
    int controller;
    int target;
    int iterations;
    float angle;
    unsigned int link_count;
    struct rasterfall_model_ik_link *links;
};

struct rasterfall_model_header {
    unsigned int magic;
    unsigned int version;
    unsigned int vertex_count;
    unsigned int index_count;
    unsigned int position_scale;
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
};

#define RASTERFALL_MODEL_MAX_GALLERY 128

struct rasterfall_model_asset {
    const unsigned char *data;
    int data_size;
    unsigned int format_version;
    unsigned int vertex_count;
    unsigned int index_count;
    unsigned int primitive_count;
    unsigned int material_count;
    unsigned int material_bytes;
    unsigned int vertex_bytes;
    const unsigned char *primitives;
    const unsigned char *materials;
    const unsigned char *vertices;
    const unsigned char *indices;
    const unsigned char *skin_vertices;
    struct rasterfall_model_bone *bones;
    struct rasterfall_model_bone_transform *bone_transforms;
    unsigned int *bone_order;
    unsigned int bone_count;
    struct rasterfall_model_ik *iks;
    unsigned int ik_count;
    int ik_enabled;
    int grant_enabled;
    int grant_pose_applied;
    int ik_limits_enabled;
    int ik_synthetic_target;
    int ik_synthetic_side;
    int ik_analytic_geometry_dump;
    int ik_analytic_pole_override;
    double ik_analytic_pole[3];
    int ik_analytic_probe_ran;
    int ik_analytic_probe_raw_knee_x;
    int ik_analytic_probe_knee_valid;
    double ik_analytic_probe_ankle_error;
    int ik_diagnostic_dump;
    int ik_target_space_diagnostic;
    /* Inspector-only solver diagnostics.  These remain disabled in runtime. */
    int ik_warm_start_diagnostic;
    int ik_legacy_knee_ccd;
    int ik_diagnostic_reverse_order;
    int ik_diagnostic_knee_scale_milli;
    int ik_diagnostic_thigh_scale_milli;
    int ik_iteration_trace_time_ms;
    int ik_warm_start_valid[2];
    int ik_warm_start_thigh[2][3];
    int ik_warm_start_knee[2][3];
    double ik_synthetic_offset[3];
    unsigned long ik_sample_count;
    unsigned long ik_controller_sample_count;
    unsigned long ik_analytic_solved_count;
    unsigned long ik_analytic_clamped_count;
    unsigned long ik_analytic_rejected_count;
    unsigned long ik_iteration_total;
    unsigned int ik_iteration_max;
    double ik_error_before_total;
    double ik_error_after_total;
    double ik_error_before_max;
    double ik_error_after_max;
    unsigned long ik_reach_sample_count;
    unsigned long ik_unreachable_count;
    double ik_reach_distance_total;
    double ik_reach_ratio_total;
    double ik_reach_distance_max;
    double ik_reach_ratio_max;
    unsigned long center_ab_samples;
    unsigned long center_ab_a_unreachable;
    unsigned long center_ab_b_unreachable;
    double center_ab_a_ratio_total;
    double center_ab_b_ratio_total;
    double center_ab_a_ratio_max;
    double center_ab_b_ratio_max;
    double center_ab_a_excess_total;
    double center_ab_b_excess_total;
    double center_ab_a_excess_max;
    double center_ab_b_excess_max;
    unsigned int root_bone_count;
    unsigned int max_bone_depth;
    int skinning_enabled;
    int pose;
    int demo_right_arm;
    int demo_left_arm;
    int demo_body;
    struct rasterfall_animation_rotation *animation_rotations;
    struct rasterfall_animation_clip demo_clips[3];
    struct rasterfall_animation_track demo_tracks[3][2];
    struct rasterfall_animation_keyframe demo_keys[3][6];
    struct toy_texture_asset *texture_assets;
    struct toy_texture_view *texture_views;
    unsigned int texture_count;
    int min_x, min_y, min_z;
    int max_x, max_y, max_z;
    /* Presentation-only Center/Groove offset, in RFM2 units. */
    int animation_offset_x, animation_offset_y, animation_offset_z;
    int vmd_skeleton_translation_enabled;
    int vmd_center_translation[3];
    int vmd_groove_translation[3];
};

int rasterfall_model_load(struct rasterfall_model_asset *asset,
                           const char *path);
void rasterfall_model_unload(struct rasterfall_model_asset *asset);
int rasterfall_model_set_skinning(struct rasterfall_model_asset *asset,
                                  int enabled);
int rasterfall_model_set_pose(struct rasterfall_model_asset *asset, int pose);
int rasterfall_model_build_demo_clips(struct rasterfall_model_asset *asset);
int rasterfall_model_sample_clip(struct rasterfall_model_asset *asset,
                                 const struct rasterfall_animation_clip *clip,
                                 int time_ms);
void rasterfall_model_dump_ik_hierarchy(const struct rasterfall_model_asset *asset);
void rasterfall_model_reset_center_ab_diagnostic(struct rasterfall_model_asset *asset);
void rasterfall_model_center_ab_diagnostic(struct rasterfall_model_asset *asset,
                                           const struct rasterfall_vmd_clip *vmd,
                                           int time_ms, int print_sample);
void rasterfall_model_print_center_ab_diagnostic(
    const struct rasterfall_model_asset *asset);
void rasterfall_model_set_ik_enabled(struct rasterfall_model_asset *asset,
                                     int enabled);
void rasterfall_model_set_grant_enabled(struct rasterfall_model_asset *asset,
                                        int enabled);
void rasterfall_model_set_legacy_knee_ccd(struct rasterfall_model_asset *asset,
                                           int enabled);
int rasterfall_model_apply_rotation_grants(struct rasterfall_model_asset *asset);
void rasterfall_model_set_vmd_skeleton_translation(
    struct rasterfall_model_asset *asset, const int center[3],
    const int groove[3], int enabled);
int rasterfall_model_sample_glb_rotation_clip(
    struct rasterfall_model_asset *asset,
    const struct rasterfall_glb_rotation_clip *clip,
    const struct rasterfall_glb_rotation_reference *source_reference,
    int time_ms);
int rasterfall_model_update_bones(struct rasterfall_model_asset *asset);
int rasterfall_model_skin_vertex(const struct rasterfall_model_asset *asset,
                                 unsigned int index, int position[3],
                                 int normal[3]);
int rasterfall_model_find_bone(const struct rasterfall_model_asset *asset,
                               const char *name);
void rasterfall_model_dump_bones(const struct rasterfall_model_asset *asset,
                                 const char *search);
void rasterfall_model_dump_ik(const struct rasterfall_model_asset *asset);
const char *rasterfall_humanoid_bone_name(enum rasterfall_humanoid_bone bone);
void rasterfall_humanoid_mapping_init(struct rasterfall_humanoid_mapping *mapping);
void rasterfall_humanoid_map_eula(const struct rasterfall_model_asset *asset,
                                  struct rasterfall_humanoid_mapping *mapping);
void rasterfall_humanoid_validate(const struct rasterfall_model_asset *asset,
                                  const struct rasterfall_humanoid_mapping *mapping,
                                  struct rasterfall_humanoid_diagnostics *diagnostics);
void rasterfall_model_dump_humanoid(const struct rasterfall_model_asset *asset);
int rasterfall_model_build_humanoid_bases(
    const struct rasterfall_model_asset *asset,
    struct rasterfall_humanoid_rest_basis *bases);
void rasterfall_model_dump_humanoid_bases(
    const struct rasterfall_model_asset *asset);
int rasterfall_model_retarget_synthetic_test(
    const struct rasterfall_model_asset *asset, const char *action);
int rasterfall_model_glb_animation_test(struct rasterfall_model_asset *asset,
                                        const char *glb_path,const char *clip_name);
int rasterfall_model_glb_motion_diagnostic(struct rasterfall_model_asset *asset,
                                           const char *glb_path);
int rasterfall_humanoid_logic_test(void);
int rasterfall_model_skinning_logic_test(void);

#endif
