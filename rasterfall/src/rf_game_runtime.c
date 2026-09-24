#include "rasterfall_enemy_visual.h"
#include "tlibc_everything.h"
#include "rf_gpu_scene_pose.h"
#include "rf_gpu_scene_enemy.h"
#include "rf_gpu_scene_world.h"
#include "rasterfall_world_content.h"
#include <limits.h>
/*
 * rasterfall — Toyc 软件渲染第一人称僵尸射击游戏
 *
 * 架构速览：
 *   rasterfall.c 是协调层，负责 Wayland 窗口、输入、固定逻辑步和主循环。
 *   rasterfall_session 连接地图、相机与游戏状态；lib/game.c 是无窗口的规则核心，
 *   处理玩家、武器、碰撞、AI、波次和胜负。lib/map.c 与 rasterfall_map.c 负责地图。
 *   rasterfall_render.c、rasterfall_hud.c、rasterfall_viewmodel.c 和 effects.c
 *   负责软件渲染与画面表现；audio.c 负责音效；net.c 负责 UDP 联机。
 *
 * 每帧流程：输入事件 → rasterfall_command → session 更新 toy_game → 音频/特效事件
 * → 场景、敌人、武器和 HUD 渲染。资源位于 assets/；程序保持 freestanding，不依赖
 * SDL、OpenGL 或宿主 libc。
 *
 * 阅读入口：先看 rasterfall_session.h，再看 lib/game.c、rasterfall_map.c 和
 * rasterfall_render.c；net.c、audio.c 分别是联机和音频边界。
 *
 * 命令行选项：
 *   --host                         创建主机房间
 *   --connect <ip>                 连接指定主机
 *   --port <port>                  使用指定 UDP 端口
 *   --net-loss <percent>           模拟网络丢包
 *   --auto                         自动化压测模式
 *   --textures / --no-textures     开启/关闭纹理渲染
 *   --no-edge-pass                 关闭模型 edge pass
 *   --no-stats                     关闭性能统计
 *   --texture-stats                显示纹理统计
 *   --dump-frame <path>            导出帧图像
 *   --model-views <model> <dir>     离屏导出模型正面/侧面/背面图
 *   --model-views-supersample <1|2> 设置离屏模型视图内部采样倍率
 *   --model-material-regression <model> <dir>
 *                                   导出四组材质基线和像素统计清单
 *   --model-performance <model> [iterations] [workers]
 *                                   固定视角模型功能与 worker 负载基准
 *   --actor-performance [iterations] [frontend-workers] [raster-workers]
 *                                   1280x720 五角色固定工作负载基准
 *   --model-humanoid <model>        输出通用 Humanoid 骨骼映射诊断
 *   --model-humanoid-basis <model>  输出 canonical rest basis
 *   --model-retarget-test <model> <right-arm|left-arm|right-leg|chest>
 *   --model-glb-animation <model> <glb> <clip>
 *   --model-glb-motion-diagnostic <model> <glb>
 *   legacy VMD options             old PMX/VMD compatibility diagnostics
 *   --frames <count>               运行指定帧数后退出
 *   --input-test                   输入调试测试
 *   --logic-test                   运行逻辑和网络回归测试
 *   --help                         显示完整选项说明
 */

#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_renderer.h"
#include "toy_assets.h"
#include "toy_input.h"
#include "toy_game.h"
#include "fb_draw.h"
#include "fb_font.h"
#include "toy_map.h"
#include "rasterfall_map.h"
#include "rasterfall_hud.h"
#include "rasterfall_audio.h"
#include "rasterfall_effects.h"
#include "rasterfall_sky.h"
#include "rasterfall_viewmodel.h"
#include "rasterfall_render.h"
#include "rasterfall_session.h"
#include "rasterfall_net.h"
#include "rasterfall_humanoid_retarget.h"
#include "rasterfall_character.h"
#include "rasterfall_prop.h"
#include "rasterfall_console.h"
#include "rasterfall_units.h"
#include "rasterfall_animation_composition.h"
#include "rasterfall_action.h"
#include "rasterfall_options.h"
#include "rf_core_host.h"
#include "rf_core_mixed_frame.h"
#include "rf_game_lifecycle.h"
#include "rf_application_projection.h"
#include "rasterfall_feature_freeze.h"
#ifdef TOYC_WINDOWS
#include "rf_gpu_vulkan_backend.h"
#include "rf_gpu_scene_world_gpu.h"
#endif
#include "math.h"

#define KEY_ESC   1
#define KEY_1     2
#define KEY_2     3
#define KEY_3     4
#define KEY_4     5
#define KEY_MINUS 12
#define KEY_EQUAL 13
#define KEY_E     18
#define KEY_F     33
#define KEY_B     48
#define KEY_C     46
#define KEY_G     34
#define KEY_H     35
#define KEY_I     23
#define KEY_J     36
#define KEY_K     37
#define KEY_L     38
#define KEY_M     50
#define KEY_N     49
#define KEY_O     24
#define KEY_P     25
#define KEY_Q     16
#define KEY_R     19
#define KEY_T     20
#define KEY_U     22
#define KEY_V     47
#define KEY_X     45
#define KEY_Y     21
#define KEY_Z     44
#define KEY_Z     44
#define KEY_ENTER 28
#define KEY_TAB   15
#define KEY_W     17
#define KEY_A     30
#define KEY_S     31
#define KEY_D     32
#define KEY_SPACE 57
#define KEY_LEFTSHIFT 42
#define KEY_COMMA 51
#define KEY_DOT   52
#define KEY_SLASH 53
#define KEY_BACKSPACE 14
#define KEY_F2      60
#define KEY_F12     88
#define KEY_UP    103
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_DOWN  108
#define KEY_GRAVE 41
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111

#define FIXED_STEP_US 16667
#define MAX_FRAME_US 250000
#define MAX_LOGIC_STEPS 4
#define NEAR_Z 192
#define ENEMY_RENDER_DISTANCE 24000 /* 3x the old 8000-unit enemy cutoff */
#define UV_ONE 65536

struct vec3 { int x, y, z; };
/* 朝向：sy/cy = 偏航 sin/cos，pitch_sy/pitch_cy = 俯仰 sin/cos（均 1024 定点）。 */
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };
struct control_settings { int mouse_level, keyboard_level; };
struct pause_menu { int selected; };
struct managed_terminal {
    int open;
    char line[32];
    char message[96];
};

#ifdef TOYC_WINDOWS
static void rf_windows_log(const char *message) { toy_windows_log(message); }
#else
static void rf_windows_log(const char *message) { (void)message; }
#endif

#define PAUSE_ITEM_RESUME   0
#define PAUSE_ITEM_MOUSE    1
#define PAUSE_ITEM_COORDS   2
#define PAUSE_ITEM_KEYBOARD 3
#define PAUSE_ITEM_EXIT    4
#define PAUSE_ITEM_COUNT    5

enum rasterfall_startup_screen {
    RASTERFALL_STARTUP_MAIN,
    RASTERFALL_STARTUP_MANUAL_IP,
    RASTERFALL_STARTUP_PUBLIC_ROOM,
    RASTERFALL_STARTUP_LAN_ROOMS
};

static struct rasterfall_session session;
#define level_map (session.level)
#define map_safe_rooms (session.safe_rooms)
#define map_spawn_zones (session.spawn_zones)
#define map_spawn_count (session.spawn_count)
#define game (session.game_state)
#define air_wall_enabled (session.air_walls_enabled)
typedef struct rasterfall_interactable interactable;
#define interactables (session.items)
#define interactable_count (session.item_count)
#define manual_alarm_enabled (session.manual_alarm_on)
#define manual_alarm_timer_ms (session.manual_alarm_timer)
static struct toy_texture_asset model_texture;
static struct toy_texture_view model_texture_view;
/* Toyc 自托管版的纹理光栅化仍有运行时崩溃风险；纯色路径作为稳定默认值。
 * 调试纹理渲染时可显式传入 --textures。 */
#ifdef TOYC_WINDOWS
/* The MinGW build uses the host compiler/runtime and supports the complete
 * texture path.  Keep Windows models textured without requiring a hidden
 * command-line switch. */
static int textures_enabled = 1;
#else
static int textures_enabled = 0;
#endif
/* The active effect pool is owned by rf_game_runtime.  Helpers in this
 * translation unit use the active facade during the host loop; keeping the
 * indirection here avoids creating a second presentation pool. */
static struct rasterfall_effects *active_effects;
#define effects (*active_effects)

/* 主循环仍需要少量坐标和特效同步辅助；几何绘制本身位于
 * rasterfall_render.c。 */
static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    fb_put_pixel((unsigned char *)surface->pixels, x, y, color,
                 surface->stride);
}

static void view_to_world(const struct camera *camera, const struct vec3 *view,
                          struct vec3 *world)
{
    int vx = view->x, vy = view->y, vz = view->z;
    int wy = (vy * camera->pitch_cy + vz * camera->pitch_sy) / 1024;
    int wz = (-vy * camera->pitch_sy + vz * camera->pitch_cy) / 1024;
    world->x = camera->x + (vx * camera->cy + wz * camera->sy) / 1024;
    world->z = camera->z + (-vx * camera->sy + wz * camera->cy) / 1024;
    /* View-space Y is relative to the camera.  Omitting camera->y makes a
     * platform muzzle snap back to ground height while ground-level shots
     * appear correct because camera->y happens to be zero. */
    world->y = camera->y + wy;
}

static void copy_vec3(struct vec3 *out, const struct vec3 *in)
{
    out->x = in->x; out->y = in->y; out->z = in->z;
}

static void near_intersection(const struct vec3 *a, const struct vec3 *b,
                              struct vec3 *out)
{
    long numerator = RASTERFALL_NEAR_Z - a->z;
    long denominator = b->z - a->z;
    out->x = a->x + (int)(((long)b->x - (long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long)b->y - (long)a->y) *
                          numerator / denominator);
    out->z = RASTERFALL_NEAR_Z;
}

static int clip_near(const struct vec3 *input, int count,
                     struct vec3 *output)
{
    int out_count = 0;
    struct vec3 previous;
    copy_vec3(&previous, &input[count - 1]);
    int previous_inside = previous.z >= RASTERFALL_NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct vec3 current;
        copy_vec3(&current, &input[i]);
        int current_inside = current.z >= RASTERFALL_NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection(&previous, &current, &output[out_count++]);
        if (current_inside) copy_vec3(&output[out_count++], &current);
        copy_vec3(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

/* A deliberately tiny offline-style light bake.  The map is static, so this
 * is generated once when the level is loaded and then only sampled while
 * recording triangles.  Box proximity gives walls and cover a soft contact
 * shadow without a runtime shadow map. */
/* Only the distant room boundary uses the stylized wall texture. Gameplay
 * cover remains flat-shaded so texture sampling does not dominate the
 * software rasterizer. */
#define horde_banner_ms (session.banner_ms)
#define interaction_banner (session.banner_text)
#define highlighted (session.highlight_index)

#undef game
#undef interactables
#undef interactable_count
#undef highlighted
#undef manual_alarm_enabled
#undef manual_alarm_timer_ms
#undef horde_banner_ms
#undef interaction_banner
static void fill_hud_state(struct rasterfall_hud_state *hud,
                           const struct rasterfall_net *net_state,
                           const char *host_address, int host_port,
                           const struct camera *camera)
{
    int player_target = -1;
    long player_target_d2 = 0;
    int player_progress = 0;
    int player_active = 0;
    int revive_index = rasterfall_session_find_down_ai(&session, camera);
    int i;
    memset(hud, 0, sizeof(*hud));
    if (net_state && net_state->mode != RASTERFALL_NET_OFF) {
        if (net_state->mode == RASTERFALL_NET_HOST) {
            for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
                const struct rasterfall_net_client *client =
                    &net_state->clients[i];
                int actor_index = TOY_GAME_REMOTE_ACTOR_BASE +
                                  client->client_id - 1;
                const struct toy_game_actor *actor;
                long dx, dz, d2;
                if (!client->active || !client->connected || actor_index < 0 ||
                    actor_index >= TOY_GAME_MAX_ACTORS)
                    continue;
                actor = &session.game_state.actors[actor_index];
                if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER ||
                    actor->state != TOY_GAME_ACTOR_DOWNED)
                    continue;
                dx = (long)camera->x - client->camera.x;
                dz = (long)camera->z - client->camera.z;
                d2 = dx * dx + dz * dz;
                if (d2 > (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE ||
                    (player_target >= 0 && d2 >= player_target_d2)) continue;
                player_target = client->client_id;
                player_target_d2 = d2;
                player_progress = actor->revive_progress_ms;
                player_active = (net_state->host_revive_active &&
                                 net_state->host_revive_target_id ==
                                     client->client_id) ||
                                player_progress > 0;
            }
        } else {
            for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
                const struct toy_game_actor *actor;
                int actor_index;
                long dx, dz, d2;
                if (i == net_state->local_player_id) continue;
                actor_index = TOY_GAME_REMOTE_ACTOR_BASE + i - 1;
                if (actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS) continue;
                actor = &session.game_state.actors[actor_index];
                if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER ||
                    actor->state != TOY_GAME_ACTOR_DOWNED) continue;
                dx = (long)camera->x - net_state->remote_render_camera[i].x;
                dz = (long)camera->z - net_state->remote_render_camera[i].z;
                d2 = dx * dx + dz * dz;
                if (d2 > (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE ||
                    (player_target >= 0 && d2 >= player_target_d2)) continue;
                player_target = i;
                player_target_d2 = d2;
                player_progress = actor->revive_progress_ms;
                player_active = player_progress > 0;
            }
        }
    }
    hud->game = &session.game_state;
    hud->map = &level_map;
    hud->safe_rooms = map_safe_rooms;
    hud->player_name = toy_game_local_player_actor_const(
        &session.game_state)->name;
    hud->interactables = session.items;
    hud->interactable_count = session.item_count;
    hud->highlighted = session.highlight_index;
    hud->air_walls_enabled = air_wall_enabled;
    hud->manual_alarm_enabled = session.manual_alarm_on;
    hud->manual_alarm_timer_ms = session.manual_alarm_timer;
    hud->ai_revive_active = session.ai_revive_active;
    hud->ai_revive_available = revive_index >= 0;
    hud->ai_revive_progress_ms = session.ai_revive_active &&
        session.ai_revive_actor_index >= 0 ?
        session.game_state.actors[session.ai_revive_actor_index].revive_progress_ms : 0;
    hud->ai_revive_name = revive_index >= 0 ?
        session.game_state.actors[revive_index].name : NULL;
    hud->player_revive_available = player_target >= 0;
    hud->player_revive_active = player_active;
    hud->player_revive_progress_ms = player_progress;
    hud->player_revive_name = player_target >= 0 ? "PLAYER" : NULL;
    hud->horde_banner_ms = session.banner_ms;
    hud->interaction_banner = session.banner_text;
    hud->interaction_banner_success = session.banner_success;
    hud->net = net_state;
    hud->host_address = host_address;
    hud->host_port = host_port;
    hud->shop_open = session.shop_open;
    hud->shop_page = session.shop_page;
    hud->shop_selected = session.shop_selected;
    hud->shop_nav_selected = session.shop_nav_selected;
    hud->shop_scroll = session.shop_scroll;
    hud->flag_count = session.flag_count;
    hud->assignment_flag = session.assignment_flag;
    hud->flag_carried = session.carried_flag >= 0;
    hud->pose_debug_active = session.pose_debug_active;
    hud->pose_debug_bone = session.pose_debug_bone;
    hud->pose_debug_axis = session.pose_debug_axis;
    hud->rifle_pose = &session.rifle_pose;
    hud->pose_debug_layer = session.pose_debug_layer;
    hud->hit_pose = &session.hit_pose;
    hud->pose_editor = &session.pose_editor;
    for (i = 0; i < session.flag_count && i < 8; i++)
        hud->flag_colors[i] = session.flags[i].color;
    hud->flag_near = 0;
    for (i = 0; i < session.flag_count; i++) {
        long dx = (long)camera->x - session.flags[i].x;
        long dz = (long)camera->z - session.flags[i].z;
        if (!session.flags[i].carried &&
            dx * dx + dz * dz <= (long)RASTERFALL_INTERACT_RANGE *
                                  RASTERFALL_INTERACT_RANGE)
            hud->flag_near = 1;
    }
}
#define game (session.game_state)
#define interactables (session.items)
#define interactable_count (session.item_count)
#define highlighted (session.highlight_index)
#define manual_alarm_enabled (session.manual_alarm_on)
#define manual_alarm_timer_ms (session.manual_alarm_timer)
#define horde_banner_ms (session.banner_ms)
#define interaction_banner (session.banner_text)

static int64_t diagnostic_monotonic_us(void)
{
    return rf_core_clock_now_us();
}

static int parse_positive_int(const char *text, int fallback)
{
    int value = 0;
    if (!text || !*text) return fallback;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        text++;
    }
    if (*text || value <= 0 || value > 65535) return fallback;
    return value;
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void set_network_spectator_camera(struct camera *camera,
                                         const struct rasterfall_net *net)
{
    int target_id = -1;
    int i;
    int distance = 1250;
    if (!camera || !net) return;
    if (net->mode == RASTERFALL_NET_CLIENT &&
        net->local_player_id >= 0 &&
        net->local_player_id < RASTERFALL_NET_PLAYER_MAX &&
        game.actors[net->local_player_id == 0 ?
                    TOY_GAME_PLAYER_ACTOR_INDEX :
                    TOY_GAME_REMOTE_ACTOR_BASE + net->local_player_id - 1].state ==
            TOY_GAME_ACTOR_DOWNED) {
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
            int actor_index = i == 0 ? TOY_GAME_PLAYER_ACTOR_INDEX :
                              TOY_GAME_REMOTE_ACTOR_BASE + i - 1;
            if (i != net->local_player_id &&
                actor_index >= 0 && actor_index < TOY_GAME_MAX_ACTORS &&
                game.actors[actor_index].active &&
                game.actors[actor_index].kind == TOY_GAME_ACTOR_PLAYER &&
                game.actors[actor_index].state != TOY_GAME_ACTOR_DOWNED) {
                target_id = i;
                break;
            }
        }
        if (target_id < 0) return;
    } else if (net->mode == RASTERFALL_NET_HOST &&
               toy_game_local_player_actor_const(&game)->state ==
                   TOY_GAME_ACTOR_DOWNED) {
        for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
            if (net->clients[i].active && net->clients[i].connected &&
                net->clients[i].client_id > 0 &&
                net->clients[i].client_id <= RASTERFALL_NET_CLIENT_MAX &&
                game.actors[TOY_GAME_REMOTE_ACTOR_BASE +
                            net->clients[i].client_id - 1].active &&
                game.actors[TOY_GAME_REMOTE_ACTOR_BASE +
                            net->clients[i].client_id - 1].state ==
                    TOY_GAME_ACTOR_ALIVE) {
                camera->x = net->clients[i].camera.x -
                    net->clients[i].camera.sy * distance / 1024;
                camera->z = net->clients[i].camera.z -
                    net->clients[i].camera.cy * distance / 1024;
                camera->sy = net->clients[i].camera.sy;
                camera->cy = net->clients[i].camera.cy;
                camera->pitch_sy = net->clients[i].camera.pitch_sy;
                camera->pitch_cy = net->clients[i].camera.pitch_cy;
                camera->y = net->clients[i].camera.y;
                return;
            }
        return;
    } else return;
    camera->x = net->remote_render_camera[target_id].x -
        net->remote_render_camera[target_id].sy * distance / 1024;
    camera->z = net->remote_render_camera[target_id].z -
        net->remote_render_camera[target_id].cy * distance / 1024;
    camera->sy = net->remote_render_camera[target_id].sy;
    camera->cy = net->remote_render_camera[target_id].cy;
    camera->pitch_sy = net->remote_render_camera[target_id].pitch_sy;
    camera->pitch_cy = net->remote_render_camera[target_id].pitch_cy;
    camera->y = net->remote_render_camera[target_id].y;
}

static void set_managed_spectator_camera(struct camera *render_camera,
                                         const struct camera *body_camera,
                                         int third_person)
{
    if (!render_camera || !body_camera || !third_person) return;
    render_camera->x = body_camera->x - body_camera->sy * 1100 / 1024;
    render_camera->z = body_camera->z - body_camera->cy * 1100 / 1024;
    render_camera->sy = body_camera->sy;
    render_camera->cy = body_camera->cy;
    render_camera->y = body_camera->y + 650;
    /* Keep the body in view from slightly above; positive pitch looks down. */
    render_camera->pitch_sy = 230;
    render_camera->pitch_cy = 998;
}


static int sensitivity_percent(int level)
{
    return 50 + clampi(level, 0, 15) * 10;
}

static void accumulate_mouse_look(int *pending_turn, int *pending_pitch,
                                  int relative_x, int relative_y,
                                  const struct control_settings *settings)
{
    int percent = sensitivity_percent(settings->mouse_level);
    /* 水平/垂直同一倍率，避免方向手感不一致 */
    int turn = relative_x * 9 * percent / 100;
    int pitch = -relative_y * 9 * percent / 100;
    *pending_turn += clampi(turn, -256, 256);
    *pending_pitch += pitch;
}

