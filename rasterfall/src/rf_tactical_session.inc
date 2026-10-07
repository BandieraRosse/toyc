#if RF_TACTICAL_INTERACTIVE
/* Session owns scenario controls; Game owns every participant. */
void rasterfall_session_tactical_reset(struct rasterfall_session *s)
{
    if(s->world_id!=RASTERFALL_WORLD_TACTICAL_ARENA && s->world_id!=RASTERFALL_WORLD_TACTICAL_RANGE)return;
    if(!s->tactical)s->tactical=calloc(1,sizeof(*s->tactical));
    if(!s->tactical)return;
    struct rf_tactical_lab *lab=s->tactical;
    lab->kind=s->world_id==RASTERFALL_WORLD_TACTICAL_ARENA?1:2;
    lab->seed=1337;lab->selected=0;lab->accumulator_us=0;++lab->generation;
    struct rf_tac_policy policies[2];
    rf_tac_policy_default(&policies[0],RF_TAC_M0);
    rf_tac_policy_default(&policies[1],RF_TAC_M0);
    if(lab->kind==1) {
        lab->match.bound_game=&s->game_state;
        rf_tac_match_reset(&lab->match,100,lab->seed,4,RF_TW_RIFLE,policies);
        rf_tac_prepare(&lab->match.world,1);
    }
    else {
        rf_range_reset(&lab->range,RF_TW_RIFLE,RF_TW_AUTO,3,0,lab->seed);
        rf_range_bind(&lab->range,&s->game_state);lab->player_firing=1;
        rf_range_prepare(&lab->range,0);
    }
    s->game_state.external_director=1;
    s->game_state.to_spawn=0;
}

void rasterfall_session_tactical_step(struct rasterfall_session *s,const struct camera *camera,
    struct rasterfall_command *cmd,int dt_us,int blocked)
{
    struct rf_tactical_lab *lab=s->tactical;
    (void)camera;
    if(!lab)return;
    lab->accumulator_us=dt_us/1000;
    if(lab->kind==1) {
        if(lab->match.running && !lab->match.world.finished) {
            if(rf_tac_match_prepare(&lab->match)<0)lab->match.running=0;
        }
        rf_tac_prepare(&lab->match.world,!lab->match.running);
    } else {
        /* Only UI focus suppresses input; normal player weapon rules own fire. */
        if(blocked) {cmd->buttons&=~(RASTERFALL_CMD_FIRE|RASTERFALL_CMD_RELOAD);cmd->fire_held=0;}
        rf_range_prepare(&lab->range,dt_us/1000);
    }
}

int rasterfall_session_tactical_logic_test(void)
{
    struct rasterfall_session *s=calloc(1,sizeof(*s));
    struct camera camera={0};struct rasterfall_command cmd={0};
    struct rf_tac_world reference={0};int result=0;
    if(!s)return 1;
    if(rasterfall_session_load(s,"rasterfall/assets/maps/tactical_arena.map")<0){result=2;goto done;}
    rasterfall_session_reset(s,&camera,1337);
    if(!s->tactical || s->tactical->match.world.game!=&s->game_state){result=3;goto done;}
    struct rf_tac_match *m=&s->tactical->match;m->running=1;
    if(!rf_tac_world_clone(&reference,&m->world)){result=4;goto done;}
    for(int i=0;i<100;++i) {
        memset(&cmd,0,sizeof(cmd));
        rasterfall_session_tactical_step(s,&camera,&cmd,16000,0);
        if(reference.time_ms%RF_TAC_THINK_MS<RF_TAC_DT_MS)
            for(int t=0;t<2;++t)if(!rf_tac_apply(&reference,&m->plans[t])){result=5;goto done;}
        rf_tac_step(&reference);
        rasterfall_session_step(s,&camera,&cmd,16);rf_tac_finish(&m->world,16);
        if(rf_tac_hash(&reference)!=rf_tac_hash(&m->world)){result=6;goto done;}
    }
    rf_tac_world_destroy(&reference);
    if(rasterfall_session_load(s,"rasterfall/assets/maps/tactical_range.map")<0){result=7;goto done;}
    rasterfall_session_reset(s,&camera,1337);
    if(!s->tactical || s->tactical->range.game!=&s->game_state){result=8;goto done;}
    struct rf_range_lab *range=&s->tactical->range;range->running=1;
    struct toy_game_actor *player=toy_game_local_player_actor(&s->game_state);
    camera.x=player->x=(range->lane-4)*1536;camera.z=player->z=0;
    camera.sy=0;camera.cy=1024;camera.pitch_sy=0;camera.pitch_cy=1024;
    for(int i=0;i<160;++i) {
        memset(&cmd,0,sizeof(cmd));cmd.fire_held=1;cmd.buttons=RASTERFALL_CMD_FIRE;
        rasterfall_session_tactical_step(s,&camera,&cmd,16000,0);
        rasterfall_session_step(s,&camera,&cmd,16);rf_range_collect(range,16);
    }
    int shots=0;for(int l=0;l<RF_RANGE_LANES;++l)shots+=range->stats[1][l].shots;
    if(!shots || shots!=player->combat_stats.shots || !range->stats[0][range->lane].shots){result=9;goto done;}
    int before=0,after=0;
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)before+=s->game_state.actors[i].active!=0;
    rasterfall_session_reset(s,&camera,1337);
    for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)after+=s->game_state.actors[i].active!=0;
    if(before!=after || !s->tactical || s->tactical->range.game!=&s->game_state){result=10;goto done;}
    __printf("TACTICAL-SESSION Game authority / headless parity / player fire / reset passed\n");
done:
    rf_tac_world_destroy(&reference);rasterfall_session_unload(s);free(s);return result;
}

#else
int rasterfall_session_tactical_logic_test(void){return 0;}
void rasterfall_session_tactical_reset(struct rasterfall_session *s){(void)s;}
void rasterfall_session_tactical_step(struct rasterfall_session *s,const struct camera *c,struct rasterfall_command *cmd,int dt,int blocked)
{(void)s;(void)c;(void)cmd;(void)dt;(void)blocked;}
#endif
