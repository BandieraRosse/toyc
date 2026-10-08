/* Session owns experiment lifetimes; renderers only consume normal map/actors. */
static void experiment_remove_actors(struct toy_game *game,
                                     const struct rf_experiment_group *group)
{
    for (int i=0;i<group->count;++i) {
        if(group->ids[i]<0) {
            int slot=-group->ids[i]-1;
            if(slot<TOY_GAME_MAX_ENEMIES && game->enemies[slot].combat_generation==group->generations[i]) {
                if(game->enemies[slot].active==1)--game->enemies_alive;
                memset(&game->enemies[slot],0,sizeof(game->enemies[slot]));
            }
            continue;
        }
        struct toy_game_actor *actor=toy_game_actor_by_id(game,group->ids[i]);
        if (!actor || actor->combat_generation!=group->generations[i]) continue;
        for (int j=0;j<TOY_GAME_MAX_PROJECTILES;++j)
            if(game->projectiles[j].owner_actor_id==actor->actor_id)
                game->projectiles[j].active=0;
        for (int j=0;j<TOY_CONFIG_MAX_BURN_ZONES;++j)
            if(game->burn_zones[j].owner_actor_id==actor->actor_id)
                game->burn_zones[j].active=0;
        memset(actor,0,sizeof(*actor));
    }
}

void rasterfall_experiment_dispose(struct rasterfall_session *s,int restore_base)
{
    struct rasterfall_experiments *owner=s?s->experiments:NULL;
    if(!owner)return;
    for(int i=0;i<RF_EXPERIMENT_GROUPS;++i) {
        experiment_remove_actors(&s->game_state,&owner->groups[i]);
        rf_map_runtime_unload(&owner->groups[i].map);
    }
    if(restore_base && owner->base.impl) {
        rf_map_runtime_unload(&s->map_ops.runtime);
        s->map_ops.runtime=owner->base;
        memset(&owner->base,0,sizeof(owner->base));
    }
    rf_map_runtime_unload(&owner->base);
    free(owner);s->experiments=NULL;
}

