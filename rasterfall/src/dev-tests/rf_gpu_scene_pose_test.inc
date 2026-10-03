#include "rf_gpu_scene_clothing_test.inc"
#include "rf_gpu_scene_grip_test.inc"
struct scene_infected_test_geometry { uint64_t hash; unsigned count; int fail; };
static int scene_infected_test_triangle(void *context,
    const struct rf_gpu_scene_enemy_point *a,const struct rf_gpu_scene_enemy_point *b,
    const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    struct scene_infected_test_geometry *g=context;
    const struct rf_gpu_scene_enemy_point *p[3]={a,b,c};
    if (g->fail || color>0xffffff) return -1;
    for (int k=0;k<3;++k) {
        g->hash=(g->hash^(unsigned)p[k]->x)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->y)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->z)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->form_light_q8)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->light_min_q8)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->light_max_q8)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[k]->double_sided)*1099511628211ULL;
    }
    g->hash=(g->hash^color)*1099511628211ULL;g->count++;return 0;
}

static int scene_evasion_pose_test(void)
{
    static struct rasterfall_model_instance instances[3];
    static struct rf_gpu_scene_local_frame frame, saved_frame;
    static struct rf_gpu_scene_pose_v1 baseline, evaded, replay;
    static struct toy_game sample_game, saved_game;
    struct rasterfall_action_composition composition = {0};
    struct rasterfall_action_clip invalid;
    struct rf_gpu_scene_local_source source = {0};
    struct camera camera = {0};
    struct toy_game_actor actor = {0}, *sample;
    struct rasterfall_actor_evasion_history history = {0}, peer_history = {0};
    struct rasterfall_actor_evasion_pose pose, peer_pose;
    int failure = 0, bone, result;
#define EVADE_CHECK(x) do { if (!(x)) { failure = __LINE__; goto done; } } while (0)
    actor.active = 1; actor.state = TOY_GAME_ACTOR_ALIVE;
    actor.actor_id = 4; actor.combat_generation = 1; actor.cy = 1024;
    actor.evasion.sequence = 1; actor.evasion.animation_ms = 400;
    actor.evasion.source_x = 1000; actor.evasion.strength_milli = 30000;
    rasterfall_actor_evasion_sample(&actor, &history, &pose);
    EVADE_CHECK(pose.side == -1 && pose.time_ms == 100 && pose.weight_milli == 850);
    actor.evasion.animation_ms = 350; actor.evasion.source_x = -1000;
    actor.evasion.strength_milli = 60000;
    rasterfall_actor_evasion_sample(&actor, &history, &pose);
    EVADE_CHECK(pose.side == -1 && pose.time_ms == 150 && pose.weight_milli == 850);
    rasterfall_actor_evasion_sample(&actor, &peer_history, &peer_pose);
    EVADE_CHECK(peer_pose.side == 1 && peer_pose.weight_milli == 1000);
    for (int state = 0; state < 6; state++) {
        actor.evasion.sequence++;
        actor.control_disabled = state == 0;
        actor.special_control = state == 1 ? TOY_GAME_SPECIAL_CONTROL_SMOKER : 0;
        actor.airborne_ms = state == 2 ? 160 : 0;
        actor.state = state == 3 ? TOY_GAME_ACTOR_DEAD :
            state == 5 ? TOY_GAME_ACTOR_DOWNED : TOY_GAME_ACTOR_ALIVE;
        actor.animation.id = state == 4 ? TOY_GAME_ANIM_REVIVE : TOY_GAME_ANIM_IDLE;
        rasterfall_actor_evasion_sample(&actor, &history, &pose);
        EVADE_CHECK(!pose.weight_milli);
        actor.control_disabled = actor.special_control = actor.airborne_ms = 0;
        actor.state = TOY_GAME_ACTOR_ALIVE; actor.animation.id = TOY_GAME_ANIM_IDLE;
        rasterfall_actor_evasion_sample(&actor, &history, &pose);
        EVADE_CHECK(!pose.weight_milli); /* An interrupted event never reappears. */
    }
    actor.combat_generation++;
    rasterfall_actor_evasion_sample(&actor, &history, &pose);
    EVADE_CHECK(pose.weight_milli && pose.side == 1);
    actor.evasion.animation_ms = 0;
    rasterfall_actor_evasion_sample(&actor, &history, &pose);
    EVADE_CHECK(!pose.weight_milli);
    EVADE_CHECK(!modular_actor_resources_init());
    memset(instances, 0, sizeof(instances));
    for (int i = 0; i < 3; i++)
        EVADE_CHECK(!rasterfall_model_instance_init(&instances[i], &modular_actor_runtime.body));
    composition.layers[0].clip = humanoid_action(RASTERFALL_ACTION_LOCOMOTION_WALK);
    composition.layers[0].time_ms = 180;
    composition.layers[1].clip = humanoid_action(RASTERFALL_ACTION_RIFLE_AIM);
    composition.layers[1].time_ms = 35;
    composition.layers[2].clip = humanoid_action(RASTERFALL_ACTION_RIFLE_RECOIL);
    composition.layers[2].time_ms = 35;
    EVADE_CHECK(composition.layers[0].clip && composition.layers[1].clip && composition.layers[2].clip);
    EVADE_CHECK(!rasterfall_action_compose(&instances[0], &composition));
    composition.secondary_additive.clip = humanoid_action(RASTERFALL_ACTION_EVADE_LEFT);
    composition.secondary_additive.time_ms = 140;
    composition.secondary_additive_weight_milli = 1000;
    EVADE_CHECK(composition.secondary_additive.clip && !rasterfall_action_compose(&instances[1], &composition));
    composition.secondary_additive.clip = humanoid_action(RASTERFALL_ACTION_EVADE_RIGHT);
    EVADE_CHECK(composition.secondary_additive.clip && !rasterfall_action_compose(&instances[2], &composition));
    for (int role = 0; role < RASTERFALL_HUMANOID_BONE_COUNT; role++) {
        bone = rasterfall_model_humanoid_bone(&instances[0].pose, role);
        EVADE_CHECK(bone >= 0);
        if (role == RASTERFALL_HUMANOID_ROOT || role == RASTERFALL_HUMANOID_HIPS ||
            role >= RASTERFALL_HUMANOID_LEFT_UPPER_LEG)
            for (int i = 1; i < 3; i++)
                EVADE_CHECK(!memcmp(&instances[0].pose.bone_transforms[bone],
                    &instances[i].pose.bone_transforms[bone], sizeof(instances[0].pose.bone_transforms[bone])));
    }
    bone = rasterfall_model_humanoid_bone(&instances[0].pose, RASTERFALL_HUMANOID_CHEST);
    EVADE_CHECK(instances[1].pose.bones[bone].rotate_z > 0 && instances[2].pose.bones[bone].rotate_z < 0);
    composition.secondary_additive_weight_milli = 500;
    EVADE_CHECK(!rasterfall_action_compose(&instances[1], &composition));
    EVADE_CHECK(instances[1].pose.bones[bone].rotate_z < 0 &&
        instances[1].pose.bones[bone].rotate_z > instances[2].pose.bones[bone].rotate_z);
    composition.secondary_additive_weight_milli = 1001;
    EVADE_CHECK(rasterfall_action_compose(&instances[1], &composition) < 0);
    composition.secondary_additive_weight_milli = 1000;
    result = instances[2].pose.bones[bone].rotate_x;
    composition.layers[2].clip = NULL;
    EVADE_CHECK(!rasterfall_action_compose(&instances[1], &composition));
    EVADE_CHECK(instances[1].pose.bones[bone].rotate_x != result &&
        instances[2].pose.bones[bone].rotate_x == result); /* Recoil and peer survive. */
    composition.layers[2].clip = humanoid_action(RASTERFALL_ACTION_RIFLE_RECOIL);
    composition.secondary_additive.time_ms = 500;
    EVADE_CHECK(!rasterfall_action_compose(&instances[1], &composition));
    EVADE_CHECK(!memcmp(instances[0].pose.bone_transforms, instances[1].pose.bone_transforms,
        instances[0].pose.bone_count * sizeof(*instances[0].pose.bone_transforms)));
    composition.secondary_additive.time_ms = 140;
    composition.secondary_additive_weight_milli = 0;
    EVADE_CHECK(!rasterfall_action_compose(&instances[1], &composition));
    EVADE_CHECK(!memcmp(instances[0].pose.bone_transforms, instances[1].pose.bone_transforms,
        instances[0].pose.bone_count * sizeof(*instances[0].pose.bone_transforms)));
    invalid = *composition.secondary_additive.clip;
    invalid.tracks[0].target = RASTERFALL_HUMANOID_HIPS;
    composition.secondary_additive.clip = &invalid;
    composition.secondary_additive_weight_milli = 1000;
    EVADE_CHECK(rasterfall_action_compose(&instances[1], &composition) < 0);

    /* The real Scene source freezes independent event samples; extraction
     * consumes only that immutable frame and includes final attachment IK. */
    toy_game_init(&sample_game, 938);
    camera.cy = camera.pitch_cy = 1024;
    EVADE_CHECK(!rf_gpu_scene_local_world(&source));
    for (int i = 0; i < 2; i++) {
        int id = toy_game_add_gunner(&sample_game, 1, i * 1000, 0, "POSE EVADE");
        sample = toy_game_actor_by_id(&sample_game, id);
        EVADE_CHECK(sample);
        sample->animation.id = TOY_GAME_ANIM_MOVE; sample->animation.time_ms = 180;
        EVADE_CHECK(!rf_gpu_scene_local_created(&source, sample));
    }
    EVADE_CHECK(!rf_gpu_scene_local_freeze(&source, &sample_game, &camera, 1280, 720, 1, &frame));
    for (int i = 0; i < 2; i++) {
        sample = toy_game_actor_by_id(&sample_game, source.actors[i].actor_id);
        sample->animation.id = TOY_GAME_ANIM_FIRE; sample->animation.time_ms = 35;
        sample->evasion.sequence = 1; sample->evasion.animation_ms = 360 - i * 60;
        sample->evasion.source_x = sample->x + (i ? -1000 : 1000);
        sample->evasion.strength_milli = 50000;
    }
    saved_game = sample_game;
    EVADE_CHECK(!rf_gpu_scene_local_freeze(&source, &sample_game, &camera, 1280, 720, 1, &frame));
    EVADE_CHECK(frame.presentation.evasion.side == -1 && frame.presentations[1].evasion.side == 1 &&
        frame.presentation.evasion.time_ms == 140 && frame.presentations[1].evasion.time_ms == 200);
    saved_frame = frame;
    EVADE_CHECK(!rf_gpu_scene_pose_extract_at(&frame, 0, &evaded));
    EVADE_CHECK(!rf_gpu_scene_pose_extract_at(&frame, 1, &replay));
    EVADE_CHECK(memcmp(evaded.palette, replay.palette, sizeof(evaded.palette)));
    EVADE_CHECK(!rf_gpu_scene_pose_extract_at(&frame, 0, &replay));
    EVADE_CHECK(!memcmp(&evaded, &replay, sizeof(evaded)) &&
        !memcmp(&frame, &saved_frame, sizeof(frame)) && !memcmp(&sample_game, &saved_game, sizeof(sample_game)));
    frame.presentation.evasion.weight_milli = 0;
    EVADE_CHECK(!rf_gpu_scene_pose_extract(&frame, &baseline));
    EVADE_CHECK(memcmp(baseline.palette, evaded.palette, sizeof(evaded.palette)) &&
        memcmp(&baseline.weapon_to_world, &evaded.weapon_to_world, sizeof(evaded.weapon_to_world)) &&
        !memcmp(&baseline.body_to_world, &evaded.body_to_world, sizeof(evaded.body_to_world)));
    composition.secondary_additive.clip = humanoid_action(RASTERFALL_ACTION_EVADE_LEFT);
    composition.secondary_additive.time_ms = 140;
    composition.secondary_additive_weight_milli = 1000;
    EVADE_CHECK(!rasterfall_action_compose(&instances[1], &composition) &&
        !rasterfall_rifle_pose_solve(&instances[1], TOY_GAME_WEAPON_AK, 835,&frame.presentation.rifle,NULL));
    EVADE_CHECK(!rasterfall_model_build_skin_palette(&instances[1].pose,
        replay.palette, evaded.bone_count));
    EVADE_CHECK(!memcmp(evaded.palette, replay.palette, sizeof(evaded.palette)));
done:
    for (int i = 0; i < 3; i++) rasterfall_model_instance_unload(&instances[i]);
    if (failure) __printf("scene evasion FAIL line=%d\n", failure);
    else __printf("scene evasion PASS lower/recoil/expiry/mask/independent-events/transitions/frozen-replay/final-IK\n");
#undef EVADE_CHECK
    return failure != 0;
}

