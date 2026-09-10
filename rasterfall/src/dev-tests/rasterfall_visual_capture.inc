/* Process-only offline fixture. Scene setup stays outside humanoid rendering.
 * Included here to seed only the weapon cache needed by the normal helper,
 * without scanning the gallery or initializing a gameplay session. */
static void visual_procedural_humanoid_setup(
    struct camera *camera, struct rasterfall_procedural_humanoid_state *state)
{
    memset(camera, 0, sizeof(*camera));
    memset(state, 0, sizeof(*state));
    camera->body.z = -1900;
    camera->view.y = -350;
    camera->view.cy = 1024;
    camera->view.pitch_cy = 1024;
    state->sy = 512;
    state->cy = -887;
    state->weapon = TOY_GAME_WEAPON_PISTOL;
    state->animation_id = TOY_GAME_ANIM_IDLE;
}

static int visual_modular_attachment_regression(
    struct rasterfall_model_instance *host,
    struct rasterfall_model_resource *gear);

int rasterfall_render_visual_capture(const char *scenario, const char *output)
{
    struct camera camera;
    struct rasterfall_procedural_humanoid_state state;
    struct rasterfall_render_context context;
    struct toy_renderer renderer;
    struct toy_surface surface;
    const char *weapon_path;
    int result = 1, i, count;
    int squad = scenario && !strcmp(scenario, "hurd-squad");
    int prop_lighting = scenario && !strcmp(scenario, "lighting-props");
    int modular_teammate = scenario && !strcmp(scenario, "modular-teammate");
    if (!scenario || (!squad && !prop_lighting && !modular_teammate &&
                      strcmp(scenario, "procedural-humanoid"))) {
        __fprintf(2, "rasterfall: unknown visual scenario '%s'; available: procedural-humanoid, hurd-squad, lighting-props, modular-teammate\n",
                  scenario ? scenario : "");
        return 1;
    }
    if (!output || !*output) {
        __fprintf(2, "rasterfall: visual output path must not be empty\n");
        return 1;
    }
    visual_procedural_humanoid_setup(&camera, &state);
    count = squad ? 4 : prop_lighting || modular_teammate ? 0 : 1;
    if (squad) camera.body.z = -2600;
    if (prop_lighting || modular_teammate) {
        camera.body.z = -3200;
        camera.view.y = -450;
    }
    /* Preload just the fixture weapons, never discover gameplay actors. */
    for (i = 0; i < (prop_lighting ? 0 : modular_teammate ? 1 : squad ? 2 : 1); i++) {
        weapon_path = rasterfall_weapon_model_path(
            modular_teammate ? TOY_GAME_WEAPON_AK :
            i ? TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_PISTOL);
        if (!weapon_path || rasterfall_model_load(&gallery_models[i], weapon_path) < 0) {
            __fprintf(2, "rasterfall: cannot load visual scenario weapon: %s\n",
                      weapon_path ? weapon_path : "(no model)");
            while (i > 0) rasterfall_model_unload(&gallery_models[--i]);
            return 1;
        }
        strcpy(gallery_paths[i], weapon_path);
    }
    gallery_loaded = squad ? 2 : 1;
    memset(&context, 0, sizeof(context));
    context.session = active_session;
    context.effects = active_effects;
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    surface.width = squad || prop_lighting || modular_teammate ? 1600 : 800;
    surface.height = 800;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
    if (!surface.pixels || toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) {
        __fprintf(2, "rasterfall: cannot initialize visual framebuffer\n");
        goto done;
    }
    if (prop_lighting) {
        static const int ids[] = {
            RASTERFALL_PROP_ASSET_CRATE, RASTERFALL_PROP_ASSET_WORKBENCH,
            RASTERFALL_PROP_ASSET_VENT_UNIT,
            RASTERFALL_PROP_ASSET_INDUSTRIAL_PILLAR
        };
        for (i = 0; i < 8; i++) {
            int local = i & 3;
            struct rasterfall_prop_instance instance;
            memset(&instance, 0, sizeof(instance));
            instance.asset_id = ids[local];
            instance.x = i < 4 ? -1050 + (local & 1) * 620 :
                                  430 + (local & 1) * 620;
            instance.y = -900;
            instance.z = (local / 2) * 850;
            instance.yaw_degrees = local * 25;
            instance.scale_milli = 1000;
            rasterfall_render_set_model_lighting(i >= 4);
            if (rasterfall_render_static_prop(&renderer, &camera, &instance) < 0)
                goto done;
        }
        rasterfall_render_set_model_lighting(1);
    }
    if (modular_teammate) {
        static const int actions[] = {TOY_GAME_ANIM_IDLE, TOY_GAME_ANIM_MOVE,
            TOY_GAME_ANIM_FIRE, TOY_GAME_ANIM_RELOAD, TOY_GAME_ANIM_HIT};
        for (i = 0; i < 5; i++) {
            struct toy_game_actor actor;
            const struct toy_game_weapon_info *weapon =
                toy_game_weapon_info(TOY_GAME_WEAPON_AK);
            memset(&actor, 0, sizeof(actor));
            actor.active = 1; actor.actor_id = i + 1;
            actor.kind = TOY_GAME_ACTOR_AI;
            actor.character_id = RASTERFALL_CHARACTER_RF_RIFLEMAN;
            actor.state = TOY_GAME_ACTOR_ALIVE;
            actor.x = (i * 2 - 4) * 300; actor.cy = 1024;
            actor.current_slot = weapon->slot;
            actor.slots[weapon->slot].weapon = TOY_GAME_WEAPON_AK;
            actor.animation.id = actions[i]; actor.animation.time_ms = 180;
            actor.moving = actions[i] == TOY_GAME_ANIM_MOVE;
            actor.muzzle_flash_ms = actions[i] == TOY_GAME_ANIM_FIRE ? 40 : 0;
            if (render_modular_ai_teammate(&renderer, &camera, &actor, i) < 0)
                goto done;
        }
    }
    for (i = 0; i < count; i++) {
        if (squad) {
            state.x = (i * 2 - 3) * 440;
            state.profession_id = RASTERFALL_PROFESSION_GUNSMITH + i;
            state.weapon = i == 3 ? TOY_GAME_WEAPON_SMG : TOY_GAME_WEAPON_PISTOL;
        }
        if (rasterfall_render_procedural_humanoid(&renderer, &camera, &state,
                rasterfall_character_profile(
                    squad ? RASTERFALL_CHARACTER_HURD_GUNSMITH + i :
                            RASTERFALL_CHARACTER_HURD_GUNSMITH)) < 0) {
            __fprintf(2, "rasterfall: visual humanoid rendering failed\n");
            goto done;
        }
    }
    if (renderer.cmd_overflow || renderer.cmd_count == 0 ||
        toy_renderer_flush(&renderer) < 0) {
        __fprintf(2, "rasterfall: visual scenario rendering failed\n");
        goto done;
    }
    if (prop_lighting) {
        fb_draw_string((unsigned char *)surface.pixels, 24, 24,
                       "LIGHTING OFF", RF_COLOR_UI_TEXT, surface.stride);
        fb_draw_string((unsigned char *)surface.pixels, 824, 24,
                       "LIGHTING V1", RF_COLOR_UI_TEXT, surface.stride);
    }
    if (rasterfall_hud_dump_bmp(output, &surface) < 0) {
        __fprintf(2, "rasterfall: cannot write visual capture: %s\n", output);
        goto done;
    }
    __printf("rasterfall: visual capture scenario=%s size=%dx%d format=BMP output=%s\n",
             scenario, surface.width, surface.height, output);
    result = 0;
done:
    toy_renderer_destroy(&renderer);
    tlibc_free(surface.pixels);
    rasterfall_render_set_model_lighting(1);
    for (i = 0; i < (prop_lighting ? 0 : modular_teammate ? 1 : squad ? 2 : 1); i++)
        rasterfall_model_unload(&gallery_models[i]);
    return result;
}

static void squad_acceptance_camera(struct camera *camera, int view)
{
    int distance = 4400;
    memset(camera, 0, sizeof(*camera));
    if (view == 1) {
        camera->body.x = -distance * 724 / 1024;
        camera->body.z = distance * 724 / 1024;
        camera->view.sy = 724;
        camera->view.cy = -724;
    } else if (view == 2) {
        camera->body.x = -distance;
        camera->view.sy = 1024;
    } else {
        camera->body.z = distance;
        camera->view.cy = -1024;
    }
    camera->view.y = -350;
    camera->view.pitch_cy = 1024;
}

