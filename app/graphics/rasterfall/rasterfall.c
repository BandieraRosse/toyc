/*
 * rasterfall — Toyc 软件渲染第一人称僵尸射击游戏
 *
 * 注释更新时间：2026-08-06
 *
 * 【程序定位】
 *   这是 Toyc 项目中的 Linux x86_64 / Wayland FPS 示例程序。玩家从起始
 *   安全室出发，穿过地图中的场景和刷怪区域，前往另一间安全室；核心玩法
 *   是移动、瞄准、射击、换弹、拾取武器以及应对普通尸群和可重复召唤的尸潮。
 *   程序使用 wl_shm 软件帧缓冲和项目自带的 Wayland 最小实现，不依赖 SDL、
 *   OpenGL 或 libc；游戏规则主要位于 lib/game，窗口、输入和渲染在本文件
 *   中协调完成。
 *
 * 【代码结构】
 *   1. 地图与规则：加载 assets/maps/rasterfall.map，复制地图碰撞箱、
 *      安全室、正式 spawn 区和交互物；toy_game 负责敌人 AI、碰撞、武器、
 *      弹道、波次、伤害和游戏状态。
 *   2. 输入与主循环：Wayland 事件先进入 toy_input，使用固定 16.667ms
 *      逻辑步；渲染帧和逻辑步解耦，并限制单帧最多推进 MAX_LOGIC_STEPS，
 *      避免窗口卡顿后逻辑无限追赶。
 *   3. 相机与控制：camera 保存位置、偏航/俯仰的 1024 定点 sin/cos；
 *      WASD 移动，鼠标和方向键连续视角控制，逗号/句号分别排队完成快速
 *      左右 90 度转向，而不是瞬时跳转。
 *   4. 渲染：软件光栅化场景、墙面纹理、地面、模型、敌人、武器视图模型、
 *      交互物、准星和 HUD；支持 --no-textures、--texture-stats、--dump-frame
 *      等调试/性能选项。
 *   5. 音频：启动时加载 assets/generated/sfx_*.tsnd；音频不可用时游戏仍
 *      可运行并静默降级。
 *
 * 【主要操作】
 *   鼠标左键：捕获指针并射击；Space：射击（按住可使 SMG 连发）
 *   WASD：移动；鼠标/方向键：视角；, / .：连续快速左转/右转 90 度
 *   R：换弹或在死亡/通关状态重新开始；1/2：切换武器槽
 *   E：与准星高亮的武器、弹药箱或召唤按钮互动
 *   Esc：暂停并释放指针；暂停菜单使用上下键选择、左右键调整灵敏度、
 *       Enter 确认。
 *
 * 【尸潮与地图约定】
 *   边界侧墙上的控制按钮均可重复使用：召唤按钮每次生成 15-20 个持续追踪
 *   玩家的敌人，空气墙按钮切换两段门控碰撞墙，警报按钮切换持续警报。
 *   警报开启后每秒从地图定义的 spawn 区生成 2-3 个敌人，直到再次关闭。
 *   生成位置不在按钮附近，而是直接从地图定义的 spawn 区
 *   随机选择 1-3 个区域，并避开玩家的最小距离；正式刷怪区统一来自
 *   map_spawn_zones/map_spawn_count，勿在本文件另造一套坐标。
 *   地图中的 pickup/button 坐标由 map 文件决定；按钮位于侧墙时，渲染代码
 *   需要同步处理其朝向和可交互高亮范围。
 *
 * 【构建与验证】
 *   make build/rasterfall       构建程序
 *   build/rasterfall --logic-test 运行无窗口逻辑回归测试
 *   build/rasterfall --frames N    在 Wayland 下运行有限帧数预览
 *   完整项目测试入口和工具链约束以仓库根目录 README.md、AGENTS.md 为准。
 *   修改本文件的输入、地图交互或公共渲染路径后，至少重新构建本目标并
 *   运行 --logic-test；若影响共享编译器/游戏规则代码，再扩大测试范围。
 *
 * 【实现约束】
 *   保持 freestanding，不引入宿主 libc；避免把 build/、tmp/ 或运行时生成物
 *   当作源码提交。Wayland/音频设备、容器 procfs 和只读文件系统可能造成
 *   环境相关失败，应与游戏逻辑或编译回归区分。
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
#include "math.h"

#define KEY_ESC   1
#define KEY_1     2
#define KEY_2     3
#define KEY_E     18
#define KEY_R     19
#define KEY_ENTER 28
#define KEY_W     17
#define KEY_A     30
#define KEY_S     31
#define KEY_D     32
#define KEY_SPACE 57
#define KEY_COMMA 51
#define KEY_DOT   52
#define KEY_UP    103
#define KEY_LEFT  105
#define KEY_RIGHT 106
#define KEY_DOWN  108
#define BTN_LEFT 0x110

#define FIXED_STEP_US 16667
#define MAX_FRAME_US 250000
#define MAX_LOGIC_STEPS 4
#define NEAR_Z 192
#define ENEMY_RENDER_DISTANCE 24000 /* 3x the old 8000-unit enemy cutoff */
#define PLAYER_RADIUS 180
#define MOVE_STEP 76
#define UV_ONE 65536
#define BAKED_LM_W 32
#define BAKED_LM_H 24
#define PITCH_LIMIT_SY 989        /* sin(75°) * 1024 */
#define PITCH_LIMIT_CY 265        /* cos(75°) * 1024 */

struct vec3 { int x, y, z; };
/* 朝向：sy/cy = 偏航 sin/cos，pitch_sy/pitch_cy = 俯仰 sin/cos（均 1024 定点）。 */
struct box { int minx, maxx, minz, maxz, height; uint32_t color; };
struct control_settings { int mouse_level, keyboard_level; };
struct pause_menu { int selected; };

#define PAUSE_ITEM_RESUME   0
#define PAUSE_ITEM_MOUSE    1
#define PAUSE_ITEM_KEYBOARD 2
#define PAUSE_ITEM_QUIT     3
#define PAUSE_ITEM_COUNT    4

#if 0
static const struct box obstacles[OBSTACLE_COUNT] = {
    {-1700, -700,  300, 1700, 1000, 0x755A47},
    {  600, 1800, -900,  100, 1450, 0x49677D},
    { 2300, 3200, 1800, 3000,  750, 0x64704B},
    /* start room */
    {-2000, -1800, -5700, -3800, 1300, 0x3F6751},
    { 1800,  2000, -5700, -3800, 1300, 0x3F6751},
    {-1800,  -600, -4000, -3800, 1300, 0x3F6751},
    {  600,  1800, -4000, -3800, 1300, 0x3F6751},
    /* exit room */
    {-2000, -1800,  3800,  5700, 1300, 0x477A58},
    { 1800,  2000,  3800,  5700, 1300, 0x477A58},
    {-1800,  -600,  3800,  4000, 1300, 0x477A58},
    {  600,  1800,  3800,  4000, 1300, 0x477A58}
};

/* 游戏世界（与 obstacles 同 xz 范围，交给 lib/game 做碰撞与遮挡） */
static const struct toy_game_box bounds[OBSTACLE_COUNT] = {
    {-1700, -700,  300, 1700},
    {  600, 1800, -900,  100},
    { 2300, 3200, 1800, 3000},
    {-2000, -1800, -5700, -3800},
    { 1800,  2000, -5700, -3800},
    {-1800,  -600, -4000, -3800},
    {  600,  1800, -4000, -3800},
    {-2000, -1800,  3800,  5700},
    { 1800,  2000,  3800,  5700},
    {-1800,  -600,  3800,  4000},
    {  600,  1800,  3800,  4000}
};

/* 经典闯关路线：南侧起点安全室，穿过主体区域后抵达北侧终点安全室。 */
static const struct toy_game_box safe_rooms[2] = {
    {-1800, 1800, -5700, -4000},
    {-1800, 1800,  4000,  5700}
};

/* 固定刷怪区域；渲染为红色地面，便于玩家识别危险区。 */
static const struct toy_game_box spawn_zones[3] = {
    {-5200, -3500, -2600, -800},
    { 3500,  5200,  -500, 1400},
    {-3300, -1500,  2100, 3600}
};

/* 踏入黄框后，右侧 spawn_zones[1] 在有限配额的警报尸潮中开启。 */
static const struct toy_game_box alarm_zone = {
    3300, 5200, -3000, -1500
};

#endif
static struct toy_map level_map;
static struct toy_game_box map_bounds[TOY_MAP_MAX_BOXES];
static struct toy_game_box map_safe_rooms[TOY_MAP_MAX_ZONES];
static struct toy_game_box map_spawn_zones[TOY_MAP_MAX_ZONES];
static int map_spawn_count;
static struct toy_game game;   /* 游戏规则状态（main / logic-test 共用） */
static int air_wall_enabled = 1;
static struct rasterfall_map_state map_ops;
typedef struct rasterfall_interactable interactable;
static struct rasterfall_interactable interactables[TOY_MAP_MAX_PICKUPS];
static int interactable_count;
static int manual_alarm_enabled;
static int manual_alarm_timer_ms;
static struct toy_texture_asset scene_texture;
static struct toy_texture_view scene_texture_view;
static struct toy_texture_view wall_texture_view;
static struct toy_texture_view *active_texture_view;
static int textures_enabled = 1;
static unsigned short baked_lightmap[BAKED_LM_W * BAKED_LM_H];

/* A deliberately tiny offline-style light bake.  The map is static, so this
 * is generated once when the level is loaded and then only sampled while
 * recording triangles.  Box proximity gives walls and cover a soft contact
 * shadow without a runtime shadow map. */
