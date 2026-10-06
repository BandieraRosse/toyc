/* Independent actor presentation source. Geometry extraction consumes the
 * captured values later; this adapter never enters a draw producer. */
static struct rf_gpu_scene_local_source scene_actor_history;
static const struct rasterfall_session *scene_actor_session;
static struct rasterfall_rifle_history scene_procedural_rifle[TOY_GAME_MAX_ACTORS];
static struct toy_game scene_actor_fixture_game;

struct scene_actor_fixture_clock {
    int actor_id;
    uint64_t time_ms;
    int rifle_valid;
    struct rasterfall_rifle_pose_input rifle;
};

static void scene_actor_freeze_fixture_clocks(struct rf_gpu_scene_local_frame *frame,
    const struct scene_actor_fixture_clock clocks[TOY_GAME_MAX_ACTORS])
{
    /* Only slots inserted into this presentation copy own an authored clock.
     * A gameplay actor with a similar numeric ID must retain its own history. */
    for (unsigned i=0;i<frame->snapshot.actor_count;++i) {
        const struct rf_gpu_scene_actor_v1 *actor=&frame->snapshot.actors[i];
        unsigned slot=actor->source_slot;
        if (slot<TOY_GAME_MAX_ACTORS && clocks[slot].actor_id>0 &&
            actor->identity.source==RF_GPU_SCENE_ACTOR_GAME &&
            actor->identity.source_id==(unsigned)clocks[slot].actor_id) {
            frame->presentations[i].lower_time_ms=clocks[slot].time_ms;
            if(clocks[slot].rifle_valid)frame->presentations[i].rifle=clocks[slot].rifle;
        }
    }
    if (frame->snapshot.actor_count) frame->presentation=frame->presentations[0];
}

static void scene_actor_restore_fixture_history(struct rf_gpu_scene_local_source *next,
    int actor_id)
{
    if (scene_actor_session!=active_session ||
        scene_actor_history.world_generation!=next->world_generation ||
        scene_actor_history.frame_id>=scene_enemy_capture.frame_id) return;
    for (unsigned j=0;j<scene_actor_history.actor_count;++j)
        if (scene_actor_history.actors[j].actor_id==actor_id) {
            struct rf_gpu_scene_local_actor_source *entry=
                &next->actors[next->actor_count-1];
            uint64_t epoch=entry->epoch;
            *entry=scene_actor_history.actors[j];
            entry->epoch=epoch;
            return;
        }
}

static int scene_actor_registered(int actor_id)
{
    if (!active_session || (active_net && active_net->mode==RASTERFALL_NET_CLIENT))
        return 0;
    for (unsigned i=0;i<active_session->scene_local.actor_count;++i)
        if (active_session->scene_local.actors[i].actor_id==actor_id) return 1;
    return 0;
}

