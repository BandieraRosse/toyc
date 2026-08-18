#ifndef RASTERFALL_SESSION_H
#define RASTERFALL_SESSION_H

#include "core.h"
#include "toy_game.h"
#include "toy_map.h"
#include "rasterfall_camera.h"
#include "rasterfall_map.h"
#include "rasterfall_ai.h"

#define RASTERFALL_PLAYER_RADIUS 180
#define RASTERFALL_MOVE_STEP TOY_CONFIG_PLAYER_MOVE_STEP
#define RASTERFALL_INTERACT_RANGE 1000
#define RASTERFALL_MAX_FLAGS 8
#define RASTERFALL_PAID_REVIVE_COST 20

struct rasterfall_flag {
    int active;
    int x, z;
    int carried;
    int carrier_id;
    int color;
    char label[5];
    int slot_offsets[4][2];
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
};

struct rasterfall_session {
    struct toy_map level;
    struct toy_game game_state;
    struct toy_game_box bounds[TOY_MAP_MAX_BOXES];
    struct toy_game_box safe_rooms[TOY_MAP_MAX_ZONES];
    struct toy_game_box spawn_zones[TOY_MAP_MAX_ZONES];
    int spawn_count;
    struct rasterfall_interactable items[TOY_MAP_MAX_PICKUPS];
    int item_count;
    struct rasterfall_map_state map_ops;
    struct rasterfall_ai_registry ai_registry;
    int managed_ai_enabled;
    int managed_ai_route_phase;
    int managed_ai_target_index;
    int managed_ai_retarget_ms;
    int managed_ai_escape_phase;

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
    uint64_t seed;
};

int rasterfall_session_load(struct rasterfall_session *session,
                            const char *map_path);
void rasterfall_session_unload(struct rasterfall_session *session);
void rasterfall_session_reset(struct rasterfall_session *session,
                              struct camera *camera, uint64_t seed);
/* 单人托管玩家开关。启用后，step 会用最小托管策略生成玩家命令。 */
int rasterfall_session_set_managed_ai(struct rasterfall_session *session,
                                      int active);
/* 旁观者的托管 AI 脱困命令：同步重置相机、逻辑位置和击飞状态。 */
int rasterfall_session_recover_managed_player(
    struct rasterfall_session *session, struct camera *camera);
void rasterfall_session_step(struct rasterfall_session *session,
                             struct camera *camera,
                             const struct rasterfall_command *command,
                             int dt_ms);
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
/* 只推进远端玩家的移动与朝向，不重复推进敌人/director。主机用它验证
 * 客户端移动；射击和互动要等多玩家规则状态接入后再由权威会话处理。 */
void rasterfall_session_step_remote_player(struct rasterfall_session *session,
                                           struct camera *camera,
                                           const struct rasterfall_command *command,
                                           int remote_down, int ground_y);
/* 主机验证远端玩家的交互按键，沿用同一视锥/距离判定。 */
void rasterfall_session_interact_remote(struct rasterfall_session *session,
                                         const struct camera *camera);
void rasterfall_session_toggle_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id);
void rasterfall_session_update_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id);
int rasterfall_session_revive_remote(struct rasterfall_session *session,
                                     const struct camera *camera, int dt_ms);
int rasterfall_session_revive_player(struct rasterfall_session *session,
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

#endif
