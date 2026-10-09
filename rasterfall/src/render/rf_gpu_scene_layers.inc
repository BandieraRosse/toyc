/* Independent Scene presentation extraction. No toy_renderer or framebuffer.
 * All source reads finish before resource preparation and target submission. */
#include "rasterfall_viewmodel.h"
#include "rasterfall_lab_terminal.h"
#include "rf_display_geometry_cache.h"
#include "rasterfall_actor_labels.h"
#include "rf_rts.h"
struct scene_layer_triangle {
    struct rf_gpu_graphics_vertex vertex[3];
    uint32_t color;
    int light,alpha,screen,textured,layer,emissive;
    int origin[3];
    unsigned retained;
};
/* Camera-independent packets retain exact source order and local origins.
 * Only explicitly immutable emitters use them; animated emitters stay live. */
#define SCENE_RETAINED_TRIANGLES 131072u
struct scene_layer_packet {
    struct toy_map_draw key;
    struct rf_gpu_graphics_batch_item *runs;
    struct rf_gpu_graphics_resource **resources;
    unsigned triangles,run_count,resource_count,allocated_triangles;
    int valid,min[3],max[3];
};
struct scene_layer_workspace;
struct scene_layer_mesh {
    struct scene_layer_triangle *triangles;
    uint32_t count,capacity;
    int layer,failed,width,height,emissive;
    double clip[5][4];
    int cull;
    struct rf_display_geometry *displays;
    unsigned display_budget;
    int cache_displays;
    int profile;
    int64_t clip_us;
    uint32_t culled;
    struct scene_layer_workspace *owner;
    int retain;
};
struct scene_layer_workspace {
    struct scene_layer_mesh mesh;
    struct rf_gpu_graphics_vertex *vertices;
    struct rf_gpu_scene_color_vertex *colors;
    uint32_t *indices,*texels,*packed_first;
    unsigned char *texture_source;
    uint32_t vertex_capacity,texture_bytes,texture_width,texture_height,texture_channels;
    struct rf_gpu_graphics_batch_item *ordered;
    uint32_t ordered_capacity;
    struct rf_gpu_graphics *graphics;
    struct scene_layer_packet *packets;
    unsigned retained_triangles;
    struct scene_layer_workspace *shared;
};
static void scene_packet_clear(struct scene_layer_workspace *w,struct scene_layer_packet *p)
{
    for(unsigned i=0;i<p->resource_count;++i)
        if(p->resources[i]) rf_gpu_graphics_resource_destroy(w->graphics,p->resources[i]);
    w->retained_triangles-=p->allocated_triangles;
    free(p->resources);free(p->runs);memset(p,0,sizeof(*p));
}
static int scene_batch_reserve(struct rf_gpu_graphics_batch_item **items,
    uint32_t *capacity,uint32_t needed)
{
    if (needed<=*capacity) return 0;
    if (needed>UINT32_MAX/2) return -1;
    uint32_t next_capacity=needed+needed/2;
    void *next=realloc(*items,(size_t)next_capacity*sizeof(**items));
    if (!next) return -1;
    *items=next;*capacity=next_capacity;return 0;
}
static void scene_layer_workspace_free(struct scene_layer_workspace *w)
{
    if (!w) return;
    if(w->packets) for(unsigned i=0;i<TOY_MAP_MAX_DRAW;++i) scene_packet_clear(w,&w->packets[i]);
    free(w->packets);
    if(w->mesh.displays) for(unsigned i=0;i<TOY_MAP_MAX_DRAW;++i) free(w->mesh.displays[i].quads);
    free(w->mesh.displays);
    free(w->mesh.triangles);free(w->vertices);free(w->colors);free(w->indices);free(w->packed_first);
    free(w->texels);free(w->texture_source);free(w->ordered);free(w);
}
static int scene_layer_emit(struct scene_layer_mesh *m,
    const struct rf_gpu_graphics_vertex *a,const struct rf_gpu_graphics_vertex *b,
    const struct rf_gpu_graphics_vertex *c,uint32_t color,int light,int alpha,
    int screen,int textured)
{
    if (m->failed) return -1;
    /* These alpha-blended presentation surfaces do not cast shadows. Reject
     * only triangles wholly outside one camera plane; crossing triangles keep
     * their original vertices for hardware clipping. No distance/size cutoff. */
    if (m->cull && !screen && alpha>0) {
        int64_t start=m->profile?rf_core_clock_now_us():0;
        int outside=0;
        for (int p=0;p<5;++p) {
            const double *f=m->clip[p];
            double da=f[0]*a->position[0]+f[1]*a->position[1]+f[2]*a->position[2]+f[3];
            double db=f[0]*b->position[0]+f[1]*b->position[1]+f[2]*b->position[2]+f[3];
            double dc=f[0]*c->position[0]+f[1]*c->position[1]+f[2]*c->position[2]+f[3];
            if (da<0 && db<0 && dc<0) {outside=1;break;}
        }
        if(m->profile) m->clip_us+=rf_core_clock_now_us()-start;
        if(outside) {m->culled++;return 0;}
    }
    int origin[3]={0};
    if (!screen) {
        int split=0;
        for (int k=0;k<3;++k) {
            int lo=a->position[k],hi=lo;
            if (b->position[k]<lo) lo=b->position[k];
            if (c->position[k]<lo) lo=c->position[k];
            if (b->position[k]>hi) hi=b->position[k];
            if (c->position[k]>hi) hi=c->position[k];
            if ((int64_t)hi-lo>65534) split=1;
            if (lo < -32767 || hi > 32767) {
                int center=(int)(((int64_t)lo+hi)/2);
                /* Nearby particles share a draw origin without rounding any
                 * vertex. Use the exact center when the grid cannot fit. */
                origin[k]=center/16384*16384;
                if ((int64_t)lo-origin[k]<-32767 || (int64_t)hi-origin[k]>32767)
                    origin[k]=center;
            }
        }
        /* Large untextured world surfaces need smaller triangles as well as
         * a local origin. Preserve world units and source order. */
        if (split) {
            struct rf_gpu_graphics_vertex ab=*a,bc=*b,ca=*c;
            for (int k=0;k<3;++k) {
                ab.position[k]=(int)(((int64_t)a->position[k]+b->position[k])/2);
                bc.position[k]=(int)(((int64_t)b->position[k]+c->position[k])/2);
                ca.position[k]=(int)(((int64_t)c->position[k]+a->position[k])/2);
            }
            if (textured ||
                scene_layer_emit(m,a,&ab,&ca,color,light,alpha,screen,textured)<0 ||
                scene_layer_emit(m,&ab,b,&bc,color,light,alpha,screen,textured)<0 ||
                scene_layer_emit(m,&ca,&bc,c,color,light,alpha,screen,textured)<0 ||
                scene_layer_emit(m,&ab,&bc,&ca,color,light,alpha,screen,textured)<0) {
                m->failed=1;return -1;
            }
            return 0;
        }
    }
    if (m->count==m->capacity) {
        uint32_t capacity=m->capacity ? m->capacity*2 : 1024;
        void *next;
        if (capacity>262144) { m->failed=1;return -1; }
        next=realloc(m->triangles,(size_t)capacity*sizeof(*m->triangles));
        if (!next) { m->failed=1;return -1; }
        m->triangles=next;m->capacity=capacity;
    }
    struct scene_layer_triangle *t=&m->triangles[m->count++];
    memset(t,0,sizeof(*t));t->vertex[0]=*a;t->vertex[1]=*b;t->vertex[2]=*c;
    for (int k=0;k<3;++k) {
        t->origin[k]=origin[k];
        for (int v=0;v<3;++v) t->vertex[v].position[k]-=origin[k];
    }
    t->color=color;t->light=light;t->alpha=alpha;t->screen=screen;
    t->textured=textured;t->layer=m->layer;t->emissive=m->emissive;return 0;
}
static int scene_layer_quad(struct scene_layer_mesh *m,
    struct rf_gpu_graphics_vertex v[4],uint32_t color,int alpha,int screen)
{
    if (scene_layer_emit(m,&v[0],&v[1],&v[2],color,256,alpha,screen,0)<0) return -1;
    return scene_layer_emit(m,&v[0],&v[2],&v[3],color,256,alpha,screen,0);
}
static int scene_layer_rect_depth(struct scene_layer_mesh *m,int x,int y,int w,int h,
    uint32_t color,int alpha,int inverse_z,int screen)
{
    struct rf_gpu_graphics_vertex v[4]={0};
    /* Screen-space particles/labels can project far beyond int16 geometry.
     * Clip the constant-depth rectangle before upload, preserving its visible
     * intersection instead of rejecting the entire Scene frame. */
    int64_t right=(int64_t)x+w,bottom=(int64_t)y+h;
    if (w<=0 || h<=0 || right<=0 || bottom<=0 || x>=m->width || y>=m->height) return 0;
    if (x<0) x=0;
    if (y<0) y=0;
    if (right>m->width) right=m->width;
    if (bottom>m->height) bottom=m->height;
    w=(int)(right-x);h=(int)(bottom-y);
    for (int i=0;i<4;++i) {
        v[i].position[0]=x+((i==1 || i==2)?w:0);
        v[i].position[1]=y+(i>=2?h:0);v[i].position[2]=inverse_z;
    }
    return scene_layer_quad(m,v,color,alpha,screen);
}
static int scene_layer_rect(void *context,int x,int y,int w,int h,uint32_t color,int alpha)
{ return scene_layer_rect_depth(context,x,y,w,h,color,alpha,4096,2); }
static int scene_layer_line(void *context,int x0,int y0,int x1,int y1,uint32_t color,int alpha)
{
    struct rf_gpu_graphics_vertex v[4]={0};
    int vertical=abs(y1-y0)>abs(x1-x0);
    if(vertical ? y1<y0 : x1<x0) {
        int t=x0;x0=x1;x1=t;t=y0;y0=y1;y1=t;
    }
    /* One pixel wide strip, including the last pixel; two triangles per edge. */
    x1+=!vertical;y1+=vertical;
    for(int i=0;i<4;++i) {
        v[i].position[0]=(i==1 || i==2?x1:x0)+(i>=2?vertical:0);
        v[i].position[1]=(i==1 || i==2?y1:y0)+(i>=2?!vertical:0);
        v[i].position[2]=4096;
    }
    return scene_layer_quad(context,v,color,alpha,2);
}
static int scene_layer_viewmodel(void *context,const struct toy_screen_vertex *a,
    const struct toy_screen_vertex *b,const struct toy_screen_vertex *c,
    uint32_t color,int light,int alpha,const struct toy_texture_view *texture)
{
    const struct toy_screen_vertex *p[3]={a,b,c};
    struct rf_gpu_graphics_vertex v[3]={0};
    for (int i=0;i<3;++i) {
        v[i].position[0]=p[i]->x;v[i].position[1]=p[i]->y;
        v[i].position[2]=(int)p[i]->inv_z;
        v[i].uv[0]=p[i]->u;v[i].uv[1]=p[i]->v;
    }
    return scene_layer_emit(context,&v[0],&v[1],&v[2],color,light,alpha,1,texture!=NULL);
}
static int scene_layer_project(const struct camera *camera,int width,int height,
    int wx,int wy,int wz,int *x,int *y,int *inverse_z)
{
    int64_t dx=wx-camera->x,dy=wy-camera->y,dz=wz-camera->z;
    int64_t vx=(dx*camera->cy-dz*camera->sy)/1024;
    int64_t vz=(dx*camera->sy+dz*camera->cy)/1024;
    int64_t vy=(dy*camera->pitch_cy-vz*camera->pitch_sy)/1024;
    vz=(dy*camera->pitch_sy+vz*camera->pitch_cy)/1024;
    if (vz<64) return 0;
    int64_t sx=width/2+vx*(width*3/4)/vz,sy=height/2-vy*(width*3/4)/vz;
    if (sx < -100000 || sx>100000 || sy < -100000 || sy>100000) return 0;
    *x=(int)sx;*y=(int)sy;*inverse_z=(int)(1048576/vz);return 1;
}
static void scene_layer_box(struct scene_layer_mesh *,int,int,int,int,int,int,uint32_t,int);
static void scene_layer_effects(struct scene_layer_mesh *mesh,
    const struct rasterfall_effects *effects,const struct camera *camera,int width,int height)
{
    mesh->layer=RF_GPU_SCENE_EFFECTS;
    for (int i=0;i<RASTERFALL_EFFECT_INSTANCE_SLOTS;++i) {
        const struct rasterfall_effect_instance *p=&effects->instances[i];
        int x,y,z,size,alpha=p->alpha>=256?255:p->alpha*255/256;
        if (!p->active || alpha<=0 ||
            p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_INTERACTION_HIGHLIGHT)
            continue;
        if (p->type==RASTERFALL_EFFECT_INSTANCE_RAY) {
            int tx=p->x,ty=p->y,tz=p->z,wx=p->ex,wy=p->ey,wz=p->ez;
            if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_TRACER && p->lifetime_ms>0) {
                int head=p->age_ms*1000/p->lifetime_ms;
                int tail=head-(p->ray_tail_percent>0?p->ray_tail_percent*10:200);
                if (head>1000) head=1000;
                if (tail<0) tail=0;
                tx=p->x+(p->ex-p->x)*tail/1000;ty=p->y+(p->ey-p->y)*tail/1000;tz=p->z+(p->ez-p->z)*tail/1000;
                wx=p->x+(p->ex-p->x)*head/1000;wy=p->y+(p->ey-p->y)*head/1000;wz=p->z+(p->ez-p->z)*head/1000;
            }
            /* World-space ribbon: hardware clips crossing rays at the near
             * plane and width follows perspective for remote/AI fire. */
            struct rf_gpu_graphics_vertex v[4]={0};
            int64_t dx=(int64_t)wx-tx,dy=(int64_t)wy-ty,dz=(int64_t)wz-tz;
            int64_t fx=(int64_t)camera->sy*camera->pitch_cy/1024;
            int64_t fy=camera->pitch_sy,fz=(int64_t)camera->cy*camera->pitch_cy/1024;
            int64_t ox=dy*fz-dz*fy,oy=dz*fx-dx*fz,oz=dx*fy-dy*fx;
            int64_t norm=llabs(ox)+llabs(oy)+llabs(oz);
            int thickness=(p->ray_width>0?p->ray_width:1)*6;
            if (!norm) { ox=camera->cy;oy=0;oz=-camera->sy;norm=llabs(ox)+llabs(oz); }
            if (!norm) continue;
            ox=ox*thickness/norm;oy=oy*thickness/norm;oz=oz*thickness/norm;
            for (int k=0;k<4;++k) {
                int sign=(k==0 || k==3)?-1:1;
                v[k].position[0]=(k<2?tx:wx)+(int)ox*sign;
                v[k].position[1]=(k<2?ty:wy)+(int)oy*sign;
                v[k].position[2]=(k<2?tz:wz)+(int)oz*sign;
            }
            scene_layer_quad(mesh,v,p->color?p->color:0xffd080,alpha,0);
            continue;
        }
        if (p->type!=RASTERFALL_EFFECT_INSTANCE_PARTICLE &&
            p->type!=RASTERFALL_EFFECT_INSTANCE_BILLBOARD) continue;
        if (p->type==RASTERFALL_EFFECT_INSTANCE_BILLBOARD &&
            (p->flags&RASTERFALL_EFFECT_EVENT_LOCAL_VIEW)) continue;
        if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_FRAGMENT) {
            int radius=p->size/48;if (radius<2) radius=2;
            scene_layer_box(mesh,p->x-radius,p->x+radius,p->y-radius,p->y+radius,
                p->z-radius,p->z+radius,p->color,alpha);
            continue;
        }
        if (!scene_layer_project(camera,width,height,p->x,p->y,p->z,&x,&y,&z)) continue;
        size=p->type==RASTERFALL_EFFECT_INSTANCE_PARTICLE ? p->size*2/1000 : p->size;
        if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_EXPLOSION_FLASH)
            size=11+(p->age_ms<24?p->age_ms/4:0);
        if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_PROJECTILE_FLASH) size=7;
        if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_ENEMY_DEATH_DUST)
            size=(int)((int64_t)(p->size/24)*(width*3/4)*z/1048576);
        if (size<1) size=1;
        if (size>256) size=256;
        int h=p->stretch_y>0?size*p->stretch_y/1000:size;
        if (p->type==RASTERFALL_EFFECT_INSTANCE_BILLBOARD) h=size*2;
        if (h<1) h=1;
        if (h>512) h=512;
        scene_layer_rect_depth(mesh,x-size/2,y-h/2,size,h,
            p->color?p->color:0xffc860,alpha,z,1);
    }
}
static void scene_layer_box(struct scene_layer_mesh *mesh,int x0,int x1,int y0,int y1,
    int z0,int z1,uint32_t color,int alpha)
{
    static const int faces[6][4]={{0,1,3,2},{4,6,7,5},{0,2,6,4},{1,5,7,3},{2,3,7,6},{0,4,5,1}};
    for (int face=0;face<6;++face) {
        struct rf_gpu_graphics_vertex v[4]={0};
        for (int k=0;k<4;++k) {
            int i=faces[face][k];v[k].position[0]=(i&1)?x1:x0;
            v[k].position[1]=(i&2)?y1:y0;v[k].position[2]=(i&4)?z1:z0;
        }
        scene_layer_quad(mesh,v,color,alpha,0);
    }
}
static int scene_host_quad(void *context,const int p[4][3],unsigned color)
{
    struct rf_gpu_graphics_vertex v[4]={0};
    for (int k=0;k<4;++k)
        for (int axis=0;axis<3;++axis) v[k].position[axis]=p[k][axis];
    return scene_layer_quad(context,v,color,255,0);
}
static void scene_draw_camera(struct rf_gpu_graphics_draw *d,const struct camera *c,unsigned width,unsigned height)
{
    d->camera[0]=c->x;d->camera[1]=c->y;d->camera[2]=c->z;
    d->view[0]=c->sy;d->view[1]=c->cy;d->view[2]=c->pitch_sy;d->view[3]=c->pitch_cy;
    d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
}
static int scene_packet_prepare(struct scene_layer_workspace *w,struct scene_layer_packet *p,
    const struct toy_map_draw *key)
{
    if(p->valid && !memcmp(&p->key,key,sizeof(*key))) return p->valid>0?0:1;
    struct scene_layer_mesh build={0};
    struct rf_gpu_scene_color_vertex *vertices=NULL;
    struct rf_gpu_graphics_batch_item *runs=NULL;
    uint32_t *indices=NULL;
    unsigned allocated=p->allocated_triangles,run_count=0;
    unsigned available=SCENE_RETAINED_TRIANGLES-w->retained_triangles+allocated;
    int minimum[3]={0},maximum[3]={0};
    int result=1;
    build.layer=RF_GPU_SCENE_WORLD;build.emissive=1;
    if(rf_display_generate(key,scene_host_quad,&build)<0) goto done;
    if(build.count>available) goto done;
    if(!build.count) {scene_packet_clear(w,p);result=0;goto done;}
    /* Changing one digit must not destroy buffers and descriptors. Retain a
     * bounded high-water capacity, including spare triangles in the owner
     * budget, and update only the live vertices after the previous retirement. */
    if(allocated<build.count) {
        allocated=build.count+build.count/4+256;
        if(allocated>available) allocated=available;
    }
    unsigned chunks=(allocated+RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES-1)/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
    if(chunks>p->resource_count) {
        void *next=realloc(p->resources,(size_t)chunks*sizeof(*p->resources));
        if(!next) goto done;
        p->resources=next;
        memset(p->resources+p->resource_count,0,(size_t)(chunks-p->resource_count)*sizeof(*p->resources));
        p->resource_count=chunks;
    }
    vertices=calloc((size_t)allocated*3,sizeof(*vertices));
    indices=malloc((size_t)allocated*3*sizeof(*indices));
    runs=calloc(build.count,sizeof(*runs));
    if(!vertices || !indices || !runs) goto done;
    for(unsigned n=0;n<allocated*3;++n) indices[n]=n%(RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES*3);
    for(unsigned i=0;i<build.count;++i) {
        const struct scene_layer_triangle *t=&build.triangles[i];
        for(unsigned v=0;v<3;++v) {
            unsigned n=i*3+v;
            memcpy(vertices[n].position,t->vertex[v].position,sizeof(vertices[n].position));
            vertices[n].rgb24=t->color;vertices[n].light_q8=t->alpha;
            for(int a=0;a<3;++a) {
                int x=t->vertex[v].position[a]+t->origin[a];
                if(!i && !v) minimum[a]=maximum[a]=x;
                if(x<minimum[a]) minimum[a]=x;
                if(x>maximum[a]) maximum[a]=x;
            }
        }
    }
    for(unsigned start=0;start<build.count;start+=RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES) {
        unsigned n=build.count-start;
        if(n>RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES) n=RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
        struct rf_gpu_graphics_resource **r=&p->resources[start/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES];
        int grow=*r ? rf_gpu_graphics_scene_color_resource_update(w->graphics,*r,vertices+start*3,n*3) : 1;
        if(grow<0) {result=-1;goto done;}
        if(grow) {
            unsigned capacity=allocated-start;
            if(capacity>RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES) capacity=RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
            if(*r && rf_gpu_graphics_resource_destroy(w->graphics,*r)<0) {result=-1;goto done;}
            *r=rf_gpu_graphics_scene_color_resource_create(w->graphics,
                vertices+start*3,capacity*3,indices+start*3,capacity*3);
            if(!*r || (capacity!=n && rf_gpu_graphics_scene_color_resource_update(
                    w->graphics,*r,vertices+start*3,n*3)!=0)) {result=-1;goto done;}
        }
    }
    for(unsigned first=0;first<build.count;) {
        unsigned end=first+1,chunk=first/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
        const struct scene_layer_triangle *t=&build.triangles[first];
        while(end<build.count && end/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES==chunk &&
            !memcmp(t->origin,build.triangles[end].origin,sizeof(t->origin))) ++end;
        struct rf_gpu_graphics_batch_item *item=&runs[run_count++];
        struct rf_gpu_graphics_draw *d=&item->draw;
        item->resource=p->resources[chunk];
        memcpy(d->translation_scale,t->origin,sizeof(t->origin));
        d->translation_scale[3]=1000;d->rotation[1]=1024;
        d->material[0]=t->color;d->material[1]=256;d->material[3]=2;
        d->quality[2]=3;d->texture[0]=d->texture[1]=1;d->texture[2]=256;
        d->first_index=(first%RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES)*3;
        d->index_count=(end-first)*3;d->double_sided=1;d->scene_layer=RF_GPU_SCENE_WORLD;
        first=end;
    }
    void *compact=realloc(runs,(size_t)run_count*sizeof(*runs));
    if(compact) runs=compact;
    free(p->runs);p->runs=runs;runs=NULL;p->run_count=run_count;
    memcpy(p->min,minimum,sizeof(minimum));memcpy(p->max,maximum,sizeof(maximum));
    w->retained_triangles+=allocated-p->allocated_triangles;
    p->allocated_triangles=allocated;p->triangles=build.count;result=0;
done:
    free(vertices);free(indices);free(runs);free(build.triangles);
    if(result) scene_packet_clear(w,p);
    p->key=*key;p->valid=result?-1:1;
    return result;
}
static int scene_display_emit(struct scene_layer_mesh *mesh,const struct toy_map_draw *d,unsigned index)
{
    struct scene_layer_workspace *owner=mesh->owner->shared?mesh->owner->shared:mesh->owner;
    if(mesh->retain && owner->packets && index<TOY_MAP_MAX_DRAW) {
        struct scene_layer_packet *packet=&owner->packets[index];
        int status=scene_packet_prepare(owner,packet,d);
        if(status<0) return -1;
        if(!status) {
            if(!packet->triangles) return 0;
            if(mesh->cull) for(int p=0;p<5;++p) {
                const double *f=mesh->clip[p];double maximum=f[3];
                for(int k=0;k<3;++k) maximum+=f[k]*(f[k]>=0?packet->max[k]:packet->min[k]);
                if(maximum<0) return 0;
            }
            /* One non-drawn marker preserves the exact order relative to live
             * emitters. It also prevents merging across a retained packet. */
            struct rf_gpu_graphics_vertex zero={0};
            if(scene_layer_emit(mesh,&zero,&zero,&zero,0,256,255,2,0)<0) return -1;
            mesh->triangles[mesh->count-1].retained=index+1;
            return 0;
        }
    }
    if(!mesh->cache_displays || !mesh->displays || index>=TOY_MAP_MAX_DRAW)
        return rf_display_generate(d,scene_host_quad,mesh);
    struct rf_display_geometry *entry=&mesh->displays[index];
    if(rf_display_geometry_prepare(entry,d,&mesh->display_budget)<0)
        return rf_display_generate(d,scene_host_quad,mesh);
    /* Exact cached geometry bounds, with the same conservative plane slack
     * as triangle clipping. This alpha presentation geometry casts no shadow. */
    if(mesh->cull && entry->count) for(int p=0;p<5;++p) {
        const double *f=mesh->clip[p];double maximum=f[3];
        for(int k=0;k<3;++k) maximum+=f[k]*(f[k]>=0?entry->max[k]:entry->min[k]);
        if(maximum<0) return 0;
    }
    return rf_display_geometry_emit(entry,scene_host_quad,mesh);
}
static int scene_glass_quad(void *context,const int p[4][3],unsigned color)
{
    struct rf_gpu_graphics_vertex v[4]={0};
    for (int k=0;k<4;++k)
        for (int axis=0;axis<3;++axis) v[k].position[axis]=p[k][axis];
    return scene_layer_quad(context,v,color,42,0);
}
static int scene_projection_quad(void *context,const int p[4][3],unsigned color,int alpha)
{
    struct rf_gpu_graphics_vertex v[4]={0};
    for (int k=0;k<4;++k)
        for (int axis=0;axis<3;++axis) v[k].position[axis]=p[k][axis];
    return scene_layer_quad(context,v,color,alpha,0);
}

