#include "tlibc_everything.h"
#include "rf_mesh_weaver_presentation.h"
#include "rasterfall_model.h"
#include "rasterfall_calibration.h"
#include "math.h"
#include "limits.h"
#include "rf_mesh_weaver_layout_generated.h"

#ifdef X86_64_TLIBC
/* The freestanding allocator is zero-filled and paired with tlibc_free.
 * Its unrelated thread-arena malloc has no individual free operation. */
static void *weaver_calloc(size_t count,size_t size)
{
    if(size && count>(size_t)-1/size) return NULL;
    return tlibc_malloc(count*size);
}
#define weaver_malloc tlibc_malloc
#define weaver_free tlibc_free

/* Tinylibc has no qsort. Keep the same total-order comparators as the hosted
 * path; this bounded-stack heapsort needs no second mesh-sized allocation. */
static void weaver_sort_swap(unsigned char *a,unsigned char *b,size_t size)
{
    for(size_t i=0;i<size;++i) {unsigned char value=a[i];a[i]=b[i];b[i]=value;}
}
static void weaver_sort_sift(unsigned char *base,size_t root,size_t count,
    size_t size,int (*compare)(const void *,const void *))
{
    while(root<count/2) {
        size_t child=root*2+1;
        if(child+1<count && compare(base+child*size,base+(child+1)*size)<0) ++child;
        if(compare(base+root*size,base+child*size)>=0) return;
        weaver_sort_swap(base+root*size,base+child*size,size);root=child;
    }
}
static void weaver_sort(void *values,size_t count,size_t size,
    int (*compare)(const void *,const void *))
{
    unsigned char *base=values;
    if(count<2 || !size) return;
    for(size_t root=count/2;root>0;--root) weaver_sort_sift(base,root-1,count,size,compare);
    for(size_t end=count;end>1;--end) {
        weaver_sort_swap(base,base+(end-1)*size,size);
        weaver_sort_sift(base,0,end-1,size,compare);
    }
}
#else
#define weaver_calloc calloc
#define weaver_malloc malloc
#define weaver_free free
#define weaver_sort qsort
#endif

static unsigned weaver_u32(const unsigned char *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8|(unsigned)p[2]<<16|(unsigned)p[3]<<24; }
static unsigned weaver_u16(const unsigned char *p)
{ return (unsigned)p[0]|(unsigned)p[1]<<8; }
static int weaver_round(double x) { return (int)(x+(x<0 ? -.5 : .5)); }
static int weaver_strokes_create(struct rf_weaver_mesh *);

/* Static topology acceleration belongs to the immutable weapon cache. A
 * pathological ray exceeds a fixed budget by switching the beam off. */
struct rf_weaver_ray_node {
    int minimum[3],maximum[3];
    unsigned child[2],first,count;
    double threshold_minimum;
};
struct rf_weaver_ray_cache {
    struct rf_weaver_ray_node *nodes;
    unsigned *faces,count;
};

static unsigned weaver_ray_node_build(struct rf_weaver_mesh *mesh,unsigned first,unsigned count)
{
    struct rf_weaver_ray_cache *cache=mesh->rays;
    unsigned index=cache->count++;
    struct rf_weaver_ray_node *node=&cache->nodes[index];
    int center_minimum[3]={INT_MAX,INT_MAX,INT_MAX};
    int center_maximum[3]={INT_MIN,INT_MIN,INT_MIN};
    for(int axis=0;axis<3;++axis) {node->minimum[axis]=INT_MAX;node->maximum[axis]=INT_MIN;}
    node->first=first;node->count=count;node->threshold_minimum=2;
    for(unsigned i=first;i<first+count;++i) {
        const struct rf_weaver_face *face=&mesh->faces[cache->faces[i]];
        if(face->threshold<node->threshold_minimum) node->threshold_minimum=face->threshold;
        for(int axis=0;axis<3;++axis) {
            int center=0;
            for(int corner=0;corner<3;++corner) {
                int value=face->vertex[corner].position[axis];center+=value;
                if(value<node->minimum[axis]) node->minimum[axis]=value;
                if(value>node->maximum[axis]) node->maximum[axis]=value;
            }
            if(center<center_minimum[axis]) center_minimum[axis]=center;
            if(center>center_maximum[axis]) center_maximum[axis]=center;
        }
    }
    if(count<=8) return index;
    int axis=0;
    for(int k=1;k<3;++k) if(center_maximum[k]-center_minimum[k]>
        center_maximum[axis]-center_minimum[axis]) axis=k;
    int split=center_minimum[axis]+(center_maximum[axis]-center_minimum[axis])/2;
    unsigned lo=first,hi=first+count;
    while(lo<hi) {
        const struct rf_weaver_face *face=&mesh->faces[cache->faces[lo]];
        int center=face->vertex[0].position[axis]+face->vertex[1].position[axis]+
            face->vertex[2].position[axis];
        if(center<=split) ++lo;
        else {unsigned swap=cache->faces[lo];cache->faces[lo]=cache->faces[--hi];cache->faces[hi]=swap;}
    }
    if(lo==first || lo==first+count) lo=first+count/2;
    node->count=0;
    node->child[0]=weaver_ray_node_build(mesh,first,lo-first);
    node->child[1]=weaver_ray_node_build(mesh,lo,first+count-lo);
    return index;
}