static int scene_equipment_pose_test(void)
{
    static struct rasterfall_model_instance host;
    static struct rf_gpu_scene_pose_v1 scene, repeat;
    struct rasterfall_modular_pose_cache cache;
    struct rasterfall_character_visual_recipe recipe = {0};
    struct rasterfall_action_composition composition = {0};
    struct rf_gpu_scene_actor_v1 actor = {0};
    struct rf_gpu_scene_local_presentation presentation = {0};
    struct rasterfall_rigid_transform bind_left, bind_right, expected = {0};
    struct rasterfall_model_skin_palette_bone before[RF_GPU_SCENE_POSE_BONES];
    int failure = 0;
#define EQUIPMENT_CHECK(x) do { if (!(x)) { failure = __LINE__; goto done; } } while (0)
    EQUIPMENT_CHECK(!modular_actor_resources_init());
    memset(&host,0,sizeof(host));
    EQUIPMENT_CHECK(!rasterfall_model_instance_init(&host,&modular_actor_runtime.body));
    /* Exercise the capacity contract independently of changing loadout taste:
     * every entry remains a distinct follower, including repeated sockets. */
    recipe.body_resource_id = RASTERFALL_BODY_RF_HUMANOID_V2;
    recipe.attachment_count = RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS;
    for (unsigned i=0;i<recipe.attachment_count;++i) {
        recipe.attachments[i].host_socket = RASTERFALL_ATTACHMENT_HEAD;
        recipe.attachments[i].gear_resource_id = i & 1 ?
            RASTERFALL_GEAR_BALLISTIC_GOGGLES : RASTERFALL_GEAR_HEAVY_HEAD;
    }
    recipe.attachments[0].host_socket = RASTERFALL_ATTACHMENT_HIP_L;
    recipe.attachments[0].gear_resource_id = RASTERFALL_GEAR_CARGO_THIGH_L;
    recipe.attachments[1].host_socket = RASTERFALL_ATTACHMENT_HIP_R;
    recipe.attachments[1].gear_resource_id = RASTERFALL_GEAR_CARGO_THIGH_R;
    actor.weapon=-1; actor.cy=1024; actor.x=170; actor.y=-900; actor.z=320;
    presentation.scene_light_q8=256;
    composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_WALK);
    composition.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_IDLE);
    EQUIPMENT_CHECK(composition.layers[0].clip && composition.layers[1].clip);
    for (int sample=0;sample<2;++sample) {
        composition.layers[0].time_ms=sample*180;
        EQUIPMENT_CHECK(!rasterfall_action_compose(&host,&composition));
        memset(before,0,sizeof(before)); memset(&cache,0,sizeof(cache));
        memset(&scene,0,sizeof(scene)); memset(&repeat,0,sizeof(repeat));
        EQUIPMENT_CHECK(!rasterfall_model_build_skin_palette(&host.pose,before,host.pose.bone_count));
        EQUIPMENT_CHECK(!modular_prepare_gear_transforms(&host,&recipe,modular_actor_runtime.gear,&cache));
        EQUIPMENT_CHECK(cache.gear_count==recipe.attachment_count);
        EQUIPMENT_CHECK(!scene_pose_from_instance(&scene,&host,&recipe,&actor,&presentation));
        EQUIPMENT_CHECK(scene.attachment_count==recipe.attachment_count &&
            !memcmp(before,scene.palette,sizeof(before)));
        EQUIPMENT_CHECK(!scene_pose_from_instance(&repeat,&host,&recipe,&actor,&presentation) &&
            !memcmp(&scene,&repeat,sizeof(scene)));
        for (unsigned i=0;i<recipe.attachment_count;++i) {
            modular_rigid_compose(&scene.body_to_world,&cache.gear_local[i],&expected);
            EQUIPMENT_CHECK(!memcmp(&expected,&scene.attachments[i].model_to_world,sizeof(expected)) &&
                scene.attachments[i].resource_id==(unsigned)recipe.attachments[i].gear_resource_id);
            if(i>2) EQUIPMENT_CHECK(!memcmp(&cache.gear_local[2],&cache.gear_local[i],sizeof(expected)));
        }
        if (!sample) { bind_left=cache.gear_local[0]; bind_right=cache.gear_local[1]; }
        else EQUIPMENT_CHECK(memcmp(bind_left.rotation,cache.gear_local[0].rotation,sizeof(bind_left.rotation)) &&
            memcmp(bind_right.rotation,cache.gear_local[1].rotation,sizeof(bind_right.rotation)));
        EQUIPMENT_CHECK(!rasterfall_model_build_skin_palette(&host.pose,repeat.palette,host.pose.bone_count) &&
            !memcmp(before,repeat.palette,sizeof(before)));
    }
    recipe.attachment_count++;
    EQUIPMENT_CHECK(modular_prepare_gear_transforms(&host,&recipe,modular_actor_runtime.gear,&cache)<0 &&
        scene_pose_from_instance(&scene,&host,&recipe,&actor,&presentation)<0);
