#include "tlibc_everything.h"
#include "rf_gpu_scene_world_gpu.h"
#include "rf_gpu_scene_actor_gpu.h"
#include "rf_gpu_scene_pose.h"

#ifndef TOYC_WINDOWS
int rf_gpu_scene_world_gpu_prepare(struct rf_gpu_scene_world_resources *owner,
    struct rf_gpu_resource_cache *cache,struct rf_gpu_graphics *graphics,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count)
{
    (void)owner;(void)cache;(void)graphics;(void)camera;(void)width;
    (void)height;(void)items;(void)capacity;(void)count;
    return -1;
}
int rf_gpu_scene_world_gpu_probe_frame(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *owner,
    const struct camera *camera,uint32_t width,uint32_t height,
    const struct rf_gpu_scene_pose_v1 *poses,uint32_t pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,
    const struct toy_texture_view *model_texture,
    struct rf_gpu_scene_world_gpu_probe_stats *stats,const char *capture_path)
{
    (void)probe;(void)context;(void)owner;(void)camera;
    (void)width;(void)height;(void)poses;(void)pose_count;(void)flags;
    (void)projectiles;(void)interactables;(void)model_texture;(void)enemies;
    (void)stats;(void)capture_path;
    return -1;
}
void rf_gpu_scene_world_gpu_probe_close(struct rf_gpu_scene_world_gpu_probe *probe)
{ (void)probe; }
#else
#include "rf_gpu_resource_cache.h"
#include "rasterfall_render.h"
#include "fb_font.h"
#include <limits.h>
#include <stdio.h>
#include <math.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

static const uint32_t flag_cube_indices[36]={
    0,1,3,0,3,2, 4,6,7,4,7,5,
    0,2,6,0,6,4, 1,5,7,1,7,3,
    2,3,7,2,7,6, 0,4,5,0,5,1
};
static struct rf_gpu_graphics_resource *flag_cube_resource(
    struct rf_gpu_graphics *graphics,int minx,int maxx,int miny,int maxy,
    int minz,int maxz)
{
    struct rf_gpu_graphics_vertex vertices[8]={{0}};
    const uint32_t white=0xffffff;
    for(int i=0;i<8;++i) {
        vertices[i].position[0]=(i&1)?maxx:minx;
        vertices[i].position[1]=(i&2)?maxy:miny;
        vertices[i].position[2]=(i&4)?maxz:minz;
    }
    return rf_gpu_graphics_resource_create(graphics,vertices,8,
        flag_cube_indices,36,&white,1,1);
}
static struct rf_gpu_graphics_resource *flag_label_resource(
    struct rf_gpu_graphics *graphics,const char label[5],uint32_t *index_count)
{
    const uint32_t max_quads=2u*4u*FB_FONT_W*FB_FONT_H;
    const uint32_t white=0xffffff;
    struct rf_gpu_graphics_vertex *vertices=NULL;
    uint32_t *indices=NULL,quads=0;
    struct rf_gpu_graphics_resource *resource=NULL;
    int chars=(int)strlen(label),cell,start_x,start_y,total_px;
    *index_count=UINT32_MAX;
    if (!graphics || chars<1 || chars>4) return NULL;
    cell=640/(chars*FB_FONT_W);
    if (430/FB_FONT_H<cell) cell=430/FB_FONT_H;
    if (cell<1) return NULL;
    start_x=32+(640-chars*FB_FONT_W*cell)/2;
    start_y=2418-(430-FB_FONT_H*cell)/2;
    total_px=chars*FB_FONT_W;
    vertices=calloc(max_quads*4,sizeof(*vertices));
    indices=calloc(max_quads*6,sizeof(*indices));
    if (!vertices || !indices) goto done;
    for(int side=0;side<2;++side) {
        int mirror=!side,z=side?-14:14;
        for(int i=0;i<chars;++i) for(int row=0;row<FB_FONT_H;++row) {
            unsigned char bits=fb_font_glyph_row((unsigned char)label[i],row);
            for(int col=0;col<FB_FONT_W;++col) {
                int run=col,px,x0,x1,y0,y1;
                uint32_t base;
                if (!(bits & (unsigned char)(0x80>>col))) continue;
                while(run+1<FB_FONT_W &&
                    (bits & (unsigned char)(0x80>>(run+1)))) run++;
                px=mirror ? total_px-(i*FB_FONT_W+run+1) : i*FB_FONT_W+col;
                x0=start_x+px*cell;
                x1=start_x+(mirror ? total_px-(i*FB_FONT_W+col) :
                    i*FB_FONT_W+run+1)*cell;
                y0=start_y-row*cell;y1=y0-cell;
                if (quads>=max_quads) goto done;
                base=quads*4;
                vertices[base+0].position[0]=x0;
                vertices[base+0].position[1]=y0;
                vertices[base+0].position[2]=z;
                vertices[base+1].position[0]=x1;
                vertices[base+1].position[1]=y0;
                vertices[base+1].position[2]=z;
                vertices[base+2].position[0]=x1;
                vertices[base+2].position[1]=y1;
                vertices[base+2].position[2]=z;
                vertices[base+3].position[0]=x0;
                vertices[base+3].position[1]=y1;
                vertices[base+3].position[2]=z;
                indices[quads*6+0]=base;
                indices[quads*6+1]=base+1;
                indices[quads*6+2]=base+2;
                indices[quads*6+3]=base;
                indices[quads*6+4]=base+2;
                indices[quads*6+5]=base+3;
                quads++;col=run;
            }
        }
    }
    if (quads)
        resource=rf_gpu_graphics_resource_create(graphics,vertices,quads*4,
            indices,quads*6,&white,1,1);
    if (resource || !quads) *index_count=quads*6;
done:
    free(vertices);free(indices);
    return resource;
}
static int flag_label_resources_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_flag_frame_v1 *flags)
{
    for(uint32_t i=0;i<flags->count;++i) {
        const struct rf_gpu_scene_flag_item_v1 *flag=&flags->items[i];
        if (!memchr(flag->label,0,sizeof(flag->label))) return -1;
        if (!memcmp(probe->flag_label_text[i],flag->label,sizeof(flag->label)))
            continue;
        if (probe->flag_label[i]) {
            if (rf_gpu_graphics_resource_destroy(probe->graphics,
                    probe->flag_label[i])<0) return -1;
            probe->flag_label[i]=NULL;
        }
        probe->flag_label_indices[i]=0;
        memcpy(probe->flag_label_text[i],flag->label,sizeof(flag->label));
        if (flag->label[0]) {
            probe->flag_label[i]=flag_label_resource(probe->graphics,
                flag->label,&probe->flag_label_indices[i]);
            if (probe->flag_label_indices[i]==UINT32_MAX) return -1;
        }
    }
    return 0;
}
static int flag_draws_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,
    uint32_t *count,uint32_t *text_count)
{
    uint32_t total=0,text_draws=0;
    if (!probe || !flags || !camera || !items || !count || !text_count ||
        flags->count>RF_GPU_SCENE_FLAG_CAP) return -1;
    for(uint32_t i=0;i<flags->count;++i) {
        const struct rf_gpu_scene_flag_item_v1 *flag=&flags->items[i];
        if (flag->active!=0 && flag->active!=1) return -1;
        if (!flag->active) continue;
        if (capacity-total<2) return -1;
        for(int part=0;part<2;++part) {
            struct rf_gpu_graphics_batch_item *item=&items[total++];
            struct rf_gpu_graphics_draw *draw=&item->draw;
            uint32_t cloth=(uint32_t)flag->color;
            memset(item,0,sizeof(*item));
            item->resource=part ? probe->flag_cloth : probe->flag_pole;
            draw->translation_scale[0]=flag->x;
            draw->translation_scale[2]=flag->z;
            draw->translation_scale[3]=1000;
            draw->rotation[1]=1024;
            draw->camera[0]=camera->x;draw->camera[1]=camera->y;
            draw->camera[2]=camera->z;
            draw->view[0]=camera->sy;draw->view[1]=camera->cy;
            draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
            draw->projection[0]=(int32_t)width;
            draw->projection[1]=(int32_t)height;
            draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
            if (flag->selected) cloth+=0x202020;
            draw->material[0]=part ? cloth&0xffffffu : 0x5A6470;
            draw->material[1]=256;
            draw->texture[0]=draw->texture[1]=1;
            draw->index_count=36;draw->double_sided=1;
            draw->integer_depth=0;
            if (!item->resource ||
                rf_gpu_graphics_resource_bind(probe->graphics,item->resource)<0 ||
                rf_gpu_graphics_validate_draw(probe->graphics,draw)<0) {
                fprintf(stderr,"SCENE flag draw invalid index=%u part=%d pos=(%d,%d)\n",
                    i,part,flag->x,flag->z);
                return -1;
            }
        }
        if (probe->flag_label_indices[i]) {
            struct rf_gpu_graphics_batch_item *item;
            struct rf_gpu_graphics_draw *draw;
            if (total>=capacity || !probe->flag_label[i]) return -1;
            item=&items[total++];
            *item=items[total-2];
            draw=&item->draw;
            item->resource=probe->flag_label[i];
            draw->material[0]=0xFFF0C0;
            draw->index_count=probe->flag_label_indices[i];
            if (rf_gpu_graphics_resource_bind(probe->graphics,item->resource)<0 ||
                rf_gpu_graphics_validate_draw(probe->graphics,draw)<0) return -1;
            text_draws++;
        }
    }
    *count=total;
    *text_count=text_draws;
    return 0;
}

