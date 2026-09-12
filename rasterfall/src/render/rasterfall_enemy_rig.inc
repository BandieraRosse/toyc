/* Enemy Procedural Rig V1. Catalog -> truth adapter -> pure pose -> generic
 * rigid submission. Included here to reuse the established primitive/death
 * path, without exposing renderer globals to gameplay or a new asset format. */
#include "rasterfall_enemy_rig.h"
static const int enemy_rig_parent[ER_COUNT] = {
    -1, ER_ROOT, ER_TORSO, ER_TORSO, ER_ARM_L, ER_FOREARM_L,
    ER_TORSO, ER_ARM_R, ER_FOREARM_R, ER_ROOT, ER_ROOT
};
#define RP(c,x,y,z,hx,hy,hz,color) {ER_##c,x,y,z,hx,hy,hz,color}
static const struct enemy_rig_part smoker_rig_parts[] = {
    RP(TORSO,0,80,0,105,265,90,0x68715A),
    RP(TORSO,-110,245,-25,100,145,125,0x847555),
    RP(HEAD,0,65,20,83,115,90,0xA6A080),
    RP(HEAD,0,15,113,40,28,12,0x32282A),
    RP(ARM_L,0,-170,0,48,185,53,0x727B62),
    RP(FOREARM_L,0,-175,0,37,190,42,0x92937A),
    RP(HAND_L,0,-45,15,44,65,45,0x92937A),
    RP(ARM_R,0,-170,0,40,185,43,0x68715A),
    RP(FOREARM_R,0,-175,0,32,190,36,0x92937A),
    RP(HAND_R,0,-45,15,40,65,42,0x92937A),
    RP(LEG_L,0,-295,0,48,295,57,0x393D38),
    RP(LEG_R,0,-295,0,48,295,57,0x393D38)
};
static const struct enemy_rig_part charger_rig_parts[] = {
    RP(TORSO,0,45,0,205,230,140,0x8B735A),
    RP(HEAD,0,40,35,90,100,93,0xAE9874),
    RP(HEAD,0,35,132,52,15,8,0x352B26),
    RP(ARM_L,0,-75,0,180,205,175,0xA78B68),
    RP(FOREARM_L,0,-155,15,175,195,165,0xB29A77),
    RP(HAND_L,0,-60,35,195,145,185,0x9B8366),
    RP(ARM_R,0,-125,0,45,140,48,0x887760),
    RP(FOREARM_R,0,-125,0,35,140,40,0xAD9875),
    RP(HAND_R,0,-25,10,42,55,48,0xAD9875),
    RP(LEG_L,0,-205,0,82,205,85,0x48463B),
    RP(LEG_R,0,-205,0,82,205,85,0x48463B)
};
static const struct enemy_rig_part tank_rig_parts[] = {
    RP(TORSO,0,105,0,370,250,210,0x817E61),
    RP(TORSO,0,-135,0,265,140,175,0x686A54),
    RP(HEAD,0,35,65,86,86,88,0xABA184),
    RP(HEAD,0,20,155,52,16,10,0x302C29),
    RP(ARM_L,0,-105,0,170,210,175,0x939075),
    RP(FOREARM_L,0,-160,10,175,185,170,0x9E987A),
    RP(HAND_L,0,-55,35,195,140,195,0x89866A),
    RP(ARM_R,0,-105,0,170,210,175,0x939075),
    RP(FOREARM_R,0,-160,10,175,185,170,0x9E987A),
    RP(HAND_R,0,-55,35,195,140,195,0x89866A),
    RP(LEG_L,0,-210,0,135,210,145,0x41483C),
    RP(LEG_R,0,-210,0,135,210,145,0x41483C)
};
#undef RP
/* Parent-relative pivots. Geometry belongs exclusively to this catalog. */
static const struct enemy_rig_profile enemy_rig_profiles[] = {
    { TOY_GAME_ENEMY_SMOKER,
      {[ER_ROOT]={0,-300,0,0,0,0}, [ER_TORSO]={0,0,0,0,0,0},
       [ER_HEAD]={0,340,80,0,0,0},
       [ER_ARM_L]={-135,285,0,0,0,0}, [ER_FOREARM_L]={0,-350,0,0,0,0},
       [ER_HAND_L]={0,-350,0,0,0,0},
       [ER_ARM_R]={135,285,0,0,0,0}, [ER_FOREARM_R]={0,-350,0,0,0,0},
       [ER_HAND_R]={0,-350,0,0,0,0},
       [ER_LEG_L]={-62,0,0,0,0,0}, [ER_LEG_R]={62,0,0,0,0,0}},
      smoker_rig_parts, sizeof(smoker_rig_parts)/sizeof(smoker_rig_parts[0]), 920,18,8,5,1250 },
    { TOY_GAME_ENEMY_CHARGER,
      {[ER_ROOT]={0,-490,0,0,0,0}, [ER_TORSO]={0,160,0,0,0,0},
       [ER_HEAD]={55,280,65,0,0,0},
       [ER_ARM_L]={-310,235,0,0,0,0}, [ER_FOREARM_L]={0,-270,0,0,0,0},
       [ER_HAND_L]={0,-310,0,0,0,0},
       [ER_ARM_R]={240,170,0,0,0,0}, [ER_FOREARM_R]={0,-250,0,0,0,0},
       [ER_HAND_R]={0,-250,0,0,0,0},
       [ER_LEG_L]={-110,0,0,0,0,0}, [ER_LEG_R]={110,0,0,0,0,0}},
      charger_rig_parts, sizeof(charger_rig_parts)/sizeof(charger_rig_parts[0]), 780,16,4,3,1250 },
    { TOY_GAME_ENEMY_TANK,
      {[ER_ROOT]={0,-480,0,0,0,0}, [ER_TORSO]={0,190,0,0,0,0},
       [ER_HEAD]={0,365,55,0,0,0},
       [ER_ARM_L]={-450,250,0,0,0,0}, [ER_FOREARM_L]={0,-290,0,0,0,0},
       [ER_HAND_L]={0,-300,0,0,0,0},
       [ER_ARM_R]={450,250,0,0,0,0}, [ER_FOREARM_R]={0,-290,0,0,0,0},
       [ER_HAND_R]={0,-300,0,0,0,0},
       [ER_LEG_L]={-175,0,0,0,0,0}, [ER_LEG_R]={175,0,0,0,0,0}},
      tank_rig_parts, sizeof(tank_rig_parts)/sizeof(tank_rig_parts[0]), 1200,12,5,2,1450 }
};
static const struct enemy_rig_profile *enemy_rig_profile(int type)
{
    for (int i=0;i<(int)(sizeof(enemy_rig_profiles)/sizeof(enemy_rig_profiles[0]));i++)
        if (enemy_rig_profiles[i].type==type) return &enemy_rig_profiles[i];
    return NULL;
}