static int weaver_rays_create(struct rf_weaver_mesh *mesh)
{
    mesh->rays=weaver_calloc(1,sizeof(*mesh->rays));
    if(!mesh->rays) return -1;
    mesh->rays->nodes=weaver_calloc((size_t)mesh->count*2,sizeof(*mesh->rays->nodes));
    mesh->rays->faces=weaver_malloc((size_t)mesh->count*sizeof(*mesh->rays->faces));
    if(!mesh->rays->nodes || !mesh->rays->faces) return -1;
    for(unsigned i=0;i<mesh->count;++i) mesh->rays->faces[i]=i;
    weaver_ray_node_build(mesh,0,mesh->count);return 0;
}

static int weaver_ray_box(const struct rf_weaver_ray_node *node,
    const double origin[3],const double direction[3],double maximum)
{
    double near=0,far=maximum;
    for(int axis=0;axis<3;++axis) {
        if(fabs(direction[axis])<1e-12) {
            if(origin[axis]<node->minimum[axis] || origin[axis]>node->maximum[axis]) return 0;
        } else {
            double a=(node->minimum[axis]-origin[axis])/direction[axis];
            double b=(node->maximum[axis]-origin[axis])/direction[axis];
            if(a>b) {double swap=a;a=b;b=swap;}
            if(a>near) near=a;
            if(b<far) far=b;
            if(near>far) return 0;
        }
    }
    return 1;
}

static int weaver_ray_triangle(const struct rf_weaver_face *face,
    const double origin[3],const double direction[3],double maximum)
{
    double a[3],b[3],p[3],q[3],from[3];
    for(int axis=0;axis<3;++axis) {
        a[axis]=face->vertex[1].position[axis]-face->vertex[0].position[axis];
        b[axis]=face->vertex[2].position[axis]-face->vertex[0].position[axis];
        from[axis]=origin[axis]-face->vertex[0].position[axis];
    }
    p[0]=direction[1]*b[2]-direction[2]*b[1];
    p[1]=direction[2]*b[0]-direction[0]*b[2];
    p[2]=direction[0]*b[1]-direction[1]*b[0];
    double determinant=a[0]*p[0]+a[1]*p[1]+a[2]*p[2];
    if(fabs(determinant)<1e-9) return 0;
    double u=(from[0]*p[0]+from[1]*p[1]+from[2]*p[2])/determinant;
    if(u<0 || u>1) return 0;
    q[0]=from[1]*a[2]-from[2]*a[1];
    q[1]=from[2]*a[0]-from[0]*a[2];
    q[2]=from[0]*a[1]-from[1]*a[0];
    double v=(direction[0]*q[0]+direction[1]*q[1]+direction[2]*q[2])/determinant;
    if(v<0 || u+v>1) return 0;
    double t=(b[0]*q[0]+b[1]*q[1]+b[2]*q[2])/determinant;
    return t>1e-8 && t<maximum;
}

static int weaver_surface_visible(const struct rf_weaver_mesh *mesh,
    const double origin[3],const double target[3],double front)
{
    if(!mesh->rays) return 0;
    double direction[3],squared=0;
    for(int axis=0;axis<3;++axis) {
        direction[axis]=target[axis]-origin[axis];squared+=direction[axis]*direction[axis];
    }
    if(squared<4) return 0;
    /* Two local quantization steps avoid falsely hiding a point on its own
     * triangle. All other completed/active faces are real occluders. */
    double maximum=1-2/sqrt(squared);
    unsigned stack[64]={0},pending=1,visits=0,tests=0;
    while(pending) {
        if(++visits>128) return 0;
        const struct rf_weaver_ray_node *node=&mesh->rays->nodes[stack[--pending]];
        if(node->threshold_minimum>front || !weaver_ray_box(node,origin,direction,maximum)) continue;
        if(node->count) for(unsigned i=node->first;i<node->first+node->count;++i) {
            const struct rf_weaver_face *face=&mesh->faces[mesh->rays->faces[i]];
            if(face->threshold>front) continue;
            if(++tests>128 || weaver_ray_triangle(face,origin,direction,maximum)) return 0;
        } else {
            if(pending+2>64) return 0;
            stack[pending++]=node->child[0];stack[pending++]=node->child[1];
        }
    }
    return 1;
}

