#include "tlibc_everything.h"
#include "math.h"
#include "rasterfall_session.h"

#define INTERACT_AIM_CONE 784
#define HORDE_COUNT_MIN 15
#define HORDE_COUNT_MAX 20
#define HORDE_MIN_PLAYER_DIST 700
#define QUARTER_TURN 1611
#define SMOOTH_TURN_STEP 128
static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it);

static int session_base_center(const struct rasterfall_session *session, int id,
                               int *x, int *z)
{
    int i;
    for (i = 0; i < session->level.base_count; i++) {
        const struct toy_map_base *base = &session->level.bases[i];
        if (base->id != id) continue;
        if (x) *x = (base->box.minx + base->box.maxx) / 2;
        if (z) *z = (base->box.minz + base->box.maxz) / 2;
        return 1;
    }
    return 0;
}

static void session_down_ai(struct rasterfall_session *session, int index,
                            int x, int z)
{
    struct toy_game_actor *actor = &session->game_state.actors[index];
    toy_game_move_ai_actor(&session->game_state, index, x, z);
    actor->hp = 0;
    actor->state = TOY_GAME_ACTOR_DOWNED;
    actor->revive_progress_ms = 0;
    if (index == 0) {
        session->game_state.ai_hp = 0;
        session->game_state.ai_down = 1;
        session->game_state.ai_revive_progress_ms = 0;
    }
}

static int session_spawn_base_horde(struct rasterfall_session *session,
                                    int base)
{
    struct toy_game *game = &session->game_state;
    int spawned = 0;
    if (base == 1) {
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_COMMON,
                                             12, 16, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_FAST,
                                             4, 6, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_HEAVY,
                                             2, 3, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_PURSUIT_FAST,
                                             2, 3, session->spawn_zones,
                                             session->spawn_count, 900);
    } else {
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_COMMON,
                                             20, 26, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_FAST,
                                             8, 12, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_HEAVY,
                                             8, 10, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game,
                                             TOY_GAME_ENEMY_PURSUIT_HEAVY,
                                             5, 7, session->spawn_zones,
                                             session->spawn_count, 900);
        spawned += toy_game_spawn_horde_type(game, TOY_GAME_ENEMY_PURSUIT_FAST,
                                             6, 9, session->spawn_zones,
                                             session->spawn_count, 900);
    }
    game->campaign_phase = TOY_GAME_PHASE_HORDE;
    game->phase_timer_ms = base == 1 ? 30000 : 45000;
    game->spawn_budget = 0;
    return spawned;
}

static int session_near_ai(const struct rasterfall_session *session,
                           const struct camera *camera, int *out_index)
{
    int i, best = -1;
    long best_d2 = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *actor = &session->game_state.actors[i];
        long dx, dz, d2;
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->state != TOY_GAME_ACTOR_DOWNED) continue;
        dx = (long)camera->x - actor->x;
        dz = (long)camera->z - actor->z;
        d2 = dx * dx + dz * dz;
        if (d2 > (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE)
            continue;
        if (best < 0 || d2 < best_d2) { best = i; best_d2 = d2; }
    }
    if (out_index) *out_index = best;
    return best >= 0;
}

int rasterfall_session_find_down_ai(const struct rasterfall_session *session,
                                    const struct camera *camera)
{
    int index = -1;
    if (!session || !camera || !session_near_ai(session, camera, &index))
        return -1;
    return index;
}

static void session_set_air_walls(struct rasterfall_session *session,
                                  int enabled)
{
    rasterfall_map_set_air_walls(&session->map_ops, enabled);
}

int rasterfall_session_load(struct rasterfall_session *session,
                            const char *map_path)
{
    memset(session, 0, sizeof(struct rasterfall_session));
    session->air_walls_enabled = 1;
    session->highlight_index = -1;
    rasterfall_map_bind(&session->map_ops, &session->level, session->bounds,
                        session->safe_rooms, session->spawn_zones,
                        &session->spawn_count, &session->air_walls_enabled,
                        session->items, &session->item_count);
    if (rasterfall_map_load(&session->map_ops, map_path) < 0) return -1;
    rasterfall_map_prepare(&session->map_ops);
    return 0;
}

