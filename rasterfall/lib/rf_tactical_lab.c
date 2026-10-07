#include "rf_tactical_lab.h"
#include "rf_tactical_beam.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <time.h>

const int rf_range_distances[RF_RANGE_LANES]={5,10,15,20,30,40,60,80,100};

void rf_tac_match_destroy(struct rf_tac_match *m)
{
    int t;
    if(!m)return;
    for(t=0;t<2;++t) { rf_tac_prediction_destroy(m->prediction[t]);m->prediction[t]=NULL; }
    m->ready=m->running=0;
}
int rf_tac_match_reset(struct rf_tac_match *m,unsigned map_seed,unsigned shot_seed,
    int squad,int weapon,const struct rf_tac_policy policies[2])
{
    int t;
    if(!m || !policies || !rf_tac_policy_validate(&policies[0]) || !rf_tac_policy_validate(&policies[1]))return 0;
    rf_tac_match_destroy(m);
    if(!rf_tac_map_generate(&m->map,map_seed) ||
       !rf_tac_world_init(&m->world,&m->map,shot_seed,squad,weapon,120000))return 0;
    for(t=0;t<2;++t) {
        m->policies[t]=policies[t];
        rf_tac_command(&m->world,t,t?RF_TAC_DEFEND:RF_TAC_ATTACK,m->map.objective,m->map.objective_radius);
    }
    memset(m->observations,0,sizeof(m->observations));
    memset(m->plans,0,sizeof(m->plans));memset(m->traces,0,sizeof(m->traces));
    m->previous=m->world;m->ready=1;m->running=0;m->remainder_ms=0;
    return 1;
}
int rf_tac_match_step(struct rf_tac_match *m)
{
    int t;
    if(!m || !m->ready || m->world.finished)return 0;
    if(m->world.time_ms%RF_TAC_THINK_MS==0) {
        for(t=0;t<2;++t) {
            struct rf_tac_predictor provider;
            const struct rf_tac_predictor *service=NULL;
            rf_tac_observe(&m->world,t,&m->observations[t]);
            if(m->policies[t].solver==RF_TAC_BEAM) {
                if(!m->prediction[t])m->prediction[t]=rf_tac_prediction_create(&m->world,t,m->policies[t].budget);
                else if(!rf_tac_prediction_reset(m->prediction[t],&m->world,t,m->policies[t].budget))return -1;
                if(!m->prediction[t])return -1;
                rf_tac_prediction_provider(m->prediction[t],&provider);service=&provider;
                rf_tac_solve_with_predictor(&m->observations[t],&m->policies[t],service,&m->plans[t],&m->traces[t]);
            } else rf_tac_solve(&m->observations[t],&m->policies[t],&m->plans[t],&m->traces[t]);
        }
        for(t=0;t<2;++t)if(!rf_tac_apply(&m->world,&m->plans[t]))return -1;
    }
    m->previous=m->world;
    rf_tac_step(&m->world);
    if(m->world.finished)m->running=0;
    return 1;
}
void rf_range_reset(struct rf_range_lab *r,int weapon,int mode,int lane,int target,unsigned seed)
{
    int s,l;
    memset(r,0,sizeof(*r));r->weapon=weapon;r->mode=mode;r->lane=lane;r->target=target;
    for(s=0;s<2;++s) {
        r->rng[s]=seed^(s?0x9e3779b9u:0);
        rf_tw_state_reset(rf_tw_profile_get(weapon),&r->weapons[s]);
        for(l=0;l<RF_RANGE_LANES;++l) {r->stats[s][l].hp=RF_TW_BASE_HP;r->stats[s][l].risk=RF_TW_BASE_RISK;}
    }
}
int rf_range_fire(struct rf_range_lab *r,int shooter,int lane,float aim_x,float aim_y)
{
    return rf_range_fire_distance(r,shooter,lane,aim_x,aim_y,
        lane>=0 && lane<RF_RANGE_LANES?(float)rf_range_distances[lane]:100);
}
int rf_range_fire_distance(struct rf_range_lab *r,int shooter,int lane,float aim_x,float aim_y,float distance)
{
    rf_tw_context c;rf_tw_shot shot;struct rf_range_stats *s;
    const rf_tw_profile *p=rf_tw_profile_get(r->weapon);
    int valid=lane>=0 && lane<RF_RANGE_LANES;
    if(shooter<0 || shooter>1 || !p)return 0;
    if(!isfinite(distance) || distance<=0)return 0;
    rf_tw_context_reset(&c);c.distance_m=distance;
    c.exposure=r->target==1?RF_TW_UPPER:r->target==2?RF_TW_HEAD:RF_TW_FULL;
    c.recoil_milli_mrad=r->weapons[shooter].recoil_milli_mrad;
    if(r->weapons[shooter].ammo<=0 || r->weapons[shooter].cooldown_ms || r->weapons[shooter].reload_remaining_ms)return 0;
    rf_tw_sample_aim(p,&c,&r->rng[shooter],aim_x,aim_y,&shot);
    if(!rf_tw_state_begin_shot(p,&r->weapons[shooter]))return 0;
    if(!valid)shot.hit=0;
    s=&r->stats[shooter][valid?lane:r->lane];++s->shots;
    s->distance_total+=distance;
    if(s->shots==1 || distance<s->distance_min)s->distance_min=distance;
    if(distance>s->distance_max)s->distance_max=distance;
    if(shot.hit) {
        float before=s->hp;
        ++s->hits;s->heads+=shot.head;
        s->absorbed+=rf_tw_apply_damage(&s->hp,&s->risk,shot.damage_milli/1000.0f,shot.risk_cost_milli/1000.0f);
        s->damage+=before-s->hp;s->recovery_ms=RF_TW_BASE_RECOVERY_DELAY_MS;
        if(s->hp<=0){++s->kills;s->last_ttk_ms=s->trial_ms;s->ttk_total_ms+=s->trial_ms;
            s->hp=RF_TW_BASE_HP;s->risk=RF_TW_BASE_RISK;s->trial_ms=0;}
    }
    {struct rf_range_mark *mark=&r->marks[r->mark_count++%RF_RANGE_MARKS];
     mark->x=shot.offset_x_m;mark->y=shot.offset_y_m;mark->lane=valid?lane:-1;mark->hit=shot.hit;mark->shooter=shooter;}
    return 1;
}
void rf_range_step(struct rf_range_lab *r,int dt)
{
    const rf_tw_profile *p=rf_tw_profile_get(r->weapon);
    for(int s=0;s<2;++s) {
        if(s==0 && !r->running)continue;
        rf_tw_state_advance(p,&r->weapons[s],dt);
        for(int l=0;l<RF_RANGE_LANES;++l) {
            struct rf_range_stats *v=&r->stats[s][l];
            if(v->shots){v->elapsed_ms+=dt;v->trial_ms+=dt;}
            rf_tw_recover(&v->risk,&v->recovery_ms,dt);
        }
    }
    if(!r->running)return;
    if(!r->weapons[0].ammo)rf_tw_state_begin_reload(p,&r->weapons[0]);
    r->wait_ms=r->wait_ms>dt?r->wait_ms-dt:0;
    if(!r->wait_ms && rf_range_fire(r,0,r->lane,0,0))
        r->wait_ms=rf_tw_pattern_delay_ms(p,r->mode,++r->pattern_shots);
}
int rf_tactical_lab_export(struct rf_tactical_lab *lab)
{
    static unsigned serial;
    FILE *f;
    snprintf(lab->output,sizeof(lab->output),"tactical-result-%lu-%u.json",(unsigned long)time(NULL),++serial);
    f=fopen(lab->output,"wb");if(!f){lab->output[0]=0;return 0;}
    fprintf(f,"{\"version\":%d,\"kind\":%d,\"seed\":%u,",RF_TAC_VERSION,lab->kind,lab->seed);
    if(lab->kind==1) {
        const struct rf_tac_world *w=&lab->match.world;
        fprintf(f,"\"map_seed\":%u,\"map_hash\":%u,\"hash\":\"%08x\",\"time_ms\":%d,\"finished\":%d,\"winner\":%d,\"captured\":[%d,%d],\"policies\":[",w->map->seed,w->map->content_hash,rf_tac_hash(w),w->time_ms,w->finished,w->winner,w->orders[0].captured,w->orders[1].captured);
        for(int t=0;t<2;++t){const struct rf_tac_policy *p=&lab->match.policies[t];
            fprintf(f,"%s{\"solver\":%d,\"budget\":%d,\"aggression\":%g,\"safety\":%g,\"progress\":%g,\"cover\":%g,\"focus\":%g,\"movement\":%g,\"beam_width\":%d,\"beam_branches\":%d,\"beam_horizon_ms\":%d}",t?",":"",p->solver,p->budget,p->aggression,p->safety,p->progress,p->cover,p->focus,p->movement,p->beam_width,p->beam_branches,p->beam_horizon_ms);}
        fputs("],\"units\":[",f);
        for(int i=0;i<w->squad_size*2;++i) {
            const struct rf_tac_unit *u=&w->units[(i/w->squad_size)*RF_TAC_MAX_SQUAD+i%w->squad_size];
            fprintf(f,"%s{\"id\":%d,\"shots\":%d,\"hits\":%d,\"damage\":%g,\"hp\":%g,\"risk\":%g}",i?",":"",u->id,u->shots,u->hits,u->damage,u->hp,u->evasion);
        }
    } else {
        fprintf(f,"\"weapon\":%d,\"mode\":%d,\"target\":%d,\"rows\":[",lab->range.weapon,lab->range.mode,lab->range.target);
        for(int i=0;i<RF_RANGE_LANES*2;++i) {
            const struct rf_range_stats *s=&lab->range.stats[i/RF_RANGE_LANES][i%RF_RANGE_LANES];
            fprintf(f,"%s{\"shooter\":%d,\"distance\":%d,\"shots\":%d,\"hits\":%d,\"heads\":%d,\"damage\":%g,\"absorbed\":%g,\"kills\":%d,\"elapsed_ms\":%d,\"ttk_total_ms\":%d,\"unfinished_trial_ms\":%d,\"actual_distance_mean\":%g,\"actual_distance_min\":%g,\"actual_distance_max\":%g}",i?",":"",i/RF_RANGE_LANES,rf_range_distances[i%RF_RANGE_LANES],s->shots,s->hits,s->heads,s->damage,s->absorbed,s->kills,s->elapsed_ms,s->ttk_total_ms,s->trial_ms,s->shots?s->distance_total/s->shots:0,s->distance_min,s->distance_max);
        }
    }
    fputs("]}\n",f);int ok=!ferror(f);if(fclose(f))ok=0;
    char csv[200];snprintf(csv,sizeof(csv),"%.*s.csv",(int)strlen(lab->output)-5,lab->output);
    f=fopen(csv,"wb");if(!f)return 0;
    if(lab->kind==1) {
        fputs("team,id,weapon,alive,hp,risk,shots,hits,raw_damage,absorbed\n",f);
        const struct rf_tac_world *w=&lab->match.world;
        for(int t=0;t<2;++t)for(int i=0;i<w->squad_size;++i){const struct rf_tac_unit *u=&w->units[t*RF_TAC_MAX_SQUAD+i];
            fprintf(f,"%d,%d,%d,%d,%g,%g,%d,%d,%g,%g\n",t,u->id,u->weapon,u->alive,u->hp,u->evasion,u->shots,u->hits,u->damage,u->absorbed);}
    } else {
        fputs("shooter,target_lane_m,shots,hits,heads,hp_damage,absorbed,kills,elapsed_ms,ttk_total_ms,unfinished_trial_ms,actual_distance_mean,actual_distance_min,actual_distance_max\n",f);
        for(int t=0;t<2;++t)for(int i=0;i<RF_RANGE_LANES;++i){const struct rf_range_stats *s=&lab->range.stats[t][i];
            fprintf(f,"%d,%d,%d,%d,%d,%g,%g,%d,%d,%d,%d,%g,%g,%g\n",t,rf_range_distances[i],s->shots,s->hits,s->heads,s->damage,s->absorbed,s->kills,s->elapsed_ms,s->ttk_total_ms,s->trial_ms,s->shots?s->distance_total/s->shots:0,s->distance_min,s->distance_max);}
    }
    if(ferror(f))ok=0;
    if(fclose(f))ok=0;
    return ok;
}