/* Deterministic two-squad acceptance: this uses the same modular actor
 * renderer as gameplay, with only a fixed presentation camera/roster setup. */
int rasterfall_render_squad_acceptance(const char *model_dir,
                                       const char *output_dir)
{
    static const char *views[] = {"front", "three-quarter", "side"};
    struct rasterfall_modular_actor_runtime *runtime =
        &modular_actor_runtime;
    struct rasterfall_render_context context;
    struct toy_game_actor actors[RASTERFALL_SQUAD_COUNT * RASTERFALL_SQUAD_SIZE];
    char path[512];
    int view, squad, member, actor_index, result = 1;
    if (!model_dir || !*model_dir || !output_dir || !*output_dir ||
        strlen(model_dir) > 400 || strlen(output_dir) > 400 ||
        tlibc_recursive_mkdir(output_dir) < 0)
        return 1;
    modular_actor_model_dir = model_dir;
    if (modular_actor_resources_init() < 0) {
        __fprintf(2, "rasterfall: squad acceptance resources unavailable in %s\n",
                  model_dir);
        return 1;
    }
    memset(&context, 0, sizeof(context));
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    memset(actors, 0, sizeof(actors));
    actor_index = 0;
    for (squad = 0; squad < RASTERFALL_SQUAD_COUNT; squad++) {
        const struct rasterfall_squad_roster *roster =
            rasterfall_squad_roster(squad);
        for (member = 0; member < RASTERFALL_SQUAD_SIZE; member++) {
            struct toy_game_actor *actor = &actors[actor_index];
            const struct rasterfall_roster_member *entry =
                &roster->members[member];
            const struct toy_game_weapon_info *weapon =
                toy_game_weapon_info(TOY_GAME_WEAPON_AK);
            actor->active = 1;
            actor->actor_id = actor_index + 1;
            actor->kind = TOY_GAME_ACTOR_AI;
            actor->character_id = entry->character_id;
            actor->state = TOY_GAME_ACTOR_ALIVE;
            actor->cy = 1024;
            actor->current_slot = weapon->slot;
            actor->slots[weapon->slot].weapon = TOY_GAME_WEAPON_AK;
            actor_index++;
        }
    }
    gallery_load();
    memset(runtime->instance_ready, 0, sizeof(runtime->instance_ready));
    memset(actor_action_layers, 0, sizeof(actor_action_layers));
    /* Pressure the real composition adapter on Rifleman/Engineer/Heavy.
     * Rifleman and Heavy retain WALK while FIRE occupies the upper layer;
     * Engineer exercises a plain WALK with backpack and hip gear. */
    actor_action_layers[0].lower = RASTERFALL_ACTION_LOCOMOTION_WALK;
    actor_action_layers[0].actor_id = actors[0].actor_id;
    actor_action_layers[0].valid = 1;
    actor_action_layers[0].lower_time_ms = 140;
    actors[0].animation.id = TOY_GAME_ANIM_FIRE;
    actors[0].animation.time_ms = 60;
    actors[2].animation.id = TOY_GAME_ANIM_MOVE;
    actors[2].animation.time_ms = 200;
    actor_action_layers[6].lower = RASTERFALL_ACTION_LOCOMOTION_WALK;
    actor_action_layers[6].actor_id = actors[6].actor_id;
    actor_action_layers[6].valid = 1;
    actor_action_layers[6].lower_time_ms = 140;
    actors[6].animation.id = TOY_GAME_ANIM_FIRE;
    actors[6].animation.time_ms = 60;
    for (view = 0; view < 3; view++) {
        struct camera camera;
        struct toy_renderer renderer;
        struct toy_surface surface;
        int failed = 0;
        squad_acceptance_camera(&camera, view);
        memset(&surface, 0, sizeof(surface));
        surface.width = 2400;
        surface.height = 900;
        surface.stride = surface.width * (int)sizeof(uint32_t);
        surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
        if (!surface.pixels) return 1;
        toy_renderer_init(&renderer);
        toy_renderer_set_frame_budget(&renderer, 0);
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0)
            failed = 1;
        for (actor_index = 0; actor_index <
             RASTERFALL_SQUAD_COUNT * RASTERFALL_SQUAD_SIZE && !failed;
             actor_index++) {
            int offset = (actor_index * 2 - 7) * 180;
            actors[actor_index].x =
                (int)((long long)camera.view.cy * offset / 1024);
            actors[actor_index].z =
                -(int)((long long)camera.view.sy * offset / 1024);
            if (render_modular_ai_teammate(&renderer, &camera,
                                           &actors[actor_index], actor_index) < 0)
                failed = 1;
        }
        if (!failed && (renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0 ||
            snprintf(path, sizeof(path), "%s/%s.bmp", output_dir,
                     views[view]) >= (int)sizeof(path) ||
            rasterfall_hud_dump_bmp(path, &surface) < 0))
            failed = 1;
        toy_renderer_destroy(&renderer);
        tlibc_free(surface.pixels);
        if (failed) return 1;
        __printf("rasterfall: squad acceptance view=%s output=%s actors=8\n",
                 views[view], path);
    }
    for (actor_index = 0; actor_index <
         RASTERFALL_SQUAD_COUNT * RASTERFALL_SQUAD_SIZE; actor_index++) {
        if (!runtime->instance_ready[actor_index] ||
            rasterfall_model_instance_resource(&runtime->instances[actor_index]) !=
                &runtime->body)
            return 1;
        for (member = 0; member < actor_index; member++)
            if (rasterfall_model_instance_pose(&runtime->instances[member]) ==
                rasterfall_model_instance_pose(&runtime->instances[actor_index]))
                return 1;
    }
    if (visual_modular_attachment_regression(&runtime->instances[6],
                                             runtime->gear) < 0)
        return 1;
    __printf("rasterfall: squad acceptance body_resource_shared=1 "
             "gear_resources_shared=%d model_instances=8 palette_isolated=1 "
             "attachment_regression=pass action_composition="
             "jesus_walk_fire,engineer_walk,heavy_walk_fire\n",
             RASTERFALL_GEAR_RESOURCE_COUNT);
    result = 0;
    return result;
}

/* RF Humanoid V1.1 acceptance sheet.  This is intentionally a capture-only
 * harness: it exercises the same skeletal rifle path used by AI actors while
 * keeping the asset out of gameplay character selection until art sign-off. */
static const struct rasterfall_skeletal_actor_profile visual_rf_profile = {
    "rf_humanoid_acceptance", NULL, NULL, 1736, 0, 1024
};

/* RFCHAR sources are metric: keep the frozen 2.080m V2 body at 1.736m
 * presentation height. Gear must not shrink the wearer via mesh bounds. */
static int visual_rf_scale(const struct rasterfall_model_asset *model)
{
    return model->has_character_contract ? 835 :
        character_model_scale(model, visual_rf_profile.target_height_mm);
}

static const struct rasterfall_pose_calibration *visual_rf_calibration(int mode)
{
    static struct rasterfall_pose_calibration calibration;
    calibration = *rasterfall_pose_calibration_resolve(NULL, 1, TOY_GAME_WEAPON_AK);
    calibration.offset = (struct rasterfall_cal_vec3){-35, mode == 1 ? -35 : 35, 45};
    calibration.yaw_offset = 0;
    calibration.pitch_offset = mode == 1 ? 18 : 0;
    calibration.roll_offset = 0;
    calibration.left_ik = 1;
    return &calibration;
}

