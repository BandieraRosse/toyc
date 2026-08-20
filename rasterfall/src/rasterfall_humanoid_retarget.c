#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_humanoid_retarget.h"
#include "math.h"

static void quat_identity(double *q) { q[0]=q[1]=q[2]=0.0;q[3]=1.0; }
static void quat_copy(const double *q,double *out){memcpy(out,q,4*sizeof(double));}
static void quat_inverse(const double *q,double *out)
{
    double length=q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3];
    out[0]=-q[0]/length;out[1]=-q[1]/length;out[2]=-q[2]/length;out[3]=q[3]/length;
}
static void quat_multiply(const double *a,const double *b,double *out)
{
    double q[4];
    q[0]=a[3]*b[0]+a[0]*b[3]+a[1]*b[2]-a[2]*b[1];
    q[1]=a[3]*b[1]-a[0]*b[2]+a[1]*b[3]+a[2]*b[0];
    q[2]=a[3]*b[2]+a[0]*b[1]-a[1]*b[0]+a[2]*b[3];
    q[3]=a[3]*b[3]-a[0]*b[0]-a[1]*b[1]-a[2]*b[2];
    quat_copy(q,out);
}
static int quat_normalize(double *q)
{
    double length=sqrt(q[0]*q[0]+q[1]*q[1]+q[2]*q[2]+q[3]*q[3]);
    if(length<0.000000001)return -1;
    q[0]/=length;q[1]/=length;q[2]/=length;q[3]/=length;
    if(q[3]<0.0){q[0]=-q[0];q[1]=-q[1];q[2]=-q[2];q[3]=-q[3];}
    return 0;
}
static void quat_conjugate_by(const double *basis,const double *rotation,
                              double *out)
{
    double inverse[4],temp[4];quat_inverse(basis,inverse);
    quat_multiply(basis,rotation,temp);quat_multiply(temp,inverse,out);
}
static void quat_to_canonical(const double *basis,const double *global,
                              double *out)
{
    double inverse[4],temp[4];quat_inverse(basis,inverse);
    quat_multiply(inverse,global,temp);quat_multiply(temp,basis,out);
}

static const signed char humanoid_parents[RASTERFALL_HUMANOID_BONE_COUNT]={
    -1,0,1,2,3,4,5,4,7,8,9,4,11,12,13,1,15,16,1,18,19
};

void rasterfall_humanoid_rotation_skeleton_identity(
    struct rasterfall_humanoid_rotation_skeleton *skeleton)
{
    int i;__memset(skeleton,0,sizeof(*skeleton));
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){
        quat_identity(skeleton->rest_global[i]);skeleton->parent[i]=humanoid_parents[i];
    }
}
void rasterfall_humanoid_rotation_pose_bind(
    const struct rasterfall_humanoid_rotation_skeleton *skeleton,
    struct rasterfall_humanoid_rotation_pose *pose)
{
    int i;for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++)quat_copy(skeleton->rest_global[i],pose->global[i]);
}

int rasterfall_humanoid_retarget_rotations_from_reference(
    const struct rasterfall_humanoid_rotation_skeleton *source,
    const struct rasterfall_humanoid_rotation_pose *source_pose,
    const struct rasterfall_humanoid_rotation_pose *source_reference,
    unsigned int reference_mask,
    const struct rasterfall_humanoid_rest_basis *source_basis,
    const struct rasterfall_humanoid_rotation_skeleton *target,
    const struct rasterfall_humanoid_rotation_pose *target_reference,
    const struct rasterfall_humanoid_rest_basis *target_basis,
    struct rasterfall_humanoid_retarget_result *result)
{
    int bone;
    if(!source||!source_pose||!source_basis||!target||!target_basis||!result)return -1;
    for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){
        double inverse_rest[4],source_delta[4],canonical_delta[4];
        double target_delta[4],target_global[4],parent_inverse[4];int parent=target->parent[bone];
        if(!source_basis[bone].valid||!target_basis[bone].valid||parent>=bone||parent<-1)return -1;
        const double *source_origin=((reference_mask&(1u<<bone))&&source_reference)?
            source_reference->global[bone]:source->rest_global[bone];
        const double *target_origin=((reference_mask&(1u<<bone))&&target_reference)?
            target_reference->global[bone]:target->rest_global[bone];
        /* Reference-aligned bones remove the source clip's static relaxed
         * pose instead of treating bind-to-reference as animation motion. */
        quat_inverse(source_origin,inverse_rest);
        /* Active global delta: animated global followed by inverse origin. */
        quat_multiply(source_pose->global[bone],inverse_rest,source_delta);
        quat_to_canonical(source_basis[bone].rotation,source_delta,canonical_delta);
        quat_conjugate_by(target_basis[bone].rotation,canonical_delta,target_delta);
        quat_multiply(target_delta,target_origin,target_global);
        if(quat_normalize(target_global)<0)return -1;
        quat_copy(target_global,result->global_rotation[bone]);
        if(parent<0)quat_copy(target_global,result->local_rotation[bone]);
        else{
            quat_inverse(result->global_rotation[parent],parent_inverse);
            quat_multiply(parent_inverse,target_global,result->local_rotation[bone]);
        }
        if(quat_normalize(result->local_rotation[bone])<0)return -1;
    }
    return 0;
}

