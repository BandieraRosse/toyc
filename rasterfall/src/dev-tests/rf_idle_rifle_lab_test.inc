static int rf_idle_lab_logic_test(struct rf_game_runtime *runtime)
{
    struct rasterfall_session *s=runtime->session;
    struct rasterfall_command command={0};int result=0;
#define IDLE_CHECK(x) do {if(!(x)){result=__LINE__;goto done;}}while(0)
    rf_exp_ui.category=0;rf_exp_ui.preset=6;rf_exp_ui.animation=0;
    IDLE_CHECK(!rf_exp_create(s));
    struct toy_game_actor *actor=toy_game_actor_by_id(&s->game_state,s->experiments->groups[0].ids[0]);
    IDLE_CHECK(actor && !actor->developer_only && !actor->animation_demo && !actor->hit_test_dummy);
    int x=actor->x,z=actor->z;
    for(int i=0;i<80;++i)IDLE_CHECK(!rf_game_update(runtime,&command,16));
    IDLE_CHECK(actor->x==x && actor->z==z && !actor->moving);
    rf_exp_ui.preset=7;IDLE_CHECK(!rf_exp_create(s));
    actor=toy_game_actor_by_id(&s->game_state,s->experiments->groups[0].ids[0]);z=actor->z;
    for(int i=0;i<60;++i)IDLE_CHECK(!rf_game_update(runtime,&command,16));
    IDLE_CHECK(actor->z!=z);
    IDLE_CHECK(!rasterfall_experiment_pause(s,0,1));
    z=actor->z;int elapsed=s->experiments->groups[0].elapsed_ms;
    for(int i=0;i<10;++i)IDLE_CHECK(!rf_game_update(runtime,&command,16));
    IDLE_CHECK(actor->z==z && s->experiments->groups[0].elapsed_ms==elapsed);
    rf_exp_ui.preset=8;IDLE_CHECK(!rf_exp_create(s));
    int fired=0;
    for(int i=0;i<240;++i) {
        IDLE_CHECK(!rf_game_update(runtime,&command,16));
        for(int j=0;j<4;++j) {
            actor=toy_game_actor_by_id(&s->game_state,s->experiments->groups[0].ids[j]);
            if(actor && actor->fire_seq)fired=1;
        }
    }
    IDLE_CHECK(fired);
done:
    rasterfall_experiment_replace(s,0,NULL,0,NULL);
    if(result)__printf("LIVE-AI-LAB FAIL line=%d\n",result);
    else __printf("LIVE-AI-LAB PASS dynamic idle/move/fire/pause/cleanup\n");
#undef IDLE_CHECK
    return result;
}