static int weaver_face_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_face *a=pa,*b=pb;
    if(a->material!=b->material) return a->material<b->material ? -1 : 1;
    if(a->threshold!=b->threshold) return a->threshold<b->threshold ? -1 : 1;
    return a->source<b->source ? -1 : a->source>b->source;
}
static int weaver_growth_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_growth *a=pa,*b=pb;
    if(a->threshold!=b->threshold) return a->threshold<b->threshold ? -1 : 1;
    return a->face<b->face ? -1 : a->face>b->face;
}

void rf_weaver_mesh_free(struct rf_weaver_mesh *mesh)
{
    if(!mesh) return;
    if(mesh->rays) {weaver_free(mesh->rays->nodes);weaver_free(mesh->rays->faces);weaver_free(mesh->rays);}
    weaver_free(mesh->faces);weaver_free(mesh->growth);weaver_free(mesh->strokes);memset(mesh,0,sizeof(*mesh));
}

int rf_weaver_mesh_load(struct rf_weaver_mesh *mesh,const char *path,int weapon)
{
    struct rasterfall_model_asset model={0};
    struct rasterfall_weapon_model_adapter adapter={0};
    struct rf_weaver_mesh built={0};
    int result=-1, reflected=0;
    if(!mesh || !path || mesh->faces || rasterfall_model_load(&model,path)<0) return -1;
    if(!model.position_scale || !model.index_count || model.index_count%3 ||
       model.index_count>300000 || !model.material_count ||
       model.material_count>RF_WEAVER_MAX_MATERIALS || model.vertex_bytes<22) goto done;
    if(weapon>=0) {
        int lo[3]={model.min_x,model.min_y,model.min_z};
        int hi[3]={model.max_x,model.max_y,model.max_z};
        if(rasterfall_weapon_model_adapt(weapon,lo,hi,&adapter)<0) goto done;
        const double *b=adapter.basis;
        reflected=b[0]*(b[4]*b[8]-b[5]*b[7])-b[1]*(b[3]*b[8]-b[5]*b[6])+
            b[2]*(b[3]*b[7]-b[4]*b[6])<0;
    }
    built.faces=weaver_calloc(model.index_count/3,sizeof(*built.faces));
    if(!built.faces) goto done;
    built.material_count=model.material_count;
    for(unsigned m=0;m<model.material_count;++m) {
        const unsigned char *record=model.materials+(size_t)m*model.material_bytes;
        built.materials[m].color=weaver_u32(record)&0xffffffu;
        built.materials[m].roughness=.65f;
        if(model.format_version<=3) {
            built.materials[m].metallic=weaver_u16(record+4)/65535.0f;
            built.materials[m].roughness=weaver_u16(record+6)/65535.0f;
        } else if(model.surfaces) {
            memcpy(&built.materials[m].roughness,model.surfaces+m*16+4,4);
            memcpy(&built.materials[m].metallic,model.surfaces+m*16+8,4);
        }
        /* This source is for opaque authored machine/gun models only. */
        unsigned texture=weaver_u32(record+8);
        if(texture<model.textures.count && model.textures.views &&
           model.textures.views[texture].data) goto done;
    }
    for(int a=0;a<3;++a) {built.minimum[a]=INT_MAX;built.maximum[a]=INT_MIN;}
    for(unsigned primitive=0;primitive<model.primitive_count;++primitive) {
        const unsigned char *p=model.primitives+primitive*16;
        unsigned first=weaver_u32(p),count=weaver_u32(p+4),material=weaver_u32(p+8);
        if(first%3 || count%3 || first>model.index_count || count>model.index_count-first ||
           material>=built.material_count || count/3>model.index_count/3-built.count) goto done;
        for(unsigned i=0;i<count;i+=3) {
            struct rf_weaver_face *face=&built.faces[built.count++];
            face->material=material;face->source=(first+i)/3;
            for(int corner=0;corner<3;++corner) {
                unsigned index=weaver_u32(model.indices+(size_t)(first+i+corner)*4);
                if(index>=model.vertex_count) goto done;
                const unsigned char *v=model.vertices+(size_t)index*model.vertex_bytes;
                double pos[3],normal[3];
                for(int a=0;a<3;++a) {
                    pos[a]=(int)weaver_u32(v+a*4);
                    normal[a]=(short)weaver_u16(v+12+a*2);
                }
                for(int a=0;a<3;++a) {
                    double position, n;
                    if(weapon>=0) {
                        position=n=0;
                        for(int b=0;b<3;++b) {
                            position+=adapter.basis[a*3+b]*(pos[b]-adapter.center[b]);
                            n+=adapter.basis[a*3+b]*normal[b];
                        }
                        position*=adapter.scale_milli/1000.0*RF_WEAVER_LOCAL_UNITS/512.0;
                    } else {
                        position=pos[a]*(double)RF_WEAVER_LOCAL_UNITS/model.position_scale;
                        n=normal[a];
                    }
                    if(position<-1000000 || position>1000000) goto done;
                    int q=weaver_round(position);
                    face->vertex[corner].position[a]=q;
                    face->vertex[corner].normal[a]=weaver_round(n);
                    if(q<built.minimum[a]) built.minimum[a]=q;
                    if(q>built.maximum[a]) built.maximum[a]=q;
                }
            }
            if(reflected) {
                struct rf_weaver_vertex swap=face->vertex[1];
                face->vertex[1]=face->vertex[2];face->vertex[2]=swap;
            }
        }
    }
    if(built.count!=model.index_count/3) goto done;
    for(unsigned i=0;i<built.count;++i) {
        struct rf_weaver_face *face=&built.faces[i];
        double z=(face->vertex[0].position[2]+face->vertex[1].position[2]+face->vertex[2].position[2])/3.0;
        int span=built.maximum[2]-built.minimum[2];
        face->threshold=span>0 ? (z-built.minimum[2])/span : .5;
    }
    weaver_sort(built.faces,built.count,sizeof(*built.faces),weaver_face_compare);
    built.growth=weaver_malloc((size_t)built.count*sizeof(*built.growth));
    if(!built.growth) goto done;
    for(unsigned i=0;i<built.count;++i) {
        built.growth[i].face=i;built.growth[i].threshold=built.faces[i].threshold;
    }
    weaver_sort(built.growth,built.count,sizeof(*built.growth),weaver_growth_compare);
    if(weapon>=0 && (weaver_rays_create(&built)<0 || weaver_strokes_create(&built)<0)) goto done;
    *mesh=built;memset(&built,0,sizeof(built));result=0;
done:
    rf_weaver_mesh_free(&built);rasterfall_model_unload(&model);return result;
}

