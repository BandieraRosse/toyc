#include "rf_ai.h"
#include <math.h>
#include <string.h>

/* Normal difficulty uses only the public facts and metered geometry. It has
 * no faster body, hidden accuracy bonus, Game pointer, or privileged RNG. */
struct normal_state { unsigned turn; int move_until[RF_AI_MAX_MEMBERS]; };
struct normal_position { float fire, threat; int target; };
static float normal_distance(struct rf_ai_point a,struct rf_ai_point b)
{float x=a.x-b.x,z=a.z-b.z;return sqrtf(x*x+z*z);}
static float normal_dps(const struct rf_ai_member *u,float d)
{
    if(!u->alive || !u->ammo || d>u->range_m)return 0;
    float spread=u->spread_tangent*d;
    /* Cheap ranking approximation, not a second damage simulator. */
    float accuracy=1.0f/(1.0f+spread*spread/0.09f);
    float multiplier=1;
    if(u->falloff_m[3]>0){
        multiplier=u->falloff_multiplier[3];
        if(d<=u->falloff_m[0])multiplier=u->falloff_multiplier[0];
        else for(int k=1;k<4;++k)if(d<=u->falloff_m[k]){
            float f=(d-u->falloff_m[k-1])/(u->falloff_m[k]-u->falloff_m[k-1]);
            multiplier=u->falloff_multiplier[k-1]+f*(u->falloff_multiplier[k]-u->falloff_multiplier[k-1]);break;
        }
    }
    return u->shot_damage*1000.0f/fmaxf(1,u->fire_interval_ms)*accuracy*multiplier;
}
static float normal_readiness(const struct rf_ai_member *u,struct rf_ai_point destination)
{
    float dx=destination.x-u->position.x,dz=destination.z-u->position.z;
    float d=sqrtf(dx*dx+dz*dz),angle=0;
    if(d>0){float dot=(u->facing.x*dx+u->facing.z*dz)/d;angle=acosf(fmaxf(-1,fminf(1,dot)))*57.29578f;}
    float delay=u->reload_ms*0.001f+angle/fmaxf(1,u->turn_degrees)+
        fmaxf(0,u->aim_required_ms-u->aim_ms)*0.001f;
    return fmaxf(0.05f,1-delay/2.0f);
}
static int normal_exposure(const struct rf_ai_snapshot *s,const struct rf_ai_services *api,
    int member,const struct rf_ai_route *route,int samples,int cap,struct rf_ai_query_result *r)
{
    struct rf_ai_query q={0};q.kind=RF_AI_EXPOSURE;q.member=member;
    q.route=route;q.sample_count=samples;q.max_work=cap;
    (void)s;return api->query(api->context,&q,r)==RF_AI_YES;
}
static struct rf_ai_route normal_point_route(const struct rf_ai_snapshot *s,int member,struct rf_ai_point p)
{
    struct rf_ai_route r={0};r.generation=s->generation;r.map_generation=s->map_generation;
    r.time_ms=s->time_ms;r.team=s->team;r.member=member;r.count=2;r.points[0]=r.points[1]=p;return r;
}
static float normal_threat(const struct rf_ai_snapshot *s,struct rf_ai_point p,unsigned visible)
{
    float value=0;
    for(int e=0;e<s->enemy_count;++e)if(visible&(1u<<e))
        value+=normal_dps(&s->enemies[e],normal_distance(p,s->enemies[e].position))*normal_readiness(&s->enemies[e],p);
    return value;
}
static int normal_evaluate(const struct rf_ai_snapshot *s,const struct rf_ai_services *api,
    int i,struct rf_ai_point p,const float *assigned,int floor,struct normal_position *out)
{
    const struct rf_ai_member *u=&s->members[i];
    struct rf_ai_route route=normal_point_route(s,i,p);struct rf_ai_query_result r;
    memset(out,0,sizeof(*out));out->target=-1;
    if(api->remaining(api->context)-floor<8+4*s->enemy_count)return 0;
    if(!normal_exposure(s,api,i,&route,2,api->remaining(api->context)-floor,&r))return 0;
    out->threat=normal_threat(s,p,r.samples[0].visible_enemies);
    for(int e=0;e<s->enemy_count;++e){
        const struct rf_ai_member *v=&s->enemies[e];
        if(!api->spend(api->context,1))return 0;
        if(!v->alive || !u->ammo)continue;
        if(api->remaining(api->context)-floor<3)return 0;
        struct rf_ai_query q={0};q.kind=RF_AI_SHOT;q.member=i;q.enemy=e;q.hypothetical=1;q.from=p;
        q.max_work=api->remaining(api->context)-floor;
        int answer=api->query(api->context,&q,&r);
        if(answer==RF_AI_UNKNOWN)return 0;
        if(answer!=RF_AI_YES)continue;
        float health=v->health+v->evasion;
        float dps=normal_dps(u,normal_distance(p,v->position));
        /* Prefer finishing vulnerable targets; enough assigned fire reduces
         * the value of sending everybody after the same last few HP. */
        float score=dps*(1+80/fmaxf(20,health-assigned[e]));
        score*=1+fminf(2,assigned[e]/40);
        int retained=u->previous.kind==RF_AI_FIRE && u->previous.target.id==v->identity.id &&
            u->previous.target.generation==v->identity.generation;
        if(retained)score=dps*(1+80/fmaxf(20,health))*3;
        else if(assigned[e]>=health)score=dps*0.12f;
        if(score>out->fire){out->fire=score;out->target=e;}
    }
    return 1;
}
static float normal_spacing(const struct rf_ai_snapshot *s,const struct rf_ai_plan *p,int i,struct rf_ai_point at)
{
    float penalty=0;
    for(int k=0;k<s->count;++k)if(k!=i && s->members[k].alive){
        struct rf_ai_point other=p->actions[k].kind==RF_AI_MOVE?p->actions[k].destination:s->members[k].position;
        penalty+=fmaxf(0,2-normal_distance(at,other))*5;
    }
    return penalty;
}
static void normal_decide(void *state,const struct rf_ai_config *cfg,const struct rf_ai_snapshot *s,
    const struct rf_ai_services *api,struct rf_ai_plan *p)
{
    struct normal_state *memory=state;
    float assigned[RF_AI_MAX_MEMBERS]={0};int ready=0,reloading=0,enemies=0;
    if(s->completed || !s->count)return;
    for(int e=0;e<s->enemy_count;++e)enemies+=s->enemies[e].alive!=0;
    for(int i=0;i<s->count;++i)if(s->members[i].alive){
        ready+=s->members[i].ammo>0 && !s->members[i].reload_ms;
        reloading+=s->members[i].reload_ms>0;
    }
    int first=api->remaining(api->context)<1024?(int)(memory->turn%(unsigned)s->count):0;
    ++memory->turn;
    for(int n=0;n<s->count;++n){
        int i=(first+n)%s->count;const struct rf_ai_member *u=&s->members[i];struct rf_ai_action *a=&p->actions[i];
        int remaining=api->remaining(api->context),floor=remaining-remaining/(s->count-n);
        if(!u->alive)continue;
        if(!api->spend(api->context,1))break;
        if(u->reload_ms || (!u->ammo && u->reserve!=0)){
            a->kind=RF_AI_RELOAD;if(!u->reload_ms)++reloading;continue;
        }
        if(!enemies){
            if(normal_distance(u->position,s->objective)>s->radius*0.65f){
                struct rf_ai_query q={0};struct rf_ai_query_result r;
                q.kind=RF_AI_ROUTE;q.member=i;q.destination=s->objective;
                q.max_work=api->remaining(api->context)-floor;
                if(q.max_work>0 && api->query(api->context,&q,&r)==RF_AI_YES){
                    a->kind=RF_AI_MOVE;a->destination=r.waypoint;
                }
            }else if(u->reserve!=0 && u->ammo<u->magazine)a->kind=RF_AI_RELOAD;
            continue;
        }
        struct normal_position current;
        if(!normal_evaluate(s,api,i,u->position,assigned,floor,&current))continue;
        int low_ammo=u->reserve!=0 && u->ammo<=u->magazine*cfg->parameters[0];
        if(u->reserve!=0 && u->ammo<u->magazine &&
           ((current.threat<1 && u->ammo<=u->magazine/2) ||
            (low_ammo && !reloading && ready>1 && u->ammo>0))){
            a->kind=RF_AI_RELOAD;++reloading;if(u->ammo>0)--ready;continue;
        }
        int need_cover=low_ammo || u->evasion<u->max_evasion*0.3f || u->health<u->max_health*0.5f;
        float safety=cfg->parameters[1]*(need_cover?1.6f:1);
        float objective_distance=normal_distance(u->position,s->objective);
        int return_to_area=s->command==RF_AI_DEFEND && objective_distance>s->radius+4;
        float best=current.fire*0.25f-current.threat*safety-normal_spacing(s,p,i,u->position);
        if(current.target>=0 && !return_to_area){a->kind=RF_AI_FIRE;a->target=s->enemies[current.target].identity;}
        /* A short commitment keeps an accepted movement from oscillating every
         * decision. Re-evaluate immediately if the executor reports blockage. */
        if(a->kind!=RF_AI_FIRE && u->previous.kind==RF_AI_MOVE && u->feedback==RF_AI_RUNNING &&
            s->time_ms<memory->move_until[i]){*a=u->previous;continue;}
        /* Once engaged, finish the firing action. Reposition primarily
         * while seeking a firing lane; repeated movement discards aim time. */
        if(a->kind==RF_AI_FIRE){
            assigned[current.target]+=normal_dps(u,normal_distance(u->position,
                s->enemies[current.target].position))*0.25f;
            continue;
        }
        struct rf_ai_point candidates[14];int count=0;
        float angle=6.283185307f*i/s->count;
        candidates[count]=s->objective;
        candidates[count].x+=cosf(angle)*fminf(2,s->radius*0.45f);
        candidates[count++].z+=sinf(angle)*fminf(2,s->radius*0.45f);
        for(int k=0;k<8;++k){
            float theta=6.283185307f*k/8;
            candidates[count].x=u->position.x+cosf(theta)*4;
            candidates[count++].z=u->position.z+sinf(theta)*4;
        }
        /* Nearby nav facts add corners which a fixed radial stencil misses. */
        int nodes[4]={-1,-1,-1,-1};float ranks[4]={1e20f,1e20f,1e20f,1e20f};
        for(int k=0;k<s->navigation_count;++k){
            if(k%32==0 && (api->remaining(api->context)<=floor || !api->spend(api->context,1)))break;
            struct rf_ai_point at=s->navigation[k].position;
            float distance=normal_distance(u->position,at);
            if(distance<1 || distance>12)continue;
            float rank=distance+0.5f*normal_distance(at,s->objective);
            for(int j=0;j<4;++j)if(rank<ranks[j]){
                for(int q=3;q>j;--q){ranks[q]=ranks[q-1];nodes[q]=nodes[q-1];}
                ranks[j]=rank;nodes[j]=k;break;
            }
        }
        for(int k=0;k<4;++k)if(nodes[k]>=0)candidates[count++]=s->navigation[nodes[k]].position;
        for(int k=0;k<count;++k){
            struct rf_ai_point at=candidates[k];
            if(at.x<s->bounds_min.x || at.z<s->bounds_min.z || at.x>s->bounds_max.x || at.z>s->bounds_max.z)continue;
            float destination_objective=normal_distance(at,s->objective);
            if(s->command==RF_AI_DEFEND && destination_objective>s->radius*0.9f)continue;
            if(normal_distance(u->position,at)<0.7f)continue;
            struct normal_position candidate;
            if(!normal_evaluate(s,api,i,at,assigned,floor,&candidate))break;
            float progress=fmaxf(0,objective_distance-s->radius*0.65f)-fmaxf(0,destination_objective-s->radius*0.65f);
            float value=candidate.fire*0.25f-candidate.threat*safety+progress*cfg->parameters[2]-normal_spacing(s,p,i,at);
            if(value<best+3 && !return_to_area)continue;
            int available=api->remaining(api->context)-floor;
            if(available<100)break;
            struct rf_ai_query q={0};struct rf_ai_query_result route_result,exposure;
            q.kind=RF_AI_ROUTE;q.member=i;q.destination=at;
            q.max_work=available>3500?3500:available;
            if(api->query(api->context,&q,&route_result)!=RF_AI_YES)continue;
            available=api->remaining(api->context)-floor;
            if(available<1)break;
            if(!normal_exposure(s,api,i,&route_result.route,8,available,&exposure))continue;
            float risk=0;
            for(int j=0;j<exposure.sample_count;++j)risk+=normal_threat(s,exposure.samples[j].position,exposure.samples[j].visible_enemies);
            float travel=route_result.distance/4.5f;
            value-=travel*(2+current.fire*0.15f)+risk/exposure.sample_count*fminf(3,travel)*0.10f;
            if(return_to_area)value+=progress*4;
            if(value<=best+3)continue;
            best=value;a->kind=RF_AI_MOVE;a->destination=route_result.waypoint;
        }
        if(a->kind==RF_AI_MOVE)memory->move_until[i]=s->time_ms+600;
        else if(a->kind==RF_AI_FIRE){
            for(int e=0;e<s->enemy_count;++e)if(a->target.id==s->enemies[e].identity.id)
                assigned[e]+=normal_dps(u,normal_distance(u->position,s->enemies[e].position))*0.25f;
        }else if(!u->ammo && u->reserve!=0){a->kind=RF_AI_RELOAD;++reloading;}
    }
}
static const struct rf_ai_parameter normal_parameters[]={
    {"reload_fraction",0.1f,0,0.6f},{"exposure_weight",0.30f,0.05f,2},
    {"objective_weight",2,0.1f,8}
};
const struct rf_ai_algorithm rf_ai_normal_algorithm={
    "normal",1,sizeof(struct normal_state),3,normal_parameters,NULL,normal_decide,NULL
};