static uint32_t world_u32(const unsigned char *p)
{
    return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24;
}
static int world_range(const struct rasterfall_model_asset *model,
    const unsigned char *ptr,uint64_t bytes)
{
    uintptr_t base=(uintptr_t)model->data,address=(uintptr_t)ptr;
    return model->data && model->data_size>0 && ptr && address>=base &&
        address-base<=(uintptr_t)model->data_size &&
        bytes<=(uint64_t)model->data_size-(address-base);
}
static uint16_t projectile_u16(const unsigned char *p)
{
    return (uint16_t)(p[0]|(uint16_t)p[1]<<8);
}
static struct rf_gpu_graphics_resource *projectile_asset_create(
    struct rf_gpu_graphics *graphics,const char *path,
    const struct toy_texture_view *texture,uint32_t *indices_out,
    uint32_t *color_out,int *scale_out)
{
    struct rasterfall_model_asset model={0};
    struct rf_gpu_graphics_vertex *vertices=NULL;
    uint32_t *indices=NULL,*texels=NULL;
    struct rf_gpu_graphics_resource *resource=NULL;
    uint32_t count,first,width=1,height=1;
    int length,center[3];
    *indices_out=0;
    if (rasterfall_model_load(&model,path)<0) return NULL;
    if (model.primitive_count!=1 || !model.material_count ||
        model.vertex_bytes<22 || model.index_count>65536 ||
        !world_range(&model,model.primitives,16) ||
        !world_range(&model,model.indices,(uint64_t)model.index_count*4) ||
        !world_range(&model,model.vertices,
            (uint64_t)model.vertex_count*model.vertex_bytes) ||
        !world_range(&model,model.materials,model.material_bytes)) goto done;
    first=world_u32(model.primitives);
    count=world_u32(model.primitives+4);
    if (!count || count%3 || count>65536 || first>model.index_count ||
        count>model.index_count-first ||
        world_u32(model.primitives+8)>=model.material_count) goto done;
    center[0]=model.min_x+(model.max_x-model.min_x)/2;
    center[1]=model.min_y+(model.max_y-model.min_y)/2;
    center[2]=model.min_z+(model.max_z-model.min_z)/2;
    length=model.max_x-model.min_x;
    if (model.max_y-model.min_y>length) length=model.max_y-model.min_y;
    if (model.max_z-model.min_z>length) length=model.max_z-model.min_z;
    if (length<=0) goto done;
    *scale_out=240000*TOY_CONFIG_THROW_MODEL_SCALE/1000/length;
    if (*scale_out<1) *scale_out=1;
    vertices=calloc(count,sizeof(*vertices));
    indices=malloc((size_t)count*sizeof(*indices));
    if (!vertices || !indices) goto done;
    for(uint32_t i=0;i<count;++i) {
        uint32_t source=world_u32(model.indices+(size_t)(first+i)*4);
        const unsigned char *v;
        if (source>=model.vertex_count) goto done;
        v=model.vertices+(size_t)source*model.vertex_bytes;
        for(int axis=0;axis<3;++axis)
            vertices[i].position[axis]=(int32_t)world_u32(v+axis*4)-center[axis];
        vertices[i].uv[0]=projectile_u16(v+18);
        vertices[i].uv[1]=projectile_u16(v+20);
        indices[i]=i;
    }
    if (texture && texture->data) {
        if (!texture->width || !texture->height || texture->width>1024 ||
            texture->height>1024 ||
            (texture->channels!=3 && texture->channels!=4) ||
            (uint64_t)texture->width*texture->height*texture->channels>
                texture->data_size || texture->has_transparency) goto done;
        width=texture->width;height=texture->height;
    }
    texels=malloc((size_t)width*height*sizeof(*texels));
    if (!texels) goto done;
    for(uint32_t i=0;i<width*height;++i) {
        if (width==1 && height==1 && (!texture || !texture->data))
            texels[i]=0xffffff;
        else {
            const unsigned char *pixel=texture->data+(size_t)i*texture->channels;
            if (texture->channels==4 && pixel[3]!=255) goto done;
            texels[i]=(uint32_t)pixel[0]<<16|(uint32_t)pixel[1]<<8|pixel[2];
        }
    }
    resource=rf_gpu_graphics_resource_create(graphics,vertices,count,
        indices,count,texels,width,height);
    if (resource) {
        *indices_out=count;
        *color_out=world_u32(model.materials+
            (size_t)world_u32(model.primitives+8)*model.material_bytes)&0xffffffu;
    }
done:
    free(vertices);free(indices);free(texels);
    rasterfall_model_unload(&model);
    return resource;
}
static int projectile_assets_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct toy_texture_view *texture)
{
    static const char *const paths[2]={
        "rasterfall/assets/models/bomb.rmesh",
        "rasterfall/assets/models/molotov.rmesh"};
    if (!probe || !probe->graphics) return -1;
    for(int kind=0;kind<2;++kind) if (!probe->projectile_asset[kind]) {
        probe->projectile_asset[kind]=projectile_asset_create(probe->graphics,
            paths[kind],texture,&probe->projectile_indices[kind],
            &probe->projectile_color[kind],&probe->projectile_scale[kind]);
        if (!probe->projectile_asset[kind]) {
            fprintf(stderr,"SCENE projectile asset failed kind=%d texture=%ux%u channels=%u transparency=%u\n",
                kind,texture?texture->width:0,texture?texture->height:0,
                texture?texture->channels:0,texture?texture->has_transparency:0);
            return -1;
        }
    }
    probe->projectile_texture_width=texture && texture->data ? texture->width : 1;
    probe->projectile_texture_height=texture && texture->data ? texture->height : 1;
    return 0;
}
static int projectile_draws_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct toy_texture_view *texture,const struct camera *camera,
    uint32_t width,uint32_t height,struct rf_gpu_graphics_batch_item *items,
    uint32_t capacity,uint32_t *count)
{
    static const int sin16[16]={0,391,724,946,1024,946,724,391,
        0,-391,-724,-946,-1024,-946,-724,-391};
    static const int cos16[16]={1024,946,724,391,0,-391,-724,-946,
        -1024,-946,-724,-391,0,391,724,946};
    if (!probe || !projectiles || !camera || !items || !count ||
        projectiles->count>capacity) return -1;
    for(uint32_t i=0;i<projectiles->count;++i) {
        const struct rf_gpu_scene_projectile_item_v1 *source=&projectiles->items[i];
        struct rf_gpu_graphics_batch_item *item=&items[i];
        struct rf_gpu_graphics_draw *draw=&item->draw;
        int kind=source->kind==TOY_GAME_WEAPON_BOMB ? 0 : 1;
        int angle,phase;
        if (source->source_slot>=TOY_GAME_MAX_PROJECTILES ||
            (i && source->source_slot<=projectiles->items[i-1].source_slot) ||
            (source->kind!=TOY_GAME_WEAPON_BOMB &&
             source->kind!=TOY_GAME_WEAPON_MOLOTOV) ||
            source->age_ms<0 || source->flash_ms<0) return -1;
        angle=(source->age_ms/5)%360;
        phase=angle*16/360;
        memset(item,0,sizeof(*item));
        item->resource=probe->projectile_asset[kind];
        draw->translation_scale[0]=source->x;
        draw->translation_scale[1]=-900+source->y+120;
        draw->translation_scale[2]=source->z;
        draw->translation_scale[3]=probe->projectile_scale[kind];
        draw->rotation[0]=-sin16[phase];
        draw->rotation[1]=cos16[phase];
        draw->camera[0]=camera->x;draw->camera[1]=camera->y;
        draw->camera[2]=camera->z;
        draw->view[0]=camera->sy;draw->view[1]=camera->cy;
        draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
        draw->projection[0]=(int32_t)width;
        draw->projection[1]=(int32_t)height;
        draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
        draw->material[0]=probe->projectile_color[kind];
        draw->material[1]=source->scene_light_q8;
        draw->material[2]=texture && texture->data && !source->flash_ms;
        draw->texture[0]=probe->projectile_texture_width;
        draw->texture[1]=probe->projectile_texture_height;
        draw->index_count=probe->projectile_indices[kind];
        draw->double_sided=1;
        draw->integer_depth=0;
        if (!item->resource ||
            rf_gpu_graphics_resource_bind(probe->graphics,item->resource)<0 ||
            rf_gpu_graphics_validate_draw(probe->graphics,draw)<0) {
            fprintf(stderr,"SCENE projectile draw invalid slot=%u kind=%d pos=(%d,%d,%d)\n",
                source->source_slot,source->kind,source->x,source->y,source->z);
            return -1;
        }
    }
    *count=projectiles->count;
    return 0;
}

