#include "tlibc_everything.h"
#include "math.h"
#include "rasterfall_ai_names.h"
#include "rasterfall_session.h"

#define INTERACT_AIM_CONE 784
#define HORDE_COUNT_MIN 15
#define HORDE_COUNT_MAX 20
#define HORDE_MIN_PLAYER_DIST 700
#define QUARTER_TURN 1611
#define SMOOTH_TURN_STEP 128
static const int hired_ai_positions[][2] = {
    { 1000, 0 }, { 0, -900 }, { -1000, 0 },
    { 1200, 900 }, { -1200, 900 }, { 0, 2100 },
    { 0, -300 }, { 1800, 900 }, { -1800, 900 }, { 2400, 0 },
    { -2400, 0 }, { 3000, 900 }, { -3000, 900 }, { 3600, 0 },
    { -3600, 0 }, { 4200, 900 }, { -4200, 900 }, { 4800, 0 },
    { -4800, 0 }, { 5400, 900 }, { -5400, 900 }, { 6000, 0 },
    { -6000, 0 }, { 6600, 900 }, { -6600, 900 }, { 7200, 0 },
    { -7200, 0 }, { 7800, 900 }, { -7800, 900 }, { 8400, 0 },
    { -8400, 0 }, { 9000, 900 }, { -9000, 900 }, { 9600, 0 }
};
static const int flag_colors[] = { 0x173A70, 0x9E302B, 0xC78A24, 0x2B765B,
                                   0x704A91, 0xB75A2C };
static const char *flag_names[] = { "TOYC", "GNU", "LLVM", "GCC", "NASA", "UNIX" };
static int session_is_developer_ai(const char *name)
{
    return name && (!strcmp(name, "DEV_GUNNER") ||
                    !strcmp(name, "PLATFORM_GUARD") ||
                    !strcmp(name, "HIT_TEST") || !strcmp(name, "ANIM_TEST") ||
                    !strcmp(name, "AK_TEST") || !strcmp(name, "AWP_TEST"));
}
static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it);
static int session_near_flag(const struct rasterfall_session *session,
                             const struct camera *camera);

static void session_down_ai(struct rasterfall_session *session, int index,
                            int x, int z)
{
    struct toy_game_actor *actor = &session->game_state.actors[index];
    toy_game_move_ai_actor(&session->game_state, index, x, z);
    actor->hp = 0;
    actor->state = TOY_GAME_ACTOR_DOWNED;
    toy_game_actor_set_animation(actor, TOY_GAME_ANIM_DEATH);
    actor->revive_progress_ms = 0;
}

static void session_set_flag_assignments(struct rasterfall_session *s, int fi)
{
    int i, n = 0;
    struct rasterfall_flag *f;
    if (fi < 0 || fi >= s->flag_count) return;
    f = &s->flags[fi];
    for (i = 0; i < TOY_GAME_MAX_ACTORS && n < 4; i++) {
        struct toy_game_actor *a = &s->game_state.actors[i];
        if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->base_core ||
            a->developer_only) continue;
        if (a->flag_index == fi)
            toy_game_assign_actor_deployment(&s->game_state, i,
                f->x + f->slot_offsets[n][0], f->z + f->slot_offsets[n][1], fi), n++;
    }
}

static int session_flag_assigned_count(const struct rasterfall_session *s, int fi)
{
    int i, count = 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        const struct toy_game_actor *a = &s->game_state.actors[i];
        if (a->active && a->kind == TOY_GAME_ACTOR_AI && !a->base_core &&
            !a->developer_only && a->flag_index == fi) count++;
    }
    return count;
}

static int session_assign_actor_to_flag(struct rasterfall_session *s,
                                        int actor_index, int fi)
{
    int slot;
    if (fi < 0 || fi >= s->flag_count) return 0;
    slot = session_flag_assigned_count(s, fi);
    if (slot >= 4) return 0;
    return toy_game_assign_actor_deployment(&s->game_state, actor_index,
        s->flags[fi].x + s->flags[fi].slot_offsets[slot][0],
        s->flags[fi].z + s->flags[fi].slot_offsets[slot][1], fi);
}

/* The assignment list is deliberately stable: assigned actors first, then
 * available actors.  This also makes a long list easy to scan while flags
 * are being configured. */
static int session_collect_assignable(const struct rasterfall_session *s,
                                      int fi, int *indices)
{
    int pass, i, count = 0;
    for (pass = 0; pass < 2; pass++)
        for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
            const struct toy_game_actor *a = &s->game_state.actors[i];
            int assigned = a->flag_index == fi;
            int assigned_elsewhere = a->flag_index >= 0 && !assigned;
            if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->base_core ||
                a->developer_only || assigned_elsewhere ||
                (pass == 0 ? !assigned : assigned)) continue;
            indices[count++] = i;
        }
    return count;
}