static int visual_rf_check_grips(struct rasterfall_model_asset *model,
    const struct rasterfall_model_attachment_transform *frame,
    const struct rasterfall_pose_calibration *calibration, int scale)
{
    const struct rasterfall_weapon_asset_profile *weapon = rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK);
    int side, i;
    for (side=0;side<2;side++) {
        struct rasterfall_model_attachment_transform socket;
        struct rasterfall_cal_vec3 anchor=side?calibration->foregrip:calibration->grip;
        int local[3], calibrated[3];
        double error=0;
        if (rasterfall_model_character_attachment_transform(model,side?
            RASTERFALL_ATTACHMENT_FOREGRIP:RASTERFALL_ATTACHMENT_WEAPON_R,&socket)<0) return -1;
        for(i=0;i<3;i++) local[i]=((int *)&anchor)[i]-((int *)&weapon->attachment_grip)[i];
        weapon_profile_local(calibration,local,calibrated);
        for(i=0;i<3;i++) {
            double delta=socket.position[i]-frame->position[i]-
                (frame->rotation[i*3]*calibrated[0]+frame->rotation[i*3+1]*calibrated[1]+
                 frame->rotation[i*3+2]*calibrated[2])*1000.0/scale;
            error+=delta*delta;
        }
        __printf("rfchar-grip: side=%s error_rfu=%.3f\n",side?"left":"right",sqrt(error));
        if(error>16.0) return -1;
    }
    return 0;
}

static void visual_acceptance_camera(struct camera *camera, int view, int distance)
{
    memset(camera, 0, sizeof(*camera));
    if (view == 1) camera->body.x = -distance, camera->view.sy = 1024;
    else if (view == 2) camera->body.z = -distance, camera->view.cy = 1024;
    else if (view == 3) camera->body.x = -distance * 724 / 1024,
                         camera->body.z = distance * 724 / 1024,
                         camera->view.sy = 724, camera->view.cy = -724;
    else camera->body.z = distance, camera->view.cy = -1024;
    camera->view.y = -350; camera->view.pitch_cy = 1024;
}

static void visual_acceptance_pose(struct rasterfall_model_asset *model, int mode)
{
    struct rasterfall_rifle_pose pose;
    struct rasterfall_animation_composition composition;
    const struct rasterfall_pose_calibration *calibration;
    int i, axis;
    rasterfall_model_set_pose(model, RASTERFALL_MODEL_POSE_BIND);
    if (!mode) return;
    if (model->has_character_contract) {
        if (mode == 3) {
            composition_rotate_role(model, RASTERFALL_HUMANOID_LEFT_UPPER_LEG, "RF_L_UPPER_LEG", -18, 0, 0);
            composition_rotate_role(model, RASTERFALL_HUMANOID_LEFT_LOWER_LEG, "RF_L_LOWER_LEG", 30, 0, 0);
            composition_rotate_role(model, RASTERFALL_HUMANOID_RIGHT_UPPER_LEG, "RF_R_UPPER_LEG", 10, 0, 0);
            rasterfall_model_update_bones(model);
        }
        return;
    }
    calibration = rasterfall_pose_calibration_resolve(NULL, 1, TOY_GAME_WEAPON_AK);
    memcpy(pose.rotation, calibration->body_pose, sizeof(pose.rotation));
    if (mode == 1) {
        for (i = 0; i < RASTERFALL_RIFLE_POSE_BONE_COUNT; i++)
            for (axis = 0; axis < 3; axis++) pose.rotation[i][axis] =
                pose.rotation[i][axis] * 3 / 4;
    }
    memset(&composition, 0, sizeof(composition));
    composition.rifle_stance = 1; composition.upper_body_lock = 1;
    composition.rifle_pose = &pose;
    rasterfall_animation_compose(model, &composition);
    if (mode == 3) {
        composition_rotate_role(model, RASTERFALL_HUMANOID_LEFT_UPPER_LEG,
                                "RF_L_UPPER_LEG", -18, 0, 0);
        composition_rotate_role(model, RASTERFALL_HUMANOID_LEFT_LOWER_LEG,
                                "RF_L_LOWER_LEG", 30, 0, 0);
        composition_rotate_role(model, RASTERFALL_HUMANOID_RIGHT_UPPER_LEG,
                                "RF_R_UPPER_LEG", 10, 0, 0);
        rasterfall_model_update_bones(model);
    }
}

/* Fixed presentation-only strip in the existing developer zone. It shares
 * the world renderer, depth buffer, lighting and standard AK path; it never
 * creates gameplay actors or map collision. */
static const char *character_strip_model_path;

static int render_character_test_strip(struct toy_renderer *renderer,
                                       const struct camera *camera)
{
    static const int rf_x[] = {-14000, -10000, -6000};
    static const int rf_lower[] = {
        RASTERFALL_ACTION_LOCOMOTION_IDLE,
        RASTERFALL_ACTION_LOCOMOTION_IDLE,
        RASTERFALL_ACTION_LOCOMOTION_WALK};
    static const int rf_upper[] = {
        RASTERFALL_ACTION_RIFLE_IDLE,
        RASTERFALL_ACTION_RIFLE_AIM,
        RASTERFALL_ACTION_RIFLE_IDLE};
    struct rasterfall_procedural_humanoid_state old_state;
    int i, pixels = 0, frame_pixels;
    if (!active_session || active_pose_preview) return 0;
    memset(&old_state, 0, sizeof(old_state));
    old_state.x = -18000; old_state.z = -20000;
    old_state.weapon = TOY_GAME_WEAPON_AK;
    old_state.sy = 0; old_state.cy = 1024;
    old_state.animation_id = TOY_GAME_ANIM_IDLE;
    pixels += rasterfall_render_procedural_humanoid(renderer, camera, &old_state,
        rasterfall_character_profile(RASTERFALL_CHARACTER_HURD_GUNSMITH));
    /* A world capture may replace the default V2 body with a contract
     * variant.  The modular resource loader still resolves its passive gear
     * beside that body, exactly like the gameplay path. */
    modular_actor_body_model_path = character_strip_model_path;
    for (i = 0; i < 3; i++) {
        frame_pixels = render_modular_preview_frame(renderer, camera, rf_x[i],
            -20000, rf_lower[i], rf_upper[i], i);
        if (frame_pixels < 0)
            __fprintf(2, "rasterfall: character test strip modular frame unavailable\n");
        else pixels += frame_pixels;
    }
    return pixels;
}

static void character_world_capture_camera(struct camera *camera, int distance)
{
    memset(camera, 0, sizeof(*camera));
    camera->body.x = -10000 + distance * 3 / 5;
    camera->body.z = -20000 + distance * 4 / 5;
    camera->view.y = -350;
    camera->view.sy = -614; camera->view.cy = -819;
    camera->view.pitch_cy = 1024;
}

int rasterfall_render_character_world_capture(const char *output_dir,
                                              const char *model_path)
{
    static const char *names[] = {"near", "mid", "far"};
    static const int distances[] = {2000, 4000, 8000};
    int i, result = 1;
    if (!active_session || !output_dir || !*output_dir || strlen(output_dir)>450) return 1;
    if (tlibc_recursive_mkdir(output_dir) < 0) return 1;
    character_strip_model_path = model_path && *model_path ? model_path : NULL;
    for (i = 0; i < 15; i++) {
        struct camera camera;
        struct toy_renderer renderer;
        struct toy_surface surface;
        char path[512];
        uint32_t *pixels;
        int stage_result;
        int submitted_commands;
        unsigned long submitted_triangles;
        int distance_index = i < 3 ? i : (i - 3) / 4;
        static const char *subjects[] = {"old", "idle", "aim", "motion"};
        character_world_capture_camera(&camera, distances[distance_index]);
        if (i >= 3) camera.body.x = -18000 + ((i - 3) % 4) * 4000 + distances[distance_index] * 3 / 5;
        if (i < 3) snprintf(path, sizeof(path), "%s/%s.bmp", output_dir, names[distance_index]);
        else snprintf(path, sizeof(path), "%s/%s-%s.bmp", output_dir,
                      names[distance_index], subjects[(i - 3) % 4]);
        memset(&surface, 0, sizeof(surface));
        surface.width = 1280; surface.height = 720; surface.stride = 1280 * 4;
        pixels = tlibc_malloc((size_t)surface.stride * surface.height);
        if (!pixels) return 1;
        surface.pixels = pixels;
        toy_renderer_init(&renderer);
        toy_renderer_set_frame_budget(&renderer, 0);
        if (toy_renderer_begin(&renderer, &surface, 0x151922) < 0) {
            toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
        }
        rasterfall_render_scene(&renderer, &camera);
        rasterfall_render_flags(&renderer, &camera);
        rasterfall_render_enemies(&renderer, &camera);
        rasterfall_render_ai_teammate(&renderer, &camera);
        rasterfall_render_sign_text(&renderer, &camera);
        rasterfall_render_flag_text(&renderer, &camera);
        submitted_commands = renderer.cmd_count;
        submitted_triangles = renderer.submitted_triangles;
        stage_result = toy_renderer_flush(&renderer);
        if (renderer.cmd_overflow || stage_result < 0 ||
            rasterfall_hud_dump_bmp(path, &surface) < 0) {
            toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
        }
        toy_renderer_destroy(&renderer);
        tlibc_free(pixels);
        __printf("rasterfall: character world capture distance=%d output=%s\n",
                 distances[distance_index], path);
        __printf("rasterfall: character world capture raster commands=%d triangles=%lu pixels=%d\n",
                 submitted_commands, submitted_triangles, stage_result);
    }
    result = 0;
    return result;
}