static void build_game_command(struct rasterfall_command *command,
                               const struct rf_input_frame *input,
                               const struct control_settings *settings,
                               const unsigned char *pending_key_edges,
                               int fire_edge, int shove_edge,
                               int pointer_turn,
                               int pointer_pitch)
{
    int percent = sensitivity_percent(settings->keyboard_level);
    memset(command, 0, sizeof(struct rasterfall_command));
    command->move_forward = toy_input_down(input, KEY_W) -
                            toy_input_down(input, KEY_S);
    command->move_strafe = toy_input_down(input, KEY_D) -
                           toy_input_down(input, KEY_A);
    command->turn = pointer_turn +
                    (toy_input_down(input, KEY_RIGHT) -
                     toy_input_down(input, KEY_LEFT)) * 16 * percent / 100;
    command->pitch = pointer_pitch +
                     (toy_input_down(input, KEY_UP) -
                      toy_input_down(input, KEY_DOWN)) * 16 * percent / 100;
    command->fire_held = toy_input_down(input, KEY_ENTER) ||
                         (input->mouse_buttons & 1) != 0;
    if (fire_edge) command->buttons |= RASTERFALL_CMD_FIRE;
    if (shove_edge) command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_SPACE) || pending_key_edges[KEY_SPACE])
        command->buttons |= RASTERFALL_CMD_JUMP;
    if (toy_input_pressed(input, KEY_SLASH))
        command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_R)) command->buttons |= RASTERFALL_CMD_RELOAD;
    /* A key edge may arrive between fixed ticks (or while the renderer is
     * waiting for a present buffer). Use the retained edge queue so movement
     * cannot make a number press disappear. */
    if (toy_input_pressed(input, KEY_1) || pending_key_edges[KEY_1])
        command->buttons |= RASTERFALL_CMD_SLOT_1;
    if (toy_input_pressed(input, KEY_2) || pending_key_edges[KEY_2])
        command->buttons |= RASTERFALL_CMD_SLOT_2;
    if (toy_input_pressed(input, KEY_3) || pending_key_edges[KEY_3])
        command->buttons |= RASTERFALL_CMD_SLOT_3;
    if (toy_input_pressed(input, KEY_4) || pending_key_edges[KEY_4])
        command->buttons |= RASTERFALL_CMD_SLOT_4;
    if (toy_input_pressed(input, KEY_E) || pending_key_edges[KEY_E])
        command->buttons |= RASTERFALL_CMD_INTERACT;
    if (toy_input_pressed(input, KEY_F)) command->buttons |= RASTERFALL_CMD_FLAG;
    if (session.pose_debug_active && session.pose_editor.active) {
        /* Keep movement, mouse look, and arrow-key look in the ordinary
         * command.  Only gameplay actions are consumed by the editor. */
        command->fire_held = 0;
        command->buttons = 0;
        if (toy_input_pressed(input, KEY_TAB))
            command->pose_editor_action = toy_input_down(input, KEY_LEFTSHIFT) ?
                RASTERFALL_POSE_EDITOR_PREV_PAGE : RASTERFALL_POSE_EDITOR_NEXT_PAGE;
        /* Pose editing deliberately avoids WASD and the arrow keys: movement
         * and camera look remain available while the editor is open. */
        if (toy_input_pressed(input, KEY_COMMA)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_PREV_FIELD;
        if (toy_input_pressed(input, KEY_DOT)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_NEXT_FIELD;
        if (toy_input_pressed(input, KEY_J)) command->pose_editor_action=toy_input_down(input, KEY_LEFTSHIFT)?RASTERFALL_POSE_EDITOR_DECREASE_LARGE:RASTERFALL_POSE_EDITOR_DECREASE;
        if (toy_input_pressed(input, KEY_L)) command->pose_editor_action=toy_input_down(input, KEY_LEFTSHIFT)?RASTERFALL_POSE_EDITOR_INCREASE_LARGE:RASTERFALL_POSE_EDITOR_INCREASE;
        if (toy_input_pressed(input, KEY_R)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_RESET;
        if (toy_input_pressed(input, KEY_P)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_EXPORT;
        if (toy_input_pressed(input, KEY_ESC)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_EXIT;
        if (toy_input_pressed(input, KEY_U)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_TOGGLE_AXES;
        if (toy_input_pressed(input, KEY_O)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_TOGGLE_ANCHORS;
        if (toy_input_pressed(input, KEY_I)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_TOGGLE_IK;
        if (toy_input_pressed(input, KEY_V)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_TOGGLE_ANIMATION_PLAY;
        if (toy_input_pressed(input, KEY_X)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_AXIS_X;
        if (toy_input_pressed(input, KEY_Y)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_AXIS_Y;
        if (toy_input_pressed(input, KEY_Z)) command->pose_editor_action=RASTERFALL_POSE_EDITOR_AXIS_Z;
        return;
    }
    if (session.pose_debug_active) {
        if (toy_input_pressed(input, KEY_N)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_PREV_BONE;
        if (toy_input_pressed(input, KEY_B)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_NEXT_BONE;
        if (toy_input_pressed(input, KEY_X)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_AXIS_X;
        if (toy_input_pressed(input, KEY_Y)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_AXIS_Y;
        if (toy_input_pressed(input, KEY_Z)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_AXIS_Z;
        if (toy_input_pressed(input, KEY_MINUS)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_DECREASE;
        if (toy_input_pressed(input, KEY_EQUAL)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_INCREASE;
        if (toy_input_pressed(input, KEY_P)) command->pose_debug_action=RASTERFALL_POSE_DEBUG_EXPORT;
    }
}

static void capture_jump_vector(struct rasterfall_command *command,
                                const struct camera *camera)
{
    if (!(command->buttons & RASTERFALL_CMD_JUMP)) {
        command->jump_dx = 0;
        command->jump_dz = 0;
        return;
    }
    command->jump_dx = (camera->sy * command->move_forward +
                        camera->cy * command->move_strafe) *
                       RASTERFALL_MOVE_STEP / 1024;
    command->jump_dz = (camera->cy * command->move_forward -
                        camera->sy * command->move_strafe) *
                       RASTERFALL_MOVE_STEP / 1024;
}

static void consume_game_command_edges(struct rf_input_frame *input,
                                       unsigned char *pending_key_edges)
{
    input->key_pressed[KEY_R] = 0;
    input->key_pressed[KEY_1] = 0;
    input->key_pressed[KEY_2] = 0;
    input->key_pressed[KEY_3] = 0;
    input->key_pressed[KEY_4] = 0;
    /* Catch-up can execute several fixed steps in one render iteration.
     * Consume retained slot edges with the first command. */
    pending_key_edges[KEY_1] = 0;
    pending_key_edges[KEY_2] = 0;
    pending_key_edges[KEY_3] = 0;
    pending_key_edges[KEY_4] = 0;
    input->key_pressed[KEY_E] = 0;
    input->key_pressed[KEY_F] = 0;
    input->key_pressed[KEY_SLASH] = 0;
    input->key_pressed[KEY_SPACE] = 0;
    pending_key_edges[KEY_SPACE] = 0;
    pending_key_edges[KEY_E] = 0;
    /* Pose-editor actions are edge events.  Clear their sampled state after
     * the fixed-step command has consumed it; key_down remains untouched, so
     * holding a key never turns into repeated menu navigation. */
    input->key_pressed[KEY_TAB] = 0;
    input->key_pressed[KEY_COMMA] = 0;
    input->key_pressed[KEY_DOT] = 0;
    input->key_pressed[KEY_J] = 0;
    input->key_pressed[KEY_L] = 0;
    input->key_pressed[KEY_U] = 0;
    input->key_pressed[KEY_O] = 0;
    input->key_pressed[KEY_I] = 0;
    input->key_pressed[KEY_P] = 0;
    input->key_pressed[KEY_X] = 0;
    input->key_pressed[KEY_Y] = 0;
    input->key_pressed[KEY_Z] = 0;
}

static void draw_crosshair(struct toy_surface *surface,
                           const struct toy_game *game_state)
{
    int cx = surface->width / 2, cy = surface->height / 2;
    int gap = toy_game_actor_current_spread(
        toy_game_local_player_actor_const(game_state)) * 9 / 20 + 2;
    int length = 5;
    if (gap > 65) gap = 65;
    for (int d = -gap - length; d <= gap + length; d++) {
        if (d < -gap || d > gap) {
            put_pixel(surface, cx + d, cy, 0xF0F0F0);
            put_pixel(surface, cx, cy + d, 0xF0F0F0);
        }
    }
}

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);

struct scoreboard_entry {
    char name[TOY_GAME_MAX_NAME];
    int kills;
    int special_kills;
    int damage_dealt;
    int throwable_damage_dealt;
};

static void scoreboard_add(struct scoreboard_entry *entries, int *count,
                           const char *name, int kills, int special_kills,
                           int damage_dealt, int throwable_damage_dealt)
{
    struct scoreboard_entry *entry;
    if (*count >= TOY_GAME_MAX_ACTORS) return;
    entry = &entries[(*count)++];
    snprintf(entry->name, sizeof(entry->name), "%s", name ? name : "UNKNOWN");
    entry->kills = kills;
    entry->special_kills = special_kills;
    entry->damage_dealt = damage_dealt;
    entry->throwable_damage_dealt = throwable_damage_dealt;
}

static void scoreboard_sort(struct scoreboard_entry *entries, int count)
{
    int i, j;
    for (i = 0; i < count; i++)
        for (j = i + 1; j < count; j++)
            if (entries[j].kills > entries[i].kills) {
                struct scoreboard_entry tmp = entries[i];
                entries[i] = entries[j];
                entries[j] = tmp;
            }
}

static void draw_scoreboard_column(struct toy_surface *surface, int x, int y,
                                   const char *title,
                                   struct scoreboard_entry *entries, int count)
{
    char line[64];
    int i;
    fb_draw_string((unsigned char *)surface->pixels, x, y, title,
                   RF_COLOR_UI_ACCENT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x, y + 22,
                   "NAME         NORMAL SPECIAL TOTAL ITEM DAMAGE",
                   RF_COLOR_UI_TEXT_MUTED, surface->stride);
    for (i = 0; i < count && i < 12; i++) {
        int normal = entries[i].kills - entries[i].special_kills;
        if (normal < 0) normal = 0;
        uint32_t color = i == 0 ? 0xFFD700 :
                         i == 1 ? 0xC0C0C0 :
                         i == 2 ? 0xB5A642 : RF_COLOR_UI_TEXT;
        snprintf(line, sizeof(line), "%-11.11s %6d %7d %5d %4d %6d",
                 entries[i].name, normal, entries[i].special_kills,
                 entries[i].kills, entries[i].throwable_damage_dealt,
                 entries[i].damage_dealt);
        fb_draw_string((unsigned char *)surface->pixels, x, y + 44 + i * 18,
                       line, color, surface->stride);
    }
}

static void draw_scoreboard(struct toy_surface *surface,
                            const struct rasterfall_net *net)
{
    struct scoreboard_entry players[RASTERFALL_NET_PLAYER_MAX];
    struct scoreboard_entry ais[TOY_GAME_MAX_ACTORS];
    int player_count = 0, ai_count = 0, i;
    char name[TOY_GAME_MAX_NAME];
    int x = 20, y = 38, width = surface->width - 40;
    if (net->mode == RASTERFALL_NET_CLIENT) {
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
            int actor_index = i == 0 ? TOY_GAME_PLAYER_ACTOR_INDEX :
                              TOY_GAME_REMOTE_ACTOR_BASE + i - 1;
            const struct toy_game_actor *actor =
                actor_index >= 0 && actor_index < TOY_GAME_MAX_ACTORS ?
                &game.actors[actor_index] : NULL;
            if (!actor || !actor->active ||
                actor->kind != TOY_GAME_ACTOR_PLAYER) continue;
            snprintf(name, sizeof(name), "PLAYER %d%s", i + 1,
                     i == net->local_player_id ? " *" : "");
            scoreboard_add(players, &player_count, name,
                           actor->kills, actor->special_kills,
                           actor->damage_dealt,
                           actor->throwable_damage_dealt);
        }
    } else {
        {
            const struct toy_game_actor *local_player =
                toy_game_local_player_actor_const(&game);
            scoreboard_add(players, &player_count, "PLAYER 1 *",
                           local_player->kills, local_player->special_kills,
                           local_player->damage_dealt,
                           local_player->throwable_damage_dealt);
        }
        for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
            int actor_index = TOY_GAME_REMOTE_ACTOR_BASE +
                              net->clients[i].client_id - 1;
            const struct toy_game_actor *actor;
            if (!net->clients[i].active || !net->clients[i].connected ||
                actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS)
                continue;
            actor = &game.actors[actor_index];
            if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER)
                continue;
            snprintf(name, sizeof(name), "PLAYER %d", net->clients[i].client_id + 1);
            scoreboard_add(players, &player_count, name,
                           actor->kills, actor->special_kills,
                           actor->damage_dealt, actor->throwable_damage_dealt);
        }
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &game.actors[i];
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        scoreboard_add(ais, &ai_count, actor->name[0] ? actor->name : "AI",
                       actor->kills, actor->special_kills,
                       actor->damage_dealt, actor->throwable_damage_dealt);
    }
    scoreboard_sort(players, player_count);
    scoreboard_sort(ais, ai_count);
    fill_rect(surface, x - 3, y - 3, width + 6, 292, RF_COLOR_UI_ACCENT);
    fill_rect(surface, x, y, width, 286, RF_COLOR_UI_BACKGROUND);
    draw_scoreboard_column(surface, x + 18, y + 20, "PLAYERS",
                           players, player_count);
    draw_scoreboard_column(surface, x + width / 2 + 10, y + 20,
                           "AI TEAMMATES", ais, ai_count);
    fb_draw_string((unsigned char *)surface->pixels, x + 18, y + 264,
                   "HOLD TAB   TAB+R CLEAR YOUR STATS",
                   RF_COLOR_UI_TEXT_MUTED, surface->stride);
}

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color)
{
    int right = x + width, bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    for (int py = y; py < bottom; py++)
        for (int px = x; px < right; px++) put_pixel(surface, px, py, color);
}

static void draw_pause_overlay(struct toy_surface *surface,
                               const struct pause_menu *menu,
                               const struct control_settings *settings,
                               int coordinate_axes)
{
    char line[64];
    int panel_w = surface->width * 3 / 5;
    int panel_h = surface->height * 2 / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    int row_y = y + 58;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 6) / 2, y + 28,
                   "PAUSED", RF_COLOR_UI_TEXT, surface->stride);
    for (int item = 0; item < PAUSE_ITEM_COUNT; item++) {
        uint32_t color = item == menu->selected ? RF_COLOR_UI_ACCENT : RF_COLOR_UI_TEXT;
        if (item == menu->selected)
            fill_rect(surface, x + 30, row_y - 3, panel_w - 60,
                      FB_FONT_H + 6, 0x343B49);
        if (item == PAUSE_ITEM_RESUME)
            snprintf(line, sizeof(line), "%c RESUME", item == menu->selected ? '>' : ' ');
        else if (item == PAUSE_ITEM_MOUSE)
            snprintf(line, sizeof(line), "%c MOUSE SENS  < %d%% >",
                     item == menu->selected ? '>' : ' ',
                     sensitivity_percent(settings->mouse_level));
        else if (item == PAUSE_ITEM_COORDS)
            snprintf(line, sizeof(line), "%c COORDINATE AXES < %s >",
                     item == menu->selected ? '>' : ' ',
                     coordinate_axes ? "ON" : "OFF");
        else if (item == PAUSE_ITEM_KEYBOARD)
            snprintf(line, sizeof(line), "%c KEYBOARD SENS < %d%% >",
                     item == menu->selected ? '>' : ' ',
                     sensitivity_percent(settings->keyboard_level));
        else
            snprintf(line, sizeof(line), "%c EXIT GAME",
                     item == menu->selected ? '>' : ' ');
        fb_draw_string((unsigned char *)surface->pixels, x + 42, row_y,
                       line, color, surface->stride);
        row_y += 30;
    }
    fb_draw_string((unsigned char *)surface->pixels, x + 42, y + panel_h - 62,
                   "UP DOWN SELECT  LEFT RIGHT CHANGE", 0xAEB6C2,
                   surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 42, y + panel_h - 38,
                   "ENTER CONFIRM  ESC RESUME", 0xD88A32, surface->stride);
}

static void draw_managed_terminal(struct toy_surface *surface,
                                  const struct managed_terminal *terminal)
{
    int panel_w = surface->width * 3 / 5;
    int panel_h = 170;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    char prompt[64];
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 18,
                   "MANAGED AI TERMINAL", RF_COLOR_UI_ACCENT,
                   surface->stride);
    snprintf(prompt, sizeof(prompt), "> %s_", terminal->line);
    fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 52,
                   prompt, RF_COLOR_UI_TEXT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 78,
                   terminal->message[0] ? terminal->message : "TYPE HELP",
                   RF_COLOR_UI_TEXT_MUTED, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 116,
                   "HELP STATUS UNSTUCK RETURN", 0xAEB6C2, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels, x + 24, y + 140,
                   "ENTER EXECUTE  ESC CLOSE  F2 CLOSE", 0xD88A32,
                   surface->stride);
}

static int managed_terminal_key_char(unsigned int key)
{
    static const int keys[] = {
        KEY_A, KEY_B, KEY_C, KEY_D, KEY_E, KEY_F, KEY_G, KEY_H, KEY_I,
        KEY_J, KEY_K, KEY_L, KEY_M, KEY_N, KEY_O, KEY_P, KEY_Q, KEY_R,
        KEY_S, KEY_T, KEY_U, KEY_V, KEY_W, KEY_X, KEY_Y, KEY_Z
    };
    static const char chars[] = "abcdefghijklmnopqrstuvwxyz";
    for (int i = 0; i < 26; i++)
        if (key == (unsigned int)keys[i]) return chars[i];
    return 0;
}

static int managed_terminal_take_key(struct rf_input_frame *input,
                                     unsigned char *pending,
                                     unsigned int key)
{
    int pressed = pending[key] || toy_input_pressed(input, key);
    if (pressed) {
        pending[key] = 0;
        input->key_pressed[key] = 0;
    }
    return pressed;
}

static void managed_terminal_execute(struct managed_terminal *terminal,
                                     struct rasterfall_session *session,
                                     struct camera *camera)
{
    if (!strcmp(terminal->line, "help"))
        strcpy(terminal->message, "STATUS UNSTUCK RETURN");
    else if (!strcmp(terminal->line, "status"))
        snprintf(terminal->message, sizeof(terminal->message),
                 "POS %d,%d AIR %d ESC %d",
                 camera->x, camera->z,
                 toy_game_local_player_actor_const(&session->game_state)->airborne_ms,
                 session->managed_ai_escape_phase);
    else if (!strcmp(terminal->line, "unstuck") ||
             !strcmp(terminal->line, "return")) {
        if (rasterfall_session_recover_managed_actor(session, camera))
            strcpy(terminal->message, "AI RETURNED TO BASE");
        else
            strcpy(terminal->message, "RECOVERY UNAVAILABLE");
    } else if (terminal->line[0])
        strcpy(terminal->message, "UNKNOWN COMMAND: TYPE HELP");
    else
        strcpy(terminal->message, "TYPE HELP");
    terminal->line[0] = 0;
}

static void managed_terminal_input(struct managed_terminal *terminal,
                                   struct rf_input_frame *input,
                                   unsigned char *pending,
                                   struct rasterfall_session *session,
                                   struct camera *camera)
{
    if (managed_terminal_take_key(input, pending, KEY_ESC) ||
        managed_terminal_take_key(input, pending, KEY_F2)) {
        terminal->open = 0;
        return;
    }
    if (managed_terminal_take_key(input, pending, KEY_ENTER)) {
        managed_terminal_execute(terminal, session, camera);
        return;
    }
    if (managed_terminal_take_key(input, pending, KEY_BACKSPACE)) {
        int length = (int)strlen(terminal->line);
        if (length > 0) terminal->line[length - 1] = 0;
        return;
    }
    for (unsigned int key = 0; key < TOY_INPUT_KEY_COUNT; key++) {
        int ch = managed_terminal_key_char(key);
        int length;
        if (!ch || !managed_terminal_take_key(input, pending, key)) continue;
        length = (int)strlen(terminal->line);
        if (length < (int)sizeof(terminal->line) - 1) {
            terminal->line[length] = (char)ch;
            terminal->line[length + 1] = 0;
        }
        break;
    }
}

static int startup_digit(unsigned int key)
{
    if (key >= 2 && key <= 10) return (int)(key - 1);
    if (key == 11) return 0;
    return -1;
}

static void draw_startup_menu(struct toy_surface *surface, int screen,
                              int selected, const char *address,
                              const char *port_text, int editing_port,
                              const char *room_text,
                              const char *error,
                              const struct rasterfall_net_discovery *discovery)
{
    int i, y = 92;
    int menu_x, menu_w = 440, menu_top;
    char line[96];
    fill_rect(surface, 0, 0, surface->width, surface->height, 0x10151D);
    if (screen == RASTERFALL_STARTUP_MAIN) {
        static const char *items[] = {"CREATE LOCAL ROOM", "WATCH MANAGED AI",
                                      "CREATE PUBLIC ROOM", "JOIN PUBLIC ROOM",
                                      "JOIN LAN ROOM", "JOIN BY ADDRESS", "QUIT"};
        /* Keep the complete menu block centered in the actual surface.  The
         * old coordinates were tuned for 800x450 and left the menu visibly
         * stuck in the upper-left quadrant at the 1280x720 default. */
        if (menu_w > surface->width - 40) menu_w = surface->width - 40;
        menu_x = (surface->width - menu_w) / 2;
        menu_top = (surface->height - 450) / 2;
        if (menu_top < 35) menu_top = 35;
        y = menu_top + 57;
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - fb_string_width("RASTERFALL / 光栅坠落")) / 2,
                       menu_top, "RASTERFALL / 光栅坠落",
                       RF_COLOR_UI_ACCENT, surface->stride);
        for (i = 0; i < 7; i++) {
            uint32_t color = i == selected ? RF_COLOR_UI_ACCENT : RF_COLOR_UI_TEXT;
            if (i == selected)
                fill_rect(surface, menu_x, y + i * 34 - 4, menu_w,
                          FB_FONT_H + 8,
                          0x293746);
            fb_draw_string((unsigned char *)surface->pixels,
                           menu_x + 25, y + i * 34,
                           items[i], color, surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - 34 * FB_FONT_W) / 2,
                       menu_top + 290,
                       "UP DOWN SELECT   ENTER CONFIRM", RF_COLOR_UI_TEXT_MUTED,
                       surface->stride);
        fb_draw_string((unsigned char *)surface->pixels,
                       (surface->width - 27 * FB_FONT_W) / 2,
                       menu_top + 315,
                       "DRAG TITLE AREA TO MOVE", RF_COLOR_UI_TEXT_DIM,
                       surface->stride);
        if (error && error[0])
            fb_draw_string((unsigned char *)surface->pixels,
                           (surface->width - (int)strlen(error) * FB_FONT_W) / 2,
                           menu_top + 340,
                           error, 0xFF8060, surface->stride);
    } else if (screen == RASTERFALL_STARTUP_LAN_ROOMS) {
        int row = 0;
        fb_draw_string((unsigned char *)surface->pixels, 205, 78,
                       "LOCAL ROOMS", RF_COLOR_UI_SECONDARY, surface->stride);
        if (!discovery || discovery->room_count == 0) {
            fb_draw_string((unsigned char *)surface->pixels, 220, 140,
                           "SEARCHING FOR ROOMS...", RF_COLOR_UI_TEXT_MUTED,
                           surface->stride);
        } else {
            for (i = 0; i < RASTERFALL_NET_DISCOVERY_MAX_ROOMS; i++) {
                const struct rasterfall_net_room *room = &discovery->rooms[i];
                char room_line[96];
                if (!room->active) continue;
                snprintf(room_line, sizeof(room_line), "%s  %d/%d  :%d",
                         room->name[0] ? room->name : "ROOM",
                         room->players, room->max_players, room->game_port);
                if (row == selected)
                    fill_rect(surface, 150, y + row * 30 - 4, 500,
                              FB_FONT_H + 8, 0x293746);
                fb_draw_string((unsigned char *)surface->pixels, 175,
                               y + row * 30, room_line,
                               row == selected ? RF_COLOR_UI_ACCENT :
                               RF_COLOR_UI_TEXT, surface->stride);
                row++;
            }
        }
        fb_draw_string((unsigned char *)surface->pixels, 205, 370,
                       "UP DOWN SELECT   ENTER JOIN   ESC BACK",
                       RF_COLOR_UI_TEXT_MUTED, surface->stride);
    } else if (screen == RASTERFALL_STARTUP_MANUAL_IP) {
        fb_draw_string((unsigned char *)surface->pixels, 220, 78,
                       "CONNECT TO IP", RF_COLOR_UI_SECONDARY, surface->stride);
        fill_rect(surface, 190, 128, 420, FB_FONT_H + 12,
                  editing_port ? 0x202B35 : 0x293746);
        fb_draw_string((unsigned char *)surface->pixels, 210, 133,
                       address && address[0] ? address : "_",
                       RF_COLOR_UI_ACCENT, surface->stride);
        snprintf(line, sizeof(line), "PORT: %s", port_text && port_text[0] ?
                 port_text : "_");
        fb_draw_string((unsigned char *)surface->pixels, 190, 190,
                       line, RF_COLOR_UI_ACCENT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, 190, 225,
                       "0-9 .  IP   TAB  PORT", RF_COLOR_UI_TEXT_MUTED, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, 190, 250,
                       "ENTER CONNECT   ESC BACK", RF_COLOR_UI_TEXT_MUTED, surface->stride);
    } else {
        fb_draw_string((unsigned char *)surface->pixels, 190, 105,
                       screen == RASTERFALL_STARTUP_PUBLIC_ROOM ?
                       "PUBLIC ROOM ID" : "PUBLIC ROOM ID",
                       RF_COLOR_UI_SECONDARY, surface->stride);
        fill_rect(surface, 250, 145, 300, FB_FONT_H + 12, 0x293746);
        fb_draw_string((unsigned char *)surface->pixels, 285, 150,
                       room_text && room_text[0] ? room_text : "_",
                       RF_COLOR_UI_ACCENT, surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, 190, 215,
                       "ENTER CONFIRM   ESC BACK", RF_COLOR_UI_TEXT_MUTED, surface->stride);
        if (!room_text || strlen(room_text) != 4)
            fb_draw_string((unsigned char *)surface->pixels, 190, 255,
                           "ROOM ID MUST BE 4 DIGITS", 0xFF8060,
                           surface->stride);
    }
}

/* Menu-only frames still traverse the Core layer contract. This prepares the
 * native clear target and returns the overlay/coverage surface for GUI pixels. */
static struct toy_surface *begin_menu_overlay(struct rf_core *core)
{
    rf_core_render_frame_begin_v1(core,0,0,0,1024,0,1024);
    core->render_frame.sky_enabled=0;
    if (core->mixed_frame) core->mixed_frame->sky_enabled=0;
    for (int layer=RF_RENDER_LAYER_WORLD;layer<=RF_RENDER_LAYER_VIEWMODEL;++layer)
        if (rf_core_render_frame_enter_layer_v1(core,(enum rf_render_layer_v1)layer)<0)
            return NULL;
    if (rf_core_flush(core)<0) return NULL;
    return rf_core_begin_screen_overlay(core);
}

static int run_startup_menu(struct rf_core *core,
                            int *net_mode, char *address, int address_size,
                            int *port, int *public_room, int *room_id,
                            int *managed_spectator,
                            const char *error,
                            struct rasterfall_net_discovery *discovery)
{
    struct rf_input_frame input_frame;
    struct rf_input_frame *input = &input_frame;
    struct toy_window_events *events = rf_core_events(core);
    int screen = RASTERFALL_STARTUP_MAIN, selected = 0, running = 1;
    int editing_port = 0;
    int discovery_active = 0;
    char port_text[8];
    char room_text[8];
    int64_t nav_ready = 0;
    unsigned char pending_key_edges[TOY_INPUT_KEY_COUNT];
    strcpy(address, "127.0.0.1");
    strcpy(port_text, "28460");
    room_text[0] = 0;
    memset(pending_key_edges, 0, sizeof(pending_key_edges));
    while (running) {
        struct toy_surface surface;
        int64_t now = rf_core_time_us(core);
        if (rf_core_poll_events(core) < 0) break;
        if (rf_core_get_input_frame(core, input) < 0) break;
        if (events->keyboard_focus_changed && !events->keyboard_focused)
            memset(pending_key_edges, 0, sizeof(pending_key_edges));
        for (int i = 0; i < events->key_event_count; i++) {
            unsigned int key = events->key_events[i].key;
            if (events->key_events[i].pressed && key < TOY_INPUT_KEY_COUNT)
                pending_key_edges[key] = 1;
        }
        /* Wayland 的 xdg_toplevel.move 必须使用鼠标按下事件的 serial。
         * 菜单顶部保留为可拖拽区域，不影响下方按钮操作。 */
        if (events->button_pressed && events->button == BTN_LEFT &&
            events->button_serial && events->pointer_y < 70)
            rf_core_move_window(core, events->button_serial);
        if (rf_core_should_exit(core)) break;
        if (screen == RASTERFALL_STARTUP_LAN_ROOMS && discovery_active)
            rasterfall_net_discovery_poll(discovery, NULL, 0, 0, 0, 0);
        if (screen == RASTERFALL_STARTUP_PUBLIC_ROOM) {
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                if (!events->key_events[i].pressed) continue;
                if (key == KEY_ESC) { screen = RASTERFALL_STARTUP_MAIN; selected = 0; }
                else if (key == KEY_BACKSPACE) {
                    int length = (int)strlen(room_text);
                    if (length > 0) room_text[length - 1] = 0;
                } else if (key == KEY_ENTER) {
                    if (strlen(room_text) == 4) {
                        *room_id = atoi(room_text); *public_room = 1;
                        /* Keep the public-room role selected on the main menu:
                         * item 2 creates a host and item 3 joins as a guest. */
                        *net_mode = selected == 2 ? RASTERFALL_NET_HOST :
                                                   RASTERFALL_NET_CLIENT;
                        return 1;
                    }
                } else {
                    int digit = startup_digit(key), length = (int)strlen(room_text);
                    if (digit >= 0 && length < 4) {
                        room_text[length] = (char)('0' + digit); room_text[length + 1] = 0;
                    }
                }
            }
        } else if (screen == RASTERFALL_STARTUP_LAN_ROOMS) {
            int room_count = discovery ? discovery->room_count : 0;
            int room_index = 0;
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                int active_count = discovery ? discovery->room_count : 0;
                if (!events->key_events[i].pressed) continue;
                if (key == KEY_ESC) {
                    rasterfall_net_discovery_close(discovery);
                    discovery_active = 0;
                    screen = RASTERFALL_STARTUP_MAIN;
                    selected = 4;
                } else if (key == KEY_ENTER && active_count > 0) {
                    for (int room_slot = 0;
                         room_slot < RASTERFALL_NET_DISCOVERY_MAX_ROOMS;
                         room_slot++) {
                        if (!discovery->rooms[room_slot].active) continue;
                        if (room_index++ != selected) continue;
                        {
                            char *room_address = inet_ntoa(
                                discovery->rooms[room_slot].address.sin_addr);
                            if (room_address) strcpy(address, room_address);
                            *port = discovery->rooms[room_slot].game_port;
                            *public_room = 0;
                            *net_mode = RASTERFALL_NET_CLIENT;
                            rasterfall_net_discovery_close(discovery);
                            discovery_active = 0;
                            return 1;
                        }
                    }
                } else if ((key == KEY_UP || key == KEY_DOWN) && room_count > 0) {
                    selected += key == KEY_DOWN ? 1 : -1;
                    if (selected < 0) selected = room_count - 1;
                    if (selected >= room_count) selected = 0;
                }
            }
        } else if (screen == RASTERFALL_STARTUP_MANUAL_IP) {
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                if (!events->key_events[i].pressed) continue;
                if (key == KEY_ESC) {
                    screen = RASTERFALL_STARTUP_MAIN;
                    selected = 5;
                    editing_port = 0;
                } else if (key == KEY_TAB) {
                    editing_port = !editing_port;
                } else if (key == KEY_BACKSPACE) {
                    char *text = editing_port ? port_text : address;
                    int length = (int)strlen(text);
                    if (length > 0) text[length - 1] = 0;
                } else if (key == KEY_DOT) {
                    int length = (int)strlen(address);
                    if (!editing_port && length + 1 < address_size) strcat(address, ".");
                } else if (key == KEY_ENTER) {
                    int selected_port = parse_positive_int(port_text, 0);
                    if (!editing_port && inet_addr(address) != 0xffffffffU &&
                        selected_port > 0) {
                        *port = selected_port;
                        *net_mode = RASTERFALL_NET_CLIENT;
                        return 1;
                    }
                } else {
                    int digit = startup_digit(key);
                    char *text = editing_port ? port_text : address;
                    int length = (int)strlen(text);
                    if (digit >= 0 && length + 1 < (editing_port ? 8 : address_size)) {
                        char digit_text[2];
                        digit_text[0] = (char)('0' + digit); digit_text[1] = 0;
                        strcat(text, digit_text);
                    }
                }
            }
        } else {
            int up = pending_key_edges[KEY_UP];
            int down = pending_key_edges[KEY_DOWN];
            if ((up || down) && now >= nav_ready) {
                int limit = 7;
                selected += down ? 1 : -1;
                if (selected < 0) selected = limit - 1;
                if (selected >= limit) selected = 0;
                nav_ready = now + 160000;
            }
            /* Navigation is edge-triggered.  An edge arriving during the
             * debounce window is discarded instead of queued, otherwise a
             * single physical press can move again after the key is up. */
            pending_key_edges[KEY_UP] = 0;
            pending_key_edges[KEY_DOWN] = 0;
            if (pending_key_edges[KEY_ESC]) {
                pending_key_edges[KEY_ESC] = 0;
                if (screen == RASTERFALL_STARTUP_MAIN) break;
                screen = RASTERFALL_STARTUP_MAIN;
                selected = 1;
            } else if (pending_key_edges[KEY_ENTER]) {
                pending_key_edges[KEY_ENTER] = 0;
                if (screen == RASTERFALL_STARTUP_MAIN) {
                    if (selected == 0) {
                        *net_mode = RASTERFALL_NET_HOST;
                        *public_room = 0;
                        return 1;
                    } else if (selected == 1) {
                        *net_mode = RASTERFALL_NET_OFF;
                        *public_room = 0;
                        if (managed_spectator) *managed_spectator = 1;
                        return 1;
                    } else if (selected == 2 || selected == 3) {
                        screen = RASTERFALL_STARTUP_PUBLIC_ROOM;
                        room_text[0] = 0;
                    } else if (selected == 4) {
                        if (rasterfall_net_discovery_browser_start(discovery) == 0) {
                            discovery_active = 1;
                            screen = RASTERFALL_STARTUP_LAN_ROOMS;
                            selected = 0;
                        }
                    } else if (selected == 5) {
                        screen = RASTERFALL_STARTUP_MANUAL_IP;
                        address[0] = 0;
                        editing_port = 0;
                    } else if (selected == 6) break;
                }
            }
        }
        int ready = rf_core_begin_frame(core, 0x10151D);
        surface = *rf_core_surface(core);
        if (ready < 0) break;
        if (ready > 0) {
            struct toy_surface *overlay=begin_menu_overlay(core);
            if (!overlay) break;
            surface=*overlay;
            draw_startup_menu(&surface, screen, selected, address, port_text,
                              editing_port, room_text, error, discovery);
            if (rf_core_end_frame(core) < 0) break;
        }
        memset(input->key_pressed, 0, sizeof(input->key_pressed));
    }
    if (discovery_active) rasterfall_net_discovery_close(discovery);
    return 0;
}

static int wait_for_network_connection(struct rf_core *core,
                                       struct rasterfall_net *net,
                                       const char *address, int port)
{
    struct rf_input_frame input_frame;
    struct rf_input_frame *input = &input_frame;
    int64_t deadline = rf_core_time_us(core) + 6000000;
    while (rf_core_time_us(core) < deadline) {
        struct toy_surface surface;
        char line[96];
        int ready;
        if (rf_core_poll_events(core) < 0) return -2;
        if (rf_core_get_input_frame(core, input) < 0) return -2;
        if (rf_core_should_exit(core) || toy_input_pressed(input, KEY_ESC))
            return -2;
        rasterfall_net_poll(net);
        rasterfall_net_update_connection(net);
        if (net->public_error) return -3;
        if (net->connected) return 0;
        ready = rf_core_begin_frame(core, 0x10151D);
        surface = *rf_core_surface(core);
        if (ready < 0) return -2;
        if (ready == 0) continue;
        struct toy_surface *overlay=begin_menu_overlay(core);
        if (!overlay) return -2;
        surface=*overlay;
        fb_draw_string((unsigned char *)surface.pixels, 226, 92,
                       "CONNECTING...", RF_COLOR_UI_ACCENT, surface.stride);
        snprintf(line, sizeof(line), "%s:%d", address, port);
        fb_draw_string((unsigned char *)surface.pixels, 238, 132,
                       line, RF_COLOR_UI_TEXT, surface.stride);
        fb_draw_string((unsigned char *)surface.pixels, 180, 220,
                       "WAITING FOR HOST SNAPSHOT", RF_COLOR_UI_TEXT_MUTED,
                       surface.stride);
        fb_draw_string((unsigned char *)surface.pixels, 220, 260,
                       "ESC CANCEL", RF_COLOR_UI_TEXT_MUTED, surface.stride);
        if (rf_core_end_frame(core) < 0) return -2;
        memset(input->key_pressed, 0, sizeof(input->key_pressed));
    }
    return -1;
}

/* num/den 线性插值两色（num=den 时取 from，0 时取 to） */
/* 把游戏层的水平射线转成从枪口指向屏幕准心的 3D 视觉终点。
 * 游戏命中仍使用水平平面，而 tracer 必须补偿枪口在右下方造成的视差，
 * 否则它会从枪口斜着飞向准心旁边。 */
static void tracer_world_endpoint(const struct toy_game_ray *ray,
                                  int origin_x, int origin_y, int origin_z,
                                  int pitch_sy, int pitch_cy,
                                  int ex, int ez, int *out_x, int *out_y,
                                  int *out_z)
{
    int dx = ex - origin_x;
    int dz = ez - origin_z;
    int distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (distance < 1) distance = 1;
    if (pitch_cy < 1) pitch_cy = 1;
    /* Gameplay hit testing remains horizontal XZ, but the visible bullet
     * follows the shooter's 3D sight line.  The pitch term is based on the
     * shooter's muzzle, never the observer's camera height. */
    *out_x = ex;
    *out_y = origin_y + pitch_sy * distance / pitch_cy +
             ray->vy * distance / 1024;
    *out_z = ez;
}

/* AI/远端的命中点来自角色中心的 gameplay 射线。视觉枪口通常位于角色
 * 一侧，不能直接把它连到中心射线的命中点，否则短线段移动时会出现
 * 平行四边形式的斜向漂移。用同一条水平射线方向，从视觉枪口重新取终点。 */
static void tracer_world_endpoint_on_ray(const struct toy_game_ray *ray,
                                         int origin_x, int origin_y,
                                         int origin_z, int ray_origin_x,
                                         int ray_origin_z, int ex, int ez,
                                         int *out_x, int *out_y, int *out_z)
{
    int dx = ex - ray_origin_x;
    int dz = ez - ray_origin_z;
    int distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (distance < 1) distance = 1;
    *out_x = origin_x + ray->sy * distance / 1024;
    *out_y = origin_y + ray->vy * distance / 1024;
    *out_z = origin_z + ray->cy * distance / 1024;
}

static void emit_ray_effects(const struct toy_game_ray *ray,
                             int sx, int sy, int sz,
                             int ex, int ey, int ez, int depth_test,
                             int target_id, int weapon, int source_id)
{
    struct rasterfall_effect_event event;
    memset(&event, 0, sizeof(event));
    event.type = RASTERFALL_EFFECT_EVENT_TRACER;
    event.flags = depth_test ? RASTERFALL_EFFECT_EVENT_DEPTH_TEST : 0;
    event.sx = sx; event.sy = sy; event.sz = sz;
    event.ex = ex; event.ey = ey; event.ez = ez;
    event.x = ex; event.y = ey; event.z = ez;
    event.damage = ray->damage;
    event.target_id = target_id;
    event.weapon = weapon;
    event.source_id = source_id;
    event.life_ms = toy_game_weapon_info(weapon)->tracer_lifetime_ms;
    rasterfall_effects_consume(&effects, &event);
    if (ray->hit_enemy || ray->hit_world) {
        event.type = ray->hit_enemy ? RASTERFALL_EFFECT_EVENT_ENTITY_HIT :
                                      RASTERFALL_EFFECT_EVENT_BULLET_IMPACT;
        event.dir_sy = ray->sy;
        event.dir_cy = ray->cy;
        rasterfall_effects_consume(&effects, &event);
    }
}

static void emit_weapon_fire_effect(int sx, int sy, int sz, int dir_sy,
                                    int dir_cy, int weapon,
                                    unsigned int sequence, int source_id,
                                    int local_view)
{
    struct rasterfall_effect_event event;
    memset(&event, 0, sizeof(event));
    event.type = RASTERFALL_EFFECT_EVENT_WEAPON_FIRE;
    event.flags = local_view ? RASTERFALL_EFFECT_EVENT_LOCAL_VIEW : 0;
    event.sx = sx; event.sy = sy; event.sz = sz;
    event.dir_sy = dir_sy; event.dir_cy = dir_cy;
    event.weapon = weapon;
    event.sequence = sequence;
    event.source_id = source_id;
    event.life_ms = RASTERFALL_MUZZLE_FLASH_LIFE_MS;
    rasterfall_effects_consume(&effects, &event);
}

/* 每次实际开火后同步：把 game 里最新一枪的射线搬进 tracer 环，
 * 起点统一取枪口世界坐标；命中（敌人或墙体）的弹丸在弹着点生成
 * 火花，枪口闪光则由独立的 weapon fire event 绘制。 */
static void sync_fire_effects(const struct camera *camera)
{
    const struct toy_game_actor *local_player =
        toy_game_local_player_actor_const(&game);
    int i, ray_count, weapon;
    struct vec3 muzzle;
    struct vec3 muzzle_view;
    if (local_player->fire_seq < effects.last_fire_seq) {
        /* 新局（R 重开/死亡重开）：丢弃旧弹道与粒子 */
        rasterfall_effects_reset_fire(&effects);
        return;
    }
    if (local_player->fire_seq == effects.last_fire_seq) return;
    effects.last_fire_seq = local_player->fire_seq;
    ray_count = local_player->ray_count;
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    /* 后坐力 + 枪口世界坐标（开火瞬间采样，含后坐位移） */
    effects.weapon_kick = VIEWMODEL_KICK_MAX;
    weapon = rasterfall_viewmodel_weapon(&game);
    {
        int mx, my, mz;
        rasterfall_viewmodel_muzzle_offset(weapon,
                                           effects.weapon_kick, &mx, &my, &mz);
        muzzle_view.x = mx;
        muzzle_view.y = my;
        muzzle_view.z = mz;
    }
    muzzle.x = muzzle_view.x;
    muzzle.y = muzzle_view.y;
    muzzle.z = muzzle_view.z;
    view_to_world(camera, &muzzle, &muzzle);
    emit_weapon_fire_effect(muzzle.x, muzzle.y, muzzle.z,
                            camera->pitch_sy, camera->pitch_cy,
                            weapon,
                            local_player->fire_seq, 0, 1);
    for (i = 0; i < ray_count; i++) {
        const struct toy_game_ray *r = &local_player->rays[i];
        int tracer_x, tracer_y, tracer_z;
        tracer_world_endpoint(r, muzzle.x, muzzle.y, muzzle.z,
                            camera->pitch_sy, camera->pitch_cy,
                            r->ex, r->ez,
                            &tracer_x, &tracer_y, &tracer_z);
        emit_ray_effects(r, muzzle.x, muzzle.y, muzzle.z,
                         tracer_x, tracer_y, tracer_z, 0, r->enemy_index,
                         weapon, 0);
    }
}

/* AI 的弹道同样进入 tracer/命中特效环。起点使用 AI 的世界坐标，终点
 * 仍通过当前观察相机投影，因此第一人称玩家和旁观者都能看到完整弹道。 */
static void sync_ai_fire_effects(const struct camera *camera,
                                 struct rasterfall_audio *audio)
{
    int actor_index;
    (void)camera;
    for (actor_index = 0; actor_index < TOY_GAME_MAX_ACTORS; actor_index++) {
        const struct toy_game_actor *actor = &game.actors[actor_index];
        int i, ray_count, mx, my, mz;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI) continue;
        if (actor->fire_seq < effects.last_actor_fire_seq[actor_index]) {
            effects.last_actor_fire_seq[actor_index] = 0;
            continue;
        }
        if (actor->fire_seq == effects.last_actor_fire_seq[actor_index]) continue;
        effects.last_actor_fire_seq[actor_index] = actor->fire_seq;
        if (audio && audio->running) {
            int weapon = actor->current_slot >= 0 &&
                         actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                         actor->slots[actor->current_slot].weapon : -1;
            unsigned char event = weapon == TOY_GAME_WEAPON_SMG ?
                TOY_GAME_EV_SHOOT_SMG :
                weapon == TOY_GAME_WEAPON_SHOTGUN ? TOY_GAME_EV_SHOOT_SHOTGUN :
                weapon == TOY_GAME_WEAPON_AK ? TOY_GAME_EV_SHOOT_AK :
                weapon == TOY_GAME_WEAPON_AWP ? TOY_GAME_EV_SHOOT_AWP :
                TOY_GAME_EV_SHOOT;
            rasterfall_audio_play_events(audio, &event, 1);
        }
        if (actor_index == 0) effects.last_ai_fire_seq = actor->fire_seq;
        ray_count = actor->ray_count;
        if (ray_count < 0) ray_count = 0;
        if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
        int platform_lift = actor->ground_y + actor->airborne_y;
        rasterfall_viewmodel_actor_muzzle(actor->x, actor->z,
                                          actor->sy, actor->cy,
                                          platform_lift,
                                          actor->current_slot >= 0 &&
                                          actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                          actor->slots[actor->current_slot].weapon : -1,
                                          &mx, &my, &mz);
        emit_weapon_fire_effect(mx, my, mz, actor->sy, actor->cy,
                                actor->current_slot >= 0 &&
                                actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                actor->slots[actor->current_slot].weapon : -1,
                                actor->fire_seq, 1 + actor_index, 0);
        for (i = 0; i < ray_count; i++) {
            const struct toy_game_ray *r = &actor->rays[i];
            int tracer_x, tracer_y, tracer_z;
            tracer_world_endpoint_on_ray(r, mx, my, mz, actor->x, actor->z,
                                          r->ex, r->ez,
                                          &tracer_x, &tracer_y, &tracer_z);
            emit_ray_effects(r, mx, my, mz,
                             tracer_x, tracer_y, tracer_z, 1, r->enemy_index,
                             actor->current_slot >= 0 &&
                             actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                             actor->slots[actor->current_slot].weapon : -1,
                             1 + actor_index);
        }
    }
}

static unsigned char network_weapon_fire_event(int weapon)
{
    switch (weapon) {
    case TOY_GAME_WEAPON_SMG: return TOY_GAME_EV_SHOOT_SMG;
    case TOY_GAME_WEAPON_SHOTGUN: return TOY_GAME_EV_SHOOT_SHOTGUN;
    case TOY_GAME_WEAPON_AK: return TOY_GAME_EV_SHOOT_AK;
    case TOY_GAME_WEAPON_AWP: return TOY_GAME_EV_SHOOT_AWP;
    default: return TOY_GAME_EV_SHOOT;
    }
}

static void sync_network_fire_effects(const struct camera *viewer,
                                      const struct camera *client,
                                      int source_id,
                                      int weapon, unsigned int fire_seq,
                                      int ray_count,
                                      const struct toy_game_ray *rays,
                                      struct rasterfall_audio *audio)
{
    struct vec3 muzzle;
    int i, mx, my, mz;
    if (source_id < 0 || source_id >= RASTERFALL_NET_PLAYER_MAX) return;
    if (!fire_seq || fire_seq == effects.last_network_fire_seq[source_id]) return;
    if (fire_seq < effects.last_network_fire_seq[source_id])
        effects.last_network_fire_seq[source_id] = 0;
    effects.last_network_fire_seq[source_id] = fire_seq;
    if (audio && audio->running) {
        unsigned char event = network_weapon_fire_event(weapon);
        rasterfall_audio_play_events(audio, &event, 1);
    }
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    /* Remote players are rendered in third person.  Their tracer must start
     * at the same side-mounted muzzle as the avatar, not at the observer's
     * first-person weapon offset (which sits visibly too high). */
    rasterfall_viewmodel_actor_muzzle(client->x, client->z,
                                      client->sy, client->cy, client->y,
                                      weapon, &mx, &my, &mz);
    muzzle.x = mx; muzzle.y = my; muzzle.z = mz;
    emit_weapon_fire_effect(mx, my, mz, client->sy, client->cy, weapon,
                            fire_seq, 32 + source_id, 0);
    for (i = 0; i < ray_count; i++) {
        const struct toy_game_ray *r = &rays[i];
        int tracer_x, tracer_y, tracer_z;
        tracer_world_endpoint_on_ray(r, muzzle.x, muzzle.y, muzzle.z,
                                      client->x, client->z, r->ex, r->ez,
                                      &tracer_x, &tracer_y, &tracer_z);
        emit_ray_effects(r, muzzle.x, muzzle.y, muzzle.z,
                         tracer_x, tracer_y, tracer_z, 1, r->enemy_index, weapon,
                         32 + source_id);
    }
    (void)viewer;
}

/* 一个带最终边界检查的 Bresenham。投影裁剪是为了避免远处端点导致
 * 巨量迭代，像素检查则是最后一道防线：fb_draw_line 不检查坐标。 */
static void draw_game_over_panel(struct toy_surface *surface, int network_client)
{
    char line[96];
    const char *prompt = network_client ?
                         "WAIT FOR HOST   Esc quit" :
                         "R restart   Esc quit";
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 8) / 2, y + 28,
                   "YOU DIED", RF_COLOR_UI_TEXT, surface->stride);
    snprintf(line, sizeof(line), "WAVE %d  KILLS %d", game.wave,
             toy_game_local_player_actor_const(&game)->kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, RF_COLOR_UI_TEXT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(prompt)) / 2,
                   y + 104, prompt, 0xD88A32, surface->stride);
}

