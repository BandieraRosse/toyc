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
#define KEY_E     18
#define KEY_R     19
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

#define PAUSE_ITEM_RESUME   0
#define PAUSE_ITEM_MOUSE    1
#define PAUSE_ITEM_KEYBOARD 2
#define PAUSE_ITEM_MENU     3
#define PAUSE_ITEM_COUNT    4

enum rasterfall_startup_screen {
    RASTERFALL_STARTUP_MAIN,
    RASTERFALL_STARTUP_MANUAL_IP,
    RASTERFALL_STARTUP_PUBLIC_ROOM
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
            if (net_state->peer_known && net_state->connected &&
                net_state->peer_down) {
                long dx = (long)camera->x - net_state->peer_camera.x;
                long dz = (long)camera->z - net_state->peer_camera.z;
                long d2 = dx * dx + dz * dz;
                if (d2 <= (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE) {
                    player_target = 1; player_target_d2 = d2;
                    player_progress = net_state->peer_revive_progress_ms;
                    player_active = net_state->local_revive_peer_active ||
                                    player_progress > 0;
                }
            }
            for (i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
                const struct rasterfall_net_remote *remote =
                    &net_state->remotes[i];
                long dx, dz, d2;
                if (!remote->active || !remote->connected || !remote->down)
                    continue;
                dx = (long)camera->x - remote->camera.x;
                dz = (long)camera->z - remote->camera.z;
                d2 = dx * dx + dz * dz;
                if (d2 > (long)RASTERFALL_INTERACT_RANGE *
                          RASTERFALL_INTERACT_RANGE ||
                    (player_target >= 0 && d2 >= player_target_d2)) continue;
                player_target = remote->client_id;
                player_target_d2 = d2;
                player_progress = remote->revive_progress_ms;
                player_active = remote->local_revive_active ||
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
    hud->net = net_state;
    hud->host_address = host_address;
    hud->host_port = host_port;
}
#define game (session.game_state)
#define interactables (session.items)
#define interactable_count (session.item_count)
#define highlighted (session.highlight_index)
#define manual_alarm_enabled (session.manual_alarm_on)
#define manual_alarm_timer_ms (session.manual_alarm_timer)
#define horde_banner_ms (session.banner_ms)
#define interaction_banner (session.banner_text)

static long monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
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
    } else if (net->mode == RASTERFALL_NET_HOST && game.player_down &&
               net->peer_known && net->connected && !net->peer_down) {
        camera->x = net->peer_camera.x - net->peer_camera.sy * distance / 1024;
        camera->z = net->peer_camera.z - net->peer_camera.cy * distance / 1024;
        camera->sy = net->peer_camera.sy;
        camera->cy = net->peer_camera.cy;
        camera->pitch_sy = net->peer_camera.pitch_sy;
        camera->pitch_cy = net->peer_camera.pitch_cy;
        camera->y = net->peer_camera.y;
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
    int turn = relative_x * 3 * percent / 100;
    int pitch = -relative_y * 3 * percent / 100;
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
    command->fire_held = toy_input_down(input, KEY_SPACE);
    if (fire_edge) command->buttons |= RASTERFALL_CMD_FIRE;
    if (shove_edge) command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_LEFTSHIFT))
        command->buttons |= RASTERFALL_CMD_JUMP;
    if (toy_input_pressed(input, KEY_SLASH))
        command->buttons |= RASTERFALL_CMD_SHOVE;
    if (toy_input_pressed(input, KEY_R)) command->buttons |= RASTERFALL_CMD_RELOAD;
    if (toy_input_pressed(input, KEY_1)) command->buttons |= RASTERFALL_CMD_SLOT_1;
    if (toy_input_pressed(input, KEY_2)) command->buttons |= RASTERFALL_CMD_SLOT_2;
    if (toy_input_pressed(input, KEY_E)) command->buttons |= RASTERFALL_CMD_INTERACT;
    if (toy_input_pressed(input, KEY_COMMA))
        command->buttons |= RASTERFALL_CMD_TURN_LEFT;
    if (toy_input_pressed(input, KEY_DOT))
        command->buttons |= RASTERFALL_CMD_TURN_RIGHT;
}

