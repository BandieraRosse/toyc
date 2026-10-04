/* Explicit diagnostics only. Prepare once during warmup through ordinary
 * typed commands; steady-state sampling only observes owners. No frame IO. */
#ifdef TOYC_WINDOWS
static struct {
    const char *name;
    int initialized,configured,invalid,count,aux_count[2],together;
    uint64_t previous_aux[2];
    int64_t aux_cpu[2][4096],aux_gpu[2][4096];
} rf_ui_perf;
static void rf_ui_perf_before_frame(struct rf_game_runtime *r,int frame)
{
    if(!rf_ui_perf.initialized) {
        rf_ui_perf.initialized=1;rf_ui_perf.name=getenv("RF_UI_PERF_CASE");
        if(!rf_scene_perf.enabled)rf_ui_perf.name=NULL;
    }
    if(!rf_ui_perf.name || rf_ui_perf.configured || frame>=120)return;
    const char *name=rf_ui_perf.name;
    if(!strcmp(name,"player-fps") || !strcmp(name,"experiment-fps"))rf_ui_perf.configured=1;
    else if(!strcmp(name,"player-rts") || !strcmp(name,"experiment-rts") || !strcmp(name,"dual-view")) {
        if(!strcmp(name,"dual-view") && !rf_story_camera(&r->story,r->session))return;
        rf_ui_perf.invalid|=rf_player_request(r,RF_PLAYER_VIEW,1,0,0).code!=RF_PLAYER_OK;
        if(strcmp(name,"experiment-rts")) {
            int actor=-1;
            for(int i=0;i<TOY_GAME_MAX_ACTORS;++i)if(rf_rts_selectable(&r->session->game_state,i)){actor=i;break;}
            rf_ui_perf.invalid|=actor<0 || rf_player_request(r,RF_PLAYER_RTS_SELECT,actor,0,0).code!=RF_PLAYER_OK;
        }
        rf_ui_perf.configured=1;
    } else if(!strcmp(name,"weaver-preview")) {
        int pistol=-1;
        for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
            if(rf_weaver_blueprints[i].geometry.weapon==TOY_GAME_WEAPON_PISTOL)pistol=(int)i;
        rf_ui_perf.invalid|=rf_player_request(r,RF_PLAYER_WEAVER_SELECT,pistol,0,0).code!=RF_PLAYER_OK;
        rf_weaver_terminal.open=1;rf_weaver_terminal.minimized=0;
        rf_ui_perf.configured=1;
    } else if(!strncmp(name,"comms-",6)) {
        if(!strncmp(name,"comms-remote",12) && r->story.active_story==RF_STORY_OUTPOST) {
            const struct rf_story_node *node=rf_story_current_node(&r->story);
            rf_ui_perf.invalid|=rf_player_request(r,RF_PLAYER_DIALOG_ANSWER,node&&node->choice_count?1:-1,0,0).code!=RF_PLAYER_OK;
            return;
        }
        if(!rf_story_camera(&r->story,r->session))return;
        if(!strncmp(name,"comms-remote",12) && r->story.active_story!=RF_STORY_LABS)return;
        if(!strcmp(name,"comms-hidden") || !strcmp(name,"comms-remote-hidden"))
            rf_ui_perf.invalid|=rf_player_request(r,RF_PLAYER_DIALOG_COLLAPSE,1,0,0).code!=RF_PLAYER_OK;
        else if(!strcmp(name,"comms-closed"))
            rf_ui_perf.invalid|=rf_player_request(r,RF_PLAYER_DIALOG_CLOSE,0,0,0).code!=RF_PLAYER_OK;
        else if(strcmp(name,"comms-visible") && strcmp(name,"comms-remote"))rf_ui_perf.invalid=1;
        rf_ui_perf.configured=1;
    } else {rf_ui_perf.invalid=1;rf_ui_perf.configured=1;}
}
static void rf_ui_perf_sample(const struct rf_game_runtime *r,
    const struct rf_gpu_scene_aux_slot slots[2],int frame)
{
    if(!rf_ui_perf.name)return;
    if(frame<=120){for(int i=0;i<2;++i)rf_ui_perf.previous_aux[i]=slots[i].status.frames;return;}
    int n=rf_ui_perf.count++;
    const char *name=rf_ui_perf.name;
    int dual=!strcmp(name,"dual-view");
    int wants[2]={dual || !strcmp(name,"weaver-preview") || !strcmp(name,"comms-visible") || !strcmp(name,"comms-remote"),dual || !strcmp(name,"player-rts")};
    int wants_rts=dual || !strcmp(name,"player-rts") || !strcmp(name,"experiment-rts");
    int wants_experiment=!strncmp(name,"experiment-",11);
    const struct rf_story_camera_entity *camera=rf_story_camera(&r->story,r->session);
    if(!rf_ui_perf.configured || r->rts_active!=wants_rts ||
        r->player_ui.mode!=(wants_experiment?RF_PLAYER_UI_EXPERIMENT:RF_PLAYER_UI_PLAYER))rf_ui_perf.invalid=1;
    if(!strcmp(name,"comms-hidden") || !strcmp(name,"comms-remote-hidden"))
        if(!camera || !r->story.collapsed)rf_ui_perf.invalid=1;
    if(!strcmp(name,"comms-closed") && (r->story.active_story || r->story.hold_token ||
        r->story.link!=RF_STORY_LINK_OFF))rf_ui_perf.invalid=1;
    if(!strncmp(name,"comms-remote",12)) {
        const struct toy_game_actor *player=toy_game_local_player_actor_const(&r->session->game_state);
        int64_t dx=camera?(int64_t)player->x-camera->view.x:0,dz=camera?(int64_t)player->z-camera->view.z:0;
        if(!camera || r->story.active_story!=RF_STORY_LABS || dx*dx+dz*dz<10000LL*10000)rf_ui_perf.invalid=1;
    }
    int refreshed=0;
    for(int i=0;i<2;++i) {
        const struct rf_gpu_scene_aux_status *aux=&slots[i].status;
        if(aux->state!=(wants[i]?RF_GPU_AUX_LIVE:RF_GPU_AUX_HIDDEN))rf_ui_perf.invalid=1;
        if(aux->frames==rf_ui_perf.previous_aux[i])continue;
        ++refreshed;
        if(!wants[i] || !aux->gpu_time_valid || rf_ui_perf.aux_count[i]>=4096)rf_ui_perf.invalid=1;
        else {
            int at=rf_ui_perf.aux_count[i]++;
            rf_ui_perf.aux_cpu[i][at]=aux->last_cpu_us;
            rf_ui_perf.aux_gpu[i][at]=(int64_t)(aux->last_gpu_ms*1000);
        }
        rf_ui_perf.previous_aux[i]=aux->frames;
    }
    rf_ui_perf.together+=refreshed==2;
    if(n+1<rf_scene_perf.target)return;
    for(int i=0;i<2;++i) {
        int count=rf_ui_perf.aux_count[i];
        if(wants[i] && !count)rf_ui_perf.invalid=1;
        qsort(rf_ui_perf.aux_cpu[i],count,sizeof(int64_t),rf_scene_perf_compare);
        qsort(rf_ui_perf.aux_gpu[i],count,sizeof(int64_t),rf_scene_perf_compare);
        __printf("UI-VIEW slot=%d samples=%d together=%d cpu_p50_us=%lld cpu_p95_us=%lld gpu_p50_us=%lld gpu_p95_us=%lld\n",
            i,count,rf_ui_perf.together,count?(long long)rf_ui_perf.aux_cpu[i][count/2]:0,
            count?(long long)rf_ui_perf.aux_cpu[i][(count*95+99)/100-1]:0,
            count?(long long)rf_ui_perf.aux_gpu[i][count/2]:0,
            count?(long long)rf_ui_perf.aux_gpu[i][(count*95+99)/100-1]:0);
    }
    int selected=wants[1]?1:0,count=rf_ui_perf.aux_count[selected];
    const struct rf_gpu_scene_aux_status *aux=&slots[selected].status;
    __printf("UI-PERF case=%s valid=%d samples=%d mode=%d rts=%d story=%d collapsed=%d camera=%d hold=%u aux_state=%d aux_samples=%d aux_cpu_p50_us=%lld aux_cpu_p95_us=%lld aux_gpu_p50_us=%lld aux_gpu_p95_us=%lld\n",
        name,!rf_ui_perf.invalid,rf_ui_perf.count,r->player_ui.mode,r->rts_active,r->story.active_story,
        r->story.collapsed,camera!=NULL,r->story.hold_token,aux->state,count,
        count?(long long)rf_ui_perf.aux_cpu[selected][count/2]:0,count?(long long)rf_ui_perf.aux_cpu[selected][(count*95+99)/100-1]:0,
        count?(long long)rf_ui_perf.aux_gpu[selected][count/2]:0,count?(long long)rf_ui_perf.aux_gpu[selected][(count*95+99)/100-1]:0);
}
#endif