void rf_weaver_transform_identity(struct rf_weaver_transform *t)
{
    memset(t,0,sizeof(*t));t->rotation[0]=t->rotation[4]=t->rotation[8]=1;
}
void rf_weaver_transform_point(const struct rf_weaver_transform *t,
    const double source[3],double output[3])
{
    double result[3];
    for(int a=0;a<3;++a) {
        result[a]=t->position[a];
        for(int b=0;b<3;++b) result[a]+=t->rotation[a*3+b]*source[b];
    }
    memcpy(output,result,sizeof(result));
}
void rf_weaver_transform_multiply(struct rf_weaver_transform *out,
    const struct rf_weaver_transform *a,const struct rf_weaver_transform *b)
{
    struct rf_weaver_transform t;
    rf_weaver_transform_point(a,b->position,t.position);
    for(int r=0;r<3;++r) for(int c=0;c<3;++c) {
        t.rotation[r*3+c]=0;
        for(int k=0;k<3;++k) t.rotation[r*3+c]+=a->rotation[r*3+k]*b->rotation[k*3+c];
    }
    *out=t;
}
void rf_weaver_transform_axis(struct rf_weaver_transform *t,int axis,double angle)
{
    rf_weaver_transform_identity(t);
    int a=(axis+1)%3,b=(axis+2)%3;
    t->rotation[a*3+a]=t->rotation[b*3+b]=cos(angle);
    t->rotation[a*3+b]=-sin(angle);t->rotation[b*3+a]=sin(angle);
}

static double weaver_clamp(double v,double low,double high)
{ return v<low?low:v>high?high:v; }
static double weaver_smooth(double v)
{ v=weaver_clamp(v,0,1);return v*v*(3-2*v); }
static void weaver_metres(struct rf_weaver_transform *t,const double v[3])
{ for(int a=0;a<3;++a) t->position[a]=v[a]*RF_WEAVER_LOCAL_UNITS; }

static double weaver_delivery_extension(const struct rf_mesh_weaver_frame *frame)
{
    if(frame->phase==TOY_WEAVER_READY) return 1;
    return frame->phase==TOY_WEAVER_DELIVERING && frame->phase_duration_ms>0 ?
        weaver_smooth(frame->phase_ms/frame->phase_duration_ms):0;
}

void rf_weaver_presentation_reset(struct rf_weaver_presentation_state *state)
{ if(state) memset(state,0,sizeof(*state)); }