static int visual_acceptance_model_frame(struct toy_renderer *renderer,
                                         const struct camera *camera,
                                         struct rasterfall_model_asset *character,
                                         struct rasterfall_model_asset *rifle,
                                         int mode, int actor_x, int actor_z,
                                         int distance, int draw_rifle)
{
    struct rasterfall_model_attachment_transform frame;
    const struct rasterfall_pose_calibration *calibration;
    int scale, center_x, center_z, base_y, pixels = 0;
    (void)rifle;
    visual_acceptance_pose(character, mode);
    scale = visual_rf_scale(character);
    center_x = actor_x - (int)(((long long)character->min_x + character->max_x) * scale / 2000);
    center_z = actor_z - (int)(((long long)character->min_z + character->max_z) * scale / 2000);
    if (character->has_character_contract) center_x = actor_x, center_z = actor_z;
    base_y = -900 - (int)((long long)character->min_y * scale / 1000);
    if (draw_rifle && rifle_frame_transform(character, &frame) == 0) {
        calibration = visual_rf_calibration(mode);
        if (calibration->left_ik)
            rifle_solve_hands(character, &frame, calibration,
                              rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK), scale);
        if (visual_rf_check_grips(character, &frame, calibration, scale)<0) return -1;
    }
    active_gallery_facing = 1; active_gallery_sy = 0; active_gallery_cy = 1024;
    pixels += render_gallery_model(renderer, camera, character,
                                   center_x, base_y, center_z, scale);
    if (draw_rifle && rifle_frame_transform(character, &frame) == 0) {
        calibration = visual_rf_calibration(mode);
        pixels += render_skeletal_rifle(renderer, camera, character, &frame,
                                         &visual_rf_profile, calibration,
                                         actor_x, base_y, actor_z, 0, 1024,
                                         TOY_GAME_WEAPON_AK, 0, 0);
    }
    active_gallery_facing = 0;
    (void)distance;
    return pixels;
}

static int visual_acceptance_dump(struct rasterfall_model_asset *character,
                                  const char *dir, const char *name, int mode,
                                  int distance, int ab)
{
    struct camera camera; struct rasterfall_render_context context;
    struct toy_renderer renderer; struct toy_surface surface;
    struct rasterfall_procedural_humanoid_state old_state;
    uint32_t *pixels; char path[512]; int view = 0, width = ab ? 1600 : 800;
    (void)character;
    __printf("rasterfall: acceptance A/B distance=%d dir=%s\n", distance, dir);
    visual_acceptance_camera(&camera, view, distance);
    memset(&context, 0, sizeof(context)); context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context); memset(&surface, 0, sizeof(surface));
    surface.width = width; surface.height = 800; surface.stride = width * (int)sizeof(uint32_t);
    pixels = tlibc_malloc((size_t)surface.stride * surface.height); surface.pixels = pixels;
    if (!pixels) return 1;
    toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer, 0);
    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) { tlibc_free(pixels); return 1; }
    if (ab) {
        memset(&old_state, 0, sizeof(old_state)); old_state.x = -500; old_state.weapon = TOY_GAME_WEAPON_AK;
        old_state.sy = 0; old_state.cy = 1024; old_state.animation_id = TOY_GAME_ANIM_IDLE;
        rasterfall_render_procedural_humanoid(&renderer, &camera, &old_state,
            rasterfall_character_profile(RASTERFALL_CHARACTER_HURD_GUNSMITH));
        if (visual_acceptance_model_frame(&renderer, &camera, character,
                                      NULL, mode, 500, 0, distance, mode != 0)<0) {
            toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
        }
    } else if (visual_acceptance_model_frame(&renderer, &camera, character,
                                         NULL, mode, 0, 0, distance, mode != 0)<0) {
        toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
    }
    if (renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0 ||
        snprintf(path, sizeof(path), "%s/%s/front.bmp", dir, name) >= (int)sizeof(path)) {
        toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
    }
    if (snprintf(path, sizeof(path), "%s/%s", dir, name) >= (int)sizeof(path) ||
        tlibc_recursive_mkdir(path) < 0 ||
        snprintf(path, sizeof(path), "%s/%s/front.bmp", dir, name) >= (int)sizeof(path) ||
        rasterfall_hud_dump_bmp(path, &surface) < 0) {
        toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
    }
    toy_renderer_destroy(&renderer); tlibc_free(pixels); return 0;
}

static int visual_instance_isolation_dump(
    const struct rasterfall_model_resource *resource, const char *output_dir)
{
    struct rasterfall_model_instance left, right;
    struct camera camera; struct toy_renderer renderer; struct toy_surface surface;
    uint32_t *pixels; char path[512]; int result = 1;
    __memset(&left, 0, sizeof(left)); __memset(&right, 0, sizeof(right));
    if (rasterfall_model_instance_init(&left, resource) < 0 ||
        rasterfall_model_instance_init(&right, resource) < 0) goto done;
    visual_acceptance_camera(&camera, 0, 1800);
    __memset(&surface, 0, sizeof(surface));
    surface.width = 1600; surface.height = 800; surface.stride = 1600 * 4;
    pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) goto done;
    surface.pixels = pixels;
    toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer, 0);
    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
        visual_acceptance_model_frame(&renderer, &camera, &left.pose, NULL,
            0, -520, 0, 1800, 0) < 0 ||
        visual_acceptance_model_frame(&renderer, &camera, &right.pose, NULL,
            2, 520, 0, 1800, 1) < 0 ||
        renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0 ||
        snprintf(path, sizeof(path), "%s/two-instance-isolation.bmp",
                 output_dir) >= (int)sizeof(path) ||
        rasterfall_hud_dump_bmp(path, &surface) < 0) {
        toy_renderer_destroy(&renderer); tlibc_free(pixels); goto done;
    }
    toy_renderer_destroy(&renderer); tlibc_free(pixels);
    __printf("rasterfall: two-instance isolation resource=%p left_pose=%p right_pose=%p output=%s\n",
             (void *)resource, (void *)left.pose.bones,
             (void *)right.pose.bones, path);
    result = 0;
done:
    rasterfall_model_instance_unload(&right);
    rasterfall_model_instance_unload(&left);
    return result;
}

static void rigid_identity(struct rasterfall_rigid_transform *transform)
{
    memset(transform,0,sizeof(*transform));
    transform->rotation[0]=transform->rotation[4]=transform->rotation[8]=1.0;
    transform->scale_milli=1000;
}

