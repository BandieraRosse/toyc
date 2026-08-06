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
    memset(&command, 0, sizeof(command));
    command.move_forward = 1;
    for (int i = 0; i < 10; i++)
        rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.x != 0 || camera.z != -4240) return 1;

    /* 死亡重开仍须恢复世界碰撞配置并能推进首波。 */
    game.state = TOY_GAME_OVER;
    rasterfall_session_reset(&session, &camera, 2);
    if (game.world != map_bounds || game.world_count != level_map.box_count ||
        game.room_limit != level_map.room_limit ||
        toy_game_position_blocked(&game, camera.x, camera.z,
                                  RASTERFALL_PLAYER_RADIUS)) return 13;
    for (int i = 0; i < 170; i++)
        toy_game_update(&game, NULL, 0, camera.sy, camera.cy, 16);
    if (game.enemies_alive == 0) return 14;

    camera.x = -1200; camera.z = 0;
    for (int i = 0; i < 10; i++)
        rasterfall_session_step(&session, &camera, &command, 16);
    if (camera.z >= level_map.boxes[0].minz - RASTERFALL_PLAYER_RADIUS) return 2;

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
    render_scene(&renderer, &camera);
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
    render_enemies(&renderer, &camera);
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
        render_tracers(&renderer, &camera);
        render_particles(&renderer, &camera);
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
        render_scene(&renderer, &camera);
        toy_renderer_flush(&renderer);
        effects.last_fire_seq = game.fire_seq;
        toy_game_fire(&game, 0, 1024);
        sync_fire_effects(&camera);
        toy_game_fire(&game, -1024, 0);
        sync_fire_effects(&camera);
        rasterfall_effects_update(&effects, 16);
        render_tracers(&renderer, &camera);
        render_particles(&renderer, &camera);
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
    /* 地图拾取物解析：3 个控制按钮 + 2 把主武器 + 1 个弹药盒，顺序按
     * 地图文件（尸潮按钮在武器桌前）。 */
    if (interactable_count != 6 ||
        interactables[0].kind != TOY_MAP_PICKUP_BUTTON ||
        interactables[1].kind != TOY_MAP_PICKUP_SMG ||
        interactables[3].kind != TOY_MAP_PICKUP_AMMO) return 32;
    /* 固定拾取点：拾取后武器仍在场，同武器再互动 = 补充弹药 */
    highlighted = 0;
    {
        struct camera aim = {0, -7000, 0, -1024, 0, 1024};
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
        struct camera aim = {0, -7000, 0, -1024, 0, 1024};
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
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != 1) return 33;
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
        if (highlighted != 1) return 34;
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
        if (rasterfall_session_compute_highlight(&session, &aim_cam) != 3) return 37;
    }
    /* 召唤按钮：位于西侧边界，E 互动召唤 15-20 个追踪敌人，
     * 全部处于持续追踪态且落在三个刷怪点之一的矩形内；再次互动应再次召唤。 */
    {
        int n, i, had;
        if (interactables[0].x != -11940 || interactables[0].z != -10000)
            return 45;
        had = game.enemies_alive;
        {
            struct camera aim = {-10950, -10000, -1024, 0, 0, 1024};
            memset(&command, 0, sizeof(command));
            command.buttons = RASTERFALL_CMD_INTERACT;
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        n = game.enemies_alive - had;
        if (n < 15 || n > 20) return 46;
        for (i = 0; i < TOY_GAME_MAX_ENEMIES; i++) {
            int j, in_point = 0;
            const struct toy_game_enemy *e = &game.enemies[i];
            if (e->active != 1) continue;
            if (e->ai_state != TOY_GAME_ENEMY_TRACKING) return 47;
            for (j = 0; j < map_spawn_count; j++)
                if (toy_game_point_in_box(e->x, e->z,
                                          &map_spawn_zones[j]))
                    in_point = 1;
            if (!in_point) return 47;
        }
        {
            struct camera aim = {-10950, -10000, -1024, 0, 0, 1024};
            memset(&command, 0, sizeof(command));
            command.buttons = RASTERFALL_CMD_INTERACT;
            rasterfall_session_step(&session, &aim, &command, 16);
        }
        if (game.enemies_alive <= had + n) return 48;
    }
    __printf("rasterfall: logic test passed\n");
    return 0;
}
