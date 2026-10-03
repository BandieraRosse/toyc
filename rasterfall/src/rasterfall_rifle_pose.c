#include "rasterfall_rifle_pose.h"
#include "rasterfall_model.h"
#include "rasterfall_calibration.h"
#include "rasterfall_animation.h"
#include "rasterfall_character.h"
#include "rasterfall_units.h"
#include "math.h"
#include "string.h"

static double clamp(double x,double lo,double hi) { return x<lo?lo:x>hi?hi:x; }
static void multiply(const double a[9],const double b[9],double out[9])
{
    double m[9];
    for(int i=0;i<3;++i)for(int j=0;j<3;++j)
        m[i*3+j]=a[i*3]*b[j]+a[i*3+1]*b[3+j]+a[i*3+2]*b[6+j];
    memcpy(out,m,sizeof(m));
}
static void vector(const double m[9],const double v[3],double out[3])
{ for(int i=0;i<3;++i)out[i]=m[i*3]*v[0]+m[i*3+1]*v[1]+m[i*3+2]*v[2]; }
static void transpose(const double m[9],double out[9])
{ for(int i=0;i<3;++i)for(int j=0;j<3;++j)out[i*3+j]=m[j*3+i]; }
static void quaternion(const float q[4],double m[9])
{
    double x=q[0],y=q[1],z=q[2],w=q[3];
    m[0]=1-2*(y*y+z*z);m[1]=2*(x*y-z*w);m[2]=2*(x*z+y*w);
    m[3]=2*(x*y+z*w);m[4]=1-2*(x*x+z*z);m[5]=2*(y*z-x*w);
    m[6]=2*(x*z-y*w);m[7]=2*(y*z+x*w);m[8]=1-2*(x*x+y*y);
}
static void rotation(double pitch,double yaw,double out[9])
{
    double p=pitch*M_PI/180,y=yaw*M_PI/180;
    double a[9]={cos(y),0,sin(y),0,1,0,-sin(y),0,cos(y)};
    double b[9]={1,0,0,0,cos(p),-sin(p),0,sin(p),cos(p)};
    multiply(a,b,out);
}
static void hold_rotation(double pitch,double yaw,double single,double out[9])
{
    double roll=-60*single*M_PI/180;
    double r[9]={cos(roll),-sin(roll),0,sin(roll),cos(roll),0,0,0,1};
    rotation(pitch,yaw,out);multiply(out,r,out);
}
static void add_role(struct rasterfall_model_asset *p,int role,int pitch,int yaw,int roll)
{
    int bone=rasterfall_model_humanoid_bone(p,role);
    if(bone<0)return;
    p->bones[bone].rotate_x+=pitch;p->bones[bone].rotate_y+=yaw;
    p->bones[bone].rotate_z+=roll;
}

/* Conservative chest ellipsoid and short weapon capsules. This is a visual
 * constraint, evaluated before hand IK, never a gameplay collision shape. */
static double chest_clearance(const struct rasterfall_model_asset *p,
    int weapon,const double origin[3],const double r[9],double units,double world_units,
    double width,double armor,double forward[3])
{
    int chest=rasterfall_model_humanoid_bone(p,RASTERFALL_HUMANOID_CHEST);
    int spine=rasterfall_model_humanoid_bone(p,RASTERFALL_HUMANOID_SPINE);
    int shoulder=rasterfall_model_humanoid_bone(p,RASTERFALL_HUMANOID_RIGHT_UPPER_ARM);
    const struct rasterfall_model_bone_transform *c=&p->bone_transforms[chest];
    const struct rasterfall_weapon_asset_profile *profile=rasterfall_weapon_asset_profile(weapon);
    double minimum=512*units;
    for(int i=0;i<3;++i)forward[i]=c->rotation[i*3+2];
    for(int part=0;part<profile->clearance_count;++part)for(int step=0;step<=8;++step) {
        const struct rasterfall_weapon_clearance_capsule *s=&profile->clearance[part];
        double local[3],point[3],v[3],q[3],radius=s->radius*world_units;
        for(int i=0;i<3;++i)local[i]=(s->a[i]+(s->b[i]-s->a[i])*step/8.0)*world_units;
        vector(r,local,point);
        for(int i=0;i<3;++i)v[i]=origin[i]+point[i]-c->position[i];
        for(int i=0;i<3;++i)q[i]=c->rotation[i]*v[0]+c->rotation[3+i]*v[1]+c->rotation[6+i]*v[2];
        double height=p->bones[shoulder].rest_y-p->bones[spine].rest_y;
        q[1]-=(p->bones[shoulder].rest_y+p->bones[spine].rest_y)*.5-p->bones[chest].rest_y;
        double rx=width*.34+radius,ry=height*.6+radius,rz=width*.28+armor*14*units+radius;
        double lateral=q[0]*q[0]/(rx*rx)+q[1]*q[1]/(ry*ry);
        if(lateral<1) {
            double clearance=q[2]-rz*sqrt(1-lateral);
            if(clearance<minimum)minimum=clearance;
        }
    }
    return minimum/units;
}

