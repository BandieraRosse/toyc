#ifndef TOYC_RASTERFALL_GLB_ANIMATION_H
#define TOYC_RASTERFALL_GLB_ANIMATION_H

#include "rasterfall_humanoid_retarget.h"

struct rasterfall_glb_rotation_clip {
    void *implementation;
    int duration_ms;
    int rotation_channels;
    int active_rotation_bones;
    int min_rotation_keys;
    int max_rotation_keys;
};

struct rasterfall_glb_rotation_trace {
    double rest_local[RASTERFALL_HUMANOID_BONE_COUNT][4];
    double animated_local[RASTERFALL_HUMANOID_BONE_COUNT][4];
    double rest_global[RASTERFALL_HUMANOID_BONE_COUNT][4];
    double animated_global[RASTERFALL_HUMANOID_BONE_COUNT][4];
};

int rasterfall_glb_rotation_clip_load(struct rasterfall_glb_rotation_clip *clip,
                                      const char *path,const char *name);
void rasterfall_glb_rotation_clip_unload(struct rasterfall_glb_rotation_clip *clip);
int rasterfall_glb_rotation_clip_source(
    const struct rasterfall_glb_rotation_clip *clip,int time_ms,
    struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose,
    struct rasterfall_humanoid_rest_basis *basis,
    int *sampled_time_ms);
int rasterfall_glb_rotation_clip_reference(
    const struct rasterfall_glb_rotation_clip *clip,
    struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose,
    struct rasterfall_humanoid_rest_basis *basis);
int rasterfall_glb_rotation_clip_trace(
    const struct rasterfall_glb_rotation_clip *clip,int time_ms,
    struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose,
    struct rasterfall_humanoid_rest_basis *basis,
    struct rasterfall_glb_rotation_trace *trace,int *sampled_time_ms);

#endif
