/* Shared perception/aim/weapon execution. Deployment orders remain allied
 * policy; autonomous approach/last-seen pursuit belongs to hostile policy. */
static int combat_guard_allows(const struct toy_game_actor *a, int x, int z)
{
    long long dx = (long long)x - a->deployment_x;
    long long dz = (long long)z - a->deployment_z;
    return a->ai_guard_radius <= 0 || dx * dx + dz * dz <=
        (long long)a->ai_guard_radius * a->ai_guard_radius;
}

void toy_game_actor_cancel_rescue(struct toy_game *g, int actor_index)
{
    struct toy_game_actor *a, *target;
    if (!g || actor_index < 0 || actor_index >= TOY_GAME_MAX_ACTORS) return;
    a = &g->actors[actor_index];
    if (a->ai_rescue_active && a->ai_rescue_index >= 0 &&
        a->ai_rescue_index < TOY_GAME_MAX_ACTORS) {
        target = &g->actors[a->ai_rescue_index];
        if (target->combat_generation == a->ai_rescue_generation &&
            target->state == TOY_GAME_ACTOR_DOWNED) target->revive_progress_ms = 0;
        toy_game_actor_cancel_navigation(a);
    }
    a->ai_rescue_active = 0; a->ai_rescue_index = -1;
    a->ai_rescue_generation = 0;
}

static int combat_rescue_ready(const struct toy_game_actor *a)
{
    long long dx = (long long)a->command_x - a->x;
    long long dz = (long long)a->command_z - a->z;
    return a->active && a->kind == TOY_GAME_ACTOR_AI &&
        a->faction == TOY_GAME_FACTION_ALLIED && a->state == TOY_GAME_ACTOR_ALIVE &&
        a->hp > 0 && !a->base_core && !a->developer_only && !a->hit_test_dummy &&
        !a->animation_demo && !a->control_disabled && !a->movement_hold_token &&
        !a->airborne_ms && !a->damage_flash_ms &&
        (!a->command_destination_active || dx * dx + dz * dz <=
            (long long)TOY_GAME_AI_DEPLOY_RADIUS * TOY_GAME_AI_DEPLOY_RADIUS);
}

/* Only an immediate visible threat prevents aid. Distant gunfire does not
 * leave a downed squad member permanently waiting for a perfectly safe map. */
static int combat_rescue_threat(const struct toy_game *g,
                                const struct toy_game_actor *a)
{
    int i;
    for (i = 0; i < TOY_GAME_MAX_ACTORS + TOY_GAME_MAX_ENEMIES; i++) {
        int x, z, y;
        long long dx, dz;
        if (i < TOY_GAME_MAX_ACTORS) {
            const struct toy_game_actor *other = &g->actors[i];
            if (!other->active || other->state != TOY_GAME_ACTOR_ALIVE ||
                other->hp <= 0 || other->developer_only ||
                !toy_game_actors_hostile(a, other)) continue;
            x = other->x; z = other->z;
            y = other->ground_y + other->airborne_y;
        } else {
            const struct toy_game_enemy *e = &g->enemies[i - TOY_GAME_MAX_ACTORS];
            if (e->active != 1 || e->hp <= 0) continue;
            x = e->x; z = e->z; y = e->ground_y + e->airborne_y;
        }
        dx = (long long)x - a->x; dz = (long long)z - a->z;
        if (dx * dx + dz * dz <= 1000LL * 1000 && combat_visible(g, a,
            x, y + RASTERFALL_HUMAN_HEIGHT_RFU * 64 / 100, z)) return 1;
    }
    return 0;
}

