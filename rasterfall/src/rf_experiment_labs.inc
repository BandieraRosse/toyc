/* Runtime-owned experiment registry. Spatial facts stay in Runtime Map. */
enum rf_experiment_lab {
    RF_LAB_INFECTED_POSES, RF_LAB_INFECTED_WALK, RF_LAB_ACTOR_ACTIONS,
    RF_LAB_ACTOR_WALK, RF_LAB_MODEL, RF_LAB_LIGHTING, RF_LAB_ELECTRONICS, RF_LAB_COUNT
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
    {"GPU LIGHTING", "rf_light_lab", "rf_light_lab_button", "lighting-lab", 1},
    {"RF ELECTRONICS", "rf_electronics_lab", "rf_electronics_lab_button", "electronics-lab", 0}
};
static struct {
    int requested[RF_LAB_COUNT],fan_rpm;
    unsigned fan_phase[RF_LAB_COUNT];
    uint64_t time_us[RF_LAB_COUNT], last_us;
} rf_labs;
_Static_assert(RF_LAB_COUNT<RASTERFALL_ELECTRONICS_GROUPS,"RF lab presentation groups exhausted");

static void rf_lab_control(int lab)
{
    if (lab<0 || lab>=RF_LAB_COUNT) return;
    if (lab!=RF_LAB_ELECTRONICS) rf_labs.requested[lab]=!rf_labs.requested[lab];
    else if (!rf_labs.requested[lab]) { rf_labs.requested[lab]=1;rf_labs.fan_rpm=600; }
    else if (rf_labs.fan_rpm<1800) rf_labs.fan_rpm+=600;
    else { rf_labs.requested[lab]=0;rf_labs.fan_rpm=600; }
}

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
    struct rasterfall_electronics_frame frame={0};frame.rpm=rf_labs.fan_rpm;
    for (int i=0;i<RF_LAB_COUNT;++i) {
        if (!rf_labs.requested[i]) rf_labs.time_us[i]=0;
        else if (rf_lab_effective(i,world,suspended,gpu)) {
            rf_labs.time_us[i]+=dt;
            rf_labs.fan_phase[i]=(unsigned)((rf_labs.fan_phase[i]+
                (dt%60000000u)*(unsigned)rf_labs.fan_rpm)%60000000u);
            frame.powered_mask|=1u<<(i+1);
        }
        frame.phase[i+1]=rf_labs.fan_phase[i];
        frame.time_ms[i+1]=(unsigned)(rf_labs.time_us[i]/1000);
    }
    rasterfall_electronics_set_frame(&frame);
}

static void rf_labs_reset(void)
{
    memset(&rf_labs,0,sizeof(rf_labs));rf_labs.fan_rpm=600;
    rasterfall_electronics_set_frame(NULL);
}

static void rf_labs_display(enum rasterfall_world_id world,int suspended,int gpu)
{
    for (int i=0;i<RF_LAB_COUNT;++i) {
        char text[192];
        const char *status=rf_lab_definitions[i].gpu_only && !gpu ? "GPU REQUIRED" :
            rf_lab_effective(i,world,suspended,gpu) ? "ON" :
            rf_labs.requested[i] && suspended ? "SUSPENDED" : "OFF";
        snprintf(text,sizeof(text),"%s / %s",rf_lab_definitions[i].id,status);
        if (i==RF_LAB_ELECTRONICS && rf_labs.requested[i])
            snprintf(text,sizeof(text),"RF GEN 1 / %s / %d RPM",status,rf_labs.fan_rpm);
        rasterfall_render_terminal_set(rf_lab_definitions[i].surface,text);
        char channel[64];
        snprintf(channel,sizeof(channel),"%s_computer",rf_lab_definitions[i].surface);
        snprintf(text,sizeof(text),"\033MRF / LAB\n\033F%s\n\0332\033B\033A%s\n\0331\033N\033M[E] TOGGLE\nLOCAL CONTROL",
            rf_lab_definitions[i].id,status);
        if (i==RF_LAB_ELECTRONICS)
            snprintf(text,sizeof(text),"\033MRF / GENERATION 1\n\033FELECTRONICS LAB\n\0332\033A%s\n\0331\033F%d RPM\n\033M[E] NEXT MODE\nOFF / 600 / 1200 / 1800",
                status,rf_labs.requested[i] ? rf_labs.fan_rpm : 0);
        rasterfall_render_terminal_set(channel,text);
    }
}

