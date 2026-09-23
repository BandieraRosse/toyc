/* Real Game actor constructors and updates feed the adapter. No fabricated
 * rf_gpu_scene_actor_input arrays participate in this regression. */
static int scene_local_source_test(void)
{
    static struct toy_game source_game, before;
    static struct rasterfall_session owner;
    static struct rf_gpu_scene_local_frame frame, saved;
    static struct rf_gpu_scene_frozen_v1 scene;
    struct rf_gpu_scene_local_source source = {0}, source_before;
    struct camera camera = {0};
    struct toy_game_actor *actor;
    unsigned generation;
    int id, slot;
#define LOCAL_CHECK(x) do { if (!(x)) { __printf("scene local source failed line %d\n",__LINE__); return 1; } } while (0)
    camera.cy=camera.pitch_cy=1024;
    toy_game_init(&source_game,1);
    id=toy_game_add_ai(&source_game,TOY_GAME_AI_LEVEL_1,120,240,"SCENE");
    LOCAL_CHECK(id>=0);
    slot=id-1; actor=&source_game.actors[slot];
    actor->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
    actor->ground_y=35; actor->airborne_y=80; actor->moving=1;
    toy_game_actor_set_animation(actor,TOY_GAME_ANIM_MOVE);
    toy_game_actor_update_animation(actor,16);
    LOCAL_CHECK(rf_gpu_scene_local_world(&source)==0);
    LOCAL_CHECK(rf_gpu_scene_local_created(&source,actor)==0);
    before=source_game;
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)==0);
    LOCAL_CHECK(!memcmp(&before,&source_game,sizeof(before)));
    LOCAL_CHECK(frame.snapshot.actor_count==1 && frame.snapshot.actors[0].y==-785);
    LOCAL_CHECK(frame.snapshot.actors[0].animation_time_ms==actor->animation.time_ms);
    LOCAL_CHECK(frame.snapshot.actors[0].weapon==actor->slots[actor->current_slot].weapon);
    LOCAL_CHECK(frame.presentation.character_id==actor->character_id && frame.presentation.moving==1);
    LOCAL_CHECK(rf_gpu_scene_extract_v1(&frame.snapshot,&scene)==0 && scene.item_count==1);
    LOCAL_CHECK(frame.presentation.lower_walk && frame.presentation.lower_time_ms==16);
    generation=frame.snapshot.actors[0].identity.generation;
    /* A move preserves lifetime and identity, but changes the source slot. */
    source_game.actors[slot+1]=*actor; memset(actor,0,sizeof(*actor));
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)==0);
    LOCAL_CHECK(frame.snapshot.actors[0].source_slot==(unsigned)(slot+1) &&
        frame.snapshot.actors[0].identity.generation==generation);
    rf_gpu_scene_local_destroyed(&source);
    memset(&source_game.actors[slot+1],0,sizeof(*actor));
    /* Destroy/create between freezes, reusing the same ID. */
    LOCAL_CHECK(toy_game_add_ai(&source_game,TOY_GAME_AI_LEVEL_1,300,500,"REPLACEMENT")==id);
    LOCAL_CHECK(rf_gpu_scene_local_created(&source,&source_game.actors[slot])==0);
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)==0);
    LOCAL_CHECK(frame.snapshot.actors[0].identity.generation>generation);
    generation=frame.snapshot.actors[0].identity.generation;
    LOCAL_CHECK(rf_gpu_scene_local_world(&source)==0);
    LOCAL_CHECK(rf_gpu_scene_local_created(&source,&source_game.actors[slot])==0);
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)==0);
    LOCAL_CHECK(frame.snapshot.actors[0].identity.generation>generation);
    LOCAL_CHECK(!frame.presentation.lower_walk && !frame.presentation.lower_time_ms);
    saved=frame; source_before=source;
    source_game.actors[slot].animation.time_ms=-1;
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)<0);
    LOCAL_CHECK(!memcmp(&saved,&frame,sizeof(frame)) && !memcmp(&source_before,&source,sizeof(source)));
    source_game.actors[slot].animation.time_ms=0;
    source_game.actors[slot+1]=source_game.actors[slot];
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)<0);
    LOCAL_CHECK(!memcmp(&saved,&frame,sizeof(frame)) && !memcmp(&source_before,&source,sizeof(source)));
    memset(&source_game.actors[slot+1],0,sizeof(*actor));
    rf_gpu_scene_local_destroyed(&source);
    LOCAL_CHECK(rf_gpu_scene_local_freeze(&source,&source_game,&camera,1280,720,1,&frame)==0 && !frame.snapshot.actor_count);
    LOCAL_CHECK(rf_gpu_scene_extract_v1(&frame.snapshot,&scene)==0 && !scene.item_count);
    source.next_epoch=UINT64_MAX;
    LOCAL_CHECK(rf_gpu_scene_local_created(&source,&source_game.actors[slot])<0 && source.failed);
    /* Exercise the actual session event wiring, including load's memset. */
    LOCAL_CHECK(rasterfall_session_load(&owner,"rasterfall/assets/maps/rasterfall.map")==0);
    rasterfall_session_reset(&owner,&camera,1);
    LOCAL_CHECK(owner.scene_local.active && owner.scene_local.epoch);
    {
        uint64_t epoch=owner.scene_local.epoch, world=owner.scene_local.world_generation;
        rasterfall_session_reset(&owner,&camera,1);
        LOCAL_CHECK(owner.scene_local.active && owner.scene_local.epoch>epoch && owner.scene_local.world_generation>world);
        epoch=owner.scene_local.epoch;
        LOCAL_CHECK(rasterfall_session_load(&owner,"rasterfall/assets/maps/outpost.map")==0);
        rasterfall_session_reset(&owner,&camera,1);
        LOCAL_CHECK(!owner.scene_local.active);
        LOCAL_CHECK(rasterfall_session_load(&owner,"rasterfall/assets/maps/rasterfall.map")==0);
        rasterfall_session_reset(&owner,&camera,1);
        LOCAL_CHECK(owner.scene_local.epoch>epoch);
        LOCAL_CHECK(rf_gpu_scene_local_freeze(&owner.scene_local,&owner.game_state,&camera,1280,720,1,&frame)==0 && frame.snapshot.actor_count==1);
        rasterfall_session_unload(&owner);
        LOCAL_CHECK(rf_gpu_scene_local_freeze(&owner.scene_local,&owner.game_state,&camera,1280,720,1,&frame)==0 && frame.snapshot.actor_count==0);
    }
    __printf("scene local source: PASS create/destroy/replace/move/world/pose/transaction/overflow\n");
#undef LOCAL_CHECK
    return 0;
}
