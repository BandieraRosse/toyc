#ifndef RF_GPU_SCENE_POSE_H
#define RF_GPU_SCENE_POSE_H
#include "rf_gpu_scene_local.h"
#include "rasterfall_model.h"
#include "rasterfall_character.h"
#include "rasterfall_render.h"

/* Bounded rifleman fixture payload, not a GPU resource table. Resource IDs
 * belong to the character catalog; Core must resolve/pin them before submit. */
#define RF_GPU_SCENE_POSE_BONES 256
struct rf_gpu_scene_pose_v1 {
    uint32_t abi_version, byte_size;
    uint64_t frame_id, world_generation;
    struct rf_gpu_scene_actor_identity identity;
    uint32_t actor_count, bone_count, body_resource_id, attachment_count;
    uint32_t bind_normals; /* Frozen legacy normal policy from finalized pose. */
    struct rasterfall_rigid_transform body_to_world;
    /* Weapon catalog ID plus raw RMESH-local -> world; includes authored
     * centering/basis and PRIMARY_GRIP alignment exactly once. */
    int weapon_valid, weapon;
    struct rasterfall_rigid_transform weapon_to_world;
    struct rasterfall_model_skin_palette_bone palette[RF_GPU_SCENE_POSE_BONES];
    struct {
        uint32_t resource_id, host_socket;
        struct rasterfall_rigid_transform model_to_world;
    } attachments[RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS];
};
/* Value-only, transactional output. Re-evaluation does not advance clocks or
 * borrow mutable instances from the old slot-indexed renderer. Unsupported
 * actors fail explicitly; no procedural fallback or GPU submission occurs. */
int rf_gpu_scene_pose_extract(const struct rf_gpu_scene_local_frame *frame,
                             struct rf_gpu_scene_pose_v1 *out);
int rf_gpu_scene_pose_logic_test(void);
struct rasterfall_model_asset *rf_gpu_scene_fixture_map(void);
int rf_gpu_scene_native_fixture(int frames, int fault, int fault_frame);
#endif
