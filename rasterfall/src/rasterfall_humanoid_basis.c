#include "core.h"
#include "tlibc_everything.h"
#include "rasterfall_humanoid_basis.h"
#include "math.h"

#define BASIS_EPSILON 0.0000001

static double dot3(const double *a, const double *b)
{ return a[0]*b[0] + a[1]*b[1] + a[2]*b[2]; }
static void cross3(const double *a, const double *b, double *out)
{
    out[0] = a[1]*b[2] - a[2]*b[1];
    out[1] = a[2]*b[0] - a[0]*b[2];
    out[2] = a[0]*b[1] - a[1]*b[0];
}
static int normalize3(double *v)
{
    double length = sqrt(dot3(v, v));
    if (length < BASIS_EPSILON) return -1;
    v[0] /= length; v[1] /= length; v[2] /= length; return 0;
}
static int direction(const struct rasterfall_humanoid_point *from,
                     const struct rasterfall_humanoid_point *to, double *out)
{
    if (!from->valid || !to->valid) return -1;
    out[0] = to->value[0] - from->value[0];
    out[1] = to->value[1] - from->value[1];
    out[2] = to->value[2] - from->value[2];
    return normalize3(out);
}
static int orthogonalize(const double *primary, const double *reference,
                         double *secondary, double *third)
{
    double projection = dot3(primary, reference);
    secondary[0] = reference[0] - primary[0] * projection;
    secondary[1] = reference[1] - primary[1] * projection;
    secondary[2] = reference[2] - primary[2] * projection;
    if (normalize3(secondary) < 0) return -1;
    cross3(primary, secondary, third);
    return normalize3(third);
}
static void matrix_to_quaternion(const double *p, const double *s,
                                 const double *t, double *q)
{
    double m00=p[0],m01=s[0],m02=t[0],m10=p[1],m11=s[1],m12=t[1];
    double m20=p[2],m21=s[2],m22=t[2], trace=m00+m11+m22, scale;
    if (trace > 0.0) {
        scale = sqrt(trace + 1.0) * 2.0;
        q[3]=0.25*scale; q[0]=(m21-m12)/scale;
        q[1]=(m02-m20)/scale; q[2]=(m10-m01)/scale;
    } else if (m00 > m11 && m00 > m22) {
        scale=sqrt(1.0+m00-m11-m22)*2.0; q[3]=(m21-m12)/scale;
        q[0]=0.25*scale; q[1]=(m01+m10)/scale; q[2]=(m02+m20)/scale;
    } else if (m11 > m22) {
        scale=sqrt(1.0+m11-m00-m22)*2.0; q[3]=(m02-m20)/scale;
        q[0]=(m01+m10)/scale; q[1]=0.25*scale; q[2]=(m12+m21)/scale;
    } else {
        scale=sqrt(1.0+m22-m00-m11)*2.0; q[3]=(m10-m01)/scale;
        q[0]=(m02+m20)/scale; q[1]=(m12+m21)/scale; q[2]=0.25*scale;
    }
    if (q[3] < 0.0 || (q[3] < BASIS_EPSILON &&
        (q[0] < 0.0 || (q[0] < BASIS_EPSILON &&
         (q[1] < 0.0 || (q[1] < BASIS_EPSILON && q[2] < 0.0)))))) {
        q[0]=-q[0]; q[1]=-q[1]; q[2]=-q[2]; q[3]=-q[3];
    }
}
static void quaternion_to_matrix(const double *q, double *p, double *s, double *t)
{
    double x=q[0],y=q[1],z=q[2],w=q[3];
    p[0]=1-2*(y*y+z*z);p[1]=2*(x*y+z*w);p[2]=2*(x*z-y*w);
    s[0]=2*(x*y-z*w);s[1]=1-2*(x*x+z*z);s[2]=2*(y*z+x*w);
    t[0]=2*(x*z+y*w);t[1]=2*(y*z-x*w);t[2]=1-2*(x*x+y*y);
}
static int primary_target(int bone)
{
    static const signed char targets[RASTERFALL_HUMANOID_BONE_COUNT] = {
        -1, RASTERFALL_HUMANOID_SPINE, RASTERFALL_HUMANOID_CHEST,
        RASTERFALL_HUMANOID_UPPER_CHEST, RASTERFALL_HUMANOID_NECK,
        RASTERFALL_HUMANOID_HEAD, -1,
        RASTERFALL_HUMANOID_LEFT_UPPER_ARM, RASTERFALL_HUMANOID_LEFT_FOREARM,
        RASTERFALL_HUMANOID_LEFT_HAND, -1,
        RASTERFALL_HUMANOID_RIGHT_UPPER_ARM, RASTERFALL_HUMANOID_RIGHT_FOREARM,
        RASTERFALL_HUMANOID_RIGHT_HAND, -1,
        RASTERFALL_HUMANOID_LEFT_LOWER_LEG, RASTERFALL_HUMANOID_LEFT_FOOT, -1,
        RASTERFALL_HUMANOID_RIGHT_LOWER_LEG, RASTERFALL_HUMANOID_RIGHT_FOOT, -1
    };
    return targets[bone];
}
static int choose_fallback_reference(const double *primary,
                                     const double *up, const double *forward,
                                     double *reference)
{
    double lateral[3], up_dot, forward_dot, lateral_dot;
    cross3(up, forward, lateral);
    up_dot=dot3(primary,up); if(up_dot<0)up_dot=-up_dot;
    forward_dot=dot3(primary,forward); if(forward_dot<0)forward_dot=-forward_dot;
    lateral_dot=dot3(primary,lateral); if(lateral_dot<0)lateral_dot=-lateral_dot;
    if (forward_dot <= up_dot && forward_dot <= lateral_dot) memcpy(reference,forward,24);
    else if (up_dot <= lateral_dot) memcpy(reference,up,24);
    else memcpy(reference,lateral,24);
    return 0;
}

