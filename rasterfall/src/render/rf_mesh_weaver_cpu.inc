/* CPU reference presentation for the Mesh Weaver. Include after the ordinary
 * world triangle/quad helpers and before render_scene(). The immutable frame
 * and shared pose sampler own all motion; this consumer never changes Game. */
#include "rf_mesh_weaver_presentation.h"
#include "rf_mesh_weaver_layout_generated.h"
#include "rasterfall_calibration.h"
#include "toy_mesh_weaver.h"

struct rf_weaver_cpu_edge {
    int point[2][3];
    double threshold;
};
struct rf_weaver_cpu_cache {
    struct rf_weaver_mesh tray, yoke, core, petal, gun;
    struct rf_weaver_cpu_edge *edges;
    unsigned edge_count;
    int attempted, ready, weapon, weapon_attempted, weapon_ready;
};
static struct rf_weaver_cpu_cache rf_weaver_cpu_cache;

void rasterfall_mesh_weaver_cpu_shutdown(void)
{
    struct rf_weaver_cpu_cache *c=&rf_weaver_cpu_cache;
    rf_weaver_mesh_free(&c->tray);rf_weaver_mesh_free(&c->yoke);
    rf_weaver_mesh_free(&c->core);rf_weaver_mesh_free(&c->petal);
    rf_weaver_mesh_free(&c->gun);free(c->edges);memset(c,0,sizeof(*c));
    rasterfall_mesh_weaver_set_frame(NULL);
}

static int rf_weaver_cpu_point_compare(const int a[3],const int b[3])
{
    for(int k=0;k<3;++k) if(a[k]!=b[k]) return a[k]<b[k] ? -1 : 1;
    return 0;
}
static int rf_weaver_cpu_edge_compare(const void *pa,const void *pb)
{
    const struct rf_weaver_cpu_edge *a=pa,*b=pb;
    int first=rf_weaver_cpu_point_compare(a->point[0],b->point[0]);
    return first ? first : rf_weaver_cpu_point_compare(a->point[1],b->point[1]);
}

static int rf_weaver_cpu_edges(struct rf_weaver_cpu_cache *c)
{
    /* Weld exported hard-normal/UV duplicates only for this visual edge list.
     * Real manufacturing geometry and its resource accounting are untouched. */
    unsigned count=c->gun.count*3u,used=0;
    struct rf_weaver_cpu_edge *edges=malloc((size_t)count*sizeof(*edges));
    if(!edges) return -1;
    for(unsigned face=0;face<c->gun.count;++face) for(int side=0;side<3;++side) {
        const struct rf_weaver_face *f=&c->gun.faces[face];
        const int *a=f->vertex[side].position,*b=f->vertex[(side+1)%3].position;
        if(rf_weaver_cpu_point_compare(a,b)==0) continue;
        if(rf_weaver_cpu_point_compare(a,b)>0) {const int *swap=a;a=b;b=swap;}
        memcpy(edges[used].point[0],a,sizeof(edges[used].point[0]));
        memcpy(edges[used].point[1],b,sizeof(edges[used].point[1]));
        edges[used++].threshold=f->threshold;
    }
    qsort(edges,used,sizeof(*edges),rf_weaver_cpu_edge_compare);
    count=0;
    for(unsigned i=0;i<used;++i) {
        if(count && !rf_weaver_cpu_edge_compare(&edges[count-1],&edges[i])) {
            if(edges[i].threshold<edges[count-1].threshold)
                edges[count-1].threshold=edges[i].threshold;
        } else edges[count++]=edges[i];
    }
    c->edges=edges;c->edge_count=count;return 0;
}

static int rf_weaver_cpu_prepare(int weapon,int need_gun)
{
    struct rf_weaver_cpu_cache *c=&rf_weaver_cpu_cache;
    if(!c->attempted) {
        c->attempted=1;
        if(rf_weaver_mesh_load(&c->tray,"rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_tray.rmesh",-1)<0 ||
           rf_weaver_mesh_load(&c->yoke,"rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_yoke.rmesh",-1)<0 ||
           rf_weaver_mesh_load(&c->core,"rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_core.rmesh",-1)<0 ||
           rf_weaver_mesh_load(&c->petal,"rasterfall/assets/models/props/mesh_weaver/rf_mesh_weaver_emitter_petal.rmesh",-1)<0) {
            rf_weaver_mesh_free(&c->tray);rf_weaver_mesh_free(&c->yoke);
            rf_weaver_mesh_free(&c->core);rf_weaver_mesh_free(&c->petal);
            __printf("rasterfall: mesh-weaver CPU component load failed\n");
            return -1;
        }
        c->ready=1;
    }
    if(!c->ready) return -1;
    if(!need_gun) return 0;
    if(!c->weapon_attempted || weapon!=c->weapon) {
        const struct rasterfall_weapon_asset_profile *profile=rasterfall_weapon_asset_profile(weapon);
        rf_weaver_mesh_free(&c->gun);free(c->edges);c->edges=NULL;c->edge_count=0;
        c->weapon=weapon;c->weapon_attempted=1;c->weapon_ready=0;
        /* Weapon zero is the real pistol, never the machine-component marker. */
        if(weapon<0 || !profile || !profile->model_path ||
           rf_weaver_mesh_load(&c->gun,profile->model_path,weapon)<0 ||
           rf_weaver_cpu_edges(c)<0) {
            rf_weaver_mesh_free(&c->gun);
            __printf("rasterfall: mesh-weaver CPU weapon load failed weapon=%d\n",weapon);
            return -1;
        }
        c->weapon_ready=1;
    }
    return c->weapon_ready ? 0 : -1;
}