static void session_init_flag(struct rasterfall_session *s, int fi, int x, int z)
{
    struct rasterfall_flag *f = &s->flags[fi];
    memset(f, 0, sizeof(*f)); f->active = 1; f->x = x; f->z = z;
    f->color = flag_colors[fi % (int)(sizeof(flag_colors)/sizeof(flag_colors[0]))];
    f->carrier_id = -1;
    strncpy(f->label, flag_names[fi % (int)(sizeof(flag_names)/sizeof(flag_names[0]))], 4);
    f->label[4] = 0;
    /* Four corners of a deliberately compact, adjustable square. */
    f->slot_offsets[0][0] = 420;  f->slot_offsets[0][1] = 420;
    f->slot_offsets[1][0] = -420; f->slot_offsets[1][1] = 420;
    f->slot_offsets[2][0] = -420; f->slot_offsets[2][1] = -420;
    f->slot_offsets[3][0] = 420;  f->slot_offsets[3][1] = -420;
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
            actor->base_core ||
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
    toy_game_rebuild_navigation(&session->game_state);
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
    /* The safe room is open to the player through its doorway, but its whole
     * footprint is an enemy-forbidden area.  Register it even in the endless
     * director mode; toy_game_set_campaign also rebuilds navigation. */
    toy_game_set_campaign(&session->game_state, session->safe_rooms,
                          session->level.safe_count, session->spawn_zones,
                          session->spawn_count);
    /* The game starts directly in the ordinary endless wave director.  There
     * are no safe rooms, capture stages, alarms, or objective transitions. */
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
        session->game_state.actors[actor_index].developer_only =
            session_is_developer_ai(spawn->name);
        if (spawn->weapon >= 0 &&
            spawn->weapon < TOY_GAME_WEAPON_COUNT) {
            toy_game_set_ai_weapon(&session->game_state, actor_index,
                                   spawn->weapon);
        }
        if (!strcmp(spawn->name, "BASE")) {
            struct toy_game_actor *base =
                &session->game_state.actors[actor_index];
            const struct toy_game_weapon_info *pistol =
                toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
            base->base_core = 1;
            base->max_hp = TOY_CONFIG_BASE_HP;
            base->hp = TOY_CONFIG_BASE_HP;
            base->state = TOY_GAME_ACTOR_ALIVE;
            base->slots[0].weapon = TOY_GAME_WEAPON_PISTOL;
            base->slots[0].mag = pistol->mag_size;
            base->slots[0].reserve = TOY_GAME_AMMO_INFINITE;
            base->current_slot = 0;
            session->game_state.base_actor_index = actor_index;
            session->game_state.base_regen_timer_ms =
                TOY_CONFIG_BASE_REGEN_MS;
        }
        if (!strcmp(spawn->name, "HIT_TEST")) {
            session->game_state.actors[actor_index].fire_enabled = 0;
            session->game_state.actors[actor_index].hit_test_dummy = 1;
        }
        if (!strcmp(spawn->name, "ANIM_TEST")) {
            session->game_state.actors[actor_index].fire_enabled = 0;
            session->game_state.actors[actor_index].animation_demo = 1;
            session->game_state.actors[actor_index].animation_demo_elapsed_ms = 0;
            toy_game_actor_set_animation(
                &session->game_state.actors[actor_index],
                TOY_GAME_ANIM_IDLE);
        }
        if (spawn->downed) session_down_ai(session, actor_index, spawn->x, spawn->z);
    }
    /* Registration is separate from simulation: the existing teammate
     * executor remains authoritative while policies are introduced. */
    rasterfall_ai_registry_init(&session->ai_registry);
    rasterfall_ai_registry_sync(&session->ai_registry,
                                &session->game_state);
    if (session->managed_ai_enabled)
        rasterfall_ai_registry_add(
            &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX,
            RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER, 100,
            RASTERFALL_AI_POLICY_MANAGED_SIMPLE);
    session->flag_count = 1;
    session->carried_flag = -1;
    session->assignment_flag = 0;
    session_init_flag(session, 0, camera->x, camera->z);
    for (i = 0; i < 3; i++)
        if (session->game_state.actors[i].active &&
            session->game_state.actors[i].kind == TOY_GAME_ACTOR_AI)
            toy_game_assign_actor_deployment(&session->game_state, i,
                camera->x + session->flags[0].slot_offsets[i][0],
                camera->z + session->flags[0].slot_offsets[i][1], 0);
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    toy_game_set_player_pitch(&session->game_state, camera->pitch_sy,
                              camera->pitch_cy, camera->y);
    session->banner_ms = 0;
    session->banner_text = NULL;
    session->manual_alarm_on = 0;
    session->manual_alarm_timer = 1000;
    session->highlight_index = -1;
    session->smooth_turn_remaining = 0;
    session->ai_revive_active = 0;
    session->ai_revive_actor_index = -1;
    session->shop_open = 0;
    session->shop_page = 0;
    session->shop_selected = 0;
    session->shop_nav_selected = 0;
    session->shop_scroll = 0;
    session->shop_request_only = 0;
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
    if (session->game_state.player_control_disabled)
        return;
    int dx = (camera->sy * command->move_forward +
              camera->cy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int dz = (camera->cy * command->move_forward -
              camera->sy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!toy_game_position_blocked_at_height(&session->game_state, next_x,
                                             camera->z, RASTERFALL_PLAYER_RADIUS,
                                             session->game_state.player_ground_y +
                                             session->game_state.player_airborne_y))
        camera->x = next_x;
    if (!toy_game_position_blocked_at_height(&session->game_state, camera->x,
                                             next_z, RASTERFALL_PLAYER_RADIUS,
                                             session->game_state.player_ground_y +
                                             session->game_state.player_airborne_y))
        camera->z = next_z;
}

static void session_move_remote_player(struct rasterfall_session *session,
                                       struct camera *camera,
                                       const struct rasterfall_command *command,
                                       int height)
{
    int dx = (camera->sy * command->move_forward +
              camera->cy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int dz = (camera->cy * command->move_forward -
              camera->sy * command->move_strafe) * RASTERFALL_MOVE_STEP / 1024;
    int next_x = camera->x + dx;
    int next_z = camera->z + dz;
    if (!toy_game_position_blocked_at_height(&session->game_state, next_x,
                                             camera->z, RASTERFALL_PLAYER_RADIUS,
                                             height))
        camera->x = next_x;
    if (!toy_game_position_blocked_at_height(&session->game_state, camera->x,
                                             next_z, RASTERFALL_PLAYER_RADIUS,
                                             height))
        camera->z = next_z;
}

static void session_jump_player(struct rasterfall_session *session,
                                struct camera *camera,
                                const struct rasterfall_command *command)
{
    (void)camera;
    toy_game_jump_with_velocity(&session->game_state,
                                command->jump_dx, command->jump_dz);
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
    /* A remote player's shop is a local UI on that player's machine.  The
     * host still processes shop requests separately, but must not open its
     * own armory when a client walks up to the same pickup. */
    if (index >= 0 && session->items[index].kind != TOY_MAP_PICKUP_SHOP)
        session_interact(session, &session->items[index]);
}

void rasterfall_session_toggle_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id)
{
    int i;
    if (!session || !camera || session->game_state.state != TOY_GAME_PLAYING)
        return;
    for (i = 0; i < session->flag_count; i++) {
        if (!session->flags[i].carried ||
            session->flags[i].carrier_id != player_id) continue;
        session->flags[i].carried = 0;
        session->flags[i].carrier_id = -1;
        session->flags[i].x = camera->x;
        session->flags[i].z = camera->z;
        session_set_flag_assignments(session, i);
        return;
    }
    i = session_near_flag(session, camera);
    if (i < 0 || session->flags[i].carried) return;
    session->flags[i].carried = 1;
    session->flags[i].carrier_id = player_id;
}

void rasterfall_session_update_flag_remote(struct rasterfall_session *session,
                                           const struct camera *camera,
                                           int player_id)
{
    int i;
    if (!session || !camera) return;
    for (i = 0; i < session->flag_count; i++)
        if (session->flags[i].carried &&
            session->flags[i].carrier_id == player_id) {
            session->flags[i].x = camera->x;
            session->flags[i].z = camera->z;
            session_set_flag_assignments(session, i);
        }
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

int rasterfall_session_paid_revive(struct rasterfall_session *session,
                                   struct camera *camera)
{
    struct toy_game *game;
    if (!session || !camera) return 0;
    game = &session->game_state;
    if (game->state != TOY_GAME_PLAYING || !game->player_down ||
        game->money < RASTERFALL_PAID_REVIVE_COST)
        return 0;
    game->money -= RASTERFALL_PAID_REVIVE_COST;
    toy_game_clear_player_special_control(game, 0);
    game->player_down = 0;
    game->hp = TOY_GAME_REVIVE_HP;
    game->player_revive_progress_ms = 0;
    game->player_control_disabled = 0;
    camera->x = session->level.start_x;
    camera->z = session->level.start_z;
    camera->sy = 0;
    camera->cy = 1024;
    camera->pitch_sy = 0;
    camera->pitch_cy = 1024;
    camera->y = 0;
    game->px = camera->x;
    game->pz = camera->z;
    toy_game_animation_set(&game->animation, TOY_GAME_ANIM_REVIVE);
    toy_game_emit_event(game, TOY_GAME_EV_REVIVE);
    toy_game_emit_event(game, TOY_GAME_EV_ACTOR_REVIVE);
    session->banner_ms = 1800;
    session->banner_success = 1;
    session->banner_text = "REVIVED -$20";
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
    if (it->kind == TOY_MAP_PICKUP_SHOP) {
        session->shop_open = 1;
        session->shop_page = 0;
        session->shop_selected = 0;
        session->shop_nav_selected = 0;
        session->game_state.player_control_disabled = 1;
        session->banner_ms = 0;
        session->banner_text = NULL;
        return;
    }
    session->banner_success = 1;
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
    else if (it->kind == TOY_MAP_PICKUP_TANK_BUTTON)
        session->banner_text = "TANK SUMMONED";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON)
        session->banner_text = "ATTACK POINTS X2";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON)
        session->banner_text = "ATTACK POINTS X3";
    else if (it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON)
        session->banner_text = "ATTACK POINTS X4";
    else if (it->kind == TOY_MAP_PICKUP_AMMO)
        session->banner_text = "AMMO REFILLED";
    else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
             it->kind == TOY_MAP_PICKUP_SMG ||
             it->kind == TOY_MAP_PICKUP_SHOTGUN ||
        it->kind == TOY_MAP_PICKUP_THROWABLE)
        session->banner_text = "WEAPON PICKED UP";
    else if (it->kind == TOY_MAP_PICKUP_PILL)
        session->banner_text = "PILL PICKED UP";
    else
        session->banner_text = "INTERACTION SENT TO HOST";
}