/* Observer history only: distance, latest newly observed hit's authoritative
 * charge elapsed time. No attack transitions, damage, or collision here. */
static struct enemy_rig_observer {
    int valid,type,x,z,phase,elapsed,hit_ms;
    uint64_t mask;
    unsigned long moved_us;
} enemy_rig_observers[TOY_GAME_MAX_ENEMIES];
static int enemy_rig_slot = -1;
static int enemy_rig_capture_move = -1;
static int enemy_rig_capture_hit_age = -1;
static int enemy_rig_silhouette;
static int enemy_rig_submissions;

static void enemy_rig_observe(const struct toy_game_enemy *e, int slot)
{
    struct enemy_rig_observer *o=&enemy_rig_observers[slot];
    const struct enemy_rig_profile *p=enemy_rig_profile(e->type);
    if (!p || e->active!=1) { o->valid=0; return; }
    int dx=e->x-o->x, dz=e->z-o->z;
    int distance=(dx<0?-dx:dx)+(dz<0?-dz:dz);
    if (!o->valid || o->type!=e->type || distance>1200) {
        memset(o,0,sizeof(*o)); o->hit_ms=-10000;
        o->mask=e->ability.charge_hit_actor_mask;
    } else if (distance>0) {
        o->phase=(o->phase+distance)%p->stride;
        o->moved_us=render_monotonic_us();
    }
    if (!e->charge_active || e->ability.special_timer_ms>0 || e->charge_elapsed_ms<o->elapsed)
        o->hit_ms=-10000;
    if (e->charge_active && (e->ability.charge_hit_actor_mask & ~o->mask))
        o->hit_ms=e->charge_elapsed_ms;
    o->mask=e->ability.charge_hit_actor_mask; o->elapsed=e->charge_elapsed_ms;
    o->valid=1; o->type=e->type; o->x=e->x; o->z=e->z;
}