static void consume_game_command_edges(struct toy_input *input)
{
    input->key_pressed[KEY_R] = 0;
    input->key_pressed[KEY_1] = 0;
    input->key_pressed[KEY_2] = 0;
    input->key_pressed[KEY_E] = 0;
    input->key_pressed[KEY_COMMA] = 0;
    input->key_pressed[KEY_DOT] = 0;
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
                               const struct control_settings *settings)
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
                              const char *error)
{
    int i, y = 92;
    char line[96];
    fill_rect(surface, 0, 0, surface->width, surface->height, 0x10151D);
    fb_draw_string((unsigned char *)surface->pixels, 238, 35,
                   "RASTERFALL", RF_COLOR_UI_ACCENT, surface->stride);
    if (screen == RASTERFALL_STARTUP_MAIN) {
        static const char *items[] = {"CREATE LOCAL ROOM", "CREATE PUBLIC ROOM",
                                      "JOIN PUBLIC ROOM", "JOIN BY ADDRESS", "QUIT"};
        for (i = 0; i < 5; i++) {
            uint32_t color = i == selected ? RF_COLOR_UI_ACCENT : RF_COLOR_UI_TEXT;
            if (i == selected)
                fill_rect(surface, 180, y + i * 34 - 4, 440, FB_FONT_H + 8,
                          0x293746);
            fb_draw_string((unsigned char *)surface->pixels, 205, y + i * 34,
                           items[i], color, surface->stride);
        }
        fb_draw_string((unsigned char *)surface->pixels, 150, 290,
                       "UP DOWN SELECT   ENTER CONFIRM", RF_COLOR_UI_TEXT_MUTED,
                       surface->stride);
        fb_draw_string((unsigned char *)surface->pixels, 260, 315,
                       "DRAG TITLE AREA TO MOVE", RF_COLOR_UI_TEXT_DIM,
                       surface->stride);
        if (error && error[0])
            fb_draw_string((unsigned char *)surface->pixels, 90, 340,
                           error, 0xFF8060, surface->stride);
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
                            const char *error)
{
    int screen = RASTERFALL_STARTUP_MAIN, selected = 0, running = 1;
    int editing_port = 0;
    char port_text[8];
    char room_text[8];
    long nav_ready = 0;
    strcpy(address, "127.0.0.1");
    strcpy(port_text, "28460");
    room_text[0] = 0;
    while (running) {
        struct toy_surface surface;
        long now = monotonic_us();
        toy_input_begin_frame(input);
        if (toy_window_poll(window, events, 0) < 0) break;
        toy_input_apply(input, events);
        /* Wayland 的 xdg_toplevel.move 必须使用鼠标按下事件的 serial。
         * 菜单顶部保留为可拖拽区域，不影响下方按钮操作。 */
        if (events->button_pressed && events->button == BTN_LEFT &&
            events->button_serial && events->pointer_y < 70)
            toy_window_move(window, events->button_serial);
        if (events->close_requested) break;
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
        } else if (screen == RASTERFALL_STARTUP_MANUAL_IP) {
            for (int i = 0; i < events->key_event_count; i++) {
                unsigned int key = events->key_events[i].key;
                if (!events->key_events[i].pressed) continue;
                if (key == KEY_ESC) {
                    screen = RASTERFALL_STARTUP_MAIN;
                    selected = 1;
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
            int up = toy_input_pressed(input, KEY_UP);
            int down = toy_input_pressed(input, KEY_DOWN);
            if ((up || down) && now >= nav_ready) {
                int limit = 5;
                selected += down ? 1 : -1;
                if (selected < 0) selected = limit - 1;
                if (selected >= limit) selected = 0;
                nav_ready = now + 160000;
            }
            if (toy_input_pressed(input, KEY_ESC)) {
                if (screen == RASTERFALL_STARTUP_MAIN) break;
                screen = RASTERFALL_STARTUP_MAIN;
                selected = 1;
            } else if (toy_input_pressed(input, KEY_ENTER)) {
                if (screen == RASTERFALL_STARTUP_MAIN) {
                    if (selected == 0) {
                        *net_mode = RASTERFALL_NET_HOST;
                        *public_room = 0;
                        return 1;
                    } else if (selected == 1 || selected == 2) {
                        screen = RASTERFALL_STARTUP_PUBLIC_ROOM;
                        room_text[0] = 0;
                    } else if (selected == 3) {
                        screen = RASTERFALL_STARTUP_MANUAL_IP;
                        address[0] = 0;
                        editing_port = 0;
                    } else if (selected == 4) break;
                }
            }
        }
        int ready = toy_window_begin_frame(window, &surface);
        if (ready < 0) break;
        if (ready > 0) {
            if (toy_renderer_begin(renderer, &surface, 0x10151D) < 0) break;
            draw_startup_menu(&surface, screen, selected, address, port_text,
                              editing_port, room_text, error);
            if (toy_window_present(window) < 0) break;
        }
        memset(input->key_pressed, 0, sizeof(input->key_pressed));
    }
    return 0;
}

static int wait_for_network_connection(struct toy_window *window,
                                       struct toy_renderer *renderer,
                                       struct toy_input *input,
                                       struct toy_window_events *events,
                                       struct rasterfall_net *net,
                                       const char *address, int port)
{
    long deadline = monotonic_us() + 6000000L;
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
                             int ex, int ey, int ez)
{
    struct rasterfall_effect_cue cue;
    memset(&cue, 0, sizeof(cue));
    cue.type = RASTERFALL_EFFECT_CUE_TRACER;
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
                         tracer_x, tracer_y, tracer_z);
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
                             tracer_x, tracer_y, tracer_z);
        }
    }
}