static void draw_level_won_panel(struct toy_surface *surface, int network_client)
{
    char line[96];
    const char *prompt = network_client ?
                         "WAIT FOR HOST   Esc quit" :
                         "R restart   Esc quit";
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0x56B878);
    fill_rect(surface, x, y, panel_w, panel_h, RF_COLOR_UI_BACKGROUND);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 17) / 2, y + 28,
                   "SAFE ROOM REACHED", RF_COLOR_UI_AI, surface->stride);
    snprintf(line, sizeof(line), "KILLS %d",
             toy_game_local_player_actor_const(&game)->kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, RF_COLOR_UI_TEXT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(prompt)) / 2,
                   y + 104, prompt, 0xD88A32, surface->stride);
}

static void draw_input_debug(struct toy_surface *surface,
                             const struct rf_input_frame *input,
                             unsigned int last_key, int last_pressed,
                             int event_count)
{
    char line[96];
    int x = 8;
    int y = surface->height - FB_FONT_H * 4 - 12;
    unsigned int color = input->keyboard_focused ? RF_COLOR_UI_AI : 0xE08080;
    fill_rect(surface, x - 4, y - 4, 390, FB_FONT_H * 4 + 8, RF_COLOR_UI_BACKGROUND);
    snprintf(line, sizeof(line), "KEYBOARD %s  EVENTS %d",
             input->keyboard_focused ? "FOCUSED" : "UNFOCUSED", event_count);
    fb_draw_string((unsigned char *)surface->pixels, x, y,
                   line, color, surface->stride);
    snprintf(line, sizeof(line), "LAST %u %s",
             last_key, last_pressed ? "PRESSED" : "RELEASED");
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H,
                   line, RF_COLOR_UI_TEXT, surface->stride);
    snprintf(line, sizeof(line), "DOWN W%d A%d S%d D%d SPC%d ENT%d",
             toy_input_down(input, KEY_W), toy_input_down(input, KEY_A),
             toy_input_down(input, KEY_S), toy_input_down(input, KEY_D),
             toy_input_down(input, KEY_SPACE), toy_input_down(input, KEY_ENTER));
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H * 2,
                   line, RF_COLOR_UI_TEXT, surface->stride);
    snprintf(line, sizeof(line), "EDGE SPC P%d R%d  R P%d R%d",
             toy_input_pressed(input, KEY_SPACE),
             toy_input_released(input, KEY_SPACE),
             toy_input_pressed(input, KEY_R),
             toy_input_released(input, KEY_R));
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H * 3,
                   line, 0xD8B060, surface->stride);
}

#undef toy_input_down
#undef toy_input_pressed
#undef toy_input_released
#include "rasterfall_render_resources.h"
#include "rasterfall_logic_test.inc"
#define toy_input_down rf_input_down
#define toy_input_pressed rf_input_pressed
#define toy_input_released rf_input_released

#include "rasterfall_perf.h"

/* Keep the presentation default at the same 16:9 aspect ratio used by the
 * original 800x450 build.  Gameplay units and camera FOV deliberately do not
 * depend on these values. */
#define RASTERFALL_DEFAULT_WIDTH  1280
#define RASTERFALL_DEFAULT_HEIGHT 720

struct model_view_stats {
    uint64_t hash;
    unsigned long foreground;
    unsigned long luminance_sum;
    unsigned long near_black;
};

static int requested_model_pose = RASTERFALL_MODEL_POSE_BIND;
static int requested_model_skinning = -1;

static void measure_model_view(const struct toy_surface *surface,
                               struct model_view_stats *stats)
{
    int x, y;
    memset(stats, 0, sizeof(*stats));
    stats->hash = 1469598103934665603ULL;
    for (y = 0; y < surface->height; y++) {
        const uint32_t *row = (const uint32_t *)((const unsigned char *)surface->pixels +
                                                 y * surface->stride);
        for (x = 0; x < surface->width; x++) {
            uint32_t color = row[x] & 0xffffffU;
            int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
            int luminance;
            stats->hash ^= color;
            stats->hash *= 1099511628211ULL;
            if (color == 0x30343bU) continue;
            luminance = (r * 299 + g * 587 + b * 114) / 1000;
            stats->foreground++;
            stats->luminance_sum += (unsigned long)luminance;
            if (luminance < 20) stats->near_black++;
        }
    }
}

static int dump_model_views(const char *model_path, const char *output_dir,
                            int use_sphere, int use_toon, int use_edge,
                            int use_material_light,
                            struct model_view_stats *stats, int supersample)
{
    static const char *names[4] = {"front", "side", "back", "three-quarter"};
    struct camera cameras[4];
    struct rasterfall_model_asset model;
    struct toy_surface surface, output_surface;
    struct toy_renderer renderer;
    uint32_t *pixels, *output_pixels = 0;
    char path[512];
    int i, result = 0, view_count = stats ? 3 : 4;

    memset(&model, 0, sizeof(model));
    memset(cameras, 0, sizeof(cameras));
    if (rasterfall_model_load(&model, model_path) < 0) {
        __fprintf(2, "rasterfall: cannot load preview model %s\n", model_path);
        return 1;
    }
    if (requested_model_skinning >= 0)
        rasterfall_model_set_skinning(&model, requested_model_skinning);
    if (model.bone_count)
        rasterfall_model_set_pose(&model, requested_model_pose);
    if (tlibc_recursive_mkdir(output_dir) < 0) {
        __fprintf(2, "rasterfall: cannot create preview directory %s\n",
                  output_dir);
        rasterfall_model_unload(&model);
        return 1;
    }
    if (supersample != 2) supersample = 1;
    surface.width = 800 * supersample;
    surface.height = 800 * supersample;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) {
        rasterfall_model_unload(&model);
        return 1;
    }
    surface.pixels = pixels;
    output_surface = surface;
    if (supersample == 2) {
        output_surface.width = 800;
        output_surface.height = 800;
        output_surface.stride = 800 * (int)sizeof(uint32_t);
        output_pixels = tlibc_malloc((size_t)output_surface.stride * 800);
        if (!output_pixels) {
            tlibc_free(pixels);
            rasterfall_model_unload(&model);
            return 1;
        }
        output_surface.pixels = output_pixels;
    }
    toy_renderer_init(&renderer);

    /* Front and back look along the model Z axis; side looks along +X. */
    cameras[0].z = -800; cameras[0].cy = 1024;
    cameras[1].x = -800; cameras[1].sy = 1024;
    cameras[2].z = 800; cameras[2].cy = -1024;
    cameras[3].x = -566; cameras[3].z = -566;
    cameras[3].sy = 724; cameras[3].cy = 724;
    for (i = 0; i < 4; i++) cameras[i].pitch_cy = 1024;

    for (i = 0; i < view_count; i++) {
        struct rasterfall_model_render_stats render_stats;
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
            rasterfall_render_model_preview(&renderer, &cameras[i], &model,
                                             use_sphere, use_toon, use_edge,
                                             use_material_light) < 0) {
            result = 1;
            break;
        }
        rasterfall_render_model_stats(&render_stats);
        __printf("rasterfall: model triangles view=%s body={total=%lu near_rejected=%lu near_clipped=%lu backface_culled=%lu emitted=%lu} edge={total=%lu near_rejected=%lu near_clipped=%lu backface_culled=%lu emitted=%lu} command_overflow=%d\n",
                 names[i], render_stats.body.total_triangles,
                 render_stats.body.near_rejected_triangles,
                 render_stats.body.near_clipped_triangles,
                 render_stats.body.backface_culled_triangles,
                 render_stats.body.emitted_triangles,
                 render_stats.edge.total_triangles,
                 render_stats.edge.near_rejected_triangles,
                 render_stats.edge.near_clipped_triangles,
                 render_stats.edge.backface_culled_triangles,
                 render_stats.edge.emitted_triangles,
                 render_stats.command_overflow);
        toy_renderer_flush(&renderer);
        if (supersample == 2) {
            int y, x;
            for (y = 0; y < output_surface.height; y++) {
                uint32_t *destination = (uint32_t *)((unsigned char *)
                    output_surface.pixels + y * output_surface.stride);
                const uint32_t *row0 = (const uint32_t *)((unsigned char *)
                    surface.pixels + y * 2 * surface.stride);
                const uint32_t *row1 = (const uint32_t *)((unsigned char *)
                    surface.pixels + (y * 2 + 1) * surface.stride);
                for (x = 0; x < output_surface.width; x++) {
                    uint32_t p0 = row0[x * 2], p1 = row0[x * 2 + 1];
                    uint32_t p2 = row1[x * 2], p3 = row1[x * 2 + 1];
                    unsigned int a = (p0 >> 24) + (p1 >> 24) +
                                     (p2 >> 24) + (p3 >> 24);
                    unsigned int r = ((p0 >> 16) & 255) + ((p1 >> 16) & 255) +
                                     ((p2 >> 16) & 255) + ((p3 >> 16) & 255);
                    unsigned int g = ((p0 >> 8) & 255) + ((p1 >> 8) & 255) +
                                     ((p2 >> 8) & 255) + ((p3 >> 8) & 255);
                    unsigned int b = (p0 & 255) + (p1 & 255) +
                                     (p2 & 255) + (p3 & 255);
                    destination[x] = ((a + 2) / 4 << 24) |
                                     ((r + 2) / 4 << 16) |
                                     ((g + 2) / 4 << 8) | (b + 2) / 4;
                }
            }
        }
        if (stats) measure_model_view(&output_surface, &stats[i]);
        if (snprintf(path, sizeof(path), "%s/%s.bmp", output_dir, names[i]) >=
                (int)sizeof(path) ||
            rasterfall_hud_dump_bmp(path, &output_surface) < 0) {
            __fprintf(2, "rasterfall: cannot write model preview %s\n", names[i]);
            result = 1;
            break;
        }
        if (snprintf(path, sizeof(path), "%s/%s.ppm", output_dir, names[i]) >=
                (int)sizeof(path) ||
            rasterfall_hud_dump_frame(path, &output_surface) < 0) {
            __fprintf(2, "rasterfall: cannot write model PPM %s\n", names[i]);
            result = 1;
            break;
        }
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(output_pixels);
    tlibc_free(pixels);
    rasterfall_model_unload(&model);
    return result;
}

static int dump_model_material_regression(const char *model_path,
                                          const char *output_dir)
{
    static const char *groups[4] = {"base", "sphere", "toon", "full"};
    static const char *views[3] = {"front", "side", "back"};
    static const unsigned char sphere[4] = {0, 1, 0, 1};
    static const unsigned char toon[4] = {0, 0, 1, 1};
    struct model_view_stats stats[4][3];
    struct rasterfall_model_asset model;
    char directory[512], manifest_path[512], line[256];
    unsigned int double_sided = 0, single_sided = 0;
    int group, view, fd, length;
    if (tlibc_recursive_mkdir(output_dir) < 0) return 1;
    for (group = 0; group < 4; group++) {
        if (snprintf(directory, sizeof(directory), "%s/%s", output_dir,
                     groups[group]) >= (int)sizeof(directory) ||
            dump_model_views(model_path, directory, sphere[group], toon[group],
                             1, 1, stats[group], 1) != 0) return 1;
    }
    if (snprintf(manifest_path, sizeof(manifest_path), "%s/manifest.txt",
                 output_dir) >= (int)sizeof(manifest_path)) return 1;
    memset(&model, 0, sizeof(model));
    if (rasterfall_model_load(&model, model_path) < 0) return 1;
    for (view = 0; view < (int)model.material_count; view++) {
        if (model.format_version < 7 ||
            (model.materials[view * model.material_bytes + 7] & 1))
            double_sided++;
        else single_sided++;
    }
    fd = __creat(manifest_path, 0644);
    if (fd < 0) { rasterfall_model_unload(&model); return 1; }
    length = snprintf(line, sizeof(line),
        "rfm_version=%u materials=%u double_sided=%u single_sided=%u\n",
        model.format_version, model.material_count, double_sided, single_sided);
    rasterfall_model_unload(&model);
    if (length <= 0 || length >= (int)sizeof(line) ||
        __write(fd, line, length) != length) { __close(fd); return 1; }
    for (group = 0; group < 4; group++) for (view = 0; view < 3; view++) {
        unsigned long mean = stats[group][view].foreground ?
            stats[group][view].luminance_sum / stats[group][view].foreground : 0;
        length = snprintf(line, sizeof(line),
            "%s/%s.bmp hash=%016llx foreground=%lu mean_luminance=%lu near_black=%lu\n",
            groups[group], views[view], stats[group][view].hash,
            stats[group][view].foreground, mean, stats[group][view].near_black);
        if (length <= 0 || length >= (int)sizeof(line) ||
            __write(fd, line, length) != length) { __close(fd); return 1; }
    }
    __close(fd);
    __printf("rasterfall: material regression manifest %s\n", manifest_path);
    return 0;
}

struct model_performance_result {
    int64_t wall_us;
    int64_t begin_us;
    int64_t setup_us;
    int64_t flush_us;
    long sort_us;
    long classify_us;
    long merge_copy_us;
    long actual_sort_us;
    long flat_raster_cpu_us;
    long texture_raster_cpu_us;
    unsigned long triangles;
    unsigned long bbox_pixels;
    unsigned long inside_pixels;
    unsigned long textured_pixels;
    unsigned long opaque_commands;
    unsigned long transparent_commands;
    unsigned long edge_commands;
    unsigned long sorted_commands;
    long worker_active_us[8];
    long worker_cpu_us[8];
    unsigned long worker_commands[8];
    unsigned long worker_triangles[8];
    unsigned long worker_bbox_pixels[8];
    unsigned long worker_inside_pixels[8];
    unsigned long worker_depth_pass_pixels[8];
    unsigned long worker_shaded_pixels[8];
    unsigned long worker_written_pixels[8];
    unsigned long worker_flat_pixels[8];
    unsigned long worker_texture_pixels[8];
    unsigned long alpha_blended_pixels;
    unsigned long alpha_zero_pixels;
    unsigned long depth_divisions;
    unsigned long base_perspective_divisions;
    unsigned long sphere_perspective_divisions;
    unsigned long texture_address_divisions;
    unsigned long material_color_divisions;
    unsigned long alpha_divisions;
    unsigned long blend_divisions;
    unsigned long material_path_pixels[4];
    unsigned long material_path_divisions[4];
    long setup_model_total_us;
    long setup_bone_hierarchy_us;
    long setup_skinning_us;
    long setup_vertex_cache_us;
    long setup_material_us;
    long setup_body_triangles_us;
    long setup_edge_triangles_us;
    long worker_wait_us;
    int worker_count;
    int frames;
};

static int benchmark_model_features(const char *model_path, int iterations,
                                    int requested_workers)
{
    static const char *names[10] = {
        "full", "base_texture_only", "opaque_texture_diag",
        "affine_uv_diag", "simple_address_diag", "edge_off", "toon_off",
        "sphere_off", "lighting_off", "model_off"
    };
    static const unsigned char sphere[10] = {1, 0, 1, 1, 1, 1, 1, 0, 1, 0};
    static const unsigned char toon[10] = {1, 0, 1, 1, 1, 1, 0, 1, 1, 0};
    static const unsigned char edge[10] = {1, 1, 1, 1, 1, 0, 1, 1, 1, 0};
    static const unsigned char lighting[10] = {1, 0, 1, 1, 1, 1, 1, 1, 1, 0};
    static const unsigned char model_enabled[10] = {1, 1, 1, 1, 1, 1, 1, 1, 1, 0};
    static const int diagnostic[10] = {
        0, 0, TOY_RENDER_DIAG_FORCE_OPAQUE, TOY_RENDER_DIAG_AFFINE_UV,
        TOY_RENDER_DIAG_SIMPLE_ADDRESS, 0, 0, 0, 0, 0
    };
    static const unsigned char intentionally_changed[10] = {
        0, 1, 1, 1, 0, 1, 1, 1, 1, 1
    };
    struct model_performance_result results[10];
    struct rasterfall_model_asset model;
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct camera cameras[3];
    uint32_t *pixels;
    int configuration, iteration, view, step;

    if (iterations < 1) iterations = 1;
    if (iterations > 100) iterations = 100;
    memset(&model, 0, sizeof(model));
    memset(cameras, 0, sizeof(cameras));
    memset(results, 0, sizeof(results));
    if (rasterfall_model_load(&model, model_path) < 0) {
        __fprintf(2, "rasterfall: cannot load performance model %s\n",
                  model_path);
        return 1;
    }
    surface.width = 800;
    surface.height = 800;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) { rasterfall_model_unload(&model); return 1; }
    surface.pixels = pixels;
    toy_renderer_init(&renderer);
    toy_renderer_set_worker_count(&renderer, requested_workers);
    cameras[0].z = -800; cameras[0].cy = 1024;
    cameras[1].x = -800; cameras[1].sy = 1024;
    cameras[2].z = 800; cameras[2].cy = -1024;
    for (view = 0; view < 3; view++) cameras[view].pitch_cy = 1024;

    if (model.bone_count) {
        static const char *skin_names[3] = {
            "static_old_path", "bind_pose_skinning", "right_arm_pose"
        };
        int skin_mode;
        long full_us[3] = {0, 0, 0}, hierarchy_us[3] = {0, 0, 0};
        long skinning_us[3] = {0, 0, 0}, cache_us[3] = {0, 0, 0};
        long triangles_us[3] = {0, 0, 0}, raster_us[3] = {0, 0, 0};
        int frames[3] = {0, 0, 0};
        for (skin_mode = 0; skin_mode < 3; skin_mode++) {
            rasterfall_model_set_skinning(&model, skin_mode != 0);
            rasterfall_model_set_pose(&model,
                skin_mode == 2 ? RASTERFALL_MODEL_POSE_RIGHT_ARM :
                                 RASTERFALL_MODEL_POSE_BIND);
            if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
                rasterfall_render_model_preview(&renderer, &cameras[0], &model,
                                                1, 1, 1, 1) < 0) goto fail;
            toy_renderer_flush(&renderer);
            for (iteration = 0; iteration < iterations; iteration++)
                for (view = 0; view < 3; view++) {
                    struct rasterfall_model_setup_timing timing;
                    int64_t start = diagnostic_monotonic_us(), after_setup, after_flush;
                    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
                        rasterfall_render_model_preview(&renderer, &cameras[view],
                            &model, 1, 1, 1, 1) < 0) goto fail;
                    after_setup = diagnostic_monotonic_us();
                    rasterfall_render_model_setup_timing(&timing);
                    toy_renderer_flush(&renderer);
                    after_flush = diagnostic_monotonic_us();
                    full_us[skin_mode] += after_flush - start;
                    hierarchy_us[skin_mode] += timing.bone_hierarchy_us;
                    skinning_us[skin_mode] += timing.skinning_us;
                    cache_us[skin_mode] += timing.vertex_cache_us;
                    triangles_us[skin_mode] += timing.material_us +
                        timing.body_triangles_us + timing.edge_triangles_us;
                    raster_us[skin_mode] += after_flush - after_setup;
                    frames[skin_mode]++;
                }
        }
        for (skin_mode = 0; skin_mode < 3; skin_mode++) {
            int count = frames[skin_mode] ? frames[skin_mode] : 1;
            __printf("rasterfall: skeletal benchmark mode=%s frames=%d bone_hierarchy_us_per_frame=%ld skinning_us_per_frame=%ld vertex_cache_us_per_frame=%ld triangle_setup_us_per_frame=%ld pixel_raster_us_per_frame=%ld full_us_per_frame=%ld\n",
                     skin_names[skin_mode], frames[skin_mode],
                     hierarchy_us[skin_mode] / count,
                     skinning_us[skin_mode] / count,
                     cache_us[skin_mode] / count,
                     triangles_us[skin_mode] / count,
                     raster_us[skin_mode] / count,
                     full_us[skin_mode] / count);
        }
        {
            static const char *animation_names[4] = {
                "Bind/static", "ARM RAISE", "ARMS LOOP", "BODY TURN"
            };
            long animation_us[4] = {0, 0, 0, 0};
            int animation_id, sample, sample_count = iterations * 100;
            for (animation_id = 0; animation_id < 4; animation_id++) {
                const struct rasterfall_animation_clip *clip =
                    animation_id == 0 ? NULL : &model.animation.demo_clips[animation_id-1];
                for (sample = 0; sample < sample_count; sample++) {
                    long start = diagnostic_monotonic_us();
                    rasterfall_model_sample_clip(&model, clip,
                        clip ? (sample * 17) % clip->duration_ms : 0);
                    animation_us[animation_id] += diagnostic_monotonic_us() - start;
                }
                __printf("rasterfall: animation benchmark clip=%s samples=%d animation_sample_us_per_sample=%ld\n",
                         animation_names[animation_id], sample_count,
                         animation_us[animation_id] / (sample_count ? sample_count : 1));
            }
        }
        rasterfall_model_set_skinning(&model, 1);
        rasterfall_model_set_pose(&model, RASTERFALL_MODEL_POSE_BIND);
    }

    /* Populate renderer buffers and texture/model caches before measuring. */
    for (configuration = 0; configuration < 10; configuration++) {
        rasterfall_render_set_model_lighting(configuration != 8);
        toy_renderer_set_texture_diagnostics(&renderer,
                                             diagnostic[configuration]);
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) goto fail;
        if (model_enabled[configuration] &&
            rasterfall_render_model_preview(&renderer, &cameras[0], &model,
                sphere[configuration], toon[configuration], edge[configuration],
                lighting[configuration]) < 0) goto fail;
        toy_renderer_flush(&renderer);
    }
    for (iteration = 0; iteration < iterations; iteration++) {
        /* Alternate direction so thermal/scheduler drift does not always
         * penalize the same late configuration. */
        for (step = 0; step < 10; step++) {
            configuration = iteration & 1 ? 9 - step : step;
            rasterfall_render_set_model_lighting(configuration != 8);
            for (view = 0; view < 3; view++) {
                struct rasterfall_model_setup_timing setup_timing;
                int64_t start = diagnostic_monotonic_us();
                int64_t after_begin, after_setup, after_flush;
                if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0)
                    goto fail;
                after_begin = diagnostic_monotonic_us();
                toy_renderer_set_texture_diagnostics(
                    &renderer, diagnostic[configuration]);
                if (model_enabled[configuration] &&
                    rasterfall_render_model_preview(&renderer, &cameras[view],
                        &model, sphere[configuration], toon[configuration],
                        edge[configuration], lighting[configuration]) < 0)
                    goto fail;
                after_setup = diagnostic_monotonic_us();
                memset(&setup_timing, 0, sizeof(setup_timing));
                if (model_enabled[configuration])
                    rasterfall_render_model_setup_timing(&setup_timing);
                toy_renderer_flush(&renderer);
                after_flush = diagnostic_monotonic_us();
                results[configuration].wall_us += after_flush - start;
                results[configuration].begin_us += after_begin - start;
                results[configuration].setup_us += after_setup - after_begin;
                results[configuration].setup_model_total_us +=
                    setup_timing.total_us;
                results[configuration].setup_bone_hierarchy_us +=
                    setup_timing.bone_hierarchy_us;
                results[configuration].setup_skinning_us +=
                    setup_timing.skinning_us;
                results[configuration].setup_vertex_cache_us +=
                    setup_timing.vertex_cache_us;
                results[configuration].setup_material_us +=
                    setup_timing.material_us;
                results[configuration].setup_body_triangles_us +=
                    setup_timing.body_triangles_us;
                results[configuration].setup_edge_triangles_us +=
                    setup_timing.edge_triangles_us;
                results[configuration].flush_us += after_flush - after_setup;
                results[configuration].sort_us += renderer.last_sort_us;
                results[configuration].classify_us +=
                    renderer.last_classify_us;
                results[configuration].merge_copy_us +=
                    renderer.last_merge_copy_us;
                results[configuration].actual_sort_us +=
                    renderer.last_actual_sort_us;
                results[configuration].flat_raster_cpu_us +=
                    renderer.last_flat_us;
                results[configuration].texture_raster_cpu_us +=
                    renderer.last_tex_us;
                results[configuration].triangles += renderer.submitted_triangles;
                results[configuration].bbox_pixels += renderer.last_bbox_px;
                results[configuration].inside_pixels += renderer.last_inside_px;
                results[configuration].textured_pixels += renderer.last_tex_px;
                results[configuration].opaque_commands +=
                    renderer.last_opaque_cmds;
                results[configuration].transparent_commands +=
                    renderer.last_transparent_cmds;
                results[configuration].edge_commands +=
                    renderer.last_edge_cmds;
                results[configuration].sorted_commands +=
                    renderer.last_sorted_cmds;
                results[configuration].worker_wait_us +=
                    renderer.last_worker_wait_us;
                results[configuration].worker_count = renderer.worker_count;
                for (int worker = 0; worker < renderer.worker_count; worker++) {
                    const struct toy_render_worker *w = &renderer.workers[worker];
                    results[configuration].worker_active_us[worker] += w->active_us;
                    results[configuration].worker_cpu_us[worker] += w->cpu_us;
                    results[configuration].worker_commands[worker] += w->commands;
                    results[configuration].worker_triangles[worker] += w->triangles;
                    results[configuration].worker_bbox_pixels[worker] += w->bbox_px;
                    results[configuration].worker_inside_pixels[worker] += w->inside_px;
                    results[configuration].worker_depth_pass_pixels[worker] +=
                        w->depth_pass_px;
                    results[configuration].worker_shaded_pixels[worker] += w->shaded_px;
                    results[configuration].worker_written_pixels[worker] += w->written_px;
                    results[configuration].worker_flat_pixels[worker] += w->flat_pixels;
                    results[configuration].worker_texture_pixels[worker] +=
                        w->textured_pixels;
                    results[configuration].alpha_blended_pixels +=
                        w->alpha_blended_pixels;
                    results[configuration].alpha_zero_pixels +=
                        w->alpha_zero_pixels;
                    results[configuration].depth_divisions +=
                        w->depth_divisions;
                    results[configuration].base_perspective_divisions +=
                        w->base_perspective_divisions;
                    results[configuration].sphere_perspective_divisions +=
                        w->sphere_perspective_divisions;
                    results[configuration].texture_address_divisions +=
                        w->texture_address_divisions;
                    results[configuration].material_color_divisions +=
                        w->material_color_divisions;
                    results[configuration].alpha_divisions +=
                        w->alpha_divisions;
                    results[configuration].blend_divisions +=
                        w->blend_divisions;
                    for (int path = 0; path < 4; path++) {
                        results[configuration].material_path_pixels[path] +=
                            w->material_path_pixels[path];
                        results[configuration].material_path_divisions[path] +=
                            w->material_path_divisions[path];
                    }
                }
                results[configuration].frames++;
            }
        }
    }
    __printf("rasterfall: model performance path=%s iterations=%d views=3 size=800x800 detected_cpus=%d selected_workers=%d\n",
             model_path, iterations, renderer.detected_cpu_count,
             renderer.worker_count);
    rasterfall_render_set_model_lighting(1);
    for (configuration = 0; configuration < 10; configuration++) {
        const struct model_performance_result *r = &results[configuration];
        int frames = r->frames ? r->frames : 1;
        long sort_us = r->sort_us / frames;
        long flush_us = (long)(r->flush_us / frames);
        long worker_min = 0, worker_max = 0, worker_sum = 0, worker_cpu_sum = 0;
        unsigned long tex_min = 0, tex_max = 0, tex_sum = 0;
        unsigned long bbox_min = 0, bbox_max = 0, bbox_sum = 0;
        for (int worker = 0; worker < r->worker_count; worker++) {
            long active = r->worker_active_us[worker] / frames;
            unsigned long tex = r->worker_texture_pixels[worker] /
                                (unsigned long)frames;
            unsigned long worker_bbox = r->worker_bbox_pixels[worker] /
                                        (unsigned long)frames;
            if (worker == 0 || active < worker_min) worker_min = active;
            if (worker == 0 || active > worker_max) worker_max = active;
            if (worker == 0 || tex < tex_min) tex_min = tex;
            if (worker == 0 || tex > tex_max) tex_max = tex;
            if (worker == 0 || worker_bbox < bbox_min) bbox_min = worker_bbox;
            if (worker == 0 || worker_bbox > bbox_max) bbox_max = worker_bbox;
            worker_sum += active;
            worker_cpu_sum += r->worker_cpu_us[worker] / frames;
            tex_sum += tex;
            bbox_sum += worker_bbox;
        }
        long worker_avg = r->worker_count ? worker_sum / r->worker_count : 0;
        unsigned long texture_pixels = r->textured_pixels /
                                       (unsigned long)frames;
        long texture_cpu = r->texture_raster_cpu_us / frames;
        __printf("rasterfall: model performance mode=%s intentionally_changed=%s frames=%d wall_us_per_frame=%ld clear_us_per_frame=%ld triangle_setup_us_per_frame=%ld sort_us_per_frame=%ld classify_us_per_frame=%ld merge_copy_us_per_frame=%ld actual_sort_us_per_frame=%ld pixel_raster_wall_us_per_frame=%ld worker_count=%d worker_wait_us_per_frame=%ld worker_total_cpu_us_per_frame=%ld worker_us_min=%ld worker_us_max=%ld worker_us_avg=%ld worker_max_avg_permille=%ld worker_spread_us=%ld texture_pixels_min=%lu texture_pixels_max=%lu texture_pixels_avg=%lu worker_bbox_min=%lu worker_bbox_max=%lu worker_bbox_avg=%lu opaque_commands_per_frame=%lu transparent_commands_per_frame=%lu edge_commands_per_frame=%lu sorted_elements_per_frame=%lu flat_raster_cpu_us_per_frame=%ld texture_raster_cpu_us_per_frame=%ld texture_cpu_ns_per_pixel=%ld alpha_blended_pixels_per_frame=%lu alpha_zero_pixels_per_frame=%lu triangles_per_frame=%lu bbox_pixels_per_frame=%lu inside_pixels_per_frame=%lu textured_pixels_per_frame=%lu\n",
                 names[configuration],
                 intentionally_changed[configuration] ? "yes" : "no",
                 r->frames,
                 (long)(r->wall_us / frames),
                 (long)(r->begin_us / frames),
                 (long)(r->setup_us / frames), sort_us,
                 r->classify_us / frames,
                 r->merge_copy_us / frames,
                 r->actual_sort_us / frames,
                 flush_us > sort_us ? flush_us - sort_us : 0,
                 r->worker_count, r->worker_wait_us / frames,
                 worker_cpu_sum, worker_min, worker_max, worker_avg,
                 worker_avg ? worker_max * 1000 / worker_avg : 0,
                 worker_max - worker_min,
                 tex_min, tex_max,
                 r->worker_count ? tex_sum / (unsigned long)r->worker_count : 0,
                 bbox_min, bbox_max,
                 r->worker_count ? bbox_sum / (unsigned long)r->worker_count : 0,
                 r->opaque_commands / (unsigned long)frames,
                 r->transparent_commands / (unsigned long)frames,
                 r->edge_commands / (unsigned long)frames,
                 r->sorted_commands / (unsigned long)frames,
                 r->flat_raster_cpu_us / frames,
                 texture_cpu,
                 texture_pixels ? texture_cpu * 1000 / (long)texture_pixels : 0,
                 r->alpha_blended_pixels / (unsigned long)frames,
                 r->alpha_zero_pixels / (unsigned long)frames,
                 r->triangles / (unsigned long)frames,
                 r->bbox_pixels / (unsigned long)frames,
                 r->inside_pixels / (unsigned long)frames,
                 r->textured_pixels / (unsigned long)frames);
        if (configuration == 0) for (int worker = 0;
                                      worker < r->worker_count; worker++)
            __printf("rasterfall: model performance worker=%d active_us_per_frame=%ld cpu_us_per_frame=%ld commands_per_frame=%lu triangles_per_frame=%lu bbox_pixels_per_frame=%lu inside_pixels_per_frame=%lu depth_pass_pixels_per_frame=%lu shaded_pixels_per_frame=%lu written_pixels_per_frame=%lu flat_pixels_per_frame=%lu texture_pixels_per_frame=%lu\n",
                     worker,
                     r->worker_active_us[worker] / frames,
                     r->worker_cpu_us[worker] / frames,
                     r->worker_commands[worker] / (unsigned long)frames,
                     r->worker_triangles[worker] / (unsigned long)frames,
                     r->worker_bbox_pixels[worker] / (unsigned long)frames,
                     r->worker_inside_pixels[worker] / (unsigned long)frames,
                     r->worker_depth_pass_pixels[worker] / (unsigned long)frames,
                     r->worker_shaded_pixels[worker] / (unsigned long)frames,
                     r->worker_written_pixels[worker] / (unsigned long)frames,
                     r->worker_flat_pixels[worker] / (unsigned long)frames,
                     r->worker_texture_pixels[worker] / (unsigned long)frames);
        if (configuration == 0 || configuration == 3) {
            unsigned long total_divisions = r->depth_divisions;
            for (int path = 0; path < 4; path++)
                total_divisions += r->material_path_divisions[path];
            __printf("rasterfall: model divisions mode=%s depth_per_frame=%lu total_per_frame=%lu base_pixels=%lu base_divisions_per_pixel=%lu toon_pixels=%lu toon_divisions_per_pixel=%lu sphere_pixels=%lu sphere_divisions_per_pixel=%lu toon_sphere_pixels=%lu toon_sphere_divisions_per_pixel=%lu\n",
                     names[configuration],
                     r->depth_divisions / (unsigned long)frames,
                     total_divisions / (unsigned long)frames,
                     r->material_path_pixels[0] / (unsigned long)frames,
                     r->material_path_pixels[0] ?
                        r->material_path_divisions[0] /
                        r->material_path_pixels[0] : 0,
                     r->material_path_pixels[1] / (unsigned long)frames,
                     r->material_path_pixels[1] ?
                        r->material_path_divisions[1] /
                        r->material_path_pixels[1] : 0,
                     r->material_path_pixels[2] / (unsigned long)frames,
                     r->material_path_pixels[2] ?
                        r->material_path_divisions[2] /
                        r->material_path_pixels[2] : 0,
                     r->material_path_pixels[3] / (unsigned long)frames,
                     r->material_path_pixels[3] ?
                        r->material_path_divisions[3] /
                        r->material_path_pixels[3] : 0);
            __printf("rasterfall: model division kinds mode=%s depth=%lu base_perspective=%lu sphere_perspective=%lu texture_address=%lu material_color=%lu alpha=%lu blend=%lu per_frame=yes\n",
                     names[configuration],
                     r->depth_divisions / (unsigned long)frames,
                     r->base_perspective_divisions / (unsigned long)frames,
                     r->sphere_perspective_divisions / (unsigned long)frames,
                     r->texture_address_divisions / (unsigned long)frames,
                     r->material_color_divisions / (unsigned long)frames,
                     r->alpha_divisions / (unsigned long)frames,
                     r->blend_divisions / (unsigned long)frames);
        }
        if (configuration == 0) {
            long accounted = r->setup_bone_hierarchy_us +
                r->setup_skinning_us + r->setup_vertex_cache_us + r->setup_material_us +
                r->setup_body_triangles_us + r->setup_edge_triangles_us;
            __printf("rasterfall: model setup mode=full model_total_us_per_frame=%ld bone_hierarchy_us_per_frame=%ld skinning_us_per_frame=%ld vertex_cache_us_per_frame=%ld material_us_per_frame=%ld body_triangle_pipeline_us_per_frame=%ld edge_triangle_pipeline_us_per_frame=%ld model_unaccounted_us_per_frame=%ld outer_setup_us_per_frame=%ld\n",
                     r->setup_model_total_us / frames,
                     r->setup_bone_hierarchy_us / frames,
                     r->setup_skinning_us / frames,
                     r->setup_vertex_cache_us / frames,
                     r->setup_material_us / frames,
                     r->setup_body_triangles_us / frames,
                     r->setup_edge_triangles_us / frames,
                     (r->setup_model_total_us - accounted) / frames,
                     (long)(r->setup_us / frames));
        }
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);
    rasterfall_model_unload(&model);
    return 0;
