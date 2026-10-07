#if RF_TACTICAL_INTERACTIVE
/* Session owns experimental authority; the ordinary Game owns only the observer. */
void rasterfall_session_tactical_reset(struct rasterfall_session *s)
{
    if(s->world_id!=RASTERFALL_WORLD_TACTICAL_ARENA && s->world_id!=RASTERFALL_WORLD_TACTICAL_RANGE)return;
    if(!s->tactical)s->tactical=calloc(1,sizeof(*s->tactical));
    if(!s->tactical)return;
    struct rf_tactical_lab *lab=s->tactical;
    lab->kind=s->world_id==RASTERFALL_WORLD_TACTICAL_ARENA?1:2;
    lab->seed=1337;lab->selected=0;lab->accumulator_us=0;++lab->generation;
    struct rf_tac_policy policies[2];
    rf_tac_policy_default(&policies[0],RF_TAC_SIMPLE);
    rf_tac_policy_default(&policies[1],RF_TAC_MECHANICAL);
    if(lab->kind==1)rf_tac_match_reset(&lab->match,100,lab->seed,4,RF_TW_RIFLE,policies);
    else {rf_range_reset(&lab->range,RF_TW_RIFLE,RF_TW_AUTO,3,0,lab->seed);lab->player_firing=1;}
    s->game_state.external_director=1;
    s->game_state.to_spawn=0;
}

void rasterfall_session_tactical_step(struct rasterfall_session *s,const struct camera *camera,
    struct rasterfall_command *cmd,int dt_us,int blocked)
{
    struct rf_tactical_lab *lab=s->tactical;
    if(!lab)return;
    unsigned buttons=cmd->buttons;int held=cmd->fire_held;
    /* Old weapon settlement must never also fire a tactical test shot. */
    if(lab->kind==2 && lab->player_firing) {
        cmd->buttons&=~(RASTERFALL_CMD_FIRE|RASTERFALL_CMD_RELOAD);cmd->fire_held=0;
    }
    if(blocked || !lab->player_firing)lab->pending_fire=lab->pending_reload=0;
    else {lab->pending_fire|=(buttons&RASTERFALL_CMD_FIRE)!=0;lab->pending_reload|=(buttons&RASTERFALL_CMD_RELOAD)!=0;}
    lab->accumulator_us+=dt_us;
    while(lab->accumulator_us>=RF_TAC_DT_MS*1000) {
        lab->accumulator_us-=RF_TAC_DT_MS*1000;
        if(lab->kind==1) {
            if(lab->match.running) {
                int result=rf_tac_match_step(&lab->match);
                if(result<0){lab->match.running=0;s->banner_text="TACTICAL STEP FAILED";s->banner_ms=2500;}
                if(lab->match.world.finished && !rf_tactical_lab_export(lab)) {
                    s->banner_text="TACTICAL EXPORT FAILED";s->banner_ms=5000;
                }
            }
        } else {
            struct rf_range_lab *r=&lab->range;
            rf_range_step(r,RF_TAC_DT_MS);
            if(!blocked && lab->pending_reload)rf_tw_state_begin_reload(rf_tw_profile_get(r->weapon),&r->weapons[1]);
            if(!blocked && lab->player_firing && (held || lab->pending_fire)) {
                int lane=-1;float ax=1000,ay=1000,distance=100,best=1e20f;
                /* The player may move and jump normally. Pick the target
                 * closest to the aim ray and settle at the actual distance. */
                if(camera->cy>0 && camera->pitch_cy>0) {
                    for(int l=0;l<RF_RANGE_LANES;++l) {
                        float dz=rf_range_distances[l]*512.0f-camera->z;
                        float dx=(l-4)*1536-camera->x;
                        float x=dz*camera->sy/camera->cy-dx;
                        float y=camera->y+900+dz*camera->pitch_sy*1024.0f/(camera->pitch_cy*camera->cy)-435.2f;
                        if(dz>32) {
                            float score=(x*x+y*y)/(dx*dx+dz*dz);
                            if(score<best){best=score;lane=l;ax=x/512.0f;ay=y/512.0f;distance=(float)sqrt(dx*dx+dz*dz)/512.0f;}
                        }
                    }
                }
                if(rf_range_fire_distance(r,1,lane,ax,ay,distance)) {
                    struct toy_game_actor *p=toy_game_local_player_actor(&s->game_state);
                    p->muzzle_flash_ms=80;
                    if(s->game_state.event_count<TOY_GAME_MAX_EVENTS)
                        s->game_state.events[s->game_state.event_count++]=TOY_GAME_EV_SHOOT;
                }
            }
            struct toy_game_actor *p=toy_game_local_player_actor(&s->game_state);
            p->current_slot=0;p->slots[0].weapon=r->weapon==RF_TW_RIFLE?TOY_GAME_WEAPON_AK:TOY_GAME_WEAPON_SMG;
            p->slots[0].mag=r->weapons[1].ammo;
        }
        buttons&=~RASTERFALL_CMD_FIRE;
        lab->pending_fire=lab->pending_reload=0;
    }
}

#else
void rasterfall_session_tactical_reset(struct rasterfall_session *s){(void)s;}
void rasterfall_session_tactical_step(struct rasterfall_session *s,const struct camera *c,struct rasterfall_command *cmd,int dt,int blocked)
{(void)s;(void)c;(void)cmd;(void)dt;(void)blocked;}
#endif
