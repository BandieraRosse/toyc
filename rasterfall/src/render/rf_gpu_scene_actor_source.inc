/* Independent actor presentation source. Geometry extraction consumes the
 * captured values later; this adapter never enters a draw producer. */
static struct rf_gpu_scene_local_source scene_actor_history;
static const struct rasterfall_session *scene_actor_session;
static struct toy_game scene_actor_fixture_game;

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
    item->state.profession_id=actor->character_id<0 ? RASTERFALL_PROFESSION_NONE : profile->profession_id;
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
    const struct rasterfall_net *net=active_net;
    static const unsigned colors[RASTERFALL_NET_PLAYER_MAX]={
        RF_COLOR_AI_RIFLE,RF_COLOR_UI_PLAYER,0xD59BFF,0x66D9D9};
    if (!camera || !width || !height || !active_session ||
        !scene_enemy_capture_active || scene_enemy_capture.failed ||
        scene_enemy_capture.procedural_count || scene_enemy_capture.modular_count ||
        scene_enemy_capture.frame_id>UINT_MAX) return -1;
    next.world_generation=scene_enemy_capture.world_generation;
    next.frame_id=scene_enemy_capture.frame_id-1;
    if (active_session->rts_active) {
        const struct toy_game_actor *player =
            toy_game_local_player_actor_const(&game);
        if (player && player->active &&
            scene_actor_procedural(player, player->x, player->z,
                player->sy, player->cy,
                player->ground_y + player->airborne_y,
                RF_COLOR_UI_PLAYER, 0) < 0) goto fail;
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
        (outpost_actor_actions_visible || outpost_actor_walk_visible)) {
        memcpy(&scene_actor_fixture_game,&game,sizeof(game));
        pose_game=&scene_actor_fixture_game;
        for (int family=0;family<2;++family) {
            if (!outpost_actor_actions_visible) break;
            for (int i=0;i<OUTPOST_ACTOR_ACTION_COUNT;++i) {
                struct toy_game_actor actor;
                int x=-4284+(i%6)*1650;
                int z=3156-(i/6)*2100-family*4200;
                if (family && i>=3) continue;
                outpost_actor_fixture(&actor,family,1,outpost_actor_actions[i],
                    x,z,1,outpost_actor_actions_time_us,family*32+i);
                if (family) {
                    int slot=TOY_GAME_MAX_ACTORS-1;
                    while (slot>=0 && scene_actor_fixture_game.actors[slot].active)
                        --slot;
                    if (slot<0) goto fail;
                    scene_actor_fixture_game.actors[slot]=actor;
                    if (rf_gpu_scene_local_created(&next,&actor)<0) goto fail;
                    scene_actor_restore_fixture_history(&next,actor.actor_id);
                } else {
                    if (scene_actor_procedural(&actor,x,z,0,1024,0,
                            RF_COLOR_AI_RIFLE,1)<0) goto fail;
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
                    if (rf_gpu_scene_local_created(&next,&actor)<0) goto fail;
                    scene_actor_restore_fixture_history(&next,actor.actor_id);
                } else {
                    unsigned tint=entry->level==2 ? RF_COLOR_AI_HEAVY :
                        entry->level==1 ? RF_COLOR_AI_RIFLE : RF_COLOR_AI_BASIC;
                    if (scene_actor_procedural(&actor,actor.x,actor.z,
                            actor.sy,actor.cy,0,tint,1)<0) goto fail;
                }
            }
    }
    if (rf_gpu_scene_local_freeze_presentation(&next,pose_game,camera,width,height,
            0,NULL,0,&frame)<0) goto fail;
    for (unsigned i=0;i<frame.snapshot.actor_count;++i) {
        struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[scene_enemy_capture.modular_count];
        if (rf_gpu_scene_pose_extract_at(&frame,i,pose)<0) goto fail;
        pose->identity.source_id=0x80000000u+i;
        pose->identity.generation=(unsigned)scene_enemy_capture.frame_id;
        scene_enemy_capture.modular_count++;
    }
    if (active_session->world_id==RASTERFALL_WORLD_OUTPOST && outpost_model_lab_visible &&
        outpost_model_lab_status>=0) {
        unsigned begin=scene_enemy_capture.modular_count;
        for (unsigned i=0;i<2;++i) {
            if (scene_enemy_capture.modular_count>=TOY_GAME_MAX_ACTORS) goto fail;
            struct rf_gpu_scene_pose_v1 *pose=&scene_enemy_capture.modular[scene_enemy_capture.modular_count];
            int x,z;
            rasterfall_render_lab_point("rf_model_lab_area",-384+(int)i*1200,344,&x,&z);
            if (rf_gpu_scene_pose_body(RASTERFALL_BODY_RF_C01_V22H,
                    scene_enemy_capture.frame_id,scene_enemy_capture.world_generation,
                    outpost_model_lab_time_us/1000,i,x,-900,z,1024,pose)<0) {
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