static void weaver_return_finish(struct rf_weaver_presentation_state *state)
{ state->returning=0;state->extension=state->velocity=0; }

static void weaver_return_sample(struct rf_weaver_presentation_state *state,double dt_ms)
{
    state->return_elapsed_ms+=dt_ms;
    double duration=state->return_duration_ms;
    if(duration<=0 || state->return_elapsed_ms>=duration) {weaver_return_finish(state);return;}
    double u=state->return_elapsed_ms/duration,u2=u*u,u3=u2*u;
    /* Cubic Hermite: preserve the current velocity when shortening a return
     * for a new calibration; reach home with zero final velocity. */
    state->extension=weaver_clamp(state->return_from*(2*u3-3*u2+1)+
        state->return_velocity*duration*(u3-2*u2+u),0,1);
    state->velocity=state->return_from*(6*u2-6*u)/duration+
        state->return_velocity*(3*u2-4*u+1);
}

static void weaver_return_begin(struct rf_weaver_presentation_state *state,double duration_ms)
{
    if(duration_ms<=0 || state->extension<=0) {weaver_return_finish(state);return;}
    state->returning=1;state->return_from=state->extension;
    /* This tangent interval makes the entire Hermite segment monotone. In
     * normal use only the deadline is shortened, preserving the old tangent. */
    state->return_velocity=weaver_clamp(state->velocity,-3*state->extension/duration_ms,0);
    state->return_elapsed_ms=0;state->return_duration_ms=duration_ms;
}

void rf_weaver_presentation_update(struct rf_weaver_presentation_state *state,
    struct rf_mesh_weaver_frame *frame,unsigned long long world_generation,
    unsigned long long time_us,int paused)
{
    if(!state || !frame) return;
    frame->tray_pose_override=0;frame->tray_extension=0;
    if(!frame->present) {rf_weaver_presentation_reset(state);return;}
    paused=!!(paused || !frame->powered || frame->pause_reason);
    if(!state->valid || state->world_generation!=world_generation ||
       state->x!=frame->x || state->y!=frame->y || state->z!=frame->z ||
       time_us<state->last_time_us || frame->serial<state->serial ||
       frame->collected_count<state->collected_count) {
        rf_weaver_presentation_reset(state);
        state->valid=1;state->world_generation=world_generation;
        state->x=frame->x;state->y=frame->y;state->z=frame->z;
        state->last_time_us=time_us;state->paused=paused;
        state->serial=frame->serial;state->collected_count=frame->collected_count;
        state->extension=weaver_delivery_extension(frame);
    } else {
        double dt_ms=(double)(time_us-state->last_time_us)/1000.0;
        if(paused || state->paused) dt_ms=0;
        state->last_time_us=time_us;state->paused=paused;
        if(frame->phase==TOY_WEAVER_DELIVERING || frame->phase==TOY_WEAVER_READY) {
            state->returning=0;state->velocity=0;
            state->extension=weaver_delivery_extension(frame);
        } else {
            if(frame->collected_count>state->collected_count) {
                state->velocity=0;weaver_return_begin(state,400);
                /* The collection happened since the last displayed frame;
                 * start from its exact visible position, never a guessed time. */
                dt_ms=0;
            }
            if(state->returning) weaver_return_sample(state,dt_ms);
            if(state->returning && frame->phase==TOY_WEAVER_CALIBRATING) {
                double available=frame->phase_duration_ms-frame->phase_ms;
                double remaining=state->return_duration_ms-state->return_elapsed_ms;
                if(available<remaining) weaver_return_begin(state,available);
            }
            /* If a very late frame skipped calibration entirely, the return
             * deadline has already passed. Do not carry it into fabrication. */
            if(frame->phase==TOY_WEAVER_WEAVING) weaver_return_finish(state);
        }
        state->serial=frame->serial;state->collected_count=frame->collected_count;
    }
    frame->tray_pose_override=1;frame->tray_extension=state->extension;
}

static unsigned weaver_growth_lower(const struct rf_weaver_mesh *mesh,double threshold)
{
    unsigned lo=0,hi=mesh->count;
    while(lo<hi) {
        unsigned mid=lo+(hi-lo)/2;
        if(mesh->growth[mid].threshold<threshold) lo=mid+1;else hi=mid;
    }
    return lo;
}

static const struct rf_weaver_stroke *weaver_stroke(const struct rf_weaver_mesh *mesh,
    int head,unsigned bin)
{ return &mesh->strokes[(unsigned)head*(RF_WEAVER_STROKE_BINS+1)+bin]; }