static struct toy_game_actor *combat_rescue_intent(struct toy_game *g,
                                                  struct toy_game_actor *a)
{
    struct toy_game_actor *best = NULL;
    long long best_distance = (long long)TOY_GAME_AI_RESCUE_SEARCH_RANGE *
                                       TOY_GAME_AI_RESCUE_SEARCH_RANGE + 1;
    int i, has_downed = 0, self = (int)(a - g->actors);
    if (!combat_rescue_ready(a)) {
        toy_game_actor_cancel_rescue(g, self);
        return NULL;
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++)
        if (g->actors[i].active && g->actors[i].state == TOY_GAME_ACTOR_DOWNED &&
            g->actors[i].faction == a->faction && !g->actors[i].base_core &&
            !g->actors[i].developer_only) { has_downed = 1; break; }
    if (!has_downed || combat_rescue_threat(g, a)) {
        toy_game_actor_cancel_rescue(g, self);
        return NULL;
    }
    if (a->ai_rescue_active && a->ai_rescue_index >= 0 &&
        a->ai_rescue_index < TOY_GAME_MAX_ACTORS) {
        struct toy_game_actor *target = &g->actors[a->ai_rescue_index];
        long long dx = (long long)target->x - a->x, dz = (long long)target->z - a->z;
        if (target->active && target->state == TOY_GAME_ACTOR_DOWNED &&
            target->faction == a->faction &&
            target->combat_generation == a->ai_rescue_generation &&
            (!a->command_destination_active || dx * dx + dz * dz <=
                (long long)TOY_GAME_AI_RESCUE_RANGE * TOY_GAME_AI_RESCUE_RANGE))
            return target;
        toy_game_actor_cancel_rescue(g, self);
    }
    for (i = 0; i < TOY_GAME_MAX_ACTORS; i++) {
        struct toy_game_actor *target = &g->actors[i];
        long long dx, dz, distance;
        int n, claimed = 0;
        if (!target->active || target->state != TOY_GAME_ACTOR_DOWNED ||
            target->faction != a->faction || target->base_core ||
            target->developer_only || target->airborne_ms) continue;
        dx = (long long)target->x - a->x; dz = (long long)target->z - a->z;
        distance = dx * dx + dz * dz;
        if (distance >= best_distance || ((a->command_destination_active ||
            a->ai_stationary) && distance >
            (long long)TOY_GAME_AI_RESCUE_RANGE * TOY_GAME_AI_RESCUE_RANGE)) continue;
        for (n = 0; n < TOY_GAME_MAX_ACTORS; n++) {
            const struct toy_game_actor *peer = &g->actors[n];
            if (peer == a || !peer->ai_rescue_active ||
                !combat_rescue_ready(peer)) continue;
            if (peer->ai_rescue_index == i && peer->ai_rescue_generation ==
                target->combat_generation) { claimed = 1; break; }
        }
        if (claimed) continue;
        best = target; best_distance = distance;
    }
    if (best) {
        a->ai_rescue_active = 1; a->ai_rescue_index = (int)(best - g->actors);
        a->ai_rescue_generation = best->combat_generation;
        best->revive_progress_ms = 0;
        a->nav_active = 0;
    }
    return best;
}

static int combat_target_valid(const struct toy_game *g,
                                const struct toy_game_actor *actor)
{
    const struct toy_game_combat_target *t = &actor->combat_target;
    if (t->kind == TOY_GAME_ENTITY_ACTOR && t->index >= 0 &&
        t->index < TOY_GAME_MAX_ACTORS) {
        const struct toy_game_actor *a = &g->actors[t->index];
        return a->active && a->state == TOY_GAME_ACTOR_ALIVE && a->hp > 0 &&
            !a->developer_only && a->combat_generation == (unsigned int)t->generation &&
            toy_game_actors_hostile(actor, a) && combat_guard_allows(actor, a->x, a->z);
    }
    if (t->kind == TOY_GAME_ENTITY_ENEMY && t->index >= 0 &&
        t->index < TOY_GAME_MAX_ENEMIES) {
        const struct toy_game_enemy *e = &g->enemies[t->index];
        return e->active == 1 && e->hp > 0 &&
            e->combat_generation == (unsigned int)t->generation &&
            combat_guard_allows(actor, e->x, e->z);
    }
    return 0;
}