static unsigned idle_random(struct rasterfall_rifle_history *h)
{
    unsigned x=h->idle_random;
    x^=x<<13;x^=x>>17;x^=x<<5;
    return h->idle_random=x;
}
static int approach(int value,int target,int step)
{ return value<target?value+(target-value<step?target-value:step):
    value-(value-target<step?value-target:step); }

void rasterfall_rifle_sample(const struct toy_game_actor *a,unsigned tick,
    struct rasterfall_rifle_history *h,struct rasterfall_rifle_pose_input *out)
{
    int aiming,hip,ready,step,idle,weapon;
    memset(out,0,sizeof(*out));
    if(!a || !h)return;
    weapon=a->current_slot>=0 && a->current_slot<TOY_GAME_WEAPON_SLOTS?
        a->slots[a->current_slot].weapon:-1;
    ready=!a->reloading && !a->weapon_switch_timer_ms && !a->control_disabled &&
        !a->melee_timer_ms && !a->throw_timer_ms && !a->airborne_ms && !a->airborne_y &&
        a->state==TOY_GAME_ACTOR_ALIVE;
    hip=ready && a->moving && a->current_slot>=0 &&
        a->current_slot<TOY_GAME_WEAPON_SLOTS &&
        a->slots[a->current_slot].weapon==TOY_GAME_WEAPON_AK;
    aiming=ready && !hip &&
        (a->combat_target.kind>=0 || a->animation.id==TOY_GAME_ANIM_FIRE);
    idle=ready && weapon==TOY_GAME_WEAPON_AK && a->active && a->kind==TOY_GAME_ACTOR_AI &&
        !a->developer_only && !a->animation_demo && !a->moving && a->combat_target.kind<0 &&
        (a->animation.id==TOY_GAME_ANIM_IDLE || a->animation.id==TOY_GAME_ANIM_NONE);
    if(!h->valid || h->actor_id!=a->actor_id || h->generation!=a->combat_generation ||
        (int)(tick-h->tick)<0) {
        memset(h,0,sizeof(*h));h->valid=1;h->actor_id=a->actor_id;
        h->generation=a->combat_generation;h->tick=tick;h->weapon=weapon;
        h->idle_random=((unsigned)a->actor_id*747796405u)^a->combat_generation^2891336453u;
        if(!h->idle_random)h->idle_random=1;
        h->aim_milli=aiming?1000:0;
        h->hip_milli=hip?1000:0;
    }
    if(h->weapon!=weapon) {
        h->weapon=weapon;h->idle_active=0;
        memset(h->idle_milli,0,sizeof(h->idle_milli));
    }
    if(idle) {
        if(!h->idle_active) {
            h->idle_pose=(int)(idle_random(h)%RASTERFALL_RIFLE_IDLE_COUNT);
            h->idle_until=tick+3000+idle_random(h)%4001;
        } else if((int)(tick-h->idle_until)>=0) {
            /* Three relaxed holds plus shoulder watch; never repeat a hold.
             * Private presentation RNG must not consume gameplay randomness. */
            h->idle_pose=(h->idle_pose+1+(int)(idle_random(h)%3))%4;
            h->idle_until=tick+3000+idle_random(h)%4001;
        }
        aiming=h->idle_pose==RASTERFALL_RIFLE_IDLE_COUNT;
    }
    h->idle_active=idle;
    /* Simulation milliseconds, not extraction calls. Repeated freeze is idempotent. */
    step=(int)(tick-h->tick);if(step<0)step=0;else if(step>250)step=250;
    h->aim_milli=approach(h->aim_milli,aiming?1000:0,idle?step*1000/800:step*4);
    if(hip)h->hip_milli+=(1000-h->hip_milli<step*4?1000-h->hip_milli:step*4);
    else h->hip_milli-=(h->hip_milli<step*3?h->hip_milli:step*3);
    h->tick=tick;out->aim_milli=h->aim_milli;
    out->hip_milli=h->hip_milli;
    for(int i=0;i<RASTERFALL_RIFLE_IDLE_COUNT;++i) {
        h->idle_milli[i]=approach(h->idle_milli[i],idle && h->idle_pose==i?1000:0,
            idle?step*1000/800:step*4);
        out->idle_milli[i]=h->idle_milli[i];
    }
    out->armor_milli=(a->character_id==RASTERFALL_CHARACTER_SQUAD_B_HEAVY || a->character_id==RASTERFALL_CHARACTER_GUNNER_ELITE)?1000:0;
    out->pitch_mdeg=(int)(atan2(a->pitch_sy,a->pitch_cy>0?a->pitch_cy:1024)*180000/M_PI);
    if(idle)out->pitch_mdeg=0;
    if(a->animation.id==TOY_GAME_ANIM_FIRE && a->animation.time_ms<180) {
        int t=a->animation.time_ms;
        out->recoil_milli=t<35?t*1000/35:(180-t)*1000/145;
    }
    if(a->combat_target.kind>=0) {
        double dx=a->combat_target.x-a->x,dz=a->combat_target.z-a->z;
        out->target_distance_rfu=(int)sqrt(dx*dx+dz*dz);
    }
}