static struct enemy_rig_input enemy_rig_adapt(const struct toy_game_enemy *e)
{
    struct enemy_rig_input in;
    memset(&in,0,sizeof(in));
    in.type=e->type; in.attack_ms=-1; in.recovery_ms=-1; in.impact_age_ms=-1;
    in.tongue_active=e->special_target_active;
    if (enemy_rig_slot>=0) {
        const struct enemy_rig_observer *o=&enemy_rig_observers[enemy_rig_slot];
        const struct enemy_rig_profile *p=enemy_rig_profile(e->type);
        in.stride_phase=p ? o->phase*1024/p->stride : 0;
        in.moving=o->moved_us && render_monotonic_us()-o->moved_us<100000;
        if (e->charge_active && o->hit_ms>=0)
            in.impact_age_ms=e->charge_elapsed_ms-o->hit_ms;
    }
    if (enemy_rig_capture_move>=0) {
        in.moving=enemy_rig_capture_move; in.stride_phase=128;
        in.impact_age_ms=enemy_rig_capture_hit_age;
    }
    if (e->type==TOY_GAME_ENEMY_CHARGER) {
        if (e->charge_active && e->ability.special_timer_ms>0)
            in.windup_q10=1024-e->ability.special_timer_ms*1024/TOY_GAME_CHARGER_WINDUP_MS;
        else if (e->charge_active) in.charging=1;
        else if (e->ability.special_timer_ms>0)
            in.recovery_ms=TOY_GAME_CHARGER_COOLDOWN_MS-e->ability.special_timer_ms;
    } else if (e->type==TOY_GAME_ENEMY_TANK) {
        if (e->charge_active) in.attack_ms=e->charge_elapsed_ms;
        else if (e->ability.special_timer_ms>0)
            in.recovery_ms=TOY_CONFIG_TANK_COOLDOWN_MS-e->ability.special_timer_ms;
    }
    if (e->active!=1) { in.moving=0; in.windup_q10=0; in.charging=0;
        in.attack_ms=-1; in.recovery_ms=-1; in.impact_age_ms=-1; }
    return in;
}
static int enemy_rig_lerp(int a,int b,int t) { return a+(b-a)*t/1024; }
static int enemy_rig_fraction(int t,int duration)
{ return t<=0?0:t>=duration?1024:t*1024/duration; }

