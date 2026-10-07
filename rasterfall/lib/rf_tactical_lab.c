#include "rf_tactical_lab.h"
#include "rf_tactical_beam.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>
#include "rasterfall_character.h"
#include "rasterfall_units.h"
#include <time.h>

const int rf_range_distances[RF_RANGE_LANES]={5,10,15,20,30,40,60,80,100};

void rf_tac_match_destroy(struct rf_tac_match *m)
{
    int t;
    if(!m)return;
    for(t=0;t<2;++t) { rf_tac_prediction_destroy(m->prediction[t]);m->prediction[t]=NULL;rf_ai_host_destroy(&m->ai[t]); }
    rf_tac_world_destroy(&m->world);rf_tac_map_destroy(&m->map);
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
    if(m->bound_game && (!rf_tac_map_bind(&m->map,m->bound_game) ||
        !rf_tac_world_bind(&m->world,m->bound_game))) return 0;
    for(t=0;t<2;++t) {
        m->policies[t]=policies[t];
        rf_tac_command(&m->world,t,t?RF_TAC_DEFEND:RF_TAC_ATTACK,m->map.objective,m->map.objective_radius);
    }
    memset(m->observations,0,sizeof(m->observations));
    memset(m->plans,0,sizeof(m->plans));memset(m->traces,0,sizeof(m->traces));
    m->previous=m->world;m->ready=1;m->running=0;m->remainder_ms=0;
    return 1;
}
int rf_tac_match_prepare(struct rf_tac_match *m)
{
    int t;
    if(!m || !m->ready || m->world.finished)return 0;
    if(m->world.time_ms%RF_TAC_THINK_MS<RF_TAC_DT_MS) {
        for(t=0;t<2;++t) {
            if(m->policies[t].solver==RF_TAC_M0){
                if(!rf_ai_host_decide(&m->ai[t],&m->world,t,&m->policies[t],&m->plans[t],&m->observations[t],&m->traces[t]))return -1;
                continue;
            }
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
    rf_tac_prepare(&m->world,0);
    return 1;
}
int rf_tac_match_step(struct rf_tac_match *m)
{
    if(rf_tac_match_prepare(m)<=0) return 0;
    m->world.game->event_count=0;
    toy_game_update_world(m->world.game,RF_TAC_DT_MS);
    rf_tac_finish(&m->world,RF_TAC_DT_MS);
    if(m->world.finished)m->running=0;
    return 1;
}
#include "rf_range_game.inc"

int rf_tactical_lab_export(struct rf_tactical_lab *lab)
{
    static unsigned serial;
    FILE *f;
    snprintf(lab->output,sizeof(lab->output),"tactical-result-%lu-%u.json",(unsigned long)time(NULL),++serial);
    f=fopen(lab->output,"wb");if(!f){lab->output[0]=0;return 0;}
    fprintf(f,"{\"version\":%d,\"authority\":\"toy_game\",\"dt_ms\":%d,\"kind\":%d,\"seed\":%u,",RF_TAC_VERSION,RF_TAC_DT_MS,lab->kind,lab->seed);
    if(lab->kind==1) {
        const struct rf_tac_world *w=&lab->match.world;
        fprintf(f,"\"map_seed\":%u,\"map_hash\":%u,\"hash\":\"%08x\",\"time_ms\":%d,\"finished\":%d,\"winner\":%d,\"captured\":[%d,%d],\"policies\":[",w->map->seed,w->map->content_hash,rf_tac_hash(w),w->time_ms,w->finished,w->winner,w->orders[0].captured,w->orders[1].captured);
        for(int t=0;t<2;++t){const struct rf_tac_policy *p=&lab->match.policies[t];
            if(p->solver==RF_TAC_M0){
                fprintf(f,"%s{\"solver\":%d,\"algorithm\":\"%s\",\"ai_api_version\":%d,\"algorithm_version\":%d,\"budget\":%d,\"parameters\":{",
                    t?",":"",p->solver,p->algorithm->name,RF_AI_API_VERSION,p->ai.version,p->budget);
                for(int k=0;k<p->algorithm->parameter_count;++k)
                    fprintf(f,"%s\"%s\":%g",k?",":"",p->algorithm->parameters[k].name,p->ai.parameters[k]);
                fputs("}}",f);continue;
            }
            fprintf(f,"%s{\"solver\":%d,\"budget\":%d,\"aggression\":%g,\"safety\":%g,\"progress\":%g,\"cover\":%g,\"focus\":%g,\"movement\":%g,\"beam_width\":%d,\"beam_branches\":%d,\"beam_horizon_ms\":%d}",t?",":"",p->solver,p->budget,p->aggression,p->safety,p->progress,p->cover,p->focus,p->movement,p->beam_width,p->beam_branches,p->beam_horizon_ms);}
        fputs("],\"units\":[",f);
        for(int i=0;i<w->squad_size*2;++i) {
            const struct rf_tac_unit *u=&w->units[(i/w->squad_size)*RF_TAC_MAX_SQUAD+i%w->squad_size];
            fprintf(f,"%s{\"id\":%d,\"shots\":%d,\"hits\":%d,\"damage\":%g,\"hp\":%g,\"risk\":%g}",i?",":"",u->id,u->shots,u->hits,u->damage,u->hp,u->evasion);
        }
    } else {
        fprintf(f,"\"weapon\":%d,\"mode\":%d,\"target\":%d,\"missed_events\":%d,\"rows\":[",lab->range.weapon,lab->range.mode,lab->range.target,lab->range.missed_events);
        for(int i=0;i<RF_RANGE_LANES*2;++i) {
            const struct rf_range_stats *s=&lab->range.stats[i/RF_RANGE_LANES][i%RF_RANGE_LANES];
            fprintf(f,"%s{\"shooter\":%d,\"distance\":%d,\"shots\":%d,\"hits\":%d,\"heads\":%d,\"damage\":%g,\"absorbed\":%g,\"kills\":%d,\"elapsed_ms\":%d,\"ttk_total_ms\":%d,\"unfinished_trial_ms\":%d,\"actual_distance_mean\":%g,\"actual_distance_min\":%g,\"actual_distance_max\":%g}",i?",":"",i/RF_RANGE_LANES,rf_range_distances[i%RF_RANGE_LANES],s->shots,s->hits,s->heads,s->damage,s->absorbed,s->kills,s->elapsed_ms,s->ttk_total_ms,s->trial_ms,s->shots?s->distance_total/s->shots:0,s->distance_min,s->distance_max);
        }
    }
    fputs("]}\n",f);int ok=!ferror(f);if(fclose(f))ok=0;
    char csv[200];snprintf(csv,sizeof(csv),"%.*s.csv",(int)strlen(lab->output)-5,lab->output);
    f=fopen(csv,"wb");if(!f)return 0;
    if(lab->kind==1) {
        fputs("team,id,weapon,alive,hp,risk,shots,hits,health_damage,absorbed\n",f);
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
