/* Renderer translation-unit regression: independent enemy source is read-only,
 * samples once per frame, and resets presentation history with the world. */
static int scene_independent_enemy_source_test(void)
{
    static struct rasterfall_session owner;
    static struct rasterfall_effects test_effects;
    static struct rasterfall_world_lighting test_lighting;
    static struct toy_game untouched;
    static struct rf_gpu_scene_enemy_frame_v1 a,b;
    struct rasterfall_session *saved_session=active_session;
    struct rasterfall_effects *saved_effects=active_effects;
    struct rasterfall_world_lighting *saved_lighting=active_world_lighting;
    const struct rasterfall_net *saved_net=active_net;
    int saved_family=enemy_visual_family;
    struct camera camera={0};
#define ENEMY_SOURCE_CHECK(x) do { if (!(x)) { \
    __printf("scene independent enemy source failed line %d\n",__LINE__); \
    return 1; } } while (0)
    camera.cy=camera.pitch_cy=1024;
    toy_game_init(&owner.game_state,1);
    memset(&test_effects,0,sizeof(test_effects));
    memset(&test_lighting,0,sizeof(test_lighting));
    test_lighting.field[0]=(struct rasterfall_world_light){256,0,256};
    active_session=&owner;active_effects=&test_effects;
    active_world_lighting=&test_lighting;active_net=NULL;
    enemy_visual_family=RASTERFALL_ENEMY_VISUAL_LEGACY;
    owner.game_state.enemies[0].active=1;
    owner.game_state.enemies[0].type=TOY_GAME_ENEMY_PURSUIT_COMMON;
    owner.game_state.enemies[0].z=1200;
    owner.game_state.enemies[0].dir_z=1024;
    owner.game_state.enemies[1].active=1;
    owner.game_state.enemies[1].type=TOY_GAME_ENEMY_SMOKER;
    owner.game_state.enemies[1].z=2000;
    owner.game_state.enemies[1].dir_z=1024;
    owner.game_state.enemies[1].special_target_active=1;
    untouched=owner.game_state;
    rf_gpu_scene_enemy_begin(810,73);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1000000,73));
    ENEMY_SOURCE_CHECK(rf_gpu_scene_enemy_collect_independent(&camera,1000000,73)<0);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&a) && a.count==2 &&
        a.items[0].legacy_kind==1 && a.items[0].shadow &&
        a.items[1].tongue && a.items[1].pose.channel[ER_TORSO].pitch==12 &&
        !memcmp(&untouched,&owner.game_state,sizeof(untouched)));
    owner.game_state.enemies[0].x=100;
    rf_gpu_scene_enemy_begin(811,73);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1010000,73));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) && b.count==2 &&
        b.items[0].x==100 && b.items[1].tongue);
    owner.game_state.enemies[0].active=2;
    owner.game_state.enemies[0].dying_ms=TOY_GAME_DYING_MS/2;
    test_effects.enemy_death_style[0]=RASTERFALL_ENEMY_DEATH_STYLE_LEGACY+1;
    rf_gpu_scene_enemy_begin(812,73);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1020000,73));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) && b.items[0].transformed &&
        b.items[0].roll_cos!=1024 && !b.items[0].shadow);
    /* The imported body gets independent stride history and freezes swing. */
    enemy_visual_family=RASTERFALL_ENEMY_VISUAL_BLOCK_INFECTED;
    owner.game_state.enemies[0].active=1;
    owner.game_state.enemies[0].x=0;
    rf_gpu_scene_enemy_begin(813,74);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1030000,74));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&a) &&
        a.items[0].infected_recipe>0 && a.items[0].infected_swing==0);
    owner.game_state.enemies[0].x=100;
    rf_gpu_scene_enemy_begin(814,74);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1040000,74));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) &&
        b.items[0].infected_swing==-9);
    rf_gpu_scene_enemy_begin(815,75);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1050000,75));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) &&
        b.items[0].infected_swing==0 && b.world_generation==75);
    active_session=saved_session;active_effects=saved_effects;
    active_world_lighting=saved_lighting;active_net=saved_net;
    enemy_visual_family=saved_family;
#undef ENEMY_SOURCE_CHECK
    return 0;
}
