#ifndef _WIN32
#define _POSIX_C_SOURCE 200809L
#endif
#include "rf_ai.h"
#include <string.h>
#include <math.h>
#include <time.h>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
double rf_ai_clock_seconds(void)
{
#ifdef _WIN32
    LARGE_INTEGER now,frequency;QueryPerformanceFrequency(&frequency);QueryPerformanceCounter(&now);
    return (double)now.QuadPart/(double)frequency.QuadPart;
#else
    struct timespec now;clock_gettime(CLOCK_MONOTONIC,&now);return now.tv_sec+now.tv_nsec*1e-9;
#endif
}

extern const struct rf_ai_algorithm rf_ai_mechanical_algorithm;
static const struct rf_ai_algorithm *const algorithms[]={&rf_ai_mechanical_algorithm};
int rf_ai_algorithm_count(void){return (int)(sizeof(algorithms)/sizeof(*algorithms));}
const struct rf_ai_algorithm *rf_ai_algorithm_at(int index)
{return index>=0 && index<rf_ai_algorithm_count()?algorithms[index]:NULL;}
const struct rf_ai_algorithm *rf_ai_algorithm_find(const char *name)
{
    if(!name)return NULL;
    for(unsigned i=0;i<sizeof(algorithms)/sizeof(*algorithms);++i)
        if(!strcmp(name,algorithms[i]->name))return algorithms[i];
    return NULL;
}
void rf_ai_config_default(const struct rf_ai_algorithm *a,struct rf_ai_config *c)
{
    memset(c,0,sizeof(*c));c->version=a->version;
    for(int i=0;i<a->parameter_count;++i)c->parameters[i]=a->parameters[i].initial;
}
void rf_ai_instance_destroy(struct rf_ai_instance *i)
{
    if(i->initialized && i->algorithm->destroy)i->algorithm->destroy(i->state.bytes);
    memset(i,0,sizeof(*i));
}
int rf_ai_instance_init(struct rf_ai_instance *i,const struct rf_ai_algorithm *a,const struct rf_ai_config *c)
{
    if(!i || !a || !a->decide || a->state_bytes<0 || a->state_bytes>RF_AI_STATE_BYTES ||
        a->parameter_count<0 || a->parameter_count>RF_AI_MAX_PARAMETERS)return 0;
    struct rf_ai_config defaults;if(!c){rf_ai_config_default(a,&defaults);c=&defaults;}
    if(c->version!=a->version)return 0;
    for(int k=0;k<a->parameter_count;++k)if(!isfinite(c->parameters[k]) ||
        c->parameters[k]<a->parameters[k].minimum || c->parameters[k]>a->parameters[k].maximum)return 0;
    rf_ai_instance_destroy(i);i->algorithm=a;i->config=*c;i->initialized=1;
    if(a->reset)a->reset(i->state.bytes);
    return 1;
}
void rf_ai_plan_hold(const struct rf_ai_snapshot *s,struct rf_ai_plan *p)
{
    memset(p,0,sizeof(*p));p->version=RF_AI_API_VERSION;p->generation=s->generation;
    p->team=s->team;p->time_ms=s->time_ms;p->count=s->count;
    for(int k=0;k<s->count;++k){p->actions[k].member=s->members[k].identity;
        p->actions[k].target.id=-1;p->actions[k].destination=s->members[k].position;}
}
struct ai_call {
    int limit;rf_ai_query_backend backend;void *context;struct rf_ai_stats *stats;
};
static int ai_spend(void *opaque,int n)
{
    struct ai_call *c=opaque;
    if(n<0 || n>c->limit-c->stats->work){c->stats->exhausted=1;return 0;}
    c->stats->work+=n;return 1;
}
static int ai_remaining(const void *opaque)
{const struct ai_call *c=opaque;return c->limit-c->stats->work;}
static int ai_query(void *opaque,const struct rf_ai_query *q,struct rf_ai_query_result *r)
{
    struct ai_call *c=opaque;memset(r,0,sizeof(*r));r->answer=RF_AI_UNKNOWN;
    if(!q || q->kind<RF_AI_SHOT || q->kind>RF_AI_CONNECTION)return RF_AI_UNKNOWN;
    int cost=q->kind==RF_AI_ROUTE?8:2;
    if(!ai_spend(c,cost)){++c->stats->unknown;return RF_AI_UNKNOWN;}
    ++c->stats->queries;double start=rf_ai_clock_seconds();
    int result=c->backend?c->backend(c->context,q,r):RF_AI_UNKNOWN;
    c->stats->query_seconds+=rf_ai_clock_seconds()-start;
    if(result!=RF_AI_NO && result!=RF_AI_YES){result=RF_AI_UNKNOWN;++c->stats->unknown;}
    r->answer=result;return result;
}
int rf_ai_decide(struct rf_ai_instance *i,const struct rf_ai_snapshot *s,int budget,
    rf_ai_query_backend backend,void *context,struct rf_ai_plan *p,struct rf_ai_stats *stats)
{
    if(!s || !p || !stats || s->version!=RF_AI_API_VERSION || s->count<1 ||
        s->count>RF_AI_MAX_MEMBERS || s->enemy_count<0 || s->enemy_count>RF_AI_MAX_MEMBERS || budget<0 || budget>100000)return 0;
    memset(stats,0,sizeof(*stats));rf_ai_plan_hold(s,p);
    if(!i || !i->initialized)return 0;
    if(!i->generation_known || i->generation!=s->generation){
        if(i->generation_known){
            if(i->algorithm->destroy)i->algorithm->destroy(i->state.bytes);
            memset(i->state.bytes,0,sizeof(i->state.bytes));
            if(i->algorithm->reset)i->algorithm->reset(i->state.bytes);
        }
        i->generation=s->generation;i->generation_known=1;
    }
    if(!budget){stats->exhausted=1;return 1;}
    struct ai_call call={budget,backend,context,stats};
    struct rf_ai_services services={&call,ai_spend,ai_remaining,ai_query};
    double start=rf_ai_clock_seconds();i->algorithm->decide(i->state.bytes,&i->config,s,&services,p);
    stats->decision_seconds=rf_ai_clock_seconds()-start;
    /* Structural validation is independent of any particular algorithm. */
    if(p->version!=RF_AI_API_VERSION || p->generation!=s->generation || p->team!=s->team ||
       p->time_ms!=s->time_ms || p->count!=s->count){rf_ai_plan_hold(s,p);++stats->invalid_actions;return 1;}
    for(int k=0;k<s->count;++k){struct rf_ai_action *a=&p->actions[k];
        int legal=a->kind>=RF_AI_HOLD && a->kind<=RF_AI_RELOAD &&
            a->member.id==s->members[k].identity.id && a->member.generation==s->members[k].identity.generation;
        if(a->kind==RF_AI_MOVE)legal=legal && isfinite(a->destination.x) && isfinite(a->destination.z) &&
            a->destination.x>=s->bounds_min.x && a->destination.x<=s->bounds_max.x &&
            a->destination.z>=s->bounds_min.z && a->destination.z<=s->bounds_max.z;
        if(a->kind==RF_AI_FIRE){int found=0;for(int j=0;j<s->enemy_count;++j)
            found|=s->enemies[j].alive && a->target.id==s->enemies[j].identity.id &&
                a->target.generation==s->enemies[j].identity.generation;legal=legal && found;}
        if(!legal || !s->members[k].alive){if(!legal)++stats->invalid_actions;
            memset(a,0,sizeof(*a));a->member=s->members[k].identity;a->target.id=-1;a->destination=s->members[k].position;}
    }
    return 1;
}