int rasterfall_humanoid_build_rest_bases(
    const struct rasterfall_humanoid_basis_input *input,
    struct rasterfall_humanoid_rest_basis *bases)
{
    double up[3], forward[3]; int bone;
    if (!input || !bases) return -1;
    memcpy(up,input->model_up,24); memcpy(forward,input->model_forward,24);
    if (normalize3(up)<0 || normalize3(forward)<0) return -1;
    __memset(bases,0,sizeof(*bases)*RASTERFALL_HUMANOID_BONE_COUNT);
    for (bone=0;bone<RASTERFALL_HUMANOID_BONE_COUNT;bone++) {
        struct rasterfall_humanoid_rest_basis *basis=&bases[bone];
        double reference[3]; int target=primary_target(bone), used_fallback=0;
        if (!input->bones[bone].valid) continue;
        if (bone==RASTERFALL_HUMANOID_ROOT) {
            memcpy(basis->primary,up,24); basis->source="model axes";
        } else if (bone==RASTERFALL_HUMANOID_HEAD) {
            if (direction(&input->bones[RASTERFALL_HUMANOID_NECK],&input->bones[bone],basis->primary)<0)
                memcpy(basis->primary,up,24);
            basis->source="neck-head + chest forward";
        } else if (bone==RASTERFALL_HUMANOID_LEFT_HAND || bone==RASTERFALL_HUMANOID_RIGHT_HAND) {
            const struct rasterfall_humanoid_point *middle=bone==RASTERFALL_HUMANOID_LEFT_HAND?&input->left_middle:&input->right_middle;
            if (direction(&input->bones[bone],middle,basis->primary)<0) {
                int forearm=bone==RASTERFALL_HUMANOID_LEFT_HAND?RASTERFALL_HUMANOID_LEFT_FOREARM:RASTERFALL_HUMANOID_RIGHT_FOREARM;
                if (direction(&input->bones[forearm],&input->bones[bone],basis->primary)<0) continue;
                used_fallback=1;
            }
            basis->source=used_fallback?"forearm direction fallback":"wrist-middle + thumb";
        } else if (bone==RASTERFALL_HUMANOID_LEFT_FOOT || bone==RASTERFALL_HUMANOID_RIGHT_FOOT) {
            const struct rasterfall_humanoid_point *toe=bone==RASTERFALL_HUMANOID_LEFT_FOOT?&input->left_toe:&input->right_toe;
            if (direction(&input->bones[bone],toe,basis->primary)<0) { memcpy(basis->primary,forward,24); used_fallback=1; }
            basis->source=used_fallback?"model forward fallback":"ankle-toe";
        } else if (bone==RASTERFALL_HUMANOID_HIPS) {
            if (direction(&input->bones[bone],&input->bones[RASTERFALL_HUMANOID_SPINE],basis->primary)<0) memcpy(basis->primary,up,24);
            basis->source="hips-spine + bilateral legs";
        } else if (target>=0 && direction(&input->bones[bone],&input->bones[target],basis->primary)==0) {
            basis->source="semantic chain";
        } else { memcpy(basis->primary,up,24); basis->source="model up fallback"; used_fallback=1; }

        memcpy(reference,forward,24);
        if (bone==RASTERFALL_HUMANOID_HEAD && bases[RASTERFALL_HUMANOID_UPPER_CHEST].valid)
            memcpy(reference,bases[RASTERFALL_HUMANOID_UPPER_CHEST].secondary,24);
        if (bone==RASTERFALL_HUMANOID_LEFT_HAND || bone==RASTERFALL_HUMANOID_RIGHT_HAND) {
            const struct rasterfall_humanoid_point *thumb=bone==RASTERFALL_HUMANOID_LEFT_HAND?&input->left_thumb:&input->right_thumb;
            if (direction(&input->bones[bone],thumb,reference)<0) { memcpy(reference,forward,24); used_fallback=1; }
        }
        if (orthogonalize(basis->primary,reference,basis->secondary,basis->third)<0) {
            choose_fallback_reference(basis->primary,up,forward,reference);
            if (orthogonalize(basis->primary,reference,basis->secondary,basis->third)<0) continue;
            used_fallback=1;
        }
        /* Hips lateral sign is anchored by the named left/right leg, avoiding
         * a model-axis convention silently swapping anatomical sides. */
        if (bone==RASTERFALL_HUMANOID_HIPS) {
            double left[3];
            if (direction(&input->bones[RASTERFALL_HUMANOID_RIGHT_UPPER_LEG],
                          &input->bones[RASTERFALL_HUMANOID_LEFT_UPPER_LEG],left)==0 &&
                dot3(basis->third,left)<0.0) {
                int i; for(i=0;i<3;i++){basis->secondary[i]=-basis->secondary[i];basis->third[i]=-basis->third[i];}
            }
        }
        matrix_to_quaternion(basis->primary,basis->secondary,basis->third,basis->rotation);
        basis->confidence=used_fallback?RASTERFALL_HUMANOID_BASIS_LOW:
            (bone==RASTERFALL_HUMANOID_LEFT_HAND||bone==RASTERFALL_HUMANOID_RIGHT_HAND)?RASTERFALL_HUMANOID_BASIS_MEDIUM:RASTERFALL_HUMANOID_BASIS_HIGH;
        basis->valid=1;
    }
    return 0;
}

