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

int rasterfall_glb_rotation_clip_load(struct rasterfall_glb_rotation_clip *clip,
                                      const char *path,const char *name);
void rasterfall_glb_rotation_clip_unload(struct rasterfall_glb_rotation_clip *clip);
int rasterfall_glb_rotation_clip_source(
    const struct rasterfall_glb_rotation_clip *clip,int time_ms,
    struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose,
    struct rasterfall_humanoid_rest_basis *basis,
    int *sampled_time_ms);

#endif