static int scene_camera_shell(struct scene_layer_mesh *mesh,
    const struct rf_gpu_scene_aux_view *view)
{
    const struct camera *c=&view->camera;
    struct rf_gpu_graphics_vertex corners[8]={0};
    static const unsigned faces[6][4]={{0,1,3,2},{4,6,7,5},{0,4,5,1},
        {2,3,7,6},{0,2,6,4},{1,5,7,3}};
    for(unsigned i=0;i<8;++i) {
        int x=(i&1)?100:-100,y=(i&2)?70:-70,z=(i&4)?-30:-180;
        int pitched_y=(y*c->pitch_cy+z*c->pitch_sy)/1024;
        int pitched_z=(z*c->pitch_cy-y*c->pitch_sy)/1024;
        corners[i].position[0]=c->x+(x*c->cy+pitched_z*c->sy)/1024;
        corners[i].position[1]=c->y+pitched_y;
        corners[i].position[2]=c->z+(pitched_z*c->cy-x*c->sy)/1024;
    }
    for(unsigned face=0;face<6;++face) {
        struct rf_gpu_graphics_vertex quad[4];
        for(unsigned i=0;i<4;++i)quad[i]=corners[faces[face][i]];
        if(scene_layer_quad(mesh,quad,face==1?0x55ccdc:0x344552,0,0)<0)return -1;
    }
    return 0;
}