int rasterfall_render_rigid_attachment_acceptance(const char *model_dir,
                                                   const char *output_dir)
{
    struct rasterfall_model_resource body, helmet, backpack;
    struct rasterfall_model_instance left, right;
    struct rasterfall_rigid_attachment_desc head_desc, back_desc;
    struct rasterfall_rigid_transform left_actor, right_actor, numeric;
    struct rasterfall_model_attachment_transform rifle_frame;
    struct rasterfall_render_context context;
    struct camera camera; struct toy_renderer renderer; struct toy_surface surface;
    uint32_t *pixels = NULL; char path[512]; int result=1, i;
    memset(&body,0,sizeof(body));memset(&helmet,0,sizeof(helmet));memset(&backpack,0,sizeof(backpack));
    memset(&left,0,sizeof(left));memset(&right,0,sizeof(right));
    if (!model_dir || !output_dir || tlibc_recursive_mkdir(output_dir)<0) goto done;
    if (snprintf(path,sizeof(path),"%s/rf_humanoid_v2.rmesh",model_dir)>=(int)sizeof(path) ||
        rasterfall_model_resource_load(&body,path)<0) goto done;
    if (snprintf(path,sizeof(path),"%s/rf_tactical_helmet.rmesh",model_dir)>=(int)sizeof(path) ||
        rasterfall_model_resource_load(&helmet,path)<0) goto done;
    if (snprintf(path,sizeof(path),"%s/rf_backpack.rmesh",model_dir)>=(int)sizeof(path) ||
        rasterfall_model_resource_load(&backpack,path)<0) goto done;
    if (!body.definition.has_character_contract || helmet.definition.bone_count ||
        backpack.definition.bone_count || rasterfall_model_instance_init(&left,&body)<0 ||
        rasterfall_model_instance_init(&right,&body)<0) goto done;
    memset(&head_desc,0,sizeof(head_desc));memset(&back_desc,0,sizeof(back_desc));
    head_desc.host_socket=RASTERFALL_ATTACHMENT_HEAD;head_desc.resource=&helmet;
    back_desc.host_socket=RASTERFALL_ATTACHMENT_BACK;back_desc.resource=&backpack;
    rigid_identity(&head_desc.mount_correction);rigid_identity(&back_desc.mount_correction);
    rigid_identity(&left_actor);rigid_identity(&right_actor);
    left_actor.scale_milli=right_actor.scale_milli=835;
    left_actor.translation[0]=-430;right_actor.translation[0]=430;
    left_actor.translation[1]=right_actor.translation[1]=-900;
    rasterfall_model_instance_set_pose(&left,RASTERFALL_MODEL_POSE_BIND);
    for(i=0;i<3;i++) {
        const struct rasterfall_pose_calibration *calibration;
        visual_acceptance_pose(&right.pose,i);
        if(i && rifle_frame_transform(&right.pose,&rifle_frame)==0) {
            calibration=visual_rf_calibration(i);
            rifle_solve_hands(&right.pose,&rifle_frame,calibration,
                rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK),835);
        }
        if(rigid_attachment_model_to_world(&right,&head_desc,&right_actor,&numeric)<0)goto done;
        __printf("rigid-attachment: pose=%s HEAD t=(%.3f,%.3f,%.3f) r0=(%.6f,%.6f,%.6f)\n",
            i==0?"bind":i==1?"rifle-idle":"rifle-aim",numeric.translation[0],
            numeric.translation[1],numeric.translation[2],numeric.rotation[0],
            numeric.rotation[1],numeric.rotation[2]);
        if(rigid_attachment_model_to_world(&right,&back_desc,&right_actor,&numeric)<0)goto done;
    }
    rasterfall_model_instance_set_pose(&right,RASTERFALL_MODEL_POSE_BODY_TURN);
    i=rasterfall_model_humanoid_bone(&right.pose,RASTERFALL_HUMANOID_HEAD);
    if(i<0)goto done;
    right.pose.bones[i].rotate_x=18;right.pose.bones[i].rotate_y=-28;
    if(rasterfall_model_instance_update_bones(&right)<0)goto done;
    if(rigid_attachment_model_to_world(&right,&head_desc,&right_actor,&numeric)<0)goto done;
    __printf("rigid-attachment: HEAD world t=(%.3f,%.3f,%.3f) r0=(%.6f,%.6f,%.6f)\n",
        numeric.translation[0],numeric.translation[1],numeric.translation[2],
        numeric.rotation[0],numeric.rotation[1],numeric.rotation[2]);
    if(rigid_attachment_model_to_world(&right,&back_desc,&right_actor,&numeric)<0)goto done;
    __printf("rigid-attachment: BACK world t=(%.3f,%.3f,%.3f) r0=(%.6f,%.6f,%.6f)\n",
        numeric.translation[0],numeric.translation[1],numeric.translation[2],
        numeric.rotation[0],numeric.rotation[1],numeric.rotation[2]);
    memset(&context,0,sizeof(context));context.fixed_floor_lighting=1;rasterfall_render_bind(&context);
    visual_acceptance_camera(&camera,3,2200);
    memset(&surface,0,sizeof(surface));surface.width=1600;surface.height=800;surface.stride=6400;
    pixels=tlibc_malloc((size_t)surface.stride*surface.height);if(!pixels)goto done;surface.pixels=pixels;
    toy_renderer_init(&renderer);toy_renderer_set_frame_budget(&renderer,0);
    if(toy_renderer_begin(&renderer,&surface,0x30343B)<0)goto render_done;
    rasterfall_render_frontend_current(&renderer)->gallery_facing=1;
    rasterfall_render_frontend_current(&renderer)->gallery_sy=0;
    rasterfall_render_frontend_current(&renderer)->gallery_cy=1024;
    render_gallery_model(&renderer,&camera,&left.pose,-430,-900,0,835);
    rasterfall_render_rigid_attachment(&renderer,&camera,&left,&head_desc,&left_actor);
    rasterfall_render_rigid_attachment(&renderer,&camera,&left,&back_desc,&left_actor);
    render_gallery_model(&renderer,&camera,&right.pose,430,-900,0,835);
    rasterfall_render_rigid_attachment(&renderer,&camera,&right,&head_desc,&right_actor);
    rasterfall_render_rigid_attachment(&renderer,&camera,&right,&back_desc,&right_actor);
    rasterfall_render_frontend_current(&renderer)->gallery_facing=0;
    if(renderer.cmd_overflow||toy_renderer_flush(&renderer)<0||
       snprintf(path,sizeof(path),"%s/rigid-attachment-acceptance.bmp",output_dir)>=(int)sizeof(path)||
       rasterfall_hud_dump_bmp(path,&surface)<0)goto render_done;
    __printf("rasterfall: rigid attachment acceptance body_resource=%p hosts=(%p,%p) helmet_resource=%p backpack_resource=%p output=%s\n",
        (void*)&body,(void*)left.pose.bones,(void*)right.pose.bones,
        (void*)&helmet,(void*)&backpack,path);
    result=0;
render_done:
    toy_renderer_destroy(&renderer);
done:
    if(pixels)tlibc_free(pixels);
    rasterfall_model_instance_unload(&right);
    rasterfall_model_instance_unload(&left);rasterfall_model_resource_unload(&backpack);
    rasterfall_model_resource_unload(&helmet);rasterfall_model_resource_unload(&body);return result;
}

static int visual_modular_profession_frame(
    struct toy_renderer *renderer, const struct camera *camera,
    struct rasterfall_model_instance *host,
    const struct rasterfall_character_visual_recipe *recipe,
    struct rasterfall_model_resource *gear, int mode,
    int x, int z, int distance)
{
    struct rasterfall_rigid_transform actor;
    struct rasterfall_action_composition composition;
    const struct rasterfall_action_clip *lower;
    const struct rasterfall_action_clip *upper;
    const struct rasterfall_model_asset *pose;
    int pixels;
    if (!host || rasterfall_model_instance_reset_pose(host) < 0) return -1;
    memset(&composition, 0, sizeof(composition));
    lower = humanoid_action(mode == 3 ?
        RASTERFALL_ACTION_LOCOMOTION_WALK :
        RASTERFALL_ACTION_LOCOMOTION_IDLE);
    upper = humanoid_action(mode == 2 ?
        RASTERFALL_ACTION_RIFLE_AIM :
        RASTERFALL_ACTION_RIFLE_IDLE);
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].clip = lower;
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].time_ms = 180;
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].clip = upper;
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].time_ms = 180;
    if (!lower || !upper || rasterfall_action_compose(host, &composition) < 0)
        return -1;
    pose = rasterfall_model_instance_final_pose(host);
    if (!pose) return -1;
    rigid_identity(&actor);
    actor.translation[0] = x;
    actor.translation[1] = -900 -
        (int)((long long)pose->min_y * visual_rf_scale(pose) / 1000);
    actor.translation[2] = z;
    actor.scale_milli = visual_rf_scale(pose);
    active_gallery_facing = 1; active_gallery_sy = 0; active_gallery_cy = 1024;
    pixels = rasterfall_render_character_instance(renderer, camera, host,
        &actor, recipe->shirt_color, recipe->pants_color);
    if (pixels < 0) goto fail;
    if (mode)
        pixels += render_modular_active_weapon(renderer, camera, host, &actor,
            TOY_GAME_WEAPON_AK, 0, 0);
    {
        int passive_pixels = render_modular_passive_equipment(renderer, camera,
            host, recipe, gear, &actor);
        if (passive_pixels < 0) goto fail;
        pixels += passive_pixels;
    }
    active_gallery_facing = 0;
    (void)distance;
    return pixels;