static int pickup_model_kind(const struct rf_gpu_scene_interactable_item_v1 *item)
{
    if (item->kind==TOY_MAP_PICKUP_SMG ||
        (item->kind==TOY_MAP_PICKUP_WEAPON && item->weapon==TOY_GAME_WEAPON_SMG))
        return 0;
    if (item->kind==TOY_MAP_PICKUP_SHOTGUN ||
        (item->kind==TOY_MAP_PICKUP_WEAPON && item->weapon==TOY_GAME_WEAPON_SHOTGUN))
        return 1;
    if (item->kind==TOY_MAP_PICKUP_WEAPON) {
        if (item->weapon==TOY_GAME_WEAPON_AK) return 2;
        if (item->weapon==TOY_GAME_WEAPON_AWP) return 3;
        if (item->weapon==TOY_GAME_WEAPON_AXE) return 4;
    }
    if (item->kind==TOY_MAP_PICKUP_THROWABLE)
        return item->weapon==TOY_GAME_WEAPON_BOMB ? 5 : 6;
    return -1;
}

static int pickup_asset_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    int kind,const struct toy_texture_view *texture)
{
    static const char *const paths[RF_GPU_SCENE_PICKUP_MODEL_COUNT]={
        "rasterfall/assets/models/smg_mac10.rmesh",
        "rasterfall/assets/models/sg_pump_action.rmesh",
        "rasterfall/assets/models/ar_ak47.rmesh",
        "rasterfall/assets/models/rf_AWP.rmesh",
        "rasterfall/assets/models/axe.rmesh",
        "rasterfall/assets/models/bomb.rmesh",
        "rasterfall/assets/models/molotov.rmesh"};
    struct rf_gpu_scene_pickup_asset *asset=&probe->pickup[kind];
    struct rasterfall_model_asset model={0};
    struct rf_gpu_graphics_vertex *vertices=NULL;
    uint32_t *indices=NULL,*texels=NULL;
    uint32_t width=1,height=1;
    int length,result=-1;
    if (asset->primitive_count) return 0;
    if (rasterfall_model_load(&model,paths[kind])<0) return -1;
    if (!model.primitive_count ||
        model.primitive_count>RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES ||
        model.vertex_bytes<22 || !model.material_count ||
        !world_range(&model,model.primitives,(uint64_t)model.primitive_count*16) ||
        !world_range(&model,model.materials,
            (uint64_t)model.material_count*model.material_bytes) ||
        !world_range(&model,model.indices,(uint64_t)model.index_count*4) ||
        !world_range(&model,model.vertices,
            (uint64_t)model.vertex_count*model.vertex_bytes)) goto done;
    length=model.max_x-model.min_x;
    if (model.max_y-model.min_y>length) length=model.max_y-model.min_y;
    if (model.max_z-model.min_z>length) length=model.max_z-model.min_z;
    if (length<=0) goto done;
    asset->scale=380000/length;
    if (asset->scale<1) asset->scale=1;
    if (kind==5 || kind==6) asset->scale/=2;
    else if (kind!=0) asset->scale*=2;
    if (asset->scale<1) asset->scale=1;
    asset->min_y=model.min_y;
    asset->textured=(kind>=4 && texture && texture->data);
    if (asset->textured) {
        if (!texture->width || !texture->height ||
            texture->width>1024 || texture->height>1024 ||
            (texture->channels!=3 && texture->channels!=4) ||
            texture->has_transparency ||
            (uint64_t)texture->width*texture->height*texture->channels>
                texture->data_size) goto done;
        width=texture->width;height=texture->height;
    }
    asset->texture_width=width;asset->texture_height=height;
    texels=malloc((size_t)width*height*sizeof(*texels));
    if (!texels) goto done;
    for(uint32_t i=0;i<width*height;++i) {
        if (!asset->textured) texels[i]=0xffffff;
        else {
            const unsigned char *pixel=texture->data+(size_t)i*texture->channels;
            if (texture->channels==4 && pixel[3]!=255) goto done;
            texels[i]=(uint32_t)pixel[0]<<16|(uint32_t)pixel[1]<<8|pixel[2];
        }
    }
    for(uint32_t p=0;p<model.primitive_count;++p) {
        const unsigned char *primitive=model.primitives+(size_t)p*16;
        uint32_t first=world_u32(primitive),count=world_u32(primitive+4);
        uint32_t material=world_u32(primitive+8);
        if (first%3 || !count || count%3 || count>65536 ||
            first>model.index_count || count>model.index_count-first ||
            material>=model.material_count) goto done;
        vertices=calloc(count,sizeof(*vertices));
        indices=malloc((size_t)count*sizeof(*indices));
        if (!vertices || !indices) goto done;
        for(uint32_t i=0;i<count;i+=3) {
            const unsigned char *source[3];
            for(unsigned j=0;j<3;++j) {
                uint32_t id=world_u32(model.indices+(size_t)(first+i+j)*4);
                if (id>=model.vertex_count) goto done;
                source[j]=model.vertices+(size_t)id*model.vertex_bytes;
            }
            for(unsigned j=0;j<3;++j) {
                struct rf_gpu_graphics_vertex *v=&vertices[i+j];
                for(unsigned axis=0;axis<3;++axis) {
                    v->position[axis]=(int32_t)world_u32(source[j]+axis*4);
                    for(unsigned n=0;n<3;++n)
                        v->normals[n*3+axis]=
                            (int16_t)projectile_u16(source[n]+12+axis*2);
                }
                v->uv[0]=projectile_u16(source[j]+18);
                v->uv[1]=projectile_u16(source[j]+20);
                indices[i+j]=i+j;
            }
        }
        asset->part[p]=rf_gpu_graphics_resource_create(probe->graphics,
            vertices,count,indices,count,texels,width,height);
        if (!asset->part[p]) goto done;
        asset->index_count[p]=count;
        asset->color[p]=world_u32(model.materials+
            (size_t)material*model.material_bytes)&0xffffffu;
        free(vertices);vertices=NULL;free(indices);indices=NULL;
    }
    asset->primitive_count=model.primitive_count;
    result=0;
done:
    free(vertices);free(indices);free(texels);
    rasterfall_model_unload(&model);
    if (result<0)
        fprintf(stderr,"SCENE pickup asset failed kind=%d path=%s\n",kind,paths[kind]);
    return result;
}

