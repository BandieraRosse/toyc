#ifndef TOYC_RASTERFALL_MODEL_H
#define TOYC_RASTERFALL_MODEL_H

#include "toy_assets.h"
#include "toy_renderer.h"
#include "rasterfall_animation.h"
#include "rasterfall_humanoid.h"
#include "rasterfall_humanoid_basis.h"

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
 *   optional SKN1 section      v11 bone hierarchy + BDEF1/BDEF2 records
 *
 * Header byte 60 is the SKN1 offset in v11.  SKN1 has a 32-byte header
 * (total bytes, bone count/stride, vertex count/stride, name-table bytes),
 * followed by 24-byte bones (parent, flags, absolute rest position, name
 * offset), 8-byte vertex weights (bone0, bone1, Q16 weight0, BDEF type), and
 * a NUL-terminated UTF-8 name table.  Earlier versions end after indices.
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
#define RASTERFALL_MODEL_VERSION 11
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
#define RASTERFALL_MODEL_BONE_BYTES 24
#define RASTERFALL_MODEL_SKIN_VERTEX_BYTES 8
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
};

struct rasterfall_model_bone_transform {
    double rotation[9];
    double position[3];
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
int rasterfall_model_update_bones(struct rasterfall_model_asset *asset);
int rasterfall_model_skin_vertex(const struct rasterfall_model_asset *asset,
                                 unsigned int index, int position[3],
                                 int normal[3]);
int rasterfall_model_find_bone(const struct rasterfall_model_asset *asset,
                               const char *name);
void rasterfall_model_dump_bones(const struct rasterfall_model_asset *asset,
                                 const char *search);
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
int rasterfall_humanoid_logic_test(void);
int rasterfall_model_skinning_logic_test(void);

#endif