fail:
    rasterfall_render_set_model_lighting(1);
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);
    rasterfall_model_unload(&model);
    return 1;
}

#define input_debug options.input_debug
#define action_runtime_debug options.action_runtime_debug
#define logic_test options.logic_test
#define requested_net_mode options.requested_net_mode
#define net_port options.net_port
#define net_loss_percent options.net_loss_percent
#define net_address options.net_address
#define auto_mode options.auto_mode
#define world_cycle_gate options.world_cycle_gate
#define edge_pass_enabled options.edge_pass_enabled
#define stats_enabled options.stats_enabled
#define texture_stats options.texture_stats
#define frame_limit options.frame_limit
#define dump_path options.dump_path
#define view_model_path options.view_model_path
#define view_output_dir options.view_output_dir
#define model_views_supersample options.model_views_supersample
#define material_regression options.material_regression
#define performance_model_path options.performance_model_path
#define bone_model_path options.bone_model_path
#define bone_search options.bone_search
#define humanoid_model_path options.humanoid_model_path
#define humanoid_basis_model_path options.humanoid_basis_model_path
#define retarget_model_path options.retarget_model_path
#define retarget_action options.retarget_action
#define glb_animation_model options.glb_animation_model
#define glb_animation_path options.glb_animation_path
#define glb_animation_name options.glb_animation_name
#define glb_motion_model options.glb_motion_model
#define glb_motion_path options.glb_motion_path
#define vmd_walk_model options.vmd_walk_model
#define vmd_walk_path options.vmd_walk_path
#define vmd_freeze_head options.vmd_freeze_head
#define vmd_freeze_torso options.vmd_freeze_torso
#define vmd_disable_ik options.vmd_disable_ik
#define vmd_disable_grant options.vmd_disable_grant
#define vmd_legacy_root_offset options.vmd_legacy_root_offset
#define vmd_legacy_knee_ccd options.vmd_legacy_knee_ccd
#define vmd_skin_trace options.vmd_skin_trace
#define performance_iterations options.performance_iterations
#define performance_workers options.performance_workers
#define actor_performance options.actor_performance
#define actor_raster_workers options.actor_raster_workers

#undef effects

/* The fixed-step gameplay authority.  The host loop owns platform polling and
 * rendering cadence, but it must not own a second simulation path. */
int rf_game_update(struct rf_game_runtime *runtime,
                   const struct rasterfall_command *command,
                   int dt_ms)
{
    struct rasterfall_session *game_session;
    struct rasterfall_net *game_net;
    struct rasterfall_effects *game_effects;
    struct camera *game_camera;
    int is_client;

    if (!runtime || !runtime->initialized || !runtime->session) return -1;
    if (dt_ms < 0) dt_ms = 0;
    if (dt_ms > 250) dt_ms = 250;
    game_session = runtime->session;
    game_net = &runtime->net;
    game_effects = &runtime->effects;
    game_camera = &runtime->camera;
    is_client = game_net->mode == RASTERFALL_NET_CLIENT;

    rasterfall_effects_update(game_effects, dt_ms);
    if (command) {
        runtime->command = *command;
        if (!runtime->lifecycle_paused) {
            if (game_session->game_state.state == TOY_GAME_PLAYING &&
                !(is_client && (!game_net->connected || !game_net->world_ready))) {
                if (game_net->mode == RASTERFALL_NET_HOST)
                    rasterfall_net_prepare_host_step(
                        game_net, &game_session->game_state);
                if (is_client)
                    rasterfall_session_step_client(
                        game_session, game_camera, &runtime->command, dt_ms);
                else
                    rasterfall_session_step(
                        game_session, game_camera, &runtime->command, dt_ms);
                if (game_net->mode == RASTERFALL_NET_HOST)
                    rasterfall_net_apply_local_rescue(
                        game_net, game_session, game_camera,
                        (runtime->command.buttons & RASTERFALL_CMD_INTERACT) != 0,
                        dt_ms);
                if (is_client) {
                    if ((runtime->command.buttons & RASTERFALL_CMD_INTERACT) &&
                        game_session->highlight_index >= 0 &&
                        game_session->highlight_index < game_session->item_count)
                        runtime->command.shop_arg =
                            game_session->items[game_session->highlight_index].kind + 1;
                    rasterfall_net_send_command(
                        game_net, &runtime->command, game_camera,
                        &game_session->game_state, runtime->command.jump_dx,
                        runtime->command.jump_dz);
                }
            } else if (runtime->command.buttons & RASTERFALL_CMD_RESET) {
                if (is_client)
                    rasterfall_net_send_command(
                        game_net, &runtime->command, game_camera,
                        &game_session->game_state, 0, 0);
                else {
                    rasterfall_session_step(
                        game_session, game_camera, &runtime->command, dt_ms);
                    if (game_net->mode == RASTERFALL_NET_HOST)
                        rasterfall_net_reset_host(game_net);
                }
            }
        }
    }

    {
        enum rasterfall_world_id requested_world;
        if (rasterfall_session_take_world_request(game_session,
                                                  &requested_world)) {
            if (rf_game_request_world(runtime, requested_world) < 0) {
                game_session->banner_ms = 2200;
                game_session->banner_success = 0;
                game_session->banner_text = "WORLD LOAD FAILED";
            }
        }
    }
    if (game_session->station_gui_request) {
        game_session->station_gui_request = 0;
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
        rf_gui_set_active(&runtime->gui, 1);
        rf_gui_set_icon_count(&runtime->gui, RF_GUI_ICON_COUNT);
        rf_core_set_pointer_lock(runtime->core, 0);
        rf_app_manager_register_station(&runtime->app_manager);
        rf_app_manager_set_query_context(&runtime->app_manager,
                                         &runtime->application_query);
        /* Use the same terminal desktop as F12; PERSONNEL remains available
         * beside it and reads the live actor projection. */
        rf_app_manager_open_icon(&runtime->app_manager, 1, 1024, 720);
        runtime->lifecycle_paused = 1;
#else
        game_session->banner_ms = 2200;
        game_session->banner_success = 0;
        game_session->banner_text = RASTERFALL_DESKTOP_UNAVAILABLE_MESSAGE;
#endif
    }

    rasterfall_effects_sync_fire_zones(game_effects,
                                       &game_session->game_state);
    rasterfall_effects_sync_projectile_flashes(
        game_effects, &game_session->game_state);
    rasterfall_effects_sync_damage_flash(
        game_effects, &game_session->game_state, game_camera);
    rasterfall_effects_sync_enemy_feedback(
        game_effects, &game_session->game_state);
    if (game_session->highlight_index >= 0 &&
        game_session->highlight_index < game_session->item_count) {
        const struct rasterfall_interactable *highlight =
            &game_session->items[game_session->highlight_index];
        rasterfall_effects_sync_interaction_highlight(
            game_effects, game_session->highlight_index, highlight->x,
            highlight->y, highlight->z, 1);
    } else {
        rasterfall_effects_sync_interaction_highlight(
            game_effects, -1, 0, 0, 0, 0);
    }
    if (game_net->mode == RASTERFALL_NET_HOST) {
        rasterfall_net_apply_clients(game_net, game_session, game_camera);
        rasterfall_net_capture_events(game_net, &game_session->game_state);
        /* Remote fire state is advanced by apply_clients().  Replay the
         * guarded sequence here so the host presentation observes the shot
         * in the same fixed step as the authoritative update. */
        for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
            const struct rasterfall_net_client *client = &game_net->clients[i];
            int actor_index = TOY_GAME_REMOTE_ACTOR_BASE +
                              client->client_id - 1;
            const struct toy_game_actor *actor;
            if (!client->active || !client->connected || actor_index < 0 ||
                actor_index >= TOY_GAME_MAX_ACTORS)
                continue;
            actor = &game_session->game_state.actors[actor_index];
            if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER)
                continue;
            sync_network_fire_effects(
                game_camera, &client->camera, client->client_id,
                toy_game_actor_current_weapon(actor), actor->fire_seq,
                actor->ray_count, actor->rays,
                runtime->audio.running ? &runtime->audio : NULL);
        }
        if ((game_net->tick % 4) == 0)
            rasterfall_net_send_snapshot(
                game_net, game_session, game_camera,
                &game_session->game_state, game_session->air_walls_enabled,
                game_session->manual_alarm_on,
                game_session->manual_alarm_timer);
    }
    return 0;
}

static struct {
    unsigned long enemies, ai_teammates, managed_player, network_teammates;
    unsigned long text, interaction_commands;
    double enemies_ms, ai_teammates_ms, managed_player_ms;
    double network_teammates_ms, text_ms, interaction_ms;
} rf_world_submission_audit;

#define RF_RB0_WARMUP_FRAMES 16
#define RF_RB0_SAMPLE_MAX 1024
#define RF_RB0_SLOW_MAX 64
struct rf_rb0_sample {
    unsigned long long frame;
    unsigned long long gpu_timing_frame;
    int gpu_timing_valid;
    unsigned int gpu_requested, gpu_recorded, gpu_dropped;
    unsigned long long wait_predecessor_frame;
    unsigned long long submits_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    int wait_us_by_kind[RF_GPU_SUBMIT_KIND_COUNT];
    int prepare_us, render_us, execute_us, remainder_us;
    int whole_us, raster_us, gpu_raster_us, gpu_draw_us, gpu_bridge_us;
    int cpu_producer_us, slot_wait_us, graphics_wait_us;
    int acquire_us, present_us, presenter_completion_us;
    unsigned long long bridge_transfers, bridge_bytes;
    unsigned long long producer_raster_commands[RF_CORE_PRODUCER_TOTAL];
};
struct rf_rb0_stats {
    struct rf_rb0_sample samples[RF_RB0_SAMPLE_MAX];
    int count;
};

static int rf_rb0_unattributed(const struct rf_rb0_sample *sample)
{
    int covered = sample->raster_us;
    if (sample->gpu_timing_valid && sample->gpu_raster_us > covered)
        covered = sample->gpu_raster_us;
    if (sample->cpu_producer_us > covered) covered = sample->cpu_producer_us;
    if (sample->slot_wait_us > covered) covered = sample->slot_wait_us;
    if (sample->graphics_wait_us > covered) covered = sample->graphics_wait_us;
    if (sample->acquire_us + sample->present_us > covered)
        covered = sample->acquire_us + sample->present_us;
    return sample->whole_us > covered ? sample->whole_us - covered : 0;
}

static int rf_rb0_percentile(const struct rf_rb0_stats *stats, int field, int pct)
{
    int values[RF_RB0_SAMPLE_MAX], i, j, value, index, count = 0;
    for (i = 0; i < stats->count; ++i) {
        const struct rf_rb0_sample *sample = &stats->samples[i];
        if (field >= 6 && field <= 8 && !sample->gpu_timing_valid) continue;
        values[count++] = field == 0 ? sample->whole_us :
                    field == 1 ? sample->raster_us :
                    field == 2 ? sample->cpu_producer_us :
                    field == 3 ? sample->graphics_wait_us :
                    field == 4 ? sample->acquire_us :
                    field == 5 ? sample->present_us :
                    field == 6 ? sample->gpu_raster_us :
                    field == 7 ? sample->gpu_draw_us :
                    field == 8 ? sample->gpu_bridge_us :
                    field == 9 ? sample->slot_wait_us :
                    field == 10 ? sample->presenter_completion_us :
                    rf_rb0_unattributed(sample);
    }
    for (i = 1; i < count; ++i) {
        value = values[i];
        for (j = i - 1; j >= 0 && values[j] > value; --j) values[j + 1] = values[j];
        values[j + 1] = value;
    }
    index = (count * pct + 99) / 100 - 1;
    if (index < 0) index = 0;
    if (index >= count) index = count - 1;
    return count ? values[index] : 0;
}

static void rf_rb0_dump(const struct rf_rb0_stats *stats)
{
    int order[RF_RB0_SAMPLE_MAX], slow_count, i, j, value;
    if (!stats->count) return;
    {
        int complete=0, incomplete=0, pending=0, dropped=0, max_requested=0;
        for (i=0;i<stats->count;++i) {
            const struct rf_rb0_sample *s=&stats->samples[i];
            if (!s->gpu_timing_frame) pending++;
            else if (s->gpu_timing_valid && s->gpu_requested==s->gpu_recorded &&
                     !s->gpu_dropped) complete++;
            else incomplete++;
            dropped+=(int)s->gpu_dropped;
            if ((int)s->gpu_requested>max_requested) max_requested=(int)s->gpu_requested;
        }
        __printf("RB0-COVERAGE complete=%d incomplete=%d pending=%d dropped=%d max_requested=%d\n",
            complete,incomplete,pending,dropped,max_requested);
    }
    __printf("RB0-STATS warmup=%d frames=%d whole_us median=%d p95=%d p99=%d max=%d raster_us p95=%d cpu_producer_us p95=%d wait_us p95=%d acquire_us p95=%d present_us p95=%d gpu_raster_us median=%d p95=%d p99=%d gpu_draw_us median=%d p95=%d p99=%d gpu_bridge_us median=%d p95=%d p99=%d slot_wait_us median=%d p95=%d p99=%d presenter_completion_us median=%d p95=%d p99=%d unattributed_us median=%d p95=%d p99=%d\n",
        RF_RB0_WARMUP_FRAMES, stats->count,
        rf_rb0_percentile(stats, 0, 50), rf_rb0_percentile(stats, 0, 95),
        rf_rb0_percentile(stats, 0, 99), rf_rb0_percentile(stats, 0, 100),
        rf_rb0_percentile(stats, 1, 95), rf_rb0_percentile(stats, 2, 95),
        rf_rb0_percentile(stats, 3, 95), rf_rb0_percentile(stats, 4, 95),
        rf_rb0_percentile(stats, 5, 95),
        rf_rb0_percentile(stats, 6, 50), rf_rb0_percentile(stats, 6, 95),
        rf_rb0_percentile(stats, 6, 99), rf_rb0_percentile(stats, 7, 50),
        rf_rb0_percentile(stats, 7, 95), rf_rb0_percentile(stats, 7, 99),
        rf_rb0_percentile(stats, 8, 50), rf_rb0_percentile(stats, 8, 95),
        rf_rb0_percentile(stats, 8, 99), rf_rb0_percentile(stats, 9, 50),
        rf_rb0_percentile(stats, 9, 95), rf_rb0_percentile(stats, 9, 99),
        rf_rb0_percentile(stats, 10, 50), rf_rb0_percentile(stats, 10, 95),
        rf_rb0_percentile(stats, 10, 99), rf_rb0_percentile(stats, 11, 50),
        rf_rb0_percentile(stats, 11, 95), rf_rb0_percentile(stats, 11, 99));
    for (i = 0; i < stats->count; ++i) order[i] = i;
    for (i = 1; i < stats->count; ++i) {
        value = order[i];
        for (j = i - 1; j >= 0 && stats->samples[order[j]].whole_us < stats->samples[value].whole_us; --j)
            order[j + 1] = order[j];
        order[j + 1] = value;
    }
    slow_count = (stats->count + 19) / 20;
    if (slow_count > RF_RB0_SLOW_MAX) slow_count = RF_RB0_SLOW_MAX;
    for (i = 0; i < slow_count; ++i) {
        const struct rf_rb0_sample *sample = &stats->samples[order[i]];
        const char *reason = "unclassified";
        int reason_us = sample->raster_us;
        if (sample->cpu_producer_us > reason_us) { reason = "cpu_producer"; reason_us = sample->cpu_producer_us; }
        else if (reason_us > 0) reason = "raster_workload";
        if (sample->gpu_timing_valid && sample->gpu_raster_us > reason_us) { reason = "gpu_raster_workload"; reason_us = sample->gpu_raster_us; }
        if (sample->slot_wait_us > reason_us) { reason = "frame_slot_wait"; reason_us = sample->slot_wait_us; }
        if (sample->graphics_wait_us > reason_us) { reason = "graphics_fence_wait"; reason_us = sample->graphics_wait_us; }
        if (sample->presenter_completion_us > reason_us) { reason = "presenter_completion_wait"; reason_us = sample->presenter_completion_us; }
        if (sample->acquire_us + sample->present_us > reason_us) {
            reason = "present_or_acquire";
            reason_us = sample->acquire_us + sample->present_us;
        }
        if (reason_us * 2 < sample->whole_us) reason = "unclassified";
        __printf("RB0-SLOW frame=%llu gpu_timing_frame=%llu whole_us=%d reason=%s attributed_us=%d unattributed_us=%d raster_us=%d gpu_raster_us=%d gpu_draw_us=%d gpu_bridge_us=%d cpu_producer_us=%d slot_wait_us=%d graphics_wait_us=%d wait_us=%d acquire_us=%d present_us=%d presenter_completion_us=%d bridge_transfers=%llu bridge_bytes=%llu",
            sample->frame, sample->gpu_timing_frame, sample->whole_us, reason,
            reason_us, rf_rb0_unattributed(sample), sample->raster_us,
            sample->gpu_raster_us, sample->gpu_draw_us, sample->gpu_bridge_us,
            sample->cpu_producer_us, sample->slot_wait_us,
            sample->graphics_wait_us, sample->graphics_wait_us,
            sample->acquire_us, sample->present_us,
            sample->presenter_completion_us, sample->bridge_transfers,
            sample->bridge_bytes);
        __printf(" gpu_valid=%d requested=%u recorded=%u dropped=%u predecessor_frame=%llu prepare_us=%d render_us=%d execute_us=%d remainder_us=%d",
            sample->gpu_timing_valid,sample->gpu_requested,sample->gpu_recorded,
            sample->gpu_dropped,sample->wait_predecessor_frame,
            sample->prepare_us,sample->render_us,sample->execute_us,sample->remainder_us);
        for (j=0;j<RF_GPU_SUBMIT_KIND_COUNT;++j) {
            static const char *names[]={"upload","vertex_diff","skin_input",
                "skinning","bridge","draw","readback"};
            __printf(" %s_submits=%llu %s_wait_us=%d",names[j],sample->submits_by_kind[j],
                names[j],sample->wait_us_by_kind[j]);
        }
        for (j = 0; j < RF_CORE_PRODUCER_COUNT; ++j)
            __printf(" %s=%llu", rf_core_mixed_producer_name((unsigned int)j),
                sample->producer_raster_commands[j]);
        __printf("\n");
    }
}

static unsigned long rf_world_audit_command_position(
    const struct rf_game_runtime *runtime, const struct toy_renderer *renderer)
{
    return (unsigned long)renderer->cmd_count +
        (runtime->render_context.mixed_frame ?
            runtime->render_context.mixed_frame->raster_count : 0UL);
}

static void rf_game_prepare_render_camera(struct rf_game_runtime *runtime)
{
    struct camera *render_camera = &runtime->render_camera;
    *render_camera = runtime->camera;
    set_network_spectator_camera(render_camera, &runtime->net);
    if (runtime->managed_spectator)
        set_managed_spectator_camera(render_camera, &runtime->camera,
                                     runtime->managed_third_person);
    rasterfall_effects_apply_camera_shake(&runtime->effects, render_camera);
}

static int rf_game_render_profiled(struct rf_game_runtime *runtime,
                   struct toy_renderer *renderer,
                   struct toy_surface *surface,
                   struct rasterfall_perf_stats *perf_window,
                   struct rasterfall_perf_stats *perf_total)
{
    struct rasterfall_session *game_session;
    struct camera *body_camera;
    struct camera *render_camera;
    struct control_settings settings;
    struct pause_menu pause_menu;
    struct managed_terminal managed_terminal;
    struct rasterfall_hud_state hud;
    int pixels = 0;
    int flushed;
    int64_t perf_start = 0;
    unsigned long perf_tris = renderer ? renderer->submitted_triangles : 0;
    unsigned long raster_commands = 0;
    unsigned long overlay_pixels = 0;
    unsigned long audit_commands;
    int64_t audit_start;

    if (!runtime || !runtime->initialized || !runtime->session ||
        !runtime->core || !renderer || !surface)
        return -1;
    game_session = runtime->session;
    body_camera = &runtime->camera;
    render_camera = &runtime->render_camera;

    /* Preserve the existing render-only camera derivation order. */
    rf_game_prepare_render_camera(runtime);
    rf_core_render_frame_begin_v1(runtime->core, render_camera->x,
        render_camera->z, render_camera->sy, render_camera->cy,
        render_camera->pitch_sy, render_camera->pitch_cy);
    rf_core_render_frame_record_v1(runtime->core, RF_RENDER_LAYER_SKY, 1, 0);

    if (perf_window) perf_start = rf_core_clock_now_us();
    int local_scene_light = rasterfall_render_begin_dynamic_lighting();
    rf_core_gpu_world_begin(runtime->core);
    runtime->render_context.mixed_frame =
        rf_core_mixed_current(runtime->core);
    if (runtime->render_context.mixed_frame &&
        rf_core_mixed_set_producer(runtime->render_context.mixed_frame, renderer,
            RF_CORE_PRODUCER_WORLD_MAP) < 0) return -1;

    /* World and actor submission order is intentionally unchanged. */
    {
        int scene_pixels = rasterfall_render_scene(renderer, render_camera);
        if (scene_pixels < 0 && runtime->render_context.mixed_frame) {
            rf_core_mixed_fail(runtime->core);
            return -1;
        }
        pixels += scene_pixels;
    }
    pixels += rasterfall_render_flags(renderer, render_camera);
    if (perf_window) {
        struct rasterfall_scene_stats detail;
        rasterfall_render_scene_stats(&detail);
        rasterfall_perf_add_scene(perf_window, perf_total, &detail);
        rasterfall_perf_end_stage(perf_window, perf_total, RASTERFALL_STATS_SCENE,
            &perf_start, renderer->submitted_triangles-perf_tris, 0);
        perf_tris=renderer->submitted_triangles;
    }
    memset(&rf_world_submission_audit, 0, sizeof(rf_world_submission_audit));
    if (runtime->render_context.mixed_frame &&
        rf_core_mixed_set_producer(runtime->render_context.mixed_frame, renderer,
            RF_CORE_PRODUCER_ENEMY_BODY) < 0) return -1;
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    pixels += rasterfall_render_enemies(renderer, render_camera);
    rf_world_submission_audit.enemies =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.enemies_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    pixels += rasterfall_render_ai_teammate(renderer, render_camera);
    rf_world_submission_audit.ai_teammates =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.ai_teammates_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    if (runtime->managed_spectator && runtime->managed_third_person)
        pixels += rasterfall_render_managed_player(
            renderer, render_camera, body_camera);
    rf_world_submission_audit.managed_player =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.managed_player_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    pixels += rasterfall_render_network_teammate(
        renderer, render_camera, &runtime->net, &game_session->game_state);
    rf_world_submission_audit.network_teammates =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.network_teammates_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;
    if (runtime->render_context.mixed_frame &&
        rf_core_mixed_set_producer(runtime->render_context.mixed_frame, renderer,
            RF_CORE_PRODUCER_WORLD_MAP) < 0) return -1;
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    pixels += rasterfall_render_sign_text(renderer, render_camera);
    pixels += rasterfall_render_flag_text(renderer, render_camera);
    rf_world_submission_audit.text =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.text_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;