void rasterfall_session_unload(struct rasterfall_session *session)
{
    rasterfall_map_unload(&session->map_ops);
}

void rasterfall_session_reset(struct rasterfall_session *session,
                              struct camera *camera, uint64_t seed)
{
    int i;
    camera->x = session->level.start_x;
    camera->z = session->level.start_z;
    camera->sy = 0;
    camera->cy = 1024;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    camera->y = 0;
    session->seed = seed ? seed : 1;
    toy_game_init(&session->game_state, session->seed);
    /* 环境变量不依赖 libc；HOSTNAME 是最稳定的本机身份来源，缺失时
     * toy_game_init 的 PLAYER 保底仍可用。名字只用于身份展示/未来快照。 */
    if (global_envp && get_env_var(global_envp, "HOSTNAME"))
        toy_game_set_player_name(&session->game_state,
                                 get_env_var(global_envp, "HOSTNAME"));
    toy_game_set_world(&session->game_state, session->bounds,
                       session->level.box_count, session->level.room_limit);
    toy_game_set_platforms(&session->game_state, session->level.platforms,
                           session->level.platform_count);
    toy_game_set_campaign(&session->game_state, session->safe_rooms,
                          session->level.safe_count, session->spawn_zones,
                          session->spawn_count);
    toy_game_set_campaign_safe_indices(&session->game_state,
                                       session->level.start_safe_index,
                                       session->level.goal_safe_index);
    toy_game_set_alarm(&session->game_state,
                       session->level.has_alarm ? &session->level.alarm_zone : NULL,
                       session->level.has_alarm ? session->level.alarm_spawn_zone : -1);
    for (i = 0; i < TOY_MAP_MAX_BASES; i++) session->base_actor_indices[i] = -1;
    /* AI 出生点属于地图语义；第一个条目仍占用 actor 0，以兼容旧的
     * toy_game AI 镜像和现有 HUD/网络协议。 */
    for (i = 0; i < session->level.ai_spawn_count && i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_map_ai_spawn *spawn = &session->level.ai_spawns[i];
        int actor_index;
        if (i == 0) {
            toy_game_set_ai_teammate_class(&session->game_state, 1,
                                           spawn->class_id, spawn->x, spawn->z,
                                           spawn->name);
            actor_index = 0;
        } else {
            int actor_id = toy_game_add_ai(&session->game_state, spawn->class_id,
                                           spawn->x, spawn->z, spawn->name);
            actor_index = actor_id > 0 ? actor_id - 1 : -1;
        }
        if (actor_index < 0) continue;
        if (spawn->base_id >= 0 && spawn->base_id < TOY_MAP_MAX_BASES &&
            session->base_actor_indices[spawn->base_id] < 0)
            session->base_actor_indices[spawn->base_id] = actor_index;
        if (spawn->downed) session_down_ai(session, actor_index, spawn->x, spawn->z);
    }
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    session->banner_ms = 0;
    session->banner_text = NULL;
    session->manual_alarm_on = 0;
    session->manual_alarm_timer = 1000;
    session->highlight_index = -1;
    session->smooth_turn_remaining = 0;
    session->ai_revive_active = 0;
    session->ai_revive_actor_index = -1;
    session_set_air_walls(session, 1);
    rasterfall_map_reset_interactables(&session->map_ops);
}