static void session_interact(struct rasterfall_session *session,
                             struct rasterfall_interactable *it)
{
    toy_game_emit_event(&session->game_state, TOY_GAME_EV_BUTTON);
    session->banner_success = 1;
    if (it->kind == TOY_MAP_PICKUP_SMG ||
        it->kind == TOY_MAP_PICKUP_SHOTGUN ||
        it->kind == TOY_MAP_PICKUP_AMMO ||
        it->kind == TOY_MAP_PICKUP_WEAPON ||
        it->kind == TOY_MAP_PICKUP_THROWABLE)
        toy_game_emit_event(&session->game_state, TOY_GAME_EV_PICKUP);
    if (it->kind == TOY_MAP_PICKUP_SHOP) {
        session->shop_open = 1;
        session->shop_page = 0;
        session->shop_selected = 0;
        session->banner_ms = 0;
        session->banner_text = NULL;
        session->game_state.player_control_disabled = 1;
        return;
    }
    if (it->kind == TOY_MAP_PICKUP_BUTTON) {
        int n;
        session->banner_ms = 3500;
        session->banner_text = "HORDE SUMMONED - THEY WILL FIND YOU";
        n = toy_game_spawn_horde(&session->game_state, HORDE_COUNT_MIN,
                                 HORDE_COUNT_MAX, session->spawn_zones,
                                 session->spawn_count, HORDE_MIN_PLAYER_DIST);
        __printf("rasterfall: horde summoned %d tracking enemies\n", n);
    } else if (it->kind == TOY_MAP_PICKUP_WAVE_SKIP_BUTTON) {
        session->banner_ms = 1200;
        session->banner_text = toy_game_skip_wave_rest(&session->game_state) ?
            "NEXT WAVE STARTING" : "WAVE ALREADY STARTED";
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
               it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ||
               it->kind == TOY_MAP_PICKUP_TANK_BUTTON) {
        int type = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
                   TOY_GAME_ENEMY_SMOKER :
                   it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ?
                   TOY_GAME_ENEMY_CHARGER : TOY_GAME_ENEMY_TANK;
        int n = toy_game_spawn_horde_type(&session->game_state, type, 1, 1,
                                          session->spawn_zones,
                                          session->spawn_count,
                                          HORDE_MIN_PLAYER_DIST);
        session->banner_ms = 2500;
        session->banner_text = it->kind == TOY_MAP_PICKUP_SMOKER_BUTTON ?
            "SMOKER SUMMONED" :
            it->kind == TOY_MAP_PICKUP_CHARGER_BUTTON ?
            "CHARGER SUMMONED" : "TANK SUMMONED";
        __printf("rasterfall: special test enemy summoned type %d (%d)\n",
                  type, n);
    } else if (it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON ||
               it->kind == TOY_MAP_PICKUP_ATTACK_X4_BUTTON) {
        int multiplier = it->kind == TOY_MAP_PICKUP_ATTACK_X2_BUTTON ? 2 :
                         it->kind == TOY_MAP_PICKUP_ATTACK_X3_BUTTON ? 3 : 4;
        toy_game_set_wave_attack_multiplier(&session->game_state, multiplier);
        session->banner_ms = 2000;
        session->banner_text = multiplier == 2 ? "ATTACK POINTS X2" :
            multiplier == 3 ? "ATTACK POINTS X3" : "ATTACK POINTS X4";
    } else if (it->kind == TOY_MAP_PICKUP_AMMO) {
        toy_game_refill_ammo(&session->game_state);
    } else if (it->kind == TOY_MAP_PICKUP_MONEY_BUTTON) {
        session->game_state.money += 500;
        session->banner_ms = 2000;
        session->banner_text = "MONEY +500";
    } else if (it->kind == TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON) {
        int cleared = toy_game_clear_hired_ai(&session->game_state);
        session->banner_ms = 2000;
        session->banner_text = cleared > 0 ?
            "HIRED AI CLEARED" : "NO HIRED AI";
        session->banner_success = cleared > 0;
    } else if (it->kind == TOY_MAP_PICKUP_WEAPON ||
        it->kind == TOY_MAP_PICKUP_THROWABLE ||
        it->kind == TOY_MAP_PICKUP_PILL) {
        if (it->kind == TOY_MAP_PICKUP_THROWABLE ||
            it->kind == TOY_MAP_PICKUP_PILL) {
            toy_game_equip_weapon(&session->game_state, it->weapon);
            return;
        }
        if (toy_game_weapon_unlocked(&session->game_state, it->weapon))
            toy_game_equip_weapon(&session->game_state, it->weapon);
        else {
            session->banner_ms = 2000;
            session->banner_success = 0;
            session->banner_text = "LOCKED - BUY IT IN THE ARMORY";
        }
    } else {
        int weapon = it->kind == TOY_MAP_PICKUP_SMG ?
            TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_SHOTGUN;
        if (toy_game_weapon_unlocked(&session->game_state, weapon))
            toy_game_equip_weapon(&session->game_state, weapon);
        else {
            session->banner_ms = 2000;
            session->banner_success = 0;
            session->banner_text = "LOCKED - BUY IT IN THE ARMORY";
        }
    }
}

static int session_near_flag(const struct rasterfall_session *s,
                             const struct camera *camera)
{
    int i, best = -1;
    long best_d2 = 0;
    for (i = 0; i < s->flag_count; i++) {
        long dx, dz, d2;
        if (!s->flags[i].active || s->flags[i].carried) continue;
        dx = (long)camera->x - s->flags[i].x;
        dz = (long)camera->z - s->flags[i].z;
        d2 = dx * dx + dz * dz;
        if (d2 <= (long)RASTERFALL_INTERACT_RANGE * RASTERFALL_INTERACT_RANGE &&
            (best < 0 || d2 < best_d2)) { best = i; best_d2 = d2; }
    }
    return best;
}