static void bake_static_lightmap(void)
{
    int x, z, i;
    for (z = 0; z < BAKED_LM_H; z++) for (x = 0; x < BAKED_LM_W; x++) {
        int wx = level_map.minx + (level_map.maxx - level_map.minx) * (x * 2 + 1) /
                 (BAKED_LM_W * 2);
        int wz = level_map.minz + (level_map.maxz - level_map.minz) * (z * 2 + 1) /
                 (BAKED_LM_H * 2);
        int light = 270 + (wx - level_map.minx) * 4 /
                    (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
        for (i = 0; i < level_map.box_count; i++) {
            const struct toy_map_box *b = &level_map.boxes[i];
            int dx = wx < b->minx ? b->minx - wx : wx > b->maxx ? wx - b->maxx : 0;
            int dz = wz < b->minz ? b->minz - wz : wz > b->maxz ? wz - b->maxz : 0;
            int dist = dx > dz ? dx : dz;
            if (!b->air && dist < 900)
                light -= (900 - dist) * 24 / 900;
        }
        /* Warm point light baked from the small lamp in the east corner. */
        {
            int ldx = wx - 4000, ldz = wz - 160;
            int ldist = (int)isqrt((long long)ldx * ldx +
                                   (long long)ldz * ldz);
            if (ldist < 2600)
                light += (2600 - ldist) * 26 / 2600;
        }
        if (light < 150) light = 150;
        if (light > 286) light = 286;
        baked_lightmap[z * BAKED_LM_W + x] = (unsigned short)light;
    }
}

static int baked_light_at(int x, int z)
{
    int ix = (x - level_map.minx) * BAKED_LM_W /
             (level_map.maxx - level_map.minx ? level_map.maxx - level_map.minx : 1);
    int iz = (z - level_map.minz) * BAKED_LM_H /
             (level_map.maxz - level_map.minz ? level_map.maxz - level_map.minz : 1);
    if (ix < 0) ix = 0;
    if (ix >= BAKED_LM_W) ix = BAKED_LM_W - 1;
    if (iz < 0) iz = 0;
    if (iz >= BAKED_LM_H) iz = BAKED_LM_H - 1;
    return baked_lightmap[iz * BAKED_LM_W + ix];
}

static int baked_fog_at(int distance)
{
    if (distance <= 12000) return 0;
    if (distance >= 24000) return 210;
    return (distance - 12000) * 210 / 12000;
}

static int world_distance(const struct camera *camera, int x, int z)
{
    long long dx = (long long)x - camera->x;
    long long dz = (long long)z - camera->z;
    return (int)isqrt(dx * dx + dz * dz);
}

/* Map floor paint is an authored area colour, not a lightmap preview. */
static int fixed_floor_lighting;

static void prepare_map_rules(void)
{
    rasterfall_map_bind(&map_ops, &level_map, map_bounds, map_safe_rooms,
                        map_spawn_zones, &map_spawn_count, &air_wall_enabled,
                        interactables, &interactable_count);
    rasterfall_map_prepare(&map_ops);
}

static void set_air_walls_enabled(int enabled)
{
    rasterfall_map_set_air_walls(&map_ops, enabled);
}

/* Only the distant room boundary uses the stylized wall texture. Gameplay
 * cover remains flat-shaded so texture sampling does not dominate the
 * software rasterizer. */

static void reset_interactables(void);

/* 开发者区召唤按钮：每次 E 互动都召唤 15-20 个持续追踪尸潮。
 * 尸潮实际使用地图中的 spawn 区，避免和场景装饰坐标产生两套定义。 */
#define HORDE_COUNT_MIN    15
#define HORDE_COUNT_MAX    20
#define HORDE_MIN_PLAYER_DIST 700
static int horde_banner_ms;   /* HUD 提示剩余显示时间 */
static const char *interaction_banner;
static int smooth_turn_remaining; /* 待完成的水平转向量（turn 单位） */

/* 新局和死亡重开必须走同一路径，避免 toy_game_init 清空世界配置。 */
static void reset_game(struct camera *camera, uint64_t seed)
{
    camera->x = level_map.start_x;
    camera->z = level_map.start_z;
    camera->sy = 0;
    camera->cy = 1024;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    toy_game_init(&game, seed);
    toy_game_set_world(&game, map_bounds, level_map.box_count, level_map.room_limit);
    toy_game_set_campaign(&game, map_safe_rooms, level_map.safe_count,
                          map_spawn_zones, map_spawn_count);
    toy_game_set_alarm(&game, level_map.has_alarm ? &level_map.alarm_zone : NULL,
                       level_map.has_alarm ? 1 : -1);
    game.px = camera->x;
    game.pz = camera->z;
    horde_banner_ms = 0;
    interaction_banner = NULL;
    manual_alarm_enabled = 0;
    manual_alarm_timer_ms = 1000;
    set_air_walls_enabled(1);
    smooth_turn_remaining = 0;
    reset_interactables();
}

/* ── 固定拾取点（主武器/弹药盒）：E 互动，准星对准时高亮。
 * 拾取物永远在场不消失；同武器再互动 = 补充弹药。 ─────────────── */

#define INTERACT_RANGE 1000

static int highlighted = -1;   /* 本帧准星对准的拾取物索引，-1=无 */

static void reset_interactables(void)
{
    rasterfall_map_reset_interactables(&map_ops);
}

static void fill_hud_state(struct rasterfall_hud_state *hud)
{
    hud->game = &game;
    hud->map = &level_map;
    hud->safe_rooms = map_safe_rooms;
    hud->interactables = interactables;
    hud->interactable_count = interactable_count;
    hud->highlighted = highlighted;
    hud->air_walls_enabled = air_wall_enabled;
    hud->manual_alarm_enabled = manual_alarm_enabled;
    hud->manual_alarm_timer_ms = manual_alarm_timer_ms;
    hud->horde_banner_ms = horde_banner_ms;
    hud->interaction_banner = interaction_banner;
}

static long monotonic_us(void)
{
    struct timespec now;
    if (__clock_gettime(CLOCK_MONOTONIC, &now) < 0) return 0;
    return now.tv_sec * 1000000L + now.tv_nsec / 1000;
}

static int clampi(int value, int low, int high)
{
    if (value < low) return low;
    if (value > high) return high;
    return value;
}

static void put_pixel(struct toy_surface *surface, int x, int y, uint32_t color)
{
    uint32_t *row = (uint32_t *)((unsigned char *)surface->pixels +
                                 y * surface->stride);
    row[x] = color;
}

static void copy_vec3(struct vec3 *out, const struct vec3 *in)
{
    out->x = in->x; out->y = in->y; out->z = in->z;
}

/* 偏航（绕 Y 轴）后再做俯仰（绕 X 轴）的真实 3D 旋转：
 * pitch 为正 = 上仰，视平线以下的点投影到屏幕下方。 */
static void world_to_view(const struct camera *camera, const struct vec3 *world,
                          struct vec3 *view)
{
    int dx = world->x - camera->x;
    int dz = world->z - camera->z;
    int wx = (dx * camera->cy - dz * camera->sy) / 1024;
    int wz = (dx * camera->sy + dz * camera->cy) / 1024;
    view->x = wx;
    view->y = (world->y * camera->pitch_cy - wz * camera->pitch_sy) / 1024;
    view->z = (world->y * camera->pitch_sy + wz * camera->pitch_cy) / 1024;
}

static void near_intersection(const struct vec3 *a, const struct vec3 *b,
                              struct vec3 *out)
{
    long numerator = NEAR_Z - a->z;
    long denominator = b->z - a->z;
    /* Toyc-safe widening: cast operands before subtraction so a negative
     * signed-int delta is sign-extended instead of zero-extended. */
    out->x = a->x + (int)(((long)b->x - (long)a->x) *
                          numerator / denominator);
    out->y = a->y + (int)(((long)b->y - (long)a->y) *
                          numerator / denominator);
    out->z = NEAR_Z;
}

static int clip_near(const struct vec3 *input, int count, struct vec3 *output)
{
    int out_count = 0;
    struct vec3 previous;
    copy_vec3(&previous, &input[count - 1]);
    int previous_inside = previous.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct vec3 current;
        copy_vec3(&current, &input[i]);
        int current_inside = current.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection(&previous, &current, &output[out_count++]);
        if (current_inside) copy_vec3(&output[out_count++], &current);
        copy_vec3(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

struct world_uv_vertex { struct vec3 p; int u, v; };

static void copy_world_uv(struct world_uv_vertex *out,
                          const struct world_uv_vertex *in)
{
    copy_vec3(&out->p, &in->p);
    out->u = in->u; out->v = in->v;
}

static void near_intersection_uv(const struct world_uv_vertex *a,
                                 const struct world_uv_vertex *b,
                                 struct world_uv_vertex *out)
{
    long numerator = NEAR_Z - a->p.z;
    long denominator = b->p.z - a->p.z;
    near_intersection(&a->p, &b->p, &out->p);
    out->u = a->u + (int)(((long)b->u - a->u) * numerator / denominator);
    out->v = a->v + (int)(((long)b->v - a->v) * numerator / denominator);
}

static int clip_near_uv(const struct world_uv_vertex *input, int count,
                        struct world_uv_vertex *output)
{
    int out_count = 0;
    struct world_uv_vertex previous;
    copy_world_uv(&previous, &input[count - 1]);
    int previous_inside = previous.p.z >= NEAR_Z;
    for (int i = 0; i < count; i++) {
        struct world_uv_vertex current;
        copy_world_uv(&current, &input[i]);
        int current_inside = current.p.z >= NEAR_Z;
        if (current_inside != previous_inside)
            near_intersection_uv(&previous, &current, &output[out_count++]);
        if (current_inside) copy_world_uv(&output[out_count++], &current);
        copy_world_uv(&previous, &current);
        previous_inside = current_inside;
    }
    return out_count;
}

static void project_vertex(const struct toy_surface *surface,
                           const struct vec3 *view,
                           struct toy_screen_vertex *screen)
{
    int focal = surface->width * 3 / 4;
    screen->x = surface->width / 2 + view->x * focal / view->z;
    screen->y = surface->height / 2 - view->y * focal / view->z;
    screen->z = view->z;
    /* 渲染器深度缓冲使用逆深度，纯色路径也读该字段；必须在投影时填好。 */
    screen->inv_z = (long)1048576 / view->z;
    screen->light = 256;
    screen->fog = 0;
}

static void project_uv_vertex(const struct toy_surface *surface,
                              const struct vec3 *view,
                              int u, int v,
                              struct toy_screen_vertex *screen)
{
    project_vertex(surface, view, screen);
    screen->u = u; screen->v = v;
    screen->inv_z = (long)1048576 / view->z;
    screen->u_over_z = (long)u * 1048576L / view->z;
    screen->v_over_z = (long)v * 1048576L / view->z;
}

static int draw_world_triangle(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct vec3 *a, const struct vec3 *b,
                               const struct vec3 *c,
                               uint32_t color)
{
    struct vec3 input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, a, &input[0]);
    world_to_view(camera, b, &input[1]);
    world_to_view(camera, c, &input[2]);
    count = clip_near(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        int area;
        project_vertex(&renderer->surface, &clipped[0], &sa);
        project_vertex(&renderer->surface, &clipped[i], &sb);
        project_vertex(&renderer->surface, &clipped[i + 1], &sc);
        /* Projected coordinates are bounded here, so 32-bit area is safe and
         * avoids Toyc's signed int-to-long promotion bug. */
        area = (sc.x - sa.x) * (sb.y - sa.y) -
               (sc.y - sa.y) * (sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            swap.x = sb.x; swap.y = sb.y; swap.z = sb.z;
            swap.inv_z = sb.inv_z;
            sb.x = sc.x; sb.y = sc.y; sb.z = sc.z;
            sb.inv_z = sc.inv_z;
            sc.x = swap.x; sc.y = swap.y; sc.z = swap.z;
            sc.inv_z = swap.inv_z;
        }
        int center_x = (a->x + b->x + c->x) / 3;
        int center_z = (a->z + b->z + c->z) / 3;
        int light = fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = fixed_floor_lighting ? 0 :
                  baked_fog_at(world_distance(camera, center_x, center_z));
        /* 区域涂色（fixed_floor_lighting）与地砖仅差 6 个世界单位，掠射角下
         * 插值深度误差会盖过真实差值导致 z-fight；涂色按覆盖层绘制，
         * 依赖"地砖先画、墙后画"的记录顺序保证遮挡正确。 */
        if (fixed_floor_lighting)
            drawn += toy_renderer_triangle_lit_overlay(renderer, &sa, &sb, &sc,
                                                       color, light, fog);
        else
            drawn += toy_renderer_triangle_lit(renderer, &sa, &sb, &sc,
                                               color, light, fog);
    }
    return drawn;
}

static int draw_world_triangle_tex(struct toy_renderer *renderer,
                                    const struct camera *camera,
                                    const struct world_uv_vertex *a,
                                    const struct world_uv_vertex *b,
                                    const struct world_uv_vertex *c)
{
    struct world_uv_vertex input[3], clipped[4];
    int count, drawn = 0;
    world_to_view(camera, &a->p, &input[0].p);
    world_to_view(camera, &b->p, &input[1].p);
    world_to_view(camera, &c->p, &input[2].p);
    input[0].u = a->u; input[0].v = a->v;
    input[1].u = b->u; input[1].v = b->v;
    input[2].u = c->u; input[2].v = c->v;
    count = clip_near_uv(input, 3, clipped);
    for (int i = 1; i + 1 < count; i++) {
        struct toy_screen_vertex sa, sb, sc;
        int area;
        project_uv_vertex(&renderer->surface, &clipped[0].p,
                          clipped[0].u, clipped[0].v, &sa);
        project_uv_vertex(&renderer->surface, &clipped[i].p,
                          clipped[i].u, clipped[i].v, &sb);
        project_uv_vertex(&renderer->surface, &clipped[i + 1].p,
                          clipped[i + 1].u, clipped[i + 1].v, &sc);
        area = (sc.x - sa.x) * (sb.y - sa.y) -
               (sc.y - sa.y) * (sb.x - sa.x);
        if (area >= 0) {
            struct toy_screen_vertex swap;
            swap.x=sb.x; swap.y=sb.y; swap.z=sb.z;
            swap.u=sb.u; swap.v=sb.v; swap.inv_z=sb.inv_z;
            swap.u_over_z=sb.u_over_z; swap.v_over_z=sb.v_over_z;
            sb.x=sc.x; sb.y=sc.y; sb.z=sc.z;
            sb.u=sc.u; sb.v=sc.v; sb.inv_z=sc.inv_z;
            sb.u_over_z=sc.u_over_z; sb.v_over_z=sc.v_over_z;
            sc.x=swap.x; sc.y=swap.y; sc.z=swap.z;
            sc.u=swap.u; sc.v=swap.v; sc.inv_z=swap.inv_z;
            sc.u_over_z=swap.u_over_z; sc.v_over_z=swap.v_over_z;
        }
        int center_x = (a->p.x + b->p.x + c->p.x) / 3;
        int center_z = (a->p.z + b->p.z + c->p.z) / 3;
        int light = fixed_floor_lighting ? 256 : baked_light_at(center_x, center_z);
        int fog = fixed_floor_lighting ? 0 : baked_fog_at(world_distance(camera, center_x, center_z));
        sa.light = sb.light = sc.light = light;
        sa.fog = sb.fog = sc.fog = fog;
        drawn += toy_renderer_triangle_textured_lit(renderer, &sa, &sb, &sc,
                                                     active_texture_view, 1,
                                                     0xFF202020U, light, fog);
    }
    return drawn;
}

static int draw_quad_tex(struct toy_renderer *renderer,
                         const struct camera *camera,
                         const struct world_uv_vertex *a,
                         const struct world_uv_vertex *b,
                         const struct world_uv_vertex *c,
                         const struct world_uv_vertex *d,
                         uint32_t color)
{
    if (!textures_enabled)
        return draw_world_triangle(renderer, camera, &a->p, &b->p, &c->p, color) +
               draw_world_triangle(renderer, camera, &a->p, &c->p, &d->p, color);
    return draw_world_triangle_tex(renderer, camera, a, b, c) +
           draw_world_triangle_tex(renderer, camera, a, c, d);
}

static int draw_position_quad_tex(struct toy_renderer *renderer,
                                  const struct camera *camera,
                                  const struct vec3 *a, const struct vec3 *b,
                                  const struct vec3 *c, const struct vec3 *d,
                                  int u1, int v1, uint32_t color)
{
    struct world_uv_vertex wa, wb, wc, wd;
    copy_vec3(&wa.p, a); copy_vec3(&wb.p, b);
    copy_vec3(&wc.p, c); copy_vec3(&wd.p, d);
    wa.u=0; wa.v=0; wb.u=u1; wb.v=0;
    wc.u=u1; wc.v=v1; wd.u=0; wd.v=v1;
    return draw_quad_tex(renderer, camera, &wa, &wb, &wc, &wd, color);
}

static int draw_quad(struct toy_renderer *renderer, const struct camera *camera,
                     const struct vec3 *a, const struct vec3 *b,
                     const struct vec3 *c, const struct vec3 *d,
                     uint32_t color)
{
    return draw_world_triangle(renderer, camera, a, b, c, color) +
           draw_world_triangle(renderer, camera, a, c, d, color);
}

static int draw_floor_zone(struct toy_renderer *renderer,
                           const struct camera *camera,
                           const struct toy_game_box *zone, uint32_t color)
{
    struct vec3 a, b, c, d;
    a.x = zone->minx; a.y = -894; a.z = zone->minz;
    b.x = zone->maxx; b.y = -894; b.z = zone->minz;
    c.x = zone->maxx; c.y = -894; c.z = zone->maxz;
    d.x = zone->minx; d.y = -894; d.z = zone->maxz;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

static int draw_floor_border(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_box *zone, int width,
                             uint32_t color)
{
    struct toy_game_box edge;
    int pixels = 0;
    edge.minx = zone->minx; edge.maxx = zone->maxx;
    edge.minz = zone->minz; edge.maxz = zone->minz + width;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minz = zone->maxz - width; edge.maxz = zone->maxz;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minx = zone->minx; edge.maxx = zone->minx + width;
    edge.minz = zone->minz + width; edge.maxz = zone->maxz - width;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    edge.minx = zone->maxx - width; edge.maxx = zone->maxx;
    pixels += draw_floor_zone(renderer, camera, &edge, color);
    return pixels;
}

static void draw_world_label(struct toy_renderer *renderer,
                             const struct camera *camera,
                             const struct toy_game_box *zone,
                             const char *label, uint32_t color)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int width = (int)strlen(label) * FB_FONT_W;
    world.x = (zone->minx + zone->maxx) / 2;
    world.y = -650;
    world.z = (zone->minz + zone->maxz) / 2;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    screen.x -= width / 2;
    if (screen.x < 0 || screen.x + width >= renderer->surface.width ||
        screen.y < 0 || screen.y + FB_FONT_H >= renderer->surface.height) return;
    fb_draw_string((unsigned char *)renderer->surface.pixels,
                   screen.x, screen.y, label, color, renderer->surface.stride);
}

static int render_block_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);
static int render_round_enemy(struct toy_renderer *, const struct camera *,
                              const struct toy_game_enemy *, int, uint32_t);

static int draw_box(struct toy_renderer *renderer, const struct camera *camera,
                    const struct box *box)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = box->minx; a.y = -900; a.z = box->minz;
    b.x = box->maxx; b.y = -900; b.z = box->minz;
    c.x = box->maxx; c.y = -900; c.z = box->maxz;
    d.x = box->minx; d.y = -900; d.z = box->maxz;
    e.x = a.x; e.y = box->height; e.z = a.z;
    f.x = b.x; f.y = box->height; f.z = b.z;
    g.x = c.x; g.y = box->height; g.z = c.z;
    h.x = d.x; h.y = box->height; h.z = d.z;
    pixels += draw_quad(renderer,camera,&a,&b,&f,&e,box->color);
    pixels += draw_quad(renderer,camera,&b,&c,&g,&f,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&c,&d,&h,&g,box->color);
    pixels += draw_quad(renderer,camera,&d,&a,&e,&h,box->color + 0x080808);
    pixels += draw_quad(renderer,camera,&e,&f,&g,&h,box->color + 0x181818);
    return pixels;
}