static int pickup_model_draws_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_interactable_frame_v1 *pickups,
    const struct toy_texture_view *texture,const struct camera *camera,
    uint32_t width,uint32_t height,struct rf_gpu_graphics_batch_item *items,
    uint32_t capacity,uint32_t *draw_count,uint32_t *item_count,
    uint32_t *deferred)
{
    uint32_t total=0,accepted=0,remaining=0;
    for(uint32_t i=0;i<pickups->count;++i) {
        const struct rf_gpu_scene_interactable_item_v1 *source=&pickups->items[i];
        struct rf_gpu_scene_pickup_asset *asset;
        int kind=pickup_model_kind(source);
        if (source->source_slot!=i || source->scene_light_q8<0 ||
            source->scene_light_q8>256 ||
            (source->highlight_on!=0 && source->highlight_on!=1)) return -1;
        if (kind<0) { remaining++;continue; }
        if (pickup_asset_prepare(probe,kind,texture)<0) return -1;
        asset=&probe->pickup[kind];
        if (asset->primitive_count>capacity-total) return -1;
        for(uint32_t p=0;p<asset->primitive_count;++p) {
            struct rf_gpu_graphics_batch_item *entry=&items[total++];
            struct rf_gpu_graphics_draw *draw=&entry->draw;
            memset(entry,0,sizeof(*entry));
            entry->resource=asset->part[p];
            draw->translation_scale[0]=source->x;
            draw->translation_scale[1]=source->y;
            draw->translation_scale[2]=source->z;
            draw->translation_scale[3]=asset->scale;
            draw->rotation[1]=1024;
            draw->rotation[2]=asset->min_y;
            draw->rotation[3]=1;
            draw->camera[0]=camera->x;draw->camera[1]=camera->y;
            draw->camera[2]=camera->z;
            draw->view[0]=camera->sy;draw->view[1]=camera->cy;
            draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
            draw->projection[0]=(int32_t)width;
            draw->projection[1]=(int32_t)height;
            draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
            draw->material[0]=asset->color[p];
            draw->material[1]=source->scene_light_q8;
            draw->material[2]=asset->textured;
            draw->texture[0]=asset->texture_width;
            draw->texture[1]=asset->texture_height;
            draw->index_count=asset->index_count[p];
            draw->double_sided=1;
            draw->integer_depth=0;
            if (!entry->resource ||
                rf_gpu_graphics_resource_bind(probe->graphics,entry->resource)<0 ||
                rf_gpu_graphics_validate_draw(probe->graphics,draw)<0) {
                fprintf(stderr,"SCENE pickup draw invalid slot=%u kind=%d part=%u\n",
                    source->source_slot,kind,p);
                return -1;
            }
        }
        accepted++;
    }
    *draw_count=total;*item_count=accepted;*deferred=remaining;
    return 0;
}

static struct rf_gpu_graphics_resource *pickup_cylinder_resource(
    struct rf_gpu_graphics *graphics,int radius,int bottom,int top)
{
    static const int cx[8]={1024,724,0,-724,-1024,-724,0,724};
    static const int cz[8]={0,724,1024,724,0,-724,-1024,-724};
    struct rf_gpu_graphics_vertex vertices[17]={{0}};
    uint32_t indices[72],count=0;
    const uint32_t white=0xffffff;
    for(uint32_t i=0;i<8;++i) {
        int x=cx[i]*radius/1024,z=cz[i]*radius/1024;
        vertices[i].position[0]=vertices[i+8].position[0]=x;
        vertices[i].position[1]=bottom;
        vertices[i+8].position[1]=top;
        vertices[i].position[2]=vertices[i+8].position[2]=z;
    }
    vertices[16].position[1]=top;
    for(uint32_t i=0;i<8;++i) {
        uint32_t next=(i+1)&7;
        indices[count++]=i;indices[count++]=next;indices[count++]=next+8;
        indices[count++]=i;indices[count++]=next+8;indices[count++]=i+8;
        indices[count++]=16;indices[count++]=i+8;indices[count++]=next+8;
    }
    return rf_gpu_graphics_resource_create(graphics,vertices,17,indices,72,
        &white,1,1);
}

static int pickup_shape_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    unsigned shape)
{
    static const int bounds[RF_GPU_SCENE_PICKUP_SHAPE_COUNT][6]={
        {-60,60,-40,40,-80,80}, {-60,60,16,40,-80,80},
        {-55,55,-55,55,-45,55}, {55,60,-45,45,-45,45},
        {-60,-55,-45,45,-45,45}, {-45,45,-45,45,55,60},
        {61,62,-22,22,-24,24}, {-62,-61,-22,22,-24,24},
        {-24,24,-22,22,61,62}, {-125,125,0,35,-125,125},
        {-62,62,35,78,-62,62}, {-135,135,0,250,-135,135},
        {-8,8,90,160,-142,-132}, {-42,42,120,135,-142,-132}
    };
    const int *b;
    if (shape>=RF_GPU_SCENE_PICKUP_SHAPE_COUNT) return -1;
    if (probe->pickup_shape[shape]) return 0;
    b=bounds[shape];
    if (shape==10 || shape==11)
        probe->pickup_shape[shape]=pickup_cylinder_resource(probe->graphics,
            b[1],b[2],b[3]);
    else
        probe->pickup_shape[shape]=flag_cube_resource(probe->graphics,
            b[0],b[1],b[2],b[3],b[4],b[5]);
    return probe->pickup_shape[shape] ? 0 : -1;
}

static int pickup_procedural_class(
    const struct rf_gpu_scene_interactable_item_v1 *item,int *special)
{
    switch(item->kind) {
    case TOY_MAP_PICKUP_PILL: return 1;
    case TOY_MAP_PICKUP_SHOP: return 4;
    case TOY_MAP_PICKUP_BUTTON:case TOY_MAP_PICKUP_AIR_BUTTON:
    case TOY_MAP_PICKUP_ALARM_BUTTON:case TOY_MAP_PICKUP_HEAVY_HORDE_BUTTON:
    case TOY_MAP_PICKUP_FAST_HORDE_BUTTON:
    case TOY_MAP_PICKUP_BASE_1_BUTTON:case TOY_MAP_PICKUP_BASE_2_BUTTON:
    case TOY_MAP_PICKUP_WAVE_SKIP_BUTTON:
    case TOY_MAP_PICKUP_WEST_CORRIDOR_BUTTON:
    case TOY_MAP_PICKUP_WEST_CORRIDOR_NO_TANK_BUTTON:
    case TOY_MAP_PICKUP_ENEMY_DEATH_TEST_BUTTON:
        return item->x<-10000 ? 3 : item->x>10000 ? 4 : 2;
    case TOY_MAP_PICKUP_SMOKER_BUTTON:case TOY_MAP_PICKUP_ATTACK_X2_BUTTON:
    case TOY_MAP_PICKUP_POSE_RESET_BUTTON:
    case TOY_MAP_PICKUP_POSE_RIGHT_ARM_BUTTON:
    case TOY_MAP_PICKUP_ANIM_IDLE_BUTTON:case TOY_MAP_PICKUP_GLB_IDLE_BUTTON:
    case TOY_MAP_PICKUP_MONEY_BUTTON:
        *special=0;return 5;
    case TOY_MAP_PICKUP_CHARGER_BUTTON:case TOY_MAP_PICKUP_ATTACK_X3_BUTTON:
    case TOY_MAP_PICKUP_POSE_ARMS_BUTTON:
    case TOY_MAP_PICKUP_ANIM_WALK_BUTTON:case TOY_MAP_PICKUP_GLB_WALK_BUTTON:
    case TOY_MAP_PICKUP_VMD_WALK_BUTTON:
    case TOY_MAP_PICKUP_VMD_MANJUSAKA_BUTTON:
    case TOY_MAP_PICKUP_ANIMATION_COMPOSITION_BUTTON:
    case TOY_MAP_PICKUP_HUMANOID_POSE_DEBUG_BUTTON:
    case TOY_MAP_PICKUP_HUMANOID_ACTIONS_BUTTON:
    case TOY_MAP_PICKUP_CLEAR_HIRED_BUTTON:
        *special=1;return 5;
    case TOY_MAP_PICKUP_TANK_BUTTON:case TOY_MAP_PICKUP_ATTACK_X4_BUTTON:
    case TOY_MAP_PICKUP_POSE_BODY_BUTTON:
    case TOY_MAP_PICKUP_ANIM_JOG_BUTTON:case TOY_MAP_PICKUP_GLB_JOG_BUTTON:
        *special=2;return 5;
    default: return 0;
    }
}