static void session_toggle_flag(struct rasterfall_session *s,
                                struct camera *camera)
{
    int i = s->carried_flag;
    if (i >= 0) {
        s->flags[i].carried = 0;
        s->flags[i].carrier_id = -1;
        s->flags[i].x = camera->x;
        s->flags[i].z = camera->z;
        s->carried_flag = -1;
        session_set_flag_assignments(s, i);
        s->banner_text = "FLAG PLANTED"; s->banner_ms = 1400;
        return;
    }
    i = session_near_flag(s, camera);
    if (i < 0) return;
    s->flags[i].carried = 1; s->flags[i].carrier_id = 0;
    s->carried_flag = i;
    s->banner_text = "FLAG CARRIED"; s->banner_ms = 1400;
}

static int session_hired_count(const struct rasterfall_session *session)
{
    int i, count = 0;
    for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++)
        if (session->game_state.actors[i].active &&
            session->game_state.actors[i].hired) count++;
    return count;
}

static int session_hire_ai(struct rasterfall_session *session, int weapon)
{
    int hired, position, price, actor_id;
    uint64_t raw;
    char name[TOY_GAME_MAX_NAME];
    hired = session_hired_count(session);
    position = 3 + hired;
    if (position >= (int)(sizeof(hired_ai_positions) /
                          sizeof(hired_ai_positions[0]))) return -1;
    if (!toy_game_weapon_is_valid(weapon)) return 0;
    /* 雇佣价格 = 一级 AI 基础价 + 武器价（按玩家武器价的 10 倍）；
     * 手枪没有玩家购买价，按 100 元武器价计算。 */
    price = TOY_CONFIG_AI_HIRE_PRICE +
            (weapon == TOY_GAME_WEAPON_PISTOL ?
             TOY_CONFIG_AI_HIRE_PISTOL_WEAPON_PRICE :
             toy_game_weapon_price(weapon) *
             TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER);
    if (session->game_state.money < price) return 0;
    raw = session->game_state.rng;
    raw ^= raw >> 12; raw ^= raw << 25; raw ^= raw >> 27;
    session->game_state.rng = raw;
    strcpy(name, rasterfall_hired_ai_names[
        (int)((raw >> 32) % RASTERFALL_HIRED_AI_NAME_COUNT)]);
    actor_id = toy_game_add_hired_ai(&session->game_state, weapon,
                                     hired_ai_positions[position][0],
                                     hired_ai_positions[position][1], name);
    if (actor_id < 0) return -1;
    session->game_state.money -= price;
    /* A newly hired teammate immediately occupies the first free flag slot. */
    {
        int fi;
        for (fi = 0; fi < session->flag_count; fi++)
            if (session_assign_actor_to_flag(session, actor_id - 1, fi)) break;
    }
    return 1;
}

static int session_collect_hired_ai(const struct rasterfall_session *session,
                                    int *indices)
{
    int i, count = 0;
    for (i = 0; i < TOY_GAME_REMOTE_ACTOR_BASE; i++) {
        const struct toy_game_actor *a = &session->game_state.actors[i];
        if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired) continue;
        indices[count++] = i;
    }
    return count;
}

static int session_change_ai_weapon(struct rasterfall_session *session,
                                    int actor_index, int weapon)
{
    int price;
    struct toy_game_actor *a;
    if (!session || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS ||
        !toy_game_weapon_is_valid(weapon)) return 0;
    a = &session->game_state.actors[actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI || !a->hired) return 0;
    price = weapon == TOY_GAME_WEAPON_PISTOL ? 0 :
            toy_game_weapon_price(weapon) *
            TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER;
    if (session->game_state.money < price) return 0;
    if (!toy_game_set_ai_weapon(&session->game_state, actor_index, weapon)) return 0;
    session->game_state.money -= price;
    return 1;
}

static int session_buy_flag(struct rasterfall_session *s)
{
    const int price = 250;
    int fi;
    if (s->flag_count >= RASTERFALL_MAX_FLAGS || s->game_state.money < price)
        return 0;
    fi = s->flag_count++;
    session_init_flag(s, fi, s->game_state.px, s->game_state.pz);
    s->game_state.money -= price;
    return 1;
}

int rasterfall_session_shop_can(const struct rasterfall_session *session,
                                const struct rasterfall_shop_request *request,
                                int *price)
{
    const struct toy_game *g;
    const struct toy_game_actor *actor;
    int value = 0;
    if (price) *price = 0;
    if (!session || !request || session->game_state.state != TOY_GAME_PLAYING)
        return 0;
    g = &session->game_state;
    if (request->action == RASTERFALL_SHOP_BUY_WEAPON) {
        int weapon = request->item;
        if (!toy_game_weapon_is_valid(weapon) ||
            weapon == TOY_GAME_WEAPON_PISTOL) return 0;
        if (toy_game_weapon_unlocked(g, weapon)) {
            if (weapon == TOY_GAME_WEAPON_PILL &&
                g->slots[3].mag >= TOY_GAME_PILL_MAX) return 0;
            if ((weapon == TOY_GAME_WEAPON_BOMB ||
                 weapon == TOY_GAME_WEAPON_MOLOTOV) &&
                g->slots[2].mag >= TOY_GAME_THROWABLE_MAX) return 0;
            value = (weapon == TOY_GAME_WEAPON_BOMB ||
                     weapon == TOY_GAME_WEAPON_MOLOTOV ||
                     weapon == TOY_GAME_WEAPON_PILL) ?
                    toy_game_weapon_price(weapon) : 0;
        } else value = toy_game_weapon_price(weapon);
    } else if (request->action == RASTERFALL_SHOP_HIRE_AI) {
        if (!toy_game_weapon_is_valid(request->item) ||
            3 + session_hired_count(session) >=
            (int)(sizeof(hired_ai_positions) / sizeof(hired_ai_positions[0])))
            return 0;
        value = TOY_CONFIG_AI_HIRE_PRICE +
            (request->item == TOY_GAME_WEAPON_PISTOL ?
             TOY_CONFIG_AI_HIRE_PISTOL_WEAPON_PRICE :
             toy_game_weapon_price(request->item) *
             TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER);
    } else if (request->action == RASTERFALL_SHOP_BUY_FLAG) {
        if (session->flag_count >= RASTERFALL_MAX_FLAGS) return 0;
        value = 250;
    } else if (request->action == RASTERFALL_SHOP_ASSIGN_AI) {
        if (request->item < 0 || request->item >= session->flag_count ||
            request->target_actor < 0 ||
            request->target_actor >= TOY_GAME_MAX_ACTORS) return 0;
        actor = &g->actors[request->target_actor];
        if (!actor->active || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->base_core || actor->developer_only) return 0;
    } else if (request->action == RASTERFALL_SHOP_UPGRADE_AI) {
        if (request->target_actor < 0 ||
            request->target_actor >= TOY_GAME_MAX_ACTORS) return 0;
        actor = &g->actors[request->target_actor];
        if (!actor->active || !actor->hired ||
            actor->kind != TOY_GAME_ACTOR_AI ||
            actor->class_id >= TOY_GAME_AI_LEVEL_3) return 0;
        value = actor->class_id == TOY_GAME_AI_LEVEL_1 ?
            TOY_CONFIG_AI_LEVEL_2_PRICE : TOY_CONFIG_AI_LEVEL_3_PRICE;
    } else if (request->action == RASTERFALL_SHOP_CHANGE_AI_WEAPON) {
        if (request->target_actor < 0 ||
            request->target_actor >= TOY_GAME_MAX_ACTORS ||
            !toy_game_weapon_is_valid(request->arg)) return 0;
        actor = &g->actors[request->target_actor];
        if (!actor->active || !actor->hired ||
            actor->kind != TOY_GAME_ACTOR_AI) return 0;
        value = request->arg == TOY_GAME_WEAPON_PISTOL ? 0 :
            toy_game_weapon_price(request->arg) *
            TOY_CONFIG_AI_HIRE_WEAPON_PRICE_MULTIPLIER;
    } else return 0;
    if (price) *price = value;
    return g->money >= value;
}