void rasterfall_camera_rotate(struct camera *camera, int turn, int pitch)
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
    camera->pitch_sy = (old_psy * 1024 + camera->pitch_cy * pitch) / 1024;
    camera->pitch_cy = (camera->pitch_cy * 1024 - old_psy * pitch) / 1024;
    length = isqrt((long long)camera->pitch_sy * camera->pitch_sy +
                   (long long)camera->pitch_cy * camera->pitch_cy);
    if (length > 0) {
        camera->pitch_sy = (int)((long long)camera->pitch_sy * 1024 / length);
        camera->pitch_cy = (int)((long long)camera->pitch_cy * 1024 / length);
    }
    if (camera->pitch_cy < RASTERFALL_PITCH_LIMIT_CY) {
        camera->pitch_sy = camera->pitch_sy < 0 ?
            -RASTERFALL_PITCH_LIMIT_SY : RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    } else if (camera->pitch_sy > RASTERFALL_PITCH_LIMIT_SY) {
        camera->pitch_sy = RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    } else if (camera->pitch_sy < -RASTERFALL_PITCH_LIMIT_SY) {
        camera->pitch_sy = -RASTERFALL_PITCH_LIMIT_SY;
        camera->pitch_cy = RASTERFALL_PITCH_LIMIT_CY;
    }
}

