#include "rf_mesh_weaver_presentation.h"
#include "rf_mesh_weaver_layout_generated.h"
#include "rasterfall_calibration.h"
#include "rf_weaver_blueprints_generated.h"
#include "tlibc_everything.h"
#include "math.h"
#include "limits.h"

static int failures;
static const char *repository;
static void require(int condition,const char *message)
{
    if(!condition && failures++<12) __printf("FAIL %s\n",message);
}
static double dot(const double a[3],const double b[3])
{ return a[0]*b[0]+a[1]*b[1]+a[2]*b[2]; }
static double length(const double a[3]) { return sqrt(dot(a,a)); }
static double distance(const double a[3],const double b[3])
{ double d[3];for(int k=0;k<3;++k)d[k]=a[k]-b[k];return length(d); }
static void inverse(const struct rf_weaver_transform *t,const double p[3],double q[3])
{
    for(int k=0;k<3;++k) {
        q[k]=0;for(int j=0;j<3;++j)q[k]+=t->rotation[j*3+k]*(p[j]-t->position[j]);
    }
}
static int surface_project(const struct rf_weaver_face *face,const double point[3],double *error)
{
    double a[3],b[3],p[3];
    for(int k=0;k<3;++k) {
        a[k]=face->vertex[1].position[k]-face->vertex[0].position[k];
        b[k]=face->vertex[2].position[k]-face->vertex[0].position[k];
        p[k]=point[k]-face->vertex[0].position[k];
    }
    double aa=dot(a,a),ab=dot(a,b),bb=dot(b,b),pa=dot(p,a),pb=dot(p,b);
    double divisor=aa*bb-ab*ab;
    if(divisor<=0)return 0;
    double u=(bb*pa-ab*pb)/divisor,v=(aa*pb-ab*pa)/divisor;
    double residual[3];for(int k=0;k<3;++k)residual[k]=p[k]-a[k]*u-b[k]*v;
    *error=length(residual);
    return u>=-1e-8 && v>=-1e-8 && u+v<=1+1e-8;
}

/* Independent plane intersection + Gram projection reference. Production
 * uses a bounded BVH and Moller-Trumbore, never this full-mesh audit scan. */
static int reference_occluded(const struct rf_weaver_mesh *mesh,const double origin[3],
    const double end[3],double front)
{
    double direction[3];for(int k=0;k<3;++k)direction[k]=end[k]-origin[k];
    double maximum=1-2/length(direction);
    for(unsigned i=0;i<mesh->count;++i) {
        const struct rf_weaver_face *face=&mesh->faces[i];
        if(face->threshold>front)continue;
        double a[3],b[3],normal[3],offset[3];
        for(int k=0;k<3;++k) {
            a[k]=face->vertex[1].position[k]-face->vertex[0].position[k];
            b[k]=face->vertex[2].position[k]-face->vertex[0].position[k];
            offset[k]=face->vertex[0].position[k]-origin[k];
        }
        normal[0]=a[1]*b[2]-a[2]*b[1];normal[1]=a[2]*b[0]-a[0]*b[2];
        normal[2]=a[0]*b[1]-a[1]*b[0];
        double denominator=dot(normal,direction);
        if(fabs(denominator)<1e-9)continue;
        double t=dot(normal,offset)/denominator;
        if(t<=1e-8 || t>=maximum)continue;
        double point[3],error=0;for(int k=0;k<3;++k)point[k]=origin[k]+t*direction[k];
        if(surface_project(face,point,&error) && error<1e-6)return 1;
    }
    return 0;
}