static int rf_weaver_cpu_round(double value)
{ return (int)(value+(value<0 ? -.5 : .5)); }

static struct vec3 rf_weaver_cpu_world(const struct rf_mesh_weaver_frame *f,
    const double point[3])
{
    const double scale=512.0/RF_WEAVER_LOCAL_UNITS;
    struct vec3 out={f->x+rf_weaver_cpu_round(point[0]*scale),
                     f->y+rf_weaver_cpu_round(point[1]*scale),
                     f->z+rf_weaver_cpu_round(point[2]*scale)};
    return out;
}

static struct vec3 rf_weaver_cpu_vertex(const struct rf_mesh_weaver_frame *f,
    const struct rf_weaver_transform *transform,const int position[3])
{
    double point[3]={position[0],position[1],position[2]},posed[3];
    rf_weaver_transform_point(transform,point,posed);
    return rf_weaver_cpu_world(f,posed);
}

static int rf_weaver_cpu_face_light(const struct rf_weaver_face *face,
    const struct rf_weaver_transform *transform,int world_light)
{
    double local[3]={0,0,0};int normal[3];
    for(int axis=0;axis<3;++axis) for(int corner=0;corner<3;++corner)
        local[axis]+=face->vertex[corner].normal[axis]/3.0;
    for(int axis=0;axis<3;++axis) {
        double value=0;
        for(int k=0;k<3;++k) value+=transform->rotation[axis*3+k]*local[k];
        normal[axis]=rf_weaver_cpu_round(value);
    }
    return world_light*model_form_light_q8(normal[0],normal[1],normal[2],0)/256;
}

static int rf_weaver_cpu_solid(struct toy_renderer *renderer,const struct camera *camera,
    const struct rf_mesh_weaver_frame *frame,const struct rf_weaver_mesh *mesh,
    const struct rf_weaver_transform *transform,double reveal,int world_light)
{
    int pixels=0;
    for(unsigned i=0;i<mesh->count;++i) {
        const struct rf_weaver_face *face=&mesh->faces[i];
        if(face->threshold>reveal) continue;
        if(!(i&255u) && toy_renderer_job_cancelled(renderer)) break;
        struct vec3 p[3];
        for(int corner=0;corner<3;++corner)
            p[corner]=rf_weaver_cpu_vertex(frame,transform,face->vertex[corner].position);
        active_scene_light_override_q8=rf_weaver_cpu_face_light(face,transform,world_light);
        int drawn=draw_world_triangle(renderer,camera,&p[0],&p[1],&p[2],mesh->materials[face->material].color);
        if(drawn<0) return -1;
        pixels+=drawn;
    }
    return pixels;
}

static int rf_weaver_cpu_line(struct toy_renderer *renderer,const struct camera *camera,
    const struct vec3 *a,const struct vec3 *b,double radius,unsigned color,int alpha)
{
    /* A camera-facing world ribbon retains depth tests and near clipping.
     * It is not a screen-space line that can show through finished material. */
    double direction[3]={b->x-a->x,b->y-a->y,b->z-a->z};
    double view[3]={camera->x-(a->x+b->x)*.5,camera->y-(a->y+b->y)*.5,
                    camera->z-(a->z+b->z)*.5},side[3];
    for(int k=0;k<3;++k)
        side[k]=direction[(k+1)%3]*view[(k+2)%3]-direction[(k+2)%3]*view[(k+1)%3];
    double length=sqrt(side[0]*side[0]+side[1]*side[1]+side[2]*side[2]);
    if(length<.00001) return 0;
    int offset[3];for(int k=0;k<3;++k) offset[k]=rf_weaver_cpu_round(side[k]*radius/length);
    if(!offset[0] && !offset[1] && !offset[2]) return 0;
    struct vec3 q[4]={{a->x+offset[0],a->y+offset[1],a->z+offset[2]},
                     {b->x+offset[0],b->y+offset[1],b->z+offset[2]},
                     {b->x-offset[0],b->y-offset[1],b->z-offset[2]},
                     {a->x-offset[0],a->y-offset[1],a->z-offset[2]}};
    return draw_quad_alpha(renderer,camera,&q[0],&q[1],&q[2],&q[3],color,alpha);
}