int rasterfall_experiment_replace(struct rasterfall_session *s,int slot,
    const struct rf_experiment_actor_spec *spec,int count,const char *map_path)
{
    struct rasterfall_experiments *owner;
    struct rasterfall_session *prepared=NULL;
    struct rf_experiment_group next={0};
    const struct rf_map_runtime *parts[RF_EXPERIMENT_GROUPS];
    int part_count=0,new_owner=0,result=-1;
    if(!s || !s->map_ops.runtime_loaded || slot<0 || slot>=RF_EXPERIMENT_GROUPS ||
        count<0 || count>RF_EXPERIMENT_ACTORS || (count && !spec))return -1;
    owner=s->experiments;
    if(!owner) {
        owner=calloc(1,sizeof(*owner));
        if(!owner)return -1;
        new_owner=1;
        if(rf_map_runtime_compose(&owner->base,&s->map_ops.runtime,NULL,0)<0)goto done;
    }
    if(owner->serial==~0u || s->scene_local.failed ||
        s->scene_local.world_generation==UINT64_MAX)goto done;
    if(map_path && rf_map_runtime_load(&next.map,map_path)<0)goto done;
    int geometry_changed=next.map.impl || owner->groups[slot].map.impl;
    for(int i=0;i<RF_EXPERIMENT_GROUPS;++i) {
        const struct rf_map_runtime *part=i==slot?&next.map:&owner->groups[i].map;
        if(part->impl)parts[part_count++]=part;
    }
    prepared=calloc(1,sizeof(*prepared));
    if(!prepared)goto done;
    rasterfall_map_bind(&prepared->map_ops,&prepared->level,prepared->safe_rooms,
        prepared->spawn_zones,&prepared->spawn_count,&prepared->air_walls_enabled,
        prepared->items,&prepared->item_count);
    if(geometry_changed) {
        if(rf_map_runtime_compose(&prepared->map_ops.runtime,&owner->base,parts,part_count)<0)goto done;
        prepared->map_ops.runtime_loaded=1;
        if(rasterfall_map_project_runtime(&prepared->map_ops)<0)goto done;
    }
    memcpy(&prepared->game_state,&s->game_state,sizeof(s->game_state));
    if(geometry_changed)toy_game_set_primitives(&prepared->game_state,prepared->level.primitives,
        prepared->level.primitive_count,prepared->level.room_limit);
    experiment_remove_actors(&prepared->game_state,&owner->groups[slot]);
    for(int i=0;i<count;++i) {
        const struct rf_experiment_actor_spec *entry=&spec[i];
        if(entry->enemy_kind) {
            if(entry->mode<0 || entry->mode>2 || entry->time_ms<0 ||
                (entry->animation!=TOY_GAME_ANIM_IDLE && entry->animation!=TOY_GAME_ANIM_MOVE))goto done;
            int index=toy_game_spawn_enemy(&prepared->game_state,entry->enemy_kind-1,entry->x,entry->z);
            if(index<0)goto done;
            struct toy_game_enemy *e=&prepared->game_state.enemies[index];
            e->animation_control=entry->mode;e->animation_id=entry->animation;
            e->animation_clock_ms=(unsigned)entry->time_ms;e->ground_y=entry->y;
            e->dir_x=entry->sy;e->dir_z=entry->cy;
            next.ids[i]=-index-1;next.generations[i]=e->combat_generation;next.modes[i]=entry->mode;
            ++next.count;continue;
        }
        if(entry->character<RASTERFALL_CHARACTER_NONE || entry->character>=RASTERFALL_CHARACTER_COUNT ||
            entry->weapon<TOY_GAME_WEAPON_PISTOL || entry->weapon>TOY_GAME_WEAPON_AWP)goto done;
        int id=entry->target?toy_game_add_gunner(&prepared->game_state,0,entry->x,entry->z,"EXPERIMENT TARGET"):
            toy_game_add_ai(&prepared->game_state,TOY_GAME_AI_LEVEL_2,entry->x,entry->z,"EXPERIMENT");
        struct toy_game_actor *a=toy_game_actor_by_id(&prepared->game_state,id);
        if(!a)goto done;
        a->character_id=entry->character;a->companion=0;a->ground_y=entry->y;
        a->deployment_x=entry->x;a->deployment_z=entry->z;a->flag_index=-1;
        a->sy=entry->sy;a->cy=entry->cy;a->pitch_cy=1024;
        toy_game_actor_equip_weapon(&prepared->game_state,a,entry->weapon);
        toy_game_actor_refill_ammo(&prepared->game_state,a);
        if(entry->target){a->hit_test_dummy=1;a->fire_enabled=0;}
        if(toy_game_actor_playback(a,entry->mode,entry->animation,entry->time_ms)<0)goto done;
        next.ids[i]=id;next.generations[i]=a->combat_generation;next.modes[i]=entry->mode;
        ++next.count;
    }
    /* Prepare identities before committing either Game or geometry. */
    prepared->scene_local=s->scene_local;
    if(geometry_changed) {
        if(rf_gpu_scene_local_world(&prepared->scene_local)<0)goto done;
    } else for(int i=0;i<owner->groups[slot].count;++i) {
        const struct toy_game_actor *old=toy_game_actor_by_id(&s->game_state,owner->groups[slot].ids[i]);
        if(old && old->combat_generation==owner->groups[slot].generations[i])
            rf_gpu_scene_local_removed(&prepared->scene_local,old->actor_id);
    }
    for(int i=1;i<TOY_GAME_REMOTE_ACTOR_BASE;++i) {
        const struct toy_game_actor *a=&prepared->game_state.actors[i];
        int registered=0;
        for(uint32_t j=0;j<prepared->scene_local.actor_count;++j)
            if(prepared->scene_local.actors[j].actor_id==a->actor_id)registered=1;
        if(a->active && !a->hired && !registered && rasterfall_character_visual_recipe_for_character(a->character_id) &&
            rf_gpu_scene_local_created(&prepared->scene_local,a)<0)goto done;
    }
    /* All fallible work completed. Scene retires old instances by actor epoch,
     * or old static snapshots by world generation, on the next frame. */
    if(geometry_changed) {
        rf_map_runtime_unload(&s->map_ops.runtime);
        s->map_ops.runtime=prepared->map_ops.runtime;
        memset(&prepared->map_ops.runtime,0,sizeof(prepared->map_ops.runtime));
        char *blob=s->level.blob;
        s->level=prepared->level;s->level.blob=blob;
    }
    s->game_state=prepared->game_state;s->scene_local=prepared->scene_local;
    if(slot==2)toy_mesh_weaver_defaults(&s->game_state.weaver);
    if(geometry_changed) {
        rasterfall_map_prepare(&s->map_ops);
        rasterfall_map_reset_interactables(&s->map_ops);
        s->weaver_item_serial=0;s->weaver_item_index=-1;
        rasterfall_session_weaver_sync(s);
        toy_game_set_primitives(&s->game_state,s->level.primitives,s->level.primitive_count,s->level.room_limit);
    }
    rf_map_runtime_unload(&owner->groups[slot].map);
    next.active=count>0 || map_path!=NULL;next.serial=++owner->serial;
    owner->groups[slot]=next;memset(&next.map,0,sizeof(next.map));
    s->experiments=owner;result=0;
done:
    if(result<0)__fprintf(2,"EXPERIMENT group=%d rejected: %s / %s\n",slot,next.map.error,
        prepared?prepared->map_ops.runtime.error:"");
    rf_map_runtime_unload(&next.map);
    if(prepared) {rf_map_runtime_unload(&prepared->map_ops.runtime);free(prepared);}
    if(result<0 && new_owner) {rf_map_runtime_unload(&owner->base);free(owner);}
    return result;
}

int rasterfall_experiment_pause(struct rasterfall_session *s,int slot,int paused)
{
    if(!s || !s->experiments || slot<0 || slot>=RF_EXPERIMENT_GROUPS)return -1;
    struct rf_experiment_group *group=&s->experiments->groups[slot];
    if(!group->active)return -1;
    for(int i=0;i<group->count;++i) {
        if(group->ids[i]<0) {
            struct toy_game_enemy *e=&s->game_state.enemies[-group->ids[i]-1];
            if(e->combat_generation==group->generations[i])e->animation_control=paused?2:group->modes[i];
            continue;
        }
        struct toy_game_actor *a=toy_game_actor_by_id(&s->game_state,group->ids[i]);
        if(!a || a->combat_generation!=group->generations[i])continue;
        if(paused && !a->animation_control)a->animation_clock_ms=(unsigned)s->game_state.combat_time_ms;
        a->animation_control=paused?2:group->modes[i];
        a->simulation_paused=paused || group->modes[i]!=0;
    }
    group->paused=paused!=0;
    return 0;
}
