/* Live experiments issue normal deployment orders to session-owned actors. */
#define RF_IDLE_LAB_ACTORS 4
static void rf_idle_lab_step(struct rf_game_runtime *r,int dt_ms)
{
    struct rasterfall_session *s=r->session;
    if(!s->experiments || r->lifecycle_paused || s->game_state.state!=TOY_GAME_PLAYING)return;
    struct rf_experiment_group *group=&s->experiments->groups[0];
    if(!group->active || group->paused || group->behavior!=2)return;
    const struct rf_map_runtime_region *area=rf_map_runtime_find_region(&s->map_ops.runtime,"actor_actions_lab_area");
    if(!area)return;
    group->elapsed_ms=(group->elapsed_ms+dt_ms)%12000;
    for(int i=0;i<group->count;++i) {
        struct toy_game_actor *a=toy_game_actor_by_id(&s->game_state,group->ids[i]);
        if(a && a->combat_generation==group->generations[i])
            a->deployment_z=area->origin_z+(group->elapsed_ms<6000?-1600:1600);
    }
}
static void rf_idle_lab_camera(const struct rasterfall_session *s,struct camera *camera)
{
    rf_lab_camera_position(s,camera,"actor_actions_lab_area",0,5600);
    camera->y=1600;camera->sy=0;camera->cy=-1024;
    camera->pitch_sy=-280;camera->pitch_cy=985;
}