done:
    rasterfall_model_instance_unload(&host);
    if(failure) __printf("scene equipment FAIL line=%d\n",failure);
    else __printf("scene equipment PASS capacity/repeated-socket/left-right-walk/CPU-Scene/read-only/overflow\n");
#undef EQUIPMENT_CHECK
    return failure != 0;
}

static int scene_fixture_clock_test(void)
{
    static struct toy_game fixture_game, untouched;
    static struct rf_gpu_scene_local_frame frame, frozen, unpatched;
    static struct rf_gpu_scene_pose_v1 pose, repeat;
    static struct rasterfall_model_instance reference;
    struct rf_gpu_scene_local_source source={0};
    struct scene_actor_fixture_clock clocks[TOY_GAME_MAX_ACTORS]={0};
    struct rasterfall_action_composition composition={0};
    struct camera camera={0};
    uint64_t saved_time=outpost_actor_actions_time_us;
    int period=toy_game_animation_info(TOY_GAME_ANIM_MOVE)->duration_ms;
    int failure=0;
#define FIXTURE_CLOCK_CHECK(x) do { if (!(x)) { failure=__LINE__; goto done; } } while (0)
    memset(&fixture_game,0,sizeof(fixture_game));
    memset(&reference,0,sizeof(reference));
    camera.cy=camera.pitch_cy=1024;
    outpost_actor_actions_time_us=(uint64_t)(period+180)*1000;
    outpost_actor_fixture(&fixture_game.actors[0],1,1,TOY_GAME_ANIM_MOVE,
        0,0,1,outpost_actor_actions_time_us,32);
    /* An ordinary actor in the old numeric fixture range is not tagged. */
    fixture_game.actors[1]=fixture_game.actors[0];
    fixture_game.actors[1].actor_id=10098;
    clocks[0].actor_id=fixture_game.actors[0].actor_id;
    clocks[0].time_ms=outpost_actor_locomotion_time(clocks[0].actor_id);
    untouched=fixture_game;
    FIXTURE_CLOCK_CHECK(!rf_gpu_scene_local_world(&source));
    for (int i=0;i<2;++i)
        FIXTURE_CLOCK_CHECK(!rf_gpu_scene_local_created(&source,&fixture_game.actors[i]));
    FIXTURE_CLOCK_CHECK(!rf_gpu_scene_local_freeze_presentation(&source,&fixture_game,
        &camera,640,480,0,NULL,0,&frame));
    unpatched=frame;
    FIXTURE_CLOCK_CHECK(frame.presentation.lower_time_ms==180);
    scene_actor_freeze_fixture_clocks(&frame,clocks);
    FIXTURE_CLOCK_CHECK(frame.presentation.lower_time_ms==(uint64_t)(period+180) &&
        frame.presentations[0].lower_time_ms==frame.presentation.lower_time_ms &&
        frame.presentations[1].lower_time_ms==unpatched.presentations[1].lower_time_ms);
    frozen=frame;
    FIXTURE_CLOCK_CHECK(!rf_gpu_scene_pose_extract_at(&frame,0,&pose));
    FIXTURE_CLOCK_CHECK(!rasterfall_model_instance_init(&reference,&modular_actor_runtime.body));
    composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_WALK);
    composition.layers[0].time_ms=outpost_actor_locomotion_time(clocks[0].actor_id);
    composition.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_IDLE);
    composition.layers[1].time_ms=fixture_game.actors[0].animation.time_ms;
    FIXTURE_CLOCK_CHECK(!rasterfall_action_compose(&reference,&composition) &&
        !rasterfall_rifle_pose_solve(&reference,TOY_GAME_WEAPON_AK,835,&frame.presentation.rifle,NULL));
    memset(&repeat,0,sizeof(repeat));
    FIXTURE_CLOCK_CHECK(!rasterfall_model_build_skin_palette(&reference.pose,
        repeat.palette,reference.pose.bone_count) &&
        !memcmp(pose.palette,repeat.palette,sizeof(pose.palette)));
    scene_actor_freeze_fixture_clocks(&frame,clocks);
    FIXTURE_CLOCK_CHECK(!rf_gpu_scene_pose_extract_at(&frame,0,&repeat) &&
        !memcmp(&pose,&repeat,sizeof(pose)) && !memcmp(&frame,&frozen,sizeof(frame)) &&
        !memcmp(&fixture_game,&untouched,sizeof(fixture_game)) &&
        outpost_actor_actions_time_us==(uint64_t)(period+180)*1000);
    /* A recycled slot with a different identity cannot borrow the clock. */
    clocks[0].actor_id++;
    frame=unpatched;
    scene_actor_freeze_fixture_clocks(&frame,clocks);
    FIXTURE_CLOCK_CHECK(!memcmp(&frame,&unpatched,sizeof(frame)));