static void session_move_player(struct rasterfall_session *session,
                                struct camera *camera,
                                const struct rasterfall_command *command)
{
    if (session->game_state.player_control_disabled ||
        session->game_state.player_airborne_ms > 0)
        return;
    int dx = (camera->sy * command->move_forward +
              camera->cy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int dz = (camera->cy * command->move_forward -
              camera->sy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!toy_game_position_blocked_at_height(&session->game_state, next_x,
                                             camera->z, RASTERFALL_PLAYER_RADIUS,
                                             session->game_state.player_ground_y))
        camera->x = next_x;
    if (!toy_game_position_blocked_at_height(&session->game_state, camera->x,
                                             next_z, RASTERFALL_PLAYER_RADIUS,
                                             session->game_state.player_ground_y))
        camera->z = next_z;
}

static void session_move_remote_player(struct rasterfall_session *session,
                                       struct camera *camera,
                                       const struct rasterfall_command *command,
                                       int ground_y)
{
    int dx = (camera->sy * command->move_forward +
              camera->cy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int dz = (camera->cy * command->move_forward -
              camera->sy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!toy_game_position_blocked_at_height(&session->game_state, next_x,
                                             camera->z, RASTERFALL_PLAYER_RADIUS,
                                             ground_y))
        camera->x = next_x;
    if (!toy_game_position_blocked_at_height(&session->game_state, camera->x,
                                             next_z, RASTERFALL_PLAYER_RADIUS,
                                             ground_y))
        camera->z = next_z;
}

static void session_jump_player(struct rasterfall_session *session,
                                struct camera *camera,
                                const struct rasterfall_command *command)
{
    int dx = (camera->sy * command->move_forward +
              camera->cy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int dz = (camera->cy * command->move_forward -
              camera->sy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    toy_game_jump_with_velocity(&session->game_state, dx, dz);
}

static void session_sync_special_motion(struct rasterfall_session *session,
                                        struct camera *camera)
{
    if (!session || !camera) return;
    if (session->game_state.player_control_disabled ||
        session->game_state.player_airborne_ms > 0) {
        camera->x = session->game_state.px;
        camera->z = session->game_state.pz;
    }
    camera->y = session->game_state.player_ground_y +
                session->game_state.player_airborne_y;
}

static void session_update_smooth_turn(struct rasterfall_session *session,
                                       struct camera *camera)
{
    int step = session->smooth_turn_remaining;
    if (step > SMOOTH_TURN_STEP) step = SMOOTH_TURN_STEP;
    if (step < -SMOOTH_TURN_STEP) step = -SMOOTH_TURN_STEP;
    if (step == 0) return;
    rasterfall_camera_rotate(camera, step, 0);
    session->smooth_turn_remaining -= step;
}

void rasterfall_session_step_remote_player(struct rasterfall_session *session,
                                           struct camera *camera,
                                           const struct rasterfall_command *command,
                                           int remote_down, int ground_y)
{
    if (!remote_down)
        session_move_remote_player(session, camera, command, ground_y);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    if (command->buttons & RASTERFALL_CMD_SHOVE) {
        /* 推开以远端玩家自己的位置/朝向为准（主机 px/pz 属本地玩家） */
        int save_px = session->game_state.px;
        int save_pz = session->game_state.pz;
        session->game_state.px = camera->x;
        session->game_state.pz = camera->z;
        toy_game_shove(&session->game_state, camera->sy, camera->cy);
        session->game_state.px = save_px;
        session->game_state.pz = save_pz;
    }
}

void rasterfall_session_interact_remote(struct rasterfall_session *session,
                                        const struct camera *camera)
{
    int index;
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    index = rasterfall_session_compute_highlight(session, camera);
    if (index >= 0) session_interact(session, &session->items[index]);
}

int rasterfall_session_revive_remote(struct rasterfall_session *session,
                                     const struct camera *camera, int dt_ms)
{
    int actor_index = -1;
    if (!session || !camera || session->game_state.player_down) return -1;
    if (!session_near_ai(session, camera, &actor_index)) return -1;
    return toy_game_revive_actor(&session->game_state, actor_index, dt_ms);
}

int rasterfall_session_revive_player(struct rasterfall_session *session,
                                     const struct camera *rescuer,
                                     const struct camera *target,
                                     int *progress_ms, int dt_ms)
{
    long dx, dz;
    if (!session || !rescuer || !target || !progress_ms || dt_ms <= 0)
        return -1;
    dx = (long)rescuer->x - target->x;
    dz = (long)rescuer->z - target->z;
    if (dx * dx + dz * dz >
        (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE)
        return -1;
    *progress_ms += dt_ms;
    if (*progress_ms < TOY_GAME_REVIVE_MS) return 0;
    *progress_ms = 0;
    return 1;
}

int rasterfall_session_compute_highlight(const struct rasterfall_session *session,
                                         const struct camera *camera)
{
    int i, best = -1;
    long best_d2 = 0;
    for (i = 0; i < session->item_count; i++) {
        const struct rasterfall_interactable *it = &session->items[i];
        long dx = it->x - camera->x;
        long dz = it->z - camera->z;
        long d2 = dx * dx + dz * dz;
        long dist, dot;
        if (d2 > (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE ||
            d2 == 0) continue;
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

static void session_client_interact_banner(struct rasterfall_session *session)
{
    const struct rasterfall_interactable *it;
    if (session->highlight_index < 0 ||
        session->highlight_index >= session->item_count) return;
    it = &session->items[session->highlight_index];
    session->banner_ms = 1800;
    if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON)
        session->banner_text = session->air_walls_enabled ?
            "AIR WALLS DISABLED" : "AIR WALLS ENABLED";
    else if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON)
        session->banner_text = session->manual_alarm_on ?
            "ALARM DISABLED" : "ALARM ENABLED - 2-3 ENEMIES EACH SECOND";
    else if (it->kind == TOY_MAP_PICKUP_BUTTON)
        session->banner_text = "HORDE SUMMONED - THEY WILL FIND YOU";
    else if (it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON)
        session->banner_text = "BROWN BRUTE HORDE SUMMONED";
    else if (it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON)
        session->banner_text = "RED RUNNER HORDE SUMMONED";
    else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON)
        session->banner_text = "SMOKER SUMMONED";
    else if (it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON)
        session->banner_text = "CHARGER SUMMONED";
    else if (it->kind == TOY_MAP_PICKUP_AMMO)
        session->banner_text = "AMMO REFILLED";
    else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
             it->kind == TOY_MAP_PICKUP_SMG ||
             it->kind == TOY_MAP_PICKUP_SHOTGUN)
        session->banner_text = "WEAPON PICKED UP";
    else
        session->banner_text = "INTERACTION SENT TO HOST";
}

static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it)
{
    if (it->kind == TOY_MAP_PICKUP_BASE_1_BUTTON) {
        if (session->game_state.campaign_stage != 0) {
            session->banner_text = "FIRST BASE ALREADY CLEARED";
            session->banner_ms = 1800;
            return;
        }
        /* 据点防守的前提：先救援本据点的 AI，否则尸潮来了没人守。 */
        if (session->base_actor_indices[1] < 0 ||
            session->game_state.actors[session->base_actor_indices[1]].state !=
                TOY_GAME_ACTOR_ALIVE) {
            session->banner_text = "REVIVE GUARD FIRST";
            session->banner_ms = 1800;
            return;
        }
        toy_game_set_campaign_stage(&session->game_state, 1);
        session->banner_text = "BASE 1 OVERRUN - MIXED HORDE INCOMING";
        session->banner_ms = 3500;
        __printf("rasterfall: base 1 horde summoned %d\n",
                  session_spawn_base_horde(session, 1));
    } else if (it->kind == TOY_MAP_PICKUP_BASE_2_BUTTON) {
        if (session->game_state.campaign_stage != 1) {
            session->banner_text = "CLEAR BASE 1 FIRST";
            session->banner_ms = 1800;
            return;
        }
        if (session->base_actor_indices[2] < 0 ||
            session->game_state.actors[session->base_actor_indices[2]].state !=
                TOY_GAME_ACTOR_ALIVE) {
            session->banner_text = "REVIVE JESUS FIRST";
            session->banner_ms = 1800;
            return;
        }
        {
            int base_x, base_z;
            if (session_base_center(session, 2, &base_x, &base_z) &&
                session->base_actor_indices[1] >= 0)
            toy_game_move_ai_actor(&session->game_state,
                                   session->base_actor_indices[1],
                                   base_x + 700, base_z);
        }
        toy_game_set_campaign_stage(&session->game_state, 2);
        session->banner_text = "BASE 2 OVERRUN - FINAL HORDE INCOMING";
        session->banner_ms = 4000;
        __printf("rasterfall: base 2 horde summoned %d\n",
                  session_spawn_base_horde(session, 2));
    } else if (it->kind == TOY_MAP_PICKUP_BUTTON) {
        int n;
        session->banner_ms = 3500;
        session->banner_text = "HORDE SUMMONED - THEY WILL FIND YOU";
        n = toy_game_spawn_horde(&session->game_state, HORDE_COUNT_MIN,
                                 HORDE_COUNT_MAX, session->spawn_zones,
                                 session->spawn_count, HORDE_MIN_PLAYER_DIST);
        __printf("rasterfall: horde summoned %d tracking enemies\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_AIR_BUTTON) {
        session_set_air_walls(session, !session->air_walls_enabled);
        session->banner_ms = 1800;
        session->banner_text = session->air_walls_enabled ?
            "AIR WALLS ENABLED" : "AIR WALLS DISABLED";
    } else if (it->kind == TOY_MAP_PICKUP_ALARM_BUTTON) {
        session->manual_alarm_on = !session->manual_alarm_on;
        session->manual_alarm_timer = 1000;
        session->banner_ms = 1800;
        session->banner_text = session->manual_alarm_on ?
            "ALARM ENABLED - 2-3 ENEMIES EACH SECOND" : "ALARM DISABLED";
    } else if (it->kind == TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON) {
        int n = toy_game_spawn_horde_type(&session->game_state,
            TOY_GAME_ENEMY_PURSUIT_HEAVY, 2, 3, session->spawn_zones,
            session->spawn_count, HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3000;
        session->banner_text = "BROWN BRUTE HORDE SUMMONED";
        __printf("rasterfall: heavy pursuit enemies summoned %d\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_FAST_HORDE_BUTTON) {
        int n = toy_game_spawn_horde_type(&session->game_state,
            TOY_GAME_ENEMY_PURSUIT_FAST, 2, 3, session->spawn_zones,
            session->spawn_count, HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 3000;
        session->banner_text = "RED RUNNER HORDE SUMMONED";
        __printf("rasterfall: fast pursuit enemies summoned %d\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ||
               it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON) {
        int type = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
                   TOY_GAME_ENEMY_SMOKER : TOY_GAME_ENEMY_CHARGER;
        int n = toy_game_spawn_horde_type(&session->game_state, type, 1, 1,
                                          session->spawn_zones,
                                          session->spawn_count,
                                          HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 2500;
        session->banner_text = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
            "SMOKER SUMMONED" : "CHARGER SUMMONED";
        __printf("rasterfall: special test enemy summoned type %d (%d)\n",
                  type, n);
    } else if (it->kind == TOY_MAP_PICKUP_AMMO) {
        toy_game_refill_ammo(&session->game_state);
    } else if (it->kind == TOY_MAP_PICKUP_WEAPON) {
        toy_game_equip_weapon(&session->game_state, it->weapon);
    } else {
        toy_game_equip_weapon(&session->game_state,
            it->kind == TOY_MAP_PICKUP_SMG ?
            TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN);
    }
}

static void session_update_manual_alarm(struct rasterfall_session *session,
                                        int dt_ms)
{
    if (!session->manual_alarm_on ||
        session->game_state.state != TOY_GAME_PLAYING) return;
    session->manual_alarm_timer -= dt_ms;
    if (session->manual_alarm_timer > 0) return;
    session->manual_alarm_timer += 1000;
    toy_game_spawn_horde(&session->game_state, 2, 3, session->spawn_zones,
                         session->spawn_count, HORDE_MIN_PLAYER_DIST);
}

void rasterfall_session_step(struct rasterfall_session *session,
                             struct camera *camera,
                             const struct rasterfall_command *command,
                             int dt_ms)
{
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (command->buttons & RASTERFALL_CMD_JUMP)
        session_jump_player(session, camera, command);
    if (!session->game_state.player_down)
        session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    if (command->buttons & RASTERFALL_CMD_TURN_LEFT)
        session->smooth_turn_remaining -= QUARTER_TURN;
    if (command->buttons & RASTERFALL_CMD_TURN_RIGHT)
        session->smooth_turn_remaining += QUARTER_TURN;
    session_update_smooth_turn(session, camera);
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    toy_game_update_player_ground(&session->game_state);
    /* Ground/platform resolution must precede weapon simulation: the visual
     * muzzle is derived from camera->y during this same tick. */
    session_sync_special_motion(session, camera);
    session->highlight_index = rasterfall_session_compute_highlight(session, camera);
    if (command->buttons & RASTERFALL_CMD_INTERACT)
        session_client_interact_banner(session);
    if (command->buttons & RASTERFALL_CMD_SHOVE)
        toy_game_shove(&session->game_state, camera->sy, camera->cy);
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session_near_ai(session, camera, &session->ai_revive_actor_index))
        session->ai_revive_active = 1;
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session->highlight_index >= 0 && !session->game_state.player_down)
        session_interact(session, &session->items[session->highlight_index]);
    if (session->ai_revive_active) {
        if (!session_near_ai(session, camera, NULL) || session->game_state.player_down) {
            session->ai_revive_active = 0;
            session->game_state.ai_revive_progress_ms = 0;
        } else if (toy_game_revive_actor(&session->game_state,
                                         session->ai_revive_actor_index,
                                         dt_ms)) {
            const struct toy_game_actor *revived =
                &session->game_state.actors[session->ai_revive_actor_index];
            session->ai_revive_active = 0;
            session->banner_ms = 1800;
            session->banner_text = revived->name;
        }
    }
    memset(keys, 0, sizeof(keys));
    if (command->buttons & RASTERFALL_CMD_RELOAD) keys[TOY_GAME_KEY_RELOAD] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_1) keys[TOY_GAME_KEY_SLOT_1] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_2) keys[TOY_GAME_KEY_SLOT_2] = 1;
    toy_game_update_held(&session->game_state, keys,
                         (command->buttons & RASTERFALL_CMD_FIRE) != 0,
                         command->fire_held, camera->sy, camera->cy, dt_ms);
    session_sync_special_motion(session, camera);
    session_update_manual_alarm(session, dt_ms);
    if (session->banner_ms > 0) {
        session->banner_ms -= dt_ms;
        if (session->banner_ms <= 0) {
            session->banner_ms = 0;
            session->banner_text = NULL;
        }
    }
}

void rasterfall_session_step_client(struct rasterfall_session *session,
                                    struct camera *camera,
                                    const struct rasterfall_command *command,
                                    int dt_ms)
{
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    struct toy_game_enemy enemies[TOY_GAME_MAX_ENEMIES];
    int enemy_count, kills, event_start, write, i;
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (command->buttons & RASTERFALL_CMD_JUMP)
        session_jump_player(session, camera, command);
    session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    if (command->buttons & RASTERFALL_CMD_TURN_LEFT)
        session->smooth_turn_remaining -= QUARTER_TURN;
    if (command->buttons & RASTERFALL_CMD_TURN_RIGHT)
        session->smooth_turn_remaining += QUARTER_TURN;
    session_update_smooth_turn(session, camera);
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    toy_game_update_player_ground(&session->game_state);
    session_sync_special_motion(session, camera);
    session->highlight_index = rasterfall_session_compute_highlight(session, camera);
    if (command->buttons & RASTERFALL_CMD_INTERACT)
        session_client_interact_banner(session);
    /* AI rescue remains host-authoritative, but keep the local action state so
     * the client can render the same progress bar while the host advances it.
     * The actor's authoritative progress arrives in the next snapshot. */
    if ((command->buttons & RASTERFALL_CMD_INTERACT) &&
        session_near_ai(session, camera, &session->ai_revive_actor_index))
        session->ai_revive_active = 1;
    if (session->ai_revive_active) {
        int index = session->ai_revive_actor_index;
        if (index < 0 || index >= TOY_GAME_MAX_ACTORS ||
            !session_near_ai(session, camera, NULL) ||
            session->game_state.player_down ||
            session->game_state.actors[index].state != TOY_GAME_ACTOR_DOWNED) {
            session->ai_revive_active = 0;
            session->game_state.ai_revive_progress_ms = 0;
        }
    }
    /* 交互由主机权威执行。客户端只发送 INTERACT 命令，等待主机快照
     * 回传拾取、救援、弹药、空气墙和刷怪结果，避免两端世界分叉。 */
    memset(keys, 0, sizeof(keys));
    if (command->buttons & RASTERFALL_CMD_RELOAD) keys[TOY_GAME_KEY_RELOAD] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_1) keys[TOY_GAME_KEY_SLOT_1] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_2) keys[TOY_GAME_KEY_SLOT_2] = 1;
    memcpy(enemies, session->game_state.enemies, sizeof(enemies));
    enemy_count = session->game_state.enemies_alive;
    kills = session->game_state.kills;
    event_start = session->game_state.event_count;
    toy_game_update_weapon_held(&session->game_state, keys,
                                (command->buttons & RASTERFALL_CMD_FIRE) != 0,
                                command->fire_held, camera->sy, camera->cy,
                                dt_ms);
    toy_game_update_player_motion(&session->game_state, dt_ms);
    memcpy(session->game_state.enemies, enemies, sizeof(enemies));
    session->game_state.enemies_alive = enemy_count;
    session->game_state.kills = kills;
    /* 预测射击仍保留开火/换弹音效，但击杀音效只由主机事件复制。 */
    write = event_start;
    for (i = event_start; i < session->game_state.event_count; i++)
        if (session->game_state.events[i] != TOY_GAME_EV_KILL)
            session->game_state.events[write++] = session->game_state.events[i];
    session->game_state.event_count = write;
    if (session->banner_ms > 0) {
        session->banner_ms -= dt_ms;
        if (session->banner_ms <= 0) {
            session->banner_ms = 0;
            session->banner_text = NULL;
        }
    }
}
