/* Real Game actor constructors and updates feed the adapter. No fabricated
 * rf_gpu_scene_actor_input arrays participate in this regression. */
struct scene_enemy_test_geometry {
    uint64_t hash;
    unsigned count;
    int fail;
};
static int scene_enemy_test_triangle(void *context,
    const struct rf_gpu_scene_enemy_point *a,const struct rf_gpu_scene_enemy_point *b,
    const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    struct scene_enemy_test_geometry *g=context;
    const struct rf_gpu_scene_enemy_point *p[3]={a,b,c};
    if (g->fail) return -1;
    for (int i=0;i<3;++i) {
        g->hash=(g->hash^(unsigned)p[i]->x)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[i]->y)*1099511628211ULL;
        g->hash=(g->hash^(unsigned)p[i]->z)*1099511628211ULL;
    }
    g->hash=(g->hash^color)*1099511628211ULL;g->count++;return 0;
}
static int scene_local_source_test(void)
{
    static struct toy_game source_game, before;
    static struct rasterfall_session owner;
    static struct rf_gpu_scene_local_frame frame, saved;
    static struct rf_gpu_scene_frozen_v1 scene;
    static struct rf_gpu_scene_world_input_v2 world_on[RF_GPU_SCENE_MAX_WORLD_V2];
    static struct rf_gpu_scene_world_input_v2 world_off[RF_GPU_SCENE_MAX_WORLD_V2];
    static struct rf_gpu_scene_world_render_frame_v1 world_render;
    static struct rf_gpu_scene_world_floor_frame_v1 floor_render;
    static struct rf_gpu_scene_world_prop_frame_v1 prop_render;
    static struct rasterfall_world_lighting actor_lighting;
    static struct rasterfall_effects pickup_effects;
    static struct rf_gpu_scene_interactable_frame_v1 pickup_frame;
    struct rf_gpu_scene_local_source source = {0}, source_before;
    struct camera camera = {0};
    struct toy_game_actor *actor;
    unsigned generation;
    uint32_t world_count=0,off_count=0;
    int id, slot;
#define LOCAL_CHECK(x) do { if (!(x)) { __printf("scene local source failed line %d\n",__LINE__); return 1; } } while (0)
    camera.cy=camera.pitch_cy=1024;
    {
        static struct rf_gpu_scene_enemy_frame_v1 frozen, saved_enemy;
        struct rf_gpu_scene_enemy_item_v1 item={0};
        const int types[3]={TOY_GAME_ENEMY_SMOKER,TOY_GAME_ENEMY_CHARGER,TOY_GAME_ENEMY_TANK};
        item.dir_z=-1024;item.scene_light_q8=256;item.squash=1000;
        for (int i=0;i<3;++i) {
            struct scene_enemy_test_geometry a={0},b={0},posed={0},failure={0};
            item.type=types[i];memset(&item.pose,0,sizeof(item.pose));
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&a));
            LOCAL_CHECK(a.count>0 && a.count<=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES);
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&b));
            LOCAL_CHECK(a.hash==b.hash && a.count==b.count);
            item.pose.channel[ER_TORSO].yaw=35;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&posed));
            LOCAL_CHECK(a.hash!=posed.hash && a.count==posed.count);
            failure.fail=1;
            LOCAL_CHECK(rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&failure)<0);
        }
        item.type=TOY_GAME_ENEMY_PURSUIT_COMMON;
        for (int kind=1;kind<=2;++kind) {
            struct scene_enemy_test_geometry body={0},death={0},shadow={0},tongue={0};
            item.legacy_kind=kind;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&body));
            item.transformed=1;item.pivot_y=-300;item.roll_sin=1024;item.roll_cos=0;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&death));
            LOCAL_CHECK(body.count==death.count && body.hash!=death.hash);
            item.transparent=1;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&shadow) && !shadow.count);
            item.shadow=1;item.shadow_rx=230;item.shadow_rz=150;item.shadow_y=-886;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&shadow) && shadow.count==2);
            item.tongue=1;item.mouth_y=30;item.target_z=1500;
            LOCAL_CHECK(!rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&tongue) && tongue.count>shadow.count);
            item.transformed=0;item.transparent=0;item.shadow=0;item.tongue=0;
        }
        item.legacy_kind=0;
        struct scene_enemy_test_geometry rejected={0};
        LOCAL_CHECK(rf_gpu_scene_enemy_triangles(&item,scene_enemy_test_triangle,&rejected)<0 && !rejected.count);
        rf_gpu_scene_enemy_begin(73,4);
        LOCAL_CHECK(rf_gpu_scene_enemy_freeze(NULL)<0);
        LOCAL_CHECK(!rf_gpu_scene_enemy_freeze(&frozen));
        LOCAL_CHECK(frozen.frame_id==73 && frozen.world_generation==4 && !frozen.count);
        saved_enemy=frozen;
        LOCAL_CHECK(rf_gpu_scene_enemy_freeze(&frozen)<0 && !memcmp(&frozen,&saved_enemy,sizeof(frozen)));
        rf_gpu_scene_enemy_begin(74,5);
        LOCAL_CHECK(!rf_gpu_scene_enemy_freeze(&frozen) && frozen.frame_id==74 && !frozen.count);
    }
    {
        struct rasterfall_model_asset mesh={0};
        struct rasterfall_draw_instance instance={0};
        struct rasterfall_draw_view view={0};
        mesh.min_x=mesh.min_y=mesh.min_z=-2000;
        mesh.max_x=mesh.max_y=mesh.max_z=2000;
        instance.mesh=&mesh;instance.scale_milli=1000;
        instance.yaw_cos_q10=1024;
        view.camera=camera;view.width=1280;view.height=720;
        view.near_z=64;view.focal=960;
        /* A large near-crossing prop exceeds the old screen envelope,
         * but hardware clipping can consume its safe vertex transform. */
        LOCAL_CHECK(!rasterfall_render_scene_static_prop_eligible(&view,&instance,1));
        LOCAL_CHECK(rasterfall_render_scene_static_prop_eligible(&view,&instance,0));
        instance.z=10000;
        LOCAL_CHECK(rasterfall_render_scene_static_prop_eligible(&view,&instance,1));
        LOCAL_CHECK(rasterfall_render_scene_static_prop_eligible(&view,&instance,0));
        instance.x=3000000;
        LOCAL_CHECK(!rasterfall_render_scene_static_prop_eligible(&view,&instance,0));
        instance.x=0;mesh.max_y=1000000;instance.scale_milli=8000;
        LOCAL_CHECK(!rasterfall_render_scene_static_prop_eligible(&view,&instance,0));
        mesh.max_y=2000;instance.scale_milli=1000;mesh.bone_count=1;
        LOCAL_CHECK(!rasterfall_render_scene_static_prop_eligible(&view,&instance,0));
    }
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
    actor_lighting.minx=0;actor_lighting.maxx=1000;
    actor_lighting.minz=0;actor_lighting.maxz=1000;
    for(int i=0;i<RF_WORLD_LIGHT_W*RF_WORLD_LIGHT_H;++i) {
        actor_lighting.field[i].environment_q8=256;
        actor_lighting.field[i].sun_visibility_q8=0;
        actor_lighting.field[i].contact_q8=256;
    }
    LOCAL_CHECK(rf_gpu_scene_local_freeze_world_lit(&source,&source_game,
        &camera,1280,720,1,NULL,0,&actor_lighting,&frame)==0);
    LOCAL_CHECK(frame.presentation.scene_light_q8==192);
    LOCAL_CHECK(!memcmp(&before,&source_game,sizeof(before)));
    owner.item_count=2;
    owner.items[0].kind=TOY_MAP_PICKUP_PILL;
    owner.items[0].x=250;owner.items[0].y=400;owner.items[0].z=150;
    owner.items[1].kind=TOY_MAP_PICKUP_WEAPON;
    owner.items[1].weapon=TOY_GAME_WEAPON_AK;
    owner.items[1].x=360;
    pickup_effects.instances[2].active=1;
    pickup_effects.instances[2].kind=
        RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT;
    pickup_effects.instances[2].target_id=1;
    LOCAL_CHECK(rf_gpu_scene_interactable_freeze(&owner,&pickup_effects,
        &actor_lighting,1,1,1,&pickup_frame)==0 && pickup_frame.count==2 &&
        pickup_frame.items[0].source_slot==0 &&
        !pickup_frame.items[0].highlight_on &&
        pickup_frame.items[1].source_slot==1 &&
        pickup_frame.items[1].highlight_on &&
        pickup_frame.items[1].weapon==TOY_GAME_WEAPON_AK &&
        pickup_frame.items[0].scene_light_q8==192);
    owner.items[1].x=700;
    LOCAL_CHECK(pickup_frame.items[1].x==360 &&
        rf_gpu_scene_interactable_freeze(&owner,&pickup_effects,
            &actor_lighting,0,2,1,&pickup_frame)==0 &&
        !pickup_frame.count && pickup_frame.frame_id==2);
    owner.item_count=TOY_MAP_MAX_PICKUPS+1;
    LOCAL_CHECK(rf_gpu_scene_interactable_freeze(&owner,&pickup_effects,
        &actor_lighting,1,3,1,&pickup_frame)<0 &&
        pickup_frame.frame_id==2);
    owner.item_count=0;
    {
        struct rf_gpu_scene_projectile_frame_v1 projectiles;
        source_game.projectiles[1].active=1;
        source_game.projectiles[1].kind=TOY_GAME_WEAPON_BOMB;
        source_game.projectiles[1].x=250;
        source_game.projectiles[1].y=400;
        source_game.projectiles[1].age_ms=95;
        source_game.projectiles[1].flash_ms=100;
        source_game.projectiles[3].active=1;
        source_game.projectiles[3].kind=TOY_GAME_WEAPON_MOLOTOV;
        source_game.projectiles[3].x=500;
        LOCAL_CHECK(rf_gpu_scene_projectile_freeze(&source_game,&actor_lighting,
            1,1,&projectiles)==0 && projectiles.count==2 &&
            projectiles.items[0].source_slot==1 &&
            projectiles.items[1].source_slot==3 &&
            projectiles.items[0].x==250 &&
            projectiles.items[0].age_ms==95 &&
            projectiles.items[0].flash_ms==100 &&
            projectiles.items[0].scene_light_q8==192);
        source_game.projectiles[1].x=600;
        source_game.projectiles[1].age_ms=160;
        LOCAL_CHECK(projectiles.items[0].x==250 &&
            projectiles.items[0].age_ms==95 &&
            rf_gpu_scene_projectile_freeze(&source_game,&actor_lighting,
                2,1,&projectiles)==0 && projectiles.items[0].x==600 &&
            projectiles.items[0].age_ms==160);
        source_game.projectiles[3].kind=TOY_GAME_WEAPON_PISTOL;
        LOCAL_CHECK(rf_gpu_scene_projectile_freeze(&source_game,&actor_lighting,
            3,1,&projectiles)<0 && projectiles.frame_id==2 &&
            projectiles.items[0].x==600);
        memset(source_game.projectiles,0,sizeof(source_game.projectiles));
    }
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
    /* Two source lifetimes and lower-body clocks remain independent even
     * when registration order differs from the actor submission order. */
    {
        static struct toy_game team_game;
        struct rf_gpu_scene_local_source team_source={0}, team_before;
        int first_id,second_id;
        toy_game_init(&team_game,1);
        first_id=toy_game_add_ai(&team_game,TOY_GAME_AI_LEVEL_1,120,240,"FIRST");
        second_id=toy_game_add_ai(&team_game,TOY_GAME_AI_LEVEL_1,360,240,"SECOND");
        LOCAL_CHECK(first_id>0 && second_id>first_id);
        team_game.actors[first_id-1].character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
        team_game.actors[second_id-1].character_id=RASTERFALL_CHARACTER_SQUAD_A_MEDIC;
        toy_game_actor_set_animation(&team_game.actors[first_id-1],TOY_GAME_ANIM_MOVE);
        toy_game_actor_update_animation(&team_game.actors[first_id-1],32);
        toy_game_actor_set_animation(&team_game.actors[second_id-1],TOY_GAME_ANIM_IDLE);
        LOCAL_CHECK(!rf_gpu_scene_local_world(&team_source) &&
            !rf_gpu_scene_local_created(&team_source,&team_game.actors[second_id-1]) &&
            !rf_gpu_scene_local_created(&team_source,&team_game.actors[first_id-1]) &&
            rf_gpu_scene_local_created(&team_source,&team_game.actors[first_id-1])<0);
        LOCAL_CHECK(!rf_gpu_scene_local_freeze(&team_source,&team_game,&camera,1280,720,1,&frame) &&
            frame.snapshot.actor_count==2 &&
            frame.snapshot.actors[0].identity.source_id==(uint32_t)first_id &&
            frame.snapshot.actors[1].identity.source_id==(uint32_t)second_id &&
            frame.presentations[0].character_id==RASTERFALL_CHARACTER_RF_RIFLEMAN &&
            frame.presentations[1].character_id==RASTERFALL_CHARACTER_SQUAD_A_MEDIC &&
            frame.presentations[0].lower_time_ms==32 &&
            frame.presentations[1].lower_time_ms==0);
        toy_game_actor_update_animation(&team_game.actors[first_id-1],16);
        team_before=team_source;
        saved=frame;
        team_game.actors[second_id-1].animation.time_ms=-1;
        LOCAL_CHECK(rf_gpu_scene_local_freeze(&team_source,&team_game,&camera,1280,720,1,&frame)<0 &&
            !memcmp(&team_source,&team_before,sizeof(team_source)) &&
            !memcmp(&frame,&saved,sizeof(frame)));
        team_game.actors[second_id-1].animation.time_ms=0;
        LOCAL_CHECK(!rf_gpu_scene_local_freeze(&team_source,&team_game,&camera,1280,720,1,&frame) &&
            frame.presentations[0].lower_time_ms==48 &&
            frame.presentations[1].lower_time_ms==0);
    }
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
        {
            struct rf_gpu_scene_flag_frame_v1 flags, unchanged;
            int original_x=owner.flags[0].x;
            LOCAL_CHECK(rf_gpu_scene_flag_freeze(&owner,
                owner.scene_local.frame_id+1,owner.scene_local.world_generation,
                &flags)==0 && flags.count==5 && flags.items[0].active &&
                flags.items[0].x==original_x && !flags.items[0].selected &&
                !strcmp(flags.items[RASTERFALL_STANDARD_FLAG_INDEX].label,"RESP"));
            unchanged=flags;
            owner.flags[0].x=original_x+700;
            owner.carried_flag=0;
            LOCAL_CHECK(flags.items[0].x==original_x &&
                !strcmp(flags.items[RASTERFALL_STANDARD_FLAG_INDEX].label,"RESP") &&
                rf_gpu_scene_flag_freeze(&owner,
                    owner.scene_local.frame_id+1,owner.scene_local.world_generation,
                    &flags)==0 && flags.items[0].x==original_x+700 &&
                flags.items[0].selected);
            owner.flag_count=RF_GPU_SCENE_FLAG_CAP+1;
            LOCAL_CHECK(rf_gpu_scene_flag_freeze(&owner,
                owner.scene_local.frame_id+1,owner.scene_local.world_generation,
                &flags)<0 && flags.items[0].x==original_x+700);
            owner.flag_count=(int)unchanged.count;
            owner.flags[0].x=original_x;
            owner.carried_flag=-1;
        }
        LOCAL_CHECK(rf_gpu_scene_local_freeze(&owner.scene_local,&owner.game_state,&camera,1280,720,1,&frame)==0 && frame.snapshot.actor_count==8);
        LOCAL_CHECK(rf_gpu_scene_world_capture(&owner.map_ops,1,world_on,
            RF_GPU_SCENE_MAX_WORLD_V2,&world_count)==0 && world_count>0);
        LOCAL_CHECK(rf_gpu_scene_local_freeze_world(&owner.scene_local,&owner.game_state,
            &camera,1280,720,1,world_on,world_count,&frame)==0);
        LOCAL_CHECK(frame.snapshot.world_count==world_count &&
            rf_gpu_scene_extract_v1(&frame.snapshot,&scene)==0 &&
            scene.item_count==world_count+8);
        saved=frame;
        frame.snapshot.world[0].submission_ordinal=1000;
        LOCAL_CHECK(rf_gpu_scene_extract_v1(&frame.snapshot,&scene)==0 &&
            scene.items[scene.item_count-1].source_kind==RF_GPU_SCENE_ITEM_WORLD &&
            scene.items[scene.item_count-1].source_index==0);
        frame=saved;
        world_on[1].submission_ordinal=world_on[0].submission_ordinal;
        LOCAL_CHECK(rf_gpu_scene_local_freeze_world(&owner.scene_local,&owner.game_state,
            &camera,1280,720,1,world_on,world_count,&frame)<0);
        world_on[1].submission_ordinal=1;
        for(uint32_t i=0;i<world_count;++i) {
            const struct rf_map_runtime_render *render=
                rasterfall_map_render_projection_at(&owner.map_ops,(int)i);
            LOCAL_CHECK(render && !strcmp(world_on[i].id,render->id) &&
                world_on[i].submission_ordinal==i);
        }
        LOCAL_CHECK(rf_gpu_scene_world_prop_freeze(&owner.map_ops,NULL,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            &prop_render)==0 && prop_render.count==134);
        {
            struct rasterfall_model_asset *boundary=NULL;
            uint32_t accepted=0;
            LOCAL_CHECK(rf_gpu_scene_world_boundary_mesh_build(&prop_render,
                &boundary,&accepted)==0 && accepted==21 && boundary &&
                boundary->primitive_count>0);
            rasterfall_model_unload(boundary);tlibc_free(boundary);
        }
        LOCAL_CHECK(rf_gpu_scene_world_render_freeze(&owner.map_ops,1,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            world_on,RF_GPU_SCENE_MAX_WORLD_V2,&world_count,&world_render)==0);
        LOCAL_CHECK(rf_gpu_scene_world_floor_freeze(&owner.map_ops,0,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            &floor_render)==0);
        LOCAL_CHECK(rf_gpu_scene_local_freeze_world(&owner.scene_local,&owner.game_state,
            &camera,1280,720,1,world_on,world_count,&frame)==0 &&
            rf_gpu_scene_world_render_validate(&frame.snapshot,&world_render)==0);
        {
            struct rasterfall_model_asset *models[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT]={0};
            uint32_t opaque=0,deferred=0,transparent=0,prop_accepted=0;
            LOCAL_CHECK(rf_gpu_scene_world_opaque_mesh_build(&frame.snapshot,&world_render,
                &floor_render,&prop_render,models,&opaque,&deferred,
                &transparent,&prop_accepted)==0);
            LOCAL_CHECK(opaque==60 && deferred==15 && transparent==4 &&
                prop_accepted==21 && models[RF_GPU_SCENE_WORLD_MODEL_BOX] &&
                models[RF_GPU_SCENE_WORLD_MODEL_BOX]->primitive_count>0);
            LOCAL_CHECK(models[RF_GPU_SCENE_WORLD_MODEL_LEGACY] &&
                models[RF_GPU_SCENE_WORLD_MODEL_LEGACY]->vertex_count>210 &&
                models[RF_GPU_SCENE_WORLD_MODEL_LEGACY]->vertex_count%6==0);
            LOCAL_CHECK(models[RF_GPU_SCENE_WORLD_MODEL_SPECIAL] &&
                models[RF_GPU_SCENE_WORLD_MODEL_SPECIAL]->vertex_count>300 &&
                models[RF_GPU_SCENE_WORLD_MODEL_SPECIAL]->vertex_count%6==0);
            LOCAL_CHECK(models[RF_GPU_SCENE_WORLD_MODEL_INFECTED] &&
                models[RF_GPU_SCENE_WORLD_MODEL_INFECTED]->vertex_count>10000 &&
                models[RF_GPU_SCENE_WORLD_MODEL_INFECTED]->vertex_count%6==0);
            LOCAL_CHECK(models[RF_GPU_SCENE_WORLD_SIGN] &&
                models[RF_GPU_SCENE_WORLD_SIGN]->vertex_count>60 &&
                models[RF_GPU_SCENE_WORLD_SIGN]->vertex_count%6==0);
            {
                const struct rasterfall_model_asset *box=models[RF_GPU_SCENE_WORLD_MODEL_BOX];
                const struct toy_map_draw *first=NULL;
                const unsigned char *vertices=box->vertices;
                int expected;
                for(uint32_t i=0;i<world_render.count;++i)
                    if (world_render.items[i].draw.type==TOY_MAP_DRAW_MODEL &&
                        !world_render.items[i].draw.style) {
                        first=&world_render.items[i].draw;break;
                    }
                LOCAL_CHECK(first && box->vertex_count>=6 &&
                    box->vertex_count%6==0);
                expected=rasterfall_diagnostic_world_light_q8(
                    rasterfall_diagnostic_world_light_at_v1(&floor_render.model_light_v1,
                        (first->a+first->b+first->b)/3,
                        (first->e+first->e+first->f)/3,first->c));
                for(uint32_t k=0;k<box->vertex_count;k+=6) {
                    int light0=vertices[(k+0)*box->vertex_bytes+18] |
                        vertices[(k+0)*box->vertex_bytes+19]<<8;
                    int light3=vertices[(k+3)*box->vertex_bytes+18] |
                        vertices[(k+3)*box->vertex_bytes+19]<<8;
                    if (!k) LOCAL_CHECK(light0==expected);
                    for(uint32_t j=1;j<3;++j)
                        LOCAL_CHECK((vertices[(k+j)*box->vertex_bytes+18] |
                            vertices[(k+j)*box->vertex_bytes+19]<<8)==light0);
                    for(uint32_t j=4;j<6;++j)
                        LOCAL_CHECK((vertices[(k+j)*box->vertex_bytes+18] |
                            vertices[(k+j)*box->vertex_bytes+19]<<8)==light3);
                }
            }
            for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind)
                if (models[kind]) {
                    rasterfall_model_unload(models[kind]);tlibc_free(models[kind]);
                }
        }
        LOCAL_CHECK(rasterfall_session_load(&owner,
            "rasterfall/assets/maps/gpu_scene_render_fixture.map")==0);
        rasterfall_session_reset(&owner,&camera,1);
        LOCAL_CHECK(rf_gpu_scene_world_render_freeze(&owner.map_ops,1,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            world_on,RF_GPU_SCENE_MAX_WORLD_V2,&world_count,&world_render)==0);
        LOCAL_CHECK(rf_gpu_scene_world_floor_freeze(&owner.map_ops,0,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            &floor_render)==0);
        LOCAL_CHECK(rf_gpu_scene_world_prop_freeze(&owner.map_ops,NULL,
            owner.scene_local.frame_id+1,owner.scene_local.world_generation,
            &prop_render)==0 && !prop_render.count);
        LOCAL_CHECK(rf_gpu_scene_local_freeze_world(&owner.scene_local,&owner.game_state,
            &camera,1280,720,1,world_on,world_count,&frame)==0 &&
            rf_gpu_scene_world_render_validate(&frame.snapshot,&world_render)==0);
        LOCAL_CHECK(rf_gpu_scene_world_capture(&owner.map_ops,0,world_off,
            RF_GPU_SCENE_MAX_WORLD_V2,&off_count)==0 && off_count==world_count);
        int gate_box=0,gate_platform=0,gate_box_index=-1;
        for(uint32_t i=0;i<world_count;++i) {
            LOCAL_CHECK(!strcmp(world_on[i].id,world_off[i].id) &&
                world_on[i].submission_ordinal==world_off[i].submission_ordinal);
            if (!strcmp(world_on[i].id,"air_gate_box")) {
                gate_box=1;gate_box_index=(int)i;
                LOCAL_CHECK(world_on[i].visible && !world_off[i].visible &&
                    world_on[i].alpha==48);
            }
            if (!strcmp(world_on[i].id,"air_gate_platform")) {
                gate_platform=1;LOCAL_CHECK(world_on[i].visible && !world_off[i].visible &&
                    world_on[i].alpha==96);
            }
        }
        LOCAL_CHECK(gate_box && gate_platform);
        {
            unsigned int saved_color=owner.map_ops.level->draw[gate_box_index].color;
            owner.map_ops.level->draw[gate_box_index].color^=0xffffffu;
            LOCAL_CHECK(world_render.items[gate_box_index].draw.color==saved_color &&
                rf_gpu_scene_world_render_validate(&frame.snapshot,&world_render)==0);
            owner.map_ops.level->draw[gate_box_index].color=saved_color;
            world_render.items[gate_box_index].submission_ordinal++;
            LOCAL_CHECK(rf_gpu_scene_world_render_validate(&frame.snapshot,&world_render)<0);
            world_render.items[gate_box_index].submission_ordinal--;
        }
        {
            struct rasterfall_model_asset *models[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT]={0};
            struct rasterfall_model_asset *frozen_floor=NULL;
            uint32_t opaque=0,deferred=0,transparent=0,prop_accepted=0;
            LOCAL_CHECK(rf_gpu_scene_world_opaque_mesh_build(&frame.snapshot,&world_render,
                &floor_render,&prop_render,models,&opaque,&deferred,
                &transparent,&prop_accepted)==0);
            LOCAL_CHECK(opaque==8 && deferred==1 && transparent==2 && !prop_accepted &&
                models[RF_GPU_SCENE_WORLD_SIGN] &&
                models[RF_GPU_SCENE_WORLD_SIGN]->vertex_count>60);
            {
                unsigned int saved=owner.map_ops.level->draw[0].color;
                owner.map_ops.level->draw[0].color^=0xffffffu;
                LOCAL_CHECK(rf_gpu_scene_world_floor_mesh_build(&world_render,
                    &floor_render,&frozen_floor)==0 && frozen_floor &&
                    frozen_floor->data_size==models[RF_GPU_SCENE_WORLD_FLOOR]->data_size &&
                    !memcmp(frozen_floor->data,models[RF_GPU_SCENE_WORLD_FLOOR]->data,
                        frozen_floor->data_size));
                owner.map_ops.level->draw[0].color=saved;
                rasterfall_model_unload(frozen_floor);
                tlibc_free(frozen_floor);
            }
            for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_BOUNDARY;++kind) {
                LOCAL_CHECK(models[kind] && models[kind]->primitive_count>0);
                rasterfall_model_unload(models[kind]);
                tlibc_free(models[kind]);
            }
        }
        {
            static struct rf_gpu_scene_world_resources resources;
            struct rasterfall_resource_handle retired[RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT];
            struct rasterfall_resource_stats stats;
            LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                &frame.snapshot,&world_render,&floor_render,&prop_render,1)==0);
            LOCAL_CHECK(resources.accepted==8 && resources.deferred==1 &&
                resources.transparent==2);
            frame.snapshot.frame_id++;
            world_render.frame_id++;
            floor_render.frame_id++;
            prop_render.frame_id++;
            LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                &frame.snapshot,&world_render,&floor_render,&prop_render,1)==0);
            rasterfall_resources_stats(&resources.registry,&stats);
            LOCAL_CHECK(stats.live==6 && stats.loads==6);
            {
                struct rasterfall_resource_handle old=resources.opaque[RF_GPU_SCENE_WORLD_WALL];
                LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                    &frame.snapshot,&world_render,&floor_render,&prop_render,2)==0);
                LOCAL_CHECK(resources.light_generation==2 &&
                    resources.opaque[RF_GPU_SCENE_WORLD_WALL].generation!=old.generation);
                rasterfall_resources_stats(&resources.registry,&stats);
                LOCAL_CHECK(stats.live==6 && stats.loads==12 && stats.releases==6);
            }
            {
                struct rasterfall_resource_handle old=resources.opaque[RF_GPU_SCENE_WORLD_FLOOR];
                world_render.items[0].draw.color^=0xffffffu;
                LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                    &frame.snapshot,&world_render,&floor_render,&prop_render,2)==0);
                LOCAL_CHECK(resources.opaque[RF_GPU_SCENE_WORLD_FLOOR].generation!=old.generation);
                rasterfall_resources_stats(&resources.registry,&stats);
                LOCAL_CHECK(stats.live==6 && stats.loads==18 && stats.releases==12);
            }
            frame.snapshot.world[gate_box_index].visible=0;
            world_render.items[gate_box_index].visible=0;
            LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                &frame.snapshot,&world_render,&floor_render,&prop_render,2)==0 && resources.transparent==1);
            frame.snapshot.world[gate_box_index].visible=1;
            world_render.items[gate_box_index].visible=1;
            LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                &frame.snapshot,&world_render,&floor_render,&prop_render,2)==0 && resources.transparent==2);
            memcpy(retired,resources.opaque,sizeof(retired));
            rasterfall_resources_stats(&resources.registry,&stats);
            LOCAL_CHECK(stats.live==6 && stats.loads==30 && stats.releases==24);
            LOCAL_CHECK(rasterfall_resources_frame_begin(&resources.registry)==0);
            for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind)
                if (retired[kind].generation)
                    LOCAL_CHECK(rasterfall_resources_pin(&resources.registry,retired[kind])==0);
            rasterfall_resources_frame_submitted(&resources.registry);
            frame.snapshot.world_generation++;frame.snapshot.map_generation++;
            world_render.world_generation++;world_render.map_generation++;
            floor_render.world_generation++;floor_render.map_generation++;
            prop_render.world_generation++;prop_render.map_generation++;
            LOCAL_CHECK(rf_gpu_scene_world_resources_prepare(&resources,
                &frame.snapshot,&world_render,&floor_render,&prop_render,2)==0);
            for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind)
                if (retired[kind].generation)
                    LOCAL_CHECK(rasterfall_resources_resolve(&resources.registry,retired[kind]) &&
                        !rasterfall_resources_resolve_active(&resources.registry,retired[kind]) &&
                        rasterfall_resources_resolve_active(&resources.registry,resources.opaque[kind]));
            rasterfall_resources_stats(&resources.registry,&stats);
            LOCAL_CHECK(stats.live==6 && stats.retired==6 && stats.pinned==6);
            rasterfall_resources_frame_complete(&resources.registry);
            rasterfall_resources_stats(&resources.registry,&stats);
            LOCAL_CHECK(stats.live==6 && !stats.retired && !stats.pinned);
            rf_gpu_scene_world_resources_invalidate(&resources);
            rasterfall_resources_stats(&resources.registry,&stats);
            LOCAL_CHECK(!stats.live && !stats.retired && stats.releases==36);
        }
        rasterfall_session_unload(&owner);
        LOCAL_CHECK(rf_gpu_scene_local_freeze(&owner.scene_local,&owner.game_state,&camera,1280,720,1,&frame)==0 && frame.snapshot.actor_count==0);
    }
    __printf("scene local source: PASS create/destroy/replace/move/world/pose/transaction/overflow/runtime-map\n");
#undef LOCAL_CHECK
    return 0;
}
