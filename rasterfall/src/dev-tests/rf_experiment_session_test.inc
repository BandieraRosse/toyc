static int rf_experiment_session_test(void)
{
    struct rasterfall_session *s=calloc(1,sizeof(*s));
    struct camera camera={0};
    struct rf_experiment_actor_spec spec={RASTERFALL_CHARACTER_RF_RIFLEMAN,
        TOY_GAME_WEAPON_AK,TOY_GAME_ANIM_MOVE,2,123,-9728,0,-19968,0,1024,0,0};
    int result=-1;
    if(!s)return -1;
    if(rasterfall_session_load(s,"rasterfall/assets/maps/outpost.map")<0)goto done;
    rasterfall_session_reset(s,&camera,731);
    int original_props=s->level.prop_count,original_bodies=0;
    uint64_t initial_world=s->scene_local.world_generation;
    void *initial_map=s->map_ops.runtime.impl;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)original_bodies+=s->game_state.actors[i].active!=0;
    if(rasterfall_experiment_replace(s,0,&spec,1,NULL)<0)goto done;
    if(s->scene_local.world_generation!=initial_world || s->map_ops.runtime.impl!=initial_map)goto done;
    struct rf_experiment_group *group=&s->experiments->groups[0];
    int id=group->ids[0];unsigned generation=group->generations[0];
    struct toy_game_actor *a=toy_game_actor_by_id(&s->game_state,id);
    if(!a || a->animation_control!=2 || a->animation.time_ms!=123)goto done;
    toy_game_update_world(&s->game_state,16);
    if(a->animation.time_ms!=123 || a->x!=spec.x || a->z!=spec.z)goto done;
    uint64_t world=s->scene_local.world_generation;
    void *map=s->map_ops.runtime.impl;
    /* Failed replacement must retain all published instances and identities. */
    if(rasterfall_experiment_replace(s,0,&spec,1,"rasterfall/assets/maps/__missing_group__.map")>=0 ||
        map!=s->map_ops.runtime.impl || world!=s->scene_local.world_generation ||
        a->combat_generation!=generation)goto done;
    struct rf_map_runtime duplicate={0};
    const struct rf_map_runtime *parts[]={&s->map_ops.runtime};
    if(rf_map_runtime_compose(&duplicate,&s->map_ops.runtime,NULL,0)<0)goto done;
    void *prior=duplicate.impl;
    int rejected=rf_map_runtime_compose(&duplicate,&s->map_ops.runtime,parts,1)<0 && duplicate.impl==prior;
    rf_map_runtime_unload(&duplicate);
    if(!rejected)goto done;
    spec.mode=1;
    if(rasterfall_experiment_replace(s,0,&spec,1,NULL)<0)goto done;
    a=toy_game_actor_by_id(&s->game_state,s->experiments->groups[0].ids[0]);
    if(!a || a->combat_generation==generation)goto done;
    if(rasterfall_experiment_pause(s,0,1)<0)goto done;
    toy_game_update_world(&s->game_state,16);
    if(a->animation.time_ms!=123)goto done;
    if(rasterfall_experiment_pause(s,0,0)<0)goto done;
    toy_game_update_world(&s->game_state,16);
    if(a->animation.time_ms!=139)goto done;
    /* A reused slot is not owned by the old group. */
    ++a->combat_generation;
    if(rasterfall_experiment_replace(s,0,NULL,0,NULL)<0 || !a->active)goto done;
    a->active=0;
    if(s->level.prop_count!=original_props)goto done;
    int remaining=0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)remaining+=s->game_state.actors[i].active!=0;
    if(remaining!=original_bodies)goto done;
    spec.enemy_kind=TOY_GAME_ENEMY_PURSUIT_COMMON+1;spec.mode=2;
    if(rasterfall_experiment_replace(s,0,&spec,1,NULL)<0)goto done;
    struct toy_game_enemy *e=&s->game_state.enemies[-s->experiments->groups[0].ids[0]-1];
    toy_game_update_world(&s->game_state,16);
    if(!e->active || e->animation_clock_ms!=123 || e->x!=spec.x || e->z!=spec.z)goto done;
    if(rasterfall_experiment_replace(s,0,NULL,0,NULL)<0 || e->active)goto done;
    spec.enemy_kind=0;
    static const char *templates[]={"experiment_material","experiment_atmosphere","experiment_electronics","experiment_weaver","experiment_components"};
    for(int i=0;i<5;++i) {
        char path[128];snprintf(path,sizeof(path),"rasterfall/assets/maps/%s.map",templates[i]);
        int old_primitives=s->level.primitive_count;
        if(rasterfall_experiment_replace(s,1,NULL,0,path)<0 || s->level.primitive_count<=old_primitives)goto done;
        if(rasterfall_experiment_replace(s,1,NULL,0,NULL)<0 || s->level.primitive_count!=old_primitives ||
            s->level.prop_count!=original_props)goto done;
    }
    if(rasterfall_experiment_replace(s,0,&spec,1,NULL)<0)goto done;
    id=s->experiments->groups[0].ids[0];generation=s->experiments->groups[0].generations[0];
    int base_primitives=s->level.primitive_count;
    if(rasterfall_experiment_replace(s,1,NULL,0,"rasterfall/assets/maps/experiment_material.map")<0 ||
       rasterfall_experiment_replace(s,2,NULL,0,"rasterfall/assets/maps/experiment_weaver.map")<0 ||
       rasterfall_experiment_replace(s,3,NULL,0,"rasterfall/assets/maps/experiment_components.map")<0)goto done;
    int combined=s->level.primitive_count;
    if(rasterfall_experiment_replace(s,2,NULL,0,NULL)<0 || s->level.primitive_count>=combined ||
       !s->experiments->groups[1].active || !s->experiments->groups[3].active)goto done;
    a=toy_game_actor_by_id(&s->game_state,id);
    if(!a || a->combat_generation!=generation)goto done;
    if(rasterfall_experiment_replace(s,1,NULL,0,NULL)<0 ||
       rasterfall_experiment_replace(s,3,NULL,0,NULL)<0 || s->level.primitive_count!=base_primitives)goto done;
    rasterfall_session_reset(s,&camera,731);
    if(s->experiments)goto done;
    result=0;
done:
    rasterfall_session_unload(s);free(s);
    if(!result)__printf("EXPERIMENT-GROUP real-actors/freeze/pause/replace/rollback/stale-slot/reset: PASS\n");
    return result;
}