static int pickup_procedural_append(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_graphics_resource *resource,uint32_t index_count,
    const struct rf_gpu_scene_interactable_item_v1 *source,uint32_t color,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *total)
{
    struct rf_gpu_graphics_batch_item *entry;
    struct rf_gpu_graphics_draw *draw;
    if (!resource || *total>=capacity) return -1;
    entry=&items[(*total)++];memset(entry,0,sizeof(*entry));
    entry->resource=resource;draw=&entry->draw;
    draw->translation_scale[0]=source->x;
    draw->translation_scale[1]=source->y;
    draw->translation_scale[2]=source->z;
    draw->translation_scale[3]=1000;
    draw->rotation[1]=1024;
    draw->camera[0]=camera->x;draw->camera[1]=camera->y;
    draw->camera[2]=camera->z;
    draw->view[0]=camera->sy;draw->view[1]=camera->cy;
    draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
    draw->projection[0]=(int32_t)width;
    draw->projection[1]=(int32_t)height;
    draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
    draw->material[0]=color;
    draw->material[1]=source->scene_light_q8;
    draw->texture[0]=draw->texture[1]=1;
    draw->index_count=index_count;
    draw->double_sided=1;draw->integer_depth=0;
    return rf_gpu_graphics_resource_bind(probe->graphics,resource)<0 ||
        rf_gpu_graphics_validate_draw(probe->graphics,draw)<0 ? -1 : 0;
}

static int pickup_procedural_draws_prepare(
    struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_interactable_frame_v1 *pickups,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,
    uint32_t *draw_count,uint32_t *item_count)
{
    uint32_t total=0,accepted=0;
#define PICKUP_SHAPE(shape,color) do { \
    unsigned s=(shape); \
    if (pickup_shape_prepare(probe,s)<0 || \
        pickup_procedural_append(probe,probe->pickup_shape[s], \
            (s==10 || s==11)?72:36,source,(color),camera,width,height, \
            items,capacity,&total)<0) return -1; \
} while(0)
    for(uint32_t i=0;i<pickups->count;++i) {
        const struct rf_gpu_scene_interactable_item_v1 *source=&pickups->items[i];
        int special=0,kind;
        uint32_t tint=source->highlight_on ? 0x383838u : 0;
        if (pickup_model_kind(source)>=0) continue;
        kind=pickup_procedural_class(source,&special);
        if (kind==0) {
            PICKUP_SHAPE(0,0x555F3Fu+tint);
            PICKUP_SHAPE(1,0x6A7550u+tint);
        } else if (kind==1) {
            PICKUP_SHAPE(11,source->highlight_on?0xD8E8D8u:0xB7C7B7u);
            PICKUP_SHAPE(12,0x20B84Bu);
            PICKUP_SHAPE(13,0x20B84Bu);
        } else if (kind>=2 && kind<=4) {
            PICKUP_SHAPE(2,0x2E333Bu+tint);
            PICKUP_SHAPE(kind==3?3:kind==4?4:5,0x1E2229u+tint);
            PICKUP_SHAPE(kind==3?6:kind==4?7:8,0xFF3030u);
        } else {
            struct rf_gpu_graphics_resource *pedestal;
            uint32_t base=special==2?0x65713Du:
                special==1?0x9B5528u:0x3E7462u;
            if (source->source_slot>=TOY_MAP_MAX_PICKUPS) return -1;
            pedestal=probe->pickup_pedestal[source->source_slot];
            if (!pedestal || probe->pickup_pedestal_y[source->source_slot]!=source->y) {
                struct rf_gpu_graphics_resource *next=pickup_cylinder_resource(
                    probe->graphics,190,-900-source->y,0);
                if (!next) return -1;
                if (pedestal && rf_gpu_graphics_resource_destroy(probe->graphics,
                    pedestal)<0) { rf_gpu_graphics_resource_destroy(probe->graphics,next);return -1; }
                pedestal=probe->pickup_pedestal[source->source_slot]=next;
                probe->pickup_pedestal_y[source->source_slot]=source->y;
            }
            if (pickup_procedural_append(probe,pedestal,72,source,base,camera,
                    width,height,items,capacity,&total)<0) return -1;
            PICKUP_SHAPE(9,0x252B31u+tint);
            PICKUP_SHAPE(10,source->highlight_on?0xFFE080u:
                special==2?0x91A54Cu:special==1?0xD43A28u:0x38CFA0u);
        }
        accepted++;
    }
#undef PICKUP_SHAPE
    *draw_count=total;*item_count=accepted;
    return 0;
}
static int world_material(const struct rasterfall_model_asset *model,
    const unsigned char *material)
{
    return model->format_version==9 && model->material_bytes>=32 &&
        material[4]==255 && !material[6] && !(material[7]&~1u) &&
        world_u32(material+8)==UINT32_MAX && !world_u32(material+12) &&
        !world_u32(material+16) && !world_u32(material+20) &&
        (model->material_bytes<36 || !world_u32(material+32)) &&
        (model->material_bytes<40 || !world_u32(material+36));
}

