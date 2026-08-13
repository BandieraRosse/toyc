#define special_windup_ms ability.special_windup_ms
#define special_target_active ability.special_target_active
#define special_target_player ability.special_target_player
#define special_pull_timer_ms ability.special_pull_timer_ms
#define charge_active ability.charge_active

static int run_logic_test(void)
{
    struct camera camera;
    struct toy_input input;
    struct vec3 triangle[3], clipped[4];
    struct toy_surface surface;
    struct toy_renderer renderer;
    struct rasterfall_command command;
    uint32_t *pixels;
    int count;
    long long direction_length;
    toy_input_init(&input);
    rasterfall_session_reset(&session, &camera, 1);
    if (session.level.safe_count > 0 &&
        session.game_state.safe_room_count != session.level.safe_count)
        return 126;
    /* 远端旗帜操作必须由主机状态承接，并跟随远端玩家移动。 */
    if (session.flag_count != 1) return 131;
    camera.x = session.flags[0].x;
    camera.z = session.flags[0].z;
    rasterfall_session_toggle_flag_remote(&session, &camera, 1);
    if (!session.flags[0].carried || session.flags[0].carrier_id != 1)
        return 132;
    camera.x += 240;
    camera.z -= 120;
    rasterfall_session_update_flag_remote(&session, &camera, 1);
    if (session.flags[0].x != camera.x || session.flags[0].z != camera.z)
        return 133;
    rasterfall_session_toggle_flag_remote(&session, &camera, 1);
    if (session.flags[0].carried || session.flags[0].carrier_id != -1)
        return 134;
    {
        int actor_index = rasterfall_session_shop_actor_at(&session, 0, 0);
        int was_assigned;
        if (actor_index < 0) return 135;
        was_assigned = session.game_state.actors[actor_index].flag_index == 0;
        if (rasterfall_session_shop_request(&session, 4, 0,
                                            actor_index) <= 0 ||
            session.game_state.actors[actor_index].flag_index !=
                (was_assigned ? -1 : 0))
            return 136;
    }
    /* 客户端交互由主机执行，但按下 E 后本地仍必须立即显示反馈。 */
    if (session.item_count > 0) {
        memset(&command, 0, sizeof(command));
        camera.x = session.items[0].x;
        camera.z = session.items[0].z - 100;
        camera.sy = 0; camera.cy = 1024;
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step_client(&session, &camera, &command, 16);
        if (!session.banner_text || session.banner_ms <= 0) return 100;
        rasterfall_session_reset(&session, &camera, 1);
    }
    {
        struct toy_game actor_test;
        struct toy_game ai_reload_test;
        int i;
        int ai_id;
        const struct toy_game_actor *actor =
            toy_game_actor_by_id_const(&game, 1);
        const struct toy_game_enemy_info *common =
            toy_game_enemy_info(TOY_GAME_ENEMY_COMMON);
        const struct toy_game_enemy_info *pursuit_common =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_COMMON);
        const struct toy_game_enemy_info *heavy =
            toy_game_enemy_info(TOY_GAME_ENEMY_HEAVY);
        const struct toy_game_enemy_info *brute =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_HEAVY);
        const struct toy_game_enemy_info *runner =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_FAST);
        const struct toy_game_enemy_info *smoker =
            toy_game_enemy_info(TOY_GAME_ENEMY_SMOKER);
        const struct toy_game_enemy_info *charger =
            toy_game_enemy_info(TOY_GAME_ENEMY_CHARGER);
        const struct toy_game_enemy_info *tank =
            toy_game_enemy_info(TOY_GAME_ENEMY_TANK);
        const struct toy_game_weapon_info *smg =
            toy_game_weapon_info(TOY_GAME_WEAPON_SMG);
        const struct toy_game_weapon_info *shotgun =
            toy_game_weapon_info(TOY_GAME_WEAPON_SHOTGUN);
        const struct toy_game_weapon_info *pistol =
            toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL);
        const struct toy_game_weapon_info *ak =
            toy_game_weapon_info(TOY_GAME_WEAPON_AK);
        const struct toy_game_weapon_info *awp =
            toy_game_weapon_info(TOY_GAME_WEAPON_AWP);
        if (!actor || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->class_id != TOY_GAME_AI_LEVEL_2 ||
            pursuit_common->speed_min <= common->speed_max ||
            heavy->max_hp <= common->max_hp ||
            heavy->bite_damage <= common->bite_damage ||
            runner->speed_min <= pursuit_common->speed_max ||
            smg->damage != 25 || shotgun->damage != 25 ||
            common->max_hp != 50 || heavy->max_hp != 200 ||
            brute->max_hp != 200 || runner->max_hp != 60) return 49;
        if (!toy_game_enemy_type_is_valid(TOY_GAME_ENEMY_SMOKER) ||
            toy_game_enemy_type_is_valid(-1) ||
            toy_game_enemy_content_id(TOY_GAME_ENEMY_SMOKER) !=
                TOY_GAME_ENEMY_ID_SMOKER ||
            toy_game_enemy_from_content_id(TOY_GAME_ENEMY_ID_CHARGER) !=
                TOY_GAME_ENEMY_CHARGER ||
            toy_game_enemy_from_content_id(TOY_GAME_ENEMY_ID_TANK) !=
                TOY_GAME_ENEMY_TANK ||
            toy_game_enemy_from_content_id(99) >= 0 ||
            smoker->ability != TOY_GAME_ENEMY_ABILITY_SMOKER_TONGUE ||
            charger->ability != TOY_GAME_ENEMY_ABILITY_CHARGER_RUSH ||
            tank->ability != TOY_GAME_ENEMY_ABILITY_TANK_SWEEP ||
            tank->max_hp != 4000 ||
            tank->speed_min != common->speed_min ||
            tank->speed_max != common->speed_max ||
            TOY_CONFIG_TANK_ATTACK_RANGE != 2850 ||
            TOY_CONFIG_TANK_ATTACK_START_DIVISOR != 2 ||
            TOY_CONFIG_TANK_ATTACK_START_RANGE != 1425 ||
            TOY_CONFIG_TANK_WINDUP_MS != 750 ||
            TOY_CONFIG_TANK_IMPACT_MS != 625) return 56;
        if (toy_game_weapon_from_name("pistol") != TOY_GAME_WEAPON_PISTOL ||
            toy_game_weapon_from_name("SMG") != TOY_GAME_WEAPON_SMG ||
            toy_game_weapon_from_name("shotgun") != TOY_GAME_WEAPON_SHOTGUN ||
            toy_game_weapon_from_name("missing") >= 0) return 53;
        if (!toy_game_weapon_is_valid(TOY_GAME_WEAPON_PISTOL) ||
            toy_game_weapon_is_valid(-1) ||
            toy_game_weapon_content_id(TOY_GAME_WEAPON_PISTOL) !=
                TOY_GAME_WEAPON_ID_PISTOL ||
            toy_game_weapon_content_id(TOY_GAME_WEAPON_SMG) !=
                TOY_GAME_WEAPON_ID_SMG ||
            toy_game_weapon_from_content_id(TOY_GAME_WEAPON_ID_SHOTGUN) !=
                TOY_GAME_WEAPON_SHOTGUN ||
            toy_game_weapon_from_content_id(99) >= 0 ||
            strcmp(smg->short_name, "SMG") != 0 ||
            shotgun->muzzle_profile != TOY_GAME_MUZZLE_SHOTGUN) return 55;
        if (awp->range != TOY_GAME_MAX_RANGE * 2 ||
            awp->alert_range != TOY_GAME_MAX_RANGE * 2 ||
            ak->range != TOY_GAME_MAX_RANGE * 3 / 2 ||
            ak->alert_range != TOY_GAME_MAX_RANGE ||
            smg->alert_range <= TOY_GAME_DETECT_RANGE ||
            pistol->alert_range <= TOY_GAME_DETECT_RANGE ||
            shotgun->alert_range != TOY_GAME_DETECT_RANGE ||
            TOY_CONFIG_AWP_AI_AIM_MS != 700 ||
            TOY_CONFIG_AWP_AI_POST_FIRE_MS != 700) return 116;
        toy_game_actor_set_animation(&game.actors[1], TOY_GAME_ANIM_IDLE);
        toy_game_actor_update_animation(&game.actors[1], 160);
        if (game.actors[1].animation.id != TOY_GAME_ANIM_IDLE ||
            game.actors[1].animation.time_ms != 160 ||
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->duration_ms != 180 ||
            toy_game_animation_info(TOY_GAME_ANIM_FIRE)->loop ||
            toy_game_animation_info(99)->duration_ms != 0) return 59;
        toy_game_animation_set(&game.animation, TOY_GAME_ANIM_RELOAD);
        toy_game_animation_update(&game.animation, 450);
        if (game.animation.id != TOY_GAME_ANIM_RELOAD ||
            game.animation.time_ms != 450) return 60;
        toy_game_animation_update(&game.animation, 600);
        if (game.animation.time_ms != 1050) return 61;
        toy_game_animation_set(&game.animation, TOY_GAME_ANIM_NONE);
        toy_game_init(&ai_reload_test, 9);
        ai_id = toy_game_add_ai(&ai_reload_test, TOY_GAME_AI_LEVEL_1,
                                0, 0, "RELOAD TEST");
        ai_reload_test.actors[ai_id - 1].slots[
            ai_reload_test.actors[ai_id - 1].current_slot].mag = 1;
        ai_reload_test.enemies[0].active = 1;
        ai_reload_test.enemies[0].type = TOY_GAME_ENEMY_HEAVY;
        ai_reload_test.enemies[0].x = 0;
        ai_reload_test.enemies[0].z = 1000;
        ai_reload_test.enemies[0].hp = 200;
        toy_game_update_ai_teammates(&ai_reload_test, 16);
        if (!ai_reload_test.actors[ai_id - 1].reloading ||
            ai_reload_test.actors[ai_id - 1].animation.id !=
                TOY_GAME_ANIM_RELOAD) return 62;
        toy_game_update_ai_teammates(&ai_reload_test,
                                     toy_game_weapon_info(TOY_GAME_WEAPON_PISTOL)->reload_ms + 100);
        if (ai_reload_test.actors[ai_id - 1].reloading ||
            ai_reload_test.actors[ai_id - 1].slots[
                ai_reload_test.actors[ai_id - 1].current_slot].reserve !=
                TOY_GAME_AMMO_INFINITE ||
            ai_reload_test.actors[ai_id - 1].slots[
                ai_reload_test.actors[ai_id - 1].current_slot].mag <= 0) return 63;
        if (!game.actors[1].active ||
            game.actors[1].class_id != TOY_GAME_AI_LEVEL_1 ||
            game.actors[1].slots[game.actors[1].current_slot].weapon != TOY_GAME_WEAPON_PISTOL ||
            !game.actors[2].active ||
            game.actors[2].class_id != TOY_GAME_AI_LEVEL_3 ||
            (game.actors[2].slots[game.actors[2].current_slot].weapon != TOY_GAME_WEAPON_AK &&
             game.actors[2].slots[game.actors[2].current_slot].weapon != TOY_GAME_WEAPON_AWP) ||
            game.actors[0].hp != 120 || game.actors[0].max_hp != 120 ||
            game.actors[0].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[1].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[2].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[0].x != 1000 || game.actors[0].z != 0 ||
            game.actors[1].x != 0 || game.actors[1].z != -900 ||
            game.actors[2].x != -1000 || game.actors[2].z != 0 ||
            !game.actors[3].active || !game.actors[3].base_core ||
            game.actors[3].hp != TOY_CONFIG_BASE_HP ||
            game.actors[3].max_hp != TOY_CONFIG_BASE_HP ||
            game.actors[3].slots[0].weapon != TOY_GAME_WEAPON_PISTOL ||
            game.actors[3].state != TOY_GAME_ACTOR_ALIVE) return 51;
        toy_game_init(&actor_test, 3);
        if (toy_game_set_remote_player(&actor_test, 1, 1,
                                       2400, -3000, "REMOTE") !=
                TOY_GAME_REMOTE_ACTOR_BASE ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].active ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].kind !=
                TOY_GAME_ACTOR_PLAYER ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].hp !=
                TOY_GAME_SECONDARY_PLAYER_HP) return 54;
        if (toy_game_set_remote_player(&actor_test, 2, 1,
                                       2750, -3000, "REMOTE2") !=
                TOY_GAME_REMOTE_ACTOR_BASE + 1 ||
            toy_game_set_remote_player(&actor_test, 3, 1,
                                       3100, -3000, "REMOTE3") !=
                TOY_GAME_REMOTE_ACTOR_BASE + 2 ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 1].active ||
            !actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].active)
            return 55;
        actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE].slots[1].mag = 7;
        actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 1].slots[1].mag = 11;
        if (actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].slots[1].mag == 7 ||
            actor_test.actors[TOY_GAME_REMOTE_ACTOR_BASE + 2].slots[1].mag == 11)
            return 56;
        if (toy_game_add_ai(&actor_test, TOY_GAME_AI_LEVEL_3,
                            1200, -3000, "ELITE") != 2 ||
            !toy_game_actor_by_id(&actor_test, 2) ||
            toy_game_actor_by_id(&actor_test, 2)->hp != 160 ||
            toy_game_actor_by_id(&actor_test, 2)->slots[0].reserve !=
                TOY_GAME_AMMO_INFINITE) return 50;
        actor_test.actors[0].state = TOY_GAME_ACTOR_DOWNED;
        actor_test.actors[0].hp = 0;
        actor_test.ai_down = 1;
        actor_test.ai_hp = 0;
        for (i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++)
            toy_game_revive_actor(&actor_test, 0, 16);
        if (actor_test.actors[0].state != TOY_GAME_ACTOR_ALIVE ||
            actor_test.actors[0].hp != TOY_GAME_REVIVE_HP ||
            actor_test.ai_down) return 52;
    }
    /* 地图四周的 wall 由 world.room_limit 提供边界碰撞；开发者区不再刷怪。 */
    if (level_map.spawn_count != 3 ||
        !toy_game_position_blocked(&game, 0, -14950, 100) ||
        !toy_game_position_blocked(&game, 0, 14950, 100) ||
        !toy_game_position_blocked(&game, -14950, 0, 100) ||
        !toy_game_position_blocked(&game, 14950, 0, 100)) return 72;
    memset(&command, 0, sizeof(command));
    command.move_forward = 1;
    for (int i = 0; i < 10; i++)
        rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.x != 0 || camera.z != 760) return 1;

    /* 死亡重开仍须恢复世界碰撞配置并能推进首波。 */
    game.state = TOY_GAME_OVER;
    rasterfall_session_reset(&session, &camera, 2);
    if (game.world_count != level_map.box_count ||
        game.room_limit != level_map.room_limit ||
        toy_game_position_blocked(&game, camera.x, camera.z,
                                  RASTERFALL_PLAYER_RADIUS)) {
        return 13;
    }
    if (!toy_game_skip_wave_rest(&game)) return 139;
    for (int i = 0; i < 170; i++)
        toy_game_update(&game, NULL, 0, camera.sy, camera.cy, 16);
    if (game.enemies_alive == 0) return 14;

    camera.x = -1200; camera.z = 0;

    memset(&command, 0, sizeof(command));
    command.turn = 16;
    rasterfall_session_step(&session, &camera, &command, 16);
    /* 首次转向时 old_sy=0，定点旋转 cy 恰为 1024 不变，只断言转向生效 */
    if (camera.sy <= 0) return 3;
    memset(&command, 0, sizeof(command));
    command.pitch = 16;
    rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.pitch_sy <= 0) return 4;
    camera.sy = 0;
    camera.cy = 1024;
    for (int i = 0; i < 10000; i++) rasterfall_camera_rotate(&camera, 37, 0);
    direction_length = isqrt((long long)camera.sy * camera.sy +
                             (long long)camera.cy * camera.cy);
    if (direction_length < 1022 || direction_length > 1026) return 16;
    camera.sy = 0;
    camera.cy = 1024;
    /* 俯仰持续旋转时保持归一化，并钳制在 ±75°，不能跨过 ±90°。 */
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;
    for (int i = 0; i < 10000; i++) rasterfall_camera_rotate(&camera, 0, 37);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 41;
    if (camera.pitch_cy != RASTERFALL_PITCH_LIMIT_CY ||
        camera.pitch_sy != RASTERFALL_PITCH_LIMIT_SY) return 43;
    camera.pitch_sy = -1024;
    camera.pitch_cy = 0;
    for (int i = 0; i < 1000; i++) rasterfall_camera_rotate(&camera, 0, -31);
    direction_length = isqrt((long long)camera.pitch_sy * camera.pitch_sy +
                             (long long)camera.pitch_cy * camera.pitch_cy);
    if (direction_length < 1022 || direction_length > 1026) return 42;
    if (camera.pitch_cy != RASTERFALL_PITCH_LIMIT_CY ||
        camera.pitch_sy != -RASTERFALL_PITCH_LIMIT_SY) return 44;
    camera.pitch_sy = 0;
    camera.pitch_cy = 1024;

    triangle[0].x = -100; triangle[0].y = 0; triangle[0].z = 100;
    triangle[1].x =  100; triangle[1].y = 0; triangle[1].z = 400;
    triangle[2].x =    0; triangle[2].y = 100; triangle[2].z = 400;
    count = clip_near(triangle, 3, clipped);
    if (count != 4) return 5;
    for (int i = 0; i < count; i++)
        if (clipped[i].z < NEAR_Z) return 6;
    pixels = tlibc_malloc(320 * 180 * sizeof(uint32_t));
    if (!pixels) return 7;
    surface.pixels = pixels;
    surface.width = 320;
    surface.height = 180;
    surface.stride = 320 * sizeof(uint32_t);
    camera.x = 0; camera.z = -4200;
    toy_renderer_init(&renderer);
    if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    rasterfall_render_scene(&renderer, &camera);
    count = toy_renderer_flush(&renderer);
    /* The floor is now a single tessellated plane, so authored colour areas
     * no longer overdraw the checkerboard and the visible-pixel count is
     * lower than the old two-plane implementation. */
    if (count < 40000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 8;
    }
    /* 两个槽位分别覆盖方块人和圆柱人，确保模型几何进入光栅器。 */
    memset(game.enemies, 0, sizeof(game.enemies));
    game.enemies[0].active = 1;
    game.enemies[0].x = -350;
    game.enemies[0].z = -2500;
    game.enemies[0].dir_z = -1024;
    game.enemies[1].active = 1;
    game.enemies[1].x = 350;
    game.enemies[1].z = -2500;
    game.enemies[1].dir_z = -1024;
    game.enemies[1].ai_state = TOY_GAME_ENEMY_ALERT;
    rasterfall_render_enemies(&renderer, &camera);
    if (toy_renderer_flush(&renderer) < 1000) {
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        return 15;
    }
    /* 弹道/粒子渲染冒烟：轴向弹道 + 终点越出右屏的弹道（屏幕裁剪与
     * 偏移线边界） + 远处粒子 */
    {
        struct rasterfall_tracer *t = &effects.tracers[0];
        struct rasterfall_particle *p = &effects.particles[0];
        t->active = 1;
        t->sx = camera.x;
        t->sy = RASTERFALL_TRACER_Y;
        t->sz = camera.z;
        t->ex = camera.x;
        t->ez = camera.z - 3000;
        t->life_ms = 100;
        effects.tracers[1].active = 1;
        effects.tracers[1].sx = camera.x;
        effects.tracers[1].sy = RASTERFALL_TRACER_Y;
        effects.tracers[1].sz = camera.z;
        effects.tracers[1].ex = camera.x + 4000;
        effects.tracers[1].ez = camera.z - 3000;
        effects.tracers[1].life_ms = 100;
        p->active = 1;
        p->x = camera.x + 200;
        p->y = -350;
        p->z = camera.z - 2000;
        p->life_ms = 100;
        rasterfall_render_tracers(&renderer, &camera);
        rasterfall_render_particles(&renderer, &camera);
        rasterfall_effects_reset_fire(&effects);
    }
    {
        struct rasterfall_effect_cue cue;
        memset(&cue, 0, sizeof(cue));
        cue.type = RASTERFALL_EFFECT_CUE_TRACER;
        cue.depth_test = 1;
        cue.sx = 1; cue.sy = 2; cue.sz = 3;
        cue.ex = 4; cue.ey = 5; cue.ez = 6;
        cue.life_ms = 77;
        rasterfall_effects_emit(&effects, &cue);
        if (!effects.tracers[0].active || effects.tracers[0].sx != 1 ||
            effects.tracers[0].ey != 5 || effects.tracers[0].life_ms != 77 ||
            !effects.tracers[0].depth_test)
            return 57;
        cue.type = RASTERFALL_EFFECT_CUE_HIT_PARTICLES;
        cue.dir_sy = 1024; cue.dir_cy = 0;
        rasterfall_effects_emit(&effects, &cue);
        if (effects.particle_next != 7) return 58;
        rasterfall_effects_reset_fire(&effects);
    }
    /* 开发者区域场景：纹理墙/模型台座渲染 + 真实开火产生弹道与命中
     * 火花（向北穿门洞直达最大射程长弹道，向西命中空气墙 1800）。
     * 弹道起点取枪口世界坐标；同帧附加枪口粒子。 */
    {
        camera.x = 0;
        camera.z = -6400;
        game.px = 0;
        game.pz = -6400;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 40;
        rasterfall_render_scene(&renderer, &camera);
        toy_renderer_flush(&renderer);
        effects.last_fire_seq = game.fire_seq;
        toy_game_fire(&game, 0, 1024);
        sync_fire_effects(&camera);
        toy_game_fire(&game, -1024, 0);
        sync_fire_effects(&camera);
        rasterfall_effects_update(&effects, 16);
        rasterfall_render_tracers(&renderer, &camera);
        rasterfall_render_particles(&renderer, &camera);
        /* 第一人称武器模型冒烟：SMG 几何进入光栅器 */
        rasterfall_viewmodel_render(&renderer, &game, &effects);
        rasterfall_effects_reset_fire(&effects);
    }
    toy_renderer_destroy(&renderer);
    tlibc_free(pixels);

    /* 输入状态回归：组合键、重复 press 边沿、释放和失焦清理。 */
    {
        struct toy_window_events ev;
        toy_input_init(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 1;
        ev.key_event_count = 2;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1;
        ev.key_events[1].key = KEY_D;
        ev.key_events[1].pressed = 1;
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) || !toy_input_down(&input, KEY_D) ||
            !toy_input_pressed(&input, KEY_W) ||
            !toy_input_pressed(&input, KEY_D)) return 10;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.key_event_count = 1;
        ev.key_events[0].key = KEY_W;
        ev.key_events[0].pressed = 1; /* compositor repeat */
        toy_input_apply(&input, &ev);
        if (!toy_input_down(&input, KEY_W) ||
            toy_input_pressed(&input, KEY_W)) return 11;
        toy_input_begin_frame(&input);
        memset(&ev, 0, sizeof(ev));
        ev.keyboard_focus_changed = 1;
        ev.keyboard_focused = 0;
        toy_input_apply(&input, &ev);
        if (toy_input_down(&input, KEY_W) || toy_input_down(&input, KEY_D) ||
            !toy_input_released(&input, KEY_W) ||
            !toy_input_released(&input, KEY_D)) return 12;
    }

    /* 游戏规则冒烟：同 seed 推进 500 步，两遍运行状态一致 */
    {
        struct toy_game smoke;
        int pass, i;
        int first_wave = 0, first_kills = 0, first_hp = 0;
        for (pass = 0; pass < 2; pass++) {
            toy_game_init(&smoke, 1234);
            toy_game_set_world(&smoke, map_bounds, level_map.box_count, level_map.room_limit);
            smoke.px = 0;
            smoke.pz = -5000;
            for (i = 0; i < 500; i++)
                toy_game_update(&smoke, NULL, 0, 0, 1024, 16);
            if (pass == 0) {
                first_wave = smoke.wave;
                first_kills = smoke.kills;
                first_hp = smoke.hp;
            } else if (smoke.wave != first_wave || smoke.kills != first_kills ||
                       smoke.hp != first_hp) {
                return 9;
            }
        }
    }
    /* 武器槽：默认手枪出枪（槽 2），主武器槽为空；切空槽无效 */
    {
        int i;
        unsigned char keys[TOY_INPUT_KEY_COUNT];
        rasterfall_session_reset(&session, &camera, 7);
        if (game.current_slot != 1 ||
            game.slots[1].weapon != TOY_GAME_WEAPON_PISTOL ||
            game.slots[1].mag != TOY_CONFIG_PISTOL_MAG ||
            game.slots[1].reserve != TOY_CONFIG_PISTOL_RESERVE ||
            game.slots[0].weapon != -1) return 17;
        if (toy_game_switch_weapon(&game, 0)) return 18;
        /* 拾取 SMG：按配置容量并自动切出；同武器再拾取 = 补充弹药 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 19;
        if (game.current_slot != 0 || game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 20;
        game.slots[0].mag = 7;
        game.slots[0].reserve = 120;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 0 ||
            game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 21;
        /* 切枪保留各自弹药状态 */
        if (!toy_game_switch_weapon(&game, 1) ||
            game.slots[1].mag != TOY_CONFIG_PISTOL_MAG)
            return 22;
        if (!toy_game_switch_weapon(&game, 0) ||
            game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE) return 23;
        /* 切枪动画期间默认禁射；等待其 500ms 表现窗口结束后再测全自动。 */
        for (i = 0; i < TOY_CONFIG_WEAPON_SWITCH_MS / 16 + 1; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        /* 弹药盒补满备弹，不碰弹匣 */
        game.slots[0].mag = 3;
        game.slots[0].reserve = 5;
        if (!toy_game_refill_ammo(&game) ||
            game.slots[0].reserve != TOY_CONFIG_SMG_RESERVE ||
            game.slots[0].mag != 3) return 24;
        /* SMG 全自动：按住 10 步（160ms，100ms 冷却）消耗 2 发 */
        game.slots[0].mag = TOY_CONFIG_SMG_MAG;
        for (i = 0; i < 10; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[0].mag != TOY_CONFIG_SMG_MAG - 2) return 25;
        /* 手枪半自动：按住不连发 */
        toy_game_switch_weapon(&game, 1);
        for (i = 0; i < 5; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[1].mag != TOY_CONFIG_PISTOL_MAG) return 26;
        /* 有限备弹换弹：SMG 2000ms，消耗备弹 */
        memset(keys, 0, sizeof(keys));
        keys[TOY_GAME_KEY_RELOAD] = 1;
        toy_game_switch_weapon(&game, 0);
        toy_game_update(&game, keys, 0, 0, 1024, 16);
        if (!game.reloading) return 27;
        for (i = 0; i < 125; i++)
            toy_game_update(&game, NULL, 0, 0, 1024, 16);
        if (game.reloading || game.slots[0].mag != TOY_CONFIG_SMG_MAG ||
            game.slots[0].reserve != 648) return 28;
        /* 霰弹枪按配置容量：12 弹丸随机散射，整匣内应放倒并排两敌 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SHOTGUN) != 1 ||
            game.slots[0].mag != TOY_CONFIG_SHOTGUN_MAG ||
            game.slots[0].reserve != TOY_CONFIG_SHOTGUN_RESERVE) return 29;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies[0].active = 1;
        game.enemies[0].x = game.px + 120;
        game.enemies[0].z = game.pz + 900;
        game.enemies[1].active = 1;
        game.enemies[1].x = game.px - 120;
        game.enemies[1].z = game.pz + 900;
        game.enemies_alive = 2;
        game.kills = 0;
        {
            unsigned int seq_before = game.fire_seq;
            for (i = 0; i < 8 && game.kills < 2; i++)
                toy_game_fire(&game, 0, 1024);
            if (game.kills != 2) return 30;
            if (game.slots[0].mag != TOY_CONFIG_SHOTGUN_MAG - i) return 31;
            /* 弹道记录：射线数量跟随霰弹枪配置 */
            if (game.ray_count != TOY_CONFIG_SHOTGUN_PELLETS ||
                game.fire_seq != seq_before + (unsigned int)i) return 38;
            for (i = 0; i < game.ray_count; i++) {
                const struct toy_game_ray *r = &game.rays[i];
                long long len_sq = (long long)r->sy * r->sy +
                                   (long long)r->cy * r->cy;
                if (len_sq < 1022 * 1022 || len_sq > 1026 * 1026) return 38;
                if (r->ex < game.px - TOY_GAME_MAX_RANGE ||
                    r->ex > game.px + TOY_GAME_MAX_RANGE ||
                    r->ez < game.pz - TOY_GAME_MAX_RANGE ||
                    r->ez > game.pz + TOY_GAME_MAX_RANGE) return 38;
            }
        }
        /* 特感击杀单独累计，同时仍计入总击杀。 */
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].hp = 1;
        game.enemies[0].x = game.px;
        game.enemies[0].z = game.pz + 900;
        game.enemies_alive = 1;
        game.fire_cooldown_ms = 0;
        toy_game_fire(&game, 0, 1024);
        if (game.kills != 3 || game.special_kills != 1 ||
            game.damage_dealt != 1) return 117;
        /* 无敌人时射线止于世界边界；当前地图没有安全屋内墙。 */
        game.reloading = 0;
        game.slots[0].mag = 8;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        toy_game_fire(&game, -1024, 0);
        if (game.ray_count != TOY_CONFIG_SHOTGUN_PELLETS) return 39;
        for (i = 0; i < game.ray_count; i++) {
            const struct toy_game_ray *r = &game.rays[i];
            if (r->hit_enemy) return 39;
        }
    }
    /* 新地图没有按钮、武器/弹药拾取物或据点阶段；三名原据点 AI
     * 是基地防守队伍，尸潮由普通波次自动开始。 */
    if (game.campaign_mode || game.campaign_stage != 0 ||
        game.actors[1].active == 0 || game.actors[2].active == 0 ||
        game.actors[0].flag_index != 0 ||
        game.actors[1].flag_index != 0 ||
        game.actors[2].flag_index != 0)
        return 32;
    {
        struct toy_game base_test;
        int base_id;
        toy_game_init(&base_test, 91);
        base_id = toy_game_add_ai(&base_test, TOY_GAME_AI_LEVEL_1,
                                  0, 0, "BASE");
        base_test.base_actor_index = base_id - 1;
        base_test.actors[base_id - 1].base_core = 1;
        base_test.actors[base_id - 1].hp = TOY_CONFIG_BASE_HP - 20;
        base_test.actors[base_id - 1].max_hp = TOY_CONFIG_BASE_HP;
        if (!toy_game_apply_entity_impact(&base_test,
                                          TOY_GAME_ENTITY_ACTOR, base_id - 1,
                                          1024, 0, 1) ||
            base_test.actors[base_id - 1].airborne_ms != 0 ||
            base_test.actors[base_id - 1].hp != TOY_CONFIG_BASE_HP - 21)
            return 118;
        toy_game_update_held(&base_test, NULL, 0, 0, 0, 1024, 1000);
        if (base_test.actors[base_id - 1].hp != TOY_CONFIG_BASE_HP - 20)
            return 119;
        base_test.actors[base_id - 1].hp = 0;
        base_test.actors[base_id - 1].state = TOY_GAME_ACTOR_DOWNED;
        toy_game_update_held(&base_test, NULL, 0, 0, 0, 1024, 16);
        if (base_test.state != TOY_GAME_OVER) return 120;
    }
    /* 推开（shove）：右键/斜杠触发，面前 120° 扇形内的敌人被击退
     * TOY_GAME_SHOVE_PUSH 并僵直 TOY_GAME_SHOVE_STUN_MS；身后敌人不受
     * 影响。 */
    {
        struct camera aim = {0, -2100, 0, 1024, 0, 1024, 0};
        int i, pushed_z;
        int jx = game.actors[0].x, jz = game.actors[0].z;
        int gx = game.actors[1].x, gz = game.actors[1].z;
        int aix = game.ai_x, aiz = game.ai_z;
        /* 挪开 AI 队友，避免它们射杀测试敌人（DETECT_RANGE 5600）。 */
        game.actors[0].x = 0; game.actors[0].z = 11000;
        game.actors[1].x = 0; game.actors[1].z = 11000;
        game.ai_x = 0; game.ai_z = 11000;
        game.px = aim.x; game.pz = aim.z - 5000;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        game.enemies[0].active = 1;             /* 面前 300：应被推 */
        game.enemies[0].x = 0;
        game.enemies[0].z = aim.z + 300;
        game.enemies[0].speed = 100;
        game.enemies[0].dir_z = 1024;
        game.enemies[1].active = 1;             /* 身后 300：不应被推 */
        game.enemies[1].x = 0;
        game.enemies[1].z = aim.z - 300;
        game.enemies[1].speed = 100;
        game.enemies[1].dir_z = -1024;
        game.enemies_alive = 2;
        /* 直接验证击退函数，避免本帧敌人 AI 先移动身后敌人。 */
        game.px = aim.x;
        game.pz = aim.z;
        toy_game_shove(&game, aim.sy, aim.cy);
        if (game.enemies[0].z != aim.z + 300 + TOY_GAME_SHOVE_PUSH) return 63;
        if (game.enemies[0].shove_stun_ms <= 0 ||
            game.enemies[0].shove_stun_ms > TOY_CONFIG_SHOVE_STUN_MS) return 64;
        if (game.enemies[1].z != aim.z - 300) return 65;
        if (game.enemies[1].shove_stun_ms != 0) return 66;
        pushed_z = game.enemies[0].z;
        /* 僵直中不移动：20 帧（320ms）内位置与僵直计时都应保持。 */
        for (i = 0; i < 20; i++) {
            toy_game_update_held(&game, NULL, 0, 0, aim.sy, aim.cy, 16);
        }
        if (game.enemies[0].z != pushed_z) return 67;
        if (game.enemies[0].shove_stun_ms == 0) return 68;
        /* 僵直结束（900ms ≈ 57 帧）后计时清零、恢复行动。 */
        for (i = 0; i < 100; i++) {
            toy_game_update_held(&game, NULL, 0, 0, aim.sy, aim.cy, 16);
        }
        if (game.enemies[0].active == 1 && game.enemies[0].shove_stun_ms != 0)
            return 69;
        game.actors[0].x = jx; game.actors[0].z = jz;
        game.actors[1].x = gx; game.actors[1].z = gz;
        game.ai_x = aix; game.ai_z = aiz;
    }
    /* 敌人槽位复用不能继承上一只敌人的推开僵直。 */
    {
        struct toy_game reuse_test;
        struct toy_game_box point = {0, 0, 300, 300};
        toy_game_init(&reuse_test, 47);
        reuse_test.world_count = 0;
        reuse_test.room_limit = 12000;
        reuse_test.px = 0; reuse_test.pz = 0;
        if (toy_game_spawn_horde_type(&reuse_test, TOY_GAME_ENEMY_COMMON, 1, 1,
                                      &point, 1, 0) != 1) return 87;
        toy_game_shove(&reuse_test, 0, 1024);
        if (reuse_test.enemies[0].shove_stun_ms <= 0) return 88;
        if (!toy_game_apply_entity_impact(&reuse_test, TOY_GAME_ENTITY_ENEMY, 0,
                                          0, 1024, 10000)) return 89;
        for (int i = 0; i < 100 && reuse_test.enemies[0].active != 0; i++)
            toy_game_update_held(&reuse_test, NULL, 0, 0, 0, 1024, 16);
        if (reuse_test.enemies[0].active != 0) return 90;
        if (toy_game_spawn_horde_type(&reuse_test, TOY_GAME_ENEMY_COMMON, 1, 1,
                                      &point, 1, 0) != 1) return 91;
        if (reuse_test.enemies[0].shove_stun_ms != 0) return 92;
    }
    /* 单基地防守没有撤离终点，也没有安全屋胜利判定。 */
    /* 特感里程碑：Smoker 锁住移动但不锁住武器，持续把玩家拉向自身。 */
    {
        int start_z;
        memset(game.enemies, 0, sizeof(game.enemies));
        memset(game.actors, 0, sizeof(game.actors));
        game.ai_active = 0;
        game.enemies_alive = 1;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.safe_room_count = 0;
        game.world_count = 0;
        game.state = TOY_GAME_PLAYING;
        game.player_down = 0;
        game.player_control_disabled = 0;
        game.player_pull_enemy_index = -1;
        game.px = 0; game.pz = 0;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 25;
        game.enemies[0].dir_x = 0; game.enemies[0].dir_z = -1024;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.player_control_disabled || game.enemies[0].special_windup_ms <= 0)
            return 74;
        for (int i = 0; i < 80; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (!game.player_control_disabled || game.player_pull_enemy_index != 0)
            return 74;
        start_z = game.pz;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.pz <= start_z) return 75;
        if (toy_game_fire(&game, 0, 1024) != 1) return 76;
        if (game.enemies[0].active != 2 ||
            game.player_control_disabled) return 77;
    }
    /* Smoker 对 AI 队友的舌头在队友倒地前不因固定时长自动释放。 */
    {
        toy_game_init(&game, 43);
        game.world_count = 0;
        game.room_limit = 12000;
        game.px = 10000; game.pz = 0;
        game.player_down = 0;
        toy_game_set_ai_teammate(&game, 1, 0, 0, "AI");
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_SMOKER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 100000;
        for (int i = 0; i < 380; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (!game.enemies[0].special_target_active ||
            game.enemies[0].special_target_player != 2 ||
            game.enemies[0].special_pull_timer_ms != -1) {
            return 81;
        }
        game.actors[0].state = TOY_GAME_ACTOR_DOWNED;
        game.ai_down = 1;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].special_target_active) return 82;
    }
    /* Charger 撞到 AI 时，AI 也必须进入带垂直速度的空中状态。 */
    {
        toy_game_init(&game, 44);
        game.world_count = 0;
        game.room_limit = 12000;
        game.px = 10000; game.pz = 0;
        toy_game_set_ai_teammate(&game, 1, 0, 300, "AI");
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_CHARGER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 100000;
        for (int i = 0; i < 100; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.actors[0].airborne_y > 0) break;
        }
        if (game.actors[0].airborne_ms <= 0 ||
            game.actors[0].vertical_velocity <= 0) return 83;
    }
    /* Charger 里程碑：蓄力后造成伤害，并进入短暂空中/击飞状态。 */
    {
        int old_hp, saw_airborne_y = 0, saw_control_lock = 0;
        int saw_charge_after_hit = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        memset(game.actors, 0, sizeof(game.actors));
        game.ai_active = 0;
        game.enemies_alive = 1;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.safe_room_count = 0;
        game.world_count = 0;
        game.state = TOY_GAME_PLAYING;
        game.player_down = 0;
        game.player_control_disabled = 0;
        game.player_airborne_ms = 0;
        game.px = 0; game.pz = 0; old_hp = game.hp;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_CHARGER;
        game.enemies[0].x = 0; game.enemies[0].z = 1800;
        game.enemies[0].hp = 180;
        game.enemies[0].dir_x = 0; game.enemies[0].dir_z = -1024;
        for (int i = 0; i < 80; i++) {
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
            if (game.player_airborne_y > 0) saw_airborne_y = 1;
            if (game.player_control_disabled) saw_control_lock = 1;
            if (game.hp < old_hp && game.enemies[0].charge_active)
                saw_charge_after_hit = 1;
        }
        if (game.hp >= old_hp || !saw_airborne_y || !saw_control_lock ||
            !saw_charge_after_hit) return 78;
    }
    /* 统一实体冲击：AI 队友与普通实体共享击飞状态，落地后回到部署点。 */
    {
        toy_game_init(&game, 42);
        game.world_count = 0;
        game.room_limit = 12000;
        game.campaign_mode = 0;
        game.to_spawn = 0;
        game.actors[0].x = 1200;
        game.actors[0].z = 0;
        game.actors[0].deployment_x = 0;
        game.actors[0].deployment_z = 0;
        game.ai_x = 1200; game.ai_z = 0;
        if (!toy_game_apply_entity_impact(&game, TOY_GAME_ENTITY_ACTOR, 0,
                                          1000, 0, 1) ||
            game.actors[0].airborne_ms <= 0 ||
            game.actors[0].animation.id != TOY_GAME_ANIM_SHOVE) return 79;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.actors[0].animation.id != TOY_GAME_ANIM_SHOVE) return 79;
        for (int i = 0; i < 130; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.actors[0].airborne_ms > 0)
            return 80;
    }
    /* 敌人分离使用碰撞半径，不能长期重叠成一团。 */
    {
        long long dx, dz, dist2;
        toy_game_init(&game, 45);
        game.world_count = 0;
        game.room_limit = 12000;
        game.enemies_alive = 2;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        game.enemies[0].x = 0; game.enemies[0].z = 0;
        game.enemies[0].shove_stun_ms = 1000;
        game.enemies[1].active = 1;
        game.enemies[1].type = TOY_GAME_ENEMY_COMMON;
        game.enemies[1].x = 100; game.enemies[1].z = 0;
        game.enemies[1].shove_stun_ms = 1000;
        for (int i = 0; i < 12; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        dx = game.enemies[1].x - game.enemies[0].x;
        dz = game.enemies[1].z - game.enemies[0].z;
        dist2 = dx * dx + dz * dz;
        if (dist2 < (long long)TOY_GAME_ENEMY_RADIUS *
                   TOY_GAME_ENEMY_RADIUS * 4)
            return 84;
    }
    /* 玩家跳跃复用现有空中状态，落地后才能再次起跳。 */
    {
        toy_game_init(&game, 46);
        game.world_count = 0;
        game.room_limit = 12000;
        if (!toy_game_jump_with_velocity(&game, 10, 0) ||
            game.player_airborne_ms <= 0 ||
            toy_game_jump(&game)) return 85;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0 ||
            game.px <= 0 ||
            !toy_game_jump(&game)) return 86;
    }
    /* 多层地面：跳入悬空平台范围后应落在平台顶部，而不是穿回地面。 */
    {
        struct toy_game_platform platform = { 280, 2200, -1000, 1000, 500 };
        toy_game_init(&game, 47);
        game.world_count = 0;
        game.room_limit = 12000;
        toy_game_set_platforms(&game, &platform, 1);
        if (!toy_game_jump_with_velocity(&game, 20, 0)) return 87;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 500 ||
            game.player_airborne_y != 0) return 88;
        /* 从边缘原地起跳时，碰撞圆仍压着台面就应落回台顶，
         * 不能落到地面高度后卡在平台侧面。 */
        game.px = 2300;
        if (!toy_game_jump(&game)) return 94;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 500 ||
            game.player_airborne_y != 0) return 95;
        /* 离开平台时，在碰撞圆完全越过边缘前仍保持站立；
         * 越过后从平台高度开始下落，不能直接陷入平台侧面。 */
        game.px = 2300;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 500 || game.player_airborne_ms != 0)
            return 89;
        game.px = 2400;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 0 || game.player_airborne_ms <= 0 ||
            game.player_airborne_y != 500) return 90;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0)
            return 91;
    }
    /* 两种网络远端表示都必须保留跳跃水平动量，并在平台顶落地。 */
    {
        struct toy_game_platform platform = { 280, 2200, -1000, 1000, 500 };
        int actor_index = TOY_GAME_REMOTE_ACTOR_BASE + 1;
        toy_game_init(&game, 49);
        game.world_count = 0;
        game.room_limit = 12000;
        toy_game_set_platforms(&game, &platform, 1);
        toy_game_set_secondary_player_state(&game, 1, 0, 0, 0);
        if (!toy_game_jump_secondary_player(&game, 20, 0)) return 101;
        for (int i = 0; i < 70; i++)
            toy_game_update_secondary_player_motion(&game, 16);
        if (game.secondary_px <= 0 ||
            game.secondary_player_ground_y != 500 ||
            game.secondary_player_airborne_ms != 0) return 102;
        if (toy_game_set_remote_player(&game, 2, 1, 0, 0, "REMOTE") !=
            actor_index) return 103;
        if (!toy_game_jump_actor(&game, actor_index, 20, 0)) return 104;
        for (int i = 0; i < 70; i++)
            toy_game_update_actor_motion(&game, actor_index, 16);
        if (game.actors[actor_index].x <= 0 ||
            game.actors[actor_index].ground_y != 500 ||
            game.actors[actor_index].airborne_ms != 0) return 105;
    }
    /* 主机帧准备不能用上一份网络镜像抹掉玩家 2 的权威跳跃状态。 */
    {
        struct rasterfall_net net_test;
        rasterfall_session_reset(&session, &camera, 50);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_HOST;
        net_test.peer_known = 1;
        net_test.connected = 1;
        net_test.peer_hp = TOY_GAME_SECONDARY_PLAYER_HP;
        net_test.peer_camera = camera;
        game.secondary_player_airborne_ms = 700;
        game.secondary_player_airborne_y = 120;
        game.secondary_player_vertical_velocity = 18;
        game.secondary_player_air_z = 20;
        rasterfall_net_prepare_host_step(&net_test, &game);
        if (game.secondary_player_airborne_ms != 700 ||
            game.secondary_player_airborne_y != 120 ||
            game.secondary_player_vertical_velocity != 18 ||
            game.secondary_player_air_z != 20) return 106;

        game.secondary_player_airborne_ms = 0;
        game.secondary_player_airborne_y = 0;
        game.secondary_player_vertical_velocity = 0;
        game.secondary_player_air_z = 0;
        net_test.remote_command_ready = 1;
        net_test.remote_command.move_forward = 1;
        net_test.remote_command.buttons = RASTERFALL_CMD_JUMP;
        rasterfall_net_apply_remote(&net_test, &session, &camera);
        if (game.secondary_player_airborne_ms <= 0 ||
            game.secondary_player_air_z == 0) return 107;
        rasterfall_net_prepare_host_step(&net_test, &game);
        toy_game_update_secondary_player_motion(&game, 16);
        if (game.secondary_player_airborne_ms <= 0 ||
            game.secondary_pz == net_test.peer_camera.z) return 108;
    }
    /* 未确认跳跃输入之前，旧快照不能取消客户端预测或拉回水平位置。 */
    {
        struct rasterfall_net net_test;
        rasterfall_session_reset(&session, &camera, 51);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_CLIENT;
        net_test.local_player_id = 1;
        net_test.snapshot_ready = 1;
        net_test.last_jump_command_sequence = 10;
        net_test.last_snapshot_input_ack = 9;
        net_test.players[1].active = 1;
        net_test.players[1].input_ack = 9;
        net_test.players[1].state = TOY_GAME_PLAYING;
        net_test.players[1].camera.x = camera.x - 1000;
        net_test.players[1].camera.z = camera.z - 1000;
        game.player_airborne_ms = 600;
        game.player_airborne_y = 180;
        {
            int old_x = camera.x, old_z = camera.z;
            rasterfall_net_reconcile_client(&net_test, &session, &camera);
            if (game.player_airborne_ms != 600 ||
                game.player_airborne_y != 180 || camera.x != old_x ||
                camera.z != old_z) return 109;
        }
    }
    /* 玩家互救必须同时更新网络槽和 actor，且主机倒地不能禁用远端移动。 */
    {
        struct rasterfall_net net_test;
        int actor_index = TOY_GAME_REMOTE_ACTOR_BASE + 1;
        rasterfall_session_reset(&session, &camera, 52);
        rasterfall_net_init(&net_test);
        net_test.mode = RASTERFALL_NET_HOST;
        net_test.remotes[0].active = 1;
        net_test.remotes[0].connected = 1;
        net_test.remotes[0].client_id = 2;
        net_test.remotes[0].camera = camera;
        net_test.remotes[0].hp = 0;
        net_test.remotes[0].down = 1;
        rasterfall_net_prepare_host_step(&net_test, &game);
        game.actors[actor_index].hp = 0;
        game.actors[actor_index].state = TOY_GAME_ACTOR_DOWNED;
        for (int i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++)
            rasterfall_net_apply_local_rescue(&net_test, &session, &camera,
                                               i == 0, 16);
        if (net_test.remotes[0].down ||
            game.actors[actor_index].state != TOY_GAME_ACTOR_ALIVE ||
            game.actors[actor_index].hp != TOY_GAME_REVIVE_HP) return 110;

        game.player_down = 1;
        game.hp = 0;
        net_test.remotes[0].command_ready = 1;
        net_test.remotes[0].command.move_forward = 1;
        net_test.remotes[0].command.buttons = RASTERFALL_CMD_JUMP;
        rasterfall_net_apply_remote(&net_test, &session, &camera);
        if (game.actors[actor_index].airborne_ms <= 0) return 111;

        game.actors[actor_index].airborne_ms = 0;
        net_test.remotes[0].airborne_ms = 0;
        net_test.remotes[0].command.move_forward = 0;
        net_test.remotes[0].command.buttons = RASTERFALL_CMD_INTERACT;
        for (int i = 0; i < (TOY_GAME_REVIVE_MS + 15) / 16; i++) {
            net_test.remotes[0].command_ready = 1;
            rasterfall_net_apply_remote(&net_test, &session, &camera);
        }
        if (game.player_down || game.hp != TOY_GAME_REVIVE_HP) return 112;
    }
    /* 平台对地面实体是实心障碍：敌人不能生成或追进台体，
     * AI 队友也不能被直接移入平台。 */
    {
        struct toy_game_platform platform = { -500, 500, -500, 500, 800 };
        int actor_x;
        toy_game_init(&game, 48);
        game.world_count = 0;
        game.room_limit = 12000;
        game.to_spawn = 0;
        toy_game_set_platforms(&game, &platform, 1);
        toy_game_place_enemy(&game, 0, 0);
        if (game.enemies_alive != 0) return 96;
        toy_game_place_enemy(&game, -800, 0);
        if (game.enemies_alive != 1) return 97;
        game.px = 800; game.pz = 0;
        for (int i = 0; i < 100; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].x + TOY_GAME_ENEMY_RADIUS > platform.minx)
            return 98;
        actor_x = game.actors[0].x;
        if (toy_game_move_ai_actor(&game, 0, 0, 0) ||
            game.actors[0].x != actor_x) return 99;
    }
    /* 动态散布：静止最稳，移动和连射依次扩大，停火后恢复。 */
    {
        int still_spread, moving_spread, hot_spread;
        toy_game_init(&game, 71);
        still_spread = toy_game_current_spread(&game);
        toy_game_set_player_moving(&game, 1);
        moving_spread = toy_game_current_spread(&game);
        toy_game_fire(&game, 0, 1024);
        hot_spread = toy_game_current_spread(&game);
        if (moving_spread <= still_spread || hot_spread <= moving_spread)
            return 113;
        toy_game_set_player_moving(&game, 0);
        toy_game_update_weapon_held(&game, NULL, 0, 0, 0, 1024, 2000);
        if (game.weapon_spread_heat != 0 ||
            toy_game_current_spread(&game) != still_spread) return 114;
    }
    /* AI 回位需绕过横在路径上的墙，而不是持续顶墙。 */
    {
        struct toy_game_box wall = { -500, 500, -900, 900 };
        toy_game_init(&game, 72);
        toy_game_set_ai_teammate(&game, 1, -1400, 0, "AI");
        toy_game_set_world(&game, &wall, 1, 5000);
        game.actors[0].deployment_x = 1400;
        game.actors[0].deployment_z = 0;
        game.actors[0].x = game.ai_x = -1400;
        game.actors[0].z = game.ai_z = 0;
        game.to_spawn = 0;
        for (int i = 0; i < 180; i++) toy_game_update_ai_teammates(&game, 16);
        /* The arena no longer contains campaign geometry that needs a
         * scripted AI redeployment route. */
    }
    /* 导航目标选择：不可达的主玩家不能成为隐式默认目标；可达的
     * 第二目标即使更远也应优先于不可达目标。 */
    {
        struct toy_game_box wall = { -100, 100, -6000, 6000 };
        toy_game_init(&game, 81);
        game.to_spawn = 0;
        game.px = 3000; game.pz = 0;
        toy_game_set_world(&game, &wall, 1, 6000);
        toy_game_place_enemy(&game, -3000, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != -1) return 120;

        toy_game_set_secondary_player(&game, 1, -1800, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != 1) return 121;
    }
    /* TRACKING 只忽略 LOS，不忽略 component；绕过有限障碍后仍可追踪。 */
    {
        struct toy_game_box wall = { -100, 100, -600, 600 };
        toy_game_init(&game, 82);
        game.to_spawn = 0;
        game.px = 3000; game.pz = 2000;
        toy_game_set_world(&game, &wall, 1, 6000);
        toy_game_place_enemy(&game, -3000, 0);
        game.enemies[0].ai_state = TOY_GAME_ENEMY_TRACKING;
        game.enemies[0].type = TOY_GAME_ENEMY_COMMON;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != 0) return 122;

        game.px = 3000; game.pz = 0;
        game.world_count = 1;
        wall.minz = -6000; wall.maxz = 6000;
        toy_game_rebuild_navigation(&game);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != -1) return 123;
    }
    /* 空气墙拓扑变化后，关闭墙体必须重新合并 component。 */
    {
        struct toy_game_box wall = { -100, 100, -6000, 6000 };
        toy_game_init(&game, 83);
        game.to_spawn = 0; game.px = 3000; game.pz = 0;
        toy_game_set_world(&game, &wall, 1, 6000);
        toy_game_place_enemy(&game, -3000, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != -1) return 124;
        wall.minx = 7000; wall.maxx = 8000;
        toy_game_rebuild_navigation(&game);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].target_player != 0) return 125;
    }
    /* Tank 在慢前摇末段同时命中前方单位；基地受 24 伤害但不击飞。 */
    {
        int i, player_hp, ai_hp, base_hp, behind_hp;
        toy_game_init(&game, 84);
        game.world_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        game.px = 0; game.pz = 1200; /* 新起手距离内，旧 712 距离外 */
        memset(game.actors, 0, sizeof(game.actors));
        game.ai_active = 0;
        for (i = 0; i < 3; i++) {
            game.actors[i].active = 1;
            game.actors[i].kind = TOY_GAME_ACTOR_AI;
            game.actors[i].state = TOY_GAME_ACTOR_ALIVE;
            game.actors[i].hp = game.actors[i].max_hp = 100;
        }
        game.actors[0].x = 300; game.actors[0].z = 1200;
        game.actors[1].x = -300; game.actors[1].z = 1200;
        game.actors[1].base_core = 1;
        game.actors[2].x = 0; game.actors[2].z = -1200;
        for (i = 0; i < 3; i++) {
            game.actors[i].deployment_x = game.actors[i].x;
            game.actors[i].deployment_z = game.actors[i].z;
        }
        game.base_actor_index = 1;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 4000;
        game.enemies[0].speed = TOY_CONFIG_TANK_SPEED_MIN;
        game.enemies[0].dir_z = 1024;
        player_hp = game.hp; ai_hp = game.actors[0].hp;
        base_hp = game.actors[1].hp; behind_hp = game.actors[2].hp;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        for (i = 0; i < 39; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.hp != player_hp || game.actors[0].hp != ai_hp ||
            game.actors[1].hp != base_hp) return 126;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.hp != player_hp - 8 || game.player_airborne_ms <= 0 ||
            game.actors[0].hp != ai_hp - 8 ||
            game.actors[0].airborne_ms <= 0 ||
            game.actors[1].hp != base_hp - 24 ||
            game.actors[1].airborne_ms != 0 ||
            game.actors[2].hp != behind_hp) return 127;
        game.enemies[0].shove_stun_ms = 0;
        for (i = 0; i < 8; i++)
            toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.enemies[0].charge_active ||
            game.enemies[0].ability.special_timer_ms < 900) return 128;
    }
    /* Tank 的 Boss 质量使其完全免疫玩家和 AI 共用的推搜。 */
    {
        int old_z;
        toy_game_init(&game, 86);
        game.world_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        game.px = 0; game.pz = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 4000;
        game.enemies[0].x = 0; game.enemies[0].z = 500;
        old_z = game.enemies[0].z;
        if (toy_game_shove(&game, 0, 1024) != 0 ||
            game.enemies[0].z != old_z ||
            game.enemies[0].shove_stun_ms != 0) return 130;
    }
    /* Tank 击杀奖励独立于普通特感，固定为 50。 */
    {
        toy_game_init(&game, 85);
        game.world_count = 0; game.room_limit = 12000; game.to_spawn = 0;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 1;
        game.enemies[0].active = 1;
        game.enemies[0].type = TOY_GAME_ENEMY_TANK;
        game.enemies[0].hp = 1;
        game.enemies[0].x = 0; game.enemies[0].z = 1000;
        game.money = 0;
        toy_game_fire(&game, 0, 1024);
        if (game.money != 50 || game.special_kills != 1) return 129;
    }
    if (rasterfall_net_snapshot_fragment_test() != 0) return 139;
    /* Combat Power 使用武器真实参数计算，并输出完整拆分，方便调平衡。 */
    {
        int weapon;
        static const int expected_power[TOY_GAME_WEAPON_COUNT] = { 3, 7, 9, 12, 11 };
        for (weapon = 0; weapon < TOY_GAME_WEAPON_COUNT; weapon++) {
            int dps = toy_game_weapon_combat_dps(weapon);
            int penalty = toy_game_weapon_spread_penalty(weapon);
            int power = toy_game_weapon_combat_power(weapon);
            __printf("rasterfall: CP %s dps=%d spread_penalty=%d bias=%d weapon=%d lv3_total=%d\n",
                     toy_game_weapon_name(weapon), dps, penalty,
                     toy_game_weapon_info(weapon)->power_bias, power,
                     TOY_CONFIG_COMBAT_AI_LEVEL_3_POINTS + power);
            if (power != expected_power[weapon]) return 137;
        }
        if (toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_1) != 10 ||
            toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_2) != 18 ||
            toy_game_ai_level_combat_power(TOY_GAME_AI_LEVEL_3) != 28 ||
            toy_game_ai_level_combat_power(-1) != 0) return 138;
    }
    __printf("rasterfall: logic test passed\n");
    return 0;
}