static void weaver_stroke_point(const struct rf_weaver_mesh *mesh,
    const struct rf_weaver_stroke *stroke,unsigned end,double point[3])
{
    const struct rf_weaver_face *face=&mesh->faces[stroke->face];
    for(int axis=0;axis<3;++axis) {
        point[axis]=0;
        for(int corner=0;corner<3;++corner)
            point[axis]+=stroke->barycentric[end][corner]*face->vertex[corner].position[axis];
    }
}

static int weaver_strokes_create(struct rf_weaver_mesh *mesh)
{
    const double radians=3.141592653589793/180.0;
    struct rf_weaver_transform gun;
    rf_weaver_transform_axis(&gun,1,TOY_WEAVER_PRODUCT_YAW_DEG*radians);
    weaver_metres(&gun,rf_mesh_weaver_build_center_m);
    mesh->strokes=weaver_calloc(8*(RF_WEAVER_STROKE_BINS+1),sizeof(*mesh->strokes));
    if(!mesh->strokes) return -1;
    for(int head=0;head<8;++head) {
        double origin[3];
        for(int axis=0;axis<3;++axis) {
            origin[axis]=0;
            for(int k=0;k<3;++k)
                origin[axis]+=gun.rotation[k*3+axis]*(rf_mesh_weaver_heads[head].position_m[k]*
                    RF_WEAVER_LOCAL_UNITS-gun.position[k]);
        }
        double offset=head*RF_WEAVER_STROKE_HEAD_OFFSET;
        for(unsigned bin=0;bin<=RF_WEAVER_STROKE_BINS;++bin) {
            struct rf_weaver_stroke *stroke=&mesh->strokes[head*(RF_WEAVER_STROKE_BINS+1)+bin];
            static const double weights[2][3]={{.60,.25,.15},{.15,.25,.60}};
            for(int end_point=0;end_point<2;++end_point) for(int corner=0;corner<3;++corner)
                stroke->barycentric[end_point][(corner+head)%3]=weights[end_point][corner];
            double center=(bin+.5-offset)/RF_WEAVER_STROKE_BINS;
            double first=(bin+RF_WEAVER_STROKE_MARGIN-offset)/RF_WEAVER_STROKE_BINS;
            double last=(bin+1-RF_WEAVER_STROKE_MARGIN-offset)/RF_WEAVER_STROKE_BINS;
            unsigned begin=weaver_growth_lower(mesh,last-RF_WEAVER_ACTIVE_BEHIND);
            unsigned end=weaver_growth_lower(mesh,first+RF_WEAVER_ACTIVE_AHEAD);
            double best=1e30;
            stroke->face=UINT_MAX;
            for(unsigned i=begin;i<end;++i) {
                unsigned face_index=mesh->growth[i].face;
                const struct rf_weaver_face *face=&mesh->faces[face_index];
                double center_point[3],edge_a[3],edge_b[3],normal[3];
                for(int axis=0;axis<3;++axis) {
                    center_point[axis]=(face->vertex[0].position[axis]+
                        face->vertex[1].position[axis]+face->vertex[2].position[axis])/3.0;
                    edge_a[axis]=face->vertex[1].position[axis]-face->vertex[0].position[axis];
                    edge_b[axis]=face->vertex[2].position[axis]-face->vertex[0].position[axis];
                }
                normal[0]=edge_a[1]*edge_b[2]-edge_a[2]*edge_b[1];
                normal[1]=edge_a[2]*edge_b[0]-edge_a[0]*edge_b[2];
                normal[2]=edge_a[0]*edge_b[1]-edge_a[1]*edge_b[0];
                if(normal[0]*normal[0]+normal[1]*normal[1]+normal[2]*normal[2]<1) continue;
                double distance=0;
                for(int axis=0;axis<3;++axis) {
                    double d=(center_point[axis]-origin[axis])/RF_WEAVER_LOCAL_UNITS;
                    distance+=d*d;
                }
                double delta=face->threshold-center;
                /* Prefer the surface facing this mount, while keeping the
                 * stroke near the middle of the active fabrication band. */
                double score=distance+delta*delta*64;
                if(score<best) {
                    struct rf_weaver_stroke candidate=*stroke;candidate.face=face_index;
                    double a[3],b[3];
                    weaver_stroke_point(mesh,&candidate,0,a);weaver_stroke_point(mesh,&candidate,1,b);
                    if(weaver_surface_visible(mesh,origin,a,last+RF_WEAVER_ACTIVE_AHEAD) &&
                       weaver_surface_visible(mesh,origin,b,last+RF_WEAVER_ACTIVE_AHEAD)) {
                        best=score;stroke->face=face_index;stroke->active=1;
                    }
                }
            }
            if(stroke->face==UINT_MAX) {
                unsigned nearest=weaver_growth_lower(mesh,center);
                if(nearest==mesh->count) --nearest;
                if(nearest && fabs(mesh->growth[nearest-1].threshold-center)<
                    fabs(mesh->growth[nearest].threshold-center)) --nearest;
                /* Empty/occluded intervals retain a stable aim, but cannot
                 * emit even if the fallback happens to enter the active band. */
                stroke->face=mesh->growth[nearest].face;
            }
        }
    }
    return 0;
}