static void rf_lab_camera_position(const struct rasterfall_session *session,
    struct camera *camera, const char *id, int x, int z)
{
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
        &session->map_ops.runtime,id);
    if (!area) return;
    camera->x=area->origin_x+x;camera->z=area->origin_z+z;
}

static void rf_equipment_camera(const struct rasterfall_session *session,struct camera *camera)
{
    const char *station_text=getenv("RF_GPU_EQUIPMENT_STATION");
    const char *view=getenv("RF_GPU_EQUIPMENT_VIEW");
    int station=station_text ? atoi(station_text) : 3;
    if (station<0 || station>9) station=3;
    double angle=view && !strcmp(view,"front") ? 0.0 :
        view && !strcmp(view,"side") ? 1.5707963267948966 :
        view && !strcmp(view,"back") ? 3.1415926535897932 : 0.5235987755982988;
    rf_lab_camera_position(session,camera,"actor_actions_lab_area",
        -4284+(station%6)*1650+(int)(sin(angle)*1400),
        -1044-(station/6)*2100+(int)(cos(angle)*1400));
    camera->y=-430;camera->sy=(int)(-sin(angle)*1024);camera->cy=(int)(-cos(angle)*1024);
    camera->pitch_sy=0;camera->pitch_cy=1024;
}

static void rf_electronics_camera(const struct rasterfall_session *session,struct camera *camera,const char *view)
{
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(
        &session->map_ops.runtime,"rf_electronics_lab_area");
    if (!area) return;
    camera->x=area->origin_x;camera->z=area->origin_z+5400;camera->y=2400;
    camera->sy=0;camera->cy=-1024;camera->pitch_sy=-320;camera->pitch_cy=973;
    if (!strcmp(view,"electronics-case")) {
        const struct rf_map_runtime_object *o=rf_map_runtime_find_object(
            &session->map_ops.runtime,"rf_electronics_demo");
        if (!o) return;
        camera->x=o->x-1200;camera->z=o->z+330;camera->y=110;
        camera->sy=947;camera->cy=-389;camera->pitch_sy=-205;camera->pitch_cy=1003;
    }
}

static void rf_labs_diagnostic_view(const char *view)
{
    if (!view) return;
    const char *all=getenv("RF_LABS_ALL");
    if (all && all[0]=='1')
        for (int i=0;i<RF_LAB_COUNT;++i) rf_labs.requested[i]=1;
    if (!strcmp(view,"electronics-case")) rf_labs.requested[RF_LAB_ELECTRONICS]=1;
    if (!strcmp(view,"equipment-lab")) rf_labs.requested[RF_LAB_ACTOR_ACTIONS]=1;
    for (int i=0; i<RF_LAB_COUNT; ++i)
        if (!strcmp(view,rf_lab_definitions[i].view)) rf_labs.requested[i]=1;
}

static void rf_lab_computer_camera(const struct rasterfall_session *session,
    struct camera *camera,const char *view)
{
    const struct rf_map_runtime_object *o=rf_map_runtime_find_object(
        &session->map_ops.runtime,"lab_showcase_button");
    if (!o) return;
    int close=!strcmp(view,"lab-computer-close");
    camera->x=o->x+(close?130:260);camera->z=o->z+(close?720:1500);
    camera->y=close?20:240;camera->sy=close?0:-174;camera->cy=close?-1024:-1009;
    camera->pitch_sy=close?-110:-300;camera->pitch_cy=close?1018:979;
    if (!strcmp(view,"lab-computer-side")) {
        camera->x=o->x-1050;camera->z=o->z+270;camera->y=110;
        camera->sy=947;camera->cy=-389;camera->pitch_sy=-155;camera->pitch_cy=1012;
    } else if (!strcmp(view,"lab-computer-rear")) {
        camera->x=o->x-1050;camera->z=o->z-750;camera->y=160;
        camera->sy=729;camera->cy=719;camera->pitch_sy=-190;camera->pitch_cy=1006;
    }
}

static void rf_performance_terminal_camera(const struct rasterfall_session *session,struct camera *camera)
{
    const struct rf_map_runtime_object *a=rf_map_runtime_find_object(&session->map_ops.runtime,"perf_control_terminal");
    const struct rf_map_runtime_object *b=rf_map_runtime_find_object(&session->map_ops.runtime,"perf_result_terminal");
    if(!a || !b) return;
    camera->x=(a->x+b->x)/2;camera->z=a->z-2300;camera->y=150;
    camera->sy=0;camera->cy=1024;camera->pitch_sy=80;camera->pitch_cy=1021;
}
