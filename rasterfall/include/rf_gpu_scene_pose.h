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
    int character_id;
    uint32_t shirt_color, pants_color;
    uint32_t clothing_count, body_hidden_material_mask;
    uint32_t clothing_resources[RASTERFALL_CHARACTER_RECIPE_CLOTHING];
    uint32_t bind_normals; /* Frozen legacy normal policy from finalized pose. */
    int scene_light_q8;
    struct rasterfall_rigid_transform body_to_world;
    /* Weapon catalog ID plus raw RMESH-local -> world; includes authored
     * centering/basis and PRIMARY_GRIP alignment exactly once. */
    int weapon_valid, weapon;
    struct rasterfall_rigid_transform weapon_to_world;
    int muzzle_flash, weapon_muzzle[3]; /* Frozen presentation flash only. */
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
int rf_gpu_scene_pose_extract_at(const struct rf_gpu_scene_local_frame *frame,
    uint32_t actor_index,struct rf_gpu_scene_pose_v1 *out);
int rf_gpu_scene_pose_logic_test(void);
void rf_gpu_scene_pose_body_release(void);
/* Catalog body preview: independent instance, authored colors, no gameplay ID. */
int rf_gpu_scene_pose_body(int body_id, uint64_t frame, uint64_t world,
    uint64_t time_ms, int walk, int x, int y, int z, int cy,
    struct rf_gpu_scene_pose_v1 *out);
enum rf_gpu_scene_body_action {
    RF_GPU_SCENE_BODY_IDLE, RF_GPU_SCENE_BODY_WALK,
    RF_GPU_SCENE_BODY_RIFLE_IDLE, RF_GPU_SCENE_BODY_RIFLE_AIM,
    RF_GPU_SCENE_BODY_WALK_FIRE, RF_GPU_SCENE_BODY_WALK_RIFLE
};
/* Catalog previews share the gameplay action, grip solver and weapon source. */
struct rf_gpu_scene_body_sample {
    uint64_t lower_time_ms;
    int action_time_ms, walk, armed, fire;
    struct rasterfall_rifle_pose_input rifle;
};
/* Explicit presentation sample; no environment, gameplay or clock reads. */
int rf_gpu_scene_pose_body_sample(int body_id, uint64_t frame, uint64_t world,
    const struct rf_gpu_scene_body_sample *sample, int x, int y, int z,
    int sy, int cy, struct rf_gpu_scene_pose_v1 *out);
int rf_gpu_scene_pose_body_action(int body_id, uint64_t frame, uint64_t world,
    uint64_t time_ms, int action, int x, int y, int z, int cy,
    const struct rasterfall_rifle_pose_input *rifle, struct rf_gpu_scene_pose_v1 *out);
struct rasterfall_model_asset *rf_gpu_scene_fixture_map(void);
int rf_gpu_scene_native_fixture(int frames, int fault, int fault_frame);
int rf_gpu_scene_lighting_fixture(void);
#endif
