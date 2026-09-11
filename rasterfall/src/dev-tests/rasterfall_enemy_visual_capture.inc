/* Process-only acceptance: existing spawn/update APIs and real enemy renderer.
 * The fixture owns all mutation; rendering is checked against a byte copy. */
int rasterfall_render_enemy_visual_capture(const char *output)
{
    static struct rasterfall_session session;
    static struct rasterfall_effects visual_effects;
    static struct toy_game before;
    static const int types[] = {TOY_GAME_ENEMY_PURSUIT_COMMON,
        TOY_GAME_ENEMY_PURSUIT_FAST, TOY_GAME_ENEMY_PURSUIT_HEAVY};
    static const char *names[] = {"common", "fast", "heavy"};
    static const char *poses[] = {"bind", "idle", "move"};
    static const char *views[] = {"front", "side", "three-quarter"};
    struct rasterfall_render_context context;
    struct camera camera;
    int result = 1, subject, frame, spawned = 0;
    if (!output || !*output || strlen(output) > 400) return 1;
    if (tlibc_recursive_mkdir(output) < 0 ||
        rasterfall_session_load(&session, "rasterfall/assets/maps/rasterfall.map") < 0)
        return 1;
    rasterfall_session_reset(&session, &camera, 20260911);
    rasterfall_effects_init(&visual_effects);
    memset(&context, 0, sizeof(context));
    context.session = &session;
    context.effects = &visual_effects;
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    rasterfall_render_bake_lightmap();
    memset(game.enemies, 0, sizeof(game.enemies));
    game.enemies_alive = 0;
    for (subject = 0; subject < 3; subject++) {
        struct toy_game_box point = {11000 + subject*1200,11000 + subject*1200,
                                     -13500,-13500,0,0};
        spawned += toy_game_spawn_horde_type(&game, types[subject],1,1,&point,1,0);
    }
    if (spawned != 3) goto done;
    /* Exercise real movement before freezing the observation fixture. */
    int initial_x = game.enemies[0].x, initial_z = game.enemies[0].z;
    for (frame = 0; frame < 12; frame++) toy_game_update_world(&game,16);
    if (game.enemies[0].x == initial_x && game.enemies[0].z == initial_z) {
        __fprintf(2,"enemy visual: spawn movement failed\n"); goto done;
    }
    __printf("enemy visual: spawn API=%d simulation=192ms movement=PASS mode=%s\n",
             spawned, enemy_visual_family == RASTERFALL_ENEMY_VISUAL_AUTO ?
             "AUTO" : enemy_visual_family_name(enemy_visual_family));
    /* Isolated views still draw the spawned enemy slots through render_enemies. */
    for (frame = 0; frame < 33; frame++) {
        struct toy_renderer renderer;
        struct toy_surface surface;
        char path[512], label[160];
        int world = frame >= 27 && frame < 30;
        int death = frame >= 30;
        int distance = world ? (frame == 27 ? 5120 : frame == 28 ? 15360 : 51200) : 1700;
        int view = frame < 27 ? frame % 3 : 0;
        int pose_id = frame < 27 ? frame / 3 % 3 : 2;
        subject = frame < 27 ? frame / 9 : 1;
        int expected_infected = 0;
        for (int i = 0; i < 3; i++) {
            struct toy_game_enemy *e = &game.enemies[i];
            e->active = world || death || i == subject ? 1 : 0;
            e->x = world || death ? 11000+i*1200 : 12200;
            e->z = -13500;
            e->dir_x = 0; e->dir_z = -1024;
            if (world) {
                e->x = -20000 + distance;
                e->z = -22000 + (1-i)*1200;
                e->dir_x = -1024; e->dir_z = 0;
            }
            e->ground_y = 0; e->airborne_y = 0;
            if (death) {
                e->active = 2; e->dying_ms = frame == 30 ? 850 : frame == 31 ? 240 : 400;
                visual_effects.enemy_death_style[i] = frame == 32 ?
                    RASTERFALL_ENEMY_DEATH_STYLE_LEGACY : RASTERFALL_ENEMY_DEATH_STYLE_DISSOLVE;
                visual_effects.enemy_hit_dir_x[i] = 650;
                visual_effects.enemy_hit_dir_z[i] = 800;
            }
            if (e->active && enemy_visual_family_for_enemy(e->type, i) !=
                    RASTERFALL_ENEMY_VISUAL_LEGACY)
                expected_infected++;
        }
        if (death) distance = 3800;
        memset(&camera, 0, sizeof(camera));
        camera.body.x = 12200; camera.body.z = -13500-distance;
        camera.view.y = -350; camera.view.cy = 1024; camera.view.pitch_cy = 1024;
        if (view == 1) {
            camera.body.x = 12200-distance; camera.body.z = -13500;
            camera.view.sy = 1024; camera.view.cy = 0;
        } else if (view == 2) {
            camera.body.x = 12200-distance*3/5; camera.body.z = -13500-distance*4/5;
            camera.view.sy = 614; camera.view.cy = 819;
        }
        if (world) {
            camera.body.x = -20000; camera.body.z = -22000;
            camera.view.sy = 1024; camera.view.cy = 0;
            camera.view.y = 0; /* FPS eye height: 900 RFU above ground. */
        }
        enemy_visual_capture_pose = pose_id;
        active_fixed_floor_lighting = !world;
        surface.width = world || death ? 1600 : 800; surface.height = 800;
        surface.stride = surface.width*4;
        surface.pixels = tlibc_malloc((size_t)surface.stride*surface.height);
        toy_renderer_init(&renderer);
        toy_renderer_set_frame_budget(&renderer, 0);
        if (!surface.pixels || toy_renderer_begin(&renderer,&surface,0x30343B) < 0) {
            toy_renderer_destroy(&renderer); tlibc_free(surface.pixels); goto done;
        }
        before = game;
        if (world && frame != 29) rasterfall_render_scene(&renderer,&camera);
        else if (frame == 29) {
            /* The real map has perimeter walls before 100m. Keep a separate,
             * explicitly labelled unoccluded distance check, with true scale. */
            rasterfall_sky_draw(&renderer.surface,&camera);
            active_fixed_floor_lighting = 1;
            draw_partitioned_floor(&renderer,&camera);
        }
        int submissions_before = enemy_visual_submissions;
        render_enemies(&renderer,&camera);
        int unchanged = !memcmp(&before,&game,sizeof(game));
        int expected = expected_infected;
        int submitted = enemy_visual_submissions-submissions_before;
        int commands = renderer.cmd_count;
        int failed = renderer.cmd_overflow || !commands || !unchanged ||
                     submitted != expected || enemy_visual_failures ||
                     toy_renderer_flush(&renderer)<0;
        if (world) {
            snprintf(path,sizeof(path),"%s/%s-%dm.bmp",output,frame == 29 ? "distance" : "world",distance/512);
            snprintf(label,sizeof(label),"%s COMMON / BLOCK_INFECTED / HUMANOID_INFECTED - %dm - %s",
                     frame == 29 ? "UNOBSTRUCTED DISTANCE" : "WORLD SPAWN",distance/512,
                     enemy_visual_family == RASTERFALL_ENEMY_VISUAL_AUTO ? "MIXED" :
                     enemy_visual_family_name(enemy_visual_family));
        } else if (death) {
            snprintf(path,sizeof(path),"%s/death-%d.bmp",output,frame-30);
            snprintf(label,sizeof(label),"EXISTING DEATH PRESENTATION %d",frame-30);
        } else {
            snprintf(path,sizeof(path),"%s/%s-%s-%s.bmp",output,names[subject],poses[pose_id],views[view]);
            snprintf(label,sizeof(label),"%s / %s / %s - %s / %s",names[subject],poses[pose_id],views[view],
                     enemy_visual_type_name(types[subject]),
                     enemy_visual_family_name(enemy_visual_family_for_enemy(types[subject], subject)));
        }
        fb_draw_string((unsigned char *)surface.pixels,24,24,label,0xE7E7DB,surface.stride);
        if (!failed && rasterfall_hud_dump_bmp(path,&surface)<0) failed=1;
        __printf("enemy visual: frame=%d submissions=%d commands=%d gameplay=%s output=%s\n",
                 frame,submitted,commands,unchanged ? "UNCHANGED":"CHANGED",path);
        toy_renderer_destroy(&renderer); tlibc_free(surface.pixels);
        if (failed) goto done;
    }
    result = 0;
done:
    enemy_visual_capture_pose = -1;
    rasterfall_session_unload(&session);
    return result;
}
