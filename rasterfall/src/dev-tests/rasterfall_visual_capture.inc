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
    if (!scenario || (!squad && strcmp(scenario, "procedural-humanoid"))) {
        __fprintf(2, "rasterfall: unknown visual scenario '%s'; available: procedural-humanoid, hurd-squad\n",
                  scenario ? scenario : "");
        return 1;
    }
    if (!output || !*output) {
        __fprintf(2, "rasterfall: visual output path must not be empty\n");
        return 1;
    }
    visual_procedural_humanoid_setup(&camera, &state);
    count = squad ? 4 : 1;
    if (squad) camera.body.z = -2600;
    /* Preload just the fixture weapons, never discover gameplay actors. */
    for (i = 0; i < (squad ? 2 : 1); i++) {
        weapon_path = rasterfall_weapon_model_path(
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
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    surface.width = squad ? 1600 : 800;
    surface.height = 800;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
    if (!surface.pixels || toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) {
        __fprintf(2, "rasterfall: cannot initialize visual framebuffer\n");
        goto done;
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
    for (i = 0; i < (squad ? 2 : 1); i++)
        rasterfall_model_unload(&gallery_models[i]);
    return result;
}

/* RF Humanoid V1.1 acceptance sheet.  This is intentionally a capture-only
 * harness: it exercises the same skeletal rifle path used by AI actors while
 * keeping the asset out of gameplay character selection until art sign-off. */
static const struct rasterfall_skeletal_actor_profile visual_rf_profile = {
    "rf_humanoid_acceptance", NULL, NULL, 1736, 0, 1024
};

static void visual_acceptance_camera(struct camera *camera, int view, int distance)
{
    memset(camera, 0, sizeof(*camera));
    if (view == 1) camera->body.x = -distance, camera->view.sy = 1024;
    else if (view == 2) camera->body.z = distance, camera->view.cy = -1024;
    else if (view == 3) camera->body.x = -distance * 724 / 1024,
                         camera->body.z = -distance * 724 / 1024,
                         camera->view.sy = 724, camera->view.cy = 724;
    else camera->body.z = -distance, camera->view.cy = 1024;
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
    scale = character_model_scale(character, visual_rf_profile.target_height_mm);
    center_x = actor_x - (int)(((long long)character->min_x + character->max_x) * scale / 2000);
    center_z = actor_z - (int)(((long long)character->min_z + character->max_z) * scale / 2000);
    base_y = -900 - (int)((long long)character->min_y * scale / 1000);
    if (draw_rifle && rifle_frame_transform(character, &frame) == 0) {
        calibration = rasterfall_pose_calibration_resolve(NULL, 1, TOY_GAME_WEAPON_AK);
        if (calibration->left_ik)
            rifle_solve_hands(character, &frame, calibration,
                              rasterfall_weapon_asset_profile(TOY_GAME_WEAPON_AK), scale);
    }
    active_gallery_facing = 1; active_gallery_sy = 0; active_gallery_cy = 1024;
    pixels += render_gallery_model(renderer, camera, character,
                                   center_x, base_y, center_z, scale);
    if (draw_rifle && rifle_frame_transform(character, &frame) == 0) {
        calibration = rasterfall_pose_calibration_resolve(NULL, 1, TOY_GAME_WEAPON_AK);
        pixels += render_skeletal_rifle(renderer, camera, character, &frame,
                                         &visual_rf_profile, calibration,
                                         actor_x, base_y, actor_z, 0, 1024,
                                         TOY_GAME_WEAPON_AK, 0);
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
        visual_acceptance_model_frame(&renderer, &camera, &acceptance_capture_model,
                                      NULL, mode, 500, 0, distance, mode != 0);
    } else visual_acceptance_model_frame(&renderer, &camera, &acceptance_capture_model,
                                         NULL, mode, 0, 0, distance, mode != 0);
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

int rasterfall_render_character_acceptance_capture(const char *model_path,
                                                    const char *output_dir)
{
    static const char *modes[] = {"bind", "rifle-idle", "rifle-aim"};
    struct rasterfall_render_context context; struct rasterfall_model_asset model;
    int mode, view, distance; char path[512];
    if (!model_path || !output_dir || !*model_path || !*output_dir) return 1;
    memset(&model, 0, sizeof(model));
    if (rasterfall_model_load(&model, model_path) < 0 || !model.has_character_contract) return 1;
    memset(&context, 0, sizeof(context)); context.fixed_floor_lighting = 1; rasterfall_render_bind(&context);
    /* Warm the standard AK cache once; render_skeletal_rifle resolves it by stable asset path. */
    gallery_load();
    acceptance_capture_model = model;
    for (mode = 0; mode < 3; mode++) for (view = 0; view < 4; view++) {
        struct camera camera; struct toy_renderer renderer; struct toy_surface surface;
        uint32_t *pixels; int width = 800;
        visual_acceptance_camera(&camera, view, 900);
        memset(&surface, 0, sizeof(surface)); surface.width=width; surface.height=800; surface.stride=width*4;
        pixels=tlibc_malloc((size_t)surface.stride*surface.height); surface.pixels=pixels;
        if (!pixels) { rasterfall_model_unload(&acceptance_capture_model); return 1; }
        toy_renderer_init(&renderer); toy_renderer_set_frame_budget(&renderer,0);
        if (toy_renderer_begin(&renderer,&surface,0x30343B)<0 ||
            visual_acceptance_model_frame(&renderer,&camera,&acceptance_capture_model,NULL,mode,0,0,900,mode!=0)<0 ||
            toy_renderer_flush(&renderer)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); rasterfall_model_unload(&acceptance_capture_model); return 1; }
        snprintf(path,sizeof(path),"%s/%s",output_dir,modes[mode]);
        if (tlibc_recursive_mkdir(path)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); rasterfall_model_unload(&acceptance_capture_model); return 1; }
        /* The current CLI keeps the model capture contract's fixed view names. */
        { static const char *names[] = {"front","side","back","three-quarter"};
          snprintf(path,sizeof(path),"%s/%s/%s.bmp",output_dir,modes[mode],names[view]);
          if (rasterfall_hud_dump_bmp(path,&surface)<0) { toy_renderer_destroy(&renderer); tlibc_free(pixels); rasterfall_model_unload(&acceptance_capture_model); return 1; } }
        toy_renderer_destroy(&renderer); tlibc_free(pixels);
    }
    for (distance = 0; distance < 3; distance++) {
        static const char *names[] = {"near","mid","far"};
        snprintf(path,sizeof(path),"%s/ab-%s",output_dir,names[distance]);
        if (visual_acceptance_dump(&acceptance_capture_model,path,"sheet",2, distance==0?2100:distance==1?4200:8400,1)<0) { rasterfall_model_unload(&acceptance_capture_model); return 1; }
    }
    rasterfall_model_unload(&acceptance_capture_model);
    __printf("rasterfall: character acceptance capture output=%s triangles=%u materials=%u\n",
             output_dir, model.index_count/3, model.material_count);
    return 0;
}
