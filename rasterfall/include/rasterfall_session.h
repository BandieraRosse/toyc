#ifndef RASTERFALL_SESSION_H
#define RASTERFALL_SESSION_H

#include "core.h"
#include "toy_game.h"
#include "toy_map.h"
#include "rasterfall_camera.h"
#include "rasterfall_map.h"

#define RASTERFALL_PLAYER_RADIUS 180
#define RASTERFALL_MOVE_STEP 76
#define RASTERFALL_INTERACT_RANGE 1000

enum rasterfall_command_button {
    RASTERFALL_CMD_FIRE       = 1 << 0,
    RASTERFALL_CMD_RELOAD     = 1 << 1,
    RASTERFALL_CMD_SLOT_1     = 1 << 2,
    RASTERFALL_CMD_SLOT_2     = 1 << 3,
    RASTERFALL_CMD_INTERACT   = 1 << 4,
    RASTERFALL_CMD_TURN_LEFT  = 1 << 5,
    RASTERFALL_CMD_TURN_RIGHT = 1 << 6,
    RASTERFALL_CMD_RESET      = 1 << 7,
    RASTERFALL_CMD_SHOVE      = 1 << 8
};

/* 与窗口系统无关的单个逻辑步输入。以后网络客户端发送的也是这类游戏语义，
 * 而不是 evdev 键码或 toy_input 的内部数组。 */
struct rasterfall_command {
    int move_forward;
    int move_strafe;
    int turn;
    int pitch;
    unsigned int buttons;
    int fire_held;
};

struct rasterfall_session {
    struct toy_map level;
    struct toy_game game_state;
    struct toy_game_box bounds[TOY_MAP_MAX_BOXES];
    struct toy_game_box safe_rooms[TOY_MAP_MAX_ZONES];
    struct toy_game_box spawn_zones[TOY_MAP_MAX_ZONES];
    int spawn_count;
    int base_actor_indices[TOY_MAP_MAX_BASES];
    struct rasterfall_interactable items[TOY_MAP_MAX_PICKUPS];
    int item_count;
    struct rasterfall_map_state map_ops;

    int air_walls_enabled;
    int manual_alarm_on;
    int manual_alarm_timer;
    int highlight_index;
    int banner_ms;
    const char *banner_text;
    int smooth_turn_remaining;
    int ai_revive_active;
    int ai_revive_actor_index;
    uint64_t seed;
};

int rasterfall_session_load(struct rasterfall_session *session,
                            const char *map_path);
void rasterfall_session_unload(struct rasterfall_session *session);
void rasterfall_session_reset(struct rasterfall_session *session,
                              struct camera *camera, uint64_t seed);
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
/* 只推进远端玩家的移动与朝向，不重复推进敌人/director。主机用它验证
 * 客户端移动；射击和互动要等多玩家规则状态接入后再由权威会话处理。 */
void rasterfall_session_step_remote_player(struct rasterfall_session *session,
                                           struct camera *camera,
                                           const struct rasterfall_command *command,
                                           int remote_down);
/* 主机验证远端玩家的交互按键，沿用同一视锥/距离判定。 */
void rasterfall_session_interact_remote(struct rasterfall_session *session,
                                         const struct camera *camera);
int rasterfall_session_revive_remote(struct rasterfall_session *session,
                                     const struct camera *camera, int dt_ms);
int rasterfall_session_revive_player(struct rasterfall_session *session,
                                     const struct camera *rescuer,
                                     const struct camera *target,
                                     int *progress_ms, int dt_ms);
void rasterfall_camera_rotate(struct camera *camera, int turn, int pitch);
int rasterfall_session_compute_highlight(const struct rasterfall_session *session,
                                         const struct camera *camera);

#endif
