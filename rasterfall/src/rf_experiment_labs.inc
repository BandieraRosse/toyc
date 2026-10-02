/* Runtime-owned experiment registry. Spatial facts stay in Runtime Map. */
enum rf_experiment_lab {
    RF_LAB_INFECTED_POSES, RF_LAB_INFECTED_WALK, RF_LAB_ACTOR_ACTIONS,
    RF_LAB_ACTOR_WALK, RF_LAB_MODEL, RF_LAB_LIGHTING, RF_LAB_COUNT
};
struct rf_experiment_definition {
    const char *id, *surface, *terminal, *view;
    int gpu_only;
};
static const struct rf_experiment_definition rf_lab_definitions[RF_LAB_COUNT] = {
    {"INFECTED POSES", "character_lab", "lab_showcase_button", "character-lab", 0},
    {"INFECTED WALK", "walk_lab", "walk_lab_showcase_button", "walk-lab", 0},
    {"AI ACTIONS", "actor_actions_lab", "actor_actions_button", "actor-actions-lab", 0},
    {"AI WALK", "actor_walk_lab", "actor_walk_button", "actor-walk-lab", 0},
    {"RF MODEL", "rf_model_lab", "rf_model_lab_button", "model-lab", 1},
    {"GPU LIGHTING", "rf_light_lab", "rf_light_lab_button", "lighting-lab", 1}
};
static struct { int requested[RF_LAB_COUNT]; uint64_t time_us[RF_LAB_COUNT], last_us; } rf_labs;

static int rf_lab_effective(int lab, enum rasterfall_world_id world,
                            int suspended, int gpu)
{
    return lab >= 0 && lab < RF_LAB_COUNT && rf_labs.requested[lab] &&
        world == RASTERFALL_WORLD_OUTPOST && !suspended &&
        (!rf_lab_definitions[lab].gpu_only || gpu);
}

static int rf_lab_near(const struct rasterfall_session *session, int x, int z)
{
    for (int i=0; i<RF_LAB_COUNT; ++i) {
        const struct rf_map_runtime_object *button=rf_map_runtime_find_object(
            &session->map_ops.runtime,rf_lab_definitions[i].terminal);
        if (button && x>=button->x-750 && x<=button->x+750 &&
            z>=button->z-800 && z<=button->z+800) return i+1;
    }
    return 0;
}

static void rf_labs_tick(enum rasterfall_world_id world,int suspended,int gpu,uint64_t now)
{
    uint64_t dt=rf_labs.last_us && now>=rf_labs.last_us ? now-rf_labs.last_us : 0;
    rf_labs.last_us=now;
    for (int i=0;i<RF_LAB_COUNT;++i) {
        if (!rf_labs.requested[i]) rf_labs.time_us[i]=0;
        else if (rf_lab_effective(i,world,suspended,gpu)) rf_labs.time_us[i]+=dt;
    }
}

static void rf_labs_reset(void) { memset(&rf_labs,0,sizeof(rf_labs)); }

static void rf_labs_diagnostic_view(const char *view)
{
    if (!view) return;
    for (int i=0; i<RF_LAB_COUNT; ++i)
        if (!strcmp(view,rf_lab_definitions[i].view)) rf_labs.requested[i]=1;
}
