#ifndef RASTERFALL_VMD_H
#define RASTERFALL_VMD_H

#include "rasterfall_animation.h"
struct rasterfall_model_asset;

#define RASTERFALL_VMD_MAX_BONES 256
#define RASTERFALL_VMD_MAX_NAME 64

struct rasterfall_vmd_keyframe {
    int frame;
    float tx, ty, tz;
    struct rasterfall_animation_quaternion rotation;
    unsigned char interpolation[64];
};

struct rasterfall_vmd_bone_track {
    char name[RASTERFALL_VMD_MAX_NAME];
    int key_count;
    struct rasterfall_vmd_keyframe *keys;
    struct rasterfall_animation_keyframe *animation_keys;
    int first_frame, last_frame;
    int translation_changed, rotation_changed;
    int target_bone;
    int mapping_status; /* 0 missing, 1 exact, 2 ignored, 3 duplicate */
    int is_ik, is_center, is_groove;
};

struct rasterfall_vmd_clip {
    char model_name[RASTERFALL_VMD_MAX_NAME];
    int version;
    int motion_count;
    int track_count;
    int max_frame;
    int duration_ms;
    int interpolation_tracks;
    int use_bezier_interpolation;
    int ignored_ik_tracks;
    struct rasterfall_vmd_bone_track *tracks;
};

int rasterfall_vmd_load(struct rasterfall_vmd_clip *clip, const char *path);
void rasterfall_vmd_unload(struct rasterfall_vmd_clip *clip);
int rasterfall_vmd_map_eula(struct rasterfall_vmd_clip *clip,
                            const struct rasterfall_model_asset *asset);
void rasterfall_vmd_dump(const struct rasterfall_vmd_clip *clip,
                         const struct rasterfall_model_asset *asset);
void rasterfall_vmd_dump_motion_diagnostic(
    const struct rasterfall_vmd_clip *clip,
    const struct rasterfall_model_asset *asset);
int rasterfall_vmd_build_animation(const struct rasterfall_vmd_clip *vmd,
                                   struct rasterfall_animation_clip *clip,
                                   struct rasterfall_animation_track *tracks,
                                   int track_capacity);
void rasterfall_vmd_set_linear_interpolation(struct rasterfall_vmd_clip *vmd,
                                             int linear);
void rasterfall_vmd_sample_translation(const struct rasterfall_vmd_clip *vmd,
                                       int time_ms, int out[3]);
void rasterfall_vmd_dump_translation_diagnostic(
    const struct rasterfall_vmd_clip *vmd);
int rasterfall_vmd_logic_test(void);

#endif
