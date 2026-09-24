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
    camera.cy=camera.pitch_cy=1024;
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
            modular_solve_left_hand(reference,TOY_GAME_WEAPON_AK,835)<0) failed=1;
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
                modular_weapon_canonical_point(placement.model,placement.asset,raw,canonical);
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
