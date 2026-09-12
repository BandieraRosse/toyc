#ifndef RASTERFALL_SESSION_H
#define RASTERFALL_SESSION_H

#include "core.h"
#include "toy_game.h"
#include "toy_map.h"
#include "rasterfall_camera.h"
#include "rasterfall_map.h"
#include "rasterfall_ai.h"
#include "rasterfall_animation.h"
#include "rasterfall_animation_composition.h"
#include "rasterfall_calibration.h"
#include "rasterfall_roster.h"
#include "rasterfall_world_content.h"

#define RASTERFALL_PLAYER_RADIUS 180
#define RASTERFALL_MOVE_STEP TOY_CONFIG_PLAYER_MOVE_STEP
#define RASTERFALL_INTERACT_RANGE 1000
#define RASTERFALL_MAX_FLAGS 8
#define RASTERFALL_PAID_REVIVE_COST 20
#define RASTERFALL_HURD_SQUAD_SIZE 4
#define RASTERFALL_MAID_SQUAD_SIZE 4
#define RASTERFALL_MAID_FLAG_INDEX 1
#define RASTERFALL_HURD_FLAG_INDEX 2
#define RASTERFALL_STANDARD_FLAG_INDEX 3
#define RASTERFALL_ASSAULT_FLAG_INDEX 4

struct rasterfall_flag {
    int active;
    int x, z;
    int carried;
    int carrier_id;
    int color;
    char label[5];
    int slot_offsets[4][2];
};

/* Fixed session identity/configuration for the Hurd relay.  Assignment is
 * still owned solely by actor.flag_index; these indices let later mission
 * orchestration address the four story actors without duplicating it. */
struct rasterfall_hurd_outpost {
    int flag_index;
    int minx, maxx, minz, maxz;
    int squad_actor_indices[RASTERFALL_HURD_SQUAD_SIZE];
};

/* Derived gameplay truth.  assigned_count and capable_count deliberately
 * retain distinct meanings: DOWNED actors remain assigned but not capable. */
struct rasterfall_hurd_status {
    int flag_deployed_in_region;
    int assigned_count;
    int capable_count;
    int controlled;
};

struct rasterfall_squad_runtime {
    int actor_indices[RASTERFALL_SQUAD_SIZE];
};

enum rasterfall_command_button {
    RASTERFALL_CMD_FIRE       = 1 << 0,
    RASTERFALL_CMD_RELOAD     = 1 << 1,
    RASTERFALL_CMD_SLOT_1     = 1 << 2,
    RASTERFALL_CMD_SLOT_2     = 1 << 3,
    RASTERFALL_CMD_SLOT_3     = 1 << 14,
    RASTERFALL_CMD_SLOT_4     = 1 << 15,
    RASTERFALL_CMD_INTERACT   = 1 << 4,
    RASTERFALL_CMD_TURN_LEFT  = 1 << 5,
    RASTERFALL_CMD_TURN_RIGHT = 1 << 6,
    RASTERFALL_CMD_RESET      = 1 << 7,
    RASTERFALL_CMD_SHOVE      = 1 << 8,
    RASTERFALL_CMD_JUMP       = 1 << 9,
    RASTERFALL_CMD_CLEAR_STATS = 1 << 10,
    RASTERFALL_CMD_FLAG       = 1 << 11,
    RASTERFALL_CMD_SHOP       = 1 << 12,
    RASTERFALL_CMD_REVIVE     = 1 << 13
};

/* Stable single-player shop vocabulary.  The UI may use pages and selections,
 * but game logic and AI use these semantic operations instead. */
enum rasterfall_shop_action {
    RASTERFALL_SHOP_BUY_WEAPON = 1,
    RASTERFALL_SHOP_HIRE_AI = 2,
    RASTERFALL_SHOP_BUY_FLAG = 3,
    RASTERFALL_SHOP_ASSIGN_AI = 4,
    RASTERFALL_SHOP_UPGRADE_AI = 5,
    RASTERFALL_SHOP_CHANGE_AI_WEAPON = 6
};

