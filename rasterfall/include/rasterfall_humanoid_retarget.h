#ifndef TOYC_RASTERFALL_HUMANOID_RETARGET_H
#define TOYC_RASTERFALL_HUMANOID_RETARGET_H

#include "rasterfall_humanoid_basis.h"

struct rasterfall_humanoid_rotation_skeleton {
    double rest_global[RASTERFALL_HUMANOID_BONE_COUNT][4];
    int parent[RASTERFALL_HUMANOID_BONE_COUNT];
};

struct rasterfall_humanoid_rotation_pose {
    double global[RASTERFALL_HUMANOID_BONE_COUNT][4];
};

struct rasterfall_humanoid_retarget_result {
    double local_rotation[RASTERFALL_HUMANOID_BONE_COUNT][4];
    double global_rotation[RASTERFALL_HUMANOID_BONE_COUNT][4];
};

int rasterfall_humanoid_retarget_rotations(
    const struct rasterfall_humanoid_rotation_skeleton *source,
    const struct rasterfall_humanoid_rotation_pose *source_pose,
    const struct rasterfall_humanoid_rest_basis *source_basis,
    const struct rasterfall_humanoid_rotation_skeleton *target,
    const struct rasterfall_humanoid_rest_basis *target_basis,
    struct rasterfall_humanoid_retarget_result *result);
int rasterfall_humanoid_retarget_rotations_from_reference(
    const struct rasterfall_humanoid_rotation_skeleton *source,
    const struct rasterfall_humanoid_rotation_pose *source_pose,
    const struct rasterfall_humanoid_rotation_pose *source_reference,
    unsigned int reference_mask,
    const struct rasterfall_humanoid_rest_basis *source_basis,
    const struct rasterfall_humanoid_rotation_skeleton *target,
    const struct rasterfall_humanoid_rotation_pose *target_reference,
    const struct rasterfall_humanoid_rest_basis *target_basis,
    struct rasterfall_humanoid_retarget_result *result);

void rasterfall_humanoid_rotation_skeleton_identity(
    struct rasterfall_humanoid_rotation_skeleton *skeleton);
void rasterfall_humanoid_rotation_pose_bind(
    const struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose);
void rasterfall_humanoid_synthetic_delta(int bone, int degrees,
                                         double delta[4]);
int rasterfall_humanoid_retarget_logic_test(void);

#endif