int rasterfall_rifle_pose_solve(struct rasterfall_model_instance *instance,
    int weapon,int character_scale,const struct rasterfall_rifle_pose_input *input,
    struct rasterfall_rifle_diagnostics *diagnostics)
{
    struct rasterfall_model_asset *p;
    struct rasterfall_weapon_socket_transform stock,muzzle,grips[2];
    struct rasterfall_model_attachment_transform target,actual,right;
    double weapon_r[9],origin[3],anchor[3],offset[3],units,world_units;
    double aim,hip,track,rest,pitch,yaw,shoulder_width,armor,aim_target[3],forward[3];
    double single,chest,low,relaxed,carry;
    int shoulders[2],upper[2],fore[2],hand[2];
    struct rasterfall_rifle_diagnostics result={0};
    if(!instance || !input || character_scale<=0)return -1;
    p=rasterfall_model_instance_pose(instance);
    if(!p || !p->position_scale ||
       rasterfall_model_humanoid_bone(p,RASTERFALL_HUMANOID_CHEST)<0 ||
       rasterfall_model_humanoid_bone(p,RASTERFALL_HUMANOID_SPINE)<0)return -1;
    if(rasterfall_weapon_socket_transform(weapon,RASTERFALL_WEAPON_SOCKET_STOCK,&stock)<0 ||
       rasterfall_weapon_socket_transform(weapon,RASTERFALL_WEAPON_SOCKET_MUZZLE,&muzzle)<0 ||
       rasterfall_weapon_socket_transform(weapon,RASTERFALL_WEAPON_SOCKET_PRIMARY_GRIP,&grips[1])<0 ||
       rasterfall_weapon_socket_transform(weapon,RASTERFALL_WEAPON_SOCKET_FOREGRIP,&grips[0])<0)return -1;
    for(int side=0;side<2;++side) {
        shoulders[side]=rasterfall_model_humanoid_bone(p,side?RASTERFALL_HUMANOID_RIGHT_SHOULDER:RASTERFALL_HUMANOID_LEFT_SHOULDER);
        upper[side]=rasterfall_model_humanoid_bone(p,side?RASTERFALL_HUMANOID_RIGHT_UPPER_ARM:RASTERFALL_HUMANOID_LEFT_UPPER_ARM);
        fore[side]=rasterfall_model_humanoid_bone(p,side?RASTERFALL_HUMANOID_RIGHT_FOREARM:RASTERFALL_HUMANOID_LEFT_FOREARM);
        hand[side]=rasterfall_model_humanoid_bone(p,side?RASTERFALL_HUMANOID_RIGHT_HAND:RASTERFALL_HUMANOID_LEFT_HAND);
        if(shoulders[side]<0 || upper[side]<0 || fore[side]<0 || hand[side]<0)return -1;
    }
    units=p->position_scale/512.0;world_units=units*1000/character_scale;
    aim=clamp(input->aim_milli/1000.0,0,1);armor=clamp(input->armor_milli/1000.0,0,1);
    hip=weapon==TOY_GAME_WEAPON_AK?clamp(input->hip_milli/1000.0,0,1):0;
    /* Cross-fade height independently of target tracking. A moving shot must
     * not raise the weapon back to the shoulder or point it at the ground. */
    if(aim+hip>1) { double total=aim+hip;aim/=total;hip/=total; }
    track=aim+hip;rest=1-track;
    single=clamp(input->idle_milli[RASTERFALL_RIFLE_IDLE_SINGLE]/1000.0,0,1);
    chest=clamp(input->idle_milli[RASTERFALL_RIFLE_IDLE_CHEST]/1000.0,0,1);
    low=clamp(input->idle_milli[RASTERFALL_RIFLE_IDLE_LOW]/1000.0,0,1);
    relaxed=single+chest+low;
    if(relaxed>rest) { single*=rest/relaxed;chest*=rest/relaxed;low*=rest/relaxed; }
    carry=hip+low;
    relaxed=single+chest+low;
    pitch=clamp(input->pitch_mdeg/1000.0,-75,75)*track-(rest-relaxed)*30+
        single*72+chest*24-low*8;
    yaw=clamp(input->yaw_mdeg/1000.0,-50,50)*track+10*(rest-relaxed)-single*8+chest*58-low*12;
    double level=1-fabs(pitch)/75;
    /* Authored shoulder-aim base, distributed through torso/neck. The weapon
     * itself is aimed in character space after all local additive layers. */
    add_role(p,RASTERFALL_HUMANOID_SPINE,(int)(-pitch*.18*(1-single-chest)),(int)((-12+2*carry*level+yaw*.45)*(1-single-chest)),0);
    add_role(p,RASTERFALL_HUMANOID_CHEST,(int)(-pitch*.42*(1-single-chest)),(int)((-23+2*carry*level+yaw*.55)*(1-single-chest)),0);
    add_role(p,RASTERFALL_HUMANOID_NECK,(int)(-pitch*.15*(1-single-chest)+16*aim),(int)(35*aim+(35-4*level)*carry),0);
    add_role(p,RASTERFALL_HUMANOID_HEAD,(int)(-pitch*.25*(1-single-chest)),0,(int)(12*aim));
    if(rasterfall_model_instance_update_bones(instance)<0)return -1;
    shoulder_width=fabs((double)p->bones[upper[1]].rest_x-p->bones[upper[0]].rest_x);
    /* Contact lies on the front of the firing-side shoulder, with a bounded
     * armor allowance. It follows torso motion rather than the old hand. */
    offset[0]=shoulder_width*(.18+.10*fabs(pitch)/75+.08*(rest-low)-.04*carry*level);
    /* AK shoulder contact sits below the shoulder joint; the moving hold
     * rests beside the lower ribs. Preserve the other weapon profiles. */
    double shoulder_height=20-(weapon==TOY_GAME_WEAPON_AK?28*level:0);
    offset[1]=(shoulder_height*(track+low)-clamp(pitch,0,75)*.65-(rest-low)*33-
        carry*105*level)*units;
    offset[2]=(16+armor*12+carry*18*level)*units;
    offset[0]=offset[0]*(1-single-chest)+shoulder_width*(single*.16+chest*.05);
    offset[1]=offset[1]*(1-single-chest)-(single*155+chest*100)*units;
    offset[2]=offset[2]*(1-single-chest)+(single*65+chest*55+armor*12*(single+chest))*units;
    vector(p->bone_transforms[shoulders[1]].rotation,offset,anchor);
    for(int i=0;i<3;++i)anchor[i]+=p->bone_transforms[upper[1]].position[i];
    hold_rotation(-pitch,yaw,single,weapon_r);
    double local_stock[3]={stock.position.x*world_units,stock.position.y*world_units,stock.position.z*world_units};
    double local_muzzle[3]={muzzle.position.x*world_units,muzzle.position.y*world_units,muzzle.position.z*world_units};
    double distance=input->target_distance_rfu>0?input->target_distance_rfu:16384;
    /* A very close target cannot be reached by sighting backwards through
     * the shoulder. Limit visual convergence to two metres; gameplay owns
     * its unchanged eye-origin ray. */
    distance=clamp(distance,1024,131072);
    aim_target[0]=sin(yaw*M_PI/180)*distance*world_units;
    aim_target[1]=(RASTERFALL_HUMAN_EYE_HEIGHT_RFU+sin(pitch*M_PI/180)/cos(pitch*M_PI/180)*distance)*world_units;
    aim_target[2]=cos(yaw*M_PI/180)*distance*world_units;
    for(int iteration=0;iteration<(relaxed>0?32:12);++iteration) {
        vector(weapon_r,local_stock,offset);
        for(int i=0;i<3;++i)origin[i]=anchor[i]-offset[i];
        double clearance=chest_clearance(p,weapon,origin,weapon_r,units,world_units,shoulder_width,armor,forward);
        double margin=2+hip*2*clamp(level*2,0,1);
        if(clearance<margin) {
            double shift=clamp(margin-clearance,0,12);
            for(int i=0;i<3;++i)anchor[i]+=forward[i]*shift*units;
            result.clearance_shift_rfu+=shift;
            vector(weapon_r,local_stock,offset);
            for(int i=0;i<3;++i)origin[i]=anchor[i]-offset[i];
        }
        /* Small shoulder-pocket motion keeps different arm/shoulder
         * proportions reachable. Project the weapon, never scale the limbs
         * or the rifle. Recheck clearance on the next iteration. */
        for(int side=0;side<2;++side) {
            if(!side && single>0)continue;
            int socket=side?RASTERFALL_ATTACHMENT_WEAPON_R:RASTERFALL_ATTACHMENT_FOREGRIP;
            double g[9],s[9],inverse[9],palm[9],wrist_offset[3],grip[3],delta[3];
            double local[3]={grips[side].position.x*world_units,grips[side].position.y*world_units,grips[side].position.z*world_units};
            quaternion(grips[side].rotation,g);multiply(weapon_r,g,s);
            quaternion(p->attachments[socket].local_rotation,g);transpose(g,inverse);
            multiply(s,inverse,palm);
            for(int i=0;i<3;++i)wrist_offset[i]=p->attachments[socket].local_position[i];
            vector(palm,wrist_offset,offset);vector(weapon_r,local,grip);
            double length=0,a=0,b=0;
            for(int i=0;i<3;++i) {
                delta[i]=origin[i]+grip[i]-offset[i]-p->bone_transforms[upper[side]].position[i];
                double v=p->bone_transforms[fore[side]].position[i]-p->bone_transforms[upper[side]].position[i];
                double w=p->bone_transforms[hand[side]].position[i]-p->bone_transforms[fore[side]].position[i];
                length+=delta[i]*delta[i];a+=v*v;b+=w*w;
            }
            length=sqrt(length);
            double excess=length-.97*(sqrt(a)+sqrt(b));
            double budget=(40+relaxed*200-result.reach_shift_rfu)*units;
            if(excess>0 && budget>0) {
                double shift=clamp(excess,0,budget);
                for(int i=0;i<3;++i) {
                    double d=delta[i]*shift/length;
                    anchor[i]-=d;origin[i]-=d;
                }
                result.reach_shift_rfu+=shift/units;
            }
        }
        if(track>0) {
            double m[3],v[3];vector(weapon_r,local_muzzle,m);
            for(int i=0;i<3;++i)v[i]=aim_target[i]-origin[i]-m[i];
            double converge_pitch=atan2(v[1],sqrt(v[0]*v[0]+v[2]*v[2]))*180/M_PI;
            double converge_yaw=atan2(v[0],v[2])*180/M_PI;
            hold_rotation(-pitch-track*(converge_pitch-pitch),yaw+track*(converge_yaw-yaw),single,weapon_r);
        }
    }
    /* Recoil rotates around shoulder contact, then both hands follow. */
    if(input->recoil_milli) {
        double recoil[9];rotation(-clamp(input->recoil_milli,0,1000)*.0018,0,recoil);
        multiply(weapon_r,recoil,weapon_r);
    }
    vector(weapon_r,local_stock,offset);
    for(int i=0;i<3;++i) {
        result.stock_target[i]=anchor[i]/units;
        origin[i]=anchor[i]-offset[i];
    }
    /* Right hand owns the final weapon frame. Solve it first, derive the
     * actual weapon frame, and only then close the support hand constraint. */
    for(int pass=0;pass<2;++pass) {
        int side=1-pass;
        if(!side && single>=1)continue;
        int socket=side?RASTERFALL_ATTACHMENT_WEAPON_R:RASTERFALL_ATTACHMENT_FOREGRIP;
        double g[9],local_socket[9],inverse[9],palm[9],pole[3];
        double local[3]={grips[side].position.x*world_units,grips[side].position.y*world_units,grips[side].position.z*world_units};
        vector(weapon_r,local,offset);
        for(int i=0;i<3;++i)target.position[i]=origin[i]+offset[i];
        quaternion(grips[side].rotation,g);multiply(weapon_r,g,target.rotation);
        quaternion(p->attachments[socket].local_rotation,local_socket);transpose(local_socket,inverse);
        multiply(target.rotation,inverse,palm);
        for(int i=0;i<3;++i)pole[i]=-(1-.7*(hip+low))*weapon_r[i*3+1]+
            (side?(.35+(hip+low)*2):(-.30-(hip+low)*2))*palm[i*3];
        pole[0]+=side?-.25:.18;
        for(int i=0;i<3;++i)pole[i]=pole[i]*(1-relaxed)+(side?1:-1)*palm[i*3]*relaxed*2;
        /* Elbow opening is body-relative, not weapon-relative: diagonal and
         * upright holds must not drag the elbows inward with the gun roll. */
        pole[0]+=(side?-1:1)*(.3*level+single*.65+chest*.5+low*.25);
        p->attachment_ik_previous_pole_valid=0;
        if(rasterfall_model_solve_two_bone_attachment_pose(p,p->bones[upper[side]].name,
            p->bones[fore[side]].name,p->bones[hand[side]].name,socket,&target,pole)<0)return -1;
        result.reach_clamped[side]=p->attachment_ik_diagnostics.reach_clamped;
        if(rasterfall_model_instance_attachment_transform(instance,socket,&actual)<0)return -1;
        double distance=0,length=0,dot=0;
        for(int i=0;i<3;++i) {
            double d=(actual.position[i]-target.position[i])/units;
            double arm=p->bone_transforms[hand[side]].position[i]-p->bone_transforms[fore[side]].position[i];
            distance+=d*d;length+=arm*arm;dot+=arm*p->bone_transforms[hand[side]].rotation[i*3]*(side?-1:1);
        }
        result.grip_error[side]=sqrt(distance);result.wrist_dot[side]=dot/sqrt(length);
        if(side) {
            right=actual;transpose(g,inverse);multiply(right.rotation,inverse,weapon_r);
            vector(weapon_r,local,offset);
            for(int i=0;i<3;++i)origin[i]=right.position[i]-offset[i];
        }
    }
    /* Release the support arm from the foregrip toward a relaxed hanging
     * pose. Quaternion blending keeps the return to a two-hand grip smooth. */
    if(single>0) {
        int bones[4]={shoulders[0],upper[0],fore[0],hand[0]};
        const int relaxed_angles[4][3]={{0,0,0},{-4,0,-76},{0,-8,0},{0,0,0}};
        for(int i=0;i<4;++i) {
            struct rasterfall_model_bone *b=&p->bones[bones[i]];
            struct rasterfall_animation_rotation r;
            rasterfall_animation_quat_to_euler(rasterfall_animation_quat_nlerp(
                rasterfall_animation_quat_from_euler(b->rotate_x,b->rotate_y,b->rotate_z),
                rasterfall_animation_quat_from_euler(relaxed_angles[i][0],relaxed_angles[i][1],relaxed_angles[i][2]),
                (int)(single*1000)),&r);
            b->rotate_x=r.x;b->rotate_y=r.y;b->rotate_z=r.z;
        }
        for(unsigned i=0;i<p->bone_count;++i)if(!strncmp(p->bones[i].name,"RF_L_FINGER_",12)) {
            p->bones[i].rotate_z=(int)(p->bones[i].rotate_z*(1-single)-12*single);
            p->bones[i].rotate_y=(int)(p->bones[i].rotate_y*(1-single));
        }
    }
    vector(weapon_r,local_stock,offset);
    for(int i=0;i<3;++i) {
        result.stock_actual[i]=(origin[i]+offset[i])/units;
        result.muzzle_direction[i]=weapon_r[i*3+2];
    }
    result.clearance_rfu=chest_clearance(p,weapon,origin,weapon_r,units,world_units,shoulder_width,armor,forward);
    double m[3],dot=0,length=0;vector(weapon_r,local_muzzle,m);
    for(int i=0;i<3;++i) {
        double v=aim_target[i]-origin[i]-m[i];
        dot+=v*result.muzzle_direction[i];length+=v*v;
    }
    dot=clamp(dot/sqrt(length),-1,1);
    result.aim_error_degrees=atan2(sqrt(1-dot*dot),dot)*180/M_PI;
    if(diagnostics)*diagnostics=result;
    return rasterfall_model_instance_update_bones(instance);
}