void toy_game_actor_set_intent(struct toy_game_actor *a, int move,
    int x, int y, int z, int target_id, unsigned int generation, int fire, int reload)
{
    if (!a) return;
    if(a->intent_target_id!=target_id || a->intent_target_generation!=generation)
        a->combat_aim_ms=0;
    a->controller_external = 1;
    a->intent_move = move; a->intent_x = x; a->intent_y = y; a->intent_z = z;
    a->intent_target_id = target_id; a->intent_target_generation = generation;
    a->intent_fire = fire; a->intent_reload = reload;
}

/* Script, tactical and range policies share normal navigation, aim readiness,
 * skill limits, weapon execution and animation. Never called by presentation. */
static void actor_external_step(struct toy_game *g, struct toy_game_actor *a, int dt_ms)
{
    struct toy_game_actor_command cmd = {0};
    struct toy_game_capabilities caps;
    const struct toy_game_actor *target = toy_game_actor_by_id_const(g, a->intent_target_id);
    unsigned sequence = a->fire_seq;
    int error = 180, visible = 0;
    toy_game_actor_capabilities(a, toy_game_actor_current_weapon(a), &caps);
    cmd.switch_slot = -1;
    a->moving = 0;
    if (a->intent_move && !(a->intent_exclusive && a->reloading) && !a->control_disabled && !a->movement_hold_token) {
        cmd.move_target_active = 1; cmd.move_x = a->intent_x; cmd.move_z = a->intent_z;
        cmd.move_speed = toy_game_actor_move_step(a, TOY_CONFIG_AI_RETURN_SPEED);
    }
    if(a->command_destination_active && !a->control_disabled && !a->movement_hold_token) {
        cmd.move_target_active=1;cmd.move_x=a->command_x;cmd.move_z=a->command_z;
        cmd.move_speed=toy_game_actor_move_step(a,TOY_CONFIG_AI_RETURN_SPEED);
    }
    if (target && target->combat_generation == a->intent_target_generation &&
        target->state == TOY_GAME_ACTOR_ALIVE && toy_game_actors_hostile(a, target)) {
        int dx = target->x - a->x, dz = target->z - a->z;
        int distance = isqrt((long long)dx * dx + (long long)dz * dz);
        int aim_height=a->intent_aim_height_percent;
        if(aim_height<1 || aim_height>100) aim_height=64;
        int y = target->ground_y + target->airborne_y + RASTERFALL_HUMAN_HEIGHT_RFU * aim_height / 100;
        if (distance) {
            error = ai_turn_toward(a, dx, dz, caps.turn_degrees, dt_ms);
            a->pitch_sy = (int)((long long)(y - a->ground_y - a->airborne_y -
                RASTERFALL_HUMAN_EYE_HEIGHT_RFU) * 1024 / distance);
            a->pitch_cy = 1024;
            visible = combat_visible(g, a, target->x, y, target->z);
        }
    } else if (cmd.move_target_active) {
        ai_turn_toward(a, cmd.move_x-a->x, cmd.move_z-a->z, caps.turn_degrees, dt_ms);
    }
    if (visible && error <= 2 && !cmd.move_target_active) {
        a->combat_aim_ms += dt_ms;a->combat_aim_lost_ms=0;
    } else {
        a->combat_aim_lost_ms+=dt_ms;
        if(cmd.move_target_active || a->combat_aim_lost_ms>200)a->combat_aim_ms=0;
    }
    cmd.fire_pressed = a->intent_fire && a->fire_enabled && !a->control_disabled &&
        visible && error <= 2 && a->combat_aim_ms >= caps.aim_ms;
    cmd.fire_held = cmd.fire_pressed; cmd.reload = a->intent_reload;
    if(a->intent_exclusive){
        if(a->reloading)cmd.move_target_active=0;
        if(cmd.move_target_active){cmd.fire_pressed=cmd.fire_held=0;cmd.reload=0;}
    }
    toy_game_execute_actor_command(g, a, &cmd, dt_ms, 100);
    if (a->reloading) toy_game_actor_set_animation(a, TOY_GAME_ANIM_RELOAD);
    else if (sequence != a->fire_seq) toy_game_actor_set_animation(a, TOY_GAME_ANIM_FIRE);
    else if (a->animation.id != TOY_GAME_ANIM_HIT && a->animation.id != TOY_GAME_ANIM_FIRE)
        toy_game_actor_set_animation(a, a->moving ? TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_IDLE);
    else if (a->animation.time_ms >= toy_game_animation_info(a->animation.id)->duration_ms)
        toy_game_actor_set_animation(a, a->moving ? TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_IDLE);
    toy_game_actor_update_animation(a, dt_ms);
}