/* 敌人模型使用任意底面高度的盒体，而场景障碍物仍走上面的落地盒体。 */
static int draw_cuboid(struct toy_renderer *renderer, const struct camera *camera,
                       int minx, int maxx, int miny, int maxy,
                       int minz, int maxz, uint32_t color)
{
    struct vec3 a, b, c, d, e, f, g, h;
    int pixels = 0;
    a.x = minx; a.y = miny; a.z = minz;
    b.x = maxx; b.y = miny; b.z = minz;
    c.x = maxx; c.y = miny; c.z = maxz;
    d.x = minx; d.y = miny; d.z = maxz;
    e.x = minx; e.y = maxy; e.z = minz;
    f.x = maxx; f.y = maxy; f.z = minz;
    g.x = maxx; g.y = maxy; g.z = maxz;
    h.x = minx; h.y = maxy; h.z = maxz;
    pixels += draw_quad(renderer, camera, &a, &b, &f, &e, color);
    pixels += draw_quad(renderer, camera, &b, &c, &g, &f, color + 0x080808);
    pixels += draw_quad(renderer, camera, &c, &d, &h, &g, color);
    pixels += draw_quad(renderer, camera, &d, &a, &e, &h, color + 0x080808);
    pixels += draw_quad(renderer, camera, &e, &f, &g, &h, color + 0x181818);
    return pixels;
}