static void audit_support(const struct rf_weaver_mesh *gun,struct rf_mesh_weaver_frame *frame)
{
    struct rf_weaver_mesh tray={0};char path[512];
    snprintf(path,sizeof(path),"%s/rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_tray.rmesh",repository);
    require(rf_weaver_mesh_load(&tray,path,-1)==0,"load real tray support topology");
    if(!tray.faces)return;
    frame->phase=TOY_WEAVER_READY;frame->progress=1;frame->powered=1;
    struct rf_weaver_pose pose;rf_weaver_pose_sample(frame,gun,&pose);
    unsigned contacts=0;
    for(unsigned i=0;i<gun->count;++i)for(int corner=0;corner<3;++corner) {
        const int *vertex=gun->faces[i].vertex[corner].position;
        if(vertex[1]!=gun->minimum[1])continue;
        double local[3],world[3],on_tray[3];
        for(int k=0;k<3;++k)local[k]=vertex[k];
        rf_weaver_transform_point(&pose.bones[41],local,world);
        inverse(&pose.bones[0],world,on_tray);
        int supported=0;
        for(unsigned face=0;face<tray.count && !supported;++face) {
            double error=0;
            supported=surface_project(&tray.faces[face],on_tray,&error) && error<1e-7;
        }
        require(supported,"delivered lowest vertex touches a real tray support triangle");
        ++contacts;
    }
    require(contacts>0,"delivery has physical support contacts");
    for(int k=0;k<3;++k)require(fabs(pose.bones[0].position[k]-
        (rf_mesh_weaver_tray_position_m[k]+rf_mesh_weaver_tray_delivery_translation_m[k])*
        RF_WEAVER_LOCAL_UNITS)<1e-8,"tray uses authored delivery translation");
    double expected_z=(rf_mesh_weaver_build_center_m[2]+rf_mesh_weaver_tray_delivery_translation_m[2])*
        RF_WEAVER_LOCAL_UNITS;
    require(fabs(pose.bones[41].position[2]-expected_z)<1e-8,"gun moves forward with tray");
    frame->phase=TOY_WEAVER_DELIVERING;frame->phase_duration_ms=1;frame->phase_ms=1;
    struct rf_weaver_pose last_delivery;rf_weaver_pose_sample(frame,gun,&last_delivery);
    require(!memcmp(last_delivery.bones,pose.bones,sizeof(pose.bones)),"delivery ends at exact ready pose");
    rf_weaver_mesh_free(&tray);
}

static double joint_yaw(const struct rf_weaver_pose *pose,int head)
{
    const double *r=pose->bones[1+head].rotation;
    return atan2(r[2],r[8])*180/3.141592653589793;
}
static double joint_pitch(const struct rf_weaver_pose *pose,int head)
{
    const double *r=pose->bones[9+head].rotation;
    return atan2(-r[5],sqrt(r[2]*r[2]+r[8]*r[8]))*180/3.141592653589793;
}
static double joint_difference(double a,double b)
{
    double delta=a-b;
    while(delta>180)delta-=360;
    while(delta<-180)delta+=360;
    return fabs(delta);
}

static void presentation_frame(const struct toy_mesh_weaver *w,struct rf_mesh_weaver_frame *frame)
{
    memset(frame,0,sizeof(*frame));frame->present=1;frame->output_slot=-1;
    frame->powered=w->enabled && w->supply.power_on && w->supply.energy_kj>0;
    frame->phase=w->phase;frame->pause_reason=w->pause_reason;frame->weapon=w->blueprint.weapon;
    frame->serial=w->job_serial;frame->collected_count=w->collected_count;
    frame->progress=w->phase==TOY_WEAVER_IDLE?0:w->progress;frame->phase_ms=w->phase_ms;
    frame->phase_duration_ms=w->phase==TOY_WEAVER_CALIBRATING?w->coefficients.calibration_ms:
        w->phase==TOY_WEAVER_DELIVERING?w->coefficients.delivery_ms:0;
    frame->time_ms=(unsigned)w->elapsed_ms;
}