/* Progress alone drives the immutable schedule: pausing, replaying a frozen
 * value, or arriving at it with another frame step produces the same pose. */
static unsigned weaver_stroke_sample(const struct rf_weaver_mesh *mesh,int head,
    double progress,double target[3])
{
    double coordinate=progress*RF_WEAVER_STROKE_BINS+head*RF_WEAVER_STROKE_HEAD_OFFSET;
    unsigned bin=(unsigned)coordinate;
    if(bin>RF_WEAVER_STROKE_BINS) bin=RF_WEAVER_STROKE_BINS;
    double phase=coordinate-bin;
    const struct rf_weaver_stroke *stroke=weaver_stroke(mesh,head,bin);
    double a[3],b[3],blend;
    unsigned face=UINT_MAX;
    if(phase<RF_WEAVER_STROKE_MARGIN) {
        const struct rf_weaver_stroke *previous=weaver_stroke(mesh,head,bin?bin-1:0);
        weaver_stroke_point(mesh,previous,bin?1:0,a);
        weaver_stroke_point(mesh,stroke,0,b);
        blend=weaver_smooth((phase+RF_WEAVER_STROKE_MARGIN)/(2*RF_WEAVER_STROKE_MARGIN));
    } else if(phase>1-RF_WEAVER_STROKE_MARGIN) {
        const struct rf_weaver_stroke *next=weaver_stroke(mesh,head,
            bin<RF_WEAVER_STROKE_BINS?bin+1:bin);
        weaver_stroke_point(mesh,stroke,1,a);
        weaver_stroke_point(mesh,next,0,b);
        blend=weaver_smooth((phase-(1-RF_WEAVER_STROKE_MARGIN))/(2*RF_WEAVER_STROKE_MARGIN));
    } else {
        weaver_stroke_point(mesh,stroke,0,a);weaver_stroke_point(mesh,stroke,1,b);
        blend=weaver_smooth((phase-RF_WEAVER_STROKE_MARGIN)/(1-2*RF_WEAVER_STROKE_MARGIN));
        double threshold=mesh->faces[stroke->face].threshold;
        if(stroke->active && threshold>progress-RF_WEAVER_ACTIVE_BEHIND &&
           threshold<=progress+RF_WEAVER_ACTIVE_AHEAD)
            face=stroke->face;
    }
    for(int axis=0;axis<3;++axis) target[axis]=a[axis]+(b[axis]-a[axis])*blend;
    return face;
}