/* Pure samplers: pose channels are offsets from the profile bind pivots. */
static void enemy_rig_sample(const struct enemy_rig_profile *p,
    const struct enemy_rig_input *in, struct enemy_rig_pose *pose)
{
    memset(pose,0,sizeof(*pose));
    struct enemy_rig_transform *c=pose->channel;
    int wave=in->stride_phase<512 ? in->stride_phase*4-1024 : 3072-in->stride_phase*4;
    if (!in->moving) wave=0;
    c[ER_LEG_L].pitch=wave*p->leg_swing/1024;
    c[ER_LEG_R].pitch=-c[ER_LEG_L].pitch;
    c[ER_ARM_L].pitch=-wave*p->arm_swing/1024;
    c[ER_ARM_R].pitch=wave*p->arm_swing/1024;
    c[ER_TORSO].roll=wave*p->sway/1024;
    if (in->type==TOY_GAME_ENEMY_SMOKER) {
        c[ER_TORSO].pitch=12; c[ER_TORSO].roll+=4;
        c[ER_HEAD].pitch=-8; c[ER_HEAD].yaw=in->tongue_active ? 0:8;
        c[ER_ARM_L].pitch-=12; c[ER_FOREARM_L].pitch=-10;
        c[ER_ARM_R].pitch-=12;
    } else if (in->type==TOY_GAME_ENEMY_CHARGER) {
        int w=in->windup_q10, drive=in->charging?1024:0;
        if (in->recovery_ms>=0) drive=1024-enemy_rig_fraction(in->recovery_ms,420);
        int hit=in->impact_age_ms>=0 ? 1024-enemy_rig_fraction(in->impact_age_ms,180):0;
        c[ER_ROOT].y=-90*w/1024-60*drive/1024;
        c[ER_TORSO].roll+=7;
        c[ER_TORSO].pitch=8+26*w/1024+36*drive/1024;
        c[ER_TORSO].yaw=-10*w/1024+12*hit/1024;
        c[ER_HEAD].pitch=-c[ER_TORSO].pitch/2;
        c[ER_ARM_L].pitch+=-30*w/1024-115*drive/1024;
        c[ER_ARM_L].yaw=-15*w/1024+15*hit/1024;
        c[ER_ARM_L].roll=-12*w/1024;
        c[ER_FOREARM_L].pitch=-45*w/1024+15*drive/1024;
        c[ER_HAND_L].z=65*hit/1024;
        c[ER_ARM_R].pitch=-45*w/1024-45*drive/1024;
        c[ER_LEG_L].pitch=c[ER_LEG_L].pitch*(1024-drive)/1024-12*w/1024;
        c[ER_LEG_R].pitch=c[ER_LEG_R].pitch*(1024-drive)/1024-12*w/1024;
    } else if (in->type==TOY_GAME_ENEMY_TANK) {
        /* Authored sweep keys use gameplay's impact and end constants.
         * Torso starts the rotation, shoulder/forearm trail, fist crosses the
         * forward plane at impact and travels beyond it before settling. */
        int t=in->attack_ms;
        if (t<0 && in->recovery_ms>=0 && in->recovery_ms<420)
            t=TOY_CONFIG_TANK_WINDUP_MS+in->recovery_ms;
        c[ER_TORSO].pitch=5;
        if (t>=0) {
            static const int keys[5][6] = {
                {0,0,0,0,0,0}, {35,-75,85,15,-20,-55},
                {-28,-90,-5,0,-10,-35}, {-44,-80,-45,-10,10,-20},
                {0,0,0,0,0,0}
            };
            int times[5]={0,TOY_CONFIG_TANK_IMPACT_MS*2/3,
                TOY_CONFIG_TANK_IMPACT_MS,TOY_CONFIG_TANK_WINDUP_MS,
                TOY_CONFIG_TANK_WINDUP_MS+420};
            int k=0; while (k<3 && t>times[k+1]) k++;
            int f=enemy_rig_fraction(t-times[k],times[k+1]-times[k]);
            int value[6];
            for (int j=0;j<6;j++) value[j]=enemy_rig_lerp(keys[k][j],keys[k+1][j],f);
            c[ER_TORSO].yaw=value[0]; c[ER_ARM_R].pitch=value[1];
            c[ER_ARM_R].yaw=value[2]; c[ER_ARM_R].roll=value[3];
            c[ER_FOREARM_R].pitch=value[4]; c[ER_ROOT].y=value[5];
            c[ER_HEAD].yaw=-value[0];
            c[ER_TORSO].pitch+=-value[5]/8;
            int balance=t<TOY_CONFIG_TANK_IMPACT_MS ? enemy_rig_fraction(t,180) :
                1024-enemy_rig_fraction(t-TOY_CONFIG_TANK_WINDUP_MS,420);
            c[ER_ARM_L].pitch=-20*balance/1024; c[ER_ARM_L].yaw=-value[0]/2;
            c[ER_LEG_L].pitch=0; c[ER_LEG_R].pitch=0;
        }
    }
}

/* Finalize the tiny rigid hierarchy once per enemy submission, not once per
 * vertex. Affine matrices are transient presentation data, never skinning. */