fail:
    active_gallery_facing = 0;
    return -1;
}

static int visual_modular_attachment_regression(
    struct rasterfall_model_instance *host,
    struct rasterfall_model_resource *gear)
{
    static const enum rasterfall_character_attachment sockets[] = {
        RASTERFALL_ATTACHMENT_CHEST, RASTERFALL_ATTACHMENT_HIP_L,
        RASTERFALL_ATTACHMENT_HIP_R};
    static const int gear_ids[] = {RASTERFALL_GEAR_HEAVY_CHEST,
        RASTERFALL_GEAR_HEAVY_HIP_L, RASTERFALL_GEAR_HEAVY_HIP_R};
    static const char *socket_names[] = {"CHEST", "HIP_L", "HIP_R"};
    struct rasterfall_rigid_transform actor, out, bind[3];
    int mode, i;
    rigid_identity(&actor); actor.scale_milli = 835;
    for (mode = 0; mode < 4; mode++) {
        struct rasterfall_model_asset *pose;
        if (rasterfall_model_instance_reset_pose(host) < 0) return -1;
        pose = rasterfall_model_instance_pose(host);
        if (mode < 3) visual_acceptance_pose(pose, mode);
        else if (rasterfall_model_instance_set_pose(host,
                     RASTERFALL_MODEL_POSE_BODY_TURN) < 0) return -1;
        for (i = 0; i < 3; i++) {
            struct rasterfall_rigid_attachment_desc desc;
            memset(&desc, 0, sizeof(desc));
            desc.host_socket = sockets[i]; desc.resource = &gear[gear_ids[i]];
            rigid_identity(&desc.mount_correction);
            if (rigid_attachment_model_to_world(host, &desc, &actor, &out) < 0 ||
                out.scale_milli != 835 || out.translation[0] < -2000 ||
                out.translation[0] > 2000 || out.translation[1] < -2000 ||
                out.translation[1] > 2000 || out.translation[2] < -2000 ||
                out.translation[2] > 2000) return -1;
            if (!mode) bind[i] = out;
            __printf("profession-attachment: pose=%s socket=%s t=(%.3f,%.3f,%.3f) r0=(%.6f,%.6f,%.6f)\n",
                mode==0?"bind":mode==1?"rifle-idle":mode==2?"rifle-aim":"turned",
                socket_names[i], out.translation[0], out.translation[1],
                out.translation[2], out.rotation[0], out.rotation[1], out.rotation[2]);
        }
    }
    if (!(bind[1].translation[0] > 0.0 && bind[2].translation[0] < 0.0))
        return -1;
    return 0;
}

/* Six independent instances share one immutable body resource. Gear is
 * resolved from presentation recipes and every passive follower queries the
 * finalized host instance socket. Legacy carriers remain loaded only for A/B. */
int rasterfall_render_profession_lineup(const char *model_dir, const char *output_dir)
{
    static const char *views[] = {"front", "three-quarter", "side"};
    static const int view_ids[] = {0, 3, 1};
    static const char *ranges[] = {"near", "mid", "far"};
    static const int distances[] = {2200, 4400, 8800};
    struct rasterfall_model_resource body, gear[RASTERFALL_GEAR_RESOURCE_COUNT];
    struct rasterfall_model_instance hosts[RASTERFALL_MODULAR_PROFESSION_COUNT];
    struct rasterfall_model_asset carriers[RASTERFALL_MODULAR_PROFESSION_COUNT];
    static const char *profession_names[] = {
        "rifleman", "breacher", "recon", "medic", "engineer", "heavy"};
    static struct rasterfall_render_context context;
    char path[512];
    int i, v, d, result = 1;
    memset(&body, 0, sizeof(body)); memset(gear, 0, sizeof(gear));
    memset(hosts, 0, sizeof(hosts)); memset(carriers, 0, sizeof(carriers));
    if (!model_dir || !output_dir || tlibc_recursive_mkdir(output_dir) < 0) return 1;
    if (snprintf(path, sizeof(path), "%s/%s.rmesh", model_dir,
                 rasterfall_character_body_resource_name(
                     RASTERFALL_BODY_RF_HUMANOID_V2)) >= (int)sizeof(path) ||
        rasterfall_model_resource_load(&body, path) < 0) goto done;
    for (i = 0; i < RASTERFALL_GEAR_RESOURCE_COUNT; i++) {
        if (snprintf(path, sizeof(path), "%s/%s.rmesh", model_dir,
                     rasterfall_character_gear_resource_name(i)) >= (int)sizeof(path) ||
            rasterfall_model_resource_load(&gear[i], path) < 0 ||
            gear[i].definition.bone_count) {
            __fprintf(2, "rasterfall: modular profession cannot load gear %s\n",
                      rasterfall_character_gear_resource_name(i));
            goto done;
        }
    }
    for (i = 0; i < RASTERFALL_MODULAR_PROFESSION_COUNT; i++)
        if (rasterfall_model_instance_init(&hosts[i], &body) < 0 ||
            snprintf(path, sizeof(path), "%s/rf_profession_%s.rmesh", model_dir,
                     profession_names[i]) >= (int)sizeof(path) ||
            rasterfall_model_load(&carriers[i], path) < 0) goto done;
    if (visual_modular_attachment_regression(&hosts[RASTERFALL_MODULAR_HEAVY],
                                             gear) < 0) goto done;
    memset(&context, 0, sizeof(context));
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    rasterfall_render_set_model_lighting(1);
    gallery_load();
    for (v = 0; v < 3; v++) for (d = 0; d < 3; d++) {
        struct camera camera;
        struct toy_renderer renderer;
        struct toy_surface surface;
        int failed = 0;
        if (v == 2 && d != 1) continue;
        visual_acceptance_camera(&camera, view_ids[v], distances[d]);
        memset(&surface, 0, sizeof(surface));
        surface.width = 2400; surface.height = 900; surface.stride = surface.width * 4;
        surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
        if (!surface.pixels) goto done;
        toy_renderer_init(&renderer);
        toy_renderer_set_frame_budget(&renderer, 0);
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) failed = 1;
        for (i = 0; i < 6 && !failed; i++) {
            int offset = (i * 2 - 5) * (v == 2 ? 330 : 220);
            int x = (int)((long long)camera.view.cy * offset / 1024);
            int z = -(int)((long long)camera.view.sy * offset / 1024);
            if (visual_modular_profession_frame(&renderer, &camera, &hosts[i],
                    rasterfall_character_visual_recipe(i), gear, 1,
                    x, z, distances[d]) < 0) failed = 1;
        }
        if (!failed && (renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0 ||
            snprintf(path, sizeof(path), "%s/%s-%s.bmp", output_dir, views[v], ranges[d]) >= (int)sizeof(path) ||
            rasterfall_hud_dump_bmp(path, &surface) < 0)) failed = 1;
        toy_renderer_destroy(&renderer);
        tlibc_free(surface.pixels);
        if (failed) goto done;
        __printf("rasterfall: modular profession lineup view=%s distance=%d output=%s\n", views[v], distances[d], path);
    }
    __printf("rasterfall: modular profession resources body_loads=1 body_resource=%p host_instances=6 gear_resources=%d\n",
             (void *)&body, RASTERFALL_GEAR_RESOURCE_COUNT);
    for (i = 0; i < RASTERFALL_MODULAR_PROFESSION_COUNT; i++) {
        struct camera camera;
        struct toy_renderer renderer;
        struct toy_surface surface;
        int failed = 0;
        visual_acceptance_camera(&camera, 0, 2200);
        memset(&surface, 0, sizeof(surface));
        surface.width = 1600; surface.height = 800; surface.stride = 6400;
        surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
        if (!surface.pixels) goto done;
        toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer, 0);
        if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
            visual_acceptance_model_frame(&renderer, &camera, &carriers[i], NULL,
                1, -360, 0, 2200, 1) < 0 ||
            visual_modular_profession_frame(&renderer, &camera, &hosts[i],
                rasterfall_character_visual_recipe(i), gear, 1,
                360, 0, 2200) < 0 || renderer.cmd_overflow ||
            toy_renderer_flush(&renderer) < 0 ||
            snprintf(path, sizeof(path), "%s/ab-%s.bmp", output_dir,
                     profession_names[i]) >= (int)sizeof(path) ||
            rasterfall_hud_dump_bmp(path, &surface) < 0) failed = 1;
        toy_renderer_destroy(&renderer); tlibc_free(surface.pixels);
        if (failed) goto done;
        __printf("rasterfall: profession carrier-modular A/B profession=%s output=%s\n",
                 profession_names[i], path);
    }
    result = 0;