static int rf_weaver_cpu_dot(struct toy_renderer *renderer,const struct camera *camera,
    const struct vec3 *center,double radius,unsigned color,int alpha)
{
    double right[3]={camera->cy/1024.0,0,-camera->sy/1024.0};
    double up[3]={-(double)camera->sy*camera->pitch_sy/1048576.0,
        camera->pitch_cy/1024.0,-(double)camera->cy*camera->pitch_sy/1048576.0};
    struct vec3 q[4];
    for(int i=0;i<4;++i) {
        double *axis=(i&1)?up:right,sign=i<2?1:-1;
        q[i].x=center->x+rf_weaver_cpu_round(axis[0]*radius*sign);
        q[i].y=center->y+rf_weaver_cpu_round(axis[1]*radius*sign);
        q[i].z=center->z+rf_weaver_cpu_round(axis[2]*radius*sign);
    }
    return draw_quad_alpha(renderer,camera,&q[0],&q[1],&q[2],&q[3],color,alpha);
}

static int rf_weaver_cpu_growth(struct toy_renderer *renderer,const struct camera *camera,
    const struct rf_mesh_weaver_frame *frame,const struct rf_weaver_pose *pose)
{
    struct rf_weaver_cpu_cache *c=&rf_weaver_cpu_cache;
    const struct rf_weaver_transform *transform=&pose->bones[41];
    int pixels=0,calibrating=frame->phase==TOY_WEAVER_CALIBRATING;
    double reveal=calibrating ? -1 : pose->reveal-RF_WEAVER_ACTIVE_BEHIND;
    double front=calibrating ? -1 : pose->reveal+RF_WEAVER_ACTIVE_AHEAD;
    unsigned active_count=0,preview_count=0,active_index=0,preview_index=0;
    unsigned active_color=frame->pause_reason ? 0xDFA96B : 0x87E9EB;
    active_scene_light_override_q8=256;
    if(!calibrating) for(unsigned i=0;i<c->gun.count;++i) {
        const struct rf_weaver_face *face=&c->gun.faces[i];
        if(face->threshold<=reveal || face->threshold>front) continue;
        struct vec3 p[3];
        for(int corner=0;corner<3;++corner)
            p[corner]=rf_weaver_cpu_vertex(frame,transform,face->vertex[corner].position);
        pixels+=draw_world_triangle_alpha(renderer,camera,&p[0],&p[1],&p[2],active_color,54);
    }
    for(unsigned i=0;i<c->edge_count;++i) {
        double threshold=c->edges[i].threshold;
        if(threshold<=reveal) continue;
        if(!calibrating && threshold<=front) active_count++;
        else preview_count++;
    }
    double dx=camera->x-frame->x,dz=camera->z-frame->z;
    int far=dx*dx+dz*dz>4096.0*4096.0;
    unsigned active_stride=(active_count+(far?96:320)-1)/(far?96:320);
    unsigned preview_stride=(preview_count+(far?60:140)-1)/(far?60:140);
    if(!active_stride) active_stride=1;
    if(!preview_stride) preview_stride=1;
    for(unsigned i=0;i<c->edge_count;++i) {
        const struct rf_weaver_cpu_edge *edge=&c->edges[i];
        if(edge->threshold<=reveal) continue;
        int active=!calibrating && edge->threshold<=front;
        if(active ? active_index++%active_stride : preview_index++%preview_stride) continue;
        struct vec3 a=rf_weaver_cpu_vertex(frame,transform,edge->point[0]);
        struct vec3 b=rf_weaver_cpu_vertex(frame,transform,edge->point[1]);
        pixels+=rf_weaver_cpu_line(renderer,camera,&a,&b,active?1.0:.72,
            active?active_color:0x438C99,active?205:46);
        if(active && i%7u==0)
            pixels+=rf_weaver_cpu_dot(renderer,camera,&a,2.0,0xC0F6ED,190);
    }
    return pixels;
}

