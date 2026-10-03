#ifndef RASTERFALL_RIFLE_POSE_H
#define RASTERFALL_RIFLE_POSE_H
#include "toy_game.h"

struct rasterfall_model_instance;
enum rasterfall_rifle_idle_pose {
    RASTERFALL_RIFLE_IDLE_SINGLE,
    RASTERFALL_RIFLE_IDLE_CHEST,
    RASTERFALL_RIFLE_IDLE_LOW,
    RASTERFALL_RIFLE_IDLE_COUNT
};
/* Frozen, presentation-only inputs. Angles are millidegrees. */
struct rasterfall_rifle_pose_input {
    int aim_milli, pitch_mdeg, yaw_mdeg, recoil_milli;
    int armor_milli, target_distance_rfu;
    int hip_milli; /* Low moving hold; tracks the same target as shoulder aim. */
    int idle_milli[RASTERFALL_RIFLE_IDLE_COUNT];
};
struct rasterfall_rifle_history {
    int valid, actor_id;
    unsigned generation, tick;
    int aim_milli, hip_milli;
    int idle_milli[RASTERFALL_RIFLE_IDLE_COUNT];
    int idle_active, idle_pose, weapon;
    unsigned idle_random, idle_until;
};
struct rasterfall_rifle_diagnostics {
    double stock_target[3], stock_actual[3], muzzle_direction[3];
    double grip_error[2], wrist_dot[2];
    double clearance_rfu, clearance_shift_rfu, aim_error_degrees;
    double reach_shift_rfu;
    int reach_clamped[2];
};
void rasterfall_rifle_sample(const struct toy_game_actor *actor,unsigned tick,
    struct rasterfall_rifle_history *history,struct rasterfall_rifle_pose_input *out);
int rasterfall_rifle_pose_solve(struct rasterfall_model_instance *instance,
    int weapon,int character_scale,const struct rasterfall_rifle_pose_input *input,
    struct rasterfall_rifle_diagnostics *diagnostics);
#endif
