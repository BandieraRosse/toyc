/* Independent Scene presentation extraction. No toy_renderer or framebuffer.
 * All source reads finish before resource preparation and target submission. */
#include "rasterfall_sky.h"
#include "rasterfall_viewmodel.h"
struct scene_layer_triangle {
    struct rf_gpu_graphics_vertex vertex[3];
    uint32_t color;
    int light,alpha,screen,textured,layer;
};
struct scene_layer_mesh {
    struct scene_layer_triangle *triangles;
    uint32_t count,capacity;
    int layer,failed;
};
static int scene_layer_emit(struct scene_layer_mesh *m,
    const struct rf_gpu_graphics_vertex *a,const struct rf_gpu_graphics_vertex *b,
    const struct rf_gpu_graphics_vertex *c,uint32_t color,int light,int alpha,
    int screen,int textured)
{
    if (m->failed) return -1;
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
    t->color=color;t->light=light;t->alpha=alpha;t->screen=screen;
    t->textured=textured;t->layer=m->layer;return 0;
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
    for (int i=0;i<4;++i) {
        v[i].position[0]=x+((i==1 || i==2)?w:0);
        v[i].position[1]=y+(i>=2?h:0);v[i].position[2]=inverse_z;
    }
    return scene_layer_quad(m,v,color,alpha,screen);
}
static int scene_layer_rect(void *context,int x,int y,int w,int h,uint32_t color,int alpha)
{ return scene_layer_rect_depth(context,x,y,w,h,color,alpha,4096,2); }
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
        if (!p->active || alpha<=0) continue;
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
static int scene_layers_extract(struct scene_layer_mesh *mesh,
    const struct rf_gpu_scene_layers_input *input,const struct camera *camera,int width,int height)
{
    struct rasterfall_canvas canvas={width,height,0,mesh,scene_layer_rect};
    const struct toy_game_actor *player=toy_game_local_player_actor_const(input->source_game);
    mesh->layer=RF_GPU_SCENE_SKY;rasterfall_sky_layout(&canvas,camera);
    mesh->layer=RF_GPU_SCENE_TRANSPARENT;
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct rf_gpu_scene_world_render_item_v1 *item=&input->map->items[i];
        const struct toy_map_draw *d=&item->draw;
        if (!item->visible || item->alpha<=0 || item->alpha>=255) continue;
        if (d->type==TOY_MAP_DRAW_BOX)
            scene_layer_box(mesh,d->a,d->b,-900,d->e-900,d->c,d->d,d->color,item->alpha);
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
    mesh->layer=RF_GPU_SCENE_VIEWMODEL;
    if (player && player->state!=TOY_GAME_ACTOR_DOWNED) {
        struct rasterfall_viewmodel_geometry geometry={width,height,0,mesh,scene_layer_viewmodel};
        if (rasterfall_viewmodel_geometry(&geometry,input->source_game,input->source_effects,input->viewmodel_light)<0) return -1;
    }
    mesh->layer=RF_GPU_SCENE_OVERLAY;
    for (int i=0;i<TOY_GAME_MAX_ACTORS;++i) {
        const struct toy_game_actor *actor=&input->source_game->actors[i];
        int x,y,z;
        int64_t dx=(int64_t)actor->x-camera->x,dz=(int64_t)actor->z-camera->z;
        if (!actor->active || actor==player || actor->kind!=TOY_GAME_ACTOR_AI ||
            dx*dx+dz*dz>10800LL*10800LL) continue;
        int downed=actor->state==TOY_GAME_ACTOR_DOWNED;
        if (!scene_layer_project(camera,width,height,actor->x,
                actor->ground_y+actor->airborne_y+(downed?-350:700),actor->z,&x,&y,&z)) continue;
        uint32_t color=downed?0xff6060:0xb0e0ff;
        rasterfall_canvas_text(&canvas,x-fb_string_width(actor->name)/2,y,actor->name,color);
        rasterfall_canvas_rect(&canvas,x-34,y+18,68,7,0x18202c,255);
        int hp=actor->max_hp>0?actor->hp*64/actor->max_hp:0;
        if (hp>64) hp=64;
        rasterfall_canvas_rect(&canvas,x-32,y+20,hp,3,actor->hp<40?0xff8050:0x60cf80,255);
        if (downed) {
            rasterfall_canvas_text(&canvas,x-24,y+29,"DOWNED",color);
            int revive=actor->revive_progress_ms*64/TOY_GAME_REVIVE_MS;
            if (revive>64) revive=64;
            rasterfall_canvas_rect(&canvas,x-32,y+46,revive,3,0xffd060,255);
        }
    }
    for (unsigned i=0;input->map && i<input->map->count;++i) {
        const struct toy_map_draw *d=&input->map->items[i].draw;
        int x,y,z;
        if (d->type==TOY_MAP_DRAW_LABEL && scene_layer_project(camera,width,height,(d->a+d->b)/2,-650,(d->c+d->d)/2,&x,&y,&z))
            rasterfall_canvas_text(&canvas,x-fb_string_width(d->text)/2,y,d->text,d->color);
    }
    if (!input->paused && input->source_game->state==TOY_GAME_PLAYING) {
        rasterfall_hud_layout(&canvas,input->fps,&input->hud);
        if (!input->hud.shop_open) rasterfall_hud_prompt_layout(&canvas,&input->hud);
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
    struct scene_layer_mesh mesh={0};
    struct rf_gpu_graphics_vertex *vertices=NULL;
    uint32_t *indices=NULL,*texels=NULL,white=0xffffff;
    int result=-1,textured=0;
    if (probe->layers && scene_layers_extract(&mesh,probe->layers,camera,(int)width,(int)height)<0) goto done;
    if (mesh.count) {
        vertices=calloc((size_t)mesh.count*3,sizeof(*vertices));
        indices=calloc((size_t)mesh.count*3,sizeof(*indices));
        if (!vertices || !indices) goto done;
        for (unsigned i=0;i<mesh.count;++i) {
            memcpy(vertices+i*3,mesh.triangles[i].vertex,3*sizeof(*vertices));
            for (unsigned k=0;k<3;++k) indices[i*3+k]=i*3+k;
            textured|=mesh.triangles[i].textured;
        }
        if (textured) {
            if (!texture || !texture->data || (texture->channels!=3 && texture->channels!=4) ||
                (uint64_t)texture->width*texture->height*texture->channels>texture->data_size) goto done;
            texels=calloc((size_t)texture->width*texture->height,sizeof(*texels));
            if (!texels) goto done;
            for (unsigned i=0;i<texture->width*texture->height;++i) {
                const unsigned char *p=texture->data+(size_t)i*texture->channels;
                texels[i]=((unsigned)p[0]<<16)|((unsigned)p[1]<<8)|p[2];
            }
        }
        for (int i=0;i<2;++i) {
            if (probe->layer_resource[i] && rf_gpu_graphics_resource_destroy(probe->graphics,probe->layer_resource[i])<0) goto done;
            probe->layer_resource[i]=NULL;
            if (i && !textured) continue;
            probe->layer_resource[i]=rf_gpu_graphics_resource_create(probe->graphics,vertices,mesh.count*3,
                indices,mesh.count*3,i?texels:&white,i?texture->width:1,i?texture->height:1);
            if (!probe->layer_resource[i]) goto done;
        }
        void *next=realloc(*batch,((size_t)*count+mesh.count)*sizeof(**batch));
        if (!next) goto done;
        *batch=next;
        for (unsigned first=0;first<mesh.count;) {
            unsigned end=first+1;
            const struct scene_layer_triangle *t=&mesh.triangles[first];
            while (end<mesh.count && !memcmp(&mesh.triangles[end].color,&t->color,
                    sizeof(*t)-offsetof(struct scene_layer_triangle,color))) ++end;
            struct rf_gpu_graphics_batch_item *entry=&(*batch)[(*count)++];
            struct rf_gpu_graphics_draw *d=&entry->draw;
            memset(entry,0,sizeof(*entry));entry->resource=probe->layer_resource[t->textured];
            d->translation_scale[3]=1000;d->rotation[1]=1024;
            d->camera[0]=camera->x;d->camera[1]=camera->y;d->camera[2]=camera->z;
            d->view[0]=camera->sy;d->view[1]=camera->cy;d->view[2]=camera->pitch_sy;d->view[3]=camera->pitch_cy;
            d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
            d->material[0]=t->color;d->material[1]=t->light;d->material[2]=t->textured;
            d->texture[0]=t->textured?texture->width:1;d->texture[1]=t->textured?texture->height:1;
            d->texture[2]=t->alpha;d->texture[3]=t->screen;
            d->first_index=first*3;d->index_count=(end-first)*3;d->double_sided=1;d->scene_layer=t->layer;
            first=end;
        }
    }
    /* Stable pass partition; never sort transparent sources by material/depth. */
    struct rf_gpu_graphics_batch_item *ordered=calloc(*count,sizeof(*ordered));
    if (!ordered) goto done;
    unsigned n=0;
    for (int rank=0;rank<RF_GPU_SCENE_LAYER_COUNT;++rank)
        for (int alpha=0;alpha<2;++alpha)
            for (unsigned i=0;i<*count;++i) {
                struct rf_gpu_graphics_draw *d=&(*batch)[i].draw;
                if (scene_layer_rank(d->scene_layer)==rank && !!d->texture[2]==alpha) {
                    ordered[n++]=(*batch)[i];stats->layer_draws[d->scene_layer]++;
                }
            }
    if (n!=*count) { free(ordered);goto done; }
    free(*batch);*batch=ordered;result=0;
done:
    free(mesh.triangles);free(vertices);free(indices);free(texels);return result;
}