static int scene_actor_procedural(const struct toy_game_actor *actor,
    int x,int z,int sy,int cy,int lift,unsigned tint,int ordinary_ai)
{
    const struct rasterfall_character_profile *profile=
        rasterfall_character_profile(actor->character_id);
    struct rf_gpu_scene_procedural_item_v1 *item;
    if (scene_enemy_capture.procedural_count>=TOY_GAME_MAX_ACTORS) return -1;
    item=&scene_enemy_capture.procedural[scene_enemy_capture.procedural_count++];
    memset(item,0,sizeof(*item));
    item->state.x=x;item->state.z=z;item->state.lift=lift;
    item->state.sy=sy;item->state.cy=cy;
    item->state.weapon=actor->current_slot>=0 && actor->current_slot<TOY_GAME_WEAPON_SLOTS ?
        actor->slots[actor->current_slot].weapon : -1;
    item->state.downed=actor->state==TOY_GAME_ACTOR_DOWNED;
    item->state.animation_id=actor->animation.id;
    item->state.animation_time_ms=actor->animation.time_ms;
    item->state.moving=actor->moving;
    item->state.locomotion_time_ms=(int)((unsigned long long)game.combat_time_ms%60000);
    item->state.profession_id=actor->character_id<0 ? RASTERFALL_PROFESSION_NONE : profile->profession_id;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)if(actor==&game.actors[i] && actor->kind==TOY_GAME_ACTOR_AI) {
        rasterfall_rifle_sample(actor,(unsigned)game.combat_time_ms,&scene_procedural_rifle[i],&item->state.rifle);
        item->state.rifle_valid=item->state.weapon>=TOY_GAME_WEAPON_PISTOL && item->state.weapon<=TOY_GAME_WEAPON_AWP &&
            actor->state==TOY_GAME_ACTOR_ALIVE && !actor->reloading && !actor->weapon_switch_timer_ms &&
            !actor->control_disabled && !actor->melee_timer_ms && !actor->throw_timer_ms &&
            !actor->airborne_ms && !actor->airborne_y;
        if(item->state.rifle_valid)item->state.muzzle_flash=actor->muzzle_flash_ms;
    }
    item->body_color=actor->character_id<0 ? tint : profile->body_color;
    item->leg_color=actor->character_id<0 && ordinary_ai ? 0x25354A : profile->leg_color;
    item->skin_color=profile->skin_color;item->hair_color=profile->hair_color;
    item->scene_light_q8=256;
    item->vertex_lighting=0;
    item->double_sided=1;
    return 0;
}