int rf_gpu_scene_world_gpu_prepare(struct rf_gpu_scene_world_resources *owner,
    struct rf_gpu_resource_cache *cache,struct rf_gpu_graphics *graphics,
    const struct camera *camera,uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count)
{
    uint32_t total=0;
    if (!owner || !cache || !graphics || !camera || !items || !count ||
        !owner->world_generation || !owner->map_generation ||
        !owner->registry.frame_active || !width || !height ||
        width>INT_MAX || height>INT_MAX || width>INT_MAX/3) return -1;
    owner->prop_draws=owner->prop_deferred=owner->prop_culled=0;
    owner->prop_numeric_deferred=owner->prop_material_deferred=0;
    owner->prop_transparent_deferred=0;
    for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind) {
        struct rasterfall_resource_handle handle=owner->opaque[kind];
        const struct rasterfall_model_asset *model;
        if (!handle.generation) continue;
        model=rasterfall_resources_resolve_active(&owner->registry,handle);
        if (!model || model->material_bytes<32 ||
            !world_range(model,model->primitives,(uint64_t)model->primitive_count*16) ||
            !world_range(model,model->materials,
                (uint64_t)model->material_count*model->material_bytes) ||
            model->primitive_count>capacity-total ||
            rasterfall_resources_pin(&owner->registry,handle)<0) return -1;
        for(uint32_t p=0;p<model->primitive_count;++p) {
            const unsigned char *primitive=model->primitives+(size_t)p*16;
            uint32_t material_index=world_u32(primitive+8);
            struct rf_gpu_cached_submesh info;
            struct rf_gpu_graphics_batch_item *item;
            struct rf_gpu_graphics_draw *draw;
            const unsigned char *material;
            if (material_index>=model->material_count) return -1;
            material=model->materials+(size_t)material_index*model->material_bytes;
            if (!world_material(model,material) ||
                rf_gpu_resource_cache_prepare(cache,owner->registry.frame_epoch,
                    handle,p,RF_GPU_CACHE_FLAT_TEXTURE,&info)<0 ||
                rf_gpu_resource_cache_bind(cache,owner->registry.frame_epoch,
                    handle,p,RF_GPU_CACHE_FLAT_TEXTURE)<0) return -1;
            item=&items[total++];memset(item,0,sizeof(*item));
            item->resource=rf_gpu_resource_cache_resource(cache,owner->registry.frame_epoch,
                handle,p,RF_GPU_CACHE_FLAT_TEXTURE);
            draw=&item->draw;
            draw->translation_scale[0]=(int32_t)world_u32(material+24);
            draw->translation_scale[1]=model->min_y;
            draw->translation_scale[2]=(int32_t)world_u32(material+28);
            draw->translation_scale[3]=1000;
            draw->rotation[1]=1024;draw->rotation[2]=model->min_y;
            draw->camera[0]=camera->x;draw->camera[1]=camera->y;
            draw->camera[2]=camera->z;
            draw->view[0]=camera->sy;draw->view[1]=camera->cy;
            draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
            draw->projection[0]=(int32_t)width;
            draw->projection[1]=(int32_t)height;
            draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
            draw->material[0]=world_u32(material)&0xffffffu;
            draw->material[1]=256;draw->material[3]=1;
            draw->texture[0]=draw->texture[1]=1;
            /* Generated world meshes carry baked vertex light in UV.x. */
            draw->integer_depth=0;
            draw->index_count=info.index_count;
            draw->double_sided=material[7]&1u;
            if (!item->resource || rf_gpu_graphics_validate_draw(graphics,draw)<0)
                return -1;
        }
    }
    for(uint32_t source=0;source<owner->prop_count;++source) {
        const struct rf_gpu_scene_world_prop_item_v1 *prop=&owner->prop_items[source];
        const struct rasterfall_prop_asset_profile *profile;
        const struct rasterfall_model_asset *model;
        struct rasterfall_resource_handle handle;
        struct rasterfall_draw_instance instance;
        struct rasterfall_draw_view view;
        int asset=prop->prop.asset_id,scale,yaw,integer_depth;
        if (asset==RASTERFALL_PROP_ASSET_BOUNDARY_WALL) continue;
        if (asset<1 || asset>RASTERFALL_PROP_ASSET_COUNT) return -1;
        profile=rasterfall_prop_asset_profile(asset);
        handle=owner->prop_asset[asset];
        model=rasterfall_resources_resolve_active(&owner->registry,handle);
        if (!profile || !model || !handle.generation ||
            !world_range(model,model->primitives,(uint64_t)model->primitive_count*16) ||
            !world_range(model,model->materials,
                (uint64_t)model->material_count*model->material_bytes)) return -1;
        scale=rasterfall_prop_render_scale(profile,prop->prop.scale_milli);
        yaw=prop->prop.yaw_degrees%360;if(yaw<0)yaw+=360;
        memset(&instance,0,sizeof(instance));memset(&view,0,sizeof(view));
        instance.mesh=model;instance.mesh_handle=handle;instance.asset_id=asset;
        instance.x=prop->prop.x;instance.y=-900+prop->prop.y;instance.z=prop->prop.z;
        instance.scale_milli=scale;
        instance.yaw_sin_q10=(int)(sin((double)yaw*3.141592653589793/180.0)*1024.0);
        instance.yaw_cos_q10=(int)(cos((double)yaw*3.141592653589793/180.0)*1024.0);
        instance.scene_light_q8=prop->scene_light_q8;instance.form_lighting=1;
        instance.force_backface_culling=asset>=RASTERFALL_PROP_ASSET_ARCH_BEAM &&
            asset<=RASTERFALL_PROP_ASSET_ARCH_FLOOR_HATCH;
        view.camera=*camera;view.width=(int)width;view.height=(int)height;
        view.near_z=64;view.focal=(int)(width*3/4);
        if (!rasterfall_render_scene_static_prop_visible(&view,&instance)) {
            owner->prop_culled++;continue;
        }
        integer_depth=rasterfall_render_scene_static_prop_eligible(&view,&instance,1);
        if (!integer_depth &&
            !rasterfall_render_scene_static_prop_eligible(&view,&instance,0)) {
            owner->prop_deferred++;owner->prop_numeric_deferred++;continue;
        }
        for(uint32_t p=0;p<model->primitive_count;++p) {
            struct rasterfall_draw_item item;
            enum rasterfall_draw_reject reject=
                rasterfall_render_scene_static_prop_resolve(&instance,p,&item);
            if (reject!=RASTERFALL_DRAW_ACCEPTED) {
                if (reject==RASTERFALL_DRAW_TRANSPARENT)
                    owner->prop_transparent_deferred++;
                else owner->prop_material_deferred++;
                break;
            }
            if (p+1==model->primitive_count) goto prop_ready;
        }
        owner->prop_deferred++;continue;
prop_ready:
        if (model->primitive_count>capacity-total ||
            rasterfall_resources_pin(&owner->registry,handle)<0) return -1;
        for(uint32_t p=0;p<model->primitive_count;++p) {
            struct rasterfall_draw_item resolved;
            struct rf_gpu_cached_submesh info;
            struct rf_gpu_graphics_batch_item *entry;
            struct rf_gpu_graphics_draw *draw;
            uint32_t texture=RF_GPU_CACHE_FLAT_TEXTURE;
            if (rasterfall_render_scene_static_prop_resolve(&instance,p,&resolved)!=
                RASTERFALL_DRAW_ACCEPTED) return -1;
            if (resolved.material.texture) {
                for(uint32_t t=0;t<model->textures.count;++t)
                    if (resolved.material.texture==&model->textures.views[t]) {
                        texture=t;break;
                    }
                if (texture==RF_GPU_CACHE_FLAT_TEXTURE) return -1;
            }
            if (rf_gpu_resource_cache_prepare(cache,owner->registry.frame_epoch,
                    handle,p,texture,&info)<0 ||
                rf_gpu_resource_cache_bind(cache,owner->registry.frame_epoch,
                    handle,p,texture)<0) return -1;
            entry=&items[total++];memset(entry,0,sizeof(*entry));
            entry->resource=rf_gpu_resource_cache_resource(cache,owner->registry.frame_epoch,
                handle,p,texture);
            draw=&entry->draw;
            draw->translation_scale[0]=instance.x;
            draw->translation_scale[1]=instance.y;
            draw->translation_scale[2]=instance.z;
            draw->translation_scale[3]=instance.scale_milli;
            draw->rotation[0]=instance.yaw_sin_q10;
            draw->rotation[1]=instance.yaw_cos_q10;
            draw->rotation[2]=model->min_y;
            draw->rotation[3]=instance.form_lighting;
            draw->camera[0]=camera->x;draw->camera[1]=camera->y;
            draw->camera[2]=camera->z;
            draw->view[0]=camera->sy;draw->view[1]=camera->cy;
            draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
            draw->projection[0]=(int32_t)width;
            draw->projection[1]=(int32_t)height;
            draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
            draw->material[0]=resolved.material.color;
            draw->material[1]=instance.scene_light_q8;
            draw->material[2]=texture!=RF_GPU_CACHE_FLAT_TEXTURE;
            draw->texture[0]=info.texture_width;
            draw->texture[1]=info.texture_height;
            draw->index_count=info.index_count;
            draw->double_sided=resolved.material.double_sided;
            draw->integer_depth=integer_depth;
            if (!entry->resource || rf_gpu_graphics_validate_draw(graphics,draw)<0)
                return -1;
            owner->prop_draws++;
        }
    }
    *count=total;
    return 0;
}

struct scene_enemy_mesh {
    struct rf_gpu_graphics_vertex vertices[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3];
    uint32_t indices[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3];
    uint32_t colors[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES], count;
    unsigned char double_sided[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES];
    const struct rf_gpu_scene_enemy_frame_v1 *frame;
    const struct rf_gpu_scene_enemy_item_v1 *source;
};
static int scene_enemy_triangle(void *context,const struct rf_gpu_scene_enemy_point *a,
    const struct rf_gpu_scene_enemy_point *b,const struct rf_gpu_scene_enemy_point *c,unsigned color)
{
    struct scene_enemy_mesh *mesh=context;
    const struct rf_gpu_scene_enemy_point *points[3]={a,b,c};
    if (mesh->count>=RF_GPU_SCENE_ENEMY_MAX_TRIANGLES) return -1;
    mesh->colors[mesh->count]=color;
    mesh->double_sided[mesh->count]=(unsigned char)a->double_sided;
    for (unsigned k=0;k<3;++k) {
        unsigned index=mesh->count*3+k;
        struct rf_gpu_graphics_vertex *v=&mesh->vertices[index];
        v->position[0]=points[k]->x-mesh->source->x;
        v->position[1]=points[k]->y-mesh->source->lift;
        v->position[2]=points[k]->z-mesh->source->z;
        v->uv[0]=mesh->frame->vertex_lighting ?
            rasterfall_world_light_v2_q8(rasterfall_world_light_at(
                &mesh->frame->lighting,points[k]->x,points[k]->y,points[k]->z)) :
            mesh->source->scene_light_q8;
        if (!mesh->frame->vertex_lighting && points[k]->light_min_q8>0) {
            int light=(int)v->uv[0]*points[k]->form_light_q8/256;
            if (light<points[k]->light_min_q8) light=points[k]->light_min_q8;
            if (light>points[k]->light_max_q8) light=points[k]->light_max_q8;
            v->uv[0]=light;
        }
        mesh->indices[index]=index;
    }
    mesh->count++;return 0;
}
static int enemy_draws_prepare(struct rf_gpu_scene_world_gpu_probe *probe,
    const struct rf_gpu_scene_enemy_frame_v1 *frame,const struct camera *camera,
    uint32_t width,uint32_t height,struct rf_gpu_graphics_batch_item *items,
    uint32_t capacity,uint32_t *count)
{
    struct scene_enemy_mesh *mesh=NULL;
    uint32_t total=0,white=0xffffff;
    int result=-1;
    /* The diagnostic submits and waits synchronously. Keep resources in the
     * owner on any failure; teardown drains before releasing GPU references. */
    for (unsigned i=0;i<TOY_GAME_MAX_ENEMIES;++i) {
        if (probe->enemy[i] &&
            rf_gpu_graphics_resource_destroy(probe->graphics,probe->enemy[i])<0)
            return -1;
        probe->enemy[i]=NULL;
    }
    if (!frame->count) { *count=0;return 0; }
    mesh=calloc(1,sizeof(*mesh));
    if (!mesh) return -1;
    for (unsigned i=0;i<frame->count;++i) {
        const struct rf_gpu_scene_enemy_item_v1 *source=&frame->items[i];
        if (i && source->source_slot<=frame->items[i-1].source_slot) goto done;
        memset(mesh,0,sizeof(*mesh));mesh->frame=frame;mesh->source=source;
        if (rf_gpu_scene_enemy_triangles(source,scene_enemy_triangle,mesh)<0 ||
            !mesh->count || mesh->count>capacity-total) goto done;
        probe->enemy[i]=rf_gpu_graphics_resource_create(probe->graphics,
            mesh->vertices,mesh->count*3,mesh->indices,mesh->count*3,&white,1,1);
        if (!probe->enemy[i]) goto done;
        for (unsigned first=0;first<mesh->count;) {
            unsigned end=first+1;
            struct rf_gpu_graphics_batch_item *entry=&items[total++];
            struct rf_gpu_graphics_draw *draw=&entry->draw;
            while (end<mesh->count && mesh->colors[end]==mesh->colors[first] &&
                mesh->double_sided[end]==mesh->double_sided[first]) end++;
            memset(entry,0,sizeof(*entry));entry->resource=probe->enemy[i];
            draw->translation_scale[0]=source->x;
            draw->translation_scale[1]=source->lift;
            draw->translation_scale[2]=source->z;
            draw->translation_scale[3]=1000;draw->rotation[1]=1024;
            draw->camera[0]=camera->x;draw->camera[1]=camera->y;draw->camera[2]=camera->z;
            draw->view[0]=camera->sy;draw->view[1]=camera->cy;
            draw->view[2]=camera->pitch_sy;draw->view[3]=camera->pitch_cy;
            draw->projection[0]=(int32_t)width;draw->projection[1]=(int32_t)height;
            draw->projection[2]=64;draw->projection[3]=(int32_t)(width*3/4);
            draw->material[0]=mesh->colors[first];
            draw->material[1]=256;draw->material[3]=1;
            draw->double_sided=mesh->double_sided[first];
            draw->texture[0]=draw->texture[1]=1;
            draw->first_index=first*3;draw->index_count=(end-first)*3;
            if (rf_gpu_graphics_resource_bind(probe->graphics,entry->resource)<0 ||
                rf_gpu_graphics_validate_draw(probe->graphics,draw)<0) goto done;
            first=end;
        }
    }
    *count=total;result=0;
done:
    free(mesh);return result;
}