int rasterfall_humanoid_validate_rest_bases(
    const struct rasterfall_humanoid_rest_basis *bases,double *maximum_error)
{
    int i; double maximum=0.0;
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++) {
        const struct rasterfall_humanoid_rest_basis *b=&bases[i]; double errors[7]; int j;
        if(!b->valid)return -1;
        errors[0]=dot3(b->primary,b->primary)-1;errors[1]=dot3(b->secondary,b->secondary)-1;errors[2]=dot3(b->third,b->third)-1;
        errors[3]=dot3(b->primary,b->secondary);errors[4]=dot3(b->primary,b->third);errors[5]=dot3(b->secondary,b->third);
        {double cross[3];cross3(b->primary,b->secondary,cross);errors[6]=dot3(cross,b->third)-1;}
        for(j=0;j<7;j++){if(errors[j]<0)errors[j]=-errors[j];if(errors[j]>maximum)maximum=errors[j];}
        { double q=dot3(b->rotation,b->rotation)+b->rotation[3]*b->rotation[3]-1; if(q<0)q=-q;if(q>maximum)maximum=q; }
        { double p[3],s[3],t[3];quaternion_to_matrix(b->rotation,p,s,t);
          for(j=0;j<3;j++){double e=p[j]-b->primary[j];if(e<0)e=-e;if(e>maximum)maximum=e;e=s[j]-b->secondary[j];if(e<0)e=-e;if(e>maximum)maximum=e;e=t[j]-b->third[j];if(e<0)e=-e;if(e>maximum)maximum=e;} }
    }
    if(maximum_error)*maximum_error=maximum;
    return maximum<0.000001?0:-1;
}

