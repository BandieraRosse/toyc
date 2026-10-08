#include "rf_ai.h"
#include <math.h>
#include <string.h>

/* M0 decisions live here, never in data extraction or the execution adapter. */
struct mechanical_state { unsigned turn; };
static float distance(struct rf_ai_point a,struct rf_ai_point b)
{float x=a.x-b.x,z=a.z-b.z;return sqrtf(x*x+z*z);}
static void mechanical_decide(void *state,const struct rf_ai_config *config,
    const struct rf_ai_snapshot *s,const struct rf_ai_services *api,struct rf_ai_plan *plan)
{
    struct mechanical_state *memory=state;
    if(s->completed)return;
    int first=(int)(memory->turn++%(unsigned)s->count);
    for(int n=0;n<s->count;++n){
        int i=(first+n)%s->count;
        const struct rf_ai_member *u=&s->members[i];struct rf_ai_action *a=&plan->actions[i];
        if(!u->alive)continue;
        /* Equal per-member allowance; low budgets rotate which members work. */
        int floor=api->remaining(api->context)-api->remaining(api->context)/(s->count-n);
        if(!api->spend(api->context,1))break;
        if(u->reload_ms || (u->reserve && u->ammo<=u->magazine*config->parameters[0])){
            a->kind=RF_AI_RELOAD;continue;
        }
        int candidates[RF_AI_MAX_MEMBERS],count=0;
        for(int j=0;j<s->enemy_count;++j){
            if(api->remaining(api->context)<=floor || !api->spend(api->context,1))break;
            if(!s->enemies[j].alive || !u->ammo || distance(u->position,s->enemies[j].position)>u->range_m)continue;
            int k=count;float d=distance(u->position,s->enemies[j].position);
            while(k && distance(u->position,s->enemies[candidates[k-1]].position)>d){candidates[k]=candidates[k-1];--k;}
            candidates[k]=j;++count;
        }
        /* Defense returns to its area before taking distant engagements.
         * Attack stops for a visible opponent. Neither policy pursues ghosts. */
        int return_to_area=s->command==RF_AI_DEFEND && distance(u->position,s->objective)>s->radius+config->parameters[1];
        for(int k=0;k<count && k<2 && !return_to_area;++k){
            if(api->remaining(api->context)-3<floor+3)break;
            struct rf_ai_query q={0};struct rf_ai_query_result r;
            q.kind=RF_AI_SHOT;q.member=i;q.enemy=candidates[k];
            if(api->query(api->context,&q,&r)==RF_AI_YES){a->kind=RF_AI_FIRE;
                a->target=s->enemies[candidates[k]].identity;break;}
        }
        if(a->kind==RF_AI_FIRE)continue;
        if(distance(u->position,s->objective)<=s->radius*0.70f)continue;
        /* Destination is an algorithm choice, not one of eight host candidates. */
        if(u->previous.kind==RF_AI_MOVE && u->feedback==RF_AI_RUNNING){*a=u->previous;continue;}
        if(api->remaining(api->context)-3<floor)continue;
        float angle=6.283185307f*i/s->count;
        struct rf_ai_query q={0};struct rf_ai_query_result r;
        q.kind=RF_AI_ROUTE;q.member=i;q.destination=s->objective;
        q.max_work=api->remaining(api->context)-floor;
        q.destination.x+=cosf(angle)*fminf(1.5f,s->radius*0.3f);
        q.destination.z+=sinf(angle)*fminf(1.5f,s->radius*0.3f);
        if(api->query(api->context,&q,&r)==RF_AI_YES && distance(r.destination,s->objective)<s->radius){
            a->kind=RF_AI_MOVE;a->destination=r.waypoint;
        }
    }
}
static const struct rf_ai_parameter parameters[]={
    {"reload_fraction",0,0,0.8f},{"defense_leash_m",4,0,16}
};
const struct rf_ai_algorithm rf_ai_mechanical_algorithm={
    "mechanical-v3",2,sizeof(struct mechanical_state),2,parameters,NULL,mechanical_decide,NULL
};
