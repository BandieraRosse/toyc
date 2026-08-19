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
 *   --no-stats                     关闭性能统计
 *   --texture-stats                显示纹理统计
 *   --dump-frame <path>            导出帧图像
 *   --model-views <model> <dir>     离屏导出模型正面/侧面/背面图
 *   --model-views-compare <model> <dir>
 *                                   同时导出启用/禁用 sphere 的三视图
 *   --model-views-toon-compare <model> <dir>
 *                                   同时导出启用/禁用 toon 的三视图
 *   --frames <count>               运行指定帧数后退出
 *   --input-test                   输入调试测试
 *   --logic-test / --net-test      运行逻辑测试
 */

#include "core.h"
#include "string.h"
#include "tlibc_everything.h"
#include "toy_window.h"
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
#include "math.h"

#define KEY_ESC   1
#define KEY_1     2
#define KEY_2     3
#define KEY_3     4
#define KEY_4     5
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
#define KEY_UP    103
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_DOWN  108
#define BTN_LEFT 0x110
#define BTN_RIGHT 0x111

#define FIXED_STEP_US 16667
#define MAX_FRAME_US 250000
#define MAX_LOGIC_STEPS 4
#define NEAR_Z 192
#define ENEMY_RENDER_DISTANCE 24000 /* 3x the old 8000-unit enemy cutoff */
#define UV_ONE 65536
#define BAKED_LM_W 32
#define BAKED_LM_H 24

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
#define PAUSE_ITEM_MENU     4
#define PAUSE_ITEM_COUNT    5

enum rasterfall_startup_screen {
    RASTERFALL_STARTUP_MAIN,
    RASTERFALL_STARTUP_MANUAL_IP,
    RASTERFALL_STARTUP_PUBLIC_ROOM,
    RASTERFALL_STARTUP_LAN_ROOMS
};

static struct rasterfall_session session;
static struct rasterfall_render_context render_context;
#define level_map (session.level)
#define map_bounds (session.bounds)
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
static struct toy_texture_asset scene_texture;
static struct toy_texture_view scene_texture_view;
static struct toy_texture_view wall_texture_view;
static struct toy_texture_asset model_texture;
static struct toy_texture_view model_texture_view;
/* Toyc 自托管版的纹理光栅化仍有运行时崩溃风险；纯色路径作为稳定默认值。
 * 调试纹理渲染时可显式传入 --textures。 */
static int textures_enabled = 0;
static struct rasterfall_effects effects;

/* 主循环仍需要少量坐标和特效同步辅助；几何绘制本身位于
 * rasterfall_render.c。 */
static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                 y * surface->stride);
    row[x] = color;
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
    long numerator = 192 - a->z;
    long denominator = b->z - a->z;
    out->x = a->x + (int)(((long)b->x - (long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long)b->y - (long)a->y) *
                          numerator / denominator);
    out->z = 192;
}

static int clip_near(const struct vec3 *input, int count,
                     struct vec3 *output)
{
    int out_count = 0;
    struct vec3 previous;
    copy_vec3(&previous, &input[count - 1]);
    int previous_inside = previous.z >= 192;
    for (int i = 0; i < count; i++) {
        struct vec3 current;
        copy_vec3(&current, &input[i]);
        int current_inside = current.z >= 192;
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
                long dx, dz, d2;
                if (!client->active || !client->connected || !client->down)
                    continue;
                dx = (long)camera->x - client->camera.x;
                dz = (long)camera->z - client->camera.z;
                d2 = dx * dx + dz * dz;
                if (d2 > (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE ||
                    (player_target >= 0 && d2 >= player_target_d2)) continue;
                player_target = client->client_id;
                player_target_d2 = d2;
                player_progress = client->revive_progress_ms;
                player_active = (net_state->host_revive_active &&
                                 net_state->host_revive_target_id ==
                                     client->client_id) ||
                                player_progress > 0;
            }
        } else {
            for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
                const struct rasterfall_net_player *player =
                    &net_state->players[i];
                long dx, dz, d2;
                if (i == net_state->local_player_id || !player->active ||
                    !player->downed) continue;
                dx = (long)camera->x - player->camera.x;
                dz = (long)camera->z - player->camera.z;
                d2 = dx * dx + dz * dz;
                if (d2 > (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE ||
                    (player_target >= 0 && d2 >= player_target_d2)) continue;
                player_target = i;
                player_target_d2 = d2;
                player_progress = player->revive_progress_ms;
                player_active = player_progress > 0;
            }
        }
    }
    hud->game = &session.game_state;
    hud->map = &level_map;
    hud->safe_rooms = map_safe_rooms;
    hud->player_name = session.game_state.player_name;
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

static int64_t monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return (int64_t)now.tv_sec * 1000000 + now.tv_nsec / 1000;
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
    const struct rasterfall_net_player *target = NULL;
    int i;
    int distance = 1250;
    if (!camera || !net) return;
    if (net->mode == RASTERFALL_NET_CLIENT &&
        net->local_player_id >= 0 &&
        net->local_player_id < RASTERFALL_NET_PLAYER_MAX &&
        net->players[net->local_player_id].downed) {
        for (i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
            if (i != net->local_player_id && net->players[i].active &&
                !net->players[i].downed) {
                target = &net->players[i];
                break;
            }
        }
        if (!target) return;
    } else if (net->mode == RASTERFALL_NET_HOST && game.player_down) {
        for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++)
            if (net->clients[i].active && net->clients[i].connected &&
                !net->clients[i].down) {
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
    camera->x = target->camera.x - target->camera.sy * distance / 1024;
    camera->z = target->camera.z - target->camera.cy * distance / 1024;
    camera->sy = target->camera.sy;
    camera->cy = target->camera.cy;
    camera->pitch_sy = target->camera.pitch_sy;
    camera->pitch_cy = target->camera.pitch_cy;
    camera->y = target->camera.y;
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
                               const struct toy_input *input,
                               const struct control_settings *settings,
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
    command->fire_held = toy_input_down(input, KEY_SPACE) ||
                         (input->mouse_buttons & 1) != 0;
    if (fire_edge) command->buttons |= RASTERFALL_CMD_FIRE;
    if (shove_edge) command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_LEFTSHIFT))
        command->buttons |= RASTERFALL_CMD_JUMP;
    if (toy_input_pressed(input, KEY_SLASH))
        command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_R)) command->buttons |= RASTERFALL_CMD_RELOAD;
    if (toy_input_pressed(input, KEY_1)) command->buttons |= RASTERFALL_CMD_SLOT_1;
    if (toy_input_pressed(input, KEY_2)) command->buttons |= RASTERFALL_CMD_SLOT_2;
    if (toy_input_pressed(input, KEY_3)) command->buttons |= RASTERFALL_CMD_SLOT_3;
    if (toy_input_pressed(input, KEY_4)) command->buttons |= RASTERFALL_CMD_SLOT_4;
    if (toy_input_pressed(input, KEY_E)) command->buttons |= RASTERFALL_CMD_INTERACT;
    if (toy_input_pressed(input, KEY_F)) command->buttons |= RASTERFALL_CMD_FLAG;
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

