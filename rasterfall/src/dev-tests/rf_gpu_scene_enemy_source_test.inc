/* Renderer translation-unit regression: independent enemy source is read-only,
 * samples once per frame, and resets presentation history with the world. */
static int scene_showcase_test_triangle(void *context,
    const struct rf_gpu_scene_enemy_point *a,
    const struct rf_gpu_scene_enemy_point *b,
    const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    (void)a;(void)b;(void)c;(void)color;
    ++*(int *)context;
    return 0;
}
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
    int saved_showcase=outpost_showcase_visible;
    int showcase_triangles=0;
    uint64_t saved_showcase_time=outpost_showcase_time_us;
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
    owner.game_state.enemies[0].ground_y=1800;
    owner.game_state.enemies[0].airborne_y=250;
    test_effects.enemy_death_style[0]=RASTERFALL_ENEMY_DEATH_STYLE_LEGACY+1;
    rf_gpu_scene_enemy_begin(812,73);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1020000,73));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) && b.items[0].transformed &&
        b.items[0].roll_cos!=1024 && !b.items[0].shadow &&
        b.items[0].pivot_y==b.items[0].lift-300 && b.items[0].lift==2480);
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
    owner.world_id=RASTERFALL_WORLD_OUTPOST;
    rasterfall_render_set_outpost_showcase(0,1000000);
    rasterfall_render_set_outpost_showcase(1,1000000);
    rasterfall_render_set_outpost_showcase(1,1016000);
    untouched=owner.game_state;
    rf_gpu_scene_enemy_begin(816,75);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1016000,75));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&a) &&
        a.count==2+RASTERFALL_OUTPOST_SHOWCASE_COUNT &&
        !memcmp(&untouched,&owner.game_state,sizeof(untouched)));
    for (int i=0;i<RASTERFALL_OUTPOST_SHOWCASE_COUNT;++i) {
        const struct rf_gpu_scene_enemy_item_v1 *item=&a.items[2+i];
        const struct toy_game_enemy_info *info=toy_game_enemy_info(item->type);
        struct rasterfall_infected_motion previous={0};
        struct rasterfall_infected_sample first,expected;
        rasterfall_infected_sample_motion(&previous,1,0,0,1000000,&first);
        rasterfall_infected_sample_motion(&first.next,1,
            i%2 ? (info->speed_min+info->speed_max)/2 : 0,0,1016000,&expected);
        ENEMY_SOURCE_CHECK(item->source_slot==TOY_GAME_MAX_ENEMIES+(unsigned)i &&
            item->infected_recipe==i/2+1 &&
            item->type==enemy_visual_recipes[i/2].type &&
            item->infected_swing==expected.swing &&
            item->dir_z==1024 && item->x>=-5120 && item->x<=5120 &&
            item->z>=-24064 && item->z<=-14848);
        showcase_triangles=0;
        ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_triangles(item,
            scene_showcase_test_triangle,&showcase_triangles) && showcase_triangles>0);
    }
    /* Neither repeat submission nor extraction advances the display motion. */
    rasterfall_render_set_outpost_showcase(1,1016000);
    rf_gpu_scene_enemy_begin(817,75);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1020000,75));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) && b.count==a.count &&
        !memcmp(&a.items[2],&b.items[2],
            RASTERFALL_OUTPOST_SHOWCASE_COUNT*sizeof(a.items[0])));
    rasterfall_render_set_outpost_showcase(0,1020000);
    rf_gpu_scene_enemy_begin(818,75);
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_collect_independent(&camera,1020000,75));
    ENEMY_SOURCE_CHECK(!rf_gpu_scene_enemy_freeze(&b) && b.count==2);
    rasterfall_render_set_outpost_showcase(1,1030000);
    ENEMY_SOURCE_CHECK(outpost_showcase_motion[1].swing==0);
    rasterfall_render_set_outpost_showcase(1,1046000);
    ENEMY_SOURCE_CHECK(outpost_showcase_motion[1].swing!=0);
    rasterfall_render_set_outpost_showcase(1,1000000);
    ENEMY_SOURCE_CHECK(outpost_showcase_motion[1].swing==0);
    active_session=saved_session;active_effects=saved_effects;
    active_world_lighting=saved_lighting;active_net=saved_net;
    enemy_visual_family=saved_family;
    rasterfall_render_set_outpost_showcase(saved_showcase,saved_showcase_time);
#undef ENEMY_SOURCE_CHECK
    return 0;
}