int rf_gpu_scene_world_gpu_probe_frame(struct rf_gpu_scene_world_gpu_probe *probe,
    struct rf_gpu_vulkan_context *context,struct rf_gpu_scene_world_resources *owner,
    const struct camera *camera,uint32_t width,uint32_t height,
    const struct rf_gpu_scene_pose_v1 *poses,uint32_t pose_count,
    const struct rf_gpu_scene_flag_frame_v1 *flags,
    const struct rf_gpu_scene_projectile_frame_v1 *projectiles,
    const struct rf_gpu_scene_interactable_frame_v1 *interactables,
    const struct rf_gpu_scene_enemy_frame_v1 *enemies,
    const struct toy_texture_view *model_texture,
    struct rf_gpu_scene_world_gpu_probe_stats *stats,const char *capture_path)
{
    struct rf_gpu_graphics_batch_item *items=NULL;
    struct rf_gpu_resource_cache_stats before,after;
    uint32_t *color=NULL,capacity=0,draws=0,actor_draws=0,flag_draws=0;
    uint32_t flag_text_draws=0,projectile_draws=0,covered=0;
    uint32_t pickup_model_draws=0,pickup_model_items=0,pickup_procedural_deferred=0;
    uint32_t pickup_procedural_draws=0,pickup_procedural_items=0,enemy_draws=0;
    float *depth=NULL;
    size_t pixels=(size_t)width*height;
    uint32_t actor_prepared=0;
    int frame_active=0,result=-1;
    if (!probe || !context || !owner || !camera || !flags || !projectiles ||
        !interactables || !enemies || enemies->count>TOY_GAME_MAX_ENEMIES ||
        enemies->frame_id!=flags->frame_id ||
        enemies->world_generation!=owner->world_generation ||
        !stats || !width || !height ||
        pose_count>TOY_GAME_MAX_ACTORS || (pose_count && !poses) ||
        flags->count>RF_GPU_SCENE_FLAG_CAP ||
        flags->world_generation!=owner->world_generation ||
        (pose_count && flags->frame_id!=poses[0].frame_id) ||
        projectiles->count>RF_GPU_SCENE_PROJECTILE_CAP ||
        projectiles->world_generation!=owner->world_generation ||
        projectiles->frame_id!=flags->frame_id ||
        interactables->count>TOY_MAP_MAX_PICKUPS ||
        interactables->world_generation!=owner->world_generation ||
        interactables->frame_id!=flags->frame_id ||
        pixels>UINT32_MAX || pixels>SIZE_MAX/sizeof(*color)) return -1;
    for(uint32_t i=0;i<pose_count;++i) {
        if (poses[i].actor_count!=1 ||
            poses[i].world_generation!=owner->world_generation ||
            (i && poses[i].frame_id!=poses[0].frame_id)) return -1;
        for(uint32_t j=0;j<i;++j)
            if (!memcmp(&poses[i].identity,&poses[j].identity,
                    sizeof(poses[i].identity))) return -1;
    }
    memset(stats,0,sizeof(*stats));
    if (!probe->graphics) {
        probe->graphics=rf_gpu_graphics_create(context);
        if (!probe->graphics) return -1;
        probe->cache=rf_gpu_resource_cache_create(probe->graphics,&owner->registry);
        if (!probe->cache) return -1;
    }
    if (!probe->cache || rf_gpu_graphics_resize(probe->graphics,width,height)<0)
        return -1;
    if (!probe->flag_pole) {
        probe->flag_pole=flag_cube_resource(probe->graphics,-16,16,-900,2700,-16,16);
        if (!probe->flag_pole) return -1;
    }
    if (!probe->flag_cloth) {
        probe->flag_cloth=flag_cube_resource(probe->graphics,12,700,1950,2450,-10,10);
        if (!probe->flag_cloth) return -1;
    }
    if (flag_label_resources_prepare(probe,flags)<0) return -1;
    if (projectiles->count && projectile_assets_prepare(probe,model_texture)<0)
        return -1;
    rf_gpu_resource_cache_collect(probe->cache);
    rf_gpu_resource_cache_get_stats(probe->cache,&before);
    for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind) {
        struct rasterfall_resource_handle handle=owner->opaque[kind];
        const struct rasterfall_model_asset *model;
        if (!handle.generation) continue;
        model=rasterfall_resources_resolve_active(&owner->registry,handle);
        if (!model || model->primitive_count>UINT32_MAX-capacity) return -1;
        capacity+=model->primitive_count;
    }
    for(uint32_t source=0;source<owner->prop_count;++source) {
        int asset=owner->prop_items[source].prop.asset_id;
        const struct rasterfall_model_asset *model;
        if (asset==RASTERFALL_PROP_ASSET_BOUNDARY_WALL) continue;
        if (asset<1 || asset>RASTERFALL_PROP_ASSET_COUNT) return -1;
        model=rasterfall_resources_resolve_active(&owner->registry,owner->prop_asset[asset]);
        if (!model || model->primitive_count>UINT32_MAX-capacity) return -1;
        capacity+=model->primitive_count;
    }
    if (pose_count) {
        if (pose_count>(UINT32_MAX-capacity)/RF_GPU_SCENE_ACTOR_MAX_DRAWS) return -1;
        capacity+=pose_count*RF_GPU_SCENE_ACTOR_MAX_DRAWS;
        for(uint32_t i=0;i<pose_count;++i) {
            if (!probe->actor[i]) {
                probe->actor[i]=rf_gpu_scene_actor_gpu_create(probe->graphics);
                if (!probe->actor[i]) return -1;
            }
        }
    }
    if (flags->count>(UINT32_MAX-capacity)/3) return -1;
    capacity+=flags->count*3;
    if (projectiles->count>UINT32_MAX-capacity) return -1;
    capacity+=projectiles->count;
    if (interactables->count>(UINT32_MAX-capacity)/RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES)
        return -1;
    capacity+=interactables->count*RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES;
    if (enemies->count>(UINT32_MAX-capacity)/RF_GPU_SCENE_ENEMY_MAX_TRIANGLES)
        return -1;
    capacity+=enemies->count*RF_GPU_SCENE_ENEMY_MAX_TRIANGLES;
    if (!capacity) return 0;
    items=calloc(capacity ? capacity : 1,sizeof(*items));
    color=calloc(pixels,sizeof(*color));
    depth=calloc(pixels,sizeof(*depth));
    if (!items || !color || !depth ||
        rasterfall_resources_frame_begin(&owner->registry)<0) goto done;
    frame_active=1;
    if (rf_gpu_scene_world_gpu_prepare(owner,probe->cache,probe->graphics,
            camera,width,height,items,capacity,&draws)<0) goto done;
    for(uint32_t i=0;i<pose_count;++i) {
        uint32_t count=0;
        if (rf_gpu_scene_actor_gpu_prepare(probe->actor[i],&poses[i],camera,width,height,
                items+draws,capacity-draws,&count)<0) goto done;
        actor_prepared++;
        draws+=count;actor_draws+=count;
    }
    if (flag_draws_prepare(probe,flags,camera,width,height,items+draws,
            capacity-draws,&flag_draws,&flag_text_draws)<0) {
        fprintf(stderr,"SCENE flag preparation failed count=%u\n",flags->count);
        goto done;
    }
    draws+=flag_draws;
    if (projectile_draws_prepare(probe,projectiles,model_texture,camera,
            width,height,items+draws,capacity-draws,&projectile_draws)<0)
        goto done;
    draws+=projectile_draws;
    if (pickup_model_draws_prepare(probe,interactables,model_texture,camera,
            width,height,items+draws,capacity-draws,&pickup_model_draws,
            &pickup_model_items,&pickup_procedural_deferred)<0) goto done;
    draws+=pickup_model_draws;
    if (pickup_procedural_draws_prepare(probe,interactables,camera,
            width,height,items+draws,capacity-draws,&pickup_procedural_draws,
            &pickup_procedural_items)<0 ||
        pickup_procedural_items!=pickup_procedural_deferred) goto done;
    draws+=pickup_procedural_draws;
    pickup_procedural_deferred=0;
    if (enemy_draws_prepare(probe,enemies,camera,width,height,
            items+draws,capacity-draws,&enemy_draws)<0) goto done;
    draws+=enemy_draws;
    if (rf_gpu_graphics_scene_capture(probe->graphics,items,draws,
            color,depth,(uint32_t)pixels)<0) goto done;
    if (capture_path) {
        size_t path_size=strlen(capture_path)+sizeof(".scene.ppm");
        char *path=malloc(path_size);
        FILE *file;
        if (!path) goto done;
        snprintf(path,path_size,"%s.scene.ppm",capture_path);
        file=fopen(path,"wb");
        free(path);
        if (!file) goto done;
        if (fprintf(file,"P6\n%u %u\n255\n",width,height)<0) {
            fclose(file);goto done;
        }
        for(size_t i=0;i<pixels;++i) {
            unsigned char rgb[3]={(unsigned char)color[i],
                (unsigned char)(color[i]>>8),(unsigned char)(color[i]>>16)};
            if (fwrite(rgb,1,3,file)!=3) { fclose(file);goto done; }
        }
        if (fclose(file)) goto done;
    }
    for(size_t i=0;i<pixels;++i)
        if (depth[i]>0 && color[i]) covered++;
    rf_gpu_resource_cache_get_stats(probe->cache,&after);
    stats->enemy_draws=enemy_draws;stats->enemy_items=enemies->count;
    stats->enemy_deferred=enemies->deferred;stats->enemy_culled=enemies->culled;
    stats->draws=draws;stats->actor_draws=actor_draws;
    stats->flag_draws=flag_draws;stats->flag_text_draws=flag_text_draws;
    stats->projectile_draws=projectile_draws;
    stats->pickup_model_draws=pickup_model_draws;
    stats->pickup_model_items=pickup_model_items;
    stats->pickup_procedural_draws=pickup_procedural_draws;
    stats->pickup_procedural_items=pickup_procedural_items;
    stats->pickup_procedural_deferred=pickup_procedural_deferred;
    stats->covered_pixels=covered;
    stats->prop_draws=owner->prop_draws;stats->prop_deferred=owner->prop_deferred;
    stats->prop_culled=owner->prop_culled;
    stats->prop_numeric_deferred=owner->prop_numeric_deferred;
    stats->prop_material_deferred=owner->prop_material_deferred;
    stats->prop_transparent_deferred=owner->prop_transparent_deferred;
    stats->uploads=after.uploads-before.uploads;
    stats->hits=after.hits-before.hits;
    result=0;
