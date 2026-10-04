/* Shared perception/aim/weapon execution. Deployment orders remain allied
 * policy; autonomous approach/last-seen pursuit belongs to hostile policy. */
static int combat_target_valid(const struct toy_game *g,
                                const struct toy_game_actor *actor)
{
    const struct toy_game_combat_target *t = &actor->combat_target;
    if (t->kind == TOY_GAME_ENTITY_ACTOR && t->index >= 0 &&
        t->index < TOY_GAME_MAX_ACTORS) {
        const struct toy_game_actor *a = &g->actors[t->index];
        return a->active && a->state == TOY_GAME_ACTOR_ALIVE && a->hp > 0 &&
            !a->developer_only && a->combat_generation == (unsigned int)t->generation &&
            toy_game_actors_hostile(actor, a);
    }
    if (t->kind == TOY_GAME_ENTITY_ENEMY && t->index >= 0 &&
        t->index < TOY_GAME_MAX_ENEMIES)
        return g->enemies[t->index].active == 1 && g->enemies[t->index].hp > 0;
    return 0;
}

void toy_game_update_ai_teammate(struct toy_game *g, int dt_ms)
{
    struct toy_game_actor *a;
    struct toy_game_capabilities caps;
    struct toy_game_actor_command command = {0};
    int sx, sz, desired_x, desired_z, sy, cy, error = 180, visible = 0;
    int moved, target_present, keep_animation;
    unsigned int sequence;
    if (!g || g->ai_context_actor_index < 0 ||
        g->ai_context_actor_index >= TOY_GAME_MAX_ACTORS) return;
    a = &g->actors[g->ai_context_actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI) return;
    a->ai_shove_cooldown_ms = combat_timer(a->ai_shove_cooldown_ms, dt_ms);
    /* A dead/downed body still finishes an impulse already in flight. The
     * life-state gate below disables intent, not deterministic falling. */
    if (a->airborne_ms > 0) {
        toy_game_actor_update_animation(a, dt_ms);
        update_motion_values(g, a, dt_ms);
        return;
    }
    if (a->state != TOY_GAME_ACTOR_ALIVE || g->state != TOY_GAME_PLAYING ||
        a->animation_demo) {
        toy_game_actor_update_animation(a, dt_ms);
        return;
    }
    toy_game_actor_capabilities(a, toy_game_actor_current_weapon(a), &caps);
    a->combat_scan_ms = combat_timer(a->combat_scan_ms, dt_ms);
    a->combat_lost_ms = combat_timer(a->combat_lost_ms, dt_ms);
    if (!combat_target_valid(g, a)) {
        a->combat_target.kind = -1;
        a->combat_aim_ms = 0;
    }
    if (!a->combat_scan_ms && g->combat_scan_budget > 0) {
        struct toy_game_combat_target next;
        g->combat_scan_budget--;
        a->combat_scan_ms = 250;
        if (toy_game_find_combat_target(g, a, &next)) {
            if (next.kind != a->combat_target.kind ||
                next.index != a->combat_target.index ||
                next.generation != a->combat_target.generation) a->combat_aim_ms = 0;
            memcpy(&a->combat_target, &next, sizeof(next));
            a->combat_last_x = next.x; a->combat_last_z = next.z;
            a->combat_last_y = next.y; a->combat_lost_ms = 1500;
        } else {
            a->combat_target.kind = -1;
            a->combat_aim_ms = 0;
        }
    }
    if (a->hit_test_dummy) {
        /* Existing hit-pose fixtures stay rooted and keep facing their target;
         * experiments may drive their weapon explicitly after this tick. */
        if (a->combat_target.kind >= 0) {
            int dx = a->combat_target.x - a->x, dz = a->combat_target.z - a->z;
            int distance = isqrt((long long)dx * dx + (long long)dz * dz);
            if (distance) {
                a->sy = (int)((long long)dx * 1024 / distance);
                a->cy = (int)((long long)dz * 1024 / distance);
            }
        }
        toy_game_actor_update_animation(a, dt_ms);
        return;
    }
    target_present = a->combat_target.kind >= 0;
    desired_x = a->deployment_x; desired_z = a->deployment_z;
    if (a->faction == TOY_GAME_FACTION_HOSTILE) {
        desired_x = a->x; desired_z = a->z;
        if (target_present || a->combat_lost_ms) {
            int dx = a->combat_last_x - a->x, dz = a->combat_last_z - a->z;
            int distance = isqrt((long long)dx * dx + (long long)dz * dz);
            int reach = toy_game_weapon_info(toy_game_actor_current_weapon(a))->range;
            int hold = reach / 3;
            if (hold > 6000) hold = 6000;
            if (hold < 2400) hold = 2400;
            if (distance > hold || !target_present) {
                desired_x = a->combat_last_x; desired_z = a->combat_last_z;
            } else if (distance < 1800 && distance > 0) {
                desired_x = a->x - dx * 700 / distance;
                desired_z = a->z - dz * 700 / distance;
            }
        }
    } else if (a->companion && !a->ai_stationary) {
        const struct toy_game_actor *player = toy_game_local_player_actor_const(g);
        long long dx = (long long)player->x - a->x, dz = (long long)player->z - a->z;
        desired_x = a->x; desired_z = a->z;
        if (dx * dx + dz * dz > 1600LL * 1600) {
            desired_x = player->x; desired_z = player->z;
        }
    }
    sx = a->x; sz = a->z;
    if (!a->control_disabled && !a->base_core && !a->movement_hold_token) {
        long long dx = (long long)desired_x - a->x, dz = (long long)desired_z - a->z;
        if (dx * dx + dz * dz > TOY_GAME_AI_DEPLOY_RADIUS * TOY_GAME_AI_DEPLOY_RADIUS)
            actor_path_toward(g, a, desired_x, desired_z,
                toy_game_actor_move_step(a, TOY_CONFIG_AI_RETURN_SPEED));
        else a->nav_active = 0;
    }
    moved = a->x != sx || a->z != sz;
    sy = a->sy; cy = a->cy;
    if (target_present) {
        int dx = a->combat_target.x - a->x, dz = a->combat_target.z - a->z;
        int distance = isqrt((long long)dx * dx + (long long)dz * dz);
        if (distance > 0) {
            sy = (int)((long long)dx * 1024 / distance);
            cy = (int)((long long)dz * 1024 / distance);
            visible = combat_visible(g, a, a->combat_target.x,
                                      a->combat_target.y, a->combat_target.z);
            error = ai_turn_toward(a, sy, cy, caps.turn_degrees, dt_ms);
            a->pitch_sy = (int)((long long)(a->combat_target.y - a->ground_y -
                a->airborne_y - RASTERFALL_HUMAN_EYE_HEIGHT_RFU) * 1024 / distance);
            a->pitch_cy = 1024;
        }
    } else if (moved) {
        ai_turn_toward(a, a->x - sx, a->z - sz, caps.turn_degrees, dt_ms);
    }
    if (visible && error <= 6) a->combat_aim_ms += dt_ms;
    else a->combat_aim_ms = 0;
    command.switch_slot = -1;
    command.aim_active = target_present;
    command.aim_sy = a->sy; command.aim_cy = a->cy;
    command.fire_pressed = a->fire_enabled && visible && error <= 6 &&
        a->combat_aim_ms >= caps.aim_ms;
    command.fire_held = command.fire_pressed;
    a->moving = moved;
    sequence = a->fire_seq;
    toy_game_execute_actor_command(g, a, &command, dt_ms, 100);
    keep_animation = (a->animation.id == TOY_GAME_ANIM_HIT ||
        a->animation.id == TOY_GAME_ANIM_REVIVE || a->animation.id == TOY_GAME_ANIM_SHOVE ||
        a->animation.id == TOY_GAME_ANIM_FIRE) && a->animation.time_ms <
            toy_game_animation_info(a->animation.id)->duration_ms;
    if (a->reloading) toy_game_actor_set_animation(a, TOY_GAME_ANIM_RELOAD);
    else if (a->fire_seq != sequence) toy_game_actor_set_animation(a, TOY_GAME_ANIM_FIRE);
    else if (!keep_animation)
        toy_game_actor_set_animation(a, moved ? TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_IDLE);
    toy_game_actor_update_animation(a, dt_ms);
}