static void consume_game_command_edges(struct toy_input *input)
{
    input->key_pressed[KEY_R] = 0;
    input->key_pressed[KEY_1] = 0;
    input->key_pressed[KEY_2] = 0;
    input->key_pressed[KEY_3] = 0;
    input->key_pressed[KEY_4] = 0;
    input->key_pressed[KEY_E] = 0;
    input->key_pressed[KEY_F] = 0;
    input->key_pressed[KEY_SLASH] = 0;
    input->key_pressed[KEY_LEFTSHIFT] = 0;
}

static void draw_crosshair(struct toy_surface *surface,
                           const struct toy_game *game_state)
{
    int cx = surface->width / 2, cy = surface->height / 2;
    int gap = toy_game_current_spread(game_state) * 9 / 20 + 2;
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
            if (!net->players[i].active) continue;
            snprintf(name, sizeof(name), "PLAYER %d%s", i + 1,
                     i == net->local_player_id ? " *" : "");
            scoreboard_add(players, &player_count, name,
                           net->players[i].kills,
                           net->players[i].special_kills,
                           net->players[i].damage_dealt,
                           net->players[i].throwable_damage_dealt);
        }
    } else {
        scoreboard_add(players, &player_count, "PLAYER 1 *",
                       game.kills, game.special_kills, game.damage_dealt,
                       game.throwable_damage_dealt);
        for (i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
            if (!net->clients[i].active || !net->clients[i].connected) continue;
            snprintf(name, sizeof(name), "PLAYER %d", net->clients[i].client_id + 1);
            scoreboard_add(players, &player_count, name,
                           net->clients[i].kills,
                           net->clients[i].special_kills,
                           net->clients[i].damage_dealt,
                           net->clients[i].throwable_damage_dealt);
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
            snprintf(line, sizeof(line), "%c RETURN TO MENU",
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

static int managed_terminal_take_key(struct toy_input *input,
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
                 session->game_state.player_airborne_ms,
                 session->managed_ai_escape_phase);
    else if (!strcmp(terminal->line, "unstuck") ||
             !strcmp(terminal->line, "return")) {
        if (rasterfall_session_recover_managed_player(session, camera))
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
                                   struct toy_input *input,
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
    char line[96];
    fill_rect(surface, 0, 0, surface->width, surface->height, 0x10151D);
    fb_draw_string((unsigned char *)surface->pixels, 238, 35,
                   "RASTERFALL", RF_COLOR_UI_ACCENT, surface->stride);
    if (screen == RASTERFALL_STARTUP_MAIN) {
        static const char *items[] = {"CREATE LOCAL ROOM", "WATCH MANAGED AI",
                                      "CREATE PUBLIC ROOM", "JOIN PUBLIC ROOM",
                                      "JOIN LAN ROOM", "JOIN BY ADDRESS", "QUIT"};
        for (i = 0; i < 7; i++) {
            uint32_t color = i == selected ? RF_COLOR_UI_ACCENT : RF_COLOR_UI_TEXT;
            if (i == selected)
                fill_rect(surface, 180, y + i * 34 - 4, 440, FB_FONT_H + 8,
                          0x293746);
            fb_draw_string((unsigned char *)surface->pixels, 205, y + i * 34,
                           items[i], color, surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels, 150, 325,
                       "UP DOWN SELECT   ENTER CONFIRM", RF_COLOR_UI_TEXT_MUTED,
                       surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, 260, 350,
                       "DRAG TITLE AREA TO MOVE", RF_COLOR_UI_TEXT_DIM,
                       surface->stride);
        if (error && error[0])
            fb_draw_string((unsigned char *)surface->pixels, 90, 375,
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

static int run_startup_menu(struct toy_window *window, struct toy_renderer *renderer,
                            struct toy_input *input,
                            struct toy_window_events *events,
                            int *net_mode, char *address, int address_size,
                            int *port, int *public_room, int *room_id,
                            int *managed_spectator,
                            const char *error,
                            struct rasterfall_net_discovery *discovery)
{
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
        int64_t now = monotonic_us();
        toy_input_begin_frame(input);
        if (toy_window_poll(window, events, 0) < 0) break;
        toy_input_apply(input, events);
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
            toy_window_move(window, events->button_serial);
        if (events->close_requested) break;
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
                        *net_mode = selected == 1 ? RASTERFALL_NET_HOST : RASTERFALL_NET_CLIENT;
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
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(renderer, &surface, 0x10151D) < 0) break;
            draw_startup_menu(&surface, screen, selected, address, port_text,
                              editing_port, room_text, error, discovery);
            if (toy_window_present(window) < 0) break;
        }
        memset(input->key_pressed, 0, sizeof(input->key_pressed));
    }
    if (discovery_active) rasterfall_net_discovery_close(discovery);
    return 0;
}

static int wait_for_network_connection(struct toy_window *window,
                                       struct toy_renderer *renderer,
                                       struct toy_input *input,
                                       struct toy_window_events *events,
                                       struct rasterfall_net *net,
                                       const char *address, int port)
{
    int64_t deadline = monotonic_us() + 6000000;
    while (monotonic_us() < deadline) {
        struct toy_surface surface;
        char line[96];
        int ready;
        toy_input_begin_frame(input);
        if (toy_window_poll(window, events, 0) < 0) return -2;
        toy_input_apply(input, events);
        if (events->close_requested || toy_input_pressed(input, KEY_ESC))
            return -2;
        rasterfall_net_poll(net);
        rasterfall_net_update_connection(net);
        if (net->connected) return 0;
        ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) return -2;
        if (ready == 0) continue;
        if (toy_renderer_begin(renderer, &surface, 0x10151D) < 0) return -2;
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
        if (toy_window_present(window) < 0) return -2;
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

static void emit_ray_effects(const struct toy_game_ray *ray,
                             int sx, int sy, int sz,
                             int ex, int ey, int ez, int depth_test)
{
    struct rasterfall_effect_cue cue;
    memset(&cue, 0, sizeof(cue));
    cue.type = RASTERFALL_EFFECT_CUE_TRACER;
    cue.depth_test = depth_test;
    cue.sx = sx; cue.sy = sy; cue.sz = sz;
    cue.ex = ex; cue.ey = ey; cue.ez = ez;
    cue.life_ms = RASTERFALL_TRACER_LIFE_MS;
    rasterfall_effects_emit(&effects, &cue);
    if (ray->hit_enemy || ray->hit_world) {
        cue.type = RASTERFALL_EFFECT_CUE_HIT_PARTICLES;
        cue.dir_sy = ray->sy;
        cue.dir_cy = ray->cy;
        rasterfall_effects_emit(&effects, &cue);
    }
}

/* 每次实际开火后同步：把 game 里最新一枪的射线搬进 tracer 环，
 * 起点统一取枪口世界坐标；命中（敌人或墙体）的弹丸在弹着点生成
 * 火花，枪口火花则由独立的粒子与闪光绘制。 */
static void sync_fire_effects(const struct camera *camera)
{
    int i, ray_count;
    struct vec3 muzzle;
    struct vec3 muzzle_view;
    if (game.fire_seq < effects.last_fire_seq) {
        /* 新局（R 重开/死亡重开）：丢弃旧弹道与粒子 */
        rasterfall_effects_reset_fire(&effects);
        return;
    }
    if (game.fire_seq == effects.last_fire_seq) return;
    effects.last_fire_seq = game.fire_seq;
    ray_count = game.ray_count;
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    /* 后坐力 + 枪口世界坐标（开火瞬间采样，含后坐位移） */
    effects.weapon_kick = VIEWMODEL_KICK_MAX;
    {
        int mx, my, mz;
        rasterfall_viewmodel_muzzle_offset(rasterfall_viewmodel_weapon(&game),
                                           effects.weapon_kick, &mx, &my, &mz);
        muzzle_view.x = mx;
        muzzle_view.y = my;
        muzzle_view.z = mz;
    }
    muzzle.x = muzzle_view.x;
    muzzle.y = muzzle_view.y;
    muzzle.z = muzzle_view.z;
    view_to_world(camera, &muzzle, &muzzle);
    for (i = 0; i < ray_count; i++) {
        const struct toy_game_ray *r = &game.rays[i];
        int tracer_x, tracer_y, tracer_z;
        tracer_world_endpoint(r, muzzle.x, muzzle.y, muzzle.z,
                            camera->pitch_sy, camera->pitch_cy,
                            r->ex, r->ez,
                            &tracer_x, &tracer_y, &tracer_z);
        emit_ray_effects(r, muzzle.x, muzzle.y, muzzle.z,
                         tracer_x, tracer_y, tracer_z, 0);
    }
}

/* AI 的弹道同样进入 tracer/命中特效环。起点使用 AI 的世界坐标，终点
 * 仍通过当前观察相机投影，因此第一人称玩家和旁观者都能看到完整弹道。 */
static void sync_ai_fire_effects(const struct camera *camera)
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
        if (actor_index == 0) effects.last_ai_fire_seq = actor->fire_seq;
        ray_count = actor->ray_count;
        if (ray_count < 0) ray_count = 0;
        if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
        int platform_lift = actor->airborne_y;
        int p;
        for (p = 0; p < session.level.platform_count; p++) {
            const struct toy_game_platform *platform = &session.level.platforms[p];
            if (actor->x >= platform->minx && actor->x <= platform->maxx &&
                actor->z >= platform->minz && actor->z <= platform->maxz &&
                platform->height > platform_lift)
                platform_lift = platform->height;
        }
        rasterfall_viewmodel_actor_muzzle(actor->x, actor->z,
                                          actor->sy, actor->cy,
                                          platform_lift,
                                          actor->current_slot >= 0 &&
                                          actor->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                          actor->slots[actor->current_slot].weapon : -1,
                                          &mx, &my, &mz);
        for (i = 0; i < ray_count; i++) {
            const struct toy_game_ray *r = &actor->rays[i];
            int tracer_x, tracer_y, tracer_z;
            tracer_world_endpoint(r, mx, my, mz, 0, 1024,
                                r->ex, r->ez,
                                &tracer_x, &tracer_y, &tracer_z);
            emit_ray_effects(r, mx, my, mz,
                             tracer_x, tracer_y, tracer_z, 1);
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
    for (i = 0; i < ray_count; i++) {
        const struct toy_game_ray *r = &rays[i];
        int tracer_x, tracer_y, tracer_z;
        tracer_world_endpoint(r, muzzle.x, muzzle.y, muzzle.z,
                              0, 1024,
                              r->ex, r->ez,
                              &tracer_x, &tracer_y, &tracer_z);
        emit_ray_effects(r, muzzle.x, muzzle.y, muzzle.z,
                         tracer_x, tracer_y, tracer_z, 0);
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
    snprintf(line, sizeof(line), "WAVE %d  KILLS %d", game.wave, game.kills);
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
    snprintf(line, sizeof(line), "KILLS %d", game.kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, RF_COLOR_UI_TEXT, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(prompt)) / 2,
                   y + 104, prompt, 0xD88A32, surface->stride);
}

static void draw_input_debug(struct toy_surface *surface,
                             const struct toy_input *input,
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

#include "rasterfall_logic_test.inc"

#include "rasterfall_perf.h"

static int dump_model_views(const char *model_path, const char *output_dir,
                            int use_sphere, int use_toon)
{
    static const char *names[3] = {"front", "side", "back"};
    struct camera cameras[3];
    struct rasterfall_model_asset model;
    struct toy_surface surface;
    struct toy_renderer renderer;
    uint32_t *pixels;
    char path[512];
    int i, result = 0;

    memset(&model, 0, sizeof(model));
    memset(cameras, 0, sizeof(cameras));
    if (rasterfall_model_load(&model, model_path) < 0) {
        __fprintf(2, "rasterfall: cannot load preview model %s\n", model_path);
        return 1;
    }
    if (tlibc_recursive_mkdir(output_dir) < 0) {
        __fprintf(2, "rasterfall: cannot create preview directory %s\n",
                  output_dir);
        rasterfall_model_unload(&model);
        return 1;
    }
    surface.width = 800;
    surface.height = 800;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) {
        rasterfall_model_unload(&model);
        return 1;
    }
    surface.pixels = pixels;
    toy_renderer_init(&renderer);

    /* Front and back look along the model Z axis; side looks along +X. */
    cameras[0].z = -800; cameras[0].cy = 1024;
    cameras[1].x = -800; cameras[1].sy = 1024;
    cameras[2].z = 800; cameras[2].cy = -1024;
    for (i = 0; i < 3; i++) cameras[i].pitch_cy = 1024;

    for (i = 0; i < 3; i++) {
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
            rasterfall_render_model_preview(&renderer, &cameras[i], &model,
                                             use_sphere, use_toon) < 0) {
            result = 1;
            break;
        }
        toy_renderer_flush(&renderer);
        if (snprintf(path, sizeof(path), "%s/%s.bmp", output_dir, names[i]) >=
                (int)sizeof(path) ||
            rasterfall_hud_dump_bmp(path, &surface) < 0) {
            __fprintf(2, "rasterfall: cannot write model preview %s\n", names[i]);
            result = 1;
            break;
        }
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);
    rasterfall_model_unload(&model);
    return result;
}

static int dump_model_view_comparison(const char *model_path,
                                      const char *output_dir)
{
    char with_sphere[512], without_sphere[512];
    if (snprintf(with_sphere, sizeof(with_sphere), "%s/with-sphere",
                 output_dir) >= (int)sizeof(with_sphere) ||
        snprintf(without_sphere, sizeof(without_sphere), "%s/without-sphere",
                 output_dir) >= (int)sizeof(without_sphere)) return 1;
    if (dump_model_views(model_path, with_sphere, 1, 1) != 0) return 1;
    return dump_model_views(model_path, without_sphere, 0, 1);
}

static int dump_model_toon_comparison(const char *model_path,
                                      const char *output_dir)
{
    char with_toon[512], without_toon[512];
    if (snprintf(with_toon, sizeof(with_toon), "%s/with-toon",
                 output_dir) >= (int)sizeof(with_toon) ||
        snprintf(without_toon, sizeof(without_toon), "%s/without-toon",
                 output_dir) >= (int)sizeof(without_toon)) return 1;
    if (dump_model_views(model_path, with_toon, 1, 1) != 0) return 1;
    return dump_model_views(model_path, without_toon, 1, 0);
}

int main(int argc, char **argv)
{
    struct toy_window *window;
    struct toy_window_events events;
    struct toy_input input;
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct camera camera;
    struct control_settings settings;
    struct pause_menu pause_menu;
    struct managed_terminal managed_terminal;
    int64_t last_time, accumulator = 0, fps_window_start, fps_elapsed;
    int64_t prev_begin = 0, last_active = 0;   /* 帧间隔统计 */
    int running = 1, pointer_lock_requested = 0, paused = 1;
    int coordinate_axes = 0;
    int return_to_menu = 0;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;
    int display_fps = 0, fps_window_frames = 0;
    int fire_edge = 0;
    int shove_edge = 0;
    int pointer_turn_pending = 0, pointer_pitch_pending = 0;
    int64_t menu_nav_ready_us = 0;
    /* 按键按压边沿跨帧保留位：逻辑步（E/R 及切枪换弹）可能因
     * accumulator 不足而整帧不跑（长 stall 后连续几帧都不跑），边沿若
     * 只在 key_pressed 里会被下一轮 begin_frame 清掉。这里逐键记录
     * 到达的按压，每帧合入 key_pressed 供消费方读取；逻辑步跑过的那
     * 帧末尾统一清除。 */
    unsigned char pending_key_edges[TOY_INPUT_KEY_COUNT];
    int input_debug = 0, input_event_count = 0, have_last_key = 0;
    int texture_stats = 0;
    int stats_enabled = 1;
    struct rasterfall_perf_stats stats, stats_total;
    unsigned int last_key = 0;
    int last_key_pressed = 0;
    struct rasterfall_audio audio;
    struct rasterfall_net net;
    struct rasterfall_net_discovery discovery;
    char host_address[16];
    uint64_t seed;

    int logic_test = 0;
    int requested_net_mode = RASTERFALL_NET_OFF;
    int net_port = RASTERFALL_NET_DEFAULT_PORT;
    int public_room = 0, public_room_id = 0;
    int net_loss_percent = 0;
    const char *net_address = NULL;
    const char *startup_error = NULL;
    char selected_address[64];
    int auto_mode = 0;
    int managed_spectator = 0;
    int managed_third_person = 0;
    const char *dump_path = 0;
    const char *view_model_path = 0;
    const char *view_output_dir = 0;
    int compare_model_views = 0;
    int compare_model_toon = 0;
    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "--input-test") == 0) input_debug = 1;
        else if (strcmp(argv[arg], "--logic-test") == 0 ||
                 strcmp(argv[arg], "--net-test") == 0) logic_test = 1;
        else if (strcmp(argv[arg], "--host") == 0)
            requested_net_mode = RASTERFALL_NET_HOST;
        else if (strcmp(argv[arg], "--connect") == 0 && arg + 1 < argc) {
            requested_net_mode = RASTERFALL_NET_CLIENT;
            net_address = argv[++arg];
        } else if (strcmp(argv[arg], "--port") == 0 && arg + 1 < argc)
            net_port = parse_positive_int(argv[++arg], RASTERFALL_NET_DEFAULT_PORT);
        else if (strcmp(argv[arg], "--net-loss") == 0 && arg + 1 < argc)
            net_loss_percent = parse_positive_int(argv[++arg], 0);
        else if (strcmp(argv[arg], "--auto") == 0) auto_mode = 1;
        else if (strcmp(argv[arg], "--textures") == 0) textures_enabled = 1;
        else if (strcmp(argv[arg], "--no-textures") == 0) textures_enabled = 0;
        else if (strcmp(argv[arg], "--no-stats") == 0) stats_enabled = 0;
        else if (strcmp(argv[arg], "--texture-stats") == 0) texture_stats = 1;
        else if (strcmp(argv[arg], "--dump-frame") == 0 && arg + 1 < argc)
            dump_path = argv[++arg];
        else if (strcmp(argv[arg], "--model-views") == 0 && arg + 2 < argc) {
            view_model_path = argv[++arg];
            view_output_dir = argv[++arg];
        } else if (strcmp(argv[arg], "--model-views-compare") == 0 &&
                   arg + 2 < argc) {
            view_model_path = argv[++arg];
            view_output_dir = argv[++arg];
            compare_model_views = 1;
        } else if (strcmp(argv[arg], "--model-views-toon-compare") == 0 &&
                   arg + 2 < argc) {
            view_model_path = argv[++arg];
            view_output_dir = argv[++arg];
            compare_model_toon = 1;
        }
        else if (strcmp(argv[arg], "--frames") == 0 && arg + 1 < argc) {
            const char *p = argv[++arg];
            while (*p >= '0' && *p <= '9')
                frame_limit = frame_limit * 10 + (*p++ - '0');
        }
    }
    if (view_model_path) {
        if (compare_model_views)
            return dump_model_view_comparison(view_model_path, view_output_dir);
        if (compare_model_toon)
            return dump_model_toon_comparison(view_model_path, view_output_dir);
        return dump_model_views(view_model_path, view_output_dir, 1, 1);
    }
    rasterfall_net_init(&net);
    rasterfall_net_set_loss(&net, net_loss_percent);
    rasterfall_net_discovery_init(&discovery);
    rf_windows_log("startup: loading map");
    strcpy(host_address, "127.0.0.1");
    if (rasterfall_session_load(&session, "rasterfall/assets/maps/rasterfall.map") < 0) {
        __fprintf(2, "rasterfall: cannot load map rasterfall/assets/maps/rasterfall.map\n");
        return 1;
    }
    render_context.session = &session;
    render_context.effects = &effects;
    render_context.net = &net;
    render_context.wall_texture = &wall_texture_view;
    render_context.model_texture = &model_texture_view;
    render_context.textures_enabled = textures_enabled;
    rf_windows_log("startup: map loaded, binding renderer");
    rasterfall_render_bind(&render_context);
    rasterfall_render_bake_lightmap();
    rf_windows_log("startup: lightmap baked");
    rasterfall_effects_init(&effects);
    __printf("rasterfall: baked lightmap %dx%d\n", BAKED_LM_W, BAKED_LM_H);
    memset(&scene_texture, 0, sizeof(scene_texture));
    memset(&scene_texture_view, 0, sizeof(scene_texture_view));
    memset(&model_texture, 0, sizeof(model_texture));
    memset(&model_texture_view, 0, sizeof(model_texture_view));
    if (toy_texture_load("rasterfall/assets/textures/model_diffuse.ttex",
                         &model_texture) == 0) {
        model_texture_view.data = model_texture.data;
        model_texture_view.width = model_texture.width;
        model_texture_view.height = model_texture.height;
        model_texture_view.data_size = model_texture.data_size;
        model_texture_view.channels = model_texture.channels;
        __printf("rasterfall: model texture loaded (%u x %u)\n",
                 model_texture.width, model_texture.height);
    }
    if (textures_enabled && toy_texture_load("rasterfall/assets/textures/wall.ttex",
                                              &scene_texture) == 0) {
        scene_texture_view.data = scene_texture.data;
        scene_texture_view.width = scene_texture.width;
        scene_texture_view.height = scene_texture.height;
        scene_texture_view.data_size = scene_texture.data_size;
        scene_texture_view.channels = scene_texture.channels;
        wall_texture_view.data = scene_texture_view.data;
        wall_texture_view.width = scene_texture_view.width;
        wall_texture_view.height = scene_texture_view.height;
        wall_texture_view.data_size = scene_texture_view.data_size;
        wall_texture_view.channels = scene_texture_view.channels;
        __printf("rasterfall: UV texture loaded (%u x %u)\n",
                 scene_texture.width, scene_texture.height);
    } else if (textures_enabled) {
        __printf("rasterfall: UV texture unavailable, using checkerboard fallback\n");
    } else {
        __printf("rasterfall: textures disabled, using pure colors\n");
    }
    if (logic_test) {
        int result = run_logic_test();
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        if (model_texture.blob) toy_texture_unload(&model_texture);
        rasterfall_session_unload(&session);
        return result;
    }
    /* 服务器断开（WSLg 组合器/音频服务重启）时 socket 写会触发 SIGPIPE
     * 并默认杀死进程；忽略后写返回 EPIPE，由既有错误路径接管（音频线程
     * 静默停声、wayland 发送失败则主循环干净退出）。SIG_IGN 值为 1。 */
    tlibc_sigaction(SIGPIPE, (void (*)(int))1);
    toy_input_init(&input);
    memset(&managed_terminal, 0, sizeof(managed_terminal));
    memset(pending_key_edges, 0, sizeof(pending_key_edges));
    toy_renderer_init(&renderer);
    rasterfall_viewmodel_set_texture(&model_texture_view);
    settings.mouse_level = 3;
    settings.keyboard_level = 5;
    rasterfall_render_set_coordinate_axes(coordinate_axes);
    pause_menu.selected = PAUSE_ITEM_RESUME;
    if (__getrandom(&seed, sizeof(seed), 0) < 0)
        seed = (uint64_t)monotonic_us();
    if (seed == 0) seed = 1;
    rasterfall_session_reset(&session, &camera, seed);
    rf_windows_log("startup: session reset");
    window = toy_window_open("Rasterfall", 800, 450);
    if (!window) {
        __fprintf(2, "rasterfall: cannot create Wayland window\n");
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        if (model_texture.blob) toy_texture_unload(&model_texture);
        rasterfall_net_close(&net);
        rasterfall_session_unload(&session);
        toy_renderer_destroy(&renderer);
        return 1;
    }
    rf_windows_log("startup: window opened");
startup_again:
    {
        int menu_selected = requested_net_mode != RASTERFALL_NET_OFF ||
                            frame_limit > 0 || auto_mode || dump_path;
        strcpy(selected_address, net_address ? net_address : "127.0.0.1");
        if (!menu_selected && !run_startup_menu(window, &renderer, &input,
                                                &events,
                                                &requested_net_mode,
                                                selected_address,
                                                sizeof(selected_address),
                                                &net_port, &public_room,
                                                &public_room_id, &managed_spectator,
                                                startup_error,
                                                &discovery)) {
            toy_window_close(window);
            if (scene_texture.blob) toy_texture_unload(&scene_texture);
            if (model_texture.blob) toy_texture_unload(&model_texture);
            rasterfall_session_unload(&session);
            toy_renderer_destroy(&renderer);
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
            int connect_result = wait_for_network_connection(window, &renderer,
                                                             &input, &events,
                                                             &net, host_address,
                                                             RASTERFALL_NET_PUNCH_PORT);
            if (connect_result != 0) {
                startup_error = connect_result == -2 ?
                    "PUBLIC JOIN CANCELLED" :
                    "PUBLIC JOIN TIMEOUT: ROOM NOT FOUND";
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
            toy_window_close(window);
            if (scene_texture.blob) toy_texture_unload(&scene_texture);
            if (model_texture.blob) toy_texture_unload(&model_texture);
            rasterfall_session_unload(&session);
            toy_renderer_destroy(&renderer);
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
            int connect_result = wait_for_network_connection(window, &renderer,
                                                             &input, &events,
                                                             &net, net_address,
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
    if (rasterfall_audio_start(&audio) < 0) {
        __printf("rasterfall: audio unavailable, playing silent\n");
        rf_windows_log("startup: audio unavailable");
    } else {
        rf_windows_log("startup: audio ready");
    }
    last_time = monotonic_us();
    fps_window_start = last_time;
    rasterfall_perf_init(&stats);
    rasterfall_perf_init(&stats_total);
    while (running) {
        int64_t now, elapsed, t_frame, t_stage;
        unsigned long prev_tris;
        int logic_steps = 0;
        int resumed = 0;
        int stage_pixels;
        int ready;
        static int logged_first_frame;
        unsigned char game_events[TOY_GAME_MAX_EVENTS];
        int game_event_count;
        toy_input_begin_frame(&input);
        /* 非阻塞收输入：present 后立刻开始下一帧 CPU 工作，组合器处理
         * 已提交缓冲的时间被渲染流水线掩盖（双缓冲）。 */
        if (toy_window_poll(window, &events, 0) < 0) break;
        toy_input_apply(&input, &events);
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
        if (net.mode == RASTERFALL_NET_CLIENT && net.players[0].active) {
            sync_network_fire_effects(&camera, &net.players[0].camera,
                                      0,
                                      net.players[0].weapon,
                                      net.players[0].fire_seq,
                                      net.players[0].ray_count,
                                      net.players[0].rays, NULL);
        }
        if (net.mode == RASTERFALL_NET_HOST) {
            for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
                const struct rasterfall_net_client *client = &net.clients[i];
                int weapon;
                if (!client->active || !client->connected) continue;
                weapon = client->current_slot >= 0 &&
                         client->current_slot < TOY_GAME_WEAPON_SLOTS ?
                         client->slots[client->current_slot].weapon : -1;
                sync_network_fire_effects(&camera, &client->camera,
                                          client->client_id, weapon,
                                          client->fire_seq, client->ray_count,
                                          client->rays, &audio);
            }
        } else if (net.mode == RASTERFALL_NET_CLIENT) {
            for (int i = 0; i < RASTERFALL_NET_PLAYER_MAX; i++) {
                const struct rasterfall_net_player *player = &net.players[i];
                if (!player->active || i == net.local_player_id) continue;
                sync_network_fire_effects(&camera, &player->camera, i,
                                          player->weapon, player->fire_seq,
                                          player->ray_count, player->rays,
                                          NULL);
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
        if (managed_spectator && !managed_terminal.open && !paused &&
            managed_terminal_take_key(&input, pending_key_edges, KEY_F2)) {
            managed_terminal.open = 1;
            managed_terminal.line[0] = 0;
            strcpy(managed_terminal.message, "TYPE HELP");
            toy_window_set_pointer_lock(window, 0);
            pointer_lock_requested = 0;
            paused = 1;
        }
        {
            int terminal_was_open = managed_terminal.open;
            if (managed_terminal.open)
                managed_terminal_input(&managed_terminal, &input,
                                       pending_key_edges, &session, &camera);
            if (terminal_was_open && !managed_terminal.open) {
                int capture_result = toy_window_set_pointer_lock(window, 1);
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
                int64_t menu_now = monotonic_us();
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
                else if (pause_menu.selected == PAUSE_ITEM_MENU) {
                    return_to_menu = 1;
                    running = 0;
                }
            }
            if (pending_key_edges[KEY_ESC]) {
                pending_key_edges[KEY_ESC] = 0;
                resume_requested = 1;
            }
            if (resume_requested) {
            int capture_result = toy_window_set_pointer_lock(window, 1);
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
                toy_window_set_pointer_lock(window, 0);
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
        if (!paused && !resumed && toy_input_pressed(&input, KEY_SPACE)) {
            if (managed_spectator)
                managed_third_person = !managed_third_person;
            else
                fire_edge = 1;
        }
        /* 推开输入：右键与开火同一套边沿锁存（恢复点击帧不算） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_RIGHT)
            shove_edge = 1;
        if (events.close_requested) running = 0;
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
                    game.px = camera.x;
                    game.pz = camera.z;
                    toy_game_equip_weapon(&game, wslot[idx % 3]);
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
        now = monotonic_us();
        elapsed = now - last_time;
        last_time = now;
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
                rasterfall_effects_update(&effects, FIXED_STEP_US / 1000);
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
                if (net.mode == RASTERFALL_NET_HOST && game.player_down) {
                    /* The render-only spectator camera must not become the
                     * authoritative body position on the next tick. */
                    camera.x = game.px;
                    camera.z = game.pz;
                }
                if (game.state == TOY_GAME_PLAYING &&
                    !(net.mode == RASTERFALL_NET_CLIENT &&
                      (!net.connected || !net.world_ready))) {
                    if (shop_input)
                        memset(&command, 0, sizeof(command));
                    else
                        build_game_command(&command, &input, &settings, fire_edge,
                                           shove_edge, pointer_turn_pending,
                                           pointer_pitch_pending);
                    if (game.player_down &&
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
                            game.kills = 0;
                            game.special_kills = 0;
                            game.damage_dealt = 0;
                            game.throwable_damage_dealt = 0;
                        }
                    }
                    capture_jump_vector(&command, &camera);
                    if (net.mode == RASTERFALL_NET_CLIENT)
                        rasterfall_session_step_client(&session, &camera,
                                                       &command,
                                                       FIXED_STEP_US / 1000);
                    else {
                        /* Feed the last authoritative client position into
                         * the host AI before this tick chooses its target. */
                        if (net.mode == RASTERFALL_NET_HOST)
                            rasterfall_net_prepare_host_step(&net, &game);
                        rasterfall_session_step(&session, &camera, &command,
                                                FIXED_STEP_US / 1000);
                        if (net.mode == RASTERFALL_NET_HOST)
                            rasterfall_net_apply_local_rescue(
                                &net, &session, &camera,
                                (command.buttons & RASTERFALL_CMD_INTERACT) != 0,
                                FIXED_STEP_US / 1000);
                    }
                    if (net.mode == RASTERFALL_NET_CLIENT)
                        rasterfall_net_send_command(
                            &net, &command, &camera, &game,
                            command.jump_dx,
                            command.jump_dz);
                    consume_game_command_edges(&input);
                    pointer_turn_pending = 0;
                    pointer_pitch_pending = 0;
                    fire_edge = 0;
                    shove_edge = 0;
                } else if (toy_input_pressed(&input, KEY_R)) {
                    /* 死亡或通关结算：R 重开 */
                    memset(&command, 0, sizeof(command));
                    command.buttons = RASTERFALL_CMD_RESET;
                    if (net.mode == RASTERFALL_NET_CLIENT) {
                        /* Reset is host-authoritative; wait for its snapshot. */
                        rasterfall_net_send_command(&net, &command, &camera, &game,
                                                    0, 0);
                    } else {
                        rasterfall_session_step(&session, &camera, &command,
                                                FIXED_STEP_US / 1000);
                        if (net.mode == RASTERFALL_NET_HOST)
                            rasterfall_net_reset_host(&net);
                    }
                    input.key_pressed[KEY_R] = 0;
                    fire_edge = 0;
                    shove_edge = 0;
                }
                if (net.mode == RASTERFALL_NET_HOST) {
                    rasterfall_net_apply_clients(&net, &session, &camera);
                    rasterfall_net_capture_events(&net, &game);
                    /* Remote fire state is advanced by apply_remote().  The
                     * pre-step visual pass cannot observe that shot until
                     * the next frame; replay the guarded sequence here so
                     * the host hears it immediately and never loses a burst
                     * between snapshots. */
                    for (int i = 0; i < RASTERFALL_NET_CLIENT_MAX; i++) {
                        const struct rasterfall_net_client *client =
                            &net.clients[i];
                        int weapon;
                        if (!client->active || !client->connected) continue;
                        weapon = client->current_slot >= 0 &&
                                 client->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                 client->slots[client->current_slot].weapon : -1;
                        sync_network_fire_effects(&camera, &client->camera,
                                                  client->client_id, weapon,
                                                  client->fire_seq,
                                                  client->ray_count, client->rays,
                                                  &audio);
                    }
                    /* 15 Hz authoritative snapshots are sufficient once
                     * clients interpolate enemy positions between updates. */
                    if ((net.tick % 4) == 0)
                        rasterfall_net_send_snapshot(&net, &session, &camera, &game,
                                                     session.air_walls_enabled,
                                                     session.manual_alarm_on,
                                                     session.manual_alarm_timer);
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
            sync_ai_fire_effects(&camera);
        }
        if (accumulator >= FIXED_STEP_US) accumulator %= FIXED_STEP_US;
        /* 本帧跑过逻辑步：所有保留边沿都已暴露给消费方，可以清除；
         * 一帧都没跑（accumulator 不足，长 stall 后常见）则留到下一帧，
         * 避免按键被吞。 */
        if (logic_steps > 0)
            memset(pending_key_edges, 0, sizeof(pending_key_edges));
        rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_LOGIC, &t_stage, 0, 0);
        /* 帧渲染计时从申请缓冲开始；双缓冲占用时的等待计入 stall。
         * 帧间隔：本次 begin_frame 距上次的墙钟时间 wall，与上次渲染
         * 帧的活跃时间相减得到 wait（轮询/逻辑/调度/组合器等待），
         * 用来解释平均帧率与各阶段活跃耗时之间的缺口。 */
        /* 帧间隔：本次 begin_frame 距上次的墙钟时间 wall（含双缓冲等待、
         * 事件轮询、逻辑与调度）。循环会在双缓冲被占用时反复走 stall 路径
         * 而不渲染，wall 按迭代累计、除以渲染帧数即 1/fps；wait（present
         * 到下一次 begin 的间隔）在 dump 中用 wall − 活跃帧时间推导，
         * 与各阶段统计严格对消。 */
        t_frame = monotonic_us();
        if (prev_begin > 0)
            rasterfall_perf_add_interval(&stats, &stats_total, t_frame - prev_begin);
        prev_begin = t_frame;
        t_stage = t_frame;
        ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready == 0) {
            struct toy_window_events stall_events;
            /* 双缓冲都在组合器手里：阻塞等 frame callback 释放，期间
             * 继续收输入。等待批次必须立即并入输入状态——若沿用共用
             * events，下一轮 poll 会覆盖这批事件，按键释放事件丢失后
             * key_down 无法清零，角色会持续移动不受控制（粘键）。 */
            if (toy_window_poll(window, &stall_events, 1000) < 0) break;
            /* stall 从申请缓冲计到等回 frame callback（含 poll 等待），
             * 即 wait 中双缓冲背压的部分。 */
            rasterfall_perf_add_stall(&stats, &stats_total, monotonic_us() - t_frame);
            toy_input_apply(&input, &stall_events);
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
            struct camera render_camera;
            /* Local movement is client-authoritative; host position
             * corrections are intentionally not applied to the camera. */
            if (!logged_first_frame) {
                rf_windows_log("startup: first frame begin");
                logged_first_frame = 1;
            }
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_BEGIN,
                           &t_stage, 0, 0);
            prev_tris = renderer.submitted_triangles;
            /* Spectating after a network death is render-only.  Never mutate
             * the authoritative body camera: it is reported to the host on
             * the next input packet and must remain at the death position. */
            render_camera = camera;
            set_network_spectator_camera(&render_camera, &net);
            if (managed_spectator)
                set_managed_spectator_camera(&render_camera, &camera,
                                             managed_third_person);
            scene_pixels = rasterfall_render_scene(&renderer, &render_camera);
            scene_pixels += rasterfall_render_flags(&renderer, &render_camera);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_SCENE, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            prev_tris = renderer.submitted_triangles;
            scene_pixels += rasterfall_render_enemies(&renderer, &render_camera);
            scene_pixels += rasterfall_render_ai_teammate(&renderer, &render_camera);
            if (managed_spectator && managed_third_person)
                scene_pixels += rasterfall_render_managed_player(
                    &renderer, &render_camera, &camera);
            scene_pixels += rasterfall_render_network_teammate(&renderer, &render_camera, &net);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_ENEMIES, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            /* World lettering is submitted before the flush, so it follows
             * the sign/flag plane and participates in depth testing. */
            scene_pixels += rasterfall_render_sign_text(&renderer, &render_camera);
            scene_pixels += rasterfall_render_flag_text(&renderer, &render_camera);
            /* 世界几何并行光栅化；弹道/粒子/枪模随后直接写屏覆盖 */
            prev_tris = (unsigned long)renderer.cmd_count;
            stage_pixels = toy_renderer_flush(&renderer);
            scene_pixels += stage_pixels;
            if (coordinate_axes)
                rasterfall_render_coordinate_labels(&surface, &render_camera);
#if TOY_CONFIG_SHOW_MODEL_PATHS
            rasterfall_render_gallery_selection(&surface, &render_camera);
#endif
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_RASTER,
                           &t_stage, prev_tris, (unsigned long)stage_pixels);
            rasterfall_perf_add_raster(&stats, &stats_total, &renderer, prev_tris,
                            (unsigned long)stage_pixels);
            /* 直接写屏与第二次光栅化（拾取物）都归入 overlay 阶段 */
            prev_tris = renderer.submitted_triangles;
            stage_pixels = 0;
            stage_pixels += rasterfall_render_tracers(&renderer, &render_camera);
            stage_pixels += rasterfall_render_particles(&renderer, &render_camera);
            /* 第一人称武器：最后画，叠加在世界之上 */
            if (!game.player_down)
                stage_pixels += rasterfall_viewmodel_render(&renderer, &game,
                                                            &effects);
            if (game.state == TOY_GAME_OVER) {
                draw_game_over_panel(&surface,
                                     net.mode == RASTERFALL_NET_CLIENT);
            } else if (game.state == TOY_GAME_WON) {
                draw_level_won_panel(&surface,
                                     net.mode == RASTERFALL_NET_CLIENT);
            } else if (managed_terminal.open) {
                draw_managed_terminal(&surface, &managed_terminal);
            } else if (paused) {
                draw_pause_overlay(&surface, &pause_menu, &settings,
                                   coordinate_axes);
            } else {
                draw_crosshair(&surface, &game);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud, &net, host_address, net_port, &camera);
                    rasterfall_hud_render(&surface, display_fps, &hud);
                }
            }
            if (game.state == TOY_GAME_PLAYING && !paused &&
                !session.shop_open) {
                stage_pixels += rasterfall_render_interactables(&renderer, &render_camera);
                /* 拾取物保持画在枪模之上的现状顺序 */
                stage_pixels += toy_renderer_flush(&renderer);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud, &net, host_address, net_port, &camera);
                    rasterfall_hud_draw_interact_prompt(&renderer, &hud);
                }
            }
            scene_pixels += stage_pixels;
            rasterfall_render_ai_teammate_name(&renderer, &render_camera);
            rasterfall_render_network_teammate_status(&renderer, &render_camera, &net);
            rasterfall_hud_damage_flash(&surface, &game);
            if (game.state == TOY_GAME_PLAYING && !paused &&
                toy_input_down(&input, KEY_TAB))
                draw_scoreboard(&surface, &net);
            if (input_debug)
                draw_input_debug(&surface, &input,
                                 have_last_key ? last_key : 0,
                                 have_last_key ? last_key_pressed : 0,
                                 input_event_count);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_OVERLAY,
                           &t_stage, renderer.submitted_triangles - prev_tris,
                           (unsigned long)stage_pixels);
            present_result = toy_window_present(window);
            if (present_result < 0) break;
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_PRESENT,
                           &t_stage, 0, 0);
            rendered_frames++;
            fps_window_frames++;
            now = monotonic_us();
            last_active = now - t_frame;
            rasterfall_perf_record_frame(&stats, &stats_total, last_active);
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
    if (return_to_menu) {
        rasterfall_session_set_managed_ai(&session, 0);
        managed_spectator = 0;
        managed_third_person = 0;
        memset(&managed_terminal, 0, sizeof(managed_terminal));
        rasterfall_audio_stop(&audio);
        rasterfall_audio_unload_assets(&audio);
        rasterfall_net_discovery_close(&discovery);
        rasterfall_net_close(&net);
        rasterfall_session_reset(&session, &camera, seed);
        rasterfall_effects_init(&effects);
        requested_net_mode = RASTERFALL_NET_OFF;
        net_address = NULL;
        startup_error = NULL;
        running = 1;
        paused = 1;
        accumulator = 0;
        rendered_frames = 0;
        scene_pixels = 0;
        display_fps = 0;
        fps_window_frames = 0;
        return_to_menu = 0;
        /* Do not carry the previous pause-menu selection or its pending
         * Enter edge into the next round.  Otherwise choosing MENU leaves
         * the next game paused on MENU and Enter immediately returns again. */
        pause_menu.selected = PAUSE_ITEM_RESUME;
        memset(pending_key_edges, 0, sizeof(pending_key_edges));
        memset(&input, 0, sizeof(input));
        toy_input_init(&input);
        toy_window_set_pointer_lock(window, 0);
        pointer_lock_requested = 0;
        goto startup_again;
    }
    if (stats_enabled && stats_total.frames > 0)
        rasterfall_perf_dump(&stats_total, "total");
    rasterfall_audio_stop(&audio);
    rasterfall_audio_unload_assets(&audio);
    rasterfall_net_discovery_close(&discovery);
    if (scene_texture.blob) toy_texture_unload(&scene_texture);
    if (model_texture.blob) toy_texture_unload(&model_texture);
    if (dump_path) rasterfall_hud_dump_frame(dump_path, &surface);
    rasterfall_net_close(&net);
    rasterfall_session_unload(&session);
    toy_window_close(window);
    __printf("rasterfall: %d frames, %d scene pixels, position=(%d,%d)\n",
             rendered_frames, scene_pixels, camera.x, camera.z);
    if (texture_stats)
        __printf("rasterfall: texture stats triangles=%lu pixels=%lu fallback=%lu\n",
                 renderer.textured_triangles, renderer.textured_pixels,
                 renderer.texture_fallback_pixels);
    toy_renderer_destroy(&renderer);
    return rendered_frames > 0 && scene_pixels == 0 ? 2 : 0;
}