int rf_gpu_scene_actor_collect_independent(const struct camera *camera,
    unsigned width,unsigned height)
{
    struct rf_gpu_scene_local_source next={0};
    struct rf_gpu_scene_local_frame frame;
    struct scene_actor_fixture_clock fixture_clocks[TOY_GAME_MAX_ACTORS]={0};
    struct outpost_rifle_cycle_sample cycle={0};
    int cycle_slot=-1;
    const struct rasterfall_net *net=active_net;
    static const unsigned colors[RASTERFALL_NET_PLAYER_MAX]={
        RF_COLOR_AI_RIFLE,RF_COLOR_UI_PLAYER,0xD59BFF,0x66D9D9};
    if (!camera || !width || !height || !active_session ||
        !scene_enemy_capture_active || scene_enemy_capture.failed ||
        scene_enemy_capture.procedural_count || scene_enemy_capture.modular_count ||
        scene_enemy_capture.frame_id>UINT_MAX) return -1;
    next.world_generation=scene_enemy_capture.world_generation;
    next.frame_id=scene_enemy_capture.frame_id-1;
    if(scene_actor_session!=active_session || scene_actor_history.world_generation!=next.world_generation)
        memset(scene_procedural_rifle,0,sizeof(scene_procedural_rifle));
    if (active_session->rts_active || scene_enemy_aux_active[0] || scene_enemy_aux_active[1]) {
        const struct toy_game_actor *player =
            toy_game_local_player_actor_const(&game);
        if (player && player->active &&
            scene_actor_procedural(player, player->x, player->z,
                player->sy, player->cy,
                player->ground_y + player->airborne_y,
                RF_COLOR_UI_PLAYER, 0) < 0) goto fail;
        if(player && player->active && !active_session->rts_active)
            scene_enemy_capture.procedural[scene_enemy_capture.procedural_count-1].auxiliary_only=1;
    }
    for (int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *actor=&game.actors[i];
        if (!actor->active || actor->kind!=TOY_GAME_ACTOR_AI) continue;
        if (actor->state!=TOY_GAME_ACTOR_DOWNED &&
            rasterfall_character_visual_recipe_for_character(actor->character_id)) {
            if (scene_actor_registered(actor->actor_id)) continue;
            if (rf_gpu_scene_local_created(&next,actor)<0) goto fail;
            /* Preserve presentation time, not lifetime identity. Supplemental
             * payloads below deliberately retain frame-local identities. */
            if (scene_actor_session==active_session &&
                scene_actor_history.world_generation==next.world_generation &&
                scene_actor_history.frame_id<scene_enemy_capture.frame_id) {
                for (unsigned j=0;j<scene_actor_history.actor_count;++j) {
                    const struct rf_gpu_scene_local_actor_source *old=&scene_actor_history.actors[j];
                    if (old->actor_id==actor->actor_id) {
                        struct rf_gpu_scene_local_actor_source *entry=&next.actors[next.actor_count-1];
                        uint64_t epoch=entry->epoch;
                        *entry=*old;entry->epoch=epoch;
                        break;
                    }
                }
            }
        } else {
            unsigned tint=actor->class_id==TOY_GAME_AI_LEVEL_3 ? RF_COLOR_AI_HEAVY :
                actor->class_id==TOY_GAME_AI_LEVEL_2 ? RF_COLOR_AI_RIFLE : RF_COLOR_AI_BASIC;
            if (scene_actor_procedural(actor,actor->x,actor->z,actor->sy,actor->cy,
                    actor->ground_y+actor->airborne_y,tint,1)<0) goto fail;
        }
    }
    /* The fixtures occupy free slots in a presentation-only copy.  This
     * preserves the normal local pose extractor without changing game truth. */
    const struct toy_game *pose_game=&game;
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST &&
        (outpost_actor_actions_visible || outpost_actor_walk_visible || outpost_rifle_cycle_visible)) {
        memcpy(&scene_actor_fixture_game,&game,sizeof(game));
        pose_game=&scene_actor_fixture_game;
        for (int family=0;family<2;++family) {
            if (!outpost_actor_actions_visible) break;
            for (int i=0;i<OUTPOST_ACTOR_ACTION_COUNT;++i) {
                struct toy_game_actor actor;
                int x=-4284+(i%6)*1650;
                int z=3156-(i/6)*2100-family*4200;
                if (family && i>=(int)OUTPOST_HUMANOID_STATION_COUNT) continue;
                outpost_actor_fixture(&actor,family,1,family ?
                    outpost_humanoid_stations[i].action : outpost_actor_actions[i],
                    x,z,1,outpost_actor_actions_time_us,family*32+i);
                if (family) {
                    int slot=TOY_GAME_MAX_ACTORS-1;
                    while (slot>=0 && scene_actor_fixture_game.actors[slot].active)
                        --slot;
                    if (slot<0) goto fail;
                    scene_actor_fixture_game.actors[slot]=actor;
                    fixture_clocks[slot].actor_id=actor.actor_id;
                    fixture_clocks[slot].time_ms=outpost_actor_locomotion_time(actor.actor_id);
                    fixture_clocks[slot].rifle_valid=1;
                    fixture_clocks[slot].rifle=*outpost_actor_rifle_input(&actor);
                    if (rf_gpu_scene_local_created(&next,&actor)<0) goto fail;
                    scene_actor_restore_fixture_history(&next,actor.actor_id);
                } else {
                    if (scene_actor_procedural(&actor,actor.x,actor.z,actor.sy,actor.cy,0,
                            RF_COLOR_AI_RIFLE,1)<0) goto fail;
                    outpost_actor_block_rifle(&actor,&scene_enemy_capture.procedural[
                        scene_enemy_capture.procedural_count-1].state);
                    if (actor.animation.id==TOY_GAME_ANIM_DOWNED)
                        scene_enemy_capture.procedural[
                            scene_enemy_capture.procedural_count-1].state.downed=1;
                }
            }
        }
        if (outpost_actor_walk_visible)
            for (int i=0;i<OUTPOST_ACTOR_WALK_COUNT;++i) {
                const struct outpost_actor_walker *entry=&outpost_actor_walkers[i];
                struct toy_game_actor actor;
                outpost_actor_fixture(&actor,entry->family,entry->level,
                    TOY_GAME_ANIM_MOVE,entry->x,entry->z,entry->direction,
                    outpost_actor_walk_step_us,64+i);
                if (entry->family) {
                    int slot=TOY_GAME_MAX_ACTORS-1;
                    while (slot>=0 && scene_actor_fixture_game.actors[slot].active)
                        --slot;
                    if (slot<0) goto fail;
                    scene_actor_fixture_game.actors[slot]=actor;
                    fixture_clocks[slot].actor_id=actor.actor_id;
                    fixture_clocks[slot].time_ms=outpost_actor_locomotion_time(actor.actor_id);
                    fixture_clocks[slot].rifle_valid=1;
                    fixture_clocks[slot].rifle=*outpost_actor_rifle_input(&actor);
                    if (rf_gpu_scene_local_created(&next,&actor)<0) goto fail;
                    scene_actor_restore_fixture_history(&next,actor.actor_id);
                } else {
                    unsigned tint=entry->level==2 ? RF_COLOR_AI_HEAVY :
                        entry->level==1 ? RF_COLOR_AI_RIFLE : RF_COLOR_AI_BASIC;
                    if (scene_actor_procedural(&actor,actor.x,actor.z,
                            actor.sy,actor.cy,0,tint,1)<0) goto fail;
                    outpost_actor_block_rifle(&actor,&scene_enemy_capture.procedural[
                        scene_enemy_capture.procedural_count-1].state);
                }
            }
    }
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST && outpost_rifle_cycle_visible) {
        struct toy_game_actor actor={0};
        outpost_rifle_cycle_sample_at(outpost_rifle_cycle_time_us/1000,&cycle);
        actor.active=1;actor.actor_id=10100;actor.kind=TOY_GAME_ACTOR_AI;
        actor.state=TOY_GAME_ACTOR_ALIVE;actor.character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
        actor.cy=cycle.cy;actor.sy=cycle.sy;actor.moving=cycle.body.walk;
        actor.current_slot=0;actor.slots[0].weapon=TOY_GAME_WEAPON_AK;
        actor.animation.id=cycle.body.fire?TOY_GAME_ANIM_FIRE:
            cycle.body.walk?TOY_GAME_ANIM_MOVE:TOY_GAME_ANIM_IDLE;
        actor.animation.time_ms=cycle.body.action_time_ms;
        actor.combat_target.kind=-1;actor.pitch_cy=1024;
        rasterfall_render_lab_point("rifle_cycle_lab_area",0,cycle.z,&actor.x,&actor.z);
        cycle_slot=TOY_GAME_MAX_ACTORS-1;
        while(cycle_slot>=0 && scene_actor_fixture_game.actors[cycle_slot].active)--cycle_slot;
        if(cycle_slot<0 || rf_gpu_scene_local_created(&next,&actor)<0)goto fail;
        scene_actor_fixture_game.actors[cycle_slot]=actor;
        fixture_clocks[cycle_slot].actor_id=actor.actor_id;
        fixture_clocks[cycle_slot].time_ms=cycle.body.lower_time_ms;
        scene_actor_restore_fixture_history(&next,actor.actor_id);
        actor.character_id=RASTERFALL_CHARACTER_NONE;
        rasterfall_render_lab_point("rifle_cycle_lab_area",-3000,cycle.z,&actor.x,&actor.z);
        if(scene_actor_procedural(&actor,actor.x,actor.z,actor.sy,actor.cy,0,RF_COLOR_AI_RIFLE,1)<0)goto fail;
        struct rasterfall_procedural_humanoid_state *block=
            &scene_enemy_capture.procedural[scene_enemy_capture.procedural_count-1].state;
        block->rifle_valid=1;block->rifle=cycle.body.rifle;
        block->moving=cycle.body.walk;
        block->locomotion_time_ms=(int)(cycle.body.lower_time_ms%60000);
        block->muzzle_flash=cycle.body.fire && cycle.body.action_time_ms<65;
    }
    if (rf_gpu_scene_local_freeze_presentation(&next,pose_game,camera,width,height,
            0,NULL,0,&frame)<0) goto fail;
    scene_actor_freeze_fixture_clocks(&frame,fixture_clocks);
    if(cycle_slot>=0)for(unsigned i=0;i<frame.snapshot.actor_count;++i)
        if(frame.snapshot.actors[i].source_slot==(unsigned)cycle_slot) {
            frame.presentations[i].lower_walk=cycle.body.walk;
            frame.presentations[i].rifle=cycle.body.rifle;
        }
    if(frame.snapshot.actor_count)frame.presentation=frame.presentations[0];
    uint32_t extracted=0,begin=scene_enemy_capture.modular_count;
    if(rf_gpu_scene_pose_extract_all(&frame,scene_enemy_capture.modular+begin,
            TOY_GAME_MAX_ACTORS-begin,&extracted)<0)goto fail;
    unsigned ordinal=0;
    for (unsigned i=0;i<frame.snapshot.actor_count;++i) {
        if(frame.presentations[i].state==TOY_GAME_ACTOR_DOWNED)continue;
        struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[begin+ordinal++];
        if(cycle_slot>=0 && frame.snapshot.actors[i].source_slot==(unsigned)cycle_slot)
            pose->muzzle_flash=cycle.body.fire && cycle.body.action_time_ms<65;
        pose->identity.source_id=0x80000000u+i;
        pose->identity.generation=(unsigned)scene_enemy_capture.frame_id;
    }
    scene_enemy_capture.modular_count+=extracted;
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST && outpost_model_lab_visible &&
        outpost_model_lab_status>=0) {
        unsigned begin=scene_enemy_capture.modular_count;
        static const int actions[]={RF_GPU_SCENE_BODY_IDLE,RF_GPU_SCENE_BODY_WALK,
            RF_GPU_SCENE_BODY_RIFLE_IDLE,RF_GPU_SCENE_BODY_RIFLE_AIM,
            RF_GPU_SCENE_BODY_WALK_FIRE,RF_GPU_SCENE_BODY_WALK_RIFLE};
        for (unsigned i=0;i<sizeof(actions)/sizeof(actions[0]);++i) {
            if (scene_enemy_capture.modular_count>=TOY_GAME_MAX_ACTORS) goto fail;
            struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[scene_enemy_capture.modular_count];
            int x,z;
            rasterfall_render_lab_point("rf_model_lab_area",-384+(int)(i%2)*1200,344-(int)(i/2)*1500,&x,&z);
            if (rf_gpu_scene_pose_body_action(RASTERFALL_BODY_RF_C01_V22H,
                    scene_enemy_capture.frame_id,scene_enemy_capture.world_generation,
                    outpost_model_lab_time_us/1000,actions[i],x,-900,z,1024,
                    actions[i]==RF_GPU_SCENE_BODY_RIFLE_IDLE?&outpost_model_rifle:NULL,pose)<0) {
                __printf("RF-MODEL-LAB resource unavailable or incompatible; body=%d\n",RASTERFALL_BODY_RF_C01_V22H);
                scene_enemy_capture.modular_count=begin;
                outpost_model_lab_status=-1;
                break;
            }
            pose->identity.source_id=0x90000000u+i;
            pose->identity.generation=1;
            scene_enemy_capture.modular_count++;
        }
        if (outpost_model_lab_status>=0) outpost_model_lab_status=1;
    }
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST && outpost_rifle_cycle_visible &&
        outpost_rifle_cycle_status>=0) {
        int x,z;
        if(scene_enemy_capture.modular_count>=TOY_GAME_MAX_ACTORS)goto fail;
        struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[scene_enemy_capture.modular_count];
        rasterfall_render_lab_point("rifle_cycle_lab_area",3000,cycle.z,&x,&z);
        if(rf_gpu_scene_pose_body_sample(RASTERFALL_BODY_RF_C01_V22H,
            scene_enemy_capture.frame_id,scene_enemy_capture.world_generation,
            &cycle.body,x,-900,z,cycle.sy,cycle.cy,pose)<0) {
            outpost_rifle_cycle_status=-1;
            __printf("RIFLE-CYCLE RF-C01 resource unavailable; toggle exhibit to retry\n");
        } else {
            pose->identity.source_id=0x91000000u;pose->identity.generation=1;
            scene_enemy_capture.modular_count++;
            outpost_rifle_cycle_status=1;
        }
    }
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST && outpost_weapon_cycle_visible) {
        static const int bodies[]={RASTERFALL_BODY_RF_BLOCK,RASTERFALL_BODY_RF_HUMANOID_V2,RASTERFALL_BODY_RF_C01_V22H};
        for(int row=0;row<3;++row)for(int w=0;w<5;++w) {
            struct outpost_rifle_cycle_sample sample;
            int x,z;
            if(row==2 && outpost_weapon_cycle_status<0)continue;
            if(scene_enemy_capture.modular_count>=TOY_GAME_MAX_ACTORS)goto fail;
            outpost_weapon_cycle_sample_at(outpost_weapon_cycle_time_us/1000,w,&sample);
            rasterfall_render_lab_point("weapon_cycle_lab_area",-5200+w*2600,2600-row*2600+sample.z,&x,&z);
            struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[scene_enemy_capture.modular_count];
            if(rf_gpu_scene_pose_body_sample(bodies[row],scene_enemy_capture.frame_id,
                scene_enemy_capture.world_generation,&sample.body,x,-900,z,sample.sy,sample.cy,pose)<0) {
                if(row!=2)goto fail;
                outpost_weapon_cycle_status=-1;
                __printf("WEAPON-CYCLE RF-C01 unavailable; toggle exhibit to retry\n");
                continue;
            }
            pose->identity.source_id=0x92000000u+row*5+w;pose->identity.generation=1;
            scene_enemy_capture.modular_count++;
        }
        if(outpost_weapon_cycle_status>=0)outpost_weapon_cycle_status=1;
    }
    if (net && net->mode==RASTERFALL_NET_CLIENT) {
        for (int i=0;i<RASTERFALL_NET_PLAYER_MAX;++i) {
            int slot=rasterfall_net_player_actor_index(net,i),airborne=0;
            const struct toy_game_actor *actor;
            const struct camera *body;
            if (i==net->local_player_id || slot<0 || slot>=TOY_GAME_MAX_ACTORS) continue;
            actor=&game.actors[slot];
            if (!actor->active || actor->kind!=TOY_GAME_ACTOR_PLAYER) continue;
            body=actor->state==TOY_GAME_ACTOR_DOWNED ? &net->remote_render_camera[i] :
                rasterfall_net_remote_render_camera(net,i,&airborne);
            if (scene_actor_procedural(actor,body->x,body->z,body->sy,body->cy,
                    network_actor_lift(body->x,body->z,airborne),colors[i],0)<0) goto fail;
        }
    } else if (net && net->mode==RASTERFALL_NET_HOST) {
        for (int i=0;i<RASTERFALL_NET_CLIENT_MAX;++i) {
            const struct rasterfall_net_client *client=&net->clients[i];
            const struct toy_game_actor *actor;
            int slot=TOY_GAME_REMOTE_ACTOR_BASE+client->client_id-1;
            if (!client->active || !client->connected || client->client_id<0 ||
                client->client_id>=RASTERFALL_NET_PLAYER_MAX || slot<0 || slot>=TOY_GAME_MAX_ACTORS) continue;
            actor=&game.actors[slot];
            if (!actor->active || actor->kind!=TOY_GAME_ACTOR_PLAYER) continue;
            if (scene_actor_procedural(actor,client->camera.x,client->camera.z,
                    client->camera.sy,client->camera.cy,
                    network_actor_lift(client->camera.x,client->camera.z,actor->airborne_y),
                    colors[client->client_id],0)<0) goto fail;
        }
    }
    scene_actor_history=next;scene_actor_session=active_session;
    return 0;
fail:
    scene_enemy_capture.failed=1;
    return -1;
}