int rasterfall_session_shop_execute(struct rasterfall_session *session,
                                    const struct rasterfall_shop_request *request)
{
    int result = 0, price;
    if (!rasterfall_session_shop_can(session, request, &price)) return 0;
    if (request->action == RASTERFALL_SHOP_BUY_WEAPON)
        result = toy_game_buy_weapon(&session->game_state, request->item);
    else if (request->action == RASTERFALL_SHOP_HIRE_AI)
        result = session_hire_ai(session, request->item);
    else if (request->action == RASTERFALL_SHOP_BUY_FLAG)
        result = session_buy_flag(session);
    else if (request->action == RASTERFALL_SHOP_ASSIGN_AI) {
        struct toy_game_actor *actor =
            &session->game_state.actors[request->target_actor];
        int old_flag = actor->flag_index;
        if (old_flag == request->item) {
            actor->flag_index = -1;
            result = 1;
        } else {
            result = session_assign_actor_to_flag(session,
                                                   request->target_actor,
                                                   request->item);
            if (result && old_flag >= 0) actor->flag_index = request->item;
        }
        if (result) session_set_flag_assignments(session, request->item);
    } else if (request->action == RASTERFALL_SHOP_UPGRADE_AI)
        result = toy_game_upgrade_ai(&session->game_state,
                                     request->target_actor);
    else if (request->action == RASTERFALL_SHOP_CHANGE_AI_WEAPON)
        result = session_change_ai_weapon(session, request->target_actor,
                                          request->arg);
    (void)price;
    if (result > 0) {
        session->banner_success = 1;
        session->banner_ms = 1600;
        session->banner_text = request->action == RASTERFALL_SHOP_BUY_FLAG ?
            "FLAG PURCHASED" : request->action == RASTERFALL_SHOP_HIRE_AI ?
            "AI HIRED" : request->action == RASTERFALL_SHOP_UPGRADE_AI ?
            "AI UPGRADED" : "WEAPON PURCHASED";
    }
    return result;
}

void rasterfall_session_shop_input(struct rasterfall_session *session,
                                   int up, int down, int left, int right,
                                   int enter, int esc)
{
    int weapon;
    if (!session || !session->shop_open) return;
    if (esc) {
        if (session->shop_page == 8) {
            session->shop_page = 7;
            session->shop_selected = 0;
        } else if (session->shop_page) {
            session->shop_page = 0;
            session->shop_selected = 0;
        } else {
            session->shop_open = 0;
            session->game_state.player_control_disabled = 0;
        }
        return;
    }
    if (!session->shop_page) {
        if (up) session->shop_nav_selected =
            (session->shop_nav_selected + 6) % 7;
        if (down) session->shop_nav_selected =
            (session->shop_nav_selected + 1) % 7;
        if (enter) {
            session->shop_page = session->shop_nav_selected == 4 ? 6 :
                                 session->shop_nav_selected == 5 ? 7 :
                                 session->shop_nav_selected == 6 ? 9 :
                                 session->shop_nav_selected + 1;
            session->shop_selected = 0;
        }
        return;
    }
    if (session->shop_page == 9) return;
    if (session->shop_page == 3) {
        if (enter) {
            if (session->shop_request_only) {
                session->banner_success = 1;
                session->banner_ms = 1600;
                session->banner_text = "FLAG PURCHASED";
                return;
            }
            struct rasterfall_shop_request request = {
                RASTERFALL_SHOP_BUY_FLAG, 0, -1, 0
            };
            int result = session->shop_request_only ? 0 :
                         rasterfall_session_shop_execute(session, &request);
            session->banner_ms = 1600; session->banner_success = result;
            session->banner_text = result ? "FLAG PURCHASED" : "NOT ENOUGH MONEY";
        }
        return;
    }
    if (session->shop_page == 4) {
        if (session->flag_count <= 0) return;
        if (up) session->shop_selected =
            (session->shop_selected + session->flag_count - 1) % session->flag_count;
        if (down) session->shop_selected =
            (session->shop_selected + 1) % session->flag_count;
        if (enter) {
            session->assignment_flag = session->shop_selected;
            session->shop_page = 5;
            session->shop_selected = 0;
            session->shop_scroll = 0;
        }
        return;
    }
    if (session->shop_page == 5) {
        int count, indices[TOY_GAME_MAX_ACTORS];
        count = session_collect_assignable(session, session->assignment_flag, indices);
        if (count <= 0) return;
        if (up) session->shop_selected = (session->shop_selected + count - 1) % count;
        if (down) session->shop_selected = (session->shop_selected + 1) % count;
        if (session->shop_selected < session->shop_scroll)
            session->shop_scroll = session->shop_selected;
        if (session->shop_selected >= session->shop_scroll + 6)
            session->shop_scroll = session->shop_selected - 5;
        if (enter) {
            struct toy_game_actor *a = &session->game_state.actors[indices[session->shop_selected]];
            if (session->shop_request_only) {
                session->banner_success = 1;
                session->banner_ms = 1400;
                session->banner_text = "TEAM ASSIGNMENT UPDATED";
                return;
            }
            {
                struct rasterfall_shop_request request = {
                    RASTERFALL_SHOP_ASSIGN_AI, session->assignment_flag,
                    indices[session->shop_selected], 0
                };
                int was_assigned = a->flag_index == session->assignment_flag;
                int result = session->shop_request_only ? 1 :
                    rasterfall_session_shop_execute(session, &request);
                if (was_assigned && result) {
                    session->banner_success = 1;
                    session->banner_ms = 1400;
                    session->banner_text = "TEAMMATE REMOVED";
                } else if (!result) {
                    session->banner_ms = 1800;
                    session->banner_success = 0;
                    session->banner_text = "FLAG LIMIT: 4 AI";
                } else {
                    session->banner_ms = 1400;
                    session->banner_success = 1;
                    session->banner_text = "TEAM ASSIGNMENT UPDATED";
                }
            }
        }
        return;
    }
    if (session->shop_page == 6) {
        int indices[TOY_GAME_MAX_ACTORS], count;
        count = session_collect_hired_ai(session, indices);
        if (count <= 0) return;
        if (up) session->shop_selected =
            (session->shop_selected + count - 1) % count;
        if (down) session->shop_selected =
            (session->shop_selected + 1) % count;
        if (session->shop_selected >= count) session->shop_selected = 0;
        if (enter && !session->shop_request_only) {
            struct rasterfall_shop_request request = {
                RASTERFALL_SHOP_UPGRADE_AI, 0,
                indices[session->shop_selected], 0
            };
            int result = rasterfall_session_shop_execute(session, &request);
            session->banner_success = result;
            session->banner_ms = 1600;
            session->banner_text = result ? "AI UPGRADED" : "NOT ENOUGH MONEY";
        }
        return;
    }
    if (session->shop_page == 7) {
        int indices[TOY_GAME_MAX_ACTORS], count;
        count = session_collect_hired_ai(session, indices);
        if (count <= 0) return;
        if (up) session->shop_selected = (session->shop_selected + count - 1) % count;
        if (down) session->shop_selected = (session->shop_selected + 1) % count;
        if (enter) {
            session->assignment_flag = indices[session->shop_selected];
            session->shop_page = 8;
            session->shop_selected = 0;
        }
        return;
    }
    if (session->shop_page == 8) {
        static const int weapons[] = { TOY_GAME_WEAPON_PISTOL,
            TOY_GAME_WEAPON_SMG, TOY_GAME_WEAPON_SHOTGUN,
            TOY_GAME_WEAPON_AK, TOY_GAME_WEAPON_AWP };
        if (up) session->shop_selected = (session->shop_selected + 4) % 5;
        if (down) session->shop_selected = (session->shop_selected + 1) % 5;
        if (enter) {
            struct rasterfall_shop_request request = {
                RASTERFALL_SHOP_CHANGE_AI_WEAPON, 0,
                session->assignment_flag, weapons[session->shop_selected]
            };
            int result = session->shop_request_only ? 1 :
                rasterfall_session_shop_execute(session, &request);
            session->banner_success = result;
            session->banner_ms = 1600;
            session->banner_text = result ? "AI WEAPON CHANGED" : "NOT ENOUGH MONEY";
        }
        return;
    }
    if (session->shop_page == 1) {
        int count = 8, selected = session->shop_selected;
        (void)left; (void)right;
        if (up) selected = (selected + count - 1) % count;
        if (down) selected = (selected + 1) % count;
        session->shop_selected = selected;
    } else {
        if (up) session->shop_selected = (session->shop_selected + 4) % 5;
        if (down) session->shop_selected = (session->shop_selected + 1) % 5;
    }
    if (enter) {
        if (session->shop_request_only) {
            session->banner_success = 1;
            session->banner_ms = 2000;
            session->banner_text = session->shop_page == 1 ?
                "WEAPON PURCHASED" : "AI HIRED";
            return;
        }
        if (session->shop_page == 1) {
            static const int weapons[] = { TOY_GAME_WEAPON_SMG,
                TOY_GAME_WEAPON_SHOTGUN, TOY_GAME_WEAPON_AK,
                TOY_GAME_WEAPON_AWP, TOY_GAME_WEAPON_AXE,
                TOY_GAME_WEAPON_BOMB, TOY_GAME_WEAPON_MOLOTOV,
                TOY_GAME_WEAPON_PILL };
            weapon = weapons[session->shop_selected];
            {
                struct rasterfall_shop_request request = {
                    RASTERFALL_SHOP_BUY_WEAPON, weapon, -1, 0
                };
                int result = rasterfall_session_shop_execute(session, &request);
                session->banner_ms = 2000;
                session->banner_success = result > 0;
                session->banner_text = result > 0 ?
                    (result == 2 ? "WEAPON UNLOCKED" : "WEAPON EQUIPPED") :
                    "NOT ENOUGH MONEY";
            }
        } else {
            weapon = -1;
            {
                struct rasterfall_shop_request request = {
                    RASTERFALL_SHOP_HIRE_AI, session->shop_selected, -1, 0
                };
                int result = rasterfall_session_shop_execute(session, &request);
                session->banner_ms = 2000;
                session->banner_success = result > 0;
                session->banner_text = result > 0 ? "AI HIRED" :
                    result < 0 ? "NO AVAILABLE AI POSITION" :
                    "NOT ENOUGH MONEY";
            }
        }
    }
}