static const int circle_x[8] = {1024, 724, 0, -724, -1024, -724, 0, 724};
static const int circle_z[8] = {0, 724, 1024, 724, 0, -724, -1024, -724};

static int draw_cylinder(struct toy_renderer *renderer,
                         const struct camera *camera, int x, int z,
                         int radius, int bottom, int top, uint32_t color)
{
    int pixels = 0;
    struct vec3 lo[8], hi[8], cap;
    cap.x = x; cap.y = top; cap.z = z;
    for (int i = 0; i < 8; i++) {
        lo[i].x = x + circle_x[i] * radius / 1024;
        lo[i].y = bottom;
        lo[i].z = z + circle_z[i] * radius / 1024;
        hi[i].x = lo[i].x; hi[i].y = top; hi[i].z = lo[i].z;
    }
    for (int i = 0; i < 8; i++) {
        int next = (i + 1) & 7;
        uint32_t shade = color + ((i & 3) * 0x030303);
        pixels += draw_quad(renderer, camera, &lo[i], &lo[next],
                            &hi[next], &hi[i], shade);
        pixels += draw_world_triangle(renderer, camera, &cap,
                                      &hi[i], &hi[next], color + 0x181818);
    }
    return pixels;
}

/* 三圈八边面组成的低多边形椭圆头。 */
static int draw_ellipsoid_head(struct toy_renderer *renderer,
                               const struct camera *camera, int x, int z,
                               int center_y, int rx, int ry, uint32_t color)
{
    static const int ring_r[5] = {0, 724, 1024, 724, 0};
    static const int ring_y[5] = {-1024, -724, 0, 724, 1024};
    struct vec3 ring[5][8];
    int pixels = 0;
    for (int r = 0; r < 5; r++) {
        for (int i = 0; i < 8; i++) {
            ring[r][i].x = x + circle_x[i] * rx * ring_r[r] / 1048576;
            ring[r][i].y = center_y + ring_y[r] * ry / 1024;
            ring[r][i].z = z + circle_z[i] * rx * ring_r[r] / 1048576;
        }
    }
    for (int r = 0; r < 4; r++) {
        for (int i = 0; i < 8; i++) {
            int next = (i + 1) & 7;
            pixels += draw_quad(renderer, camera, &ring[r][i], &ring[r][next],
                                &ring[r + 1][next], &ring[r + 1][i],
                                color + ((i & 3) * 0x030303));
        }
    }
    return pixels;
}

/* 沿敌人当前朝向在头部平面画小矩形，转身过程因此清晰可见。 */
static int draw_face_rect(struct toy_renderer *renderer,
                          const struct camera *camera, int x, int z, int radius,
                          int face_x, int face_z, int h0, int h1,
                          int y0, int y1, uint32_t color)
{
    struct vec3 a, b, c, d;
    int cx = x + face_x * (radius + 3) / 1024;
    int cz = z + face_z * (radius + 3) / 1024;
    a.x = cx - face_z * h0 / 1024; a.y = y0;
    a.z = cz + face_x * h0 / 1024;
    b.x = cx - face_z * h1 / 1024; b.y = y0;
    b.z = cz + face_x * h1 / 1024;
    c.x = b.x; c.y = y1; c.z = b.z;
    d.x = a.x; d.y = y1; d.z = a.z;
    return draw_quad(renderer, camera, &a, &b, &c, &d, color);
}

/* ── 可交互拾取物渲染：桌上武器与弹药盒（准星对准时整体提亮） ── */

static uint32_t highlight_tint(uint32_t color, int on)
{
    return on ? color + 0x383838 : color;
}

/* 平放的 SMG：机匣 + 枪管 + 弹匣 + 木托 + 照门，枪口朝 +z */
static int render_smg(struct toy_renderer *renderer, const struct camera *camera,
                      int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 35, x + 35, y - 13, y + 13,
                          z - 60, z + 60, highlight_tint(0x3B4148, on));
    pixels += draw_cuboid(renderer, camera, x - 14, x + 14, y - 2, y + 10,
                          z + 60, z + 150, highlight_tint(0x2F343B, on));
    pixels += draw_cuboid(renderer, camera, x - 20, x + 20, y - 48, y - 16,
                          z - 20, z + 10, highlight_tint(0x4A4438, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 9, y + 7,
                          z - 118, z - 60, highlight_tint(0x6B4A30, on));
    pixels += draw_cuboid(renderer, camera, x - 3, x + 3, y + 13, y + 21,
                          z - 15, z + 15, highlight_tint(0x2A2E34, on));
    return pixels;
}

/* 平放的霰弹枪：机匣 + 长枪管 + 护木 + 木托，枪口朝 +z */
static int render_shotgun(struct toy_renderer *renderer, const struct camera *camera,
                          int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 32, x + 32, y - 13, y + 13,
                          z - 45, z + 45, highlight_tint(0x46505A, on));
    pixels += draw_cuboid(renderer, camera, x - 11, x + 11, y - 2, y + 10,
                          z + 45, z + 150, highlight_tint(0x3A434D, on));
    pixels += draw_cuboid(renderer, camera, x - 17, x + 17, y - 19, y - 4,
                          z + 45, z + 100, highlight_tint(0x2C3138, on));
    pixels += draw_cuboid(renderer, camera, x - 24, x + 24, y - 8, y + 8,
                          z - 100, z - 45, highlight_tint(0x6B4A30, on));
    return pixels;
}

/* 弹药盒：橄榄绿箱体 + 浅色箱盖 */
static int render_ammo_box(struct toy_renderer *renderer, const struct camera *camera,
                           int x, int y, int z, int on)
{
    int pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y - 40, y + 40,
                          z - 80, z + 80, highlight_tint(0x555F3F, on));
    pixels += draw_cuboid(renderer, camera, x - 60, x + 60, y + 16, y + 40,
                          z - 80, z + 80, highlight_tint(0x6A7550, on));
    return pixels;
}

/* 召唤按钮：从墙面凸出的暗色面板 + 正面 LED。每次都可再次触发；
 * 准星对准时面板整体提亮。 */
static int render_button(struct toy_renderer *renderer, const struct camera *camera,
                         int x, int y, int z, int on, int side_wall)
{
    struct vec3 a, b, c, d;
    int pixels = 0;
    if (side_wall == 1) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x + 55, x + 60, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 西侧墙按钮朝向房间（+x） */
        a.x = x + 61; a.y = y - 22; a.z = z - 24;
        b.x = x + 61; b.y = y + 22; b.z = z - 24;
        c.x = x + 61; c.y = y + 22; c.z = z + 24;
        d.x = x + 61; d.y = y - 22; d.z = z + 24;
    } else if (side_wall == 2) {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 60, x - 55, y - 45, y + 45,
                              z - 45, z + 45, highlight_tint(0x1E2229, on));
        /* 东侧墙按钮朝向房间（-x） */
        a.x = x - 61; a.y = y - 22; a.z = z + 24;
        b.x = x - 61; b.y = y + 22; b.z = z + 24;
        c.x = x - 61; c.y = y + 22; c.z = z - 24;
        d.x = x - 61; d.y = y - 22; d.z = z - 24;
    } else {
        pixels += draw_cuboid(renderer, camera, x - 55, x + 55, y - 55, y + 55,
                              z - 45, z + 55, highlight_tint(0x2E333B, on));
        pixels += draw_cuboid(renderer, camera, x - 45, x + 45, y - 45, y + 45,
                              z + 55, z + 60, highlight_tint(0x1E2229, on));
        /* 正面 LED 小方块，面向房间（+z） */
        a.x = x - 24; a.y = y - 22; a.z = z + 61;
        b.x = x + 24; b.y = y - 22; b.z = z + 61;
        c.x = x + 24; c.y = y + 22; c.z = z + 61;
        d.x = x - 24; d.y = y + 22; d.z = z + 61;
    }
    pixels += draw_quad(renderer, camera, &a, &b, &c, &d,
                        0xFF3030);
    return pixels;
}

/* 距离 + 朝向锥判定高亮目标。拾取物（桌上武器/地面弹药盒）都在视平线
 * 以下，屏幕投影天然偏低，不能用中心像素窗口；改在世界空间按水平朝向
 * 放宽到约 40° 半角，垂直方向不限，取范围内最近者。 */
#define INTERACT_AIM_CONE 784   /* cos(≈38°)，1024 定点 */

static int compute_highlight(const struct camera *camera)
{
    int i, best = -1;
    long best_d2 = 0;
    for (i = 0; i < interactable_count; i++) {
        const interactable *it = &interactables[i];
        long dx, dz, d2, dist, dot;
        dx = it->x - camera->x;
        dz = it->z - camera->z;
        d2 = dx * dx + dz * dz;
        if (d2 > (long)INTERACT_RANGE * INTERACT_RANGE || d2 == 0) continue;
        dist = (long)isqrt(d2);
        if (dist <= 0) continue;
        dot = dx * camera->sy + dz * camera->cy;
        if (dot < dist * INTERACT_AIM_CONE) continue;
        if (best < 0 || d2 < best_d2) {
            best = i;
            best_d2 = d2;
        }
    }
    return best;
}

static int render_interactables(struct toy_renderer *renderer,
                                const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < interactable_count; i++) {
        const interactable *it = &interactables[i];
        int on = i == highlighted;
        if (it->kind == TOY_MAP_PICKUP_SMG)
            pixels += render_smg(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_SHOTGUN)
            pixels += render_shotgun(renderer, camera, it->x, it->y, it->z, on);
        else if (it->kind == TOY_MAP_PICKUP_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_AIR_BUTTON ||
                 it->kind == TOY_MAP_PICKUP_ALARM_BUTTON)
            pixels += render_button(renderer, camera, it->x, it->y, it->z, on,
                                    it->x < -10000 ? 1 : it->x > 10000 ? 2 : 0);
        else
            pixels += render_ammo_box(renderer, camera, it->x, it->y, it->z, on);
    }
    return pixels;
}