int rasterfall_humanoid_retarget_rotations(
    const struct rasterfall_humanoid_rotation_skeleton *source,
    const struct rasterfall_humanoid_rotation_pose *source_pose,
    const struct rasterfall_humanoid_rest_basis *source_basis,
    const struct rasterfall_humanoid_rotation_skeleton *target,
    const struct rasterfall_humanoid_rest_basis *target_basis,
    struct rasterfall_humanoid_retarget_result *result)
{
    return rasterfall_humanoid_retarget_rotations_from_reference(
        source,source_pose,0,0,source_basis,target,0,target_basis,result);
}

void rasterfall_humanoid_synthetic_delta(int bone,int degrees,double delta[4])
{
    double axis[3]={1,0,0},angle=degrees*M_PI/180.0,half;
    if(bone==RASTERFALL_HUMANOID_LEFT_UPPER_ARM||bone==RASTERFALL_HUMANOID_RIGHT_UPPER_ARM){axis[0]=0;axis[1]=1;}
    else if(bone==RASTERFALL_HUMANOID_LEFT_UPPER_LEG||bone==RASTERFALL_HUMANOID_RIGHT_UPPER_LEG){axis[0]=0;axis[2]=1;}
    half=angle/2.0;delta[0]=axis[0]*sin(half);delta[1]=axis[1]*sin(half);delta[2]=axis[2]*sin(half);delta[3]=cos(half);
}

static void basis_from_z_rotation(int degrees,struct rasterfall_humanoid_rest_basis *basis)
{
    double a=degrees*M_PI/180.0,c=cos(a),s=sin(a);
    basis->primary[0]=c;basis->primary[1]=s;basis->primary[2]=0;
    basis->secondary[0]=-s;basis->secondary[1]=c;basis->secondary[2]=0;
    basis->third[0]=basis->third[1]=0;basis->third[2]=1;
    basis->rotation[0]=basis->rotation[1]=0;basis->rotation[2]=sin(a/2);basis->rotation[3]=cos(a/2);basis->valid=1;
}
static double quat_difference(const double *a,const double *b)
{
    double dot=a[0]*b[0]+a[1]*b[1]+a[2]*b[2]+a[3]*b[3];if(dot<0)dot=-dot;return 1.0-dot;
}

