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
    int result = 1;
    if (!scenario || strcmp(scenario, "procedural-humanoid")) {
        __fprintf(2, "rasterfall: unknown visual scenario '%s'; available: procedural-humanoid\n",
                  scenario ? scenario : "");
        return 1;
    }
    if (!output || !*output) {
        __fprintf(2, "rasterfall: visual output path must not be empty\n");
        return 1;
    }
    visual_procedural_humanoid_setup(&camera, &state);
    weapon_path = rasterfall_weapon_model_path(state.weapon);
    if (!weapon_path || rasterfall_model_load(&gallery_models[0], weapon_path) < 0) {
        __fprintf(2, "rasterfall: cannot load visual scenario weapon: %s\n",
                  weapon_path ? weapon_path : "(no model)");
        return 1;
    }
    strcpy(gallery_paths[0], weapon_path);
    gallery_loaded = 1;
    memset(&context, 0, sizeof(context));
    context.fixed_floor_lighting = 1;
    rasterfall_render_bind(&context);
    surface.width = 800;
    surface.height = 800;
    surface.stride = surface.width * (int)sizeof(uint32_t);
    surface.pixels = tlibc_malloc((size_t)surface.stride * surface.height);
    toy_renderer_init(&renderer);
    toy_renderer_set_frame_budget(&renderer, 0);
    if (!surface.pixels || toy_renderer_begin(&renderer, &surface, 0x30343B) < 0) {
        __fprintf(2, "rasterfall: cannot initialize visual framebuffer\n");
        goto done;
    }
    if (rasterfall_render_procedural_humanoid(&renderer, &camera, &state,
            rasterfall_character_profile(RASTERFALL_CHARACTER_AKARI)) < 0 ||
        renderer.cmd_overflow || renderer.cmd_count == 0 ||
        toy_renderer_flush(&renderer) < 0) {
        __fprintf(2, "rasterfall: visual scenario rendering failed\n");
        goto done;
    }
    if (rasterfall_hud_dump_bmp(output, &surface) < 0) {
        __fprintf(2, "rasterfall: cannot write visual capture: %s\n", output);
        goto done;
    }
    __printf("rasterfall: visual capture scenario=%s size=800x800 format=BMP output=%s\n",
             scenario, output);
    result = 0;
done:
    toy_renderer_destroy(&renderer);
    tlibc_free(surface.pixels);
    rasterfall_model_unload(&gallery_models[0]);
    return result;
}