static void audit_retraction(const struct rf_weaver_mesh *mesh,struct toy_game *game,const char *name)
{
    struct rf_weaver_presentation_state state={0};
    struct rf_mesh_weaver_frame frame;
    const unsigned long long ready_time=1000000,collect_time=1016000;
    presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&state,&frame,7,ready_time,0);
    struct rf_weaver_presentation_state ready_state=state;
    struct rf_weaver_pose ready,pose;rf_weaver_pose_sample(&frame,mesh,&ready);
    require(toy_game_weaver_collect(game,&game->actors[0])==1,"collect actual completed product");
    struct toy_mesh_weaver authority=game->weaver;
    presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&state,&frame,7,collect_time,0);
    struct rf_weaver_presentation_state collected=state;
    rf_weaver_pose_sample(&frame,mesh,&pose);
    require(frame.phase==TOY_WEAVER_IDLE && frame.tray_extension==1,"collection starts return without restoring product");
    require(!memcmp(&pose.bones[0],&ready.bones[0],sizeof(pose.bones[0])),"collection has zero tray position jump");
    double previous=1,max_step=0;
    for(unsigned ms=16;ms<=400;ms+=16) {
        presentation_frame(&game->weaver,&frame);
        rf_weaver_presentation_update(&state,&frame,7,collect_time+ms*1000,0);
        require(frame.tray_extension>=0 && frame.tray_extension<=previous,"return monotone within physical rail");
        double step=(previous-frame.tray_extension)*205;
        if(step>max_step)max_step=step;
        previous=frame.tray_extension;
        struct rf_weaver_pose gpu,cpu,frozen;
        rf_weaver_pose_sample(&frame,mesh,&gpu);rf_weaver_pose_sample(&frame,NULL,&cpu);
        rf_weaver_pose_sample(&frame,mesh,&frozen);
        require(!memcmp(gpu.bones,cpu.bones,sizeof(gpu.bones)),"CPU/GPU use identical idle return bones");
        require(!memcmp(&gpu,&frozen,sizeof(gpu)),"frozen return frame does not advance while sampled");
    }
    require(frame.tray_extension==0 && !state.returning,"nominal return finishes at 400 ms");
    require(max_step<12.4,"return removes the 205 mm single-frame snap");
    require(!memcmp(&authority,&game->weaver,sizeof(authority)),"visual return consumes no Game time or resources");

    struct rf_weaver_presentation_state a=collected,b=collected;
    presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&a,&frame,7,collect_time+200000,0);
    double direct=frame.tray_extension;
    for(unsigned ms=20;ms<=200;ms+=20) {
        presentation_frame(&game->weaver,&frame);
        rf_weaver_presentation_update(&b,&frame,7,collect_time+ms*1000,0);
    }
    require(fabs(direct-frame.tray_extension)<1e-12,"different presentation steps reach identical return freeze");
    a=collected;presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&a,&frame,7,collect_time+100000,0);
    double held=frame.tray_extension;
    rf_weaver_presentation_update(&a,&frame,7,collect_time+300000,1);
    rf_weaver_presentation_update(&a,&frame,7,collect_time+2300000,1);
    require(frame.tray_extension==held,"application pause freezes return");
    rf_weaver_presentation_update(&a,&frame,7,collect_time+2316000,0);
    require(frame.tray_extension==held,"resume does not consume the paused wall time");
    frame.powered=0;rf_weaver_presentation_update(&a,&frame,7,collect_time+2416000,0);
    require(frame.tray_extension==held,"unpowered tray return is held");
    frame.powered=1;rf_weaver_presentation_update(&a,&frame,7,collect_time+2516000,0);
    require(frame.tray_extension==held,"restoring power does not jump the tray");
    rf_weaver_presentation_update(&a,&frame,7,collect_time+2616000,0);
    require(frame.tray_extension<held,"resumed tray continues returning");
    a=collected;presentation_frame(&game->weaver,&frame);frame.x=1;
    rf_weaver_presentation_update(&a,&frame,7,collect_time+1000,0);
    require(frame.tray_extension==0 && !a.returning,"machine movement clears return state");
    a=collected;presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&a,&frame,8,collect_time+1000,0);
    require(frame.tray_extension==0 && !a.returning,"world change clears return state");
    a=collected;presentation_frame(&game->weaver,&frame);
    rf_weaver_presentation_update(&a,&frame,7,collect_time-1,0);
    require(frame.tray_extension==0 && !a.returning,"clock rollback clears return state");
    a=collected;frame.present=0;rf_weaver_presentation_update(&a,&frame,7,collect_time+1000,0);
    require(!a.valid && !frame.tray_pose_override,"missing machine clears visual state");
    a=collected;rf_weaver_presentation_reset(&a);
    require(!a.valid && !a.returning && a.extension==0,"explicit shutdown reset clears visual state");

    require(toy_game_weaver_start(game,&authority.blueprint)==TOY_WEAVER_OK,"immediately start next actual job");
    struct rf_mesh_weaver_frame calibration;presentation_frame(&game->weaver,&calibration);
    a=collected;presentation_frame(&authority,&frame);
    rf_weaver_presentation_update(&a,&frame,7,collect_time+40000,0);
    double old_extension=a.extension,old_velocity=a.velocity;
    frame=calibration;rf_weaver_presentation_update(&a,&frame,7,collect_time+40000,0);
    require(fabs(a.extension-old_extension)<1e-12 && fabs(a.velocity-old_velocity)<1e-12,
        "new calibration preserves return position and velocity");
    require(a.return_duration_ms<=300,"new calibration shortens remaining return deadline");
    previous=a.extension;
    for(unsigned ms=1;ms<=300;++ms) {
        frame=calibration;frame.phase_ms=ms;
        rf_weaver_presentation_update(&a,&frame,7,collect_time+40000+ms*1000,0);
        require(a.extension>=0 && a.extension<=previous,"retimed Hermite return remains monotone");
        previous=a.extension;
    }
    require(frame.tray_extension==0,"retimed return reaches home by calibration end");

    a=ready_state;frame=calibration;
    rf_weaver_presentation_update(&a,&frame,7,collect_time,0);
    require(frame.tray_extension==1,"collection and next start may skip an idle rendered frame");
    for(unsigned ms=10;ms<=300;ms+=10) {
        toy_game_weaver_update(game,10);presentation_frame(&game->weaver,&frame);
        rf_weaver_presentation_update(&a,&frame,7,collect_time+ms*1000,0);
    }
    require(game->weaver.phase==TOY_WEAVER_WEAVING && frame.tray_extension==0,
        "immediate next actual task starts weaving with tray home");
    __printf("%s tray_return_ms=400 collect_jump_mm=0 max_16ms_step_mm=%.6f next_job_home_ms=300\n",name,max_step);
}