    if (perf_window) {
        rasterfall_perf_end_stage(perf_window, perf_total, RASTERFALL_STATS_ENEMIES,
            &perf_start, renderer->submitted_triangles-perf_tris, 0);
        perf_tris=renderer->submitted_triangles;
    }
    /* Interactables are depth-tested world geometry.  Submit them before the
     * one normal-world consumer so native GPU frames and CPU fallback classify
     * and consume the same complete batch.  Keep this after the enemies timing
     * boundary so their triangles remain owned by the raster stage. */
    audit_commands = rf_world_audit_command_position(runtime, renderer);
    audit_start = rf_core_clock_now_us();
    if (game_session->game_state.state == TOY_GAME_PLAYING &&
        !runtime->lifecycle_paused && !game_session->shop_open)
        pixels += rasterfall_render_interactables(renderer, render_camera);
    rf_world_submission_audit.interaction_commands =
        rf_world_audit_command_position(runtime, renderer) - audit_commands;
    rf_world_submission_audit.interaction_ms =
        (double)(rf_core_clock_now_us() - audit_start) / 1000.0;
    /* Existing world-to-overlay ordering barrier. */
    if (rf_core_render_frame_enter_layer_v1(
            runtime->core, RF_RENDER_LAYER_WORLD) < 0) return -1;
    raster_commands = (unsigned long)renderer->cmd_count;
    rf_core_render_frame_record_world_v1(runtime->core, renderer->cmds,
                                         renderer->cmd_count);
    rf_core_gpu_world_flush(runtime->core);
    flushed = rf_core_flush(runtime->core);
    if (flushed < 0) return -1;
    pixels += flushed;
    if (perf_window) {
        rasterfall_perf_add_raster(perf_window, perf_total, renderer,
            raster_commands, (unsigned long)flushed);
        rasterfall_perf_end_stage(perf_window, perf_total, RASTERFALL_STATS_RASTER,
            &perf_start, raster_commands, (unsigned long)flushed);
        perf_tris=renderer->submitted_triangles;
    }

    rasterfall_render_end_dynamic_lighting();
    if (rf_core_render_frame_enter_layer_v1(
            runtime->core, RF_RENDER_LAYER_TRANSPARENT) < 0) return -1;
    if (rf_core_render_frame_enter_layer_v1(
            runtime->core, RF_RENDER_LAYER_EFFECTS) < 0) return -1;
    raster_commands = (unsigned long)renderer->cmd_count;
    {
        struct rasterfall_effect_render_stats effect_stats;
        memset(&effect_stats, 0, sizeof(effect_stats));
        flushed = rasterfall_render_effects(renderer, render_camera,
                                             &effect_stats);
        rf_core_render_frame_record_direct_pixels_v1(runtime->core,
            RF_RENDER_LAYER_EFFECTS, effect_stats.direct_pixels);
    }
    if (flushed < 0) return -1;
    pixels += flushed;
    rf_core_render_frame_record_v1(runtime->core, RF_RENDER_LAYER_EFFECTS,
        (unsigned long)renderer->cmd_count - raster_commands,
        (unsigned long)flushed);
    flushed = rf_core_flush(runtime->core);
    if (flushed < 0) return -1;
    pixels += flushed;
    rf_core_render_frame_record_v1(runtime->core, RF_RENDER_LAYER_EFFECTS,
                                   0, (unsigned long)flushed);

    if (rf_core_render_frame_enter_layer_v1(
            runtime->core, RF_RENDER_LAYER_VIEWMODEL) < 0) return -1;
    if (toy_game_local_player_actor_const(&game_session->game_state)->state !=
        TOY_GAME_ACTOR_DOWNED) {
        int viewmodel_direct_pixels;
        raster_commands = (unsigned long)renderer->cmd_count;
        viewmodel_direct_pixels = rasterfall_viewmodel_render(
            renderer, &game_session->game_state, &runtime->effects, local_scene_light);
        if (viewmodel_direct_pixels < 0) return -1;
        /* V1 normal producers only submit triangle commands.  Keep this
         * statistic separate from the subsequent command flush so direct
         * framebuffer writes cannot be mistaken for rasterized pixels. */
        pixels += viewmodel_direct_pixels;
        rf_core_render_frame_record_v1(runtime->core,
            RF_RENDER_LAYER_VIEWMODEL,
            (unsigned long)renderer->cmd_count - raster_commands,
            (unsigned long)viewmodel_direct_pixels);
        rf_core_render_frame_record_direct_pixels_v1(runtime->core,
            RF_RENDER_LAYER_VIEWMODEL, (unsigned long)viewmodel_direct_pixels);
    }

    /* Viewmodel is the last post-world scene layer and therefore the final
     * input to Post V1. Screen-space UI starts only after this barrier. */
    flushed = rf_core_flush(runtime->core);
    if (flushed < 0) return -1;
    pixels += flushed;
    rf_core_render_frame_record_v1(runtime->core, RF_RENDER_LAYER_VIEWMODEL,
                                   0, (unsigned long)flushed);

    surface = rf_core_begin_screen_overlay(runtime->core);
    if (!surface) return -1;

    rasterfall_render_map_labels(renderer, render_camera);
    if (runtime->coordinate_axes)
        rasterfall_render_coordinate_labels(surface, render_camera);
#if TOY_CONFIG_SHOW_MODEL_PATHS
    rasterfall_render_gallery_selection(surface, render_camera);
#endif

    settings.mouse_level = runtime->mouse_level;
    settings.keyboard_level = runtime->keyboard_level;
    pause_menu.selected = runtime->pause_menu_selected;
    memset(&managed_terminal, 0, sizeof(managed_terminal));
    managed_terminal.open = runtime->managed_terminal_open;
    strcpy(managed_terminal.line, runtime->managed_terminal_line);
    strcpy(managed_terminal.message, runtime->managed_terminal_message);
    if (game_session->game_state.state == TOY_GAME_OVER) {
        draw_game_over_panel(surface, runtime->net.mode == RASTERFALL_NET_CLIENT);
    } else if (game_session->game_state.state == TOY_GAME_WON) {
        draw_level_won_panel(surface, runtime->net.mode == RASTERFALL_NET_CLIENT);
    } else if (runtime->console.open || runtime->gui.active) {
        /* Developer console is drawn after every other overlay. */
    } else if (managed_terminal.open) {
        draw_managed_terminal(surface, &managed_terminal);
    } else if (runtime->lifecycle_paused) {
        draw_pause_overlay(surface, &pause_menu, &settings,
                           runtime->coordinate_axes);
    } else {
        draw_crosshair(surface, &game_session->game_state);
        fill_hud_state(&hud, &runtime->net, runtime->host_address,
                       runtime->host_port, body_camera);
        rasterfall_hud_render(surface, runtime->display_fps, &hud);
    }
    if (game_session->game_state.state == TOY_GAME_PLAYING &&
        !runtime->lifecycle_paused && !game_session->shop_open) {
        fill_hud_state(&hud, &runtime->net, runtime->host_address,
                       runtime->host_port, body_camera);
        rasterfall_hud_draw_interact_prompt(renderer, &hud);
    }
    rasterfall_render_ai_teammate_name(renderer, render_camera);
    rasterfall_render_network_teammate_status(
        renderer, render_camera, &runtime->net, &game_session->game_state);
    flushed = rasterfall_render_overlays(renderer);
    pixels += flushed;
    overlay_pixels += (unsigned long)flushed;
    rf_core_render_frame_record_v1(runtime->core, RF_RENDER_LAYER_OVERLAY,
                                   0, overlay_pixels);
    if (game_session->game_state.state == TOY_GAME_PLAYING &&
        !runtime->lifecycle_paused && !game_session->pose_editor.active &&
        toy_input_down(&runtime->input_frame, KEY_TAB))
        draw_scoreboard(surface, &runtime->net);
    if (runtime->debug_input_enabled)
        draw_input_debug(surface, &runtime->input_frame,
                         runtime->have_last_key ? runtime->last_key : 0,
                         runtime->have_last_key ? runtime->last_key_pressed : 0,
                         runtime->input_event_count);
    if (runtime->console.open)
        rasterfall_console_draw(surface, &runtime->console);
    else if (runtime->gui.active)
        rf_gui_render(surface, &runtime->gui);
    if (perf_window)
        rasterfall_perf_end_stage(perf_window, perf_total, RASTERFALL_STATS_OVERLAY,
            &perf_start, renderer->submitted_triangles-perf_tris, overlay_pixels);
    /* Native mixed frames do not write the software scene surface. Some
     * legacy pixel counters use -1 as a no-op sentinel, so their sum cannot
     * serve as the frame success code after Draw lowering is removed. */
    runtime->scene_pixels = runtime->render_context.mixed_frame ? 0 : pixels;
    return runtime->render_context.mixed_frame ? 0 : pixels;
}

int rf_game_render(struct rf_game_runtime *runtime, struct toy_renderer *renderer,
                   struct toy_surface *surface)
{
    return rf_game_render_profiled(runtime, renderer, surface, NULL, NULL);
}

#define effects (*active_effects)

int rf_game_runtime_run(const struct rf_game_config *config)
{
    static struct rf_gpu_scene_world_resources scene_world_resources;
    int scene_native_frames=0;
    struct rf_core core;
    struct rf_game_runtime game_runtime;
    struct toy_window_events events;
    struct toy_input platform_input;
    struct rf_input_frame input;
#ifdef TOYC_WINDOWS
    struct rf_gpu_vulkan_context gpu_vulkan_context;
    struct rf_gpu_scene_world_gpu_probe scene_world_probe={0};
#endif
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct camera camera;
    struct control_settings settings;
    struct pause_menu pause_menu;
    struct managed_terminal managed_terminal;
    struct rasterfall_console developer_console;
    struct rf_command_context command_context;
    int64_t last_time, fps_window_start, fps_elapsed;
    int64_t last_active = 0;   /* 帧间隔统计 */
    int64_t menu_nav_ready_us = 0;
    int64_t accumulator = 0, prev_begin = 0;
    int running = 1, pointer_lock_requested = 0, paused = 0;
    int coordinate_axes = 0;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int rendered_frames = 0, scene_pixels = 0;
#ifndef TOYC_WINDOWS
    unsigned int watchdog_warm_world = UINT_MAX;
#endif
    int display_fps = 0, fps_window_frames = 0;
    int fire_edge = 0, shove_edge = 0;
    int pointer_turn_pending = 0, pointer_pitch_pending = 0;
    unsigned char pending_key_edges[TOY_INPUT_KEY_COUNT];
    int input_event_count = 0, have_last_key = 0;
    struct rasterfall_perf_stats stats, stats_total;
    struct rf_rb0_stats rb0_stats;
    unsigned int last_key = 0;
    int last_key_pressed = 0;
    /* 按键按压边沿跨帧保留位：逻辑步（E/R 及切枪换弹）可能因
     * accumulator 不足而整帧不跑（长 stall 后连续几帧都不跑），边沿若
     * 只在 key_pressed 里会被下一轮 begin_frame 清掉。这里逐键记录
     * 到达的按压，每帧合入 key_pressed 供消费方读取；逻辑步跑过的那
     * 帧末尾统一清除。 */
    char host_address[16];
    uint64_t seed;
    struct rasterfall_options options;
    int public_room = 0, public_room_id = 0;
    int managed_spectator = 0, managed_third_person = 0;
    const char *startup_error = NULL;
    char selected_address[64];

    if (!config || !config->options) return 2;
    rf_gpu_scene_world_resources_invalidate(&scene_world_resources);
    options = *config->options;
    memset(&rb0_stats, 0, sizeof(rb0_stats));
    rasterfall_render_set_enemy_visual_family(options.enemy_visual_family);
    if (options.enemy_visual_capture_dir)
        return rasterfall_render_enemy_visual_capture(options.enemy_visual_capture_dir);
    if (options.visual_scenario)
        if (!strcmp(options.visual_scenario, "desktop-v1"))
            return rf_gui_visual_capture(options.visual_output);
    if (options.visual_scenario)
        return rasterfall_render_visual_capture(options.visual_scenario,
                                                options.visual_output);
    if (options.profession_lineup_models)
        return rasterfall_render_profession_lineup(options.profession_lineup_models,
                                                   options.profession_lineup_dir);
    if (options.squad_acceptance_models)
        return rasterfall_render_squad_acceptance(options.squad_acceptance_models,
                                                  options.squad_acceptance_dir);
    if (options.rigid_attachment_models)
        return rasterfall_render_rigid_attachment_acceptance(
            options.rigid_attachment_models, options.rigid_attachment_dir);
    if (options.action_info_path) {
        struct rasterfall_action_clip clip;
        if (rasterfall_action_load(&clip, options.action_info_path) < 0) {
            __fprintf(2, "rasterfall: invalid action %s\n", options.action_info_path);
            return 1;
        }
        rasterfall_action_dump(&clip); return 0;
    }
    if (options.action_preview_model)
        return rasterfall_render_action_preview(options.action_preview_model,
            options.action_preview_path, options.action_time_ms,
            options.action_preview_output);
    if (options.composition_capture_model)
        return rasterfall_render_action_composition_capture(
            options.composition_capture_model, options.composition_capture_lower,
            options.composition_capture_lower_time, options.composition_capture_upper,
            options.composition_capture_upper_time, options.composition_capture_additive,
            options.composition_capture_additive_time, options.composition_capture_output);
    if (options.pose_debug_model) {
        struct rasterfall_action_clip clip, upper, additive, alternate;
        struct rasterfall_action_composition composition, alternate_composition;
        struct rasterfall_model_resource resource;
        struct rasterfall_model_instance instance, alternate_instance;
        const struct rasterfall_action_clip *current_upper = NULL;
        const char *alternate_path = NULL;
        int result = 1, prepared = 0, alternate_ready = 0;
        memset(&resource,0,sizeof(resource));memset(&instance,0,sizeof(instance));
        memset(&alternate_instance,0,sizeof(alternate_instance));
        memset(&composition,0,sizeof(composition));memset(&upper,0,sizeof(upper));
        memset(&additive,0,sizeof(additive));
        memset(&alternate,0,sizeof(alternate));
        if (rasterfall_action_load(&clip,options.pose_debug_action)==0 &&
            rasterfall_model_resource_load(&resource,options.pose_debug_model)==0 &&
            rasterfall_model_instance_init(&instance,&resource)==0 &&
            (!options.pose_debug_upper_action ||
             rasterfall_action_load(&upper,options.pose_debug_upper_action)==0) &&
            (!options.pose_debug_additive_action ||
             rasterfall_action_load(&additive,options.pose_debug_additive_action)==0)) {
            if (options.pose_debug_upper_action) {
                composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].clip=&clip;
                composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].time_ms=options.action_time_ms;
                composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].clip=&upper;
                composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].time_ms=options.pose_debug_upper_time_ms;
                if (options.pose_debug_additive_action) {
                    composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].clip=&additive;
                    composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].time_ms=options.pose_debug_additive_time_ms;
                }
                __printf("LOWER:\n  %s time=%dms\nUPPER:\n  %s time=%dms\nADDITIVE:\n  %s time=%dms\nRESULT:\n  composed humanoid pose\n",
                    clip.name,options.action_time_ms,upper.name,
                    options.pose_debug_upper_time_ms,
                    options.pose_debug_additive_action ? additive.name : "NONE",
                    options.pose_debug_additive_action ? options.pose_debug_additive_time_ms : 0);
            } else {
                enum rasterfall_action_layer_id layer =
                    clip.id==RASTERFALL_ACTION_LOCOMOTION_IDLE ||
                    clip.id==RASTERFALL_ACTION_LOCOMOTION_WALK ?
                    RASTERFALL_ACTION_LAYER_LOWER_BODY :
                    RASTERFALL_ACTION_LAYER_UPPER_BODY;
                composition.layers[layer].clip=&clip;
                composition.layers[layer].time_ms=options.action_time_ms;
            }
            prepared=1;
        }
        if (prepared &&
            rasterfall_action_compose(&instance,&composition)==0 &&
            rasterfall_action_pose_debug(&instance,options.pose_debug_role)==0) {
            current_upper = options.pose_debug_upper_action ? &upper :
                clip.layer == RASTERFALL_ACTION_LAYER_UPPER_BODY ? &clip : NULL;
            if (current_upper &&
                (current_upper->id == RASTERFALL_ACTION_RIFLE_IDLE ||
                 current_upper->id == RASTERFALL_ACTION_RIFLE_AIM)) {
                alternate_path = current_upper->id == RASTERFALL_ACTION_RIFLE_IDLE ?
                    "rasterfall/assets/actions/rifle_aim.rfanim" :
                    "rasterfall/assets/actions/rifle_idle.rfanim";
                alternate_composition = composition;
                if (rasterfall_action_load(&alternate, alternate_path) == 0 &&
                    rasterfall_model_instance_init(&alternate_instance,
                                                   &resource) == 0) {
                    alternate_composition.layers[
                        RASTERFALL_ACTION_LAYER_UPPER_BODY].clip = &alternate;
                    alternate_composition.layers[
                        RASTERFALL_ACTION_LAYER_UPPER_BODY].time_ms =
                        options.pose_debug_upper_action ?
                        options.pose_debug_upper_time_ms : options.action_time_ms;
                    if (rasterfall_action_compose(&alternate_instance,
                                                  &alternate_composition) == 0)
                        alternate_ready = 1;
                }
                if (alternate_ready) {
                    if (current_upper->id == RASTERFALL_ACTION_RIFLE_AIM)
                        rasterfall_action_pipeline_compare_debug(
                            &alternate_instance, alternate.name, &instance,
                            current_upper->name, TOY_GAME_WEAPON_AK);
                    else
                        rasterfall_action_pipeline_compare_debug(
                            &instance, current_upper->name, &alternate_instance,
                            alternate.name, TOY_GAME_WEAPON_AK);
                } else {
                    __printf("ACTION_COMPARE unavailable alternate=%s\n",
                        alternate_path);
                    rasterfall_action_pipeline_debug(&instance,
                        current_upper->name, TOY_GAME_WEAPON_AK);
                }
            } else rasterfall_action_pipeline_debug(&instance,
                "current-composed", TOY_GAME_WEAPON_AK);
            int socket;
            struct rasterfall_action_weapon_targets targets;
            __printf("weapon_sockets:\n");
            for(socket=0;socket<RASTERFALL_WEAPON_SOCKET_COUNT;socket++) {
                struct rasterfall_weapon_socket_transform t;
                if(rasterfall_weapon_socket_transform(TOY_GAME_WEAPON_AK,socket,&t)==0)
                    __printf("  %s position=(%d,%d,%d) rotation=(%.3f,%.3f,%.3f,%.3f)\n",
                        rasterfall_weapon_socket_name(socket),t.position.x,t.position.y,
                        t.position.z,t.rotation[0],t.rotation[1],t.rotation[2],t.rotation[3]);
            }
            if(rasterfall_action_weapon_target_debug(&instance,TOY_GAME_WEAPON_AK,
                                                     &targets)==0)
                __printf("weapon_transform:\n  %.6f %.6f %.6f\nhand_targets:\n  right %.6f %.6f %.6f\n  left %.6f %.6f %.6f\n",
                    targets.weapon_transform[9],targets.weapon_transform[10],
                    targets.weapon_transform[11],targets.right_hand_target[0],
                    targets.right_hand_target[1],targets.right_hand_target[2],
                    targets.left_hand_target[0],targets.left_hand_target[1],
                    targets.left_hand_target[2]);
            result=0;
        }
        rasterfall_model_instance_unload(&alternate_instance);
        rasterfall_model_instance_unload(&instance);
        rasterfall_model_resource_unload(&resource);
        return result;
    }
    if (options.character_acceptance_model)
        return rasterfall_render_character_acceptance_capture(
            options.character_acceptance_model,
            options.character_acceptance_dir);
    if (options.eula_acceptance_models)
        return rasterfall_render_eula_animation_acceptance(
            options.eula_acceptance_models, options.eula_acceptance_dir);
    if (options.character_performance_model ||
        options.character_performance_suite)
        return rasterfall_render_character_performance(
            options.character_performance_model,
            options.character_performance_suite,
            options.performance_warmup, performance_iterations,
            options.performance_repeats, performance_workers);
    if (fb_font_load("rasterfall/assets/fonts/gb2312-16.rfh") < 0) {
        __fprintf(2, "rasterfall: cannot load GB2312 font asset\n");
        return 1;
    }
    textures_enabled = options.textures_enabled;
    requested_model_skinning = options.model_skinning;
    requested_model_pose = options.model_pose;
    rasterfall_render_set_edge_pass(edge_pass_enabled);
    if(glb_animation_model){
        struct rasterfall_model_asset animation_model;memset(&animation_model,0,sizeof(animation_model));
        if(rasterfall_model_load(&animation_model,glb_animation_model)<0||
           rasterfall_model_glb_animation_test(&animation_model,glb_animation_path,glb_animation_name)<0){
            __fprintf(2,"rasterfall: GLB animation test failed\n");rasterfall_model_unload(&animation_model);return 1;
        }
        rasterfall_model_unload(&animation_model);return 0;
    }
    if(glb_motion_model){
        struct rasterfall_model_asset motion_model;memset(&motion_model,0,sizeof(motion_model));
        if(rasterfall_model_load(&motion_model,glb_motion_model)<0||rasterfall_model_glb_motion_diagnostic(&motion_model,glb_motion_path)<0){__fprintf(2,"rasterfall: GLB motion diagnostic failed\n");return 1;}
        rasterfall_model_unload(&motion_model);return 0;
    }
    if (retarget_model_path) {
        struct rasterfall_model_asset retarget_model;
        memset(&retarget_model,0,sizeof(retarget_model));
        if(rasterfall_model_load(&retarget_model,retarget_model_path)<0){
            __fprintf(2,"rasterfall: cannot load retarget model %s\n",retarget_model_path);return 1;
        }
        if(rasterfall_model_retarget_synthetic_test(&retarget_model,retarget_action)<0){
            __fprintf(2,"rasterfall: invalid retarget synthetic action %s\n",retarget_action);rasterfall_model_unload(&retarget_model);return 1;
        }
        rasterfall_model_unload(&retarget_model);return 0;
    }
    if (humanoid_basis_model_path) {
        struct rasterfall_model_asset basis_model;
        memset(&basis_model, 0, sizeof(basis_model));
        if (rasterfall_model_load(&basis_model, humanoid_basis_model_path) < 0) {
            __fprintf(2, "rasterfall: cannot load humanoid basis model %s\n",
                      humanoid_basis_model_path); return 1;
        }
        rasterfall_model_dump_humanoid_bases(&basis_model);
        rasterfall_model_unload(&basis_model); return 0;
    }
    if (humanoid_model_path) {
        struct rasterfall_model_asset humanoid_model;
        memset(&humanoid_model, 0, sizeof(humanoid_model));
        if (rasterfall_model_load(&humanoid_model, humanoid_model_path) < 0) {
            __fprintf(2, "rasterfall: cannot load humanoid model %s\n",
                      humanoid_model_path);
            return 1;
        }
        rasterfall_model_dump_humanoid(&humanoid_model);
        rasterfall_model_unload(&humanoid_model);
        return 0;
    }
    if (bone_model_path) {
        struct rasterfall_model_asset bone_model;
        int result;
        memset(&bone_model, 0, sizeof(bone_model));
        result = rasterfall_model_load(&bone_model, bone_model_path);
        if (result < 0) {
            __fprintf(2, "rasterfall: cannot load bone model %s\n",
                      bone_model_path);
            return 1;
        }
        rasterfall_model_dump_bones(&bone_model, bone_search);
        rasterfall_model_unload(&bone_model);
        return 0;
    }
    if (performance_model_path)
        return benchmark_model_features(performance_model_path,
                                        performance_iterations,
                                        performance_workers);
    if (actor_performance)
        return rasterfall_render_actor_benchmark(performance_iterations,
                                                 performance_workers,
                                                 actor_raster_workers);
    if (view_model_path) {
        if (material_regression)
            return dump_model_material_regression(view_model_path,
                                                  view_output_dir);
        return dump_model_views(view_model_path, view_output_dir,
                                1, 1, 1, 1, 0, model_views_supersample);
    }
    {
        struct rf_core_config core_config;
        core_config.title = "Rasterfall";
        core_config.width = RASTERFALL_DEFAULT_WIDTH;
        core_config.height = RASTERFALL_DEFAULT_HEIGHT;
        core_config.input = &platform_input;
        core_config.renderer = &renderer;
        core_config.renderer_mode = options.renderer_mode ?
            RF_CORE_RENDERER_GPU_COMPUTE : RF_CORE_RENDERER_CPU;
        core_config.gpu_policy = options.renderer_mode ?
            (options.gpu_required ? RF_GPU_POLICY_REQUIRED : RF_GPU_POLICY_OPTIONAL) :
            RF_GPU_POLICY_DISABLED;
        core_config.gpu_backend = NULL;
        core_config.gpu_backend_context = NULL;
        core_config.native_present = options.gpu_native_present;
#ifdef TOYC_WINDOWS
        memset(&gpu_vulkan_context, 0, sizeof(gpu_vulkan_context));
        gpu_vulkan_context.present_fault =
            (unsigned int)options.gpu_present_fault;
        gpu_vulkan_context.present_fault_frame =
            (unsigned int)options.gpu_present_fault_frame;
        if (options.renderer_mode) {
            core_config.gpu_backend = &rf_gpu_vulkan_backend;
            core_config.gpu_backend_context = &gpu_vulkan_context;
        }
#endif
        if ((logic_test || options.render_performance || options.gpu_world_raster_view || options.environment_capture_dir || options.normal_frame_audit_output || options.character_world_capture_dir ?
             rf_core_init_headless(&core, &platform_input, &renderer) :
             rf_core_init_config(&core, &core_config)) < 0) {
            __fprintf(2, "rasterfall: cannot initialize RF Core host\n");
            return 1;
        }
        __printf("Rasterfall renderer=%s gpu_policy=%s\n",
            rf_core_renderer_name(core.gpu_frame.renderer),
            rf_gpu_policy_name(core.gpu.policy));
#ifndef TOYC_WINDOWS
        /* Each world's first CPU frame performs lazy model/resource
         * preparation on the software-render path.  The initial world starts
         * with the watchdog disabled; world transitions repeat this warm-up
         * below until their first frame presents successfully. */
        if (!options.renderer_mode)
            toy_renderer_set_frame_budget(&renderer, 0);
#endif
#ifdef TOYC_WINDOWS
        {
            char gpu_log[320];
            snprintf(gpu_log, sizeof(gpu_log),
                     "renderer=%s gpu-policy=%s gpu-state=%s adapter=%s",
                     rf_core_renderer_name(core.gpu_frame.renderer),
                     rf_gpu_policy_name(core.gpu.policy),
                     rf_gpu_state_name(core.gpu.state), core.gpu.info.adapter_name);
            rf_windows_log(gpu_log);
        }
#endif
    }
    rf_windows_log("startup: loading map");
    strcpy(host_address, "127.0.0.1");
    memset(&game_runtime, 0, sizeof(game_runtime));
    if (rf_game_init(&game_runtime, &core, &session,
                     config->options && config->options->legacy_map ?
                     "rasterfall/assets/maps/rasterfall_legacy.map" :
                     logic_test ?
                     "rasterfall/assets/maps/rasterfall.map" :
                     config->options && config->options->map_path ?
                     config->options->map_path :
                     config->map_path ? config->map_path :
                     "rasterfall/assets/maps/rasterfall.map") < 0) {
        __fprintf(2, "rasterfall: cannot load map rasterfall/assets/maps/rasterfall.map\n");
        rf_core_shutdown(&core);
        return 1;
    }
    /* The effects macro names the active pool for the helper functions below;
     * temporarily suspend it while naming the facade member itself. */
#undef effects
    active_effects = &game_runtime.effects;
#define effects (*active_effects)
#define net game_runtime.net
#define discovery game_runtime.discovery
#define audio game_runtime.audio
    game_runtime.render_context.wall_texture = NULL;
    game_runtime.render_context.model_texture = &model_texture_view;
    game_runtime.render_context.textures_enabled = textures_enabled;
    rf_windows_log("startup: map loaded, binding renderer");
    rasterfall_render_bind(&game_runtime.render_context);
    rasterfall_render_set_action_runtime_debug(action_runtime_debug);
    /* Do not auto-load the historical Eula/VMD preview.  The old path is
     * still available when explicitly requested, but normal startup should
     * use the current RFCHAR/model presentation path and must not depend on
     * private assets. */
    if (vmd_walk_model && vmd_walk_path) {
        rasterfall_render_set_vmd_walk(vmd_walk_model, vmd_walk_path);
        /* The direct VMD preview is a real skeletal animation, so start its
         * player explicitly.  Without this, the session remains at the
         * default clip_id=-1/playing=0 state and the renderer samples time 0
         * forever. */
        session.skeletal_demo_player.clip = NULL;
        session.skeletal_demo_player.clip_id = 9;
        session.skeletal_demo_player.time_ms = 0;
        session.skeletal_demo_player.playing = 1;
        session.skeletal_demo_player.loop = 1;
    }
    rasterfall_render_set_vmd_freeze(vmd_freeze_head, vmd_freeze_torso);
    rasterfall_render_set_vmd_ik_enabled(!vmd_disable_ik);
    rasterfall_render_set_vmd_grant_enabled(!vmd_disable_grant);
    rasterfall_render_set_vmd_legacy_root_offset(vmd_legacy_root_offset);
    rasterfall_render_set_vmd_legacy_knee_ccd(vmd_legacy_knee_ccd);
    rasterfall_render_set_vmd_skin_trace(vmd_skin_trace);
    rasterfall_render_bake_lightmap();
    rf_windows_log("startup: lightmap baked");
    rasterfall_effects_init(&effects);
    __printf("rasterfall: baked lightmap %dx%d\n", RF_WORLD_LIGHT_W, RF_WORLD_LIGHT_H);
    memset(&model_texture, 0, sizeof(model_texture));
    memset(&model_texture_view, 0, sizeof(model_texture_view));
    if (toy_texture_load("rasterfall/assets/textures/model_diffuse.ttex",
                         &model_texture) == 0) {
        model_texture_view.data = model_texture.data;
        model_texture_view.width = model_texture.width;
        model_texture_view.height = model_texture.height;
        model_texture_view.data_size = model_texture.data_size;
        model_texture_view.channels = model_texture.channels;
        model_texture_view.has_transparency = model_texture.has_transparency;
        __printf("rasterfall: model texture loaded (%u x %u)\n",
                 model_texture.width, model_texture.height);
    }
    if (!textures_enabled) {
        __printf("rasterfall: textures disabled, using pure colors\n");
    }
    if (logic_test || options.gpu_scene_pose_test || options.gpu_scene_native_fixture) {
        int result = options.gpu_scene_native_fixture ? rf_gpu_scene_native_fixture(
            frame_limit, options.gpu_present_fault, options.gpu_present_fault_frame) :
            options.gpu_scene_pose_test ? rf_gpu_scene_pose_logic_test() : run_logic_test();
        if (model_texture.blob) toy_texture_unload(&model_texture);
        rf_game_shutdown(&game_runtime);
        rf_core_shutdown(&core);
        return result;
    }
    /* 服务器断开（WSLg 组合器/音频服务重启）时 socket 写会触发 SIGPIPE
     * 并默认杀死进程；忽略后写返回 EPIPE，由既有错误路径接管（音频线程
     * 静默停声、wayland 发送失败则主循环干净退出）。SIG_IGN 值为 1。 */
    tlibc_sigaction(SIGPIPE, (void (*)(int))1);
    memset(&managed_terminal, 0, sizeof(managed_terminal));
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
    rasterfall_console_init(&developer_console);
    rf_gui_init(&game_runtime.gui);
    rf_gui_set_active(&game_runtime.gui, input_debug);
#else
    memset(&developer_console, 0, sizeof(developer_console));