static void sync_network_fire_effects(const struct camera *viewer,
                                      const struct camera *remote,
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
        unsigned char event = TOY_GAME_EV_SHOOT;
        rasterfall_audio_play_events(audio, &event, 1);
    }
    if (ray_count < 0) ray_count = 0;
    if (ray_count > TOY_GAME_MAX_RAYS) ray_count = TOY_GAME_MAX_RAYS;
    /* Remote players are rendered in third person.  Their tracer must start
     * at the same side-mounted muzzle as the avatar, not at the observer's
     * first-person weapon offset (which sits visibly too high). */
    rasterfall_viewmodel_actor_muzzle(remote->x, remote->z,
                                      remote->sy, remote->cy, remote->y,
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
                         tracer_x, tracer_y, tracer_z);
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
    long last_time, accumulator = 0, fps_window_start, fps_elapsed;
    long prev_begin = 0, last_active = 0;   /* 帧间隔统计 */
    int running = 1, pointer_lock_requested = 0, paused = 1;
    int return_to_menu = 0;
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;
    int display_fps = 0, fps_window_frames = 0;
    int fire_edge = 0;
    int shove_edge = 0;
    int pointer_turn_pending = 0, pointer_pitch_pending = 0;
    long menu_nav_ready_us = 0;
    /* 按键按压边沿跨帧保留位：逻辑步（E/R/,/. 及切枪换弹）可能因
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
    char host_address[16];
    uint64_t seed;

    int logic_test = 0;
    int requested_net_mode = RASTERFALL_NET_OFF;
    int net_port = RASTERFALL_NET_DEFAULT_PORT;
    int public_room = 0, public_room_id = 0;
    const char *net_address = NULL;
    const char *startup_error = NULL;
    char selected_address[64];
    int auto_mode = 0;
    const char *dump_path = 0;
    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "--input-test") == 0) input_debug = 1;
        else if (strcmp(argv[arg], "--logic-test") == 0) logic_test = 1;
        else if (strcmp(argv[arg], "--host") == 0)
            requested_net_mode = RASTERFALL_NET_HOST;
        else if (strcmp(argv[arg], "--connect") == 0 && arg + 1 < argc) {
            requested_net_mode = RASTERFALL_NET_CLIENT;
            net_address = argv[++arg];
        } else if (strcmp(argv[arg], "--port") == 0 && arg + 1 < argc)
            net_port = parse_positive_int(argv[++arg], RASTERFALL_NET_DEFAULT_PORT);
        else if (strcmp(argv[arg], "--auto") == 0) auto_mode = 1;
        else if (strcmp(argv[arg], "--textures") == 0) textures_enabled = 1;
        else if (strcmp(argv[arg], "--no-textures") == 0) textures_enabled = 0;
        else if (strcmp(argv[arg], "--no-stats") == 0) stats_enabled = 0;
        else if (strcmp(argv[arg], "--texture-stats") == 0) texture_stats = 1;
        else if (strcmp(argv[arg], "--dump-frame") == 0 && arg + 1 < argc)
            dump_path = argv[++arg];
        else if (strcmp(argv[arg], "--frames") == 0 && arg + 1 < argc) {
            const char *p = argv[++arg];
            while (*p >= '0' && *p <= '9')
                frame_limit = frame_limit * 10 + (*p++ - '0');
        }
    }
    rasterfall_net_init(&net);
    strcpy(host_address, "127.0.0.1");
    if (rasterfall_session_load(&session, "rasterfall/assets/maps/rasterfall.map") < 0) {
        __fprintf(2, "rasterfall: cannot load map rasterfall/assets/maps/rasterfall.map\n");
        return 1;
    }
    render_context.session = &session;
    render_context.effects = &effects;
    render_context.net = &net;
    render_context.wall_texture = &wall_texture_view;
    render_context.textures_enabled = textures_enabled;
    rasterfall_render_bind(&render_context);
    rasterfall_render_bake_lightmap();
    rasterfall_effects_init(&effects);
    __printf("rasterfall: baked lightmap %dx%d\n", BAKED_LM_W, BAKED_LM_H);
    memset(&scene_texture, 0, sizeof(scene_texture));
    memset(&scene_texture_view, 0, sizeof(scene_texture_view));
    if (textures_enabled && toy_texture_load("rasterfall/assets/textures/wall.ttex",
                                              &scene_texture) == 0) {
        scene_texture_view.data = scene_texture.data;
        scene_texture_view.width = scene_texture.width;
        scene_texture_view.height = scene_texture.height;
        scene_texture_view.data_size = scene_texture.data_size;
        wall_texture_view.data = scene_texture_view.data;
        wall_texture_view.width = scene_texture_view.width;
        wall_texture_view.height = scene_texture_view.height;
        wall_texture_view.data_size = scene_texture_view.data_size;
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
        rasterfall_session_unload(&session);
        return result;
    }
    /* 服务器断开（WSLg 组合器/音频服务重启）时 socket 写会触发 SIGPIPE
     * 并默认杀死进程；忽略后写返回 EPIPE，由既有错误路径接管（音频线程
     * 静默停声、wayland 发送失败则主循环干净退出）。SIG_IGN 值为 1。 */
    tlibc_sigaction(SIGPIPE, (void (*)(int))1);
    toy_input_init(&input);
    memset(pending_key_edges, 0, sizeof(pending_key_edges));
    toy_renderer_init(&renderer);
    settings.mouse_level = 5;
    settings.keyboard_level = 5;
    pause_menu.selected = PAUSE_ITEM_RESUME;
    if (__getrandom(&seed, sizeof(seed), 0) < 0)
        seed = (uint64_t)monotonic_us();
    if (seed == 0) seed = 1;
    rasterfall_session_reset(&session, &camera, seed);
    window = toy_window_open("Rasterfall", 800, 450);
    if (!window) {
        __fprintf(2, "rasterfall: cannot create Wayland window\n");
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        rasterfall_net_close(&net);
        rasterfall_session_unload(&session);
        toy_renderer_destroy(&renderer);
        return 1;
    }
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
                                                &public_room_id, startup_error)) {
            toy_window_close(window);
            if (scene_texture.blob) toy_texture_unload(&scene_texture);
            rasterfall_session_unload(&session);
            toy_renderer_destroy(&renderer);
            return 0;
        }
        net_address = selected_address;
        startup_error = NULL;
    }
    if (requested_net_mode == RASTERFALL_NET_HOST && public_room) {
        struct camera peer_spawn;
        memcpy(&peer_spawn, &camera, sizeof(peer_spawn));
        peer_spawn.x += 350;
        if (rasterfall_net_public_host(&net, public_room_id, &peer_spawn) < 0) {
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
        struct camera peer_spawn;
        memcpy(&peer_spawn, &camera, sizeof(peer_spawn));
        peer_spawn.x += 350;
        if (rasterfall_net_host(&net, net_port, &peer_spawn) < 0) {
            __fprintf(2, "rasterfall: cannot host UDP port %d\n", net_port);
            toy_window_close(window);
            if (scene_texture.blob) toy_texture_unload(&scene_texture);
            rasterfall_session_unload(&session);
            toy_renderer_destroy(&renderer);
            return 1;
        }
        rasterfall_net_local_address(host_address, sizeof(host_address));
        __printf("rasterfall: hosting UDP port %d\n", net_port);
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
    __printf("rasterfall: pause menu uses arrows + Enter; mouse/arrows look, "
             "WASD moves, click/Space fire (hold for SMG), R reload, "
             "1/2 weapons, E interact, ,/. turn 90, Esc pauses/resumes\n");
    if (input_debug)
        __printf("rasterfall: input debug HUD enabled; test chords and focus changes\n");
    memset(&audio, 0, sizeof(audio));
    rasterfall_audio_load_assets(&audio);
    if (rasterfall_audio_start(&audio) < 0) {
        __printf("rasterfall: audio unavailable, playing silent\n");
    }
    last_time = monotonic_us();
    fps_window_start = last_time;
    rasterfall_perf_init(&stats);
    rasterfall_perf_init(&stats_total);
    while (running) {
        long now, elapsed, t_frame, t_stage;
        unsigned long prev_tris;
        int logic_steps = 0;
        int resumed = 0;
        int stage_pixels;
        int ready;
        unsigned char game_events[TOY_GAME_MAX_EVENTS];
        int game_event_count;
        toy_input_begin_frame(&input);
        /* 非阻塞收输入：present 后立刻开始下一帧 CPU 工作，组合器处理
         * 已提交缓冲的时间被渲染流水线掩盖（双缓冲）。 */
        if (toy_window_poll(window, &events, 0) < 0) break;
        toy_input_apply(&input, &events);
        rasterfall_net_poll(&net);
        rasterfall_net_update_connection(&net);
        if (net.mode == RASTERFALL_NET_CLIENT && net.remote_event_count > 0) {
            if (audio.running)
                rasterfall_audio_play_events(&audio, net.remote_events,
                                             net.remote_event_count);
            net.remote_event_count = 0;
        }
        if (net.mode == RASTERFALL_NET_CLIENT)
                rasterfall_net_reconcile_client(&net, &session, &camera);
        if (net.mode == RASTERFALL_NET_HOST && net.peer_known) {
            sync_network_fire_effects(&camera, &net.peer_camera,
                                      1,
                                      net.peer_slots[net.peer_current_slot].weapon,
                                      net.peer_fire_seq, net.peer_ray_count,
                                      net.peer_rays, &audio);
        } else if (net.mode == RASTERFALL_NET_CLIENT && net.players[0].active) {
            sync_network_fire_effects(&camera, &net.players[0].camera,
                                      0,
                                      net.players[0].weapon,
                                      net.players[0].fire_seq,
                                      net.players[0].ray_count,
                                      net.players[0].rays, NULL);
        }
        if (net.mode == RASTERFALL_NET_HOST) {
            for (int i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
                const struct rasterfall_net_remote *remote = &net.remotes[i];
                int weapon;
                if (!remote->active || !remote->connected) continue;
                weapon = remote->current_slot >= 0 &&
                         remote->current_slot < TOY_GAME_WEAPON_SLOTS ?
                         remote->slots[remote->current_slot].weapon : -1;
                sync_network_fire_effects(&camera, &remote->camera,
                                          remote->client_id, weapon,
                                          remote->fire_seq, remote->ray_count,
                                          remote->rays, &audio);
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
         * 才清除，因此不跑逻辑步的帧不会吞掉 E/R/,/. 等按键。 */
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
        if (paused && game.state == TOY_GAME_PLAYING) {
            int resume_requested = 0;
            /* 菜单导航使用独立节流；Wayland/键盘自动重复可能在一帧内
             * 送来多次边沿，不能让选项随帧率飞快滚动。 */
            int up = toy_input_pressed(&input, KEY_UP);
            int down = toy_input_pressed(&input, KEY_DOWN);
            if (up > 0 || down > 0) {
                long menu_now = monotonic_us();
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
                /* 无论本次是否因节流被接受，都消费这个边沿；释放后
                 * 再按才会产生下一次导航。 */
                input.key_pressed[KEY_UP] = 0;
                input.key_pressed[KEY_DOWN] = 0;
                pending_key_edges[KEY_UP] = 0;
                pending_key_edges[KEY_DOWN] = 0;
            }
            {
                int change = toy_input_pressed(&input, KEY_RIGHT) -
                             toy_input_pressed(&input, KEY_LEFT);
                if (change != 0) {
                    if (pause_menu.selected == PAUSE_ITEM_MOUSE)
                        settings.mouse_level = clampi(settings.mouse_level + change, 0, 15);
                    else if (pause_menu.selected == PAUSE_ITEM_KEYBOARD)
                        settings.keyboard_level = clampi(settings.keyboard_level + change, 0, 15);
                }
            }
            if (toy_input_pressed(&input, KEY_ENTER)) {
                if (pause_menu.selected == PAUSE_ITEM_RESUME)
                    resume_requested = 1;
                else if (pause_menu.selected == PAUSE_ITEM_MENU) {
                    return_to_menu = 1;
                    running = 0;
                }
            }
            if (toy_input_pressed(&input, KEY_ESC)) {
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
        if (!paused && !resumed && toy_input_pressed(&input, KEY_ESC)) {
            if (game.state == TOY_GAME_OVER || game.state == TOY_GAME_WON)
                running = 0;
            else {
                toy_window_set_pointer_lock(window, 0);
                pointer_lock_requested = 0;
                paused = 1;
                pointer_turn_pending = 0;
                pointer_pitch_pending = 0;
                pause_menu.selected = PAUSE_ITEM_RESUME;
                __printf("rasterfall: paused, pointer released\n");
            }
        }
        /* 射击输入：每帧只取一次边沿（恢复点击帧不开火） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_LEFT)
            fire_edge = 1;
        if (!paused && !resumed && toy_input_pressed(&input, KEY_SPACE))
            fire_edge = 1;
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
            if (!paused) {
                struct rasterfall_command command;
                rasterfall_effects_update(&effects, FIXED_STEP_US / 1000);
                if (net.mode == RASTERFALL_NET_CLIENT && net.spawn_pending) {
                    camera.x = net.peer_spawn.x;
                    camera.z = net.peer_spawn.z;
                    camera.sy = net.peer_spawn.sy;
                    camera.cy = net.peer_spawn.cy;
                    camera.pitch_sy = net.peer_spawn.pitch_sy;
                    camera.pitch_cy = net.peer_spawn.pitch_cy;
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
                    build_game_command(&command, &input, &settings, fire_edge,
                                       shove_edge, pointer_turn_pending,
                                       pointer_pitch_pending);
                    if (net.mode == RASTERFALL_NET_CLIENT)
                        rasterfall_session_step_client(&session, &camera,
                                                       &command,
                                                       FIXED_STEP_US / 1000);
                    else {
                        /* Feed the last authoritative remote position into
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
                        rasterfall_net_send_command(&net, &command, &camera);
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
                        rasterfall_net_send_command(&net, &command, &camera);
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
                    rasterfall_net_apply_remote(&net, &session, &camera);
                    rasterfall_net_capture_events(&net, &game);
                    /* Remote fire state is advanced by apply_remote().  The
                     * pre-step visual pass cannot observe that shot until
                     * the next frame; replay the guarded sequence here so
                     * the host hears it immediately and never loses a burst
                     * between snapshots. */
                    if (net.peer_known) {
                        sync_network_fire_effects(
                            &camera, &net.peer_camera, 1,
                            net.peer_current_slot >= 0 &&
                            net.peer_current_slot < TOY_GAME_WEAPON_SLOTS ?
                            net.peer_slots[net.peer_current_slot].weapon : -1,
                            net.peer_fire_seq, net.peer_ray_count,
                            net.peer_rays, &audio);
                    }
                    for (int i = 0; i < RASTERFALL_NET_REMOTE_MAX; i++) {
                        const struct rasterfall_net_remote *remote =
                            &net.remotes[i];
                        int weapon;
                        if (!remote->active || !remote->connected) continue;
                        weapon = remote->current_slot >= 0 &&
                                 remote->current_slot < TOY_GAME_WEAPON_SLOTS ?
                                 remote->slots[remote->current_slot].weapon : -1;
                        sync_network_fire_effects(&camera, &remote->camera,
                                                  remote->client_id, weapon,
                                                  remote->fire_seq,
                                                  remote->ray_count, remote->rays,
                                                  &audio);
                    }
                    if ((net.tick % 3) == 0)
                        rasterfall_net_send_snapshot(&net, &camera, &game,
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
        if (audio.running && game_event_count > 0)
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
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_BEGIN,
                           &t_stage, 0, 0);
            prev_tris = renderer.submitted_triangles;
            set_network_spectator_camera(&camera, &net);
            scene_pixels = rasterfall_render_scene(&renderer, &camera);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_SCENE, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            prev_tris = renderer.submitted_triangles;
            scene_pixels += rasterfall_render_enemies(&renderer, &camera);
            scene_pixels += rasterfall_render_ai_teammate(&renderer, &camera);
            scene_pixels += rasterfall_render_network_teammate(&renderer, &camera, &net);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_ENEMIES, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            /* 世界几何并行光栅化；弹道/粒子/枪模随后直接写屏覆盖 */
            prev_tris = (unsigned long)renderer.cmd_count;
            stage_pixels = toy_renderer_flush(&renderer);
            scene_pixels += stage_pixels;
            /* Sign lettering is a framebuffer overlay: the board must be
             * flushed first, otherwise its depth-tested face covers the text. */
            rasterfall_render_sign_text(&surface, &camera);
#if TOY_CONFIG_SHOW_MODEL_PATHS
            rasterfall_render_gallery_selection(&surface, &camera);
#endif
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_RASTER,
                           &t_stage, prev_tris, (unsigned long)stage_pixels);
            rasterfall_perf_add_raster(&stats, &stats_total, &renderer, prev_tris,
                            (unsigned long)stage_pixels);
            /* 直接写屏与第二次光栅化（拾取物）都归入 overlay 阶段 */
            prev_tris = renderer.submitted_triangles;
            stage_pixels = 0;
            stage_pixels += rasterfall_render_tracers(&renderer, &camera);
            stage_pixels += rasterfall_render_particles(&renderer, &camera);
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
            } else if (paused) {
                draw_pause_overlay(&surface, &pause_menu, &settings);
            } else {
                draw_crosshair(&surface, &game);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud, &net, host_address, net_port, &camera);
                    rasterfall_hud_render(&surface, display_fps, &hud);
                }
            }
            if (game.state == TOY_GAME_PLAYING && !paused) {
                stage_pixels += rasterfall_render_interactables(&renderer, &camera);
                /* 拾取物保持画在枪模之上的现状顺序 */
                stage_pixels += toy_renderer_flush(&renderer);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud, &net, host_address, net_port, &camera);
                    rasterfall_hud_draw_interact_prompt(&renderer, &hud);
                }
            }
            scene_pixels += stage_pixels;
            rasterfall_render_ai_teammate_name(&renderer, &camera);
            rasterfall_render_network_teammate_status(&renderer, &camera, &net);
            rasterfall_hud_damage_flash(&surface, &game);
            if (input_debug)
                draw_input_debug(&surface, &input,
                                 have_last_key ? last_key : 0,
                                 have_last_key ? last_key_pressed : 0,
                                 input_event_count);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_OVERLAY,
                           &t_stage, renderer.submitted_triangles - prev_tris,
                           (unsigned long)stage_pixels);
            if (toy_window_present(window) < 0) break;
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
        rasterfall_audio_stop(&audio);
        rasterfall_audio_unload_assets(&audio);
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
        memset(input.key_pressed, 0, sizeof(input.key_pressed));
        pointer_lock_requested = 0;
        goto startup_again;
    }
    if (stats_enabled && stats_total.frames > 0)
        rasterfall_perf_dump(&stats_total, "total");
    rasterfall_audio_stop(&audio);
    rasterfall_audio_unload_assets(&audio);
    if (scene_texture.blob) toy_texture_unload(&scene_texture);
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