done:
    outpost_actor_actions_time_us=saved_time;
    rasterfall_model_instance_unload(&reference);
    if(failure) __printf("scene fixture clock FAIL line=%d\n",failure);
    else __printf("scene fixture clock PASS late-first-sample/CPU-palette/replay/identity/no-game-mutation\n");
#undef FIXTURE_CLOCK_CHECK
    return failure!=0;
}

static int scene_rifle_cycle_test(void)
{
    struct outpost_rifle_cycle_sample previous={0},sample,repeat;
    static struct rf_gpu_scene_pose_v1 pose,replay;
    int seen[4]={0};
#define CYCLE_CHECK(x) do { if(!(x)) { __printf("scene rifle cycle FAIL line=%d\n",__LINE__); return 1; } } while(0)
    for(unsigned t=0;t<=50000;t+=100) {
        outpost_rifle_cycle_sample_at(t,&sample);
        outpost_rifle_cycle_sample_at(t,&repeat);
        CYCLE_CHECK(!memcmp(&sample,&repeat,sizeof(sample)));
        CYCLE_CHECK(sample.phase>=0 && sample.phase<4);
        seen[sample.phase]=1;
        CYCLE_CHECK(sample.z>=-2200 && sample.z<=2200 &&
            sample.body.rifle.aim_milli+sample.body.rifle.hip_milli==1000);
        CYCLE_CHECK(sample.body.rifle.pitch_mdeg>=-75000 && sample.body.rifle.pitch_mdeg<=75000 &&
            sample.body.rifle.yaw_mdeg>=-50000 && sample.body.rifle.yaw_mdeg<=50000);
        if(t) CYCLE_CHECK(abs(sample.z-previous.z)<=40 &&
            sample.sy*previous.sy+sample.cy*previous.cy>900000);
        if(sample.body.walk) CYCLE_CHECK(sample.body.fire);
        if(sample.phase==3) CYCLE_CHECK(!sample.body.fire && !sample.body.walk);
        previous=sample;
    }
    for(int phase=0;phase<4;++phase) CYCLE_CHECK(seen[phase]);
    static const unsigned samples[]={1000,5835,6035,12000,15635,16335,24100,26000,38335,40635,49900};
    for(unsigned i=0;i<sizeof(samples)/sizeof(samples[0]);++i) {
        outpost_rifle_cycle_sample_at(samples[i],&sample);
        repeat=sample;
        CYCLE_CHECK(!rf_gpu_scene_pose_body_sample(RASTERFALL_BODY_RF_C01_V22H,91,7,
            &sample.body,3000,-900,sample.z,sample.sy,sample.cy,&pose));
        CYCLE_CHECK(!rf_gpu_scene_pose_body_sample(RASTERFALL_BODY_RF_C01_V22H,91,7,
            &sample.body,3000,-900,sample.z,sample.sy,sample.cy,&replay));
        CYCLE_CHECK(!memcmp(&pose,&replay,sizeof(pose)) && !memcmp(&sample,&repeat,sizeof(sample)) &&
            pose.weapon_valid && pose.body_to_world.rotation[2]==sample.sy/1024.0);
        /* Flash frames must also survive procedural color packing, including
         * the new plot's world coordinates outside signed int16 range. */
        struct rf_gpu_scene_procedural_item_v1 block={0};
        struct scene_infected_test_geometry geometry={0};
        block.state.x=45640;block.state.z=-19968+sample.z;
        block.state.sy=sample.sy;block.state.cy=sample.cy;
        block.state.weapon=TOY_GAME_WEAPON_AK;
        block.state.animation_id=sample.body.fire?TOY_GAME_ANIM_FIRE:TOY_GAME_ANIM_IDLE;
        block.state.animation_time_ms=sample.body.action_time_ms;
        block.state.aim_pitch_mdeg=sample.body.rifle.pitch_mdeg;
        block.state.aim_yaw_mdeg=sample.body.rifle.yaw_mdeg;
        block.state.moving=sample.body.walk;
        block.state.locomotion_time_ms=(int)(sample.body.lower_time_ms%400);
        block.state.muzzle_flash=pose.muzzle_flash;
        block.body_color=0x405060;block.leg_color=0x203040;
        block.skin_color=0xc09070;block.hair_color=0x302010;
        CYCLE_CHECK(!rf_gpu_scene_procedural_triangles(&block,scene_infected_test_triangle,&geometry) &&
            geometry.count && geometry.count<=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES);
    }
    __printf("scene rifle cycle PASS phases/return/turn/continuous-position/frozen-direction/palette/replay\n");
#undef CYCLE_CHECK
    return 0;
}