#endif
    command_context.core = &core;
    command_context.game_runtime = &game_runtime;
    command_context.command_state = &developer_console;
    command_context.permission_level = RF_COMMAND_PERMISSION_SUPER;
    if (!textures_enabled)
        rasterfall_console_log(&developer_console,
                               RASTERFALL_CONSOLE_WARNING,
                               "textures disabled; using pure colors");
    memset(pending_key_edges, 0, sizeof(pending_key_edges));
    rasterfall_viewmodel_set_texture(&model_texture_view);
    settings.mouse_level = 3;
    settings.keyboard_level = 5;
    rasterfall_render_set_coordinate_axes(coordinate_axes);
    pause_menu.selected = PAUSE_ITEM_RESUME;
    if (options.render_performance || options.gpu_world_raster_view ||
        options.gpu_normal_view || options.gpu_wave_repro || options.environment_capture_dir ||
        options.normal_frame_audit_output ||
        options.character_world_capture_dir || world_cycle_gate) seed = 1;
    else if (__getrandom(&seed, sizeof(seed), 0) < 0)
        seed = (uint64_t)rf_core_time_us(&core);
    if (seed == 0) seed = 1;
    rasterfall_session_reset(&session, &camera, seed);
    rf_windows_log("startup: session reset");
    if (options.gpu_wave_repro) {
        game.state = TOY_GAME_PLAYING;
        game.campaign_phase = TOY_GAME_PHASE_CALM;
        game.spawn_timer_ms = 1;
        __printf("GPU-WAVE-REPRO world=%d spawn_timer_ms=%d seed=1\n",
                 session.world_id, game.spawn_timer_ms);
    }
    if ((options.render_performance || options.gpu_world_raster_view ||
         (options.gpu_normal_view &&
          strncmp(options.gpu_normal_view, "map-", 4)) ||
         options.environment_capture_dir ||
         options.normal_frame_audit_output ||
         options.character_world_capture_dir) &&
        session.world_id != RASTERFALL_WORLD_RETURN_TO_WHU_V0 &&
        rf_game_request_world(&game_runtime, RASTERFALL_WORLD_CAMPAIGN_01) < 0) {
        if (model_texture.blob) toy_texture_unload(&model_texture);
        rf_game_shutdown(&game_runtime);
        rf_core_shutdown(&core);
        return 1;
    }
    if (options.render_performance || options.gpu_world_raster_view || options.character_world_capture_dir || options.environment_capture_dir || options.normal_frame_audit_output) {
        int capture_result = options.render_performance ?
            rasterfall_render_world_benchmark(performance_iterations) :
            options.gpu_world_raster_view ?
            rasterfall_render_gpu_world_capture(options.gpu_world_raster_view,
                options.gpu_world_raster_enemies,
                options.gpu_world_raster_output) :
            options.normal_frame_audit_output ?
            rasterfall_render_normal_frame_audit(options.normal_frame_audit_x,
                options.normal_frame_audit_z, options.normal_frame_audit_sy,
                options.normal_frame_audit_cy,
                options.normal_frame_audit_pitch_sy,
                options.normal_frame_audit_pitch_cy,
                options.normal_frame_audit_width,
                options.normal_frame_audit_height,
                options.normal_frame_audit_output) :
            options.environment_capture_dir ?
            rasterfall_render_environment_capture(options.environment_capture_dir) :
            rasterfall_render_character_world_capture(
            options.character_world_capture_dir,
            options.character_world_capture_model);
        if (model_texture.blob) toy_texture_unload(&model_texture);
        rf_game_shutdown(&game_runtime);
        rf_core_shutdown(&core);
        return capture_result;
    }
    if (options.gpu_normal_view) {
        int enemy;
        struct toy_game_actor *local_actor;
        memset(game.enemies, 0, sizeof(game.enemies));
        memset(&camera, 0, sizeof(camera));
        if (!strcmp(options.gpu_normal_view, "interior")) {
            /* Inside the west maintenance building, facing its beams,
             * panels, trays, pipes and back wall. */
            camera.x = -17500; camera.z = 5000;
            camera.sy = -1024;
        } else if (!strcmp(options.gpu_normal_view, "thin-far")) {
            /* Long view toward the west building's cable trays and pipe
             * silhouette; these are the far-depth thin-structure fixture. */
            camera.x = -5000; camera.z = 5500;
            camera.sy = -1024;
        } else if (!strcmp(options.gpu_normal_view, "base")) {
            camera.x = 0; camera.z = -3400; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "spawn")) {
            camera.x = 0; camera.z = -4850; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "west-facility")) {
            camera.x = -10500; camera.z = 2000;
            camera.sy = -819; camera.cy = 614;
        } else if (!strcmp(options.gpu_normal_view, "map-wall")) {
            /* Stable V1 runtime fixture, aimed at its left wall panel. */
            camera.sy = -384; camera.cy = 949;
        } else if (!strcmp(options.gpu_normal_view, "map-ramp")) {
            camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "map-platform")) {
            camera.sy = 384; camera.cy = 949;
        } else if (!strcmp(options.gpu_normal_view, "map-label")) {
            camera.x = -1250; camera.z = -1000; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "map-sign")) {
            camera.x = 1650; camera.z = -1000; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "model-legacy")) {
            camera.x = -2600; camera.z = -9900; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "model-special")) {
            camera.x = 4900; camera.z = -10100; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "model-infected")) {
            camera.x = 9500; camera.z = -10100; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "actor-rifleman")) {
            camera.x = 1000; camera.z = -1800; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "actor-standard")) {
            camera.x = 0; camera.z = 5200; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "actor-assault")) {
            camera.x = 14000; camera.z = -2200; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "projectile")) {
            camera.z = -3400; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "pickup")) {
            camera.x = 300; camera.z = -10300; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "map-gate-on") ||
                   !strcmp(options.gpu_normal_view, "map-gate-off")) {
            camera.z = -1000; camera.cy = 1024;
            rasterfall_map_set_air_walls(&session.map_ops,
                !strcmp(options.gpu_normal_view, "map-gate-on"));
        } else if (!strcmp(options.gpu_normal_view, "map-near")) {
            camera.x = 280; camera.z = 170; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "map-thin")) {
            camera.x = 2900; camera.z = -1500; camera.cy = 1024;
        } else if (!strcmp(options.gpu_normal_view, "whu-a18")) {
            camera.x = session.level.start_x; camera.z = session.level.start_z;
            camera.sy = session.level.start_sy; camera.cy = session.level.start_cy;
        } else if (!strcmp(options.gpu_normal_view, "whu-b-plaza")) {
            camera.x = -64000; camera.z = 8000;
            camera.sy = 819; camera.cy = -614;
        } else if (!strcmp(options.gpu_normal_view, "whu-library")) {
            camera.x = 32000; camera.z = -43000;
            camera.sy = 614; camera.cy = 819;
        } else if (!strcmp(options.gpu_normal_view, "whu-d-ef")) {
            camera.x = 74000; camera.z = -48000;
            camera.cy = -1024;
        } else {
            camera.z = !strcmp(options.gpu_normal_view, "mid") ? -8400 : -3400;
            camera.cy = 1024;
        }
        camera.y = -350;
        camera.pitch_cy = 1024;
        /* Session takes the camera body from the local actor on the first
         * fixed tick. Keep the deterministic view at its requested distance. */
        local_actor = toy_game_local_player_actor(&game);
        local_actor->x = camera.x;
        local_actor->z = camera.z;
        local_actor->sy = camera.sy;
        local_actor->cy = camera.cy;
        for (enemy = 0; enemy < options.gpu_normal_enemies; ++enemy) {
            struct toy_game_enemy *fixture = &game.enemies[enemy];
            fixture->active = 1;
            fixture->hp = 100;
            fixture->type = enemy % 10 == 8 ? TOY_GAME_ENEMY_PURSUIT_FAST :
                enemy % 10 == 9 ? TOY_GAME_ENEMY_PURSUIT_HEAVY :
                                   TOY_GAME_ENEMY_PURSUIT_COMMON;
            fixture->x = (enemy % 10 - 5) * 600 + 300;
            fixture->z = -1800 + (enemy / 10) * 800;
            fixture->ground_y = 0;
            fixture->dir_z = -1024;
        }
        game.state = TOY_GAME_PLAYING;
        if (!strcmp(options.gpu_normal_view,"actor-procedural")) {
            for (int profession=0;profession<4;++profession) {
                int id=toy_game_add_ai(&game,TOY_GAME_AI_LEVEL_1,
                    (profession-2)*1000+500,-1200,"SCENE");
                if (id<1) return 1;
                struct toy_game_actor *actor=&game.actors[id-1];
                actor->character_id=RASTERFALL_CHARACTER_HURD_GUNSMITH+profession;
                actor->sy=0;actor->cy=-1024;
                actor->slots[0].weapon=TOY_GAME_WEAPON_AK;actor->current_slot=0;
                actor->ground_y=0;
            }
        }
        if (!strcmp(options.gpu_normal_view,"enemy-special") ||
            !strcmp(options.gpu_normal_view,"enemy-death") ||
            !strcmp(options.gpu_normal_view,"enemy-tongue")) {
            for (int slot=0;slot<3;++slot) {
                struct toy_game_enemy *e=&game.enemies[slot];
                memset(e,0,sizeof(*e));e->active=1;e->hp=10000;
                e->type=slot==0 ? TOY_GAME_ENEMY_SMOKER :
                    slot==1 ? TOY_GAME_ENEMY_CHARGER : TOY_GAME_ENEMY_TANK;
                e->x=(slot-1)*1600;e->z=500;e->dir_z=-1024;
                if (slot) { e->charge_active=1;e->ability.charge_elapsed_ms=200; }
                if (!strcmp(options.gpu_normal_view,"enemy-death")) {
                    e->active=2;e->dying_ms=TOY_GAME_DYING_MS-100;
                    effects.enemy_death_style[slot]=RASTERFALL_ENEMY_DEATH_STYLE_LEGACY+1;
                    effects.enemy_hit_dir_x[slot]=1024;
                }
                if (!slot && !strcmp(options.gpu_normal_view,"enemy-tongue")) {
                    e->special_target_active=1;e->special_target_kind=0;
                    e->special_pull_timer_ms=TOY_GAME_SMOKER_PULL_MS;
                }
            }
        }
        if (!strcmp(options.gpu_normal_view,"projectile")) {
            const int kinds[3]={TOY_GAME_WEAPON_BOMB,TOY_GAME_WEAPON_BOMB,
                TOY_GAME_WEAPON_MOLOTOV};
            for(int slot=0;slot<3;++slot) {
                struct toy_game_projectile *p=&game.projectiles[slot];
                memset(p,0,sizeof(*p));
                p->active=1;p->kind=kinds[slot];
                p->x=(slot-1)*900;p->z=-1300;p->y=450;
                p->age_ms=slot*175;p->flash_ms=slot==1 ? 100 : 0;
            }
        }
        __printf("GPU-NORMAL scene=%s enemies=%d seed=1\n",
                 options.gpu_normal_view, options.gpu_normal_enemies);
    }
    rf_windows_log("startup: window opened");
startup_again:
    {
        /* The Game policy owns the default landing world.  Network and
         * diagnostic modes still bypass the old selection screen explicitly. */
        int menu_selected = 1;
        strcpy(selected_address, net_address ? net_address : "127.0.0.1");
        if (!menu_selected && !run_startup_menu(&core,
                                                &requested_net_mode,
                                                selected_address,
                                                sizeof(selected_address),
                                                &net_port, &public_room,
                                                &public_room_id, &managed_spectator,
                                                startup_error,
                                                &discovery)) {
            rf_core_shutdown(&core);
            if (model_texture.blob) toy_texture_unload(&model_texture);
            rf_game_shutdown(&game_runtime);
            return 0;
        }
        rf_windows_log("startup: menu completed");
        net_address = selected_address;
        startup_error = NULL;
    }
    rasterfall_session_set_managed_ai(&session,
                                      managed_spectator &&
                                      requested_net_mode == RASTERFALL_NET_OFF);
    if (requested_net_mode == RASTERFALL_NET_HOST && public_room) {
        struct camera client_spawn;
        memcpy(&client_spawn, &camera, sizeof(client_spawn));
        client_spawn.x += 350;
        if (rasterfall_net_public_host(&net, public_room_id, &client_spawn) < 0) {
            startup_error = "PUBLIC ROOM FAILED: SERVER UNREACHABLE";
            requested_net_mode = RASTERFALL_NET_OFF;
            goto startup_again;
        }
        {
            int connect_result = wait_for_network_connection(&core, &net, "PUBLIC ROOM",
                                                             RASTERFALL_NET_PUNCH_PORT);
            if (connect_result != 0) {
                startup_error = connect_result == -2 ?
                    "PUBLIC ROOM CANCELLED" : connect_result == -3 ?
                    "PUBLIC ROOM EXISTS OR SERVER IS FULL" :
                    "PUBLIC ROOM FAILED: SERVER UNREACHABLE";
                rasterfall_net_close(&net);
                requested_net_mode = RASTERFALL_NET_OFF;
                goto startup_again;
            }
        }
        snprintf(host_address, sizeof(host_address), "ROOM %04d", public_room_id);
        __printf("rasterfall: public room %04d, punch server %s:%d\n",
                 public_room_id, RASTERFALL_NET_PUNCH_SERVER,
                 RASTERFALL_NET_PUNCH_PORT);
    } else if (requested_net_mode == RASTERFALL_NET_CLIENT && public_room) {
        if (rasterfall_net_public_connect(&net, public_room_id) < 0) {
            startup_error = "PUBLIC JOIN FAILED: CHECK NETWORK";
            requested_net_mode = RASTERFALL_NET_OFF;
            goto startup_again;
        }
        snprintf(host_address, sizeof(host_address), "ROOM %04d", public_room_id);
        __printf("rasterfall: joining public room %04d via %s:%d\n",
                 public_room_id, RASTERFALL_NET_PUNCH_SERVER,
                 RASTERFALL_NET_PUNCH_PORT);
        {
            int connect_result = wait_for_network_connection(&core, &net, host_address,
                                                             RASTERFALL_NET_PUNCH_PORT);
            if (connect_result != 0) {
                startup_error = connect_result == -2 ?
                    "PUBLIC JOIN CANCELLED" : connect_result == -3 ?
                    (net.public_error == RASTERFALL_PUBLIC_ERROR_ROOM_FULL ?
                     "PUBLIC ROOM IS FULL" :
                     "PUBLIC ROOM NOT FOUND") :
                    "PUBLIC JOIN TIMEOUT: HOST UNREACHABLE";
                rasterfall_net_close(&net);
                requested_net_mode = RASTERFALL_NET_OFF;
                goto startup_again;
            }
        }
    } else if (requested_net_mode == RASTERFALL_NET_HOST) {
        rf_windows_log("startup: creating local room");
        struct camera client_spawn;
        memcpy(&client_spawn, &camera, sizeof(client_spawn));
        client_spawn.x += 350;
        if (rasterfall_net_host(&net, net_port, &client_spawn) < 0) {
            __fprintf(2, "rasterfall: cannot host UDP port %d\n", net_port);
            rf_core_shutdown(&core);
            if (model_texture.blob) toy_texture_unload(&model_texture);
            rf_game_shutdown(&game_runtime);
            return 1;
        }
        rasterfall_net_local_address(host_address, sizeof(host_address));
        if (rasterfall_net_discovery_host_start(&discovery) < 0)
            __printf("rasterfall: LAN discovery unavailable\n");
        __printf("rasterfall: hosting UDP port %d\n", net_port);
        rf_windows_log("startup: local room ready");
        __printf("rasterfall: players can join %s:%d\n", host_address, net_port);
    } else if (requested_net_mode == RASTERFALL_NET_CLIENT) {
        if (!net_address || rasterfall_net_connect(&net, net_address, net_port) < 0) {
            startup_error = "CONNECT FAILED: CHECK IP AND PORT";
            rasterfall_net_close(&net);
            requested_net_mode = RASTERFALL_NET_OFF;
            goto startup_again;
        }
        __printf("rasterfall: connecting to %s:%d over UDP\n",
                 net_address, net_port);
        {
            int connect_result = wait_for_network_connection(&core, &net, net_address,
                                                             net_port);
            if (connect_result != 0) {
                startup_error = connect_result == -2 ?
                    "CONNECT CANCELLED" : "CONNECT TIMEOUT: HOST NOT FOUND";
                rasterfall_net_close(&net);
                requested_net_mode = RASTERFALL_NET_OFF;
                goto startup_again;
            }
        }
    }
    rasterfall_net_set_loss(&net, net_loss_percent);
    if (net_loss_percent > 0)
        __printf("rasterfall: gameplay packet loss simulation %d%%\n",
                 net_loss_percent);
    __printf("rasterfall: pause menu uses arrows + Enter; mouse/arrows look, "
             "WASD moves, click/Space fire (hold for SMG), R reload, "
             "1/2 weapons, E interact, Esc pauses/resumes\n");
    if (input_debug)
        __printf("rasterfall: input debug HUD enabled; test chords and focus changes\n");
    memset(&audio, 0, sizeof(audio));
    rasterfall_audio_load_assets(&audio);
    if (!rf_core_audio_ready(&core) ||
        rasterfall_audio_start(&audio, rf_core_audio(&core)) < 0) {
        __printf("rasterfall: audio unavailable, playing silent\n");
        rasterfall_console_log(&developer_console,
                               RASTERFALL_CONSOLE_WARNING,
                               "audio unavailable; playing silent");
        rf_windows_log("startup: audio unavailable");
    } else {
        rf_windows_log("startup: audio ready");
    }
    pointer_lock_requested = rf_core_set_pointer_lock(&core, 1) > 0;
    last_time = rf_core_begin_tick(&core);
    fps_window_start = last_time;
    rasterfall_perf_init(&stats);
    rasterfall_perf_init(&stats_total);
    while (running && !rf_core_should_exit(&core)) {
        int64_t now, elapsed, t_frame, t_stage;
        int64_t audit_loop_start = rf_core_time_us(&core);
        if (world_cycle_gate &&
            (rendered_frames == 30 || rendered_frames == 60 ||
             rendered_frames == 90)) {
            enum rasterfall_world_id next_world = rendered_frames == 30 ?
                RASTERFALL_WORLD_CAMPAIGN_01 : rendered_frames == 60 ?
                RASTERFALL_WORLD_RETURN_TO_WHU_V0 :
                RASTERFALL_WORLD_CAMPAIGN_01;
            if (rf_game_request_world(&game_runtime, next_world) < 0) {
                __fprintf(2, "rasterfall: world cycle gate failed at frame %d world %d\n",
                          rendered_frames, next_world);
                running = 0;
                break;
            }
            __printf("GPU-WORLD-CYCLE frame=%d world=%d seed=%llu\n",
                     rendered_frames, next_world,
                     (unsigned long long)session.seed);
            {
                struct rasterfall_resource_stats cycle_resources;
                char cycle_line[192];
                rasterfall_resources_stats(rasterfall_render_resources(),
                                            &cycle_resources);
                snprintf(cycle_line, sizeof(cycle_line),
                    "GPU-WORLD-CYCLE-RESOURCES frame=%d world=%d live=%u retired=%u pinned=%u loads=%u releases=%u",
                    rendered_frames, next_world, cycle_resources.live,
                    cycle_resources.retired, cycle_resources.pinned,
                    cycle_resources.loads, cycle_resources.releases);
                __printf("%s\n", cycle_line);
                rf_windows_log(cycle_line);
            }
        }
        int64_t audit_update_us = 0, audit_render_us = 0;
        int64_t audit_present_us = 0, audit_interval_us = 0;
        int64_t audit_prepare_us = 0;
        int logic_steps = 0;
        int resumed = 0;
        int ready;
        static int logged_first_frame;
        unsigned char game_events[TOY_GAME_MAX_EVENTS];
        int game_event_count;
        if (rf_core_poll_events(&core) < 0) break;
        if (rf_core_get_input_frame(&core, &input) < 0) break;
        /* 非阻塞收输入：present 后立刻开始下一帧 CPU 工作，组合器处理
         * 已提交缓冲的时间被渲染流水线掩盖（双缓冲）。 */
        events = *rf_core_events(&core);
        rasterfall_net_poll(&net);
        if (net.mode == RASTERFALL_NET_HOST && discovery.fd >= 0) {
            int players = 1;
            for (int discovery_i = 0;
                 discovery_i < RASTERFALL_NET_CLIENT_MAX; discovery_i++)
                if (net.clients[discovery_i].active &&
                    net.clients[discovery_i].connected)
                    players++;
            rasterfall_net_discovery_poll(&discovery, "LOCAL ROOM", net_port,
                                          players, RASTERFALL_NET_PLAYER_MAX, 0);
        }
        rasterfall_net_update_connection(&net);
        if (net.mode == RASTERFALL_NET_CLIENT && net.remote_event_count > 0) {
            if (audio.running)
                rasterfall_audio_play_events(&audio, net.remote_events,
                                             net.remote_event_count);
        }
        if (net.mode == RASTERFALL_NET_CLIENT) {
            rasterfall_net_reconcile_client(&net, &session, &camera);
            rasterfall_net_update_presentation(&net, 16);
        }
        if (net.mode == RASTERFALL_NET_CLIENT && game.actors[0].active) {
            const struct toy_game_actor *local_actor = &game.actors[0];
            sync_network_fire_effects(&camera, &camera, 0,
                                      toy_game_actor_current_weapon(local_actor),
                                      local_actor->fire_seq,
                                      local_actor->ray_count,
                                      local_actor->rays, NULL);
        }
        if (net.mode == RASTERFALL_NET_HOST) {
            for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
                const struct rasterfall_net_client *client = &net.clients[i];
                const struct toy_game_actor *actor;
                int actor_index = TOY_GAME_REMOTE_ACTOR_BASE +
                                  client->client_id - 1;
                int weapon;
                if (!client->active || !client->connected || actor_index < 0 ||
                    actor_index >= TOY_GAME_MAX_ACTORS) continue;
                actor = &game.actors[actor_index];
                if (!actor->active || actor->kind != TOY_GAME_ACTOR_PLAYER)
                    continue;
                weapon = toy_game_actor_current_weapon(actor);
                sync_network_fire_effects(&camera, &client->camera,
                                          client->client_id, weapon,
                                          actor->fire_seq, actor->ray_count,
                                          actor->rays, &audio);
            }
        } else if (net.mode == RASTERFALL_NET_CLIENT) {
            for (int i = 0; i < net.actor_count; i++) {
                const struct rasterfall_net_actor *actor = &net.actors[i];
                int player_id = actor->actor_index - TOY_GAME_REMOTE_ACTOR_BASE + 1;
                if (!actor->active || player_id <= 0 ||
                    player_id >= RASTERFALL_NET_PLAYER_MAX) continue;
                sync_network_fire_effects(&camera,
                                          &net.remote_render_camera[player_id],
                                          player_id, actor->weapon,
                                          actor->fire_seq, actor->ray_count,
                                          actor->rays, &audio);
            }
        }
        /* 本帧到达的按压边沿并入保留位，再把保留位全部合入 key_pressed
         * 供顶部消费方（菜单/射击）读取。保留位在逻辑步跑过的那帧末尾
         * 才清除，因此不跑逻辑步的帧不会吞掉 E/R 等按键。 */
        for (int k = 0; k < TOY_INPUT_KEY_COUNT; k++) {
            if (input.key_pressed[k]) pending_key_edges[k] = 1;
            if (pending_key_edges[k]) input.key_pressed[k] = 1;
        }
        if (events.keyboard_focus_changed && !events.keyboard_focused)
            memset(pending_key_edges, 0, sizeof(pending_key_edges));
        if (events.key_event_count > 0) {
            int at = events.key_event_count - 1;
            last_key = events.key_events[at].key;
            last_key_pressed = events.key_events[at].pressed;
            have_last_key = 1;
            input_event_count += events.key_event_count;
        }
        if (events.pointer_lock_changed) {
            if (events.pointer_locked)
                __printf("rasterfall: pointer constraint activated\n");
            else {
                __printf("rasterfall: pointer constraint released\n");
                pointer_lock_requested = 0;
            }
        }
        if (!developer_console.open && pending_key_edges[KEY_F12]) {
            pending_key_edges[KEY_F12] = 0;
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
            if (!game_runtime.gui.active) {
                paused = 1;
                rf_gui_set_active(&game_runtime.gui, 1);
                rf_core_set_pointer_lock(&core, 0);
                pointer_lock_requested = 0;
            }
#else
            session.banner_ms = 2200;
            session.banner_success = 0;
            session.banner_text = RASTERFALL_DESKTOP_UNAVAILABLE_MESSAGE;
#endif
        }
        if (!developer_console.open && game_runtime.gui.active &&
            pending_key_edges[KEY_ESC]) {
            pending_key_edges[KEY_ESC] = 0;
            rf_gui_close_all_windows(&game_runtime.gui);
            rf_gui_set_active(&game_runtime.gui, 0);
            rf_gui_set_icon_count(&game_runtime.gui, RF_GUI_ICON_COUNT);
            rf_app_manager_register_defaults(&game_runtime.app_manager);
            rf_app_manager_set_query_context(&game_runtime.app_manager,
                                             &game_runtime.application_query);
            paused = 0;
            rf_core_set_pointer_lock(&core, 1);
            pointer_lock_requested = 1;
        }
        if (game_runtime.gui.active && !developer_console.open)
            rf_gui_handle_input(&game_runtime.gui, &input,
                                events.button_pressed, events.button);
        if (game_runtime.gui.active && !developer_console.open)
            rf_app_manager_update(&game_runtime.app_manager, &input, 16);
        if (!developer_console.open && pending_key_edges[KEY_GRAVE]) {
            pending_key_edges[KEY_GRAVE] = 0;
#if RASTERFALL_DESKTOP_RUNTIME_ENABLED
            developer_console.open = 1;
            developer_console.was_paused = paused;
            developer_console.terminal.input[0] = 0;
            rasterfall_console_log(&developer_console,
                                   RASTERFALL_CONSOLE_INFO,
                                   "developer console opened");
            rf_core_set_pointer_lock(&core, 0);
            pointer_lock_requested = 0;
#else
            session.banner_ms = 2200;
            session.banner_success = 0;
            session.banner_text = RASTERFALL_CONSOLE_UNAVAILABLE_MESSAGE;
#endif
        }
        {
            int console_was_open = developer_console.open;
            if (developer_console.open)
                rasterfall_console_handle_input_context(
                    &developer_console, &input, pending_key_edges,
                    &command_context);
            if (developer_console.pose_hud_request != 0) {
                session.pose_debug_active =
                    developer_console.pose_hud_request > 0;
                if (session.pose_debug_active) {
                    session.pose_editor = developer_console.calibration;
                    session.pose_editor.active = 1;
                    session.pose_debug_bone = 0;
                    session.pose_debug_axis = 0;
                    session.skeletal_demo_player.clip_id =
                        session.pose_editor.animation_base ? 11 : -1;
                    session.skeletal_demo_player.time_ms = 0;
                    session.skeletal_demo_player.playing =
                        session.pose_editor.animation_base != 0;
                }
                developer_console.pose_hud_request = 0;
            }
            if (developer_console.close_requested) {
                developer_console.close_requested = 0;
                developer_console.open = 0;
            }
            if (console_was_open && !developer_console.open) {
                rasterfall_console_log(&developer_console,
                                       RASTERFALL_CONSOLE_INFO,
                                       "developer console closed");
                int capture_result = rf_core_set_pointer_lock(&core, 1);
                pointer_lock_requested = capture_result > 0;
                paused = developer_console.was_paused;
            }
        }
        if (developer_console.killall_requested) {
            int killed = rasterfall_session_dev_killall(&session);
            developer_console.killall_requested = 0;
            {
                char message[64];
                snprintf(message, sizeof(message),
                         "killed %d active enemies", killed);
                rasterfall_console_log(&developer_console,
                                       RASTERFALL_CONSOLE_WARNING, message);
            }
        }
        if (developer_console.give_requested > 0) {
            int amount = developer_console.give_requested;
            rasterfall_session_dev_give_money(&session, amount);
            developer_console.give_requested = 0;
            {
                char message[64];
                snprintf(message, sizeof(message), "money +%d", amount);
                rasterfall_console_log(&developer_console,
                                       RASTERFALL_CONSOLE_INFO, message);
            }
        }
        if (developer_console.open) {
            fire_edge = 0;
            shove_edge = 0;
            pointer_turn_pending = 0;
            pointer_pitch_pending = 0;
        }
        if (managed_spectator && !developer_console.open &&
            !managed_terminal.open && !paused &&
            managed_terminal_take_key(&input, pending_key_edges, KEY_F2)) {
            managed_terminal.open = 1;
            managed_terminal.line[0] = 0;
            strcpy(managed_terminal.message, "TYPE HELP");
            rf_core_set_pointer_lock(&core, 0);
            pointer_lock_requested = 0;
            paused = 1;
        }
        {
            int terminal_was_open = managed_terminal.open;
            if (developer_console.open)
                ;
            else if (managed_terminal.open)
                managed_terminal_input(&managed_terminal, &input,
                                       pending_key_edges, &session, &camera);
            if (terminal_was_open && !managed_terminal.open) {
                int capture_result = rf_core_set_pointer_lock(&core, 1);
                pointer_lock_requested = capture_result > 0;
                paused = 0;
                pointer_turn_pending = 0;
                pointer_pitch_pending = 0;
            }
        }
        if (!managed_terminal.open && paused &&
            game.state == TOY_GAME_PLAYING) {
            int resume_requested = 0;
            /* 菜单导航使用独立节流；Wayland/键盘自动重复可能在一帧内
             * 送来多次边沿，不能让选项随帧率飞快滚动。 */
            int up = pending_key_edges[KEY_UP];
            int down = pending_key_edges[KEY_DOWN];
            if (up > 0 || down > 0) {
                int64_t menu_now = rf_core_time_us(&core);
                if (menu_now >= menu_nav_ready_us) {
                    if (up > 0) {
                        pause_menu.selected--;
                        if (pause_menu.selected < 0)
                            pause_menu.selected += PAUSE_ITEM_COUNT;
                    } else {
                        pause_menu.selected++;
                        if (pause_menu.selected >= PAUSE_ITEM_COUNT)
                            pause_menu.selected -= PAUSE_ITEM_COUNT;
                    }
                    menu_nav_ready_us = menu_now + 180000;
                }
                pending_key_edges[KEY_UP] = 0;
                pending_key_edges[KEY_DOWN] = 0;
            }
            {
                int change = pending_key_edges[KEY_RIGHT] -
                             pending_key_edges[KEY_LEFT];
                if (change != 0) {
                    if (pause_menu.selected == PAUSE_ITEM_MOUSE)
                        settings.mouse_level = clampi(settings.mouse_level + change, 0, 15);
                    else if (pause_menu.selected == PAUSE_ITEM_COORDS) {
                        coordinate_axes = change > 0 ? 1 : 0;
                        rasterfall_render_set_coordinate_axes(coordinate_axes);
                    }
                    else if (pause_menu.selected == PAUSE_ITEM_KEYBOARD)
                        settings.keyboard_level = clampi(settings.keyboard_level + change, 0, 15);
                    pending_key_edges[KEY_RIGHT] = 0;
                    pending_key_edges[KEY_LEFT] = 0;
                }
            }
            if (pending_key_edges[KEY_ENTER]) {
                pending_key_edges[KEY_ENTER] = 0;
                if (pause_menu.selected == PAUSE_ITEM_RESUME)
                    resume_requested = 1;
                else if (pause_menu.selected == PAUSE_ITEM_COORDS) {
                    coordinate_axes = !coordinate_axes;
                    rasterfall_render_set_coordinate_axes(coordinate_axes);
                }
                else if (pause_menu.selected == PAUSE_ITEM_EXIT) {
                    /* UI requests termination through Core so the host keeps
                     * ownership of the actual shutdown sequence. */
                    rf_core_request_exit(&core);
                    running = 0;
                }
            }
            if (pending_key_edges[KEY_ESC]) {
                pending_key_edges[KEY_ESC] = 0;
                resume_requested = 1;
            }
            if (resume_requested) {
            int capture_result = rf_core_set_pointer_lock(&core, 1);
            pointer_lock_requested = capture_result > 0;
            paused = 0;
            pointer_turn_pending = 0;
            pointer_pitch_pending = 0;
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
            resumed = 1;
            __printf("rasterfall: resumed, pointer constraint %s\n",
                     pointer_lock_requested ? "requested" : "unavailable");
            }
        }
        if (!paused && !resumed && !session.shop_open &&
            toy_input_pressed(&input, KEY_ESC)) {
            if (game.state == TOY_GAME_OVER || game.state == TOY_GAME_WON)
                running = 0;
            else {
                rf_core_set_pointer_lock(&core, 0);
                pointer_lock_requested = 0;
                paused = 1;
                pointer_turn_pending = 0;
                pointer_pitch_pending = 0;
                pause_menu.selected = PAUSE_ITEM_RESUME;
                pending_key_edges[KEY_ESC] = 0;
                __printf("rasterfall: paused, pointer released\n");
            }
        }
        /* 射击输入：每帧只取一次边沿（恢复点击帧不开火） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_LEFT)
            fire_edge = 1;
        if (!paused && !resumed && toy_input_pressed(&input, KEY_ENTER))
            fire_edge = 1;
        /* 推开输入：右键与开火同一套边沿锁存（恢复点击帧不算） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_RIGHT)
            shove_edge = 1;
        if (rf_core_should_exit(&core)) running = 0;
        if (!running) break;
        /* --auto：炮弹幕压测（复现崩溃用）。瞬移到关键区域（起点室/
         * 开发者区/中心/刷怪区），快速转枪口持续轰击：弹道终点大量落
         * 在屏幕边缘/屏外（裁剪路径）、穿门洞长弹道（最大射程）、
         * 近距离墙面（命中火花）。 */
        if (auto_mode) {
            static const int spot_x[8] = {
                0, 0, 0, 0, -5500, 4500, -6000, 2000
            };
            static const int spot_z[8] = {
                -5000, -6400, -9000, 0, -1500, 500, -2000, 2500
            };
            int idx;
            if (rendered_frames == 60 && paused) {
                paused = 0;
                __printf("rasterfall: auto barrage started\n");
            }
            if (rendered_frames > 60) {
                if (game.state != TOY_GAME_PLAYING)
                    input.key_pressed[KEY_R] = 1;   /* 死亡重开 */
                fire_edge = 1;
                /* Long-running GPU/presenter soak also needs authoritative
                 * gameplay motion instead of a stationary firing camera.
                 * Alternate forward/strafe input and request a periodic jump;
                 * teleport remains the bounded scene-coverage reset. */
                input.key_down[KEY_W] = ((rendered_frames / 120) & 1) == 0;
                input.key_down[KEY_S] = !input.key_down[KEY_W];
                input.key_down[KEY_A] = ((rendered_frames / 60) & 1) == 0;
                input.key_down[KEY_D] = !input.key_down[KEY_A];
                if (rendered_frames % 90 == 0)
                    input.key_pressed[KEY_SPACE] = 1;
                /* 只在水平面扫射：向上俯仰会让大部分几何体离开视锥，
                 * 帧数虚高，无法反映真实渲染负载。 */
                rasterfall_camera_rotate(&camera, 37, 0);
                idx = rendered_frames / 60;
                if (rendered_frames % 60 == 0 && idx < 32) {
                    static const int wslot[3] = {TOY_GAME_WEAPON_PISTOL,
                                                 TOY_GAME_WEAPON_SMG,
                                                 TOY_GAME_WEAPON_SHOTGUN};
                    camera.x = spot_x[idx % 8];
                    camera.z = spot_z[idx % 8];
                    toy_game_local_player_actor(&game)->x = camera.x;
                    toy_game_local_player_actor(&game)->z = camera.z;
                    toy_game_actor_equip_weapon(
                        &game, toy_game_local_player_actor(&game),
                        wslot[idx % 3]);
                    __printf("rasterfall: auto teleport %d to (%d,%d) w=%d\n",
                             idx, camera.x, camera.z, wslot[idx % 3]);
                }
            }
        }
        /* Some compositors acknowledge locked asynchronously. Relative
         * events received after our accepted request are already valid. */
        if (!paused && (input.pointer_locked || pointer_lock_requested) &&
            events.relative_moved) {
            accumulate_mouse_look(&pointer_turn_pending, &pointer_pitch_pending,
                                  input.relative_x, input.relative_y, &settings);
        } else if (!paused && pointer_lock_requested && input.pointer_moved) {
            if (have_pointer_position)
                accumulate_mouse_look(&pointer_turn_pending, &pointer_pitch_pending,
                                      input.pointer_x - last_pointer_x,
                                      input.pointer_y - last_pointer_y, &settings);
            last_pointer_x = input.pointer_x;
            last_pointer_y = input.pointer_y;
            have_pointer_position = 1;
        }
        now = rf_core_begin_tick(&core);
        elapsed = now - last_time;
        last_time = now;
        if (options.gpu_normal_fixed_tick) elapsed = FIXED_STEP_US;
        if (elapsed < 0) elapsed = 0;
        if (elapsed > MAX_FRAME_US) elapsed = MAX_FRAME_US;
        accumulator += elapsed;
        t_stage = now;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            if (!paused && !managed_terminal.open) {
                struct rasterfall_command command;
                int shop_input = session.shop_open;
                int shop_enter = toy_input_pressed(&input, KEY_ENTER);
                int shop_page_before = session.shop_page;
                int shop_selected_before = session.shop_selected;
                session.shop_request_only = net.mode == RASTERFALL_NET_CLIENT;
                if (shop_input) {
                    rasterfall_session_shop_input(
                        &session,
                        toy_input_pressed(&input, KEY_UP),
                        toy_input_pressed(&input, KEY_DOWN),
                        toy_input_pressed(&input, KEY_LEFT),
                        toy_input_pressed(&input, KEY_RIGHT),
                        toy_input_pressed(&input, KEY_ENTER),
                        toy_input_pressed(&input, KEY_ESC));
                    input.key_pressed[KEY_UP] = 0;
                    input.key_pressed[KEY_DOWN] = 0;
                    input.key_pressed[KEY_ENTER] = 0;
                    input.key_pressed[KEY_ESC] = 0;
                }
                if (net.mode == RASTERFALL_NET_CLIENT && net.spawn_pending) {
                    camera.x = net.client_spawn_base.x;
                    camera.z = net.client_spawn_base.z;
                    camera.sy = net.client_spawn_base.sy;
                    camera.cy = net.client_spawn_base.cy;
                    camera.pitch_sy = net.client_spawn_base.pitch_sy;
                    camera.pitch_cy = net.client_spawn_base.pitch_cy;
                    net.spawn_pending = 0;
                }
                if (net.mode == RASTERFALL_NET_HOST &&
                    toy_game_local_player_actor_const(&game)->state ==
                        TOY_GAME_ACTOR_DOWNED) {
                    /* The render-only spectator camera must not become the
                     * authoritative body position on the next tick. */
                    camera.x = toy_game_local_player_actor_const(&game)->x;
                    camera.z = toy_game_local_player_actor_const(&game)->z;
                }
                if (game.state == TOY_GAME_PLAYING &&
                    !(net.mode == RASTERFALL_NET_CLIENT &&
                      (!net.connected || !net.world_ready))) {
                    if (shop_input)
                        memset(&command, 0, sizeof(command));
                    else
                        if (developer_console.open)
                            memset(&command, 0, sizeof(command));
                        else
                            build_game_command(&command, &input, &settings,
                                               pending_key_edges,
                                               fire_edge, shove_edge,
                                               pointer_turn_pending,
                                               pointer_pitch_pending);
                    if (toy_game_local_player_actor_const(&game)->state ==
                            TOY_GAME_ACTOR_DOWNED &&
                        (command.buttons & RASTERFALL_CMD_FLAG)) {
                        command.buttons &= ~RASTERFALL_CMD_FLAG;
                        command.buttons |= RASTERFALL_CMD_REVIVE;
                    }
                    if (net.mode == RASTERFALL_NET_CLIENT && shop_input &&
                        shop_enter && shop_page_before > 0) {
                        command.buttons |= RASTERFALL_CMD_SHOP;
                        if (shop_page_before == 1) {
                            static const int shop_weapons[] = {
                                TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN,
                                TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP,
                                TOY_GAME_WEAPON_AXE, TOY_GAME_WEAPON_BOMB,
                                TOY_GAME_WEAPON_MOLOTOV, TOY_GAME_WEAPON_PILL };
                            command.shop_action = 1;
                            command.shop_item = shop_weapons[shop_selected_before];
                        } else if (shop_page_before == 2) {
                            command.shop_action = 2;
                            command.shop_item = shop_selected_before;
                        } else if (shop_page_before == 3) {
                            command.shop_action = 3;
                            command.shop_item = 0;
                        } else if (shop_page_before == 5) {
                            command.shop_action = 4;
                            command.shop_item = session.assignment_flag;
                            command.shop_arg = rasterfall_session_shop_actor_at(
                                &session, session.assignment_flag,
                                shop_selected_before);
                        } else if (shop_page_before == 6) {
                            int upgrade_indices[TOY_GAME_MAX_ACTORS];
                            int upgrade_count = 0;
                            for (int ai = 0; ai < TOY_GAME_REMOTE_ACTOR_BASE; ai++)
                                if (session.game_state.actors[ai].active &&
                                    session.game_state.actors[ai].kind == TOY_GAME_ACTOR_AI &&
                                    session.game_state.actors[ai].hired)
                                    upgrade_indices[upgrade_count++] = ai;
                            command.shop_action = 5;
                            command.shop_item = shop_selected_before < upgrade_count ?
                                upgrade_indices[shop_selected_before] : -1;
                        } else if (shop_page_before == 8) {
                            static const int ai_weapons[] = { TOY_GAME_WEAPON_PISTOL,
                                TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN,
                                TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP };
                            command.shop_action = 6;
                            command.shop_item = session.assignment_flag;
                            command.shop_arg = ai_weapons[shop_selected_before];
                        }
                    }
                    if (toy_input_down(&input, KEY_TAB) &&
                        toy_input_pressed(&input, KEY_R)) {
                        command.buttons &= ~RASTERFALL_CMD_RELOAD;
                        command.buttons |= RASTERFALL_CMD_CLEAR_STATS;
                        if (net.mode != RASTERFALL_NET_CLIENT) {
                            struct toy_game_actor *local_player =
                                toy_game_local_player_actor(&game);
                            local_player->kills = 0;
                            local_player->special_kills = 0;
                            local_player->damage_dealt = 0;
                            local_player->throwable_damage_dealt = 0;
                        }
                    }
                    capture_jump_vector(&command, &camera);
                    game_runtime.camera = camera;
                    game_runtime.lifecycle_paused = paused;
                    rf_game_update(&game_runtime, &command,
                                   FIXED_STEP_US / 1000);
                    if (options.gpu_wave_repro) {
                        static int previous_phase = -1, previous_alive = -1;
                        if (previous_phase != game.campaign_phase ||
                            previous_alive != game.enemies_alive) {
                            __printf("GPU-WAVE-REPRO wave=%d phase=%d alive=%d queued=%d timer=%d\n",
                                     game.wave, game.campaign_phase,
                                     game.enemies_alive, game.to_spawn,
                                     game.spawn_timer_ms);
                            previous_phase = game.campaign_phase;
                            previous_alive = game.enemies_alive;
                        }
                    }
                    camera = game_runtime.camera;
                    if (game_runtime.gui.active) {
                        paused = 1;
                        rf_core_set_pointer_lock(&core, 0);
                        pointer_lock_requested = 0;
                    }
                    consume_game_command_edges(&input, pending_key_edges);
                    pointer_turn_pending = 0;
                    pointer_pitch_pending = 0;
                    fire_edge = 0;
                    shove_edge = 0;
                } else if (toy_input_pressed(&input, KEY_R)) {
                    /* 死亡或通关结算：R 重开 */
                    memset(&command, 0, sizeof(command));
                    command.buttons = RASTERFALL_CMD_RESET;
                    /* Reset is host-authoritative; the update facade owns
                     * both the client request and host reset path. */
                    game_runtime.camera = camera;
                    game_runtime.lifecycle_paused = paused;
                    rf_game_update(&game_runtime, &command,
                                   FIXED_STEP_US / 1000);
                    camera = game_runtime.camera;
                    input.key_pressed[KEY_R] = 0;
                    fire_edge = 0;
                    shove_edge = 0;
                }
            }
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        /* 会话事件只取出一次，再分发给音频以及未来的网络/展示消费者。
         * 音频不可用时仍清空本 tick 事件，避免单消费者队列永久塞满。 */
        game_event_count = toy_game_drain_events(&game, game_events,
                                                 TOY_GAME_MAX_EVENTS);
        if (audio.running && (game_event_count > 0
#ifdef TOYC_WINDOWS_SINGLE_THREAD
                              || 1
#endif
                              ))
            rasterfall_audio_play_events(&audio, game_events, game_event_count);
        if (!paused) {
            sync_fire_effects(&camera);
            sync_ai_fire_effects(&camera, &audio);
        }
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        /* 本帧跑过逻辑步：所有保留边沿都已暴露给消费方，可以清除；
         * 一帧都没跑（accumulator 不足，长 stall 后常见）则留到下一帧，
         * 避免按键被吞。 */
        if (logic_steps > 0)
            memset(pending_key_edges, 0, sizeof(pending_key_edges));
        rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_LOGIC, &t_stage, 0, 0);
        audit_update_us = rf_core_time_us(&core) - now;
        /* 帧渲染计时从申请缓冲开始；双缓冲占用时的等待计入 stall。
         * 帧间隔：本次 begin_frame 距上次的墙钟时间 wall，与上次渲染
         * 帧的活跃时间相减得到 wait（轮询/逻辑/调度/组合器等待），
         * 用来解释平均帧率与各阶段活跃耗时之间的缺口。 */
        /* 帧间隔：本次 begin_frame 距上次的墙钟时间 wall（含双缓冲等待、
         * 事件轮询、逻辑与调度）。循环会在双缓冲被占用时反复走 stall 路径
         * 而不渲染，wall 按迭代累计、除以渲染帧数即 1/fps；wait（present
         * 到下一次 begin 的间隔）在 dump 中用 wall − 活跃帧时间推导，
         * 与各阶段统计严格对消。 */
        t_frame = rf_core_time_us(&core);
        if (prev_begin > 0) {
            audit_interval_us = t_frame - prev_begin;
            rasterfall_perf_add_interval(&stats, &stats_total, audit_interval_us);
        }
        prev_begin = t_frame;
        t_stage = t_frame;
        if (options.frame_audit || options.gpu_scene_world_preview)
            rf_gpu_scene_enemy_begin(session.scene_local.frame_id+1,
                session.scene_local.world_generation);
        ready = options.gpu_scene_independent_preview ?
            rf_core_begin_scene_frame(&core) : rf_core_begin_frame(&core, 0x151922);
        surface = *rf_core_surface(&core);
        if (ready < 0) break;
        if (ready == 0) {
            struct toy_window_events stall_events;
            /* 双缓冲都在组合器手里：阻塞等 buffer release，期间
             * 继续收输入。等待批次必须立即并入输入状态——若沿用共用
             * events，下一轮 poll 会覆盖这批事件，按键释放事件丢失后
             * key_down 无法清零，角色会持续移动不受控制（粘键）。 */
            if (rf_core_poll_events_timeout(&core, 1000) < 0) break;
            stall_events = *rf_core_events(&core);
            /* stall 从申请缓冲计到等回 buffer release（含 poll 等待），
             * 即 wait 中双缓冲背压的部分。frame callback 只作为组合器
             * 节奏提示，不再阻止 CPU 使用另一个空闲 shm buffer。 */
            rasterfall_perf_add_stall(&stats, &stats_total,
                                      rf_core_time_us(&core) - t_frame);
            /* 等待批次的按键边沿不能丢，也不能重复：菜单块在迭代顶部已
             * 消费过本迭代的事件，此时 key_pressed 里可能残留旧边沿
             * （begin_frame 只在迭代顶部清）——再读 key_pressed 锁存
             * 会让一次按键触发两次（暂停后立即恢复、按一下菜单动两格）。
             * 改为把本批次所有按下事件逐键记入 pending_key_edges，由
             * 帧顶合入 key_pressed，各消费方按当时状态决定语义（如
             * Esc 在暂停和游戏中的含义不同）。E/R/1/2/逗号句号等所有
             * 按键都走这条路径，不再吞键；释放事件已由 apply 更新
             * key_down，不会粘键。BTN_LEFT 不在按键表里，仍需单独
             * 锁存 fire_edge。 */
            if (stall_events.keyboard_focus_changed &&
                !stall_events.keyboard_focused)
                memset(pending_key_edges, 0, sizeof(pending_key_edges));
            for (int i = 0; i < stall_events.key_event_count; i++) {
                unsigned int k = stall_events.key_events[i].key;
                if (stall_events.key_events[i].pressed &&
                    k < TOY_INPUT_KEY_COUNT)
                    pending_key_edges[k] = 1;
            }
            if (!paused && !resumed && stall_events.button_pressed &&
                stall_events.button == BTN_LEFT)
                fire_edge = 1;
            if (!paused && !resumed && stall_events.button_pressed &&
                stall_events.button == BTN_RIGHT)
                shove_edge = 1;
            continue;
        }
        if (ready > 0) {
            int present_result;
            /* Local movement is client-authoritative; host position
             * corrections are intentionally not applied to the camera. */
            if (!logged_first_frame) {
                rf_windows_log("startup: first frame begin");
                logged_first_frame = 1;
            }
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_BEGIN,
                           &t_stage, 0, 0);
            game_runtime.camera = camera;
            game_runtime.lifecycle_paused = paused;
            game_runtime.managed_spectator = managed_spectator;
            game_runtime.managed_third_person = managed_third_person;
            game_runtime.coordinate_axes = coordinate_axes;
            game_runtime.display_fps = display_fps;
            game_runtime.debug_input_enabled = input_debug;
            game_runtime.mouse_level = settings.mouse_level;
            game_runtime.keyboard_level = settings.keyboard_level;
            game_runtime.pause_menu_selected = pause_menu.selected;
            game_runtime.managed_terminal_open = managed_terminal.open;
            strcpy(game_runtime.managed_terminal_line, managed_terminal.line);
            strcpy(game_runtime.managed_terminal_message,
                   managed_terminal.message);
            game_runtime.input_frame = input;
            game_runtime.host_port = net_port;
            game_runtime.last_key = last_key;
            game_runtime.last_key_pressed = last_key_pressed;
            game_runtime.have_last_key = have_last_key;
            game_runtime.input_event_count = input_event_count;
            game_runtime.console = developer_console;
            game_runtime.render_context.character_gpu_skinning =
                options.gpu_character_skinning;
            game_runtime.render_context.character_cpu_reference =
                !options.gpu_character_skinning ||
                (options.gpu_character_vertex_diff && rendered_frames + 1 == 30);
            core.gpu_frame.character_skinning=options.gpu_character_skinning;
            core.gpu_frame.character_vertex_diff_requested=
                options.gpu_character_vertex_diff && rendered_frames + 1 == 30;
