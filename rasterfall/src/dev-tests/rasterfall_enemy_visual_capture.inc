/* Process-only acceptance: existing spawn/update APIs and real enemy renderer.
 * The fixture owns all mutation; rendering is checked against a byte copy. */
static int enemy_rig_acceptance(const char *output)
{
    static const int types[]={TOY_GAME_ENEMY_PURSUIT_COMMON,TOY_GAME_ENEMY_PURSUIT_FAST,
        TOY_GAME_ENEMY_PURSUIT_HEAVY,TOY_GAME_ENEMY_SMOKER,TOY_GAME_ENEMY_CHARGER,TOY_GAME_ENEMY_TANK};
    static const char *names[]={"common","fast","heavy","smoker","charger","tank"};
    static const char *states[]={"idle","walk","windup","pre-impact","impact","follow-through","recover","charge"};
    static const char *views[]={"front","three-quarter","side"};
    static struct toy_game before;
    int result=1;
    /* Real-mask edge observation is idempotent across repeat renders. */
    {
        struct toy_game_enemy e;
        struct enemy_rig_input in;
        memset(&e,0,sizeof(e)); e.active=1; e.type=TOY_GAME_ENEMY_CHARGER;
        enemy_rig_slot=0;
        enemy_rig_observe(&e,0);
        e.charge_active=1; e.charge_elapsed_ms=200;
        enemy_rig_observe(&e,0);
        if (enemy_rig_adapt(&e).impact_age_ms>=0) goto done;
        e.ability.charge_hit_actor_mask=1;
        enemy_rig_observe(&e,0); enemy_rig_observe(&e,0);
        if (enemy_rig_adapt(&e).impact_age_ms!=0) goto done;
        e.charge_elapsed_ms=240; enemy_rig_observe(&e,0);
        if (enemy_rig_adapt(&e).impact_age_ms!=40) goto done;
        e.type=TOY_GAME_ENEMY_TANK; e.charge_elapsed_ms=TOY_CONFIG_TANK_IMPACT_MS;
        in=enemy_rig_adapt(&e);
        if (in.attack_ms!=TOY_CONFIG_TANK_IMPACT_MS) goto done;
        e.charge_active=0; e.ability.special_timer_ms=TOY_CONFIG_TANK_COOLDOWN_MS;
        in=enemy_rig_adapt(&e);
        if (in.attack_ms!=-1 || in.recovery_ms!=0) goto done;
        struct enemy_rig_pose attack_end,recover_start;
        const struct enemy_rig_profile *profile=enemy_rig_profile(e.type);
        enemy_rig_sample(profile,&in,&recover_start);
        e.charge_active=1; e.charge_elapsed_ms=TOY_CONFIG_TANK_WINDUP_MS;
        in=enemy_rig_adapt(&e); enemy_rig_sample(profile,&in,&attack_end);
        if (memcmp(&attack_end,&recover_start,sizeof(attack_end))) goto done;
        struct vec3 fist[3];
        int key_times[3]={TOY_CONFIG_TANK_IMPACT_MS*2/3,TOY_CONFIG_TANK_IMPACT_MS,
            TOY_CONFIG_TANK_WINDUP_MS};
        for (int k=0;k<3;k++) {
            struct vec3 center={0,-55,35};
            e.charge_elapsed_ms=key_times[k]; in=enemy_rig_adapt(&e);
            enemy_rig_sample(profile,&in,&attack_end);
            fist[k]=enemy_rig_point(profile,&attack_end,ER_HAND_R,center);
            __printf("enemy rig: tank fist t=%d xyz=%d,%d,%d\n",key_times[k],fist[k].x,fist[k].y,fist[k].z);
        }
        if (fist[0].z>=fist[1].z || fist[2].x>=fist[1].x || fist[2].x>=0) goto done;
        __printf("enemy rig: truth adapter / hit edge / repeat render / recovery PASS\n");
        memset(enemy_rig_observers,0,sizeof(enemy_rig_observers)); enemy_rig_slot=-1;
    }
    {
        static struct rasterfall_effects hit_effects;
        memset(&before,0,sizeof(before));
        rasterfall_effects_init(&hit_effects);
        before.enemies[0].active=1; before.enemies[0].type=TOY_GAME_ENEMY_CHARGER;
        before.actors[0].active=1;
        rasterfall_effects_sync_enemy_feedback(&hit_effects,&before);
        if (hit_effects.instance_next!=0) goto done;
        before.enemies[0].ability.charge_hit_actor_mask=1;
        rasterfall_effects_sync_enemy_feedback(&hit_effects,&before);
        int count=hit_effects.instance_next;
        if (!count) goto done;
        rasterfall_effects_sync_enemy_feedback(&hit_effects,&before);
        if (hit_effects.instance_next!=count) goto done;
        before.enemies[0].ability.charge_hit_actor_mask=0;
        rasterfall_effects_sync_enemy_feedback(&hit_effects,&before);
        before.enemies[0].ability.charge_hit_actor_mask=1;
        rasterfall_effects_sync_enemy_feedback(&hit_effects,&before);
        if (hit_effects.instance_next!=2*count) goto done;
        __printf("enemy rig: actual-hit VFX / dedup / next attack PASS\n");
    }
    memset(game.enemies,0,sizeof(game.enemies)); game.enemies_alive=0;
    for (int i=0;i<6;i++) {
        struct toy_game_box point={11000+i*1200,11000+i*1200,-13500,-13500,0,0};
        if (toy_game_spawn_horde_type(&game,types[i],1,1,&point,1,0)!=1) goto done;
    }
    /* Fixed diagnostic snapshots, not a second gameplay simulation. Adapter
     * still reads real ability fields; hit fixture explicitly sets a real mask. */
    for (int subject=0;subject<6;subject++) for (int state=0;state<8;state++) {
        if (subject<3 && state!=0) continue;
        if (subject==3 && state>1) continue;
        if (subject==4 && (state==1 || state==3 || state==5)) continue;
        if (subject==5 && (state==1 || state==7)) continue;
        for (int shot=0;shot<7;shot++) {
            struct toy_renderer renderer;
            struct toy_surface surface;
            struct camera camera;
            char path[512],label[160];
            int world=shot>=4, view=shot<3?shot:1;
            int distance=world ? (shot==4?3500:shot==5?7000:14000):3600;
            for (int i=0;i<6;i++) game.enemies[i].active=i==subject?1:0;
            struct toy_game_enemy *e=&game.enemies[subject];
            memset(&e->ability,0,sizeof(e->ability));
            e->x=11000; e->z=-13500; e->ground_y=0; e->airborne_y=0;
            e->dir_x=0; e->dir_z=-1024;
            enemy_rig_capture_move=state==1;
            enemy_rig_capture_hit_age=-1;
            enemy_visual_capture_pose=state==1?2:1;
            if (subject==4) {
                if (state==2) { e->charge_active=1; e->ability.special_timer_ms=100; }
                if (state==7 || state==4) { e->charge_active=1; e->charge_elapsed_ms=400; }
                if (state==4) { e->ability.charge_hit_actor_mask=1; enemy_rig_capture_hit_age=0; }
                if (state==6) e->ability.special_timer_ms=TOY_GAME_CHARGER_COOLDOWN_MS-210;
            }
            if (subject==5) {
                if (state>=2 && state<=5) {
                    e->charge_active=1;
                    e->charge_elapsed_ms=state==2 ? TOY_CONFIG_TANK_IMPACT_MS*2/3 :
                        state==3 ? TOY_CONFIG_TANK_IMPACT_MS-40 :
                        state==4 ? TOY_CONFIG_TANK_IMPACT_MS : TOY_CONFIG_TANK_WINDUP_MS-1;
                    e->ability.charge_hit_base=state>=4;
                    e->ability.charge_hit_actor_mask=state>=4?1:0;
                }
                if (state==6) e->ability.special_timer_ms=TOY_CONFIG_TANK_COOLDOWN_MS-210;
            }
            memset(&camera,0,sizeof(camera));
            camera.body.x=e->x; camera.body.z=e->z-distance;
            camera.view.y=-100; camera.view.cy=1024; camera.view.pitch_cy=1024;
            if (view==1) {
                camera.body.x=e->x-distance*3/5; camera.body.z=e->z-distance*4/5;
                camera.view.sy=614; camera.view.cy=819;
            } else if (view==2) {
                camera.body.x=e->x-distance; camera.body.z=e->z;
                camera.view.sy=1024; camera.view.cy=0;
            }
            if (world) {
                /* South open combat floor: fixed subject and eye height,
                 * clear of the developer strip's ramps and model displays. */
                e->x=-6000; e->z=-22000; e->dir_x=-819; e->dir_z=614;
                camera.body.x=e->x-distance; camera.body.z=e->z;
                camera.view.sy=1024; camera.view.cy=0; camera.view.y=0;
            }
            surface.width=1000; surface.height=800; surface.stride=4000;
            surface.pixels=tlibc_malloc((size_t)surface.stride*surface.height);
            toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer,0);
            if (!surface.pixels || toy_renderer_begin(&renderer,&surface,0xB4B8BC)<0) {
                toy_renderer_destroy(&renderer); tlibc_free(surface.pixels); goto done;
            }
            enemy_rig_silhouette=shot==3;
            active_fixed_floor_lighting=!world;
            before=game;
            if (world) rasterfall_render_scene(&renderer,&camera);
            else draw_partitioned_floor(&renderer,&camera);
            int submissions=enemy_rig_submissions;
            render_enemies(&renderer,&camera);
            int unchanged=!memcmp(&before,&game,sizeof(game));
            int failed=!unchanged || renderer.cmd_overflow || !renderer.cmd_count ||
                (subject>=3 && enemy_rig_submissions-submissions!=1) || toy_renderer_flush(&renderer)<0;
            const char *suffix=world?(shot==4?"world-near":shot==5?"world-mid":"world-far"):
                shot==3?"silhouette":views[view];
            snprintf(path,sizeof(path),"%s/rig-%s-%s-%s.bmp",output,names[subject],states[state],suffix);
            snprintf(label,sizeof(label),"RIG V1 %s / %s / %s / %d RFU",names[subject],states[state],suffix,distance);
            fb_draw_string((unsigned char *)surface.pixels,20,20,label,0xEEEEDD,surface.stride);
            if (!failed && rasterfall_hud_dump_bmp(path,&surface)<0) failed=1;
            __printf("enemy rig: gameplay=%s output=%s\n",unchanged?"UNCHANGED":"CHANGED",path);
            toy_renderer_destroy(&renderer); tlibc_free(surface.pixels);
            if (failed) goto done;
        }
    }
    result=0;
done:
    enemy_rig_capture_move=-1; enemy_rig_capture_hit_age=-1; enemy_rig_silhouette=0;
    return result;
}

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
    result = enemy_rig_acceptance(output);
done:
    enemy_visual_capture_pose = -1;
    rasterfall_session_unload(&session);
    return result;
}
