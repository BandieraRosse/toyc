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
    {
        struct toy_game actor_test;
        int i;
        const struct toy_game_actor *actor =
            toy_game_actor_by_id_const(&game, 1);
        const struct toy_game_enemy_info *common =
            toy_game_enemy_info(TOY_GAME_ENEMY_COMMON);
        const struct toy_game_enemy_info *fast =
            toy_game_enemy_info(TOY_GAME_ENEMY_FAST);
        const struct toy_game_enemy_info *heavy =
            toy_game_enemy_info(TOY_GAME_ENEMY_HEAVY);
        const struct toy_game_enemy_info *brute =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_HEAVY);
        const struct toy_game_enemy_info *runner =
            toy_game_enemy_info(TOY_GAME_ENEMY_PURSUIT_FAST);
        const struct toy_game_weapon_info *smg =
            toy_game_weapon_info(TOY_GAME_WEAPON_SMG);
        const struct toy_game_weapon_info *shotgun =
            toy_game_weapon_info(TOY_GAME_WEAPON_SHOTGUN);
        if (!actor || actor->kind != TOY_GAME_ACTOR_AI ||
            actor->class_id != TOY_GAME_AI_LEVEL_2 ||
            fast->speed_min <= common->speed_max ||
            heavy->max_hp <= common->max_hp ||
            heavy->bite_damage <= common->bite_damage ||
            runner->speed_min <= fast->speed_max ||
            smg->damage != 25 || shotgun->damage != 20 ||
            common->max_hp != 50 || heavy->max_hp != 200 ||
            brute->max_hp != 200 || runner->max_hp != 60) return 49;
        if (!game.actors[1].active ||
            game.actors[1].class_id != TOY_GAME_AI_LEVEL_1 ||
            game.actors[1].slots[0].weapon != TOY_GAME_WEAPON_PISTOL ||
            !game.actors[2].active ||
            game.actors[2].class_id != TOY_GAME_AI_LEVEL_3 ||
            game.actors[2].slots[0].weapon != TOY_GAME_WEAPON_SHOTGUN ||
            game.actors[0].hp != 0 || game.actors[0].max_hp != 120 ||
            game.actors[0].state != TOY_GAME_ACTOR_DOWNED ||
            game.actors[1].state != TOY_GAME_ACTOR_DOWNED ||
            game.actors[2].state != TOY_GAME_ACTOR_DOWNED ||
            game.actors[0].x != 0 || game.actors[0].z != 4200 ||
            game.actors[1].x != 0 || game.actors[1].z != -2000 ||
            game.actors[2].x != -700 || game.actors[2].z != 4200) return 51;
        toy_game_init(&actor_test, 3);
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
    if (camera.x != 0 || camera.z != -4240) return 1;

    /* 死亡重开仍须恢复世界碰撞配置并能推进首波。 */
    game.state = TOY_GAME_OVER;
    rasterfall_session_reset(&session, &camera, 2);
    if (game.world_count != level_map.box_count ||
        game.room_limit != level_map.room_limit ||
        toy_game_position_blocked(&game, camera.x, camera.z,
                                  RASTERFALL_PLAYER_RADIUS)) {
        return 13;
    }
    for (int i = 0; i < 170; i++)
        toy_game_update(&game, NULL, 0, camera.sy, camera.cy, 16);
    if (game.enemies_alive == 0) return 14;

    camera.x = -1200; camera.z = 0;
    /* 野区掩体 C1（文件首个 box 已不再是 C1，改按坐标查找）应拦住北行。 */
    {
        int c1_minz = 0, i;
        for (i = 0; i < level_map.box_count; i++)
            if (level_map.boxes[i].minx == -1700 &&
                level_map.boxes[i].maxx == -700) {
                c1_minz = level_map.boxes[i].minz;
                break;
            }
        for (i = 0; i < 10; i++)
            rasterfall_session_step(&session, &camera, &command, 16);
        if (camera.z >= c1_minz - RASTERFALL_PLAYER_RADIUS) return 2;
    }

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
    /* 破碎画面曾仍返回正数；完整 320x180 场景应稳定超过此下限。 */
    if (count < 50000) {
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
            game.slots[1].mag != 30 ||
            game.slots[1].reserve != TOY_GAME_AMMO_INFINITE ||
            game.slots[0].weapon != -1) return 17;
        if (toy_game_switch_weapon(&game, 0)) return 18;
        /* 拾取 SMG：50/650 并自动切出；同武器再拾取 = 补充弹药 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 1) return 19;
        if (game.current_slot != 0 || game.slots[0].mag != 50 ||
            game.slots[0].reserve != 650) return 20;
        game.slots[0].mag = 7;
        game.slots[0].reserve = 120;
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SMG) != 0 ||
            game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 21;
        /* 切枪保留各自弹药状态 */
        if (!toy_game_switch_weapon(&game, 1) || game.slots[1].mag != 30)
            return 22;
        if (!toy_game_switch_weapon(&game, 0) ||
            game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 23;
        /* 弹药盒补满备弹，不碰弹匣 */
        game.slots[0].mag = 3;
        game.slots[0].reserve = 5;
        if (!toy_game_refill_ammo(&game) || game.slots[0].reserve != 650 ||
            game.slots[0].mag != 3) return 24;
        /* SMG 全自动：按住 10 步（160ms，100ms 冷却）消耗 2 发 */
        game.slots[0].mag = 50;
        for (i = 0; i < 10; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[0].mag != 48) return 25;
        /* 手枪半自动：按住不连发 */
        toy_game_switch_weapon(&game, 1);
        for (i = 0; i < 5; i++)
            toy_game_update_held(&game, NULL, 0, 1, 0, 1024, 16);
        if (game.slots[1].mag != 30) return 26;
        /* 有限备弹换弹：SMG 2000ms，消耗备弹 */
        memset(keys, 0, sizeof(keys));
        keys[TOY_GAME_KEY_RELOAD] = 1;
        toy_game_switch_weapon(&game, 0);
        toy_game_update(&game, keys, 0, 0, 1024, 16);
        if (!game.reloading) return 27;
        for (i = 0; i < 125; i++)
            toy_game_update(&game, NULL, 0, 0, 1024, 16);
        if (game.reloading || game.slots[0].mag != 50 ||
            game.slots[0].reserve != 648) return 28;
        /* 霰弹枪 8/64：10 弹丸随机散射，整匣内应放倒并排两敌 */
        if (toy_game_equip_weapon(&game, TOY_GAME_WEAPON_SHOTGUN) != 1 ||
            game.slots[0].mag != 8 || game.slots[0].reserve != 64) return 29;
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
            if (game.slots[0].mag != 8 - i) return 31;
            /* 弹道记录：每枪 10 条射线，方向已归一化，终点不越最大射程 */
            if (game.ray_count != 10 ||
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
        /* 无敌人时射线止于墙：向西命中起点安全室西墙（x=-1800 面）。
         * 弹丸有随机偏转，但终点都应落在该墙面范围内。 */
        game.reloading = 0;
        game.slots[0].mag = 8;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        toy_game_fire(&game, -1024, 0);
        if (game.ray_count != 10) return 39;
        for (i = 0; i < game.ray_count; i++) {
            const struct toy_game_ray *r = &game.rays[i];
            if (r->hit_enemy || !r->hit_world) return 39;
            if (r->ex < game.px - 1803 || r->ex > game.px - 1797) return 39;
            if (r->ez < -5700 || r->ez > -3800) return 39;
        }
    }
    /* 地图拾取物解析：10 个控制按钮 + 4 把主武器 + 2 个弹药盒。 */
    {
        int has_horde_button = 0, has_base1 = 0, has_base2 = 0;
        int has_smoker = 0, has_charger = 0, i;
        for (i = 0; i < interactable_count; i++) {
            if (interactables[i].kind == TOY_MAP_PICKUP_BUTTON)
                has_horde_button = 1;
            if (interactables[i].kind == TOY_MAP_PICKUP_BASE_1_BUTTON)
                has_base1 = 1;
            if (interactables[i].kind == TOY_MAP_PICKUP_BASE_2_BUTTON)
                has_base2 = 1;
            if (interactables[i].kind == TOY_MAP_PICKUP_SMOKER_BUTTON)
                has_smoker = 1;
            if (interactables[i].kind == TOY_MAP_PICKUP_CHARGER_BUTTON)
                has_charger = 1;
        }
        if (interactable_count != 16 || !has_horde_button ||
            !has_base1 || !has_base2 || !has_smoker || !has_charger) {
            return 32;
        }
    }
    /* 固定拾取点：拾取后武器仍在场，同武器再互动 = 补充弹药 */
    highlighted = 0;
    {
        struct camera aim = {0, -7000, 0, -1024, 0, 1024, 0};
        memset(&command, 0, sizeof(command));
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&session, &aim, &command, 16);
    }
    if (game.slots[0].weapon != TOY_GAME_WEAPON_SMG ||
        game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 33;
    game.slots[0].mag = 3;
    game.slots[0].reserve = 5;
    highlighted = 0;
    {
        struct camera aim = {0, -7000, 0, -1024, 0, 1024, 0};
        memset(&command, 0, sizeof(command));
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&session, &aim, &command, 16);
    }
    if (game.slots[0].mag != 50 || game.slots[0].reserve != 650) return 33;
    /* 拾取不影响高亮：SMG 固定点仍可被选中 */
    {
        struct camera aim_cam;
        aim_cam.x = 0;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.pitch_sy = 0;
        aim_cam.pitch_cy = 1024;
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != 0) return 33;
    }
    /* 高亮判定：世界空间朝向锥。面向桌子（武器低于视平线）可选中最远
     * 的 SMG；背对或超出距离均不可选。 */
    {
        struct camera aim_cam;
        aim_cam.x = 0;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.pitch_sy = 0;
        aim_cam.pitch_cy = 1024;
        highlighted = rasterfall_session_compute_highlight(&session, &aim_cam);
        if (highlighted != 0) return 34;
        aim_cam.sy = 1024;
        aim_cam.cy = 0;
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != -1) return 35;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        aim_cam.z = -5200;
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != -1) return 36;
        /* 斜向面向桌子东侧：只有弹药盒在锥内（两把枪横向偏角过大） */
        aim_cam.x = 1000;
        aim_cam.z = -7000;
        aim_cam.sy = 0;
        aim_cam.cy = -1024;
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != 2) return 37;
    }
    /* 召唤按钮：位于西侧边界，E 互动召唤 15-20 个追踪敌人，
     * 再次互动应再次召唤。 */
    {
        int n, had;
        if (interactables[6].x != -11940 || interactables[6].z != -10000)
            return 45;
        had = game.enemies_alive;
        {
            struct camera aim = {-10950, -10000, -1024, 0, 0, 1024, 0};
            memset(&command, 0, sizeof(command));
            command.buttons = RASTERFALL_CMD_INTERACT;
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        n = game.enemies_alive - had;
        if (n < 15 || n > 20) return 46;
        {
            struct camera aim = {-10950, -10000, -1024, 0, 0, 1024, 0};
            memset(&command, 0, sizeof(command));
            command.buttons = RASTERFALL_CMD_INTERACT;
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.enemies_alive <= had + n) return 48;
    }
    /* 据点1 按钮门控：本段起清除场上敌人与刷怪预算，隔离测试。
     * GUARD 未复活时按下据点1 按钮不得推进 stage 或召唤尸潮。 */
    memset(game.enemies, 0, sizeof(game.enemies));
    game.enemies_alive = 0;
    game.spawn_budget = 0;
    {
        struct camera aim = {-500, -2100, 0, 1024, 0, 1024, 0};
        int i;
        static const char expect[] = "REVIVE GUARD FIRST";
        memset(&command, 0, sizeof(command));
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&session, &aim, &command, 16);
        if (game.campaign_stage != 0) return 53;
        if (game.enemies_alive != 0) return 54;
        for (i = 0; expect[i]; i++)
            if (!session.banner_text || session.banner_text[i] != expect[i])
                return 55;
        if (session.banner_text[i]) return 55;
    }
    /* 救援 GUARD：靠近倒地 AI 持续互动（中途无需再按键）即可复活。 */
    {
        struct camera aim = {0, -1500, 0, -1024, 0, 1024, 0};
        int i;
        memset(&command, 0, sizeof(command));
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&session, &aim, &command, 16);
        if (game.actors[1].state == TOY_GAME_ACTOR_ALIVE) return 56;
        for (i = 0; i < 200 && game.actors[1].state != TOY_GAME_ACTOR_ALIVE; i++) {
            memset(&command, 0, sizeof(command));
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.actors[1].state != TOY_GAME_ACTOR_ALIVE) return 57;
    }
    /* 复活后按下据点1 按钮：stage 推进到 1 并召唤尸潮。 */
    {
        struct camera aim = {-500, -2100, 0, 1024, 0, 1024, 0};
        memset(&command, 0, sizeof(command));
        command.buttons = RASTERFALL_CMD_INTERACT;
        rasterfall_session_step(&session, &aim, &command, 16);
        if (game.campaign_stage != 1) return 58;
        if (game.enemies_alive == 0) return 59;
    }
    /* 推开（shove）：右键/斜杠触发，面前 120° 扇形内的敌人被击退
     * TOY_GAME_SHOVE_PUSH 并僵直 TOY_GAME_SHOVE_STUN_MS；身后敌人不受
     * 影响。 */
    {
        struct camera aim = {0, -2100, 0, 1024, 0, 1024, 0};
        int i, pushed_z;
        int jx = game.actors[0].x, jz = game.actors[0].z;
        int gx = game.actors[1].x, gz = game.actors[1].z;
        /* 挪开 AI 队友，避免它们射杀测试敌人（DETECT_RANGE 5600）。 */
        game.actors[0].x = 0; game.actors[0].z = 11000;
        game.actors[1].x = 0; game.actors[1].z = 11000;
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
            game.enemies[0].shove_stun_ms > TOY_GAME_SHOVE_STUN_MS) return 64;
        if (game.enemies[1].z != aim.z - 300) return 65;
        if (game.enemies[1].shove_stun_ms != 0) return 66;
        pushed_z = game.enemies[0].z;
        /* 僵直中不移动：20 帧（320ms）内位置与僵直计时都应保持。 */
        for (i = 0; i < 20; i++) {
            memset(&command, 0, sizeof(command));
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.enemies[0].z != pushed_z) return 67;
        if (game.enemies[0].shove_stun_ms == 0) return 68;
        /* 僵直结束（900ms ≈ 57 帧）后计时清零、恢复行动。 */
        for (i = 0; i < 40; i++) {
            memset(&command, 0, sizeof(command));
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.enemies[0].shove_stun_ms != 0) return 69;
        game.actors[0].x = jx; game.actors[0].z = jz;
        game.actors[1].x = gx; game.actors[1].z = gz;
    }
    /* 终点撤离：stage 2 后终点安全屋即可正常推进撤离进度（不锁门，
     * 最终尸潮期间玩家也可随时撤离）。 */
    {
        struct camera aim = {0, 8800, 0, 1024, 0, 1024, 0};
        int i;
        memset(game.enemies, 0, sizeof(game.enemies));
        game.enemies_alive = 0;
        game.goal_hold_ms = 0;
        game.state = TOY_GAME_PLAYING;
        if (!toy_game_set_campaign_stage(&game, 2)) return 60;
        game.campaign_phase = TOY_GAME_PHASE_HORDE;
        game.phase_timer_ms = 1000000;
        game.spawn_budget = 0;
        for (i = 0; i < 30; i++) {
            memset(&command, 0, sizeof(command));
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.goal_hold_ms <= 0) return 61;
        if (game.state != TOY_GAME_PLAYING) return 62;
    }
    /* 联机胜负由主机统一判定：终点进度必须同时满足第二名玩家，且
     * 双方倒下才进入失败状态。 */
    {
        int goal_x = game.safe_rooms[game.safe_room_count - 1].minx + 100;
        int goal_z = game.safe_rooms[game.safe_room_count - 1].minz + 100;
        game.state = TOY_GAME_PLAYING;
        game.goal_hold_ms = 0;
        game.px = goal_x; game.pz = goal_z;
        toy_game_set_secondary_player_state(&game, 1, goal_x + 3000,
                                            goal_z + 3000, 0);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.goal_hold_ms != 0) return 70;
        toy_game_set_secondary_player_state(&game, 1, goal_x, goal_z, 0);
        game.player_down = 1;
        toy_game_set_secondary_player_state(&game, 1, goal_x, goal_z, 1);
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.state != TOY_GAME_OVER) return 71;
        toy_game_set_secondary_player_state(&game, 0, 0, 0, 0);

        /* 单人状态下主机倒地时没有远端玩家可以救援，应立即失败。 */
        game.state = TOY_GAME_PLAYING;
        game.player_down = 1;
        game.player_revive_progress_ms = 0;
        toy_game_update_held(&game, NULL, 0, 0, 0, 1024, 16);
        if (game.state != TOY_GAME_OVER) return 73;
    }
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
        }
        if (game.hp >= old_hp || !saw_airborne_y || !saw_control_lock) return 78;
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
            game.actors[0].airborne_ms <= 0) return 79;
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
        struct toy_game_platform platform = { 280, 2200, -1000, 1000, 800 };
        toy_game_init(&game, 47);
        game.world_count = 0;
        game.room_limit = 12000;
        toy_game_set_platforms(&game, &platform, 1);
        if (!toy_game_jump_with_velocity(&game, 20, 0)) return 87;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 800 ||
            game.player_airborne_y != 0) return 88;
        /* 从边缘原地起跳时，碰撞圆仍压着台面就应落回台顶，
         * 不能落到地面高度后卡在平台侧面。 */
        game.px = 2300;
        if (!toy_game_jump(&game)) return 94;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_ground_y != 800 ||
            game.player_airborne_y != 0) return 95;
        /* 离开平台时，在碰撞圆完全越过边缘前仍保持站立；
         * 越过后从平台高度开始下落，不能直接陷入平台侧面。 */
        game.px = 2300;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 800 || game.player_airborne_ms != 0)
            return 89;
        game.px = 2400;
        toy_game_update_player_ground(&game);
        if (game.player_ground_y != 0 || game.player_airborne_ms <= 0 ||
            game.player_airborne_y != 800) return 90;
        for (int i = 0; i < 70; i++)
            toy_game_update_player_motion(&game, 16);
        if (game.player_airborne_ms != 0 || game.player_airborne_y != 0)
            return 91;
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
    /* 告示牌文本应保留空格后的完整字符串。 */
    {
        int found = 0;
        for (int i = 0; i < level_map.draw_count; i++) {
            if (level_map.draw[i].type != TOY_MAP_DRAW_SIGN) continue;
            found = 1;
            if (strcmp(level_map.draw[i].text, "FROM HARRAN CITY")) return 92;
        }
        if (!found) return 93;
    }
    __printf("rasterfall: logic test passed\n");
    return 0;
}