int rasterfall_humanoid_validate_anatomy(
    const struct rasterfall_humanoid_rest_basis *bases)
{
    if (!bases) return -1;
    if (dot3(bases[RASTERFALL_HUMANOID_LEFT_UPPER_ARM].primary,
             bases[RASTERFALL_HUMANOID_RIGHT_UPPER_ARM].primary) >= 0.0)
        return -1;
    if (dot3(bases[RASTERFALL_HUMANOID_LEFT_UPPER_LEG].primary,
             bases[RASTERFALL_HUMANOID_RIGHT_UPPER_LEG].primary) <= 0.8)
        return -1;
    if (dot3(bases[RASTERFALL_HUMANOID_SPINE].primary,
             bases[RASTERFALL_HUMANOID_HEAD].primary) <= 0.5)
        return -1;
    return 0;
}

const char *rasterfall_humanoid_basis_confidence_name(int confidence)
{ return confidence==RASTERFALL_HUMANOID_BASIS_HIGH?"high":confidence==RASTERFALL_HUMANOID_BASIS_MEDIUM?"medium":"low"; }

int rasterfall_humanoid_basis_logic_test(void)
{
    struct rasterfall_humanoid_basis_input input;struct rasterfall_humanoid_rest_basis bases[RASTERFALL_HUMANOID_BONE_COUNT];double error;int i;
    __memset(&input,0,sizeof(input));input.model_up[1]=1;input.model_forward[2]=1;
    for(i=0;i<RASTERFALL_HUMANOID_BONE_COUNT;i++){input.bones[i].valid=1;input.bones[i].value[1]=i+1;}
    input.bones[RASTERFALL_HUMANOID_LEFT_UPPER_ARM].value[0]=1;input.bones[RASTERFALL_HUMANOID_LEFT_FOREARM].value[0]=2;input.bones[RASTERFALL_HUMANOID_LEFT_HAND].value[0]=3;
    input.bones[RASTERFALL_HUMANOID_RIGHT_UPPER_ARM].value[0]=-1;input.bones[RASTERFALL_HUMANOID_RIGHT_FOREARM].value[0]=-2;input.bones[RASTERFALL_HUMANOID_RIGHT_HAND].value[0]=-3;
    input.bones[8].value[1]=input.bones[9].value[1]=input.bones[10].value[1]=10;
    input.bones[12].value[1]=input.bones[13].value[1]=input.bones[14].value[1]=10;
    input.left_middle.valid=input.right_middle.valid=input.left_thumb.valid=input.right_thumb.valid=1;
    input.left_middle.value[0]=4;input.left_middle.value[1]=input.bones[10].value[1];input.right_middle.value[0]=-4;input.right_middle.value[1]=input.bones[14].value[1];
    input.left_thumb.value[0]=3;input.left_thumb.value[2]=1;input.right_thumb.value[0]=-3;input.right_thumb.value[2]=1;
    input.left_toe.valid=input.right_toe.valid=1;input.left_toe.value[2]=input.right_toe.value[2]=1;
    input.left_toe.value[1]=input.bones[17].value[1];input.right_toe.value[1]=input.bones[20].value[1];
    if(rasterfall_humanoid_build_rest_bases(&input,bases)<0||rasterfall_humanoid_validate_rest_bases(bases,&error)<0)return 1;
    if(bases[8].primary[0]<0.9||bases[12].primary[0]>-0.9)return 2;
    if(rasterfall_humanoid_validate_anatomy(bases)<0)return 3;
    return 0;
}