done:
    for (i = 0; i < RASTERFALL_MODULAR_PROFESSION_COUNT; i++)
        rasterfall_model_instance_unload(&hosts[i]);
    for (i = 0; i < RASTERFALL_GEAR_RESOURCE_COUNT; i++)
        rasterfall_model_resource_unload(&gear[i]);
    for (i = 0; i < RASTERFALL_MODULAR_PROFESSION_COUNT; i++)
        rasterfall_model_unload(&carriers[i]);
    rasterfall_model_resource_unload(&body);
    return result;
}

static int visual_lighting_ab_dump(struct rasterfall_model_asset *character,
                                   const char *output_dir, int mode, int view)
{
    static const char *modes[] = {"bind", "rifle-idle", "rifle-aim"};
    static const char *views[] = {"front", "side", "back", "three-quarter"};
    struct camera camera;
    struct toy_renderer renderer;
    struct toy_surface surface;
    uint32_t *pixels;
    char dir[512], path[512];
    int offset = 520, left_x, left_z, right_x, right_z;
    visual_acceptance_camera(&camera, view, 1800);
    left_x = -(int)((long long)camera.view.cy * offset / 1024);
    left_z = (int)((long long)camera.view.sy * offset / 1024);
    right_x = -left_x;
    right_z = -left_z;
    memset(&surface, 0, sizeof(surface));
    surface.width = 1600; surface.height = 800; surface.stride = 1600 * 4;
    pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) return 1;
    surface.pixels = pixels;
    toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer, 0);
    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) goto fail;
    rasterfall_render_set_model_lighting(0);
    if (visual_acceptance_model_frame(&renderer, &camera, character, NULL,
            mode, left_x, left_z, 1800, mode != 0) < 0) goto fail;
    rasterfall_render_set_model_lighting(1);
    if (visual_acceptance_model_frame(&renderer, &camera, character, NULL,
            mode, right_x, right_z, 1800, mode != 0) < 0) goto fail;
    if (renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0) goto fail;
    fb_draw_string((unsigned char *)surface.pixels, 24, 24,
                   "LIGHTING OFF", RF_COLOR_UI_TEXT, surface.stride);
    fb_draw_string((unsigned char *)surface.pixels, 824, 24,
                   "LIGHTING V1", RF_COLOR_UI_TEXT, surface.stride);
    if (snprintf(dir, sizeof(dir), "%s/lighting-ab/%s", output_dir,
                 modes[mode]) >= (int)sizeof(dir) ||
        tlibc_recursive_mkdir(dir) < 0 ||
        snprintf(path, sizeof(path), "%s/%s.bmp", dir, views[view]) >=
            (int)sizeof(path) || rasterfall_hud_dump_bmp(path, &surface) < 0)
        goto fail;
    toy_renderer_destroy(&renderer); tlibc_free(pixels); return 0;
fail:
    rasterfall_render_set_model_lighting(1);
    toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
}

/* Character Material Lighting Policy V1 acceptance.  The normal and back
 * cameras sit on opposite sides of the fixed world key light; the dark case
 * keeps the back-light camera and supplies a deterministic 96/256 scene
 * brightness override.  No gameplay session, map entity or local light participates. */
static int visual_material_lighting_dump(
    struct rasterfall_model_asset *character, const char *output_dir)
{
    static const char *names[] = {"normal-light", "back-light", "dark-environment"};
    struct rasterfall_render_context context;
    char dir[512], path[512];
    int condition;
    if (snprintf(dir, sizeof(dir), "%s/lighting-policy", output_dir) >=
            (int)sizeof(dir) || tlibc_recursive_mkdir(dir) < 0) return 1;
    for (condition = 0; condition < 3; condition++) {
        struct camera camera;
        struct toy_renderer renderer;
        struct toy_surface surface;
        uint32_t *pixels;
        memset(&camera, 0, sizeof(camera));
        camera.body.x = condition == 0 ? -1273 : 1273;
        camera.body.z = condition == 0 ? -1273 : 1273;
        camera.view.sy = condition == 0 ? 724 : -724;
        camera.view.cy = condition == 0 ? 724 : -724;
        camera.view.y = -350;
        camera.view.pitch_cy = 1024;
        memset(&context, 0, sizeof(context));
        context.session = active_session;
        context.fixed_floor_lighting = 1;
        rasterfall_render_bind(&context);
        active_model_scene_light_override_q8 = condition == 2 ? 96 : -1;
        memset(&surface, 0, sizeof(surface));
        surface.width = 800; surface.height = 800; surface.stride = 3200;
        pixels = tlibc_malloc((size_t)surface.stride * surface.height);
        if (!pixels) return 1;
        surface.pixels = pixels;
        toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer, 0);
        if (toy_renderer_begin(&renderer, &surface, 0x181B22) < 0 ||
            visual_acceptance_model_frame(&renderer, &camera, character, NULL,
                0, 0, 0, 1800, 0) < 0 || renderer.cmd_overflow ||
            toy_renderer_flush(&renderer) < 0 ||
            snprintf(path, sizeof(path), "%s/%s.bmp", dir, names[condition]) >=
                (int)sizeof(path) || rasterfall_hud_dump_bmp(path, &surface) < 0) {
            active_model_scene_light_override_q8 = -1;
            toy_renderer_destroy(&renderer); tlibc_free(pixels); return 1;
        }
        active_model_scene_light_override_q8 = -1;
        toy_renderer_destroy(&renderer); tlibc_free(pixels);
        __printf("rasterfall: character material lighting condition=%s output=%s\n",
                 names[condition], path);
    }
    return 0;
}

int rasterfall_render_character_acceptance_capture(const char *model_path,
                                                    const char *output_dir)
{
    static const char *modes[] = {"bind", "rifle-idle", "rifle-aim"};
    struct rasterfall_render_context context;
    struct rasterfall_model_resource resource;
    struct rasterfall_model_instance instance;
    struct rasterfall_model_asset *model;
    int mode, view, distance; char path[512];
    if (!model_path || !output_dir || !*model_path || !*output_dir) return 1;
    memset(&resource, 0, sizeof(resource)); memset(&instance, 0, sizeof(instance));
    if (rasterfall_model_resource_load(&resource, model_path) < 0) return 1;
    if (!resource.definition.has_character_contract ||
        rasterfall_model_instance_init(&instance, &resource) < 0) {
        rasterfall_model_resource_unload(&resource);
        return 1;
    }
    model = rasterfall_model_instance_pose(&instance);
    memset(&context, 0, sizeof(context)); context.fixed_floor_lighting = 1; rasterfall_render_bind(&context);
    /* Warm the standard AK cache once; render_skeletal_rifle resolves it by stable asset path. */
    gallery_load();
    for (mode = 0; mode < 3; mode++) for (view = 0; view < 4; view++) {
        struct camera camera; struct toy_renderer renderer; struct toy_surface surface;
        uint32_t *pixels; int width = 800;
        visual_acceptance_camera(&camera, view, 900);
        memset(&surface, 0, sizeof(surface)); surface.width=width; surface.height=800; surface.stride=width*4;
        pixels=tlibc_malloc((size_t)surface.stride*surface.height); surface.pixels=pixels;
        if (!pixels) goto fail;
        toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer,0);
        if (toy_renderer_begin(&renderer,&surface,0x30343B)<0 ||
            visual_acceptance_model_frame(&renderer,&camera,model,NULL,mode,0,0,900,mode!=0)<0 ||
            toy_renderer_flush(&renderer)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); goto fail; }
        snprintf(path,sizeof(path),"%s/%s",output_dir,modes[mode]);
        if (tlibc_recursive_mkdir(path)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); goto fail; }
        /* The current CLI keeps the model capture contract's fixed view names. */
        { static const char *names[] = {"front","side","back","three-quarter"};
          snprintf(path,sizeof(path),"%s/%s/%s.bmp",output_dir,modes[mode],names[view]);
          if (rasterfall_hud_dump_bmp(path,&surface)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); goto fail; } }
        toy_renderer_destroy(&renderer); tlibc_free(pixels);
    }
    for (mode = 0; mode < 3; mode++) for (view = 0; view < 4; view++)
        if (visual_lighting_ab_dump(model, output_dir,
                                    mode, view) != 0) {
            goto fail;
        }
    if (visual_material_lighting_dump(model, output_dir) != 0) goto fail;
    for (distance = 0; distance < 3; distance++) {
        static const char *names[] = {"near","mid","far"};
        snprintf(path,sizeof(path),"%s/ab-%s",output_dir,names[distance]);
        if (visual_acceptance_dump(model,path,"sheet",2, distance==0?2100:distance==1?4200:8400,1)!=0) goto fail;
    }
    if (visual_instance_isolation_dump(&resource, output_dir) != 0) goto fail;
    __printf("rasterfall: character acceptance capture output=%s triangles=%u materials=%u\n",
             output_dir, model->index_count/3, model->material_count);
    rasterfall_model_instance_unload(&instance);
    rasterfall_model_resource_unload(&resource);
    return 0;