struct rasterfall_shop_request {
    int action;
    int item;
    int target_actor;
    int arg;
};

struct rasterfall_session;

/* Query is side-effect free.  It returns whether the request is currently
 * executable and optionally exposes the price that execute will charge. */
int rasterfall_session_shop_can(const struct rasterfall_session *session,
                                const struct rasterfall_shop_request *request,
                                int *price);
int rasterfall_session_shop_execute(struct rasterfall_session *session,
                                    const struct rasterfall_shop_request *request);

/* 与窗口系统无关的单个逻辑步输入。以后网络客户端发送的也是这类游戏语义，
 * 而不是 evdev 键码或 toy_input 的内部数组。 */
struct rasterfall_command {
    int move_forward;
    int move_strafe;
    int turn;
    int pitch;
    unsigned int buttons;
    int fire_held;
    int shop_action;
    int shop_item;
    int shop_arg;
    unsigned int shop_request_id;
    /* Captured once when a jump command is built. */
    int jump_dx;
    int jump_dz;
    int pose_debug_action;
    int pose_editor_action;
};

enum rasterfall_pose_debug_action { RASTERFALL_POSE_DEBUG_NONE, RASTERFALL_POSE_DEBUG_PREV_BONE, RASTERFALL_POSE_DEBUG_NEXT_BONE, RASTERFALL_POSE_DEBUG_AXIS_X, RASTERFALL_POSE_DEBUG_AXIS_Y, RASTERFALL_POSE_DEBUG_AXIS_Z, RASTERFALL_POSE_DEBUG_DECREASE, RASTERFALL_POSE_DEBUG_INCREASE, RASTERFALL_POSE_DEBUG_EXPORT, RASTERFALL_POSE_DEBUG_TOGGLE_LAYER };
enum rasterfall_humanoid_debug_action { RASTERFALL_HUMANOID_DEBUG_IDLE, RASTERFALL_HUMANOID_DEBUG_WALK, RASTERFALL_HUMANOID_DEBUG_AIM, RASTERFALL_HUMANOID_DEBUG_RECOIL, RASTERFALL_HUMANOID_DEBUG_ACTION_COUNT };

struct rasterfall_session {
    enum rasterfall_world_id world_id;
    struct rasterfall_world_content content;
    enum rasterfall_world_id world_request;
    int world_request_pending;
    int station_gui_request;
    /* Existing gameplay view projected from map_ops.runtime.  Do not treat
     * this toy_map storage as the authoritative map representation. */
    struct toy_map level;
    struct toy_game game_state;
    /* Gameplay-facing projection arrays retained until gameplay migrates. */
    struct toy_game_box safe_rooms[TOY_MAP_MAX_ZONES];
    struct toy_game_box spawn_zones[TOY_MAP_MAX_ZONES];
    int spawn_count;
    /* Projection of Runtime Map pickups/interactions for current session UI. */
    struct rasterfall_interactable items[TOY_MAP_MAX_PICKUPS];
    int item_count;
    struct rasterfall_map_state map_ops;
    struct rasterfall_ai_registry ai_registry;
    int managed_ai_enabled;
    int managed_ai_route_phase;
    int managed_ai_target_index;
    int managed_ai_retarget_ms;
    int managed_ai_escape_phase;
    /* 托管玩家的武器大师：0..3 依次对应 SMG/SG/AK/AWP，4 表示最终回到 AK。 */
    int managed_ai_weapon_master_target;
    int managed_ai_weapon_master_route;
    int managed_ai_ammo_rest_wave;