static int rf_weaver_cpu_draw(struct toy_renderer *renderer,const struct camera *camera,
    const struct rf_mesh_weaver_frame *frame)
{
    if(!renderer || !camera || !frame || !frame->present) return 0;
    if(!world_box_visible(&renderer->surface,camera,frame->x-600,frame->x+600,
        frame->y-32,frame->y+1050,frame->z-600,frame->z+650)) return 0;
    int show_gun=frame->phase!=TOY_WEAVER_IDLE;
    if(rf_weaver_cpu_prepare(frame->weapon,show_gun)<0) return -1;
    struct rf_weaver_cpu_cache *c=&rf_weaver_cpu_cache;
    struct rf_weaver_pose pose;
    rf_weaver_pose_sample(frame,show_gun?&c->gun:NULL,&pose);
    int saved_light=active_scene_light_override_q8,saved_v2=active_world_light_v2;
    int saved_floor=floor_submission,pixels=0;
    struct rasterfall_frontend_state *frontend=frontend_state(),saved_frontend=*frontend;
    active_world_light_v2=0;floor_submission=0;
    active_material_alpha=255;active_material_double_sided=0;
    active_material_lighting_min_q8=0;active_material_form_light_q8=256;
    active_model_triangle_stats=NULL;
    int world_light=world_brightness_at(frame->x,frame->y+450,frame->z);
    int drawn=rf_weaver_cpu_solid(renderer,camera,frame,&c->tray,&pose.bones[0],1,world_light);
    if(drawn<0) {pixels=-1;goto done;} pixels+=drawn;
    for(int head=0;head<8;++head) {
        drawn=rf_weaver_cpu_solid(renderer,camera,frame,&c->yoke,&pose.bones[1+head],1,world_light);
        if(drawn<0) {pixels=-1;goto done;} pixels+=drawn;
        drawn=rf_weaver_cpu_solid(renderer,camera,frame,&c->core,&pose.bones[9+head],1,world_light);
        if(drawn<0) {pixels=-1;goto done;} pixels+=drawn;
        for(int petal=0;petal<3;++petal) {
            drawn=rf_weaver_cpu_solid(renderer,camera,frame,&c->petal,&pose.bones[17+head*3+petal],1,world_light);
            if(drawn<0) {pixels=-1;goto done;} pixels+=drawn;
        }
    }
    if(show_gun && frame->phase!=TOY_WEAVER_CALIBRATING) {
        drawn=rf_weaver_cpu_solid(renderer,camera,frame,&c->gun,&pose.bones[41],
            frame->phase==TOY_WEAVER_WEAVING?pose.reveal-RF_WEAVER_ACTIVE_BEHIND:1,world_light);
        if(drawn<0) {pixels=-1;goto done;} pixels+=drawn;
    }
    active_scene_light_override_q8=256;
    if(show_gun && (frame->phase==TOY_WEAVER_CALIBRATING || frame->phase==TOY_WEAVER_WEAVING))
        pixels+=rf_weaver_cpu_growth(renderer,camera,frame,&pose);
    for(int head=0;frame->powered && !frame->pause_reason && head<8;++head) if(pose.beam_mask&(1u<<head)) {
        struct vec3 start=rf_weaver_cpu_world(frame,pose.beam_start[head]);
        struct vec3 end=rf_weaver_cpu_world(frame,pose.beam_end[head]);
        pixels+=rf_weaver_cpu_line(renderer,camera,&start,&end,2.0,0x63BDCE,28);
        pixels+=rf_weaver_cpu_line(renderer,camera,&start,&end,.80,0x8DEBF0,160);
        pixels+=rf_weaver_cpu_dot(renderer,camera,&end,3.0,0xC6F8F0,220);
    }
    if(frame->phase==TOY_WEAVER_CALIBRATING) {
        /* The briefly visible calibration cube shares the physical envelope. */
        for(int axis=0;axis<3;++axis) for(int side=0;side<4;++side) {
            double a[3],b[3];int other=(axis+1)%3,last=(axis+2)%3;
            a[axis]=rf_mesh_weaver_manufacturing_bounds_m[0][axis]*RF_WEAVER_LOCAL_UNITS;
            b[axis]=rf_mesh_weaver_manufacturing_bounds_m[1][axis]*RF_WEAVER_LOCAL_UNITS;
            a[other]=b[other]=rf_mesh_weaver_manufacturing_bounds_m[side&1][other]*RF_WEAVER_LOCAL_UNITS;
            a[last]=b[last]=rf_mesh_weaver_manufacturing_bounds_m[(side>>1)&1][last]*RF_WEAVER_LOCAL_UNITS;
            struct vec3 wa=rf_weaver_cpu_world(frame,a),wb=rf_weaver_cpu_world(frame,b);
            pixels+=rf_weaver_cpu_line(renderer,camera,&wa,&wb,.8,0x63BDCE,60);
        }
    }
done:
    *frontend=saved_frontend;
    active_scene_light_override_q8=saved_light;active_world_light_v2=saved_v2;
    floor_submission=saved_floor;
    return pixels;
}