#ifndef TOYC_WINDOWS
            if (!options.renderer_mode &&
                watchdog_warm_world != (unsigned int)session.world_id)
                toy_renderer_set_frame_budget(&renderer, 0);
#endif
            {
                int64_t audit_render_start = rf_core_time_us(&core);
                /* Explicit Scene audits include cold asset/pose extraction.
                 * Their harness owns the timeout, not the interactive budget. */
                if (options.frame_audit || options.gpu_scene_world_preview)
                    toy_renderer_set_frame_budget(&renderer,0);
                audit_prepare_us = audit_render_start - audit_loop_start;
                if (options.gpu_scene_independent_preview) {
                    rf_game_prepare_render_camera(&game_runtime);
                    game_runtime.scene_pixels = 0;
                    game_runtime.render_context.mixed_frame = NULL;
                } else if (rf_game_render_profiled(&game_runtime, &renderer, &surface,
                                        &stats, &stats_total) < 0) {
                    rf_core_mixed_fail(&core);
                    if (rf_core_runtime_failed(&core)) {
                        __fprintf(2,
                            "rasterfall: GPU-required render contract failed\n");
                        break;
                    }
                    __fprintf(2,
                        "rasterfall: skipped frame after renderer watchdog timeout\n");
                    continue;
                }
                audit_render_us = rf_core_time_us(&core) - audit_render_start;
            }
            scene_pixels = game_runtime.scene_pixels;
            if (options.gpu_frame_capture &&
                rendered_frames + 1 == options.gpu_capture_frame)
                core.gpu_frame.capture_path = options.gpu_frame_capture;
            t_stage = rf_core_time_us(&core);
            present_result = options.gpu_scene_independent_preview ? 0 : options.gpu_scene_world_preview ?
                rf_core_finish_scene_recording(&core) : rf_core_end_frame(&core);
            audit_present_us = rf_core_time_us(&core) - t_stage;
            if (present_result < 0) {
                __fprintf(2, "rasterfall: frame presentation failed%s\n",
                    rf_core_runtime_failed(&core) ?
                    " (GPU-required contract violation)" : "");
                break;
            }
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_PRESENT,
                           &t_stage, 0, 0);
            rendered_frames++;
#ifndef TOYC_WINDOWS
            if (!options.renderer_mode &&
                watchdog_warm_world != (unsigned int)session.world_id) {
                watchdog_warm_world = (unsigned int)session.world_id;
                toy_renderer_set_frame_budget(&renderer, 200);
            }