done:
    for(uint32_t i=0;i<actor_prepared;++i)
        rf_gpu_scene_actor_gpu_finish(probe->actor[i]);
    if (frame_active) rasterfall_resources_frame_complete(&owner->registry);
    free(items);free(color);free(depth);
    return result;
}

void rf_gpu_scene_world_gpu_probe_close(struct rf_gpu_scene_world_gpu_probe *probe)
{
    if (!probe) return;
    for (unsigned i=0;i<TOY_GAME_MAX_ENEMIES;++i)
        if (probe->enemy[i])
            rf_gpu_graphics_resource_destroy(probe->graphics,probe->enemy[i]);
    if (probe->flag_pole)
        rf_gpu_graphics_resource_destroy(probe->graphics,probe->flag_pole);
    if (probe->flag_cloth)
        rf_gpu_graphics_resource_destroy(probe->graphics,probe->flag_cloth);
    for(uint32_t i=0;i<RF_GPU_SCENE_FLAG_CAP;++i)
        if (probe->flag_label[i])
            rf_gpu_graphics_resource_destroy(probe->graphics,probe->flag_label[i]);
    for(int i=0;i<2;++i)
        if (probe->projectile_asset[i])
            rf_gpu_graphics_resource_destroy(probe->graphics,probe->projectile_asset[i]);
    for(int kind=0;kind<RF_GPU_SCENE_PICKUP_MODEL_COUNT;++kind)
        for(uint32_t p=0;p<RF_GPU_SCENE_PICKUP_MAX_PRIMITIVES;++p)
            if (probe->pickup[kind].part[p])
                rf_gpu_graphics_resource_destroy(probe->graphics,
                    probe->pickup[kind].part[p]);
    for(uint32_t s=0;s<RF_GPU_SCENE_PICKUP_SHAPE_COUNT;++s)
        if (probe->pickup_shape[s])
            rf_gpu_graphics_resource_destroy(probe->graphics,probe->pickup_shape[s]);
    for(uint32_t i=0;i<TOY_MAP_MAX_PICKUPS;++i)
        if (probe->pickup_pedestal[i])
            rf_gpu_graphics_resource_destroy(probe->graphics,
                probe->pickup_pedestal[i]);
    for(uint32_t i=0;i<TOY_GAME_MAX_ACTORS;++i)
        if (probe->actor[i]) rf_gpu_scene_actor_gpu_destroy(probe->actor[i]);
    if (probe->cache) rf_gpu_resource_cache_destroy(probe->cache);
    if (probe->graphics) rf_gpu_graphics_destroy(probe->graphics);
    memset(probe->actor,0,sizeof(probe->actor));
    probe->cache=NULL;probe->graphics=NULL;
    probe->flag_pole=probe->flag_cloth=NULL;
    memset(probe->flag_label,0,sizeof(probe->flag_label));
    memset(probe->flag_label_text,0,sizeof(probe->flag_label_text));
    memset(probe->flag_label_indices,0,sizeof(probe->flag_label_indices));
    memset(probe->projectile_asset,0,sizeof(probe->projectile_asset));
    memset(probe->projectile_indices,0,sizeof(probe->projectile_indices));
    memset(probe->projectile_color,0,sizeof(probe->projectile_color));
    memset(probe->projectile_scale,0,sizeof(probe->projectile_scale));
    probe->projectile_texture_width=probe->projectile_texture_height=0;
    memset(probe->pickup,0,sizeof(probe->pickup));
    memset(probe->pickup_shape,0,sizeof(probe->pickup_shape));
    memset(probe->pickup_pedestal,0,sizeof(probe->pickup_pedestal));
    memset(probe->pickup_pedestal_y,0,sizeof(probe->pickup_pedestal_y));
}
#endif
