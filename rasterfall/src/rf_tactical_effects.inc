#if RF_TACTICAL_INTERACTIVE
/* Read authoritative shot counters once; presentation never advances a match. */
#undef effects
static void rf_tactical_effects_sync(struct rf_game_runtime *r)
{
    struct rf_tactical_lab *lab=r->session->tactical;if(!lab)return;
    struct rasterfall_effect_event event={0};
    event.type=RASTERFALL_EFFECT_EVENT_TRACER;event.flags=RASTERFALL_EFFECT_EVENT_DEPTH_TEST;
    event.sy=event.ey=-465;event.weapon=TOY_GAME_WEAPON_AK;
    if(lab->kind==1) {
        const struct rf_tac_world *w=&lab->match.world;
        for(int i=0;i<RF_TAC_MAX_UNITS;++i) {
            const struct rf_tac_unit *u=&w->units[i];
            if(u->shots<=lab->presented_shots[i])continue;
            lab->presented_shots[i]=u->shots;
            int target=u->last_shot_target;
            if(target<0 || target>=RF_TAC_MAX_UNITS)continue;
            const struct rf_tac_unit *v=&w->units[target];
            event.sx=(int)((u->pos.x-32)*512);event.sz=(int)((u->pos.y-24)*512);
            event.ex=(int)((v->pos.x-32)*512);event.ez=(int)((v->pos.y-24)*512);
            event.source_id=12000+i;event.sequence=u->shots;
            rasterfall_effects_consume(&r->effects,&event);
        }
    } else {
        const struct rf_range_lab *range=&lab->range;
        if(range->mark_count-lab->presented_marks>RF_RANGE_MARKS)lab->presented_marks=range->mark_count-RF_RANGE_MARKS;
        while(lab->presented_marks<range->mark_count) {
            const struct rf_range_mark *mark=&range->marks[lab->presented_marks++%RF_RANGE_MARKS];
            if(mark->lane<0)continue;
            event.sx=mark->shooter?r->camera.x:(mark->lane-4)*1536;
            event.sz=mark->shooter?r->camera.z:0;
            event.sy=mark->shooter?r->camera.y:-465;
            event.ex=(mark->lane-4)*1536+(int)(mark->x*512);
            event.ez=rf_range_distances[mark->lane]*512;event.ey=-465+(int)(mark->y*512);
            event.flags=RASTERFALL_EFFECT_EVENT_DEPTH_TEST;
            if(mark->shooter)event.flags|=RASTERFALL_EFFECT_EVENT_LOCAL_VIEW;
            rasterfall_effects_consume(&r->effects,&event);
            if(mark->hit)rasterfall_effects_spawn_hit_particles(&r->effects,event.ex,event.ey,event.ez,0,-1024);
            unsigned char sound=TOY_GAME_EV_SHOOT;rasterfall_audio_play_events(&r->audio,&sound,1);
        }
    }
}
#define effects (*active_effects)

#else
static void rf_tactical_effects_sync(struct rf_game_runtime *r){(void)r;}
#endif