/* ── 第一人称武器模型：视图空间盒体固定在镜头右下方，开火后坐后移 ── */

static struct rasterfall_effects effects;

/* 视图空间逆变换：枪口偏移 → 世界坐标（供弹道起点与枪口粒子） */
static void view_to_world(const struct camera *camera, const struct vec3 *view,
                          struct vec3 *world)
{
    int vx = view->x, vy = view->y, vz = view->z;
    int wy = (vy * camera->pitch_cy + vz * camera->pitch_sy) / 1024;
    int wz = (-vy * camera->pitch_sy + vz * camera->pitch_cy) / 1024;
    world->x = camera->x + (vx * camera->cy + wz * camera->sy) / 1024;
    world->z = camera->z + (-vx * camera->sy + wz * camera->cy) / 1024;
    world->y = wy;
}

static void fill_rect(struct toy_surface *surface, int x, int y,
                      int width, int height, uint32_t color);
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den);

/* E 键互动：拾取主武器（替换槽 0 并切出），同武器 = 补充弹药；
 * 弹药盒补满备弹；召唤按钮每次互动触发 15-20 个追踪尸潮。
 * 拾取点是固定的，互动后保留在场。 */
static void interact_current(interactable *it)
{
    if (it->kind == TOY_MAP_PICKUP_BUTTON) {
        int n;
        horde_banner_ms = 3500;
        interaction_banner = "HORDE SUMMONED - THEY WILL FIND YOU";
        n = toy_game_spawn_horde(&game, HORDE_COUNT_MIN, HORDE_COUNT_MAX,
                                 map_spawn_zones, map_spawn_count,
                                 HORDE_MIN_PLAYER_DIST);
        __printf("rasterfall: horde summoned %d tracking enemies\n", n);
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON) {
        set_air_walls_enabled(!air_wall_enabled);
        horde_banner_ms = 1800;
        interaction_banner = air_wall_enabled ?
            "AIR WALLS ENABLED" : "AIR WALLS DISABLED";
        __printf("rasterfall: air walls %s\n", air_wall_enabled ? "enabled" : "disabled");
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON) {
        manual_alarm_enabled = !manual_alarm_enabled;
        manual_alarm_timer_ms = 1000;
        horde_banner_ms = 1800;
        interaction_banner = manual_alarm_enabled ?
            "ALARM ENABLED - 2-3 ENEMIES EACH SECOND" : "ALARM DISABLED";
        __printf("rasterfall: alarm %s\n", manual_alarm_enabled ? "enabled" : "disabled");
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_AMMO) {
        toy_game_refill_ammo(&game);
        return;
    }
    toy_game_equip_weapon(&game, it->kind == TOY_MAP_PICKUP_SMG ?
                          TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN);
}

static void update_manual_alarm(int dt_ms)
{
    int n;
    if (!manual_alarm_enabled || game.state != TOY_GAME_PLAYING) return;
    manual_alarm_timer_ms -= dt_ms;
    if (manual_alarm_timer_ms > 0) return;
    manual_alarm_timer_ms += 1000;
    n = toy_game_spawn_horde(&game, 2, 3, map_spawn_zones, map_spawn_count,
                             HORDE_MIN_PLAYER_DIST);
    if (n > 0) __printf("rasterfall: alarm spawned %d enemies\n", n);
}

static int render_scene(struct toy_renderer *renderer, const struct camera *camera)
{
    int pixels = 0;
    struct vec3 a, b, c, d;
    /* 自由俯仰下先铺天空/地面：地平线由俯仰角决定，墙面与地板随后覆盖 */
    rasterfall_sky_draw(&renderer->surface, camera);
    for (int z = level_map.minz; z < level_map.maxz; z += 1000) {
        for (int x = level_map.minx; x < level_map.maxx; x += 1000) {
            uint32_t color = (((x + z) / 1000) & 1) ? 0x30343A : 0x272B31;
            a.x = x;        a.y = -900; a.z = z;
            b.x = x + 1000; b.y = -900; b.z = z;
            c.x = x + 1000; c.y = -900; c.z = z + 1000;
            d.x = x;        d.y = -900; d.z = z + 1000;
            pixels += draw_quad(renderer, camera, &a, &b, &c, &d, color);
        }
    }
    active_texture_view = &wall_texture_view;
    for (int i=0; i<level_map.draw_count; i++) {
        struct toy_map_draw *x=&level_map.draw[i];
        if (x->type==TOY_MAP_DRAW_FLOOR) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d};
            fixed_floor_lighting = 1;
            pixels+=draw_floor_zone(renderer,camera,&zone,x->color);
            fixed_floor_lighting = 0;
        } else if (x->type==TOY_MAP_DRAW_BORDER) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d};
            /* 边框与涂色同层：同样走无深度覆盖绘制，颜色也是作者指定的
             * 固定色（不参与烘焙光照/雾）。 */
            fixed_floor_lighting = 1;
            pixels+=draw_floor_border(renderer,camera,&zone,x->e,x->color);
            fixed_floor_lighting = 0;
        } else if (x->type==TOY_MAP_DRAW_WALL) {
            if (x->c==x->d) { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c; }
            else { a.x=x->a;a.y=-900;a.z=x->c;b.x=x->a;b.y=-900;b.z=x->d;c.x=x->a;c.y=x->e;c.z=x->d;d.x=x->a;d.y=x->e;d.z=x->c; }
            pixels+=draw_quad(renderer,camera,&a,&b,&c,&d,x->color);
        } else if (x->type==TOY_MAP_DRAW_MODEL) {
            if (x->style) {
                struct toy_game_enemy model;
                memset(&model,0,sizeof(model)); model.active=1;
                model.x=(x->a+x->b)/2; model.z=(x->c+x->d)/2; model.dir_z=-1024;
                if (x->style==1) pixels+=render_block_enemy(renderer,camera,&model,1000,x->color);
                else pixels+=render_round_enemy(renderer,camera,&model,1000,x->color);
            } else {
                struct box model={x->a,x->b,x->c,x->d,x->f,x->color};
                pixels+=draw_cuboid(renderer,camera,model.minx,model.maxx,x->e,x->f,model.minz,model.maxz,model.color);
            }
        } else if (x->type==TOY_MAP_DRAW_TEXTURE) {
            a.x=x->a;a.y=-900;a.z=x->c;b.x=x->b;b.y=-900;b.z=x->c;c.x=x->b;c.y=x->e;c.z=x->c;d.x=x->a;d.y=x->e;d.z=x->c;
            pixels+=draw_position_quad_tex(renderer,camera,&a,&b,&c,&d,x->texture_u*UV_ONE,x->texture_v*UV_ONE,x->color);
        } else if (x->type==TOY_MAP_DRAW_LABEL) {
            struct toy_game_box zone={x->a,x->b,x->c,x->d}; draw_world_label(renderer,camera,&zone,x->text,x->color);
        }
    }
    for (int i=0; i<level_map.box_count; i++) if (!level_map.boxes[i].air) {
        struct box obstacle={level_map.boxes[i].minx,level_map.boxes[i].maxx,level_map.boxes[i].minz,level_map.boxes[i].maxz,level_map.boxes[i].height,level_map.boxes[i].color};
        pixels+=draw_box(renderer,camera,&obstacle);
    }
    return pixels;
}

static int position_blocked(int x, int z)
{
    return toy_game_position_blocked(&game, x, z, PLAYER_RADIUS);
}

static void update_player(struct camera *camera, const struct toy_input *input)
{
    int forward = toy_input_down(input, KEY_W) - toy_input_down(input, KEY_S);
    int strafe = toy_input_down(input, KEY_D) - toy_input_down(input, KEY_A);
    int dx = (camera->sy * forward + camera->cy * strafe) * MOVE_STEP / 1024;
    int dz = (camera->cy * forward - camera->sy * strafe) * MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!position_blocked(next_x, camera->z)) camera->x = next_x;
    if (!position_blocked(camera->x, next_z)) camera->z = next_z;
}

static void rotate_camera(struct camera *camera, int turn, int pitch)
{
    int old_sy = camera->sy;
    int old_psy = camera->pitch_sy;
    long long length;
    camera->sy = (old_sy * 1024 + camera->cy * turn) / 1024;
    camera->cy = (camera->cy * 1024 - old_sy * turn) / 1024;
    length = isqrt((long long)camera->sy * camera->sy +
                   (long long)camera->cy * camera->cy);
    if (length > 0) {
        camera->sy = (int)((long long)camera->sy * 1024 / length);
        camera->cy = (int)((long long)camera->cy * 1024 / length);
    }
    /* 俯仰先按角度增量旋转，再钳制到 ±75°，避免镜头翻过头。 */
    camera->pitch_sy = (old_psy * 1024 + camera->pitch_cy * pitch) / 1024;
    camera->pitch_cy = (camera->pitch_cy * 1024 - old_psy * pitch) / 1024;
    length = isqrt((long long)camera->pitch_sy * camera->pitch_sy +
                   (long long)camera->pitch_cy * camera->pitch_cy);
    if (length > 0) {
        camera->pitch_sy = (int)((long long)camera->pitch_sy * 1024 / length);
        camera->pitch_cy = (int)((long long)camera->pitch_cy * 1024 / length);
    }
    /* 不能只看 sin：一次较大的鼠标输入可能直接越过 90°，此时
     * sin 会重新变小但 cos 已为负，仍必须钳回边界。 */
    if (camera->pitch_cy < PITCH_LIMIT_CY) {
        if (camera->pitch_sy < 0) {
            camera->pitch_sy = -PITCH_LIMIT_SY;
        } else {
            camera->pitch_sy = PITCH_LIMIT_SY;
        }
        camera->pitch_cy = PITCH_LIMIT_CY;
    } else if (camera->pitch_sy > PITCH_LIMIT_SY) {
        camera->pitch_sy = PITCH_LIMIT_SY;
        camera->pitch_cy = PITCH_LIMIT_CY;
    } else if (camera->pitch_sy < -PITCH_LIMIT_SY) {
        camera->pitch_sy = -PITCH_LIMIT_SY;
        camera->pitch_cy = PITCH_LIMIT_CY;
    }
}

