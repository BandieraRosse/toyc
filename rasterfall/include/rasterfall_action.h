#ifndef TOYC_RASTERFALL_ACTION_H
#define TOYC_RASTERFALL_ACTION_H

#include "rasterfall_animation.h"
#include "rasterfall_humanoid.h"

#define RASTERFALL_ACTION_FORMAT_VERSION 1
#define RASTERFALL_ACTION_NAME_BYTES 48
#define RASTERFALL_ACTION_SKELETON_BYTES 48
#define RASTERFALL_ACTION_MAX_TRACKS 32
#define RASTERFALL_ACTION_MAX_KEYS 256

enum rasterfall_action_id {
    RASTERFALL_ACTION_NONE,
    RASTERFALL_ACTION_RIFLE_IDLE,
    RASTERFALL_ACTION_COUNT
};

enum rasterfall_action_interpolation {
    RASTERFALL_ACTION_STEP,
    RASTERFALL_ACTION_LINEAR
};

struct rasterfall_action_track {
    enum rasterfall_humanoid_bone target;
    enum rasterfall_action_interpolation interpolation;
    unsigned int first_key, key_count;
};

struct rasterfall_action_clip {
    enum rasterfall_action_id id;
    char name[RASTERFALL_ACTION_NAME_BYTES];
    char skeleton[RASTERFALL_ACTION_SKELETON_BYTES];
    int duration_ms, loop;
    struct rasterfall_action_track tracks[RASTERFALL_ACTION_MAX_TRACKS];
    unsigned int track_count;
    struct rasterfall_animation_keyframe keys[RASTERFALL_ACTION_MAX_KEYS];
    unsigned int key_count;
};

struct rasterfall_model_instance;

const char *rasterfall_action_id_name(enum rasterfall_action_id id);
int rasterfall_action_load(struct rasterfall_action_clip *clip, const char *path);
int rasterfall_action_validate(const struct rasterfall_action_clip *clip);
int rasterfall_action_apply(struct rasterfall_model_instance *instance,
                            const struct rasterfall_action_clip *clip,
                            int time_ms);
void rasterfall_action_dump(const struct rasterfall_action_clip *clip);
int rasterfall_action_pose_debug(const struct rasterfall_model_instance *instance,
                                 const char *role_name);
int rasterfall_action_logic_test(void);

#endif