#endif
            fps_window_frames++;
            now = rf_core_time_us(&core);
            last_active = now - t_frame;
            rasterfall_perf_record_frame(&stats, &stats_total, last_active);
            if (options.gpu_rb0_stats && rendered_frames > RF_RB0_WARMUP_FRAMES &&
                rb0_stats.count < RF_RB0_SAMPLE_MAX) {
                struct rf_core_gpu_frame_stats gpu_audit;
                struct rasterfall_scene_stats scene_audit;
                struct rf_rb0_sample *sample = &rb0_stats.samples[rb0_stats.count++];
                int producer;
                memset(&gpu_audit, 0, sizeof(gpu_audit));
                memset(&scene_audit, 0, sizeof(scene_audit));
                rf_core_get_gpu_frame_stats(&core, &gpu_audit);
                rasterfall_render_scene_stats(&scene_audit);
                sample->frame = (unsigned long long)rendered_frames;
                sample->whole_us = (int)(now - audit_loop_start);
                sample->prepare_us = (int)audit_prepare_us;
                sample->render_us = (int)audit_render_us;
                sample->execute_us = (int)audit_present_us;
                sample->remainder_us = sample->whole_us - sample->prepare_us -
                    sample->render_us - sample->execute_us;
                sample->raster_us = (int)(gpu_audit.mixed_raster_segment_ms * 1000.0);
                if (gpu_audit.mixed_gpu_timing_frame) {
                    int timing_sample;
                    for (timing_sample = 0; timing_sample < rb0_stats.count;
                         ++timing_sample) {
                        struct rf_rb0_sample *gpu_sample =
                            &rb0_stats.samples[timing_sample];
                        if (gpu_sample->frame !=
                            gpu_audit.mixed_gpu_timing_frame) continue;
                        gpu_sample->gpu_raster_us = (int)(
                            gpu_audit.mixed_gpu_raster_ms * 1000.0);
                        gpu_sample->gpu_draw_us = (int)(
                            gpu_audit.mixed_gpu_draw_ms * 1000.0);
                        gpu_sample->gpu_bridge_us = (int)((
                            gpu_audit.mixed_gpu_bridge_import_ms +
                            gpu_audit.mixed_gpu_bridge_export_ms) * 1000.0);
                        gpu_sample->gpu_timing_frame =
                            gpu_audit.mixed_gpu_timing_frame;
                        gpu_sample->gpu_timing_valid = gpu_audit.mixed_gpu_timing_valid;
                        gpu_sample->gpu_requested = gpu_audit.mixed_gpu_requested;
                        gpu_sample->gpu_recorded = gpu_audit.mixed_gpu_recorded;
                        gpu_sample->gpu_dropped = gpu_audit.mixed_gpu_dropped;
                        break;
                    }
                }
                sample->cpu_producer_us = (int)(scene_audit.sky_floor_us + scene_audit.map_us +
                    scene_audit.static_props_us + scene_audit.model_gallery_us +
                    scene_audit.private_model_us + scene_audit.projectiles_us +
                    (rf_world_submission_audit.enemies_ms +
                     rf_world_submission_audit.ai_teammates_ms) * 1000.0);
                sample->slot_wait_us = (int)(gpu_audit.mixed_slot_wait_ms * 1000.0);
                sample->graphics_wait_us = (int)(gpu_audit.mixed_graphics_wait_ms * 1000.0);
                sample->wait_predecessor_frame = gpu_audit.mixed_wait_predecessor_frame;
                for (int k=0;k<RF_GPU_SUBMIT_KIND_COUNT;++k) {
                    sample->submits_by_kind[k]=gpu_audit.mixed_submits_by_kind[k];
                    sample->wait_us_by_kind[k]=(int)(gpu_audit.mixed_wait_ms_by_kind[k]*1000.0);
                }
                sample->acquire_us = (int)(gpu_audit.native_present_timing.acquire_ms * 1000.0);
                sample->present_us = (int)(gpu_audit.native_present_timing.present_ms * 1000.0);
                sample->presenter_completion_us =
                    gpu_audit.native_present_timing.audit_completion_source ==
                        RF_GPU_PRESENT_COMPLETION_IMAGE_REACQUIRED ?
                    sample->acquire_us : 0;
                sample->bridge_transfers = gpu_audit.mixed_bridge_transfers;
                sample->bridge_bytes = gpu_audit.mixed_bridge_bytes;
                for (producer = 0; producer < RF_CORE_PRODUCER_TOTAL; ++producer)
                    sample->producer_raster_commands[producer] =
                        gpu_audit.producer_raster_commands[producer];
            }
            /* Explicit audit must include fast frames too: sampled logs cannot
             * establish a complete strict run or an unbiased timing baseline. */
            if (options.frame_audit || options.gpu_scene_world_preview) {
                struct rf_gpu_scene_local_frame source_frame;
                struct rf_gpu_scene_local_source client_source;
                struct rf_gpu_scene_local_source *audit_source=&session.scene_local;
                /* Remote slot IDs are not local roster lifecycle identities.
                 * The procedural capture owns resolved client presentation. */
                if (net.mode==RASTERFALL_NET_CLIENT) {
                    client_source=session.scene_local;
                    client_source.active=0;client_source.actor_count=0;
                    memset(client_source.actors,0,sizeof(client_source.actors));
                    memset(&client_source.tracker,0,sizeof(client_source.tracker));
                    audit_source=&client_source;
                }
                struct rf_gpu_scene_frozen_v1 source_scene;
                static struct rf_gpu_scene_pose_v1 actor_pose[TOY_GAME_MAX_ACTORS];
                struct rf_gpu_scene_world_input_v2 world[RF_GPU_SCENE_MAX_WORLD_V2];
                struct rf_gpu_scene_world_render_frame_v1 world_render;
                struct rf_gpu_scene_world_floor_frame_v1 floor_render;
                struct rf_gpu_scene_world_prop_frame_v1 prop_render;
                struct rf_gpu_scene_flag_frame_v1 flag_render;
                struct rf_gpu_scene_projectile_frame_v1 projectile_render;
                struct rf_gpu_scene_interactable_frame_v1 interactable_render;
                static struct rf_gpu_scene_enemy_frame_v1 enemy_render;
                uint32_t world_count=0,pose_count=0;
                uint32_t map_primitives=0,map_resources=0;
                if ((session.map_ops.runtime_loaded &&
                        rf_gpu_scene_world_render_freeze(&session.map_ops,air_wall_enabled,
                            session.scene_local.frame_id+1,session.scene_local.world_generation,
                            world,RF_GPU_SCENE_MAX_WORLD_V2,&world_count,&world_render)<0) ||
                    (session.map_ops.runtime_loaded &&
                        rf_gpu_scene_world_floor_freeze(&session.map_ops,
                            rasterfall_world_uses_authored_ground(session.world_id),
                            session.scene_local.frame_id+1,session.scene_local.world_generation,
                            &floor_render)<0) ||
                    (session.map_ops.runtime_loaded &&
                        rf_gpu_scene_world_prop_freeze(&session.map_ops,
                            &game_runtime.render_context.world_lighting,
                            session.scene_local.frame_id+1,session.scene_local.world_generation,
                            &prop_render)<0) ||
                    rf_gpu_scene_enemy_freeze(&enemy_render)<0 ||
                    rf_gpu_scene_flag_freeze(&session,
                        session.scene_local.frame_id+1,session.scene_local.world_generation,
                        &flag_render)<0 ||
                    rf_gpu_scene_projectile_freeze(&session.game_state,
                        &game_runtime.render_context.world_lighting,
                        session.scene_local.frame_id+1,session.scene_local.world_generation,
                        &projectile_render)<0 ||
                    rf_gpu_scene_interactable_freeze(&session,&effects,
                        &game_runtime.render_context.world_lighting,
                        session.game_state.state==TOY_GAME_PLAYING &&
                            !game_runtime.lifecycle_paused && !session.shop_open,
                        session.scene_local.frame_id+1,session.scene_local.world_generation,
                        &interactable_render)<0 ||
                    rf_gpu_scene_local_freeze_world_lit(audit_source,&game,&game_runtime.render_camera,
                        renderer.surface.width,renderer.surface.height,air_wall_enabled,
                        world,world_count,&game_runtime.render_context.world_lighting,
                        &source_frame)<0 ||
                    rf_gpu_scene_extract_v1(&source_frame.snapshot,&source_scene)<0 ||
                    (session.map_ops.runtime_loaded &&
                        rf_gpu_scene_world_render_validate(&source_frame.snapshot,&world_render)<0)) {
                    __fprintf(2,"SCENE-LOCAL freeze failed\n");
#ifdef TOYC_WINDOWS
                    rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
#endif
                    return 1;
                }
                session.scene_local.frame_id=audit_source->frame_id;
                int64_t pose_extract_start=rf_core_clock_now_us();
                for(uint32_t actor_index=0;
                    actor_index<source_frame.snapshot.actor_count;++actor_index) {
                    if (source_frame.presentations[actor_index].state==TOY_GAME_ACTOR_DOWNED)
                        continue;
                    if (rf_gpu_scene_pose_extract_at(&source_frame,actor_index,
                            &actor_pose[pose_count])<0) {
                        __fprintf(2,"SCENE-LOCAL actor pose extraction failed\n");
#ifdef TOYC_WINDOWS
                        rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
#endif
                        return 1;
                    }
                    pose_count++;
                }
                for (unsigned i=0;i<enemy_render.modular_count;++i) {
                    if (pose_count>=TOY_GAME_MAX_ACTORS) return 1;
                    actor_pose[pose_count++]=enemy_render.modular[i];
                }
                int64_t pose_extract_us=rf_core_clock_now_us()-pose_extract_start;
                if (session.map_ops.runtime_loaded &&
                    rf_gpu_scene_world_resources_prepare(&scene_world_resources,
                        &source_frame.snapshot,&world_render,&floor_render,&prop_render,
                        rasterfall_render_world_light_generation())<0) {
                    __fprintf(2,"SCENE-LOCAL map resource preparation failed\n");
#ifdef TOYC_WINDOWS
                    rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
#endif
                    return 1;
                }
                if (!session.map_ops.runtime_loaded)
                    rf_gpu_scene_world_resources_invalidate(&scene_world_resources);
                for(unsigned int kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind)
                    if (scene_world_resources.opaque[kind].generation) {
                        const struct rasterfall_model_asset *model=
                            rasterfall_resources_resolve_active(&scene_world_resources.registry,
                                scene_world_resources.opaque[kind]);
                        if (!model) {
                            __fprintf(2,"SCENE-LOCAL stale map resource\n");
#ifdef TOYC_WINDOWS
                            rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
#endif
                            return 1;
                        }
                        map_primitives+=model->primitive_count;
                        map_resources++;
                    }
                __printf("SCENE-LOCAL frame=%llu world=%llu epoch=%llu generation=%u slot=%u items=%u world_items=%u map_payload=%u map_resources=%u map_loads=%u map_opaque=%u map_primitives=%u map_deferred=%u map_transparent=%u prop_payload=%u prop_opaque=%u interaction_payload=%u character=%d animation=%d time=%d weapon=%d y=%d scene_light=%d\n",
                    (unsigned long long)source_frame.snapshot.frame_id,
                    (unsigned long long)source_frame.snapshot.world_generation,
                    (unsigned long long)session.scene_local.epoch,
                    source_frame.snapshot.actors[0].identity.generation,source_frame.snapshot.actors[0].source_slot,source_scene.item_count,source_frame.snapshot.world_count,
                    session.map_ops.runtime_loaded ? world_render.count : 0,
                    map_resources,scene_world_resources.registry.loads,
                    scene_world_resources.accepted,map_primitives,
                    scene_world_resources.deferred,scene_world_resources.transparent,
                    session.map_ops.runtime_loaded ? prop_render.count : 0,
                    scene_world_resources.prop_accepted,
                    interactable_render.count,
                    source_frame.presentation.character_id,source_frame.snapshot.actors[0].animation_id,
                    source_frame.snapshot.actors[0].animation_time_ms,source_frame.snapshot.actors[0].weapon,
                    source_frame.snapshot.actors[0].y,
                    source_frame.presentation.scene_light_q8);
#ifdef TOYC_WINDOWS
                if (core.mixed_executor && session.map_ops.runtime_loaded) {
                    struct rf_gpu_scene_world_gpu_probe_stats probe_stats;
                    scene_world_probe.native_present=options.gpu_scene_world_preview;
                    if (rf_gpu_scene_world_gpu_probe_frame(&scene_world_probe,
                            &gpu_vulkan_context,&scene_world_resources,
                            &game_runtime.render_camera,(uint32_t)renderer.surface.width,
                            (uint32_t)renderer.surface.height,actor_pose,
                            pose_count,&flag_render,
                            &projectile_render,&interactable_render,&enemy_render,
                            &model_texture_view,&probe_stats,
                            options.gpu_frame_capture &&
                            rendered_frames==options.gpu_capture_frame ?
                                options.gpu_frame_capture : NULL)<0) {
                        __fprintf(2,"SCENE-WORLD-GPU normal audit failed\n");
                        rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
                        return 1;
                    }
                    if (options.gpu_scene_world_preview) {
                        scene_native_frames++;
                        if (options.gpu_scene_independent_preview) {
                            if (renderer.cmd_count || core.mixed_frame->raster_count ||
                                core.mixed_frame->draw_count) {
                                __fprintf(2,"SCENE-SOURCE unexpected legacy recording\n");
                                rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
                                return 1;
                            }
                            __printf("SCENE-SOURCE frame=%d independent=1 legacy_producer=0 raster_commands=0 mixed_draws=0 dynamic_sources_pending=1\n",
                                rendered_frames);
                        }
                        __printf("SCENE-NATIVE frame=%d world_only=1 draws=%u bridges=%llu readback=0 mixed_execute=0\n",
                            rendered_frames,probe_stats.draws,
                            (unsigned long long)probe_stats.bridge_transfers);
                    }
                    __printf("SCENE-PROCEDURAL frame=%llu items=%u draws=%u net_mode=%d\n",
                        (unsigned long long)enemy_render.frame_id,
                        probe_stats.procedural_items,probe_stats.procedural_draws,net.mode);
                    __printf("SCENE-EXTRACT frame=%llu local_pose_us=%lld geometry_us=%lld instances=%u\n",
                        (unsigned long long)enemy_render.frame_id,(long long)pose_extract_us,
                        (long long)probe_stats.geometry_extract_us,
                        pose_count+enemy_render.count+enemy_render.procedural_count);
                    __printf("SCENE-WORLD-COST frame=%llu prepare_us=%lld upload_bytes=%llu draws=%u gpu_valid=%d gpu_draw_ms=%.6f bridges=%llu supplemental_modular=%u\n",
                        (unsigned long long)enemy_render.frame_id,(long long)probe_stats.prepare_us,
                        (unsigned long long)probe_stats.upload_bytes,probe_stats.draws,
                        probe_stats.gpu_time_valid,probe_stats.gpu_draw_ms,
                        (unsigned long long)probe_stats.bridge_transfers,enemy_render.modular_count);
                    {
                        unsigned shadows=0,tongues=0,deaths=0;
                        for (unsigned i=0;i<enemy_render.count;++i) {
                            shadows+=enemy_render.items[i].shadow;
                            tongues+=enemy_render.items[i].tongue;
                            deaths+=enemy_render.items[i].transformed;
                        }
                        __printf("SCENE-ENEMY-EXTRAS frame=%llu shadows=%u tongues=%u deaths=%u\n",
                            (unsigned long long)enemy_render.frame_id,shadows,tongues,deaths);
                    }
                    __printf("SCENE-ENEMY frame=%llu items=%u draws=%u deferred=%u culled=%u transparent=%u\n",
                        (unsigned long long)enemy_render.frame_id,
                        probe_stats.enemy_items,probe_stats.enemy_draws,
                        probe_stats.enemy_deferred,probe_stats.enemy_culled,enemy_render.transparent);
                    __printf("SCENE-WORLD-GPU frame=%llu draws=%u actor_draws=%u flag_draws=%u flag_text_draws=%u projectile_draws=%u pickup_model_draws=%u pickup_model_items=%u pickup_procedural_draws=%u pickup_procedural_items=%u pickup_procedural_deferred=%u covered=%u uploads=%llu hits=%llu prop_assets=%u prop_draws=%u prop_culled=%u prop_deferred=%u prop_numeric=%u prop_material=%u prop_transparent=%u diagnostic_readback=%d\n",
                        (unsigned long long)source_frame.snapshot.frame_id,
                        probe_stats.draws,probe_stats.actor_draws,probe_stats.flag_draws,
                        probe_stats.flag_text_draws,probe_stats.projectile_draws,
                        probe_stats.pickup_model_draws,probe_stats.pickup_model_items,
                        probe_stats.pickup_procedural_draws,
                        probe_stats.pickup_procedural_items,
                        probe_stats.pickup_procedural_deferred,
                        probe_stats.covered_pixels,
                        (unsigned long long)probe_stats.uploads,
                        (unsigned long long)probe_stats.hits,
                        scene_world_resources.prop_asset_count,
                        probe_stats.prop_draws,probe_stats.prop_culled,probe_stats.prop_deferred,
                        probe_stats.prop_numeric_deferred,
                        probe_stats.prop_material_deferred,
                        probe_stats.prop_transparent_deferred,!options.gpu_scene_world_preview);
                }
#endif
            }
            if (options.frame_audit && !options.gpu_scene_world_preview) {
                struct rf_core_gpu_frame_stats gpu_audit;
                struct rf_render_frame_v1 frame_audit;
                char audit_line[768];
                memset(&gpu_audit, 0, sizeof(gpu_audit));
                memset(&frame_audit, 0, sizeof(frame_audit));
                rf_core_get_gpu_frame_stats(&core, &gpu_audit);
                rf_core_get_render_frame_v1(&core, &frame_audit);
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT frame=%llu path=%s world=%d camera=(%d,%d) direction=(%d,%d) pitch=(%d,%d) extent=%dx%d ticks=%d accumulator_us=%lld update_ms=%.3f render_ms=%.3f present_wall_ms=%.3f whole_loop_ms=%.3f frame_interval_ms=%.3f",
                    frame_audit.frame_id,
                    gpu_audit.last_path == 1 ? "gpu-native" :
                    gpu_audit.last_path == 2 ? "cpu-fallback" :
                    gpu_audit.last_path == 3 ? "gpu-readback" : "cpu",
                    session.world_id, frame_audit.camera_x,
                    frame_audit.camera_z, frame_audit.direction_sy,
                    frame_audit.direction_cy, frame_audit.pitch_sy,
                    frame_audit.pitch_cy, frame_audit.width, frame_audit.height,
                    logic_steps, (long long)accumulator,
                    (double)audit_update_us / 1000.0,
                    (double)audit_render_us / 1000.0,
                    (double)audit_present_us / 1000.0,
                    (double)(now - audit_loop_start) / 1000.0,
                    (double)audit_interval_us / 1000.0);
                __printf("%s\n", audit_line);
                rf_windows_log(audit_line);
                snprintf(audit_line,sizeof(audit_line),
                    "FRAME-AUDIT cpu-phases frame=%llu prepare_us=%lld render_us=%lld execute_us=%lld remainder_us=%lld whole_us=%lld",
                    (unsigned long long)rendered_frames,(long long)audit_prepare_us,
                    (long long)audit_render_us,(long long)audit_present_us,
                    (long long)(now-audit_loop_start-audit_prepare_us-audit_render_us-audit_present_us),
                    (long long)(now-audit_loop_start));
                __printf("%s\n",audit_line); rf_windows_log(audit_line);
                if (gpu_audit.character_diff_frames) {
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT character-vertex-diff vertices=%llu position_mismatches=%llu normal_mismatches=%llu uv_mismatches=%llu max_position_delta=%u max_normal_delta=%u",
                        gpu_audit.character_diff_vertices,
                        gpu_audit.character_position_mismatches,
                        gpu_audit.character_normal_mismatches,
                        gpu_audit.character_uv_mismatches,
                        gpu_audit.character_max_position_delta,
                        gpu_audit.character_max_normal_delta);
                    __printf("%s\n",audit_line);
                    rf_windows_log(audit_line);
                }
                if (gpu_audit.character_skin_frames) {
                    snprintf(audit_line,sizeof(audit_line),
                        "FRAME-AUDIT character-gpu-skin frames=%llu vertices=%llu",
                        gpu_audit.character_skin_frames,gpu_audit.character_skin_vertices);
                    __printf("%s\n",audit_line);
                    rf_windows_log(audit_line);
                }
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT mixed raster_spans=%llu draw_spans=%llu draws=%llu bridge_transfers=%llu bridge_bytes=%llu graphics_submits=%llu graphics_waits=%llu gpu_upload_bytes=%llu graphics_submit_ms=%.3f graphics_wait_ms=%.3f bridge_ms=%.3f gpu_capture_readback_bytes=%llu",
                    gpu_audit.mixed_raster_spans,gpu_audit.mixed_draw_spans,
                    gpu_audit.mixed_draws,gpu_audit.mixed_bridge_transfers,
                    gpu_audit.mixed_bridge_bytes,gpu_audit.mixed_graphics_submits,
                    gpu_audit.mixed_graphics_waits,gpu_audit.mixed_gpu_upload_bytes,
                    gpu_audit.mixed_graphics_submit_ms,
                    gpu_audit.mixed_graphics_wait_ms,gpu_audit.mixed_bridge_ms,
                    gpu_audit.capture_readback_bytes);
                __printf("%s\n",audit_line);
                rf_windows_log(audit_line);
                for (unsigned int producer = 0;
                     producer < RF_CORE_PRODUCER_COUNT; ++producer) {
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT producer name=%s raster_cmd=%llu spans=%llu opaque_cmd=%llu transparent_cmd=%llu",
                        rf_core_mixed_producer_name(producer),
                        gpu_audit.producer_raster_commands[producer],
                        gpu_audit.producer_raster_spans[producer],
                        gpu_audit.producer_opaque_commands[producer],
                        gpu_audit.producer_transparent_commands[producer]);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
                for (unsigned int event = 0;
                     event < gpu_audit.bridge_event_count; ++event) {
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT bridge direction=%s color_bytes=%llu depth_bytes=%llu layer=%u previous=%s next=%s target_generation=%llu",
                        gpu_audit.bridge_events[event].direction ? "export" : "import",
                        gpu_audit.bridge_events[event].color_bytes,
                        gpu_audit.bridge_events[event].depth_bytes,
                        gpu_audit.bridge_events[event].layer,
                        rf_core_mixed_producer_name(
                            gpu_audit.bridge_events[event].previous_producer),
                        rf_core_mixed_producer_name(
                            gpu_audit.bridge_events[event].next_producer),
                        gpu_audit.bridge_events[event].target_generation);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT mixed-cpu freeze_ms=%.3f cache_collect_ms=%.3f preflight_ms=%.3f dynamic_release_ms=%.3f target_setup_ms=%.3f dynamic_pack_ms=%.3f dynamic_resource_ms=%.3f plan_build_ms=%.3f raster_preflight_ms=%.3f texture_measure_ms=%.3f pack_ms=%.3f draw_encode_ms=%.3f draw_batch_prepare_ms=%.3f graphics_draw_ms=%.3f raster_segment_ms=%.3f",
                    gpu_audit.mixed_freeze_ms,gpu_audit.mixed_cache_collect_ms,
                    gpu_audit.mixed_preflight_ms,gpu_audit.mixed_dynamic_release_ms,
                    gpu_audit.mixed_target_setup_ms,gpu_audit.mixed_dynamic_pack_ms,
                    gpu_audit.mixed_dynamic_resource_ms,gpu_audit.mixed_plan_build_ms,
                    gpu_audit.mixed_raster_preflight_ms,gpu_audit.mixed_texture_measure_ms,
                    gpu_audit.mixed_pack_ms,gpu_audit.mixed_draw_encode_ms,
                    gpu_audit.mixed_draw_batch_prepare_ms,
                    gpu_audit.mixed_graphics_draw_ms,
                    gpu_audit.mixed_raster_segment_ms);
                __printf("%s\n",audit_line);
                rf_windows_log(audit_line);
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT mixed-gpu frame=%llu supported=%u valid=%u raster_ms=%.3f bridge_import_ms=%.3f draw_ms=%.3f bridge_export_ms=%.3f post_ms=%.3f overlay_ms=%.3f present_copy_ms=%.3f requested=%u recorded=%u dropped=%u",
                    gpu_audit.mixed_gpu_timing_frame,
                    gpu_audit.mixed_gpu_timing_supported,gpu_audit.mixed_gpu_timing_valid,
                    gpu_audit.mixed_gpu_raster_ms,gpu_audit.mixed_gpu_bridge_import_ms,
                    gpu_audit.mixed_gpu_draw_ms,gpu_audit.mixed_gpu_bridge_export_ms,
                    gpu_audit.mixed_gpu_post_ms,gpu_audit.mixed_gpu_overlay_ms,
                    gpu_audit.mixed_gpu_present_copy_ms,
                    gpu_audit.mixed_gpu_requested,gpu_audit.mixed_gpu_recorded,
                    gpu_audit.mixed_gpu_dropped);
                __printf("%s\n",audit_line);
                rf_windows_log(audit_line);
                for (unsigned int k=0;k<RF_GPU_SUBMIT_KIND_COUNT;++k) {
                    static const char *names[]={"upload","vertex-diff","skin-input",
                        "skinning","bridge","draw","readback"};
                    snprintf(audit_line,sizeof(audit_line),
                        "FRAME-AUDIT graphics-submit frame=%llu caller=%s submits=%llu wait_ms=%.3f predecessor_frame=%llu",
                        gpu_audit.mixed_wait_frame,names[k],
                        gpu_audit.mixed_submits_by_kind[k],gpu_audit.mixed_wait_ms_by_kind[k],
                        gpu_audit.mixed_wait_predecessor_frame);
                    __printf("%s\n",audit_line); rf_windows_log(audit_line);
                }
                {
                    struct rasterfall_scene_stats scene_audit;
                    rasterfall_render_scene_stats(&scene_audit);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT scene floor_cmd=%lu map_cmd=%lu static_cmd=%lu gallery_cmd=%lu character_cmd=%lu private_cmd=%lu projectile_cmd=%lu sky_floor_ms=%.3f map_ms=%.3f static_ms=%.3f gallery_ms=%.3f character_private_ms=%.3f projectiles_ms=%.3f models_tested=%lu models_culled=%lu triangles_culled=%lu",
                        scene_audit.floor_command_end,
                        scene_audit.map_command_end - scene_audit.floor_command_end,
                        scene_audit.static_command_end - scene_audit.map_command_end,
                        scene_audit.gallery_command_end - scene_audit.static_command_end,
                        scene_audit.character_command_end - scene_audit.gallery_command_end,
                        scene_audit.private_command_end - scene_audit.character_command_end,
                        scene_audit.projectile_command_end - scene_audit.private_command_end,
                        (double)scene_audit.sky_floor_us / 1000.0,
                        (double)scene_audit.map_us / 1000.0,
                        (double)scene_audit.static_props_us / 1000.0,
                        (double)scene_audit.model_gallery_us / 1000.0,
                        (double)scene_audit.private_model_us / 1000.0,
                        (double)scene_audit.projectiles_us / 1000.0,
                        scene_audit.models_tested, scene_audit.models_culled,
                        scene_audit.model_triangles_culled);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT character-draw instances=%lu items=%lu triangles=%lu reference_vertices=%lu output_vertices=%lu legacy_items=%lu skin_instances=%lu bind_vertices=%lu palette_bones=%lu",
                        scene_audit.character_draw_instances,
                        scene_audit.character_draw_items,
                        scene_audit.character_draw_triangles,
                        scene_audit.character_draw_upload_vertices,
                        scene_audit.character_skin_bind_vertices,
                        scene_audit.character_draw_legacy_items,
                        scene_audit.character_skin_instances,
                        scene_audit.character_skin_bind_vertices,
                        scene_audit.character_skin_palette_bones);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT draw-reference instances=%lu items=%lu draw_triangles=%lu cpu_lowered_triangles=%lu legacy_instances=%lu legacy_triangles=%lu draw_asset_mask=%llx legacy_asset_mask=%llx flush_ms=%.3f submit_ms=%.3f reject_scope=%lu reject_deformation=%lu reject_material=%lu reject_transparent=%lu reject_range=%lu reject_numeric=%lu",
                        scene_audit.static_draw_instances, scene_audit.static_draw_items,
                        scene_audit.static_draw_triangles,
                        scene_audit.static_draw_lowered_triangles,
                        scene_audit.static_draw_legacy_instances,
                        scene_audit.static_draw_legacy_triangles,
                        scene_audit.static_draw_asset_mask,
                        scene_audit.static_draw_legacy_asset_mask,
                        scene_audit.static_draw_flush_us / 1000.0,
                        scene_audit.static_draw_submit_us / 1000.0,
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_SCOPE],
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_DEFORMATION],
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_MATERIAL],
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_TRANSPARENT],
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_RANGE],
                        scene_audit.static_draw_rejected[RASTERFALL_DRAW_NUMERIC]);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT ground-draw items=%lu triangles=%lu legacy_commands=%lu mesh_builds=%lu",
                        scene_audit.ground_draw_items,
                        scene_audit.ground_draw_triangles,
                        scene_audit.ground_legacy_commands,
                        scene_audit.ground_mesh_builds);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT map-draw wall=%lu/%lu/%lu box=%lu/%lu/%lu ramp=%lu/%lu/%lu platform=%lu/%lu/%lu boundary=%lu/%lu/%lu",
                        scene_audit.map_draw_items[0], scene_audit.map_draw_triangles[0], scene_audit.map_mesh_builds[0],
                        scene_audit.map_draw_items[1], scene_audit.map_draw_triangles[1], scene_audit.map_mesh_builds[1],
                        scene_audit.map_draw_items[2], scene_audit.map_draw_triangles[2], scene_audit.map_mesh_builds[2],
                        scene_audit.map_draw_items[3], scene_audit.map_draw_triangles[3], scene_audit.map_mesh_builds[3],
                        scene_audit.boundary_draw_items, scene_audit.boundary_draw_triangles,
                        scene_audit.boundary_mesh_builds);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
                {
                    struct rasterfall_resource_stats resources;
                    rasterfall_resources_stats(rasterfall_render_resources(), &resources);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT draw-resources live=%u retired=%u pinned=%u failed=%u loads=%u releases=%u",
                        resources.live, resources.retired, resources.pinned,
                        resources.failed, resources.loads, resources.releases);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT world-submission enemies_cmd=%lu ai_teammates_cmd=%lu managed_player_cmd=%lu network_teammates_cmd=%lu text_cmd=%lu interactables_cmd=%lu enemies_ms=%.3f ai_teammates_ms=%.3f managed_player_ms=%.3f network_teammates_ms=%.3f text_ms=%.3f interactables_ms=%.3f",
                    rf_world_submission_audit.enemies,
                    rf_world_submission_audit.ai_teammates,
                    rf_world_submission_audit.managed_player,
                    rf_world_submission_audit.network_teammates,
                    rf_world_submission_audit.text,
                    rf_world_submission_audit.interaction_commands,
                    rf_world_submission_audit.enemies_ms,
                    rf_world_submission_audit.ai_teammates_ms,
                    rf_world_submission_audit.managed_player_ms,
                    rf_world_submission_audit.network_teammates_ms,
                    rf_world_submission_audit.text_ms,
                    rf_world_submission_audit.interaction_ms);
                __printf("%s\n", audit_line);
                rf_windows_log(audit_line);
                {
                    struct rasterfall_ai_submission_stats ai_audit;
                    rasterfall_render_ai_submission_stats(&ai_audit);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT ai-detail modular=%u procedural=%u body_cmd=%lu gear_cmd=%lu weapon_cmd=%lu procedural_cmd=%lu pose_ms=%.3f body_ms=%.3f gear_ms=%.3f weapon_ms=%.3f procedural_ms=%.3f",
                        ai_audit.modular_actors, ai_audit.procedural_actors,
                        ai_audit.body_commands, ai_audit.gear_commands,
                        ai_audit.weapon_commands, ai_audit.procedural_commands,
                        (double)ai_audit.pose_us / 1000.0,
                        (double)ai_audit.body_us / 1000.0,
                        (double)ai_audit.gear_us / 1000.0,
                        (double)ai_audit.weapon_us / 1000.0,
                        (double)ai_audit.procedural_us / 1000.0);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                    snprintf(audit_line, sizeof(audit_line),
                        "FRAME-AUDIT ai-triage active=%u depth=%u screen_culled=%u combined_bounds_culled=%u zero_cmd=%u offscreen_actors=%u offscreen_cmd=%lu pose_cache_hit=%u pose_cache_miss=%u bounds_cache_hit=%u bounds_cache_miss=%u gear_xform_hit=%u gear_xform_miss=%u body_source=%lu weapon_source=%lu body_skin_ms=%.3f body_cache_ms=%.3f body_triangle_ms=%.3f weapon_setup_ms=%.3f weapon_triangle_ms=%.3f",
                        ai_audit.active_actors, ai_audit.depth_actors,
                        ai_audit.screen_culled_actors,
                        ai_audit.combined_bounds_culled,
                        ai_audit.zero_command_actors,
                        ai_audit.offscreen_actors,
                        ai_audit.offscreen_commands,
                        ai_audit.pose_cache_hits,
                        ai_audit.pose_cache_misses,
                        ai_audit.bounds_cache_hits,
                        ai_audit.bounds_cache_misses,
                        ai_audit.gear_transform_cache_hits,
                        ai_audit.gear_transform_cache_misses,
                        ai_audit.body_source_triangles,
                        ai_audit.weapon_source_triangles,
                        (double)ai_audit.body_skin_us / 1000.0,
                        (double)ai_audit.body_vertex_cache_us / 1000.0,
                        (double)ai_audit.body_triangle_us / 1000.0,
                        (double)ai_audit.weapon_setup_us / 1000.0,
                        (double)ai_audit.weapon_triangle_us / 1000.0);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT layers sky=%lu world=%lu transparent=%lu effects=%lu/%lu/direct=%lu viewmodel=%lu/%lu/direct=%lu overlay_pixels=%lu cursor=%u invalid_transitions=%u retained_pre_post=%lu pre_post_cpu_fallback=%u fallback_reason=0x%x classification texture=%lu overlay=%lu edge=%lu other=%lu",
                    frame_audit.command_count[RF_RENDER_LAYER_SKY],
                    frame_audit.command_count[RF_RENDER_LAYER_WORLD],
                    frame_audit.command_count[RF_RENDER_LAYER_TRANSPARENT],
                    frame_audit.command_count[RF_RENDER_LAYER_EFFECTS],
                    frame_audit.pixel_count[RF_RENDER_LAYER_EFFECTS],
                    frame_audit.direct_pixel_count[RF_RENDER_LAYER_EFFECTS],
                    frame_audit.command_count[RF_RENDER_LAYER_VIEWMODEL],
                    frame_audit.pixel_count[RF_RENDER_LAYER_VIEWMODEL],
                    frame_audit.direct_pixel_count[RF_RENDER_LAYER_VIEWMODEL],
                    frame_audit.pixel_count[RF_RENDER_LAYER_OVERLAY],
                    frame_audit.current_layer,
                    frame_audit.invalid_layer_transitions,
                    frame_audit.retained_pre_post_commands,
                    frame_audit.pre_post_cpu_fallback,
                    frame_audit.pre_post_fallback_reason,
                    gpu_audit.last_texture_commands,
                    gpu_audit.last_overlay_commands,
                    gpu_audit.last_edge_commands,
                    gpu_audit.last_other_commands);
                __printf("%s\n", audit_line);
                rf_windows_log(audit_line);
                snprintf(audit_line, sizeof(audit_line),
                    "FRAME-AUDIT gpu frontend_ms=%.3f classification_ms=%.3f texture_measure_ms=%.3f pack_ms=%.3f binning_ms=%.3f tile_upload_ms=%.3f command_upload_ms=%.3f texture_upload_ms=%.3f submit_ms=%.3f fence_wait_ms=%.3f native_acquire_ms=%.3f native_submit_ms=%.3f native_present_ms=%.3f native_present_queue_idle_ms=%.3f native_total_ms=%.3f overlay_upload_bytes=%u readback_bytes=%u cpu_framebuffer_copy_bytes=%u mixed_draws=%llu",
                    gpu_audit.frontend_ms, gpu_audit.classification_ms,
                    gpu_audit.texture_measure_ms, gpu_audit.raster_abi_pack_ms,
                    gpu_audit.last_timing.cpu_binning_ms,
                    gpu_audit.last_timing.tile_upload_ms,
                    gpu_audit.last_timing.command_upload_ms,
                    gpu_audit.last_timing.texture_upload_ms,
                    gpu_audit.last_timing.submit_ms,
                    gpu_audit.last_timing.execution_wait_ms,
                    gpu_audit.native_present_timing.acquire_ms,
                    gpu_audit.native_present_timing.submit_ms,
                    gpu_audit.native_present_timing.present_ms,
                    gpu_audit.native_present_timing.present_queue_idle_ms,
                    gpu_audit.native_present_timing.total_ms,
                    gpu_audit.native_present_timing.overlay_upload_bytes,
                    gpu_audit.native_present_timing.color_readback_bytes,
                    gpu_audit.native_present_timing.cpu_framebuffer_copy_bytes,
                    gpu_audit.mixed_draws);
                __printf("%s\n", audit_line);
                rf_windows_log(audit_line);
                if (gpu_audit.native_present_timing.audit_swapchain_generation) {
                    const struct rf_gpu_native_present_timing *pa =
                        &gpu_audit.native_present_timing;
                    snprintf(audit_line, sizeof(audit_line),
                        "PRESENT-AUDIT frame=%llu slot=%u swapchain_gen=%llu image=%u slot_gen=%llu image_gen=%llu slot_fence=%u acquire_sem=%u image_state=%u render_finished_sem=%u acquire_gen=%llu submit_gen=%llu present_gen=%llu retire_gen=%llu image_owner_slot=%u outstanding_presents=%u hot_queue_idle_count=%llu recreate_queue_idle_count=%llu presenter_poisoned=%u completion_source=%u",
                        pa->audit_frame, pa->audit_slot,
                        pa->audit_swapchain_generation, pa->audit_image,
                        pa->audit_slot_generation, pa->audit_image_generation,
                        pa->audit_slot_fence_state,
                        pa->audit_acquire_semaphore_state,
                        pa->audit_image_state,
                        pa->audit_render_finished_semaphore_state,
                        pa->audit_acquire_generation,
                        pa->audit_submit_generation,
                        pa->audit_present_generation,
                        pa->audit_retire_generation,
                        pa->audit_image_owner_slot,
                        pa->audit_outstanding_presents,
                        pa->audit_hot_queue_idle_count,
                        pa->audit_recreate_queue_idle_count,
                        pa->audit_presenter_poisoned,
                        pa->audit_completion_source);
                    __printf("%s\n", audit_line);
                    rf_windows_log(audit_line);
                }
            }
            fps_elapsed = now - fps_window_start;
            if (fps_elapsed >= 1000000) {
                display_fps = (int)((long long)fps_window_frames * 1000000 /
                                    fps_elapsed);
                fps_window_frames = 0;
                fps_window_start = now;
            }
            if (stats_enabled &&
                now - stats.window_start >= RASTERFALL_STATS_WINDOW_US) {
                rasterfall_perf_dump(&stats, "window");
                rasterfall_perf_init(&stats);
            }
            if (frame_limit > 0 && rendered_frames >= frame_limit) running = 0;
        }
    }
    if (stats_enabled && stats_total.frames > 0)
        rasterfall_perf_dump(&stats_total, "total");
    if (options.gpu_rb0_stats) rf_rb0_dump(&rb0_stats);
    rasterfall_audio_stop(&audio);
    rasterfall_audio_unload_assets(&audio);
    rasterfall_net_discovery_close(&discovery);
    if (model_texture.blob) toy_texture_unload(&model_texture);
    if (dump_path) rasterfall_hud_dump_frame(dump_path, &surface);
    rasterfall_net_close(&net);
#ifdef TOYC_WINDOWS
    rf_gpu_scene_world_gpu_probe_close(&scene_world_probe);
#endif
    rf_gpu_scene_world_resources_invalidate(&scene_world_resources);
    rf_game_shutdown(&game_runtime);
    __printf("rasterfall: %d frames, %d scene pixels, position=(%d,%d)\n",
             rendered_frames, scene_pixels, camera.x, camera.z);
    if (texture_stats)
        __printf("rasterfall: texture stats triangles=%lu pixels=%lu fallback=%lu\n",
                 renderer.textured_triangles, renderer.textured_pixels,
                 renderer.texture_fallback_pixels);
    {
        struct rf_core_gpu_frame_stats gpu_stats;
        if (rf_core_get_gpu_frame_stats(&core, &gpu_stats) == 0 &&
            gpu_stats.frames_attempted) {
            char gpu_log[320];
            snprintf(gpu_log, sizeof(gpu_log),
                     "gpu-frame attempted=%llu rendered=%llu fallback=%llu texture=%llu transparent=%llu exec=%.3fms readback=%.3fms total=%.3fms",
                     gpu_stats.frames_attempted, gpu_stats.gpu_frames,
                     gpu_stats.cpu_fallback_frames,
                     gpu_stats.unsupported_texture,
                     gpu_stats.unsupported_transparent,
                     gpu_stats.last_timing.execution_wait_ms,
                     gpu_stats.last_timing.readback_ms,
                     gpu_stats.last_timing.total_ms);
            rf_windows_log(gpu_log);
            if (core.gpu_frame.native_present) {
                snprintf(gpu_log, sizeof(gpu_log),
                    "gpu-native overlay-composite=ready acquire=%.3fms raster-wait=%.3fms post=%.3fms overlay-draw=%.3fms overlay-upload=%.3fms overlay-composite=%.3fms copy-record=%.3fms submit=%.3fms present=%.3fms present-queue-idle=%.3fms total=%.3fms overlay-bytes=%u readback=%u cpu-copy=%u format=%u mode=%u images=%u extent=%ux%u",
                    gpu_stats.native_present_timing.acquire_ms,
                    gpu_stats.native_present_timing.gpu_raster_ms,
                    gpu_stats.native_present_timing.post_raster_ms,
                    gpu_stats.overlay_cpu_draw_ms,
                    gpu_stats.native_present_timing.overlay_upload_ms,
                    gpu_stats.native_present_timing.overlay_composite_ms,
                    gpu_stats.native_present_timing.buffer_to_swapchain_ms,
                    gpu_stats.native_present_timing.submit_ms,
                    gpu_stats.native_present_timing.present_ms,
                    gpu_stats.native_present_timing.present_queue_idle_ms,
                    gpu_stats.native_present_timing.total_ms,
                    gpu_stats.native_present_timing.overlay_upload_bytes,
                    gpu_stats.native_present_timing.color_readback_bytes,
                    gpu_stats.native_present_timing.cpu_framebuffer_copy_bytes,
                    gpu_stats.native_present_timing.format,
                    gpu_stats.native_present_timing.present_mode,
                    gpu_stats.native_present_timing.image_count,
                    gpu_stats.native_present_timing.width,
                    gpu_stats.native_present_timing.height);
                rf_windows_log(gpu_log);
            }
        }
    }
    {
        int core_runtime_failed = rf_core_runtime_failed(&core);
        int capture_missing = options.gpu_frame_capture &&
            !core.gpu_frame.capture_completed;
        int character_diff_missing = options.gpu_character_vertex_diff &&
            !core.gpu_frame.character_vertex_diff_completed;
        /* Native GPU frames never write the CPU scene pixel counter. */
        int no_scene_output = rendered_frames > 0 && scene_pixels == 0 &&
            core.gpu_frame.stats.gpu_frames == 0 && scene_native_frames == 0;
        rf_core_shutdown(&core);
        if (core_runtime_failed || capture_missing || character_diff_missing) return 3;
        if (options.gpu_scene_world_preview && scene_native_frames != rendered_frames) return 3;
        return no_scene_output ? 2 : 0;
    }
}
