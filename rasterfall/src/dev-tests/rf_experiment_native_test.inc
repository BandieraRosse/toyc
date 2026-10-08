/* Opt-in native lifecycle audit; clicks use the production terminal handler. */
static int rf_exp_native_test(struct rf_game_runtime *r,int frame,int w,int h)
{
    static int base_primitives,last_frame=-1;
    if(!getenv("RF_EXPERIMENT_NATIVE_TEST"))return 0;
    if(frame==last_frame)return 0;
    last_frame=frame;
    if(r->session->world_id!=RASTERFALL_WORLD_OUTPOST)return -1;
    if(frame==21 && !rf_labs.requested[RF_LAB_LIGHTING])return -1;
    if(frame==0) {
        for(int i=0;i<3;++i)if(rasterfall_experiment_replace(r->session,i,NULL,0,NULL)<0)return -1;
        base_primitives=r->session->level.primitive_count;
    }
    if(frame<2 || frame>22 || frame%2)return 0;
    int action=0;
    rf_exp_ui.category=0;rf_exp_ui.preset=1;rf_exp_ui.mode=1;rf_exp_ui.animation=0;
    if(frame==4 || frame==6)action=1;
    if(frame==8 || frame==12 || frame==18 || frame==22)action=3;
    if(frame==10)rf_exp_ui.preset=4;
    if(frame==14 || frame==16 || frame==18) {
        rf_exp_ui.category=2;rf_exp_ui.preset=frame==14?0:1;
    }
    if(frame==20 || frame==22){rf_exp_ui.category=1;rf_exp_ui.preset=0;}
    rf_exp_ui.open=1;rf_exp_ui.session=r->session;rf_exp_ui.dropdown=-1;
    int x,y,pw,ph;rf_exp_panel(w,h,&x,&y,&pw,&ph);
    struct toy_window_events events={0};
    rf_exp_input(r,w,h,x+24+action*((pw-56)/5+2)+10,y+ph-75,1,0,&events);
    const struct rf_experiment_group *group=&r->session->experiments->groups[rf_exp_ui.category];
    if((action==3 && group->active) || (action==0 && !group->active) ||
        (frame==4 && !group->paused) || (frame==6 && group->paused))return -1;
    if(frame==22 && r->session->level.primitive_count!=base_primitives)return -1;
    __printf("EXPERIMENT-NATIVE frame=%d group=%d action=%d active=%d bodies=%d primitives=%d world=%llu\n",
        frame,rf_exp_ui.category,action,group->active,group->count,r->session->level.primitive_count,
        (unsigned long long)r->session->scene_local.world_generation);
    rf_exp_ui.open=0;
    return 0;
}