static int sensitivity_percent(int level)
{
    return 50 + clampi(level, 0, 15) * 10;
}

static void update_mouse(struct camera *camera, int relative_x, int relative_y,
                         const struct control_settings *settings)
{
    int percent = sensitivity_percent(settings->mouse_level);
    /* 水平/垂直同一倍率，避免方向手感不一致 */
    int turn = relative_x * 3 * percent / 100;
    int pitch = -relative_y * 3 * percent / 100;
    rotate_camera(camera, clampi(turn, -256, 256), pitch);
}

static void update_keyboard_look(struct camera *camera,
                                 const struct toy_input *input,
                                 const struct control_settings *settings)
{
    int turn = toy_input_down(input, KEY_RIGHT) -
               toy_input_down(input, KEY_LEFT);
    int pitch = toy_input_down(input, KEY_UP) -
                toy_input_down(input, KEY_DOWN);
    int percent = sensitivity_percent(settings->keyboard_level);
    /* 左右/上下同一倍率 */
    if (turn || pitch)
        rotate_camera(camera, turn * 16 * percent / 100,
                      pitch * 16 * percent / 100);
}

/* ,/. 不是瞬间跳转，而是把 90° 加入队列，在固定逻辑步中快速完成。
 * turn=1024 约等于 1 弧度，90° 约为 1611 turn 单位；每步最多转 128，
 * 约 0.2 秒完成一次，期间仍可继续按键排队。 */
#define QUARTER_TURN 1611
#define SMOOTH_TURN_STEP 128

static void update_smooth_turn(struct camera *camera)
{
    int step = smooth_turn_remaining;
    if (step > SMOOTH_TURN_STEP) step = SMOOTH_TURN_STEP;
    if (step < -SMOOTH_TURN_STEP) step = -SMOOTH_TURN_STEP;
    if (step == 0) return;
    rotate_camera(camera, step, 0);
    smooth_turn_remaining -= step;
}

static void draw_crosshair(struct toy_surface *surface)
{
    int cx = surface->width / 2, cy = surface->height / 2;
    for (int d = -6; d <= 6; d++) {
        if (d < -2 || d > 2) {
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
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 6) / 2, y + 28,
                   "PAUSED", 0xE7E9EC, surface->stride);
    for (int item = 0; item < PAUSE_ITEM_COUNT; item++) {
        uint32_t color = item == menu->selected ? 0xFFD060 : 0xE7E9EC;
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
            snprintf(line, sizeof(line), "%c QUIT", item == menu->selected ? '>' : ' ');
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

static int enemy_y(int y, int scale)
{
    return -900 + (y + 900) * scale / 1000;
}

/* 偶数槽位：方块人。分离的靴子、腿、躯干和头保持 Minecraft 式轮廓。 */
static int render_block_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 105, x - 15,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, 0x252A30);
    pixels += draw_cuboid(renderer, camera, x + 15, x + 105,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 105, z + 105, 0x252A30);
    pixels += draw_cuboid(renderer, camera, x - 100, x - 10,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x + 10, x + 100,
                          enemy_y(-760, scale), enemy_y(-450, scale),
                          z - 85, z + 85, color - 0x101008);
    pixels += draw_cuboid(renderer, camera, x - 175, x + 175,
                          enemy_y(-470, scale), enemy_y(20, scale),
                          z - 105, z + 105, color);
    pixels += draw_cuboid(renderer, camera, x - 155, x + 155,
                          enemy_y(0, scale), enemy_y(320, scale),
                          z - 155, z + 155, color + 0x202010);
    /* 像素化愤怒脸：下压的双眉、亮眼和紧闭嘴。 */
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -105, -18, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             18, 105, enemy_y(210, scale),
                             enemy_y(235, scale), 0x4A1010);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -82, -28, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             28, 82, enemy_y(160, scale),
                             enemy_y(200, scale), 0xFFF0A0);
    pixels += draw_face_rect(renderer, camera, x, z, 155, e->dir_x, e->dir_z,
                             -70, 70, enemy_y(80, scale),
                             enemy_y(105, scale), 0x4A1010);
    return pixels;
}

/* 奇数槽位：Madness 风格无臂人，圆柱躯干、椭圆头和厚底短靴。 */
static int render_round_enemy(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e,
                              int scale, uint32_t color)
{
    int x = e->x, z = e->z, pixels = 0;
    pixels += draw_cuboid(renderer, camera, x - 145, x - 12,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cuboid(renderer, camera, x + 12, x + 145,
                          enemy_y(-900, scale), enemy_y(-760, scale),
                          z - 180, z + 95, 0x202328);
    pixels += draw_cylinder(renderer, camera, x, z, 180,
                            enemy_y(-770, scale), enemy_y(20, scale), color);
    pixels += draw_ellipsoid_head(renderer, camera, x, z,
                                  enemy_y(155, scale), 205,
                                  (enemy_y(350, scale) - enemy_y(-40, scale)) / 2,
                                  color + 0x181810);
    /* 普通敌人面部只有一个醒目的十字。 */
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -25, 25, enemy_y(35, scale),
                             enemy_y(275, scale), 0x251F20);
    pixels += draw_face_rect(renderer, camera, x, z, 205, e->dir_x, e->dir_z,
                             -120, 120, enemy_y(135, scale),
                             enemy_y(175, scale), 0x251F20);
    return pixels;
}

static void render_enemy_alert(struct toy_renderer *renderer,
                               const struct camera *camera,
                               const struct toy_game_enemy *e, int scale)
{
    struct vec3 world, view;
    struct toy_screen_vertex screen;
    int x, y;
    /* 追踪尸潮也常显红色惊叹号：它们已知晓玩家位置，无需侦测。 */
    if (e->ai_state != TOY_GAME_ENEMY_ALERT &&
        e->ai_state != TOY_GAME_ENEMY_TRACKING) return;
    world.x = e->x;
    world.y = enemy_y(500, scale);
    world.z = e->z;
    world_to_view(camera, &world, &view);
    if (view.z < NEAR_Z) return;
    project_vertex(&renderer->surface, &view, &screen);
    x = screen.x - 5;
    y = screen.y;
    if (x < 1 || x + 11 >= renderer->surface.width ||
        y < 1 || y + 27 >= renderer->surface.height)
        return;
    fill_rect(&renderer->surface, x - 1, y - 1, 11, 18, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 1, 7, 14, 0xFF3030);
    fill_rect(&renderer->surface, x - 1, y + 19, 11, 8, 0x301010);
    fill_rect(&renderer->surface, x + 1, y + 21, 7, 4, 0xFF3030);
}

/* 两种低多边形敌人；受击闪红/命中闪白，倒地时整体纵向压扁。 */
static int render_blob_shadow(struct toy_renderer *renderer,
                              const struct camera *camera,
                              const struct toy_game_enemy *e, int scale)
{
    struct vec3 a, b, c, d;
    int rx = 230 * scale / 1000;
    int rz = 150 * scale / 1000;
    int y = -886; /* just above the baked floor */
    if (rx < 18) rx = 18;
    if (rz < 12) rz = 12;
    a.x = e->x - rx; a.y = y; a.z = e->z - rz;
    b.x = e->x + rx; b.y = y; b.z = e->z - rz;
    c.x = e->x + rx; c.y = y; c.z = e->z + rz;
    d.x = e->x - rx; d.y = y; d.z = e->z + rz;
    /* Two nested, opaque low-alpha-style tones approximate a soft penumbra. */
    return draw_quad(renderer, camera, &a, &b, &c, &d, 0x17151A);
}

static int render_enemies(struct toy_renderer *renderer,
                          const struct camera *camera)
{
    int pixels = 0;
    for (int i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        const struct toy_game_enemy *e = &game.enemies[i];
        struct vec3 center, view;
        uint32_t color;
        int scale = 1000;
        if (e->active == 0) continue;
        center.x = e->x;
        center.y = 0;
        center.z = e->z;
        world_to_view(camera, &center, &view);
        if (view.z > ENEMY_RENDER_DISTANCE) continue;
        if (e->active == 2) {
            scale = e->dying_ms * 1000 / TOY_GAME_DYING_MS;
            color = 0x5A1A1A;
        } else {
            if (e->hurt > 0) color = 0xBB3333;
            else if (e->flash > 0) color = 0xDFDFDF;
            else if (e->ai_state == TOY_GAME_ENEMY_TRACKING)
                color = 0x8A2A2A;   /* 尸潮追踪者：红色，一眼可辨 */
            else color = 0x4A5D3A;
        }
        pixels += render_blob_shadow(renderer, camera, e, scale);
        if ((i & 1) == 0)
            pixels += render_block_enemy(renderer, camera, e, scale, color);
        else
            pixels += render_round_enemy(renderer, camera, e, scale, color);
        render_enemy_alert(renderer, camera, e, scale);
    }
    return pixels;
}

/* ── 子弹轨迹与命中粒子（纯视觉；逻辑步进 16ms 推进） ──────────── */

/* num/den 线性插值两色（num=den 时取 from，0 时取 to） */
static uint32_t mix_color(uint32_t from, uint32_t to, int num, int den)
{
    unsigned long fr = (from >> 16) & 0xFF, fg = (from >> 8) & 0xFF, fb = from & 0xFF;
    unsigned long tr = (to >> 16) & 0xFF, tg = (to >> 8) & 0xFF, tb = to & 0xFF;
    return (uint32_t)(((fr * num + tr * (den - num)) / den) << 16 |
                      ((fg * num + tg * (den - num)) / den) << 8 |
                      ((fb * num + tb * (den - num)) / den));
}

/* 把游戏层的水平射线转成从枪口指向屏幕准心的 3D 视觉终点。
 * 游戏命中仍使用水平平面，而 tracer 必须补偿枪口在右下方造成的视差，
 * 否则它会从枪口斜着飞向准心旁边。 */