/* Time derivatives use the actual Game resource rules and the production
 * math object. This protects mechanical pacing, which epsilon continuity
 * alone cannot establish. Lower compute/power rates only slow this schedule. */
static void audit_speed(const struct rf_weaver_mesh *mesh,int weapon,const char *name)
{
    const struct toy_mesh_blueprint *blueprint=NULL;
    for(unsigned i=0;i<RF_WEAVER_BLUEPRINT_COUNT;++i)
        if(rf_weaver_blueprints[i].geometry.weapon==weapon)blueprint=&rf_weaver_blueprints[i].geometry;
    struct toy_game *game=calloc(1,sizeof(*game));
    require(game && blueprint,"speed audit has real game and blueprint");
    if(!game || !blueprint){free(game);return;}
    toy_game_init(game,81173);game->weaver.enabled=1;
    require(toy_game_weaver_start(game,blueprint)==TOY_WEAVER_OK,"start real fabrication for speed audit");
    struct rf_mesh_weaver_frame frame={0};
    frame.present=frame.powered=1;frame.phase=TOY_WEAVER_CALIBRATING;
    frame.weapon=weapon;frame.phase_duration_ms=game->weaver.coefficients.calibration_ms;
    struct rf_weaver_pose previous,pose;rf_weaver_pose_sample(&frame,mesh,&previous);
    double max_yaw=0,max_pitch=0;
    unsigned ticks=0,lit_face_changes=0;
    while(game->weaver.phase!=TOY_WEAVER_READY && ticks++<60000) {
        toy_game_weaver_update(game,1);
        const struct toy_mesh_weaver *w=&game->weaver;
        frame.phase=w->phase;frame.progress=w->progress;frame.phase_ms=w->phase_ms;
        frame.time_ms=(unsigned)w->elapsed_ms;
        frame.phase_duration_ms=w->phase==TOY_WEAVER_CALIBRATING?w->coefficients.calibration_ms:
            w->phase==TOY_WEAVER_DELIVERING?w->coefficients.delivery_ms:0;
        rf_weaver_pose_sample(&frame,mesh,&pose);
        for(int h=0;h<8;++h) {
            double dy=joint_difference(joint_yaw(&previous,h),joint_yaw(&pose,h))*1000;
            double dp=joint_difference(joint_pitch(&previous,h),joint_pitch(&pose,h))*1000;
            if(dy>max_yaw)max_yaw=dy;
            if(dp>max_pitch)max_pitch=dp;
            if((previous.beam_mask&pose.beam_mask&(1u<<h)) &&
                previous.beam_face[h]!=pose.beam_face[h])++lit_face_changes;
        }
        previous=pose;
    }
    require(game->weaver.phase==TOY_WEAVER_READY,"speed audit reaches actual completed job");
    require(max_yaw<=180 && max_pitch<=180,"default rig joints stay within 180 degrees per second");
    require(!lit_face_changes,"consecutive lit samples never jump between faces");
    __printf("%s actual_game_1ms elapsed_ms=%.6f max_yaw_dps=%.6f max_pitch_dps=%.6f lit_face_changes=%u\n",
        name,game->weaver.elapsed_ms,max_yaw,max_pitch,lit_face_changes);
    audit_retraction(mesh,game,name);
    free(game);
}
static void audit(int weapon,const char *name)
{
    const struct rasterfall_weapon_asset_profile *profile=rasterfall_weapon_asset_profile(weapon);
    char path[512];snprintf(path,sizeof(path),"%s/%s",repository,profile->model_path);
    struct rf_weaver_mesh mesh={0};
    require(rf_weaver_mesh_load(&mesh,path,weapon)==0,"load complete real weapon");
    if(!mesh.faces)return;
    struct rf_mesh_weaver_frame frame={0};
    frame.present=frame.powered=1;frame.weapon=weapon;frame.phase=TOY_WEAVER_WEAVING;
    double max_surface=0,max_axis=0,max_join=0,max_angle=0;
    unsigned lit=0,heads=0,all_off=0,occluded=0;
    for(unsigned sample=0;sample<=10000;++sample) {
        frame.progress=sample/10000.0;frame.time_ms=sample*17;
        struct rf_weaver_pose pose;rf_weaver_pose_sample(&frame,&mesh,&pose);
        if(!pose.beam_mask)++all_off;
        for(int h=0;h<8;++h) if(pose.beam_mask&(1u<<h)) {
            ++lit;heads|=1u<<h;
            unsigned face=pose.beam_face[h];require(face<mesh.count,"lit face identity");
            if(face>=mesh.count)continue;
            require(mesh.faces[face].threshold>frame.progress-RF_WEAVER_ACTIVE_BEHIND &&
                mesh.faces[face].threshold<=frame.progress+RF_WEAVER_ACTIVE_AHEAD,"lit face inside active band");
            double local[3];inverse(&pose.bones[41],pose.beam_end[h],local);
            double error=0;
            require(surface_project(&mesh.faces[face],local,&error),"lit target inside nondegenerate selected triangle");
            if(error>max_surface)max_surface=error;
            double local_start[3];inverse(&pose.bones[41],pose.beam_start[h],local_start);
            if(reference_occluded(&mesh,local_start,local,frame.progress+RF_WEAVER_ACTIVE_AHEAD))++occluded;
            double aperture[3],expected[3],direction[3],axis[3];
            for(int k=0;k<3;++k) {
                aperture[k]=rf_mesh_weaver_aperture_in_core_m[k]*RF_WEAVER_LOCAL_UNITS;
                direction[k]=pose.beam_end[h][k]-pose.beam_start[h][k];
                axis[k]=pose.bones[9+h].rotation[k*3+2];
            }
            rf_weaver_transform_point(&pose.bones[9+h],aperture,expected);
            require(distance(expected,pose.beam_start[h])<1e-8,"beam starts at optical aperture");
            require(length(direction)>1,"beam extends forward");
            double axis_error=1-dot(direction,axis)/length(direction);
            if(axis_error>max_axis)max_axis=axis_error;
        }
    }
    require(max_surface<1e-7,"surface error below quantization floor");
    require(max_axis<1e-10,"beam direction aligned to real core optical axis");
    require(heads==255,"all eight upper/lower heads participate over the build");
    require(!occluded,"every lit target is the first current surface hit");
    for(int h=0;h<8;++h) for(unsigned bin=1;bin<RF_WEAVER_STROKE_BINS;++bin) {
        static const double events[3]={0,RF_WEAVER_STROKE_MARGIN,1-RF_WEAVER_STROKE_MARGIN};
        for(unsigned event=0;event<3;++event) {
            double progress=(bin+events[event]-h*RF_WEAVER_STROKE_HEAD_OFFSET)/RF_WEAVER_STROKE_BINS;
            struct rf_weaver_pose before,after;
            frame.progress=progress-1e-9;rf_weaver_pose_sample(&frame,&mesh,&before);
            frame.progress=progress+1e-9;rf_weaver_pose_sample(&frame,&mesh,&after);
            double jump=distance(before.beam_end[h],after.beam_end[h]);
            if(jump>max_join)max_join=jump;
            double cosine=0;for(int k=0;k<3;++k)
                cosine+=before.bones[9+h].rotation[k*3+2]*after.bones[9+h].rotation[k*3+2];
            if(cosine>1)cosine=1;
            if(cosine<-1)cosine=-1;
            double angle=atan2(sqrt((1-cosine)*(1+cosine)),cosine)*180/3.141592653589793;
            if(angle>max_angle)max_angle=angle;
            if(!event)require(!(before.beam_mask&(1u<<h)) && !(after.beam_mask&(1u<<h)),
                "face handoff has an unlit interval");
        }
    }
    require(max_join<.01,"target continuous across all schedule joins");
    require(max_angle<.0001,"head optical axis continuous across schedule joins");
    frame.progress=.426875;frame.time_ms=1000;
    struct rf_weaver_pose frozen,replayed,paused;
    rf_weaver_pose_sample(&frame,&mesh,&frozen);
    for(unsigned dt=7;dt<=41;dt+=17) {
        for(unsigned ms=0;ms<1000;ms+=dt) {
            frame.progress=ms/1000.0;frame.time_ms=ms;
            rf_weaver_pose_sample(&frame,&mesh,&replayed);
        }
        frame.progress=.426875;frame.time_ms=1000;
        rf_weaver_pose_sample(&frame,&mesh,&replayed);
        require(!memcmp(&frozen,&replayed,sizeof(frozen)),"different dt/history reaches identical freeze pose");
    }
    frame.time_ms=987654321;rf_weaver_pose_sample(&frame,&mesh,&replayed);
    require(!memcmp(&frozen,&replayed,sizeof(frozen)),"frozen progress ignores wall clock");
    frame.pause_reason=TOY_WEAVER_NO_POWER;rf_weaver_pose_sample(&frame,&mesh,&paused);
    require(!paused.beam_mask,"paused beam disabled");
    require(!memcmp(frozen.bones,paused.bones,sizeof(frozen.bones)),"paused skeleton frozen");
    frame.pause_reason=0;frame.powered=0;rf_weaver_pose_sample(&frame,&mesh,&paused);
    require(!paused.beam_mask,"unpowered beam disabled");
    audit_support(&mesh,&frame);
    audit_speed(&mesh,weapon,name);
    __printf("%s faces=%u lit_samples=%u heads=0x%02x all_off=%u/10001 occluded=%u surface_mm=%.12g axis_error=%.12g join_mm=%.12g join_degrees=%.12g\n",
        name,mesh.count,lit,heads,all_off,occluded,max_surface*1000/RF_WEAVER_LOCAL_UNITS,max_axis,
        max_join*1000/RF_WEAVER_LOCAL_UNITS,max_angle);
    rf_weaver_mesh_free(&mesh);
}
int main(int argc,char **argv)
{
    if(argc!=2) {__printf("usage: mesh-weaver-geometry.exe repository-root\n");return 2;}
    repository=argv[1];
    audit(TOY_GAME_WEAPON_AK,"AK");audit(TOY_GAME_WEAPON_PISTOL,"Pistol");
    audit(TOY_GAME_WEAPON_SMG,"SMG");audit(TOY_GAME_WEAPON_SHOTGUN,"Shotgun");
    __printf("weaver_geometry_audit failures=%d\n",failures);return failures?1:0;
}