fail:
    rasterfall_model_instance_unload(&instance);
    rasterfall_model_resource_unload(&resource);
    return 1;
}

int rasterfall_render_action_preview(const char *model_path,
                                     const char *action_path, int time_ms,
                                     const char *output_path)
{
    struct rasterfall_action_clip clip;
    struct rasterfall_model_resource resource;
    struct rasterfall_model_instance instance;
    struct rasterfall_model_attachment_transform rifle_frame;
    struct rasterfall_render_context context;
    struct rasterfall_rigid_transform actor;
    struct camera camera;
    struct toy_renderer renderer;
    struct toy_surface surface;
    uint32_t *pixels = NULL;
    int result = 1, scale;
    memset(&resource,0,sizeof(resource));memset(&instance,0,sizeof(instance));
    if (!model_path || !action_path || !output_path ||
        rasterfall_action_load(&clip,action_path)<0 ||
        rasterfall_model_resource_load(&resource,model_path)<0 ||
        rasterfall_model_instance_init(&instance,&resource)<0 ||
        rasterfall_action_apply(&instance,&clip,time_ms)<0) goto done;
    memset(&context,0,sizeof(context));context.fixed_floor_lighting=1;
    rasterfall_render_bind(&context);gallery_load();
    visual_acceptance_camera(&camera,3,1500);
    memset(&surface,0,sizeof(surface));surface.width=800;surface.height=800;
    surface.stride=surface.width*4;
    pixels=tlibc_malloc((size_t)surface.stride*surface.height);
    if(!pixels)goto done;
    surface.pixels=pixels;
    toy_renderer_init(&renderer);toy_renderer_set_frame_budget(&renderer,0);
    rigid_identity(&actor);scale=visual_rf_scale(&instance.pose);
    actor.translation[1]=-900-(int)((long long)instance.pose.min_y*scale/1000);
    actor.scale_milli=scale;
    if(toy_renderer_begin(&renderer,&surface,0x30343B)<0 ||
       rasterfall_render_character_instance(&renderer,&camera,&instance,&actor,
           0x40566F,0x2B3440)<0)goto render_done;
    if (instance.pose.has_character_contract)
        render_modular_active_weapon(&renderer, &camera, &instance, &actor,
            TOY_GAME_WEAPON_AK, 0, 0);
    else if (rifle_frame_transform(&instance.pose, &rifle_frame) == 0)
        render_skeletal_rifle(&renderer, &camera, &instance.pose, &rifle_frame,
            &visual_rf_profile, visual_rf_calibration(1), 0,
            (int)actor.translation[1], 0, 0, 1024, TOY_GAME_WEAPON_AK, 0, 0);
    if(renderer.cmd_overflow||toy_renderer_flush(&renderer)<0||
       rasterfall_hud_dump_bmp(output_path,&surface)<0)goto render_done;
    __printf("rasterfall: action preview action=%s time=%d output=%s\n",
             clip.name,time_ms,output_path);
    result=0;
render_done:
    toy_renderer_destroy(&renderer);
done:
    if(pixels)tlibc_free(pixels);
    rasterfall_model_instance_unload(&instance);
    rasterfall_model_resource_unload(&resource);
    return result;
}

int rasterfall_render_action_composition_capture(const char *model_path,
    const char *lower_path, int lower_time_ms, const char *upper_path,
    int upper_time_ms, const char *additive_path, int additive_time_ms,
    const char *output_path)
{
    struct rasterfall_action_clip lower, upper, additive;
    struct rasterfall_action_composition composition;
    struct rasterfall_model_resource resource;
    struct rasterfall_model_instance instance;
    struct rasterfall_model_attachment_transform rifle_frame;
    struct rasterfall_render_context context;
    struct rasterfall_rigid_transform actor;
    struct camera camera;
    struct toy_renderer renderer;
    struct toy_surface surface;
    uint32_t *pixels = NULL;
    int result = 1, scale;
    memset(&resource, 0, sizeof(resource)); memset(&instance, 0, sizeof(instance));
    memset(&composition, 0, sizeof(composition));
    if (!model_path || !lower_path || !upper_path || !additive_path || !output_path ||
        rasterfall_action_load(&lower, lower_path) < 0 ||
        rasterfall_action_load(&upper, upper_path) < 0 ||
        rasterfall_action_load(&additive, additive_path) < 0 ||
        rasterfall_model_resource_load(&resource, model_path) < 0 ||
        rasterfall_model_instance_init(&instance, &resource) < 0) goto done;
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].clip = &lower;
    composition.layers[RASTERFALL_ACTION_LAYER_LOWER_BODY].time_ms = lower_time_ms;
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].clip = &upper;
    composition.layers[RASTERFALL_ACTION_LAYER_UPPER_BODY].time_ms = upper_time_ms;
    composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].clip = &additive;
    composition.layers[RASTERFALL_ACTION_LAYER_ADDITIVE].time_ms = additive_time_ms;
    if (rasterfall_action_compose(&instance, &composition) < 0) goto done;
    memset(&context, 0, sizeof(context)); context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context); gallery_load();
    visual_acceptance_camera(&camera, 3, 1500);
    memset(&surface, 0, sizeof(surface)); surface.width = 800; surface.height = 800;
    surface.stride = surface.width * 4; pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    if (!pixels) goto done;
    surface.pixels = pixels; toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0); rigid_identity(&actor);
    scale = visual_rf_scale(&instance.pose);
    actor.translation[1] = -900 - (int)((long long)instance.pose.min_y * scale / 1000);
    actor.scale_milli = scale;
    if (toy_renderer_begin(&renderer, &surface, 0x30343B) < 0 ||
        rasterfall_render_character_instance(&renderer, &camera, &instance, &actor,
            0x40566F, 0x2B3440) < 0) goto render_done;
    if (instance.pose.has_character_contract)
        render_modular_active_weapon(&renderer, &camera, &instance, &actor,
            TOY_GAME_WEAPON_AK, 0, 0);
    else if (rifle_frame_transform(&instance.pose, &rifle_frame) == 0)
        render_skeletal_rifle(&renderer, &camera, &instance.pose, &rifle_frame,
            &visual_rf_profile, visual_rf_calibration(1), 0,
            (int)actor.translation[1], 0, 0, 1024, TOY_GAME_WEAPON_AK, 0, 0);
    if (renderer.cmd_overflow || toy_renderer_flush(&renderer) < 0 ||
        rasterfall_hud_dump_bmp(output_path, &surface) < 0) goto render_done;
    __printf("rasterfall: action composition capture lower=%s upper=%s additive=%s output=%s\n",
        lower.name, upper.name, additive.name, output_path);
    result = 0;
render_done:
    toy_renderer_destroy(&renderer);
done:
    if (pixels) tlibc_free(pixels);
    rasterfall_model_instance_unload(&instance);
    rasterfall_model_resource_unload(&resource);
    return result;
}