static void tracer_aim_endpoint(const struct camera *camera,
                                const struct toy_game_ray *ray,
                                int ex, int ez, int *out_x, int *out_y,
                                int *out_z)
{
    struct vec3 view_end;
    int dx = ex - game.px;
    int dz = ez - game.pz;
    int distance = isqrt((long long)dx * dx + (long long)dz * dz);
    int view_x, view_y, view_z;
    if (distance < 1) distance = 1;
    view_x = (dx * camera->cy - dz * camera->sy) / 1024;
    view_z = (dx * camera->sy + dz * camera->cy) / 1024;
    if (view_z < 1) view_z = 1;
    view_y = ray->vy * distance / 1024;
    /* 保留游戏层的实际 x/z 命中点，只补上同一颗弹丸的垂直扩散，
     * 射线和命中粒子因此使用完全一致的落点。 */
    view_end.x = view_x;
    view_end.y = view_y;
    view_end.z = view_z;
    view_to_world(camera, &view_end, &view_end);
    *out_x = view_end.x;
    *out_y = view_end.y;
    *out_z = view_end.z;
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
        struct rasterfall_tracer *t = &effects.tracers[effects.tracer_next];
        effects.tracer_next = (effects.tracer_next + 1) % RASTERFALL_TRACER_SLOTS;
        t->active = 1;
        t->sx = muzzle.x;
        t->sy = muzzle.y;
        t->sz = muzzle.z;
        tracer_aim_endpoint(camera, r, r->ex, r->ez,
                            &t->ex, &t->ey, &t->ez);
        t->life_ms = RASTERFALL_TRACER_LIFE_MS;
        if (r->hit_enemy || r->hit_world)
            rasterfall_effects_spawn_hit_particles(&effects, r->ex, t->ey,
                                                   r->ez, r->sy, r->cy);
    }
}

/* 一个带最终边界检查的 Bresenham。投影裁剪是为了避免远处端点导致
 * 巨量迭代，像素检查则是最后一道防线：fb_draw_line 不检查坐标。 */
static void draw_effect_line(struct toy_surface *surface,
                             int x0, int y0, int x1, int y1,
                             uint32_t color)
{
    int dx = x1 > x0 ? x1 - x0 : x0 - x1;
    int dy = y1 > y0 ? y0 - y1 : y1 - y0;
    int sx = x0 < x1 ? 1 : -1;
    int sy = y0 < y1 ? 1 : -1;
    int err = dx + dy;
    for (;;) {
        if (x0 >= 0 && x0 < surface->width &&
            y0 >= 0 && y0 < surface->height)
            put_pixel(surface, x0, y0, color);
        if (x0 == x1 && y0 == y1) break;
        {
            int e2 = 2 * err;
            if (e2 >= dy) { err += dy; x0 += sx; }
            if (e2 <= dx) { err += dx; y0 += sy; }
        }
    }
}

/* 弹道投影为屏幕线段：起点（枪口）与终点（命中点）都从世界空间投影，
 * 先裁剪到近平面，再 Liang-Barsky 裁剪到屏幕。 */
static void draw_tracer_line(struct toy_surface *surface, const struct camera *camera,
                             int sx, int sy, int sz, int ex, int ey, int ez,
                             uint32_t color)
{
    struct vec3 a, b, clipped;
    struct toy_screen_vertex pa, pb;
    int x0, y0, x1, y1, t0, t1, t_in, t_out, tmp;
    a.x = sx; a.y = sy; a.z = sz;
    b.x = ex; b.y = ey; b.z = ez;
    world_to_view(camera, &a, &a);
    world_to_view(camera, &b, &b);
    if (a.z < NEAR_Z && b.z < NEAR_Z) return;
    if (a.z < NEAR_Z) {
        near_intersection(&a, &b, &clipped);
        a.x = clipped.x;
        a.y = clipped.y;
        a.z = clipped.z;
    } else if (b.z < NEAR_Z) {
        near_intersection(&b, &a, &clipped);
        b.x = clipped.x;
        b.y = clipped.y;
        b.z = clipped.z;
    }
    project_vertex(surface, &a, &pa);
    project_vertex(surface, &b, &pb);
    x0 = pa.x; y0 = pa.y; x1 = pb.x; y1 = pb.y;
    /* 16.16 定点 Liang-Barsky；投影坐标有界但可能远超屏幕。
     * 裁剪到 [0, w-2]×[0, h-2]：第二条偏移线 (+1,+1) 也必须在界内。 */
    t0 = 0;
    t1 = 1 << 16;
    if (x0 != x1) {
        t_in = (int)((long long)(0 - x0) * 65536 / (x1 - x0));
        t_out = (int)((long long)(surface->width - 2 - x0) * 65536 / (x1 - x0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (x0 < 0 || x0 >= surface->width - 1) {
        return;   /* 竖直段整体在屏幕外 */
    }
    if (y0 != y1) {
        t_in = (int)((long long)(0 - y0) * 65536 / (y1 - y0));
        t_out = (int)((long long)(surface->height - 2 - y0) * 65536 / (y1 - y0));
        if (t_in > t_out) { tmp = t_in; t_in = t_out; t_out = tmp; }
        if (t_in > t0) t0 = t_in;
        if (t_out < t1) t1 = t_out;
        if (t0 > t1) return;
    } else if (y0 < 0 || y0 >= surface->height - 1) {
        return;   /* 水平段整体在屏幕外 */
    }
    if (t0 < 0) t0 = 0;
    if (t1 > (1 << 16)) t1 = 1 << 16;
    if (t0 > t1) return;
    if (t0 != 0 || t1 != (1 << 16)) {
        int nx0 = x0 + (int)((long long)(x1 - x0) * t0 / 65536);
        int ny0 = y0 + (int)((long long)(y1 - y0) * t0 / 65536);
        x1 = nx0 + (int)((long long)(x1 - x0) * (t1 - t0) / 65536);
        y1 = ny0 + (int)((long long)(y1 - y0) * (t1 - t0) / 65536);
        x0 = nx0;
        y0 = ny0;
    }
    /* 双线 = 2px 粗的射线；偏移像素同色，无需额外混合 */
    draw_effect_line(surface, x0, y0, x1, y1, color);
    draw_effect_line(surface, x0 + 1, y0 + 1, x1 + 1, y1 + 1, color);
}

static int render_tracers(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_TRACER_SLOTS; i++) {
        const struct rasterfall_tracer *t = &effects.tracers[i];
        uint32_t color;
        if (!t->active) continue;
        color = mix_color(0xFFE060, 0x3A2C14,
                          t->life_ms * 256 / RASTERFALL_TRACER_LIFE_MS, 256);
        draw_tracer_line(&renderer->surface, camera, t->sx, t->sy, t->sz,
                         t->ex, t->ey, t->ez, color);
        pixels++;
    }
    return pixels;
}

static int render_particles(struct toy_renderer *renderer, const struct camera *camera)
{
    int i, pixels = 0;
    for (i = 0; i < RASTERFALL_PARTICLE_SLOTS; i++) {
        const struct rasterfall_particle *p = &effects.particles[i];
        struct vec3 world, view;
        struct toy_screen_vertex screen;
        int k;
        if (!p->active) continue;
        world.x = p->x;
        world.y = p->y;
        world.z = p->z;
        world_to_view(camera, &world, &view);
        if (view.z < NEAR_Z) continue;
        project_vertex(&renderer->surface, &view, &screen);
        if (screen.x < 0 || screen.x + 2 >= renderer->surface.width ||
            screen.y < 0 || screen.y + 2 >= renderer->surface.height) continue;
        k = p->life_ms * 256 / RASTERFALL_PARTICLE_LIFE_MS;
        if (k > 256) k = 256;   /* 寿命随机 +40ms 可能超出基准，钳制插值比例 */
        fill_rect(&renderer->surface, screen.x, screen.y, 2, 2,
                  mix_color(0xFFC860, 0x4A2008, k, 256));
        pixels++;
    }
    return pixels;
}

static void draw_game_over_panel(struct toy_surface *surface)
{
    char line[96];
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0xD88A32);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 8) / 2, y + 28,
                   "YOU DIED", 0xE7E9EC, surface->stride);
    snprintf(line, sizeof(line), "WAVE %d  KILLS %d", game.wave, game.kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, 0xE7E9EC, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 21) / 2, y + 104,
                   "R restart   Esc quit", 0xD88A32, surface->stride);
}

static void draw_level_won_panel(struct toy_surface *surface)
{
    char line[96];
    int panel_w = surface->width / 3;
    int panel_h = surface->height / 3;
    int x = (surface->width - panel_w) / 2;
    int y = (surface->height - panel_h) / 2;
    fill_rect(surface, x - 3, y - 3, panel_w + 6, panel_h + 6, 0x56B878);
    fill_rect(surface, x, y, panel_w, panel_h, 0x171B24);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 17) / 2, y + 28,
                   "SAFE ROOM REACHED", 0x80E080, surface->stride);
    snprintf(line, sizeof(line), "KILLS %d", game.kills);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * (int)strlen(line)) / 2, y + 60,
                   line, 0xE7E9EC, surface->stride);
    fb_draw_string((unsigned char *)surface->pixels,
                   x + (panel_w - FB_FONT_W * 21) / 2, y + 104,
                   "R restart   Esc quit", 0xD88A32, surface->stride);
}