int rasterfall_session_shop_request(struct rasterfall_session *session,
                                    int action, int item, int arg)
{
    struct rasterfall_shop_request request;
    request.action = action;
    request.item = item;
    request.target_actor = action == RASTERFALL_SHOP_ASSIGN_AI ||
                           action == RASTERFALL_SHOP_UPGRADE_AI ||
                           action == RASTERFALL_SHOP_CHANGE_AI_WEAPON ?
                           (action == RASTERFALL_SHOP_CHANGE_AI_WEAPON ?
                            item : arg) : -1;
    request.arg = action == RASTERFALL_SHOP_CHANGE_AI_WEAPON ? arg : 0;
    if (action == RASTERFALL_SHOP_ASSIGN_AI) {
        request.item = item;
        request.target_actor = arg;
    } else if (action == RASTERFALL_SHOP_UPGRADE_AI) {
        request.target_actor = item;
    } else if (action == RASTERFALL_SHOP_CHANGE_AI_WEAPON) {
        request.target_actor = item;
        request.arg = arg;
    }
    return rasterfall_session_shop_execute(session, &request);
}

int rasterfall_session_shop_actor_at(const struct rasterfall_session *session,
                                     int flag_index, int selection)
{
    int indices[TOY_GAME_MAX_ACTORS];
    int count;
    if (!session || selection < 0) return -1;
    count = session_collect_assignable(session, flag_index, indices);
    return selection < count ? indices[selection] : -1;
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

static int session_managed_ai_active(const struct rasterfall_session *session)
{
    int i;
    if (!session) return 0;
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (session->ai_registry.agents[i].active &&
            session->ai_registry.agents[i].controller ==
                RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER &&
            session->ai_registry.agents[i].actor_index ==
                TOY_GAME_PLAYER_ACTOR_INDEX)
            return 1;
    return 0;
}

int rasterfall_session_set_managed_ai(struct rasterfall_session *session,
                                      int active)
{
    if (!session) return 0;
    session->managed_ai_enabled = active != 0;
    if (!active) {
        rasterfall_ai_registry_remove(
            &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX);
        return 1;
    }
    return rasterfall_ai_registry_add(
        &session->ai_registry, TOY_GAME_PLAYER_ACTOR_INDEX,
        RASTERFALL_AI_CONTROLLER_MANAGED_PLAYER, 100,
        RASTERFALL_AI_POLICY_MANAGED_SIMPLE) >= 0;
}

static void session_managed_ai_face(struct camera *camera, int x, int z)
{
    int dx, dz, distance;
    if (!camera) return;
    dx = x - camera->x;
    dz = z - camera->z;
    distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (distance <= 0) return;
    camera->sy = (int)((long long)dx * 1024 / distance);
    camera->cy = (int)((long long)dz * 1024 / distance);
}

static void session_build_managed_ai_command(
    struct rasterfall_session *session, struct camera *camera,
    struct rasterfall_command *command)
{
    struct toy_game_ai_observation observation;
    int target_x = camera->x, target_z = camera->z;
    int target_found = 0, target_is_wave_button = 0;
    int dx, dz, distance, i;
    memset(command, 0, sizeof(*command));
    if (session->game_state.player_down) {
        if (session->game_state.money >= RASTERFALL_PAID_REVIVE_COST)
            command->buttons = RASTERFALL_CMD_REVIVE;
        return;
    }
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (session->game_state.campaign_phase == TOY_GAME_PHASE_CALM &&
        session->game_state.spawn_timer_ms > 0) {
        for (i = 0; i < session->item_count; i++) {
            if (session->items[i].kind != TOY_MAP_PICKUP_WAVE_SKIP_BUTTON)
                continue;
            target_x = session->items[i].x;
            target_z = session->items[i].z;
            target_found = 1;
            target_is_wave_button = 1;
            break;
        }
    } else if (toy_game_ai_observe(&session->game_state,
                                   TOY_GAME_PLAYER_ACTOR_INDEX,
                                   &observation) &&
               observation.nearest_enemy_index >= 0) {
        target_x = session->game_state.enemies[
            observation.nearest_enemy_index].x;
        target_z = session->game_state.enemies[
            observation.nearest_enemy_index].z;
        target_found = 1;
    } else if (session->game_state.base_actor_index >= 0 &&
               session->game_state.base_actor_index < TOY_GAME_MAX_ACTORS &&
               session->game_state.actors[
                   session->game_state.base_actor_index].active) {
        target_x = session->game_state.actors[
            session->game_state.base_actor_index].x;
        target_z = session->game_state.actors[
            session->game_state.base_actor_index].z;
        target_found = 1;
    }
    if (!target_found) return;
    session_managed_ai_face(camera, target_x, target_z);
    dx = target_x - camera->x;
    dz = target_z - camera->z;
    distance = isqrt((long long)dx * dx + (long long)dz * dz);
    if (target_is_wave_button && distance <= RASTERFALL_INTERACT_RANGE)
        command->buttons |= RASTERFALL_CMD_INTERACT;
    else if (distance > (target_is_wave_button ?
                         RASTERFALL_INTERACT_RANGE / 2 : 900))
        command->move_forward = 1;
    if (!target_is_wave_button && observation.nearest_enemy_index >= 0) {
        command->buttons |= RASTERFALL_CMD_FIRE;
        command->fire_held = 1;
    }
}

void rasterfall_session_step(struct rasterfall_session *session,
                             struct camera *camera,
                             const struct rasterfall_command *command,
                             int dt_ms)
{
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    struct rasterfall_command managed_command;
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    if (session_managed_ai_active(session)) {
        session_build_managed_ai_command(session, camera, &managed_command);
        command = &managed_command;
    }
    if ((command->buttons & RASTERFALL_CMD_REVIVE) &&
        rasterfall_session_paid_revive(session, camera))
        return;
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (command->buttons & RASTERFALL_CMD_FLAG)
        session_toggle_flag(session, camera);
    if (session->carried_flag >= 0) {
        int fi = session->carried_flag;
        session->flags[fi].x = camera->x; session->flags[fi].z = camera->z;
        session_set_flag_assignments(session, fi);
    }
    if (command->buttons & RASTERFALL_CMD_JUMP)
        session_jump_player(session, camera, command);
    if (!session->game_state.player_down)
        session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    session_update_smooth_turn(session, camera);
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    toy_game_set_player_pitch(&session->game_state, camera->pitch_sy,
                              camera->pitch_cy, camera->y);
    toy_game_set_player_moving(&session->game_state,
                               command->move_forward || command->move_strafe);
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
    if (command->buttons & RASTERFALL_CMD_SLOT_3) keys[TOY_GAME_KEY_SLOT_3] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_4) keys[TOY_GAME_KEY_SLOT_4] = 1;
    toy_game_update_held(&session->game_state, keys,
                         (command->buttons & RASTERFALL_CMD_FIRE) != 0,
                         command->fire_held, camera->sy, camera->cy, dt_ms);
    {
        int i;
        for (i = 0; i < session->game_state.event_count; i++)
            if (session->game_state.events[i] == TOY_GAME_EV_WAVE_START) {
                session->banner_ms = TOY_GAME_WAVE_ANNOUNCE_MS;
                session->banner_success = 1;
                session->banner_text = "WAVE STARTING";
                break;
            }
    }
    if (!session->game_state.reloading &&
        toy_game_animation_allows_locomotion(
            session->game_state.animation.id))
        toy_game_animation_set(&session->game_state.animation,
                               command->move_forward || command->move_strafe ?
                               TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_NONE);
    session_sync_special_motion(session, camera);
    session_update_manual_alarm(session, dt_ms);
    if (session->banner_ms > 0) {
        session->banner_ms -= dt_ms;
        if (session->banner_ms <= 0) {
    session->banner_ms = 0;
    session->banner_text = NULL;
    session->banner_success = 1;
        }
    }
}