struct enemy_rig_final { double m[ER_COUNT][12]; };
static void enemy_rig_finalize(const struct enemy_rig_profile *p,
    const struct enemy_rig_pose *pose,struct enemy_rig_final *out)
{
    for (int i=0;i<ER_COUNT;i++) {
        const struct enemy_rig_transform *t=&pose->channel[i], *b=&p->bind[i];
        double a=(t->pitch+b->pitch)*3.141592653589793/180.0;
        double y=(t->yaw+b->yaw)*3.141592653589793/180.0;
        double z=(t->roll+b->roll)*3.141592653589793/180.0;
        double sa=sin(a),ca=cos(a),sy=sin(y),cy=cos(y),sz=sin(z),cz=cos(z);
        double local[12]={cz*cy,cz*sy*sa-sz*ca,cz*sy*ca+sz*sa,b->x+t->x,
            sz*cy,sz*sy*sa+cz*ca,sz*sy*ca-cz*sa,b->y+t->y,
            -sy,cy*sa,cy*ca,b->z+t->z};
        int parent=enemy_rig_parent[i];
        if (parent<0) memcpy(out->m[i],local,sizeof(local));
        else for (int row=0;row<3;row++) for (int col=0;col<4;col++) {
            double v=col==3 ? out->m[parent][row*4+3]:0;
            for (int k=0;k<3;k++) v+=out->m[parent][row*4+k]*local[k*4+col];
            out->m[i][row*4+col]=v;
        }
    }
}
static struct vec3 enemy_rig_final_point(const struct enemy_rig_profile *p,
    const struct enemy_rig_final *final,int channel,struct vec3 v)
{
    const double *m=final->m[channel];
    struct vec3 q;
    q.x=(int)((m[0]*v.x+m[1]*v.y+m[2]*v.z+m[3])*p->size_milli/1000);
    q.y=-900+(int)((m[4]*v.x+m[5]*v.y+m[6]*v.z+m[7]+900)*p->size_milli/1000);
    q.z=(int)((m[8]*v.x+m[9]*v.y+m[10]*v.z+m[11])*p->size_milli/1000);
    return q;
}
static struct vec3 enemy_rig_point(const struct enemy_rig_profile *p,
    const struct enemy_rig_pose *pose,int channel,struct vec3 v)
{
    struct enemy_rig_final final;
    enemy_rig_finalize(p,pose,&final);
    return enemy_rig_final_point(p,&final,channel,v);
}
static int render_enemy_rig(struct toy_renderer *renderer,const struct camera *camera,
    const struct toy_game_enemy *e,int scale,uint32_t feedback)
{
    /* An octagonal prism keeps broad low-poly planes but removes the hard
     * armor-box corner from flesh. Same primitive for every profile/part. */
    static const int ring[8][2]={{-3,-4},{3,-4},{4,-3},{4,3},
        {3,4},{-3,4},{-4,3},{-4,-3}};
    const struct enemy_rig_profile *p=enemy_rig_profile(e->type);
    struct enemy_rig_input in=enemy_rig_adapt(e);
    struct enemy_rig_pose pose;
    struct enemy_rig_final final;
    int pixels=0;
    if (!p || active_enemy_dissolve>=256) return 0;
    enemy_rig_sample(p,&in,&pose);
    enemy_rig_finalize(p,&pose,&final);
    /* Profile owns living dimensions, scale only carries legacy death squash. */
    int squash=e->active==2 && !active_enemy_transform ? scale:1000;
    for (int i=0;i<p->count;i++) {
        const struct enemy_rig_part *part=&p->parts[i];
        struct vec3 v[16];
        for (int j=0;j<16;j++) {
            struct vec3 q={part->x+ring[j%8][0]*part->hx/4,
                part->y+(j<8?-part->hy:part->hy),
                part->z+ring[j%8][1]*part->hz/4};
            q=enemy_rig_final_point(p,&final,part->channel,q);
            v[j].x=e->x+(e->dir_z*q.x+e->dir_x*q.z)/1024;
            v[j].z=e->z+(-e->dir_x*q.x+e->dir_z*q.z)/1024;
            v[j].y=-900+(q.y+900)*squash/1000+active_enemy_lift;
        }
        unsigned color=enemy_rig_silhouette ? 0x111111 : feedback ? feedback : part->color;
        for (int face=0;face<8;face++) {
            int next=(face+1)%8;
            /* Fixed world light, transformed face normal. No texture identity. */
            struct vec3 u={v[face+8].x-v[face].x,v[face+8].y-v[face].y,v[face+8].z-v[face].z};
            struct vec3 w={v[next].x-v[face].x,v[next].y-v[face].y,v[next].z-v[face].z};
            long long nx=(long long)u.y*w.z-(long long)u.z*w.y;
            long long ny=(long long)u.z*w.x-(long long)u.x*w.z;
            long long nz=(long long)u.x*w.y-(long long)u.y*w.x;
            int light=256, length=isqrt(nx*nx+ny*ny+nz*nz);
            if (length>0) light=model_form_light_q8(nx*32767/length,ny*32767/length,nz*32767/length,0);
            if (enemy_rig_silhouette) light=256;
            unsigned lit=((((color>>16)&255)*light/256)<<16)|
                ((((color>>8)&255)*light/256)<<8)|((color&255)*light/256);
            pixels+=draw_world_triangle(renderer,camera,&v[face],&v[face+8],&v[next+8],lit);
            pixels+=draw_world_triangle(renderer,camera,&v[face],&v[next+8],&v[next],lit);
        }
        for (int j=1;j<7;j++) {
            pixels+=draw_world_triangle(renderer,camera,&v[0],&v[j],&v[j+1],color);
            pixels+=draw_world_triangle(renderer,camera,&v[8],&v[j+9],&v[j+8],color);
        }
    }
    enemy_rig_submissions++;
    return pixels;
}