/* Explicit resource test: missing private assets is a failure, not a skip. */
int rf_gpu_scene_pose_logic_test(void)
{
    static struct toy_game source_game, before;
    static struct rf_gpu_scene_local_frame frame, frozen;
    static struct rf_gpu_scene_pose_v1 first, repeat, moved, replacement;
    struct rf_gpu_scene_local_source source={0};
    struct camera camera={0};
    struct toy_game_actor *actor;
    int id,slot;
#define POSE_CHECK(x) do { if (!(x)) { __printf("scene pose FAIL line=%d\n",__LINE__); return 1; } } while (0)
    POSE_CHECK(!scene_independent_enemy_source_test());
    POSE_CHECK(!scene_grip_pose_test());
    POSE_CHECK(!scene_rifle_cycle_test());
    POSE_CHECK(!scene_evasion_pose_test());
    POSE_CHECK(!scene_equipment_pose_test());
    POSE_CHECK(!scene_clothing_pose_test());
    POSE_CHECK(!scene_fixture_clock_test());
    /* Exercise the real network presentation adapter without sockets or a GPU.
     * Bodies behind the camera still freeze their values before raster clipping. */
    {
        static struct rasterfall_net net, saved_net;
        static struct rf_gpu_scene_enemy_frame_v1 captured;
        struct toy_renderer renderer={0};
        struct camera viewer={0}; viewer.cy=1024;
        for (int client=0;client<2;++client) {
            memset(&net,0,sizeof(net));memset(&source_game,0,sizeof(source_game));
            net.mode=client ? RASTERFALL_NET_CLIENT : RASTERFALL_NET_HOST;
            net.local_player_id=2;
            actor=&source_game.actors[TOY_GAME_REMOTE_ACTOR_BASE];
            actor->active=1;actor->kind=TOY_GAME_ACTOR_PLAYER;
            actor->character_id=RASTERFALL_CHARACTER_HURD_GUNSMITH;
            actor->slots[0].weapon=TOY_GAME_WEAPON_AK;actor->current_slot=0;
            actor->animation.id=TOY_GAME_ANIM_MOVE;actor->animation.time_ms=123;
            net.clients[0].active=1;net.clients[0].connected=1;net.clients[0].client_id=1;
            net.clients[0].camera.x=321;net.clients[0].camera.z=-9000;
            net.clients[0].camera.cy=-1024;
            net.remote_render_camera[1]=net.clients[0].camera;
            for (int downed=0;downed<2;++downed) {
                actor->state=downed ? TOY_GAME_ACTOR_DOWNED : TOY_GAME_ACTOR_ALIVE;
                before=source_game;saved_net=net;
                rf_gpu_scene_enemy_begin(1,1);
                render_network_teammate(&renderer,&viewer,&net,&source_game);
                POSE_CHECK(!rf_gpu_scene_enemy_freeze(&captured));
                POSE_CHECK(captured.procedural_count==1 && !captured.failed);
                const struct rasterfall_procedural_humanoid_state *s=&captured.procedural[0].state;
                POSE_CHECK(s->x==321 && s->z==-9000 && s->cy==-1024 &&
                    s->weapon==TOY_GAME_WEAPON_AK && s->downed==downed &&
                    s->animation_id==TOY_GAME_ANIM_MOVE && s->animation_time_ms==123);
                struct scene_infected_test_geometry geometry={0};
                POSE_CHECK(!rf_gpu_scene_procedural_triangles(&captured.procedural[0],
                    scene_infected_test_triangle,&geometry) && geometry.count);
                POSE_CHECK(!memcmp(&before,&source_game,sizeof(before)) &&
                    !memcmp(&saved_net,&net,sizeof(net)));
                /* The independent adapter must resolve the same network input
                 * without invoking render_network_teammate or recording draws. */
                {
                    static struct rasterfall_session owner;
                    struct rasterfall_session *saved_session=active_session;
                    struct toy_game *saved_presentation_game=active_presentation_game;
                    const struct rasterfall_net *saved_active_net=active_net;
                    struct rasterfall_world_lighting *saved_light=active_world_lighting;
                    memset(&owner,0,sizeof(owner));owner.game_state=source_game;
                    owner.world_id=RASTERFALL_WORLD_PERF_EMPTY;
                    active_session=&owner;active_net=&net;active_world_lighting=NULL;
                    active_presentation_game=&owner.game_state;
                    rf_gpu_scene_enemy_begin(10+downed,1);
                    int result=rf_gpu_scene_actor_collect_independent(&viewer,1280,720);
                    active_session=saved_session;active_net=saved_active_net;
                    active_presentation_game=saved_presentation_game;
                    active_world_lighting=saved_light;
                    POSE_CHECK(!result && !rf_gpu_scene_enemy_freeze(&captured));
                    POSE_CHECK(captured.procedural_count==1 && !captured.modular_count);
                    s=&captured.procedural[0].state;
                    POSE_CHECK(s->x==321 && s->z==-9000 && s->cy==-1024 &&
                        s->weapon==TOY_GAME_WEAPON_AK && s->downed==downed &&
                        s->animation_time_ms==123);
                    POSE_CHECK(!memcmp(&owner.game_state,&before,sizeof(before)) &&
                        !memcmp(&saved_net,&net,sizeof(net)) && !renderer.cmd_count);
                }
            }
            /* Local player exclusion on guest, disconnected peer on host. */
            if (client) {
                /* Client slot zero owns the local avatar. Changing the peer
                 * ID without remapping slots would turn this into the host. */
                source_game.actors[0]=*actor;
                actor->active=0;
            } else net.clients[0].connected=0;
            rf_gpu_scene_enemy_begin(2,1);
            render_network_teammate(&renderer,&viewer,&net,&source_game);
            POSE_CHECK(!rf_gpu_scene_enemy_freeze(&captured) && !captured.procedural_count);
            if (client) {
                *actor=source_game.actors[0];
                source_game.actors[0].active=0;
            }
            actor->active=0;net.local_player_id=2;net.clients[0].connected=1;
            rf_gpu_scene_enemy_begin(3,1);
            render_network_teammate(&renderer,&viewer,&net,&source_game);
            POSE_CHECK(!rf_gpu_scene_enemy_freeze(&captured) && !captured.procedural_count);
        }
        __printf("scene network logic PASS host/guest/downed/local/disconnected/inactive/immutable/geometry\n");
    }
    for (int profession=0;profession<RASTERFALL_PROFESSION_COUNT;++profession) {
        struct rf_gpu_scene_procedural_item_v1 item={0},original;
        item.state.x=123;item.state.z=456;item.state.lift=90;
        item.state.sy=384;item.state.cy=-949;
        item.state.weapon=TOY_GAME_WEAPON_AK;
        item.state.profession_id=profession;
        item.body_color=0x405060;item.skin_color=0xc09070;
        item.leg_color=0x203040;item.hair_color=0x302010;
        item.scene_light_q8=256;item.vertex_lighting=1;
        for (int animation=0;animation<TOY_GAME_ANIM_COUNT;++animation) {
            struct scene_infected_test_geometry a={0},b={0},failure={0};
            item.state.animation_id=animation;item.state.animation_time_ms=120;
            original=item;
            POSE_CHECK(!rf_gpu_scene_procedural_triangles(&item,scene_infected_test_triangle,&a));
            POSE_CHECK(a.count>0 && a.count<=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES);
            POSE_CHECK(!rf_gpu_scene_procedural_triangles(&item,scene_infected_test_triangle,&b));
            POSE_CHECK(a.hash==b.hash && a.count==b.count && !memcmp(&item,&original,sizeof(item)));
            failure.fail=1;
            POSE_CHECK(rf_gpu_scene_procedural_triangles(&item,scene_infected_test_triangle,&failure)<0);
            item.state.sy=1025;failure.fail=0;
            POSE_CHECK(rf_gpu_scene_procedural_triangles(&item,scene_infected_test_triangle,&failure)<0 && !failure.count);
            item=original;
        }
    }
    __printf("scene procedural PASS professions/actions/weapon/replay/failure\n");
    for (int recipe=0;recipe<6;++recipe) {
        struct rf_gpu_scene_enemy_item_v1 item={0},saved_item;
        struct scene_infected_test_geometry a={0},b={0},motion={0},failure={0};
        item.infected_recipe=recipe+1;item.type=enemy_visual_recipes[recipe].type;
        item.squash=1000;
        item.dir_x=384;item.dir_z=-949;item.x=120;item.z=500;item.lift=90;
        item.scene_light_q8=256;saved_item=item;
        POSE_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_infected_test_triangle,&a));
        POSE_CHECK(a.count>0 && a.count<=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES);
        item.infected_swing=24;
        POSE_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_infected_test_triangle,&motion));
        POSE_CHECK(a.hash!=motion.hash && a.count==motion.count);
        /* Overwrite the old producer's scratch pose; replay must ignore it. */
        enemy_visual_apply_pose(rasterfall_model_instance_pose(&enemy_visual_cache[recipe].instance),
            item.type,-24);
        POSE_CHECK(!rasterfall_model_instance_update_bones(&enemy_visual_cache[recipe].instance));
        item=saved_item;
        POSE_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_infected_test_triangle,&b));
        POSE_CHECK(a.hash==b.hash && a.count==b.count && !memcmp(&item,&saved_item,sizeof(item)));
        failure.fail=1;
        POSE_CHECK(rf_gpu_scene_enemy_triangles(&item,scene_infected_test_triangle,&failure)<0);
        item.infected_recipe=7;failure.fail=0;
        POSE_CHECK(rf_gpu_scene_enemy_triangles(&item,scene_infected_test_triangle,&failure)<0 && !failure.count);
    }
    __printf("scene infected PASS six-recipes/stride/replay/scratch-isolation/failure\n");
    camera.cy=camera.pitch_cy=1024;
    {
        static struct rasterfall_session owner;
        static struct rf_gpu_scene_enemy_frame_v1 captured;
        struct rasterfall_session *saved_session=active_session;
        struct toy_game *saved_presentation_game=active_presentation_game;
        const struct rasterfall_net *saved_net=active_net;
        struct rasterfall_world_lighting *saved_light=active_world_lighting;
        memset(&owner,0,sizeof(owner));toy_game_init(&owner.game_state,1);
        owner.world_id=RASTERFALL_WORLD_PERF_EMPTY;
        id=toy_game_add_ai(&owner.game_state,TOY_GAME_AI_LEVEL_1,120,240,"INDEPENDENT");
        POSE_CHECK(id>0);actor=&owner.game_state.actors[id-1];
        actor->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
        actor->slots[0].weapon=TOY_GAME_WEAPON_AK;actor->current_slot=0;
        for (int downed=0;downed<2;++downed) {
            actor->state=downed ? TOY_GAME_ACTOR_DOWNED : TOY_GAME_ACTOR_ALIVE;
            before=owner.game_state;
            rf_gpu_scene_enemy_begin(100+downed,77);
            active_session=&owner;active_net=NULL;active_world_lighting=NULL;
            active_presentation_game=&owner.game_state;
            int result=rf_gpu_scene_actor_collect_independent(&camera,1280,720);
            active_session=saved_session;active_net=saved_net;
            active_presentation_game=saved_presentation_game;
            active_world_lighting=saved_light;
            POSE_CHECK(!result && !rf_gpu_scene_enemy_freeze(&captured));
            POSE_CHECK(captured.modular_count==(unsigned)!downed &&
                captured.procedural_count==(unsigned)downed);
            if (downed) POSE_CHECK(captured.procedural[0].state.downed);
            else POSE_CHECK(captured.modular[0].bone_count && captured.modular[0].weapon_valid);
            POSE_CHECK(!memcmp(&owner.game_state,&before,sizeof(before)));
        }
        __printf("scene independent actors PASS supplemental/downed/immutable/network\n");
    }
    toy_game_init(&source_game,1);
    id=toy_game_add_ai(&source_game,TOY_GAME_AI_LEVEL_1,120,240,"POSE");
    POSE_CHECK(id>0); slot=id-1; actor=&source_game.actors[slot];
    actor->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
    actor->slots[0].weapon=TOY_GAME_WEAPON_AK; actor->current_slot=0;
    toy_game_actor_set_animation(actor,TOY_GAME_ANIM_MOVE);
    toy_game_actor_update_animation(actor,160);
    POSE_CHECK(!rf_gpu_scene_local_world(&source) && !rf_gpu_scene_local_created(&source,actor));
    before=source_game;
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    frozen=frame;
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&first));
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&repeat));
    POSE_CHECK(!memcmp(&first,&repeat,sizeof(first)) && !memcmp(&frame,&frozen,sizeof(frame)) &&
        !memcmp(&source_game,&before,sizeof(source_game)) && first.bone_count && first.attachment_count && first.weapon_valid);
    /* CPU instance skinning is the reference for the copied palette, including
     * attachment IK. Query attachments directly with the world transform too. */
    {
        struct rasterfall_model_instance *reference=tlibc_malloc(sizeof(*reference));
        struct rasterfall_action_composition composition={0};
        const struct rasterfall_character_visual_recipe *recipe=
            rasterfall_character_visual_recipe_for_character(actor->character_id);
        int failed=0;
        POSE_CHECK(reference);
        memset(reference,0,sizeof(*reference));
        POSE_CHECK(!rasterfall_model_instance_init(reference,&modular_actor_runtime.body));
        composition.layers[0].clip=humanoid_action(RASTERFALL_ACTION_LOCOMOTION_WALK);
        composition.layers[0].time_ms=160;
        composition.layers[1].clip=humanoid_action(RASTERFALL_ACTION_RIFLE_IDLE);
        composition.layers[1].time_ms=160;
        if (rasterfall_action_compose(reference,&composition)<0 ||
            rasterfall_rifle_pose_solve(reference,TOY_GAME_WEAPON_AK,835,&frame.presentation.rifle,NULL)<0) failed=1;
        for (unsigned int v=0; !failed && v<modular_actor_runtime.body.definition.vertex_count; ++v) {
            int a[3],b[3],an[3],bn[3];
            if (rasterfall_model_instance_skin_vertex(reference,v,a,an)<0 ||
                rasterfall_model_skin_vertex_palette(&modular_actor_runtime.body.definition,
                    first.palette,first.bone_count,v,b,bn)<0 ||
                memcmp(a,b,sizeof(a)) || memcmp(an,bn,sizeof(an))) failed=1;
        }
        for (unsigned int i=0; !failed && i<recipe->attachment_count; ++i) {
            struct rasterfall_rigid_attachment_desc desc={0};
            struct rasterfall_rigid_transform expected={0};
            desc.host_socket=recipe->attachments[i].host_socket;
            desc.resource=&modular_actor_runtime.gear[recipe->attachments[i].gear_resource_id];
            modular_rigid_identity(&desc.mount_correction);
            if (rigid_attachment_model_to_world(reference,&desc,&first.body_to_world,&expected)<0 ||
                memcmp(&expected,&first.attachments[i].model_to_world,sizeof(expected))) failed=1;
        }
        if (!failed) {
            struct rasterfall_modular_weapon_placement placement;
            if (modular_prepare_weapon_placement(reference,TOY_GAME_WEAPON_AK,835,&placement)<0) failed=1;
            for (unsigned int v=0; !failed && v<placement.model->vertex_count; ++v) {
                const int *raw=(const int *)(placement.model->vertices+v*placement.model->vertex_bytes);
                int canonical[3]; double authored[3],model_point[3],world[3],actual[3];
                double raw_point[3]={raw[0],raw[1],raw[2]};
                modular_weapon_canonical_point(&placement.adapter,raw,canonical);
                for (int a=0;a<3;++a) authored[a]=canonical[a]*placement.geometry_scale/1000.0;
                modular_weapon_model_point(placement.origin,placement.rotation,authored,835,model_point);
                rigid_matrix_vector(first.body_to_world.rotation,model_point,world);
                rigid_matrix_vector(first.weapon_to_world.rotation,raw_point,actual);
                for (int a=0;a<3;++a) {
                    world[a]=first.body_to_world.translation[a]+world[a]*835.0/1000.0;
                    actual[a]=first.weapon_to_world.translation[a]+actual[a]*first.weapon_to_world.scale_milli/1000.0;
                    if (fabs(world[a]-actual[a])>0.000001) failed=1;
                }
            }
        }
        rasterfall_model_instance_unload(reference); tlibc_free(reference);
        POSE_CHECK(!failed);
    }
    /* Advance authored time through a gameplay wrap, then retain lower phase during FIRE. */
    toy_game_actor_update_animation(actor,160);
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    toy_game_actor_update_animation(actor,160);
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    POSE_CHECK(frame.presentation.lower_time_ms==480);
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&repeat));
    POSE_CHECK(memcmp(first.palette,repeat.palette,sizeof(first.palette)));
    toy_game_actor_set_animation(actor,TOY_GAME_ANIM_FIRE);
    toy_game_actor_update_animation(actor,16);
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    POSE_CHECK(frame.presentation.lower_walk && frame.presentation.lower_time_ms==480);
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&repeat));
    /* Moving the same object changes ordering only; no slot history is borrowed. */
    source_game.actors[slot+1]=*actor; memset(actor,0,sizeof(*actor));
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&moved));
    POSE_CHECK(moved.identity.generation==repeat.identity.generation);
    moved.frame_id=repeat.frame_id;
    POSE_CHECK(!memcmp(&repeat,&moved,sizeof(moved)));
    /* Reuse ID between freezes: restarted clock and entirely fresh pose/IK. */
    rf_gpu_scene_local_destroyed(&source);
    memset(&source_game.actors[slot+1],0,sizeof(*actor));
    POSE_CHECK(toy_game_add_ai(&source_game,TOY_GAME_AI_LEVEL_1,120,240,"NEW")==id);
    actor=&source_game.actors[slot]; actor->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
    actor->slots[0].weapon=TOY_GAME_WEAPON_AK; actor->current_slot=0;
    toy_game_actor_set_animation(actor,TOY_GAME_ANIM_MOVE);
    toy_game_actor_update_animation(actor,160);
    POSE_CHECK(!rf_gpu_scene_local_created(&source,actor));
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame));
    POSE_CHECK(frame.presentation.lower_time_ms==160);
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&replacement));
    POSE_CHECK(replacement.identity.generation>first.identity.generation);
    replacement.identity=first.identity; replacement.frame_id=first.frame_id;
    POSE_CHECK(!memcmp(&replacement,&first,sizeof(first)));
    POSE_CHECK(!rf_gpu_scene_local_world(&source) && !rf_gpu_scene_local_created(&source,actor));
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,640,480,1,&frame));
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&replacement));
    POSE_CHECK(replacement.world_generation>first.world_generation &&
        replacement.identity.generation>first.identity.generation);
    replacement.identity=first.identity; replacement.frame_id=first.frame_id;
    replacement.world_generation=first.world_generation;
    POSE_CHECK(!memcmp(&replacement,&first,sizeof(first)));
    /* Old immutable input remains reproducible after all lifetime changes. */
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frozen,&repeat) && !memcmp(&first,&repeat,sizeof(first)));
    frame.presentation.identity.generation++;
    POSE_CHECK(rf_gpu_scene_pose_extract(&frame,&repeat)<0 && !memcmp(&first,&repeat,sizeof(first)));
    frame=frozen; frame.snapshot.actors[0].weapon=TOY_GAME_WEAPON_COUNT;
    POSE_CHECK(rf_gpu_scene_pose_extract(&frame,&repeat)<0 && !memcmp(&first,&repeat,sizeof(first)));
    frame=frozen; frame.presentation.state=TOY_GAME_ACTOR_DOWNED;
    POSE_CHECK(rf_gpu_scene_pose_extract(&frame,&repeat)<0 && !memcmp(&first,&repeat,sizeof(first)));
    rf_gpu_scene_local_destroyed(&source);
    POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,640,480,1,&frame));
    POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&repeat) && !repeat.actor_count && !repeat.bone_count);
    /* The real session roster adapter uses the same extraction entry. */
    {
        static struct rasterfall_session owner;
        POSE_CHECK(!rasterfall_session_load(&owner,"rasterfall/assets/maps/rasterfall.map"));
        rasterfall_session_reset(&owner,&camera,1);
        POSE_CHECK(owner.scene_local.active);
        POSE_CHECK(!rf_gpu_scene_local_freeze(&owner.scene_local,&owner.game_state,
            &camera,1280,720,1,&frame));
        POSE_CHECK(frame.snapshot.actor_count==8 &&
            !rf_gpu_scene_pose_extract(&frame,&repeat) && repeat.actor_count==1);
        for(uint32_t i=1;i<frame.snapshot.actor_count;++i)
            POSE_CHECK(!rf_gpu_scene_pose_extract_at(&frame,i,&moved) &&
                moved.actor_count==1 &&
                !memcmp(&moved.identity,&frame.snapshot.actors[i].identity,
                    sizeof(moved.identity)) &&
                moved.character_id==frame.presentations[i].character_id &&
                moved.attachment_count>=3);
        rasterfall_session_unload(&owner);
        POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&replacement) &&
            !memcmp(&repeat,&replacement,sizeof(repeat)));
    }
    POSE_CHECK(!rf_gpu_scene_local_created(&source,actor));
    for(int character=RASTERFALL_CHARACTER_RF_RIFLEMAN;
        character<RASTERFALL_CHARACTER_COUNT;++character) {
        const struct rasterfall_character_visual_recipe *recipe=
            rasterfall_character_visual_recipe_for_character(character);
        POSE_CHECK(recipe);
        actor->character_id=character;
        POSE_CHECK(!rf_gpu_scene_local_freeze(&source,&source_game,&camera,640,480,1,&frame));
        POSE_CHECK(!rf_gpu_scene_pose_extract(&frame,&repeat) &&
            repeat.actor_count==1 && repeat.character_id==character &&
            repeat.body_resource_id==(uint32_t)recipe->body_resource_id &&
            repeat.attachment_count==recipe->attachment_count &&
            repeat.shirt_color==recipe->shirt_color &&
            repeat.pants_color==recipe->pants_color);
        POSE_CHECK(repeat.clothing_count==recipe->clothing_count &&
            repeat.body_hidden_material_mask==rasterfall_character_recipe_hidden_materials(recipe));
        for(uint32_t attachment=0;attachment<repeat.attachment_count;++attachment)
            POSE_CHECK(repeat.attachments[attachment].resource_id==
                (uint32_t)recipe->attachments[attachment].gear_resource_id &&
                repeat.attachments[attachment].host_socket==
                (uint32_t)recipe->attachments[attachment].host_socket);
    }
    __printf("scene pose PASS bones=%u attachments=%u cpu-vertices/socket/weapon/repeat/wrap/fire/move/replace/world/resize/replay/failure/empty/session-unload/modular-recipes\n",
             first.bone_count,first.attachment_count);
#undef POSE_CHECK
    return 0;
}