static void session_step_client_mode(struct rasterfall_session *session,
                                     struct camera *camera,
                                     const struct rasterfall_command *command,
                                     int dt_ms, int suppress_presentation)
{
    unsigned char keys[TOY_GAME_KEY_RELOAD + 1];
    struct {
        int index;
        int hp, active, dying_ms, flash, hurt;
    } saved_enemy_hits[TOY_GAME_MAX_ENEMIES];
    int saved_enemy_count = 0;
    int enemy_count, kills, special_kills, damage_dealt;
    int throwable_damage_dealt, event_start, write, i;
    int saved_throw_timer;
    int old_reloading;
    unsigned int old_fire_seq;
    struct toy_game_animation_state saved_animation =
        session->game_state.animation;
    struct toy_game_ray saved_rays[TOY_GAME_MAX_RAYS];
    int saved_events = session->game_state.event_count;
    int saved_muzzle = session->game_state.muzzle_flash_ms;
    int saved_ray_count = session->game_state.ray_count;
    saved_throw_timer = session->game_state.throw_timer_ms;
    unsigned int saved_fire_seq = session->game_state.fire_seq;
    memcpy(saved_rays, session->game_state.rays, sizeof(saved_rays));
    if (command->buttons & RASTERFALL_CMD_RESET) {
        rasterfall_session_reset(session, camera, session->seed);
        return;
    }
    if ((command->buttons & RASTERFALL_CMD_REVIVE) &&
        rasterfall_session_paid_revive(session, camera))
        return;
    if (session->game_state.state != TOY_GAME_PLAYING) return;
    if (command->buttons & RASTERFALL_CMD_JUMP)
        session_jump_player(session, camera, command);
    session_move_player(session, camera, command);
    if (command->turn || command->pitch)
        rasterfall_camera_rotate(camera, command->turn, command->pitch);
    session->game_state.px = camera->x;
    session->game_state.pz = camera->z;
    toy_game_update_player_special_control(&session->game_state, dt_ms);
    camera->x = session->game_state.px;
    camera->z = session->game_state.pz;
    toy_game_set_player_pitch(&session->game_state, camera->pitch_sy,
                              camera->pitch_cy, camera->y);
    toy_game_set_player_moving(&session->game_state,
                               command->move_forward || command->move_strafe);
    toy_game_update_player_ground(&session->game_state);
    session_sync_special_motion(session, camera);
    session->highlight_index = rasterfall_session_compute_highlight(session, camera);
    if (command->buttons & RASTERFALL_CMD_INTERACT)
        session_client_interact_banner(session);
    /* Predict only the local first-person presentation.  The host remains
     * authoritative for the shove's enemy displacement and stun state. */
    if (command->buttons & RASTERFALL_CMD_SHOVE)
        toy_game_animation_set(&session->game_state.animation,
                               TOY_GAME_ANIM_SHOVE);
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
        }
    }
    /* 交互由主机权威执行。客户端只发送 INTERACT 命令，等待主机快照
     * 回传拾取、救援、弹药、空气墙和刷怪结果，避免两端世界分叉。 */
    memset(keys, 0, sizeof(keys));
    if (command->buttons & RASTERFALL_CMD_RELOAD) keys[TOY_GAME_KEY_RELOAD] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_1) keys[TOY_GAME_KEY_SLOT_1] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_2) keys[TOY_GAME_KEY_SLOT_2] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_3) keys[TOY_GAME_KEY_SLOT_3] = 1;
    if (command->buttons & RASTERFALL_CMD_SLOT_4) keys[TOY_GAME_KEY_SLOT_4] = 1;
    /* Weapon prediction can only mutate the enemy hit fields.  Saving the
     * complete 64-entry enemy array here made every client tick (and every
     * replay tick) pay for a world-sized memcpy. */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *enemy = &session->game_state.enemies[i];
        if (!enemy->active) continue;
        saved_enemy_hits[saved_enemy_count].index = i;
        saved_enemy_hits[saved_enemy_count].hp = enemy->hp;
        saved_enemy_hits[saved_enemy_count].active = enemy->active;
        saved_enemy_hits[saved_enemy_count].dying_ms = enemy->dying_ms;
        saved_enemy_hits[saved_enemy_count].flash = enemy->flash;
        saved_enemy_hits[saved_enemy_count].hurt = enemy->hurt;
        saved_enemy_count++;
    }
    enemy_count = session->game_state.enemies_alive;
    kills = session->game_state.kills;
    special_kills = session->game_state.special_kills;
    damage_dealt = session->game_state.damage_dealt;
    throwable_damage_dealt = session->game_state.throwable_damage_dealt;
    event_start = session->game_state.event_count;
    old_reloading = session->game_state.reloading;
    old_fire_seq = session->game_state.fire_seq;
    toy_game_update_weapon_held(&session->game_state, keys,
                                (command->buttons & RASTERFALL_CMD_FIRE) != 0,
                                command->fire_held, camera->sy, camera->cy,
                                dt_ms);
    /* The client predicts its own weapon state, so it must also predict the
     * presentation transition.  The host path uses toy_game_update_held,
     * which owns this transition; the client deliberately calls the lower
     * level weapon update to avoid simulating the shared world. */
    if (session->game_state.reloading && !old_reloading)
        toy_game_animation_set(&session->game_state.animation,
                               TOY_GAME_ANIM_RELOAD);
    else if (session->game_state.fire_seq != old_fire_seq)
        toy_game_animation_set(&session->game_state.animation,
                               TOY_GAME_ANIM_FIRE);
    if (session->game_state.animation.id == TOY_GAME_ANIM_RELOAD) {
        toy_game_animation_update(&session->game_state.animation, dt_ms);
        if (!session->game_state.reloading)
            toy_game_animation_set(&session->game_state.animation,
                                   TOY_GAME_ANIM_NONE);
    } else if (session->game_state.animation.id == TOY_GAME_ANIM_FIRE) {
        toy_game_animation_update(&session->game_state.animation, dt_ms);
        if (session->game_state.animation.time_ms >=
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms)
            toy_game_animation_set(&session->game_state.animation,
                                   TOY_GAME_ANIM_NONE);
    }
    /* There is no local gameplay update on the client for the shove, so
     * advance its presentation clock here until the authoritative snapshot
     * replaces it. */
    if (session->game_state.animation.id == TOY_GAME_ANIM_SHOVE) {
        toy_game_animation_update(&session->game_state.animation, dt_ms);
        if (session->game_state.animation.time_ms >=
            toy_game_animation_info(TOY_GAME_ANIM_SHOVE)->duration_ms)
            toy_game_animation_set(&session->game_state.animation,
                                   TOY_GAME_ANIM_NONE);
    }
    if (session->game_state.animation.id == TOY_GAME_ANIM_MELEE ||
        session->game_state.animation.id == TOY_GAME_ANIM_THROW) {
        toy_game_animation_update(&session->game_state.animation, dt_ms);
        if (session->game_state.animation.time_ms >=
            toy_game_animation_info(session->game_state.animation.id)->duration_ms)
            toy_game_animation_set(&session->game_state.animation,
                                   TOY_GAME_ANIM_NONE);
    }
    if (!session->game_state.reloading &&
        toy_game_animation_allows_locomotion(
            session->game_state.animation.id))
        toy_game_animation_set(&session->game_state.animation,
                               command->move_forward || command->move_strafe ?
                               TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_NONE);
    toy_game_update_player_motion(&session->game_state, dt_ms);
    /* Motion changes airborne_y (and may move the player horizontally).  Keep
     * the predicted first-person camera in the same post-tick state as the
     * host camera instead of waiting for the next snapshot to move camera.y. */
    session_sync_special_motion(session, camera);
    for (i = 0; i < saved_enemy_count; i++) {
        struct toy_game_enemy *enemy = &session->game_state.enemies[
            saved_enemy_hits[i].index];
        enemy->hp = saved_enemy_hits[i].hp;
        enemy->active = saved_enemy_hits[i].active;
        enemy->dying_ms = saved_enemy_hits[i].dying_ms;
        enemy->flash = saved_enemy_hits[i].flash;
        enemy->hurt = saved_enemy_hits[i].hurt;
    }
    /* Clients do not run the authoritative enemy simulation.  Still advance
     * the short death presentation locally so a lost final entity snapshot
     * cannot leave a flattened corpse rendered forever. */
    for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
        struct toy_game_enemy *enemy = &session->game_state.enemies[i];
        if (enemy->active == 2) {
            enemy->dying_ms -= dt_ms;
            if (enemy->dying_ms <= 0) enemy->active = 0;
        }
    }
    session->game_state.enemies_alive = enemy_count;
    session->game_state.kills = kills;
    session->game_state.special_kills = special_kills;
    session->game_state.damage_dealt = damage_dealt;
    session->game_state.throwable_damage_dealt = throwable_damage_dealt;
    session->game_state.throw_timer_ms = saved_throw_timer;
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
    if (suppress_presentation) {
        session->game_state.animation = saved_animation;
        session->game_state.event_count = saved_events;
        session->game_state.muzzle_flash_ms = saved_muzzle;
        session->game_state.fire_seq = saved_fire_seq;
        session->game_state.ray_count = saved_ray_count;
        memcpy(session->game_state.rays, saved_rays, sizeof(saved_rays));
    }
}

void rasterfall_session_step_client(struct rasterfall_session *session,
                                    struct camera *camera,
                                    const struct rasterfall_command *command,
                                    int dt_ms)
{
    session_step_client_mode(session, camera, command, dt_ms, 0);
}

void rasterfall_session_replay_client(struct rasterfall_session *session,
                                      struct camera *camera,
                                      const struct rasterfall_command *command,
                                      int dt_ms)
{
    session_step_client_mode(session, camera, command, dt_ms, 1);
}