void rf_weaver_pose_sample(const struct rf_mesh_weaver_frame *f,
    const struct rf_weaver_mesh *gun,struct rf_weaver_pose *pose)
{
    const double radians=3.141592653589793/180.0;
    memset(pose,0,sizeof(*pose));
    for(int i=0;i<RF_WEAVER_MAX_BONES;++i) rf_weaver_transform_identity(&pose->bones[i]);
    for(int h=0;h<8;++h) pose->beam_face[h]=UINT_MAX;
    pose->reveal=weaver_clamp(f->progress,0,1);
    double phase=f->phase_duration_ms>0 ? f->phase_ms/f->phase_duration_ms : 0;
    if(f->phase==TOY_WEAVER_CALIBRATING) pose->opening=weaver_smooth(phase);
    else if(f->phase==TOY_WEAVER_WEAVING) pose->opening=1;
    else if(f->phase==TOY_WEAVER_DELIVERING) pose->opening=1-weaver_smooth(phase);
    weaver_metres(&pose->bones[0],rf_mesh_weaver_tray_position_m);
    rf_weaver_transform_axis(&pose->bones[41],1,TOY_WEAVER_PRODUCT_YAW_DEG*radians);
    weaver_metres(&pose->bones[41],rf_mesh_weaver_build_center_m);
    double delivery=weaver_delivery_extension(f);
    double tray=f->tray_pose_override?weaver_clamp(f->tray_extension,0,1):delivery;
    if(gun && gun->count) {
        double supported=rf_mesh_weaver_tray_position_m[1]*RF_WEAVER_LOCAL_UNITS+
            weaver_round(rf_mesh_weaver_tray_support_height_m*RF_WEAVER_LOCAL_UNITS)-gun->minimum[1];
        pose->bones[41].position[1]+=(supported-pose->bones[41].position[1])*delivery;
    }
    for(int axis=0;axis<3;++axis) {
        double displacement=rf_mesh_weaver_tray_delivery_translation_m[axis]*RF_WEAVER_LOCAL_UNITS;
        pose->bones[0].position[axis]+=displacement*tray;
        pose->bones[41].position[axis]+=displacement*delivery;
    }
    for(int h=0;h<8;++h) {
        const struct rf_mesh_weaver_head_layout *layout=&rf_mesh_weaver_heads[h];
        double target[3]={0,0,0};
        if(gun && gun->count && gun->strokes)
            pose->beam_face[h]=weaver_stroke_sample(gun,h,pose->reveal,target);
        rf_weaver_transform_point(&pose->bones[41],target,target);
        double origin[3];for(int a=0;a<3;++a)origin[a]=layout->position_m[a]*RF_WEAVER_LOCAL_UNITS;
        double dx=target[0]-origin[0],dy=target[1]-origin[1],dz=target[2]-origin[2];
        double yaw=atan2(dx,dz)/radians;
        /* The pitch joint sits forward of the yoke yaw pivot. Aim from that
         * joint so the emitted ray and the core's optical axis coincide. */
        double reach=sqrt(dx*dx+dz*dz)-
            rf_mesh_weaver_core_position_in_yoke_m[2]*RF_WEAVER_LOCAL_UNITS;
        double pitch=-atan2(dy,reach>1?reach:1)/radians;
        double delta=yaw-layout->yaw_degrees;
        while(delta>180)delta-=360;
        while(delta<-180)delta+=360;
        int reachable=delta>=rf_mesh_weaver_yaw_limit_degrees[0] &&
            delta<=rf_mesh_weaver_yaw_limit_degrees[1] &&
            pitch>=rf_mesh_weaver_pitch_limit_degrees[0] && pitch<=rf_mesh_weaver_pitch_limit_degrees[1];
        yaw=layout->yaw_degrees+weaver_clamp(delta,-34,34)*pose->opening;
        pitch=layout->idle_pitch_degrees*(1-pose->opening)+weaver_clamp(pitch,-62,62)*pose->opening;
        struct rf_weaver_transform *yoke=&pose->bones[1+h],*core=&pose->bones[9+h];
        rf_weaver_transform_axis(yoke,1,yaw*radians);memcpy(yoke->position,origin,sizeof(origin));
        struct rf_weaver_transform local;
        rf_weaver_transform_axis(&local,0,pitch*radians);
        weaver_metres(&local,rf_mesh_weaver_core_position_in_yoke_m);
        rf_weaver_transform_multiply(core,yoke,&local);
        double aperture[3];for(int a=0;a<3;++a)aperture[a]=rf_mesh_weaver_aperture_in_core_m[a]*RF_WEAVER_LOCAL_UNITS;
        rf_weaver_transform_point(core,aperture,pose->beam_start[h]);
        memcpy(pose->beam_end[h],target,sizeof(target));
        /* Eight staggered strokes keep both levels involved. Test the real currently
         * visible surface prefix, including already completed occluders. */
        if(f->phase==TOY_WEAVER_WEAVING && f->powered && !f->pause_reason && reachable &&
           pose->beam_face[h]!=UINT_MAX) {
            double ray_start[3],ray_end[3];
            for(int axis=0;axis<3;++axis) {
                ray_start[axis]=ray_end[axis]=0;
                for(int k=0;k<3;++k) {
                    ray_start[axis]+=pose->bones[41].rotation[k*3+axis]*
                        (pose->beam_start[h][k]-pose->bones[41].position[k]);
                    ray_end[axis]+=pose->bones[41].rotation[k*3+axis]*
                        (target[k]-pose->bones[41].position[k]);
                }
            }
            if(weaver_surface_visible(gun,ray_start,ray_end,pose->reveal+RF_WEAVER_ACTIVE_AHEAD))
                pose->beam_mask|=1u<<h;
        }
        for(int petal=0;petal<3;++petal) {
            struct rf_weaver_transform roll,hinge,opened,combined;
            rf_weaver_transform_axis(&roll,2,rf_mesh_weaver_petal_roll_degrees[petal]*radians);
            rf_weaver_transform_identity(&hinge);weaver_metres(&hinge,rf_mesh_weaver_petal_hinge_in_core_m);
            rf_weaver_transform_axis(&opened,0,rf_mesh_weaver_petal_open_degrees*radians*pose->opening);
            rf_weaver_transform_multiply(&combined,&roll,&hinge);
            rf_weaver_transform_multiply(&combined,&combined,&opened);
            rf_weaver_transform_multiply(&pose->bones[17+h*3+petal],core,&combined);
        }
    }
}