static void draw_input_debug(struct toy_surface *surface,
                             const struct toy_input *input,
                             unsigned int last_key, int last_pressed,
                             int event_count)
{
    char line[96];
    int x = 8;
    int y = surface->height - FB_FONT_H * 4 - 12;
    unsigned int color = input->keyboard_focused ? 0x80E080 : 0xE08080;
    fill_rect(surface, x - 4, y - 4, 390, FB_FONT_H * 4 + 8, 0x171B24);
    snprintf(line, sizeof(line), "KEYBOARD %s  EVENTS %d",
             input->keyboard_focused ? "FOCUSED" : "UNFOCUSED", event_count);
    fb_draw_string((unsigned char *)surface->pixels, x, y,
                   line, color, surface->stride);
    snprintf(line, sizeof(line), "LAST %u %s",
             last_key, last_pressed ? "PRESSED" : "RELEASED");
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H,
                   line, 0xE7E9EC, surface->stride);
    snprintf(line, sizeof(line), "DOWN W%d A%d S%d D%d SPC%d ENT%d",
             toy_input_down(input, KEY_W), toy_input_down(input, KEY_A),
             toy_input_down(input, KEY_S), toy_input_down(input, KEY_D),
             toy_input_down(input, KEY_SPACE), toy_input_down(input, KEY_ENTER));
    fb_draw_string((unsigned char *)surface->pixels, x, y + FB_FONT_H * 2,
                   line, 0xE7E9EC, surface->stride);
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
    int last_pointer_x = 0, last_pointer_y = 0, have_pointer_position = 0;
    int frame_limit = 0, rendered_frames = 0, scene_pixels = 0;
    int display_fps = 0, fps_window_frames = 0;
    int fire_edge = 0;
    long menu_nav_ready_us = 0;
    /* 按键按压边沿跨帧保留位：逻辑步（E/R/,/. 及切枪换弹）可能因
     * accumulator 不足而整帧不跑（长 stall 后连续几帧都不跑），边沿若
     * 只在 key_pressed 里会被下一轮 begin_frame 清掉。这里逐键记录
     * 到达的按压，每帧合入 key_pressed 供消费方读取；逻辑步跑过的那
     * 帧末尾统一清除。 */
    unsigned char pending_key_edges[TOY_INPUT_KEY_COUNT];
    int interact_consumed = 0;   /* 每帧 E 互动只消费一次 */
    int input_debug = 0, input_event_count = 0, have_last_key = 0;
    int texture_stats = 0;
    int stats_enabled = 1;
    struct rasterfall_perf_stats stats, stats_total;
    unsigned int last_key = 0;
    int last_key_pressed = 0;
    struct rasterfall_audio audio;
    uint64_t seed;

    int logic_test = 0;
    int auto_mode = 0;
    const char *dump_path = 0;
    for (int arg = 1; arg < argc; arg++) {
        if (strcmp(argv[arg], "--input-test") == 0) input_debug = 1;
        else if (strcmp(argv[arg], "--logic-test") == 0) logic_test = 1;
        else if (strcmp(argv[arg], "--auto") == 0) auto_mode = 1;
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
    rasterfall_map_bind(&map_ops, &level_map, map_bounds, map_safe_rooms,
                        map_spawn_zones, &map_spawn_count, &air_wall_enabled,
                        interactables, &interactable_count);
    if (rasterfall_map_load(&map_ops, "assets/maps/rasterfall.map") < 0) {
        __fprintf(2, "rasterfall: cannot load map assets/maps/rasterfall.map\n");
        return 1;
    }
    bake_static_lightmap();
    rasterfall_effects_init(&effects);
    __printf("rasterfall: baked lightmap %dx%d\n", BAKED_LM_W, BAKED_LM_H);
    prepare_map_rules();
    memset(&scene_texture, 0, sizeof(scene_texture));
    memset(&scene_texture_view, 0, sizeof(scene_texture_view));
    if (textures_enabled && toy_texture_load("assets/generated/wall.ttex",
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
    active_texture_view = &scene_texture_view;
    if (logic_test) {
        int result = run_logic_test();
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        rasterfall_map_unload(&map_ops);
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
    reset_game(&camera, seed);
    window = toy_window_open("Rasterfall", 800, 450);
    if (!window) {
        __fprintf(2, "rasterfall: cannot create Wayland window\n");
        if (scene_texture.blob) toy_texture_unload(&scene_texture);
        rasterfall_map_unload(&map_ops);
        toy_renderer_destroy(&renderer);
        return 1;
    }
    __printf("rasterfall: pause menu uses arrows + Enter; mouse/arrows look, "
             "WASD moves, click/Space fire (hold for SMG), R reload, "
             "1/2 weapons, E interact, ,/. turn 90, Esc pauses/resumes\n");
    if (input_debug)
        __printf("rasterfall: input debug HUD enabled; test chords and focus changes\n");
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
        toy_input_begin_frame(&input);
        /* 非阻塞收输入：present 后立刻开始下一帧 CPU 工作，组合器处理
         * 已提交缓冲的时间被渲染流水线掩盖（双缓冲）。 */
        if (toy_window_poll(window, &events, 0) < 0) break;
        toy_input_apply(&input, &events);
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
                else if (pause_menu.selected == PAUSE_ITEM_QUIT)
                    running = 0;
            }
            if (toy_input_pressed(&input, KEY_ESC)) {
                resume_requested = 1;
            }
            if (resume_requested) {
            int capture_result = toy_window_set_pointer_lock(window, 1);
            pointer_lock_requested = capture_result > 0;
            paused = 0;
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
                pause_menu.selected = PAUSE_ITEM_RESUME;
                __printf("rasterfall: paused, pointer released\n");
            }
        }
        /* 射击输入：每帧只取一次边沿（恢复点击帧不开火） */
        if (!paused && !resumed && events.button_pressed && events.button == BTN_LEFT)
            fire_edge = 1;
        if (!paused && !resumed && toy_input_pressed(&input, KEY_SPACE))
            fire_edge = 1;
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
                rotate_camera(&camera, 37, 0);
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
            update_mouse(&camera, input.relative_x, input.relative_y, &settings);
        } else if (!paused && pointer_lock_requested && input.pointer_moved) {
            if (have_pointer_position)
                update_mouse(&camera, input.pointer_x - last_pointer_x,
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
        interact_consumed = 0;
        t_stage = now;
        while (accumulator >= FIXED_STEP_US && logic_steps < MAX_LOGIC_STEPS) {
            if (!paused) {
                rasterfall_effects_update(&effects, FIXED_STEP_US / 1000);
                if (game.state == TOY_GAME_PLAYING) {
                    update_player(&camera, &input);
                    update_keyboard_look(&camera, &input, &settings);
                    /* 90° 快转：按键只加入队列，同一帧多个逻辑步也只加入一次 */
                    if (toy_input_pressed(&input, KEY_COMMA)) {
                        smooth_turn_remaining -= QUARTER_TURN;
                        input.key_pressed[KEY_COMMA] = 0;
                    }
                    if (toy_input_pressed(&input, KEY_DOT)) {
                        smooth_turn_remaining += QUARTER_TURN;
                        input.key_pressed[KEY_DOT] = 0;
                    }
                    update_smooth_turn(&camera);
                    game.px = camera.x;
                    game.pz = camera.z;
                    highlighted = compute_highlight(&camera);
                    if (highlighted >= 0 && !interact_consumed &&
                        toy_input_pressed(&input, KEY_E)) {
                        interact_current(&interactables[highlighted]);
                        interact_consumed = 1;
                    }
                    toy_game_update_held(&game, input.key_pressed, fire_edge,
                                         toy_input_down(&input, KEY_SPACE),
                                         camera.sy, camera.cy,
                                         FIXED_STEP_US / 1000);
                    update_manual_alarm(FIXED_STEP_US / 1000);
                    fire_edge = 0;
                } else if (toy_input_pressed(&input, KEY_R)) {
                    /* 死亡或通关结算：R 重开 */
                    reset_game(&camera, seed);
                    fire_edge = 0;
                }
            }
            accumulator -= FIXED_STEP_US;
            logic_steps++;
        }
        if (!paused) sync_fire_effects(&camera);
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
            continue;
        }
        if (ready > 0) {
            if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) break;
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_BEGIN,
                           &t_stage, 0, 0);
            prev_tris = renderer.submitted_triangles;
            scene_pixels = render_scene(&renderer, &camera);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_SCENE, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            prev_tris = renderer.submitted_triangles;
            scene_pixels += render_enemies(&renderer, &camera);
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_ENEMIES, &t_stage,
                           renderer.submitted_triangles - prev_tris, 0);
            /* 世界几何并行光栅化；弹道/粒子/枪模随后直接写屏覆盖 */
            prev_tris = (unsigned long)renderer.cmd_count;
            stage_pixels = toy_renderer_flush(&renderer);
            scene_pixels += stage_pixels;
            rasterfall_perf_end_stage(&stats, &stats_total, RASTERFALL_STATS_RASTER,
                           &t_stage, prev_tris, (unsigned long)stage_pixels);
            rasterfall_perf_add_raster(&stats, &stats_total, &renderer, prev_tris,
                            (unsigned long)stage_pixels);
            /* 直接写屏与第二次光栅化（拾取物）都归入 overlay 阶段 */
            prev_tris = renderer.submitted_triangles;
            stage_pixels = 0;
            stage_pixels += render_tracers(&renderer, &camera);
            stage_pixels += render_particles(&renderer, &camera);
            /* 第一人称武器：最后画，叠加在世界之上 */
            stage_pixels += rasterfall_viewmodel_render(&renderer, &game, &effects);
            if (game.state == TOY_GAME_OVER) {
                draw_game_over_panel(&surface);
            } else if (game.state == TOY_GAME_WON) {
                draw_level_won_panel(&surface);
            } else if (paused) {
                draw_pause_overlay(&surface, &pause_menu, &settings);
            } else {
                draw_crosshair(&surface);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud);
                    rasterfall_hud_render(&surface, display_fps, &hud);
                }
            }
            if (game.state == TOY_GAME_PLAYING && !paused) {
                stage_pixels += render_interactables(&renderer, &camera);
                /* 拾取物保持画在枪模之上的现状顺序 */
                stage_pixels += toy_renderer_flush(&renderer);
                {
                    struct rasterfall_hud_state hud;
                    fill_hud_state(&hud);
                    rasterfall_hud_draw_interact_prompt(&renderer, &hud);
                }
            }
            scene_pixels += stage_pixels;
            rasterfall_hud_damage_flash(&surface, &game);
            if (input_debug)
                draw_input_debug(&surface, &input,
                                 have_last_key ? last_key : 0,
                                 have_last_key ? last_key_pressed : 0,
                                 input_event_count);
            /* 游戏线程只投递事件，音乐与 SFX 由音频线程持续混音。 */
            if (audio.running) rasterfall_audio_play_game_events(&audio, &game);
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
    if (stats_enabled && stats_total.frames > 0)
        rasterfall_perf_dump(&stats_total, "total");
    rasterfall_audio_stop(&audio);
    rasterfall_audio_unload_assets(&audio);
    if (scene_texture.blob) toy_texture_unload(&scene_texture);
    if (dump_path) rasterfall_hud_dump_frame(dump_path, &surface);
    rasterfall_map_unload(&map_ops);
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