int rasterfall_humanoid_retarget_logic_test(void)
{
    struct rasterfall_humanoid_rotation_skeleton source,target;
    struct rasterfall_humanoid_rotation_pose pose,reference,target_reference;
    struct rasterfall_humanoid_retarget_result result,second;
    struct rasterfall_humanoid_rest_basis sb[RASTERFALL_HUMANOID_BONE_COUNT],tb[RASTERFALL_HUMANOID_BONE_COUNT];
    double identity[4]={0,0,0,1},delta[4],global_delta[4],expected[4],inverse[4],roundtrip[4];int i,bone;
    rasterfall_humanoid_rotation_skeleton_identity(&source);rasterfall_humanoid_rotation_skeleton_identity(&target);
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){basis_from_z_rotation(37,&sb[i]);basis_from_z_rotation(-23,&tb[i]);}
    rasterfall_humanoid_rotation_pose_bind(&source,&pose);
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&result)<0)return 1;
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++)if(quat_difference(result.local_rotation[i],identity)>0.000000001)return 2;
    bone=RASTERFALL_HUMANOID_RIGHT_UPPER_ARM;rasterfall_humanoid_synthetic_delta(bone,-35,delta);
    quat_conjugate_by(sb[bone].rotation,delta,global_delta);quat_multiply(global_delta,source.rest_global[bone],pose.global[bone]);
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&result)<0)return 3;
    quat_conjugate_by(tb[bone].rotation,delta,expected);if(quat_difference(result.global_rotation[bone],expected)>0.000000001)return 4;
    /* Sign reversal must produce the exact inverse motion. */
    rasterfall_humanoid_synthetic_delta(bone,35,delta);quat_conjugate_by(sb[bone].rotation,delta,global_delta);quat_copy(global_delta,pose.global[bone]);
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&second)<0)return 5;
    quat_inverse(result.global_rotation[bone],inverse);if(quat_difference(second.global_rotation[bone],inverse)>0.000000001)return 6;
    /* All four required anatomical motions close through different bases. */
    {
        static const int bones[4]={RASTERFALL_HUMANOID_RIGHT_UPPER_ARM,RASTERFALL_HUMANOID_LEFT_UPPER_ARM,RASTERFALL_HUMANOID_RIGHT_UPPER_LEG,RASTERFALL_HUMANOID_CHEST};
        static const int angles[4]={-35,35,30,30};int test;
        for(test=0;test<4;test++){
            rasterfall_humanoid_rotation_pose_bind(&source,&pose);bone=bones[test];
            rasterfall_humanoid_synthetic_delta(bone,angles[test],delta);
            quat_conjugate_by(sb[bone].rotation,delta,global_delta);quat_copy(global_delta,pose.global[bone]);
            if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&result)<0)return 11+test;
            quat_conjugate_by(tb[bone].rotation,delta,expected);
            if(quat_difference(result.global_rotation[bone],expected)>0.000000001)return 15+test;
            {double length=result.local_rotation[bone][0]*result.local_rotation[bone][0]+result.local_rotation[bone][1]*result.local_rotation[bone][1]+result.local_rotation[bone][2]*result.local_rotation[bone][2]+result.local_rotation[bone][3]*result.local_rotation[bone][3];if(length<0.999999||length>1.000001)return 19+test;}
        }
    }
    /* Parent global rotation is removed from an inheriting child's local pose. */
    rasterfall_humanoid_rotation_pose_bind(&source,&pose);bone=RASTERFALL_HUMANOID_CHEST;rasterfall_humanoid_synthetic_delta(bone,30,delta);
    quat_conjugate_by(sb[bone].rotation,delta,global_delta);quat_copy(global_delta,pose.global[bone]);quat_copy(global_delta,pose.global[RASTERFALL_HUMANOID_UPPER_CHEST]);
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&result)<0)return 7;
    if(quat_difference(result.local_rotation[RASTERFALL_HUMANOID_UPPER_CHEST],identity)>0.000000001)return 8;
    quat_multiply(result.global_rotation[bone],result.local_rotation[RASTERFALL_HUMANOID_UPPER_CHEST],roundtrip);
    if(quat_difference(roundtrip,result.global_rotation[RASTERFALL_HUMANOID_UPPER_CHEST])>0.000000001)return 9;
    if(rasterfall_humanoid_retarget_rotations(&source,&pose,sb,&target,tb,&second)<0||
       quat_difference(second.local_rotation[bone],result.local_rotation[bone])>0.000000001)return 10;
    /* A non-bind source reference maps exactly to the target reference at t=0. */
    rasterfall_humanoid_rotation_pose_bind(&source,&reference);
    rasterfall_humanoid_rotation_pose_bind(&target,&target_reference);
    for(bone=1;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++){
        rasterfall_humanoid_synthetic_delta(bone,20+bone,delta);
        quat_conjugate_by(sb[bone].rotation,delta,global_delta);
        quat_copy(global_delta,reference.global[bone]);quat_copy(global_delta,pose.global[bone]);
    }
    if(rasterfall_humanoid_retarget_rotations_from_reference(&source,&pose,&reference,
       ((1u<<RASTERFALL_HUMANOID_BONE_COUNT)-1u)&~1u,sb,&target,&target_reference,tb,&result)<0)return 23;
    for(bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++)
        if(quat_difference(result.global_rotation[bone],identity)>0.000000001)return 24;
    return 0;
}