    int air_walls_enabled;
    int manual_alarm_on;
    int manual_alarm_timer;
    int highlight_index;
    int banner_ms;
    const char *banner_text;
    int banner_success;
    int smooth_turn_remaining;
    int ai_revive_active;
    int ai_revive_actor_index;
    int shop_open;
    int shop_page;
    int shop_selected;
    int shop_nav_selected;
    int shop_scroll;
    int shop_request_only;
    struct rasterfall_flag flags[RASTERFALL_MAX_FLAGS];
    int flag_count;
    int carried_flag;
    int assignment_flag;
    struct rasterfall_hurd_outpost hurd_outpost;
    struct rasterfall_squad_runtime squad_runtime[RASTERFALL_SQUAD_COUNT];
    int skeletal_demo_pose;
    struct rasterfall_animation_player skeletal_demo_player;
    int humanoid_debug_action, humanoid_debug_time_ms;
    int pose_debug_active, pose_debug_bone, pose_debug_axis, pose_debug_layer;
    struct rasterfall_rifle_pose rifle_pose, hit_pose;
    struct rasterfall_calibration_state pose_editor;
    uint64_t seed;
};

int rasterfall_session_load(struct rasterfall_session *session,
                            const char *map_path);
int rasterfall_session_load_legacy(struct rasterfall_session *session,
                                   const char *map_path);
const struct toy_game_actor *rasterfall_session_local_player_const(
    const struct rasterfall_session *session);
void rasterfall_session_unload(struct rasterfall_session *session);
int rasterfall_session_request_world(struct rasterfall_session *session,
                                     enum rasterfall_world_id world);
int rasterfall_session_take_world_request(struct rasterfall_session *session,
                                          enum rasterfall_world_id *world);
void rasterfall_session_reset(struct rasterfall_session *session,
                              struct camera *camera, uint64_t seed);
/* 单人托管玩家开关。启用后，step 会用最小托管策略生成玩家命令。 */
int rasterfall_session_set_managed_ai(struct rasterfall_session *session,
                                      int active);
/* 旁观者的托管 AI 脱困命令：同步重置相机、逻辑位置和击飞状态。 */
int rasterfall_session_recover_managed_actor(
    struct rasterfall_session *session, struct camera *camera);
void rasterfall_session_step(struct rasterfall_session *session,
                             struct camera *camera,
                             const struct rasterfall_command *command,
                             int dt_ms);
int rasterfall_session_dev_killall(struct rasterfall_session *session);
void rasterfall_session_dev_give_money(struct rasterfall_session *session,
                                        int amount);
/* 客户端预测：只推进本地玩家和武器，不推进敌人/波次，也不在本地
 * 修改敌人生命；敌人命中结果由主机快照决定。 */
void rasterfall_session_step_client(struct rasterfall_session *session,
                                     struct camera *camera,
                                     const struct rasterfall_command *command,
                                     int dt_ms);
void rasterfall_session_replay_client(struct rasterfall_session *session,
                                      struct camera *camera,
                                      const struct rasterfall_command *command,
                                      int dt_ms);
/* 主机验证远端 actor 的交互按键，沿用同一视锥/距离判定。 */
void rasterfall_session_interact_remote(struct rasterfall_session *session,
                                        const struct camera *camera,
                                        int expected_kind);
void rasterfall_session_toggle_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id);
void rasterfall_session_update_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id);
int rasterfall_session_revive_remote(struct rasterfall_session *session,
                                     const struct camera *camera, int dt_ms);
int rasterfall_session_revive_target(struct rasterfall_session *session,
                                     const struct camera *rescuer,
                                     const struct camera *target,
                                     int *progress_ms, int dt_ms);
int rasterfall_session_paid_revive(struct rasterfall_session *session,
                                   struct camera *camera);
int rasterfall_session_find_down_ai(const struct rasterfall_session *session,
                                    const struct camera *camera);
void rasterfall_camera_rotate(struct camera *camera, int turn, int pitch);
int rasterfall_session_compute_highlight(const struct rasterfall_session *session,
                                         const struct camera *camera);
void rasterfall_session_shop_input(struct rasterfall_session *session,
                                   int up, int down, int left, int right,
                                   int enter, int esc);
int rasterfall_session_shop_request(struct rasterfall_session *session,
                                    int action, int item, int arg);
int rasterfall_session_shop_actor_at(const struct rasterfall_session *session,
                                     int flag_index, int selection);
void rasterfall_session_hurd_status(
    const struct rasterfall_session *session,
    struct rasterfall_hurd_status *status);

#endif