static int scene_layers_extract(struct scene_layer_mesh *mesh,
    const struct rf_gpu_scene_layers_input *input,const struct camera *camera,int width,int height)
{
    mesh->width=width;mesh->height=height;
    const char *no_cull=getenv("RF_GPU_SCENE_DISABLE_LAYER_CULL");
    mesh->cull=!(no_cull && no_cull[0]=='1');
    double sy=camera->sy/1024.0,cy=camera->cy/1024.0;
    double ps=camera->pitch_sy/1024.0,pc=camera->pitch_cy/1024.0;
    double forward[3]={sy*pc,ps,cy*pc},right[3]={cy,0,-sy},up[3]={-sy*ps,pc,-cy*ps};
    double extent[2]={width/(2.0*(width*3/4)),height/(2.0*(width*3/4))};
    for (int p=0;p<5;++p) {
        double *f=mesh->clip[p];
        for (int k=0;k<3;++k)
            f[k]=p==0 ? forward[k] : forward[k]*extent[(p-1)/2]+
                ((p&1)?1.0:-1.0)*(p<3?right[k]:up[k]);
        /* Two RFU conservative slack covers floating-point clip arithmetic. */
        f[3]=-f[0]*camera->x-f[1]*camera->y-f[2]*camera->z+
            2*(fabs(f[0])+fabs(f[1])+fabs(f[2]))-(p==0?64:0);
    }
    struct rasterfall_canvas canvas={width,height,0,mesh,scene_layer_rect,scene_layer_line};
    /* Same-package diagnostic control for grid/UI line submission cost. */
    if(getenv("RF_GPU_CANVAS_LINE_REFERENCE"))canvas.line=NULL;
    const struct toy_game_actor *player=toy_game_local_player_actor_const(input->source_game);
    /* GPU SKY is a ray-evaluated HDR background, independent of CPU canvas. */
    mesh->layer=RF_GPU_SCENE_SKY;
    scene_layer_rect_depth(mesh,0,0,width,height,0,255,1,2);
    mesh->layer=RF_GPU_SCENE_WORLD;
    for(int i=0;input->source_game && i<input->source_game->primitive_count;++i) {
        const struct toy_map_primitive *p=&input->source_game->primitives[i];
        if(!strcmp(p->role,"range_cover"))
            scene_layer_box(mesh,p->minx,p->maxx,p->base_y-900,p->surface_y0-900,
                p->minz,p->maxz,0x647881,255);
    }
    if(!input->world_only && input->aux_view && input->aux_view->shell_visible &&
        scene_camera_shell(mesh,input->aux_view)<0)return -1;
    mesh->emissive=1;
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *item=&input->map->items[i];
        if (item->visible && item->draw.type==TOY_MAP_DRAW_SIGN &&
            scene_display_emit(mesh,&item->draw,i)<0) return -1;
        if (item->visible && item->draw.type==TOY_MAP_DRAW_SIGN && item->draw.style==7 &&
            rf_lab_beacon_powered(&item->draw,input->source_game) &&
            rf_lab_beacon_projection_emit(&item->draw,input->host_time_ms,scene_host_quad,mesh)<0) return -1;
    }
    mesh->emissive=0;
    for (unsigned i=0;input->props && i<input->props->count;++i) {
        const struct toy_map_prop *p=&input->props->items[i].prop;
        struct rasterfall_prop_instance instance={p->asset_id,p->x,p->y-900,p->z,
            p->yaw_degrees,p->scale_milli,p->length};
        if (rasterfall_host_activity(&instance,input->host_time_ms,scene_host_quad,mesh)<0)
            return -1;
        mesh->emissive=instance.asset_id==RASTERFALL_PROP_ASSET_LAB_COMPUTER_CASE;
        if (rasterfall_electronics_activity(&instance,&input->electronics,scene_host_quad,mesh)<0)
            return -1;
        mesh->emissive=0;
    }
    mesh->layer=RF_GPU_SCENE_TRANSPARENT;
    mesh->emissive=1;
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *item=&input->map->items[i];
        if (item->visible && item->draw.type==TOY_MAP_DRAW_SIGN &&
            rf_lab_beacon_powered(&item->draw,input->source_game) &&
            rf_lab_projection_light_emit(&item->draw,input->host_time_ms,camera->z,
                scene_projection_quad,mesh)<0) return -1;
    }
    mesh->emissive=0;
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *item=&input->map->items[i];
        const struct toy_map_draw *d=&item->draw;
        if (!item->visible || item->alpha<=0 || item->alpha>=255) continue;
        if (d->type==TOY_MAP_DRAW_SIGN && d->style==6) {
            if (rf_lab_terminal_emit(d,scene_glass_quad,mesh)<0) return -1;
        } else if (d->type==TOY_MAP_DRAW_BOX)
            scene_layer_box(mesh,d->a,d->b,d->f-900,d->e-900,d->c,d->d,d->color,item->alpha);
        else if (d->type==TOY_MAP_DRAW_PLATFORM) {
            struct rf_gpu_graphics_vertex v[4]={0};
            for (int k=0;k<4;++k) {
                v[k].position[0]=(k==1 || k==2)?d->b:d->a;
                v[k].position[1]=d->e-900;v[k].position[2]=k>=2?d->d:d->c;
            }
            scene_layer_quad(mesh,v,d->color,item->alpha,0);
        }
    }
    scene_layer_effects(mesh,input->source_effects,camera,width,height);
    mesh->emissive=1;
    for(unsigned i=0;input->actor_presentations && i<input->actor_presentations->modular_count;++i) {
        const struct rf_gpu_scene_pose_v1 *pose=&input->actor_presentations->modular[i];
        if(!pose->weapon_valid || !pose->muzzle_flash)continue;
        const int *p=pose->weapon_muzzle;
        scene_layer_box(mesh,p[0]-32,p[0]+32,p[1]-32,p[1]+32,p[2]-32,p[2]+32,0xffdf8c,220);
    }
    mesh->emissive=0;
    if(input->world_only)return mesh->failed ? -1 : 0;
    mesh->layer=RF_GPU_SCENE_VIEWMODEL;
    if (input->show_viewmodel && player &&
        player->state!=TOY_GAME_ACTOR_DOWNED) {
        struct rasterfall_viewmodel_geometry geometry={width,height,0,mesh,scene_layer_viewmodel};
        if (rasterfall_viewmodel_geometry(&geometry,input->source_game,input->source_effects,input->viewmodel_light)<0) return -1;
    }
    mesh->layer=RF_GPU_SCENE_OVERLAY;
    struct rf_actor_labels labels={0};
    for (int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *actor=&input->source_game->actors[i];
        int x,y,z;
        int64_t dx=(int64_t)actor->x-camera->x,dz=(int64_t)actor->z-camera->z;
        if (!actor->active || actor==player || actor->kind!=TOY_GAME_ACTOR_AI ||
            actor->state==TOY_GAME_ACTOR_DEAD ||
            dx*dx+dz*dz>10800LL*10800LL) continue;
        if(input->hud.player_ui_view.rts_active &&
            !rf_rts_label_allows(input->hud.player_ui_view.rts,actor,
                player && actor->faction!=player->faction))continue;
        int downed=actor->state==TOY_GAME_ACTOR_DOWNED;
        if (!scene_layer_project(camera,width,height,actor->x,
                actor->ground_y+actor->airborne_y+(downed?-350:700),actor->z,&x,&y,&z)) continue;
        int hostile=player && actor->faction!=player->faction;
        uint32_t color=downed?0xff6060:hostile?0xf03030:0xb0e0ff;
        if(!hostile && strcmp(actor->name,"BASE")) {
            int text_width=rasterfall_canvas_text_width(actor->name,1000);
            int block_width=text_width>68?text_width:68;
            int block_height=downed?49:25;
            /* Project can return distant offscreen pixels. Preserve clipped
             * visibility rather than pulling those actors into the viewport. */
            if(x-block_width/2+block_width<=0 || x-block_width/2>=width ||
                y+block_height<=0 || y>=height)continue;
            int hp=actor->max_hp>0?actor->hp*64/actor->max_hp:0;
            rf_actor_labels_add(&labels,actor->actor_id,actor->combat_generation,
                x,y,actor->name,hp,downed,
                actor->revive_progress_ms*64/TOY_GAME_REVIVE_MS,
                color,actor->hp<40?0xff8050:0x60cf80,0x18202c);
            continue;
        }
        rasterfall_canvas_text(&canvas,x-rasterfall_canvas_text_width(actor->name,1000)/2,y,actor->name,color);
        rasterfall_canvas_rect(&canvas,x-34,y+18,68,7,0x18202c,255);
        int hp=actor->max_hp>0?actor->hp*64/actor->max_hp:0;
        if (hp>64) hp=64;
        rasterfall_canvas_rect(&canvas,x-32,y+20,hp,3,hostile?0xf03030:actor->hp<40?0xff8050:0x60cf80,255);
        if (downed) {
            rasterfall_canvas_text(&canvas,x-24,y+29,"DOWNED",color);
            int revive=actor->revive_progress_ms*64/TOY_GAME_REVIVE_MS;
            if (revive>64) revive=64;
            rasterfall_canvas_rect(&canvas,x-32,y+46,revive,3,0xffd060,255);
        }
    }
    struct rf_actor_label_reserved keep={0};
    if(!input->paused && input->source_game->state==TOY_GAME_PLAYING &&
            player && player->state==TOY_GAME_ACTOR_DOWNED) {
        struct rasterfall_hud_wait_panel geometry;
        rasterfall_hud_wait_for_rescue_geometry(width,height,&geometry);
        keep=(struct rf_actor_label_reserved){geometry.panel.x,geometry.panel.y,
            geometry.panel.w,geometry.panel.h};
    }
    rf_actor_labels_place_reserved(&labels,width,height,&keep);
    rf_actor_labels_paint(&labels,&canvas);
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct toy_map_draw *d=&input->map->items[i].draw;
        int x,y,z;
        if (d->type==TOY_MAP_DRAW_LABEL && scene_layer_project(camera,width,height,(d->a+d->b)/2,-650,(d->c+d->d)/2,&x,&y,&z))
            rasterfall_canvas_text(&canvas,x-fb_string_width(d->text)/2,y,d->text,d->color);
    }
    if (!input->paused && input->source_game->state==TOY_GAME_PLAYING) {
        rasterfall_hud_layout(&canvas,input->fps,&input->hud);
        rasterfall_hud_prompt_layout(&canvas,&input->hud);
    }
    for (int i=0;i<RASTERFALL_EFFECT_INSTANCE_SLOTS;++i) {
        const struct rasterfall_effect_instance *p=&input->source_effects->instances[i];
        if (!p->active || p->type!=RASTERFALL_EFFECT_INSTANCE_OVERLAY) continue;
        int alpha=p->alpha*255/256;if (alpha>255) alpha=255;
        if (p->kind==RASTERFALL_EFFECT_INSTANCE_KIND_DAMAGE_FLASH) {
            rasterfall_canvas_rect(&canvas,0,0,width,8,0xaa0000,alpha);
            rasterfall_canvas_rect(&canvas,0,height-8,width,8,0xaa0000,alpha);
            rasterfall_canvas_rect(&canvas,0,8,8,height-16,0xaa0000,alpha);
            rasterfall_canvas_rect(&canvas,width-8,8,8,height-16,0xaa0000,alpha);
            if (p->dir_x || p->dir_y) {
                struct rf_gpu_graphics_vertex v[3]={0};
                int x=width/2+p->dir_x*70,y=height/2+p->dir_y*70;
                v[0].position[0]=x+p->dir_x*12;v[0].position[1]=y+p->dir_y*12;
                v[1].position[0]=x-p->dir_y*7;v[1].position[1]=y+p->dir_x*7;
                v[2].position[0]=x+p->dir_y*7;v[2].position[1]=y-p->dir_x*7;
                for (int k=0;k<3;++k) v[k].position[2]=4096;
                scene_layer_emit(mesh,&v[0],&v[1],&v[2],0xff3030,256,200,2,0);
            }
        } else rasterfall_canvas_rect(&canvas,p->x,p->y,p->width,p->height,p->color,alpha);
    }
    if (input->ui_layout) input->ui_layout(input->ui_context,&canvas);
    return mesh->failed || canvas.failed ? -1 : 0;
}
static int scene_layer_rank(unsigned layer)
{ return layer==RF_GPU_SCENE_SKY?0:layer==RF_GPU_SCENE_WORLD?1:(int)layer; }
static int scene_layers_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct camera *camera,unsigned width,unsigned height,
    const struct toy_texture_view *texture,struct rf_gpu_graphics_batch_item **batch,
    uint32_t *count,struct rf_gpu_scene_world_gpu_probe_stats *stats)
{
    if (!probe->layer_workspace) probe->layer_workspace=calloc(1,sizeof(*probe->layer_workspace));
    struct scene_layer_workspace *w=probe->layer_workspace;
    if (!w) return -1;
    struct scene_layer_mesh mesh=w->mesh;
    mesh.count=0;mesh.failed=0;
    w->graphics=probe->graphics;mesh.owner=w;
    w->shared=probe->shared_parent?probe->shared_parent->layer_workspace:NULL;
    const char *legacy_retained=getenv("RF_GPU_SCENE_LEGACY_RETAINED_LAYERS");
    const char *legacy_colors_mode=getenv("RF_GPU_SCENE_LEGACY_LAYER_COLORS");
    mesh.retain=!(legacy_retained && !strcmp(legacy_retained,"1")) &&
        !(legacy_colors_mode && !strcmp(legacy_colors_mode,"1"));
    if(mesh.retain && !w->shared && !w->packets) w->packets=calloc(TOY_MAP_MAX_DRAW,sizeof(*w->packets));
    const char *profile=getenv("RF_GPU_SCENE_PROFILE_LAYERS");
    mesh.profile=profile && !strcmp(profile,"1");mesh.clip_us=0;mesh.culled=0;
    const char *legacy_display=getenv("RF_GPU_SCENE_LEGACY_DISPLAY_GEOMETRY");
    mesh.cache_displays=!(legacy_display && !strcmp(legacy_display,"1"));
    if(mesh.cache_displays && !mesh.displays)
        mesh.displays=calloc(TOY_MAP_MAX_DRAW,sizeof(*mesh.displays));
    struct rf_gpu_graphics_vertex *vertices=w->vertices;
    uint32_t *indices=w->indices,*texels=w->texels,white=0xffffff;
    const char *legacy=getenv("RF_GPU_SCENE_REBUILD_LAYERS");
    int rebuild=legacy && legacy[0]=='1';
    const char *legacy_colors=getenv("RF_GPU_SCENE_LEGACY_LAYER_COLORS");
    int color_batch=!(legacy_colors && legacy_colors[0]=='1');
    const char *legacy_packing=getenv("RF_GPU_SCENE_LEGACY_LAYER_PACKING");
    int compact=color_batch && !(legacy_packing && !strcmp(legacy_packing,"1"));
    uint32_t packed_count[RF_GPU_SCENE_LAYER_CHUNKS][2]={{0}};
    int result=-1,textured=0;
    int64_t mark=rf_core_clock_now_us();
    if (probe->layers && scene_layers_extract(&mesh,probe->layers,camera,(int)width,(int)height)<0) goto done;
    stats->layer_extract_us=rf_core_clock_now_us()-mark;
    stats->layer_clip_us=mesh.clip_us;stats->layer_culled=mesh.culled;stats->layer_triangles=mesh.count;
    mark=rf_core_clock_now_us();
    if (mesh.count) {
        if (mesh.capacity*3>w->vertex_capacity) {
            uint32_t capacity=mesh.capacity*3;
            struct rf_gpu_graphics_vertex *next_vertices=calloc(capacity,sizeof(*vertices));
            struct rf_gpu_scene_color_vertex *next_colors=calloc(capacity,sizeof(*next_colors));
            uint32_t *next_indices=malloc((size_t)capacity*sizeof(*indices));
            uint32_t *next_first=malloc((size_t)mesh.capacity*sizeof(*next_first));
            if (!next_vertices || !next_colors || !next_indices || !next_first) {
                free(next_vertices);free(next_colors);free(next_indices);free(next_first);goto done;
            }
            for (unsigned i=0;i<capacity;++i) next_indices[i]=i%(RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES*3);
            free(w->vertices);free(w->colors);free(w->indices);free(w->packed_first);
            vertices=w->vertices=next_vertices;indices=w->indices=next_indices;
            w->colors=next_colors;
            w->packed_first=next_first;
            w->vertex_capacity=capacity;
        }
        for (unsigned i=0;i<mesh.count;++i) {
            const struct scene_layer_triangle *t=&mesh.triangles[i];
            unsigned destination=i*3;
            if (compact) {
                /* Retained packets already own geometry. Pack each remaining
                 * primitive only in its consumed vertex format. Source order
                 * stays in the draw list, including mixed transparent runs. */
                if (t->retained) continue;
                unsigned chunk=i/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
                w->packed_first[i]=packed_count[chunk][!!t->textured]++*3;
                destination=chunk*RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES*3+w->packed_first[i];
            }
            if (!compact || t->textured)
                memcpy(vertices+destination,t->vertex,3*sizeof(*vertices));
            if (!compact || !t->textured) for (unsigned k=0;k<3;++k) {
                memcpy(w->colors[destination+k].position,t->vertex[k].position,
                    sizeof(w->colors[destination+k].position));
                /* Color vertices use the retired light lane for per-face
                 * opacity only when draw.texture[2] selects vertex alpha. */
                w->colors[destination+k].light_q8=t->alpha;
                w->colors[destination+k].rgb24=t->color;
            }
            textured|=t->textured;
        }
        if (textured) {
            if (!texture || !texture->data || (texture->channels!=3 && texture->channels!=4) ||
                (uint64_t)texture->width*texture->height*texture->channels>texture->data_size) goto done;
            uint32_t bytes=texture->width*texture->height*texture->channels;
            if (!w->texture_source || w->texture_bytes!=bytes ||
                w->texture_width!=texture->width || w->texture_height!=texture->height ||
                w->texture_channels!=texture->channels || memcmp(w->texture_source,texture->data,bytes)) {
                unsigned char *source=malloc(bytes);
                uint32_t *next=malloc((size_t)texture->width*texture->height*sizeof(*next));
                if (!source || !next) { free(source);free(next);goto done; }
                memcpy(source,texture->data,bytes);
                for (unsigned i=0;i<texture->width*texture->height;++i) {
                    const unsigned char *p=source+(size_t)i*texture->channels;
                    next[i]=((unsigned)p[0]<<16)|((unsigned)p[1]<<8)|p[2];
                }
                for (unsigned chunk=0;chunk<RF_GPU_SCENE_LAYER_CHUNKS;++chunk) {
                    if (probe->layer_resource[chunk][1] &&
                        rf_gpu_graphics_resource_destroy(probe->graphics,probe->layer_resource[chunk][1])<0) {
                        free(source);free(next);goto done;
                    }
                    probe->layer_resource[chunk][1]=NULL;
                }
                free(w->texture_source);free(w->texels);
                w->texture_source=source;w->texels=next;w->texture_bytes=bytes;
                w->texture_width=texture->width;w->texture_height=texture->height;
                w->texture_channels=texture->channels;
            }
            texels=w->texels;
        }
        stats->layer_pack_us=rf_core_clock_now_us()-mark;mark=rf_core_clock_now_us();
        for (unsigned chunk=0;chunk<RF_GPU_SCENE_LAYER_CHUNKS;++chunk)
        for (int i=0;i<2;++i) {
            unsigned start=chunk*RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
            unsigned triangles=mesh.count>start?mesh.count-start:0;
            if (triangles>RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES)
                triangles=RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
            if (compact) triangles=packed_count[chunk][i];
            if (!triangles || (i && !textured)) continue;
            struct rf_gpu_graphics_resource **resource=&probe->layer_resource[chunk][i];
            int colors=color_batch && !i;
            int grow=*resource && !rebuild ? (colors ?
                rf_gpu_graphics_scene_color_resource_update(probe->graphics,*resource,w->colors+start*3,triangles*3) :
                rf_gpu_graphics_triangle_resource_update(probe->graphics,*resource,vertices+start*3,triangles*3)) : 1;
            if (grow<0) goto done;
            if (grow) stats->layer_created++;
            else stats->layer_reused++;
            if (grow) {
                unsigned allocated=mesh.capacity-start;
                if (allocated>RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES) allocated=RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
                if (rebuild) allocated=triangles;
                if (*resource && rf_gpu_graphics_resource_destroy(probe->graphics,*resource)<0) goto done;
                *resource=colors ? rf_gpu_graphics_scene_color_resource_create(probe->graphics,
                    w->colors+start*3,allocated*3,indices+start*3,allocated*3) :
                    rf_gpu_graphics_resource_create(probe->graphics,vertices+start*3,allocated*3,
                        indices+start*3,allocated*3,i?texels:&white,i?texture->width:1,i?texture->height:1);
                if (*resource && allocated!=triangles && (colors ?
                        rf_gpu_graphics_scene_color_resource_update(probe->graphics,*resource,w->colors+start*3,triangles*3) :
                        rf_gpu_graphics_triangle_resource_update(probe->graphics,*resource,vertices+start*3,triangles*3))<0) goto done;
            }
            if (!probe->layer_resource[chunk][i]) {
                fprintf(stderr,"SCENE layer upload failed triangles=%u textured=%d\n",mesh.count,i);
                for (unsigned v=0;v<mesh.count*3;++v)
                    for (unsigned k=0;k<3;++k)
                        if (mesh.triangles[v/3].vertex[v%3].position[k]<-32767 ||
                            mesh.triangles[v/3].vertex[v%3].position[k]>32767) {
                            fprintf(stderr,"SCENE layer vertex=%u axis=%u position=%d layer=%d screen=%d\n",
                                v,k,mesh.triangles[v/3].vertex[v%3].position[k],
                                mesh.triangles[v/3].layer,mesh.triangles[v/3].screen);
                            goto done;
                        }
                goto done;
            }
        }
        stats->layer_upload_us=rf_core_clock_now_us()-mark;mark=rf_core_clock_now_us();
        if (mesh.count>UINT32_MAX-*count || scene_batch_reserve(batch,
                &probe->batch_capacity,*count+mesh.count)<0) goto done;
        for (unsigned first=0;first<mesh.count;) {
            unsigned end=first+1;
            unsigned chunk=first/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES;
            const struct scene_layer_triangle *t=&mesh.triangles[first];
            if(t->retained) {
                const struct scene_layer_packet *p=&(w->shared?w->shared:w)->packets[t->retained-1];
                if(scene_batch_reserve(batch,&probe->batch_capacity,*count+p->run_count+mesh.count-first-1)<0) goto done;
                for(unsigned j=0;j<p->run_count;++j) {
                    struct rf_gpu_graphics_batch_item *item=&(*batch)[(*count)++];
                    *item=p->runs[j];scene_draw_camera(&item->draw,camera,width,height);
                    if(rf_gpu_graphics_resource_bind(probe->graphics,item->resource)<0)goto done;
                }
                ++first;continue;
            }
            /* Color and opacity live on each triangle. Keep pass, origin and
             * source order intact, including overlapping projections. The
             * legacy baked light value is not consumed by GPU HDR lighting. */
            size_t key=color_batch && !t->textured ? offsetof(struct scene_layer_triangle,screen) :
                offsetof(struct scene_layer_triangle,color);
            while (end<mesh.count && end/RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES==chunk &&
                    !mesh.triangles[end].retained &&
                    !!mesh.triangles[end].alpha==!!t->alpha &&
                    !memcmp((const char *)&mesh.triangles[end]+key,(const char *)t+key,sizeof(*t)-key)) ++end;
            struct rf_gpu_graphics_batch_item *entry=&(*batch)[(*count)++];
            struct rf_gpu_graphics_draw *d=&entry->draw;
            memset(entry,0,sizeof(*entry));entry->resource=probe->layer_resource[chunk][t->textured];
            d->translation_scale[3]=1000;d->rotation[1]=1024;
            for (int k=0;k<3;++k) d->translation_scale[k]=t->origin[k];
            d->camera[0]=camera->x;d->camera[1]=camera->y;d->camera[2]=camera->z;
            d->view[0]=camera->sy;d->view[1]=camera->cy;d->view[2]=camera->pitch_sy;d->view[3]=camera->pitch_cy;
            d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
            d->material[0]=t->color;d->material[1]=t->light;d->material[2]=t->textured;
            if (color_batch && !t->textured) d->material[3]=2;
            if (t->emissive) d->quality[2]=3;
            d->texture[0]=t->textured?texture->width:1;d->texture[1]=t->textured?texture->height:1;
            d->texture[2]=color_batch && !t->textured && t->alpha ? 256 : t->alpha;
            d->texture[3]=t->screen;
            d->first_index=compact?w->packed_first[first]:(first%RF_GPU_SCENE_LAYER_CHUNK_TRIANGLES)*3;
            d->index_count=(end-first)*3;d->double_sided=1;d->scene_layer=t->layer;
            first=end;
        }
    }
    /* Stable pass partition; never sort transparent sources by material/depth. */
    if (scene_batch_reserve(&w->ordered,&w->ordered_capacity,*count)<0) goto done;
    struct rf_gpu_graphics_batch_item *ordered=w->ordered;
    unsigned n=0;
    for (int rank=0;rank<RF_GPU_SCENE_LAYER_COUNT;++rank)
        for (int alpha=0;alpha<2;++alpha)
            for (unsigned i=0;i<*count;++i) {
                struct rf_gpu_graphics_draw *d=&(*batch)[i].draw;
                if (scene_layer_rank(d->scene_layer)==rank && !!d->texture[2]==alpha) {
                    ordered[n++]=(*batch)[i];stats->layer_draws[d->scene_layer]++;
                }
            }
    if (n!=*count) goto done;
    w->ordered=*batch;*batch=ordered;
    uint32_t old_capacity=probe->batch_capacity;
    probe->batch_capacity=w->ordered_capacity;w->ordered_capacity=old_capacity;
    stats->layer_batch_us=rf_core_clock_now_us()-mark;
    result=0;
done:
    w->mesh=mesh;return result;
}
