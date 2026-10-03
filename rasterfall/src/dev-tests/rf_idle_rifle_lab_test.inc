static int rf_idle_lab_logic_test(struct rf_game_runtime *runtime)
{
    struct toy_game *g=&runtime->session->game_state;
    struct rasterfall_command command={0};
    int result=0;
#define IDLE_CHECK(x) do {if(!(x)){result=__LINE__;goto done;}}while(0)
    rf_labs.requested[RF_LAB_IDLE_RIFLE]=1;
    rf_idle_lab_step(runtime,16);
    IDLE_CHECK(rf_idle_lab.count==RF_IDLE_LAB_ACTORS);
    struct toy_game_actor *actor=rf_idle_lab_actor(g,0);
    IDLE_CHECK(actor && !actor->developer_only && !actor->animation_demo && !actor->hit_test_dummy);
    int x=actor->x,z=actor->z;
    for(int i=0;i<80;++i)IDLE_CHECK(!rf_game_update(runtime,&command,16));
    IDLE_CHECK(actor->x==x && actor->z==z && !actor->moving && actor->combat_target.kind<0);
    rf_labs.requested[RF_LAB_IDLE_RIFLE]=2;
    int moved=0;
    for(int i=0;i<60;++i) {
        IDLE_CHECK(!rf_game_update(runtime,&command,16));
        if(actor->moving)moved=1;
    }
    IDLE_CHECK(actor->z!=z && moved);
    runtime->lifecycle_paused=1;
    int elapsed=rf_idle_lab.elapsed_ms;
    rf_idle_lab_step(runtime,100);
    IDLE_CHECK(rf_idle_lab.elapsed_ms==elapsed);
    runtime->lifecycle_paused=0;
    rf_labs.requested[RF_LAB_IDLE_RIFLE]=3;
    int acquired=0,fired=0;
    for(int i=0;i<240;++i) {
        IDLE_CHECK(!rf_game_update(runtime,&command,16));
        for(int j=0;j<RF_IDLE_LAB_ACTORS;++j) {
            struct toy_game_actor *a=rf_idle_lab_actor(g,j);
            if(a && a->combat_target.kind>=0)acquired=1;
            if(a && a->fire_seq)fired=1;
        }
    }
    IDLE_CHECK(acquired && fired);
    /* Reused slots belong to their new generation, even with the same ID. */
    actor=rf_idle_lab_actor(g,0);IDLE_CHECK(actor);
    ++actor->combat_generation;
    struct toy_game_actor replacement=*actor;
    rf_labs.requested[RF_LAB_IDLE_RIFLE]=0;
    rf_idle_lab_step(runtime,16);
    IDLE_CHECK(!rf_idle_lab.count && !memcmp(actor,&replacement,sizeof(*actor)));
    memset(actor,0,sizeof(*actor));
done:
    runtime->lifecycle_paused=0;
    rf_labs.requested[RF_LAB_IDLE_RIFLE]=0;rf_idle_lab_clear(runtime->session);
    if(result)__printf("LIVE-AI-LAB FAIL line=%d\n",result);
    else __printf("LIVE-AI-LAB PASS idle/move/target/fire/pause/owned-cleanup\n");
#undef IDLE_CHECK
    return result;
}