void toy_game_update_ai_teammate(struct toy_game *g, int dt_ms)
{
    struct toy_game_actor *a;
    struct toy_game_actor *rescue;
    struct toy_game_capabilities caps;
    struct toy_game_actor_command command = {0};
    int sx, sz, desired_x, desired_z, sy, cy, error = 180, visible = 0;
    int moved, target_present, keep_animation;
    unsigned int sequence;
    if (!g || g->ai_context_actor_index < 0 ||
        g->ai_context_actor_index >= TOY_GAME_MAX_ACTORS) return;
    a = &g->actors[g->ai_context_actor_index];
    if (!a->active || a->kind != TOY_GAME_ACTOR_AI || a->simulation_paused) return;
    a->nav_direct_ms = combat_timer(a->nav_direct_ms, dt_ms);
    a->nav_layer_retry_ms=combat_timer(a->nav_layer_retry_ms,dt_ms);
    a->ai_shove_cooldown_ms = combat_timer(a->ai_shove_cooldown_ms, dt_ms);
    /* A dead/downed body still finishes an impulse already in flight. The
     * life-state gate below disables intent, not deterministic falling. */
    if (a->airborne_ms > 0) {
        toy_game_actor_cancel_rescue(g, g->ai_context_actor_index);
        toy_game_actor_update_animation(a, dt_ms);
        update_motion_values(g, a, dt_ms);
        return;
    }
    if (a->state != TOY_GAME_ACTOR_ALIVE || g->state != TOY_GAME_PLAYING ||
        a->animation_demo) {
        toy_game_actor_cancel_rescue(g, g->ai_context_actor_index);
        toy_game_actor_update_animation(a, dt_ms);
        return;
    }
    if (a->controller_external) {
        toy_game_actor_cancel_rescue(g, g->ai_context_actor_index);
        actor_external_step(g, a, dt_ms);
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
        if (combat_find_target(g, a, &next, 1)) {
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
    rescue = combat_rescue_intent(g, a);
    desired_x = a->deployment_x; desired_z = a->deployment_z;
    if (a->faction == TOY_GAME_FACTION_HOSTILE) {
        desired_x = a->ai_guard_radius > 0 || a->ai_assault_active ?
            a->deployment_x : a->x;
        desired_z = a->ai_guard_radius > 0 || a->ai_assault_active ?
            a->deployment_z : a->z;
        if (a->ai_assault_active && !a->ai_guard_radius &&
            !target_present && !a->combat_lost_ms)
            actor_squad_idle_goal(g, g->ai_context_actor_index, &desired_x, &desired_z);
        if (target_present || a->combat_lost_ms) {
            int dx = a->combat_last_x - a->x, dz = a->combat_last_z - a->z;
            int distance = isqrt((long long)dx * dx + (long long)dz * dz);
            int reach = toy_game_weapon_info(toy_game_actor_current_weapon(a))->range;
            int hold = reach / 3;
            desired_x = a->x; desired_z = a->z;
            if (hold > 6000) hold = 6000;
            if (hold < 2400) hold = 2400;
            if (distance > hold || !target_present) {
                desired_x = a->combat_last_x; desired_z = a->combat_last_z;
            } else if (distance < 1800 && distance > 0) {
                desired_x = a->x - dx * 700 / distance;
                desired_z = a->z - dz * 700 / distance;
            }
        }
        if (!combat_guard_allows(a, desired_x, desired_z)) {
            desired_x = a->deployment_x; desired_z = a->deployment_z;
        }
    } else if (a->command_destination_active) {
        desired_x = a->command_x; desired_z = a->command_z;
    } else if (!rescue && actor_squad_idle_goal(g, g->ai_context_actor_index,
                                             &desired_x, &desired_z)) {
        /* The cached autonomous goal already replaced deployment. */
    } else if (a->companion && !a->ai_stationary) {
        const struct toy_game_actor *player = toy_game_local_player_actor_const(g);
        long long dx = (long long)player->x - a->x, dz = (long long)player->z - a->z;
        desired_x = a->x; desired_z = a->z;
        if (dx * dx + dz * dz > 1600LL * 1600) {
            desired_x = player->x; desired_z = player->z;
        }
    }
    if (rescue && !a->command_destination_active && !a->ai_stationary) {
        desired_x = rescue->x; desired_z = rescue->z;
    }
    sx = a->x; sz = a->z;
    if (!a->control_disabled && !a->base_core && !a->movement_hold_token && !a->ai_stationary) {
        long long dx = (long long)desired_x - a->x, dz = (long long)desired_z - a->z;
        if (dx * dx + dz * dz > TOY_GAME_AI_DEPLOY_RADIUS * TOY_GAME_AI_DEPLOY_RADIUS ||
            (a->command_destination_active && a->command_height_active &&
             abs(a->ground_y-a->command_y)>2))
            actor_path_toward(g, a, desired_x, desired_z,
                toy_game_actor_move_step(a, TOY_CONFIG_AI_RETURN_SPEED), 0);
        else a->nav_active = 0;
    }
    moved = a->x != sx || a->z != sz;
    if (rescue) {
        long long dx = (long long)rescue->x - a->x, dz = (long long)rescue->z - a->z;
        int height = rescue->ground_y - a->ground_y;
        if (height < 0) height = -height;
        a->moving = moved;
        if (dx * dx + dz * dz <= (long long)TOY_GAME_AI_RESCUE_RANGE *
            TOY_GAME_AI_RESCUE_RANGE && height <= TOY_CONFIG_GROUND_STEP_HEIGHT &&
            combat_visible(g, a, rescue->x,
                rescue->ground_y + RASTERFALL_HUMAN_HEIGHT_RFU * 64 / 100, rescue->z)) {
            if (toy_game_revive_actor(g, (int)(rescue - g->actors), dt_ms))
                toy_game_actor_cancel_rescue(g, g->ai_context_actor_index);
            toy_game_actor_set_animation(a, TOY_GAME_ANIM_REVIVE);
        } else toy_game_actor_set_animation(a, moved ? TOY_GAME_ANIM_MOVE : TOY_GAME_ANIM_IDLE);
        toy_game_update_actor_weapon_held(g, a, NULL, 0, 0, a->sy, a->cy, dt_ms, 100);
        toy_game_actor_update_animation(a, dt_ms);
        return;
    }
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
    if (visible && error <= 2 && !moved) {
        a->combat_aim_ms += dt_ms;a->combat_aim_lost_ms=0;
    } else {
        a->combat_aim_lost_ms+=dt_ms;
        if(moved || a->combat_aim_lost_ms>200)a->combat_aim_ms=0;
    }
    command.switch_slot = -1;
    command.aim_active = target_present;
    command.aim_sy = a->sy; command.aim_cy = a->cy;
    command.fire_pressed = a->fire_enabled && visible && error <= 2 &&
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
