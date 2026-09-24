/* Explicit Windows Scene fixture. Normal runtime never enters this executor. */
#include "tlibc_everything.h"
#include "rf_gpu_scene_pose.h"
#include "rf_gpu_scene_actor_gpu.h"
#include "rf_gpu_scene_world.h"
#include "rf_gpu_scene_world_gpu.h"
#ifndef TOYC_WINDOWS
int rf_gpu_scene_native_fixture(int frames,int fault,int fault_frame)
{ (void)frames; (void)fault; (void)fault_frame; return 3; }
struct rf_gpu_scene_actor_gpu *rf_gpu_scene_actor_gpu_create(
    struct rf_gpu_graphics *graphics)
{ (void)graphics; return NULL; }
int rf_gpu_scene_actor_gpu_prepare(struct rf_gpu_scene_actor_gpu *actor,
    const struct rf_gpu_scene_pose_v1 *pose,const struct camera *camera,
    uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count)
{
    (void)actor;(void)pose;(void)camera;(void)width;(void)height;
    (void)items;(void)capacity;(void)count;return -1;
}
void rf_gpu_scene_actor_gpu_finish(struct rf_gpu_scene_actor_gpu *actor)
{ (void)actor; }
void rf_gpu_scene_actor_gpu_destroy(struct rf_gpu_scene_actor_gpu *actor)
{ (void)actor; }
#else
#include "tlibc_everything.h"
#include "rf_gpu_graphics.h"
#include "rf_gpu_resource_cache.h"
#include "rasterfall_render_resources.h"
#include "rasterfall_render.h"
#include "rasterfall_calibration.h"
#include "toy_window.h"
#include "rasterfall_units.h"
#include "rf_core_host.h"
#include <math.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__declspec(dllimport) int __stdcall SetWindowPos(void *, void *, int, int, int, int, unsigned int);
#define SCENE_DRAWS RF_GPU_SCENE_ACTOR_MAX_DRAWS
static int scene_round(double v) { return (int)(v<0 ? v-0.5 : v+0.5); }
struct scene_mesh {
    struct rasterfall_resource_handle handle;
    uint32_t *bind, *palette, *indices;
    struct rf_gpu_graphics_vertex *vertices;
    uint32_t count, palette_count, vertex_capacity, palette_capacity;
    struct rf_gpu_graphics_resource *gpu;
};
struct scene_slot {
    struct rasterfall_resource_registry registry;
    struct scene_mesh mesh[3+RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS];
    struct rf_gpu_graphics_batch_item draws[SCENE_DRAWS];
    uint32_t draw_count, mesh_count;
    int pinned, submitted;
};
struct rf_gpu_scene_actor_gpu {
    struct scene_slot slot;
    struct rf_gpu_graphics *graphics;
    int frame_active;
};
static uint32_t scene_u32(const unsigned char *p)
{ return (uint32_t)p[0]|(uint32_t)p[1]<<8|(uint32_t)p[2]<<16|(uint32_t)p[3]<<24; }
static uint32_t scene_u16(const unsigned char *p)
{ return p[0]|(uint32_t)p[1]<<8; }
static uint32_t scene_float(double v)
{ float f=(float)v; uint32_t u; memcpy(&u,&f,4); return u; }
static int scene_range(const struct rasterfall_model_asset *m,const void *ptr,uint64_t size)
{
    uintptr_t base=(uintptr_t)m->data,p=(uintptr_t)ptr;
    return ptr && p>=base && p-base<=(uint64_t)m->data_size && size<=(uint64_t)m->data_size-(p-base);
}
static void scene_backing_free(struct scene_mesh *m)
{
    free(m->bind); free(m->palette); free(m->indices); free(m->vertices);
    m->bind=m->palette=m->indices=NULL; m->vertices=NULL;
    m->vertex_capacity=m->palette_capacity=0;
}
/* The caller only packs a retired slot. Grow each CPU upload array without
 * invalidating the other arrays if allocation fails. */
static int scene_backing_reserve(struct scene_mesh *m,uint32_t count,uint32_t palette_count,int skinned)
{
    if (count>m->vertex_capacity || (skinned && !m->bind)) {
        uint32_t *indices=malloc((size_t)count*sizeof(*indices));
        struct rf_gpu_graphics_vertex *vertices=malloc((size_t)count*sizeof(*vertices));
        uint32_t *bind=skinned ? malloc((size_t)count*22*sizeof(*bind)) : NULL;
        if (!indices || !vertices || (skinned && !bind)) {
            free(indices);free(vertices);free(bind);return -1;
        }
        if (m->count && m->indices && m->vertices) {
            memcpy(indices,m->indices,(size_t)m->count*sizeof(*indices));
            memcpy(vertices,m->vertices,(size_t)m->count*sizeof(*vertices));
            if (skinned && m->bind)
                memcpy(bind,m->bind,(size_t)m->count*22*sizeof(*bind));
        }
        free(m->indices);free(m->vertices);free(m->bind);
        m->indices=indices;m->vertices=vertices;m->bind=bind;
        m->vertex_capacity=count;
    }
    if (skinned && palette_count>m->palette_capacity) {
        uint32_t *palette=malloc((size_t)palette_count*sizeof(*palette));
        if (!palette) return -1;
        free(m->palette);m->palette=palette;m->palette_capacity=palette_count;
    }
    return 0;
}
static void scene_device_free(struct scene_slot *slot,struct rf_gpu_graphics *g)
{
    for (uint32_t i=0;i<slot->mesh_count;++i) {
        if (slot->mesh[i].gpu) rf_gpu_graphics_resource_destroy(g,slot->mesh[i].gpu);
        slot->mesh[i].gpu=NULL;
    }
}
static int scene_load(struct scene_slot *slot,const struct rf_gpu_scene_pose_v1 *pose,
    int include_map)
{
    char path[256];
    struct rasterfall_model_asset *map;
    uint32_t gear_count=include_map ? 1 : pose->attachment_count;
    const char *name;
    const struct rasterfall_character_visual_recipe *recipe=
        rasterfall_character_visual_recipe_for_character(pose->character_id);
    if (pose->body_resource_id!=RASTERFALL_BODY_RF_HUMANOID_V2 || !gear_count ||
        gear_count>RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS ||
        !recipe || (!include_map && gear_count!=recipe->attachment_count) ||
        pose->shirt_color!=recipe->shirt_color ||
        pose->pants_color!=recipe->pants_color ||
        pose->attachments[0].resource_id!=(uint32_t)recipe->attachments[0].gear_resource_id) return -1;
    if (!include_map)
        for(uint32_t i=0;i<gear_count;++i)
            if (pose->attachments[i].resource_id!=(uint32_t)recipe->attachments[i].gear_resource_id ||
                pose->attachments[i].host_socket!=recipe->attachments[i].host_socket)
                return -1;
    slot->mesh_count=2+gear_count+(!include_map && pose->weapon_valid);
    if (include_map && !slot->mesh[0].handle.generation) {
        map=rf_gpu_scene_fixture_map();
        if (!map) return -1;
        if (rasterfall_resources_adopt(&slot->registry,"scene:opaque_box",map,&slot->mesh[0].handle)<0) {
            rasterfall_model_unload(map); tlibc_free(map); return -1;
        }
    }
    for (uint32_t i=1;i<slot->mesh_count;++i) {
        if (i==2+gear_count) {
            const struct rasterfall_weapon_asset_profile *profile;
            if (pose->weapon<0 || pose->weapon>=TOY_GAME_WEAPON_COUNT ||
                !(profile=rasterfall_weapon_asset_profile(pose->weapon)) ||
                !profile->skeletal || !profile->model_path ||
                rasterfall_resources_load(&slot->registry,profile->model_path,
                    &slot->mesh[i].handle)<0) return -1;
            continue;
        }
        name=i==1 ? rasterfall_character_body_resource_name(pose->body_resource_id) :
            rasterfall_character_gear_resource_name(pose->attachments[i-2].resource_id);
        if (!name || snprintf(path,sizeof(path),"rasterfall/private-assets/models/%s.rmesh",name)>=(int)sizeof(path) ||
            rasterfall_resources_load(&slot->registry,path,&slot->mesh[i].handle)<0) return -1;
    }
    return 0;
}
static int scene_material_supported(const struct rasterfall_model_asset *m,
    const unsigned char *material,int object)
{
    uint32_t texture=scene_u32(material+8);
    return (texture>=m->textures.count || !m->textures.assets ||
        !m->textures.assets[texture].data) && !scene_u32(material+12) &&
        (m->format_version<4 || material[4]==255) &&
        (m->format_version<5 || !material[6]) &&
        (m->format_version<7 || !(material[7]&~1u)) &&
        (m->material_bytes<24 || (!scene_u32(material+16)&&!scene_u32(material+20))) &&
        (!object || m->material_bytes<40 || (!scene_u32(material+24)&&!scene_u32(material+28)&&!scene_u32(material+32)));
}
/* Whole-frame validation and CPU packing precede pin and every target write.
 * Palette/rigid transforms are values, never mutable instance pointers. */
static int scene_pack(struct scene_slot *slot,const struct rf_gpu_scene_pose_v1 *pose,
    const struct camera *camera,int w,int h,int include_map)
{
    if (!camera || pose->abi_version!=1 || pose->byte_size!=sizeof(*pose) || pose->actor_count!=1 ||
        !pose->frame_id || !pose->world_generation || !pose->bone_count ||
        pose->bone_count>RF_GPU_SCENE_POSE_BONES || pose->bind_normals>1 ||
        pose->scene_light_q8<0 || pose->scene_light_q8>384) return -1;
    slot->draw_count=0;
    for (uint32_t object=include_map ? 0 : 1;object<slot->mesh_count;++object) {
        struct scene_mesh *out=&slot->mesh[object];
        const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(&slot->registry,out->handle);
        int weapon_object=!include_map && object==2+pose->attachment_count;
        const struct rasterfall_rigid_transform *transform=!object ? NULL :
            object==1 ? &pose->body_to_world :
            weapon_object ? &pose->weapon_to_world :
            &pose->attachments[object-2].model_to_world;
        if (!m || !m->index_count || m->index_count>65536 || m->index_count%3 || m->vertex_bytes<24 ||
            !scene_range(m,m->vertices,(uint64_t)m->vertex_count*m->vertex_bytes) ||
            !scene_range(m,m->indices,(uint64_t)m->index_count*4) ||
            !scene_range(m,m->primitives,(uint64_t)m->primitive_count*16) ||
            !scene_range(m,m->materials,(uint64_t)m->material_count*m->material_bytes) || m->material_bytes<16 ||
            (object==1 && (m->bone_count!=pose->bone_count ||
                !scene_range(m,m->skin_vertices,(uint64_t)m->vertex_count*8))) ||
            (object!=1 && m->bone_count)) return -1;
        if (object) {
            out->palette_count=object==1 ? pose->bone_count*15 : 15;
            if (scene_backing_reserve(out,m->index_count,out->palette_count,1)<0) return -1;
            if (!m->position_scale || transform->scale_milli<1 || transform->scale_milli>8000) return -1;
            for(int k=0;k<9;++k) if (!__builtin_isfinite(transform->rotation[k]) || fabs(transform->rotation[k])>1.01) return -1;
            for(int k=0;k<3;++k) if (!__builtin_isfinite(transform->translation[k]) || fabs(transform->translation[k])>262144) return -1;
            for(uint32_t bone=0;bone<out->palette_count/15;++bone) {
                const struct rasterfall_model_skin_palette_bone *p=&pose->palette[bone];
                uint32_t *dst=out->palette+bone*15;
                for(int k=0;k<9;++k) {
                    double v=object==1 ? p->rotation[k] : transform->rotation[k]*
                        (weapon_object ? transform->scale_milli/1000.0 :
                        (((int64_t)RASTERFALL_RFU_PER_METER*transform->scale_milli+
                            m->position_scale/2)/m->position_scale)/1000.0);
                    if (!__builtin_isfinite(v) || fabs(v)>8) return -1;
                    dst[k]=scene_float(v);
                }
                for(int k=0;k<3;++k) {
                    double v=object==1 ? p->position[k] : transform->translation[k];
                    if (!__builtin_isfinite(v) || fabs(v)>262144) return -1;
                    dst[9+k]=scene_float(v); dst[12+k]=object==1 ? (uint32_t)p->rest[k] : 0;
                }
            }
        } else if (scene_backing_reserve(out,m->index_count,0,0)<0) return -1;
        out->count=m->index_count;
        for(uint32_t v=0;v<out->count;++v) {
            struct rf_gpu_graphics_vertex *dst=&out->vertices[v];
            uint32_t id=scene_u32(m->indices+v*4);
            if (id>=m->vertex_count) return -1;
            const unsigned char *raw=m->vertices+(size_t)id*m->vertex_bytes;
            memcpy(dst->position,raw,12);
            dst->uv[0]=scene_u16(raw+18); dst->uv[1]=scene_u16(raw+20);
            out->indices[v]=v;
            for(int corner=0;corner<3;++corner) {
                uint32_t ci=scene_u32(m->indices+(v/3*3+corner)*4);
                if (ci>=m->vertex_count) return -1;
                const unsigned char *cr=m->vertices+(size_t)ci*m->vertex_bytes;
                for(int axis=0;axis<3;++axis) dst->normals[corner*3+axis]=(int16_t)scene_u16(cr+12+axis*2);
            }
            if (object) {
                uint32_t *packed=out->bind+v*22;
                memcpy(packed,dst,56);
                for(int influence=0;influence<4;++influence) {
                    uint32_t ci=influence ? scene_u32(m->indices+(v/3*3+influence-1)*4) : id;
                    uint32_t b0=0,b1=0,weight=65535,type=0;
                    if (object==1) {
                        const unsigned char *skin=m->skin_vertices+ci*8;
                        b0=scene_u16(skin); b1=scene_u16(skin+2); weight=scene_u16(skin+4); type=skin[6];
                        if (b0>=pose->bone_count || (type && b1>=pose->bone_count) || type>1) return -1;
                        if (!type) b1=b0;
                    }
                    packed[14+influence*2]=b0|(b1<<16);
                    packed[15+influence*2]=weight|((type|(object==1 && pose->bind_normals ? 0x100u : 0))<<16);
                }
            }
        }
        for(uint32_t primitive=0;primitive<m->primitive_count;++primitive) {
            const unsigned char *p=m->primitives+primitive*16;
            uint32_t first=scene_u32(p),count=scene_u32(p+4),mi=scene_u32(p+8);
            if (!count || count%3 || first%3 || first>out->count || count>out->count-first || mi>=m->material_count || slot->draw_count>=SCENE_DRAWS) return -1;
            const unsigned char *material=m->materials+mi*m->material_bytes;
            /* Frozen opaque flat materials only. Version 2 has no alpha/toon flags. */
            if (!scene_material_supported(m,material,object)) return -1;
            struct rf_gpu_graphics_batch_item *item=&slot->draws[slot->draw_count++];
            memset(item,0,sizeof(*item));
            struct rf_gpu_graphics_draw *d=&item->draw;
            d->translation_scale[3]=1000; d->rotation[1]=1024;
            if (object==1) {
                /* Frozen actor has yaw only; rigid gear consumes full matrix in compute. */
                if (fabs(transform->rotation[1])+fabs(transform->rotation[3])+fabs(transform->rotation[5])+fabs(transform->rotation[7])>0.00001 || fabs(transform->rotation[4]-1)>0.00001) return -1;
                for(int k=0;k<3;++k) d->translation_scale[k]=(int)scene_round(transform->translation[k]);
                d->translation_scale[3]=transform->scale_milli;
                d->rotation[0]=(int)scene_round(transform->rotation[2]*1024);
                d->rotation[1]=(int)scene_round(transform->rotation[0]*1024);
            } else if (!object) {
                d->translation_scale[0]=(int32_t)scene_u32(material+24);
                d->translation_scale[2]=(int32_t)scene_u32(material+28);
            }
            d->camera[0]=camera->x;d->camera[1]=camera->y;
            d->camera[2]=camera->z;
            d->view[0]=camera->sy;d->view[1]=camera->cy;
            d->view[2]=camera->pitch_sy;d->view[3]=camera->pitch_cy;
            d->projection[0]=w;d->projection[1]=h;d->projection[2]=64;d->projection[3]=w*3/4;
            d->material[0]=scene_u32(material)&0xffffff;
            d->material[1]=object ? (uint32_t)pose->scene_light_q8 : 256;
            if (object==1) {
                if (mi==0) d->material[0]=pose->pants_color;
                if (mi==1) d->material[0]=pose->shirt_color;
            }
            d->rotation[3]=object!=0; d->texture[0]=d->texture[1]=1;
            d->first_index=first;d->index_count=count; d->double_sided=m->format_version>=7 ? material[7]&1u : 1u;
            /* Until device preparation, resource index is held by draw grouping. */
        }
    }
    return 0;
}
static int scene_prepare(struct scene_slot *slot,struct rf_gpu_graphics *g,
    int include_map)
{
    uint32_t white=0xffffff,draw=0;
    if (rasterfall_resources_frame_begin(&slot->registry)<0) return -1;
    slot->pinned=1;
    for(uint32_t i=include_map ? 0 : 1;i<slot->mesh_count;++i)
        if (rasterfall_resources_pin(&slot->registry,slot->mesh[i].handle)<0) return -1;
    for(uint32_t i=include_map ? 0 : 1;i<slot->mesh_count;++i) {
        struct scene_mesh *m=&slot->mesh[i];
        const struct rasterfall_model_asset *asset=rasterfall_resources_resolve(&slot->registry,m->handle);
        if (i) {
            int grow=m->gpu ? rf_gpu_graphics_skinned_resource_update(g,m->gpu,m->count,
                m->bind,m->count*22,m->palette,m->palette_count) : 1;
            if (grow<0) return -1;
            if (grow) {
                if (m->gpu) {
                    if (rf_gpu_graphics_resource_destroy(g,m->gpu)<0) return -1;
                    __printf("SCENE resource-growth object=%d vertices=%u retired_before_replace=1\n",i,m->count);
                }
                m->gpu=rf_gpu_graphics_skinned_resource_create(g,NULL,m->count,m->indices,m->count,
                    m->bind,m->count*22,m->palette,m->palette_count,&white,1,1);
            }
        } else if (!m->gpu) m->gpu=rf_gpu_graphics_resource_create(g,m->vertices,m->count,m->indices,m->count,&white,1,1);
        if (!m->gpu || !asset || rf_gpu_graphics_resource_bind(g,m->gpu)<0) return -1;
        for(uint32_t p=0;p<asset->primitive_count;++p) {
            slot->draws[draw].resource=m->gpu;
            if (rf_gpu_graphics_validate_draw(g,&slot->draws[draw].draw)<0) return -1;
            draw++;
        }
    }
    return 0;
}
struct rf_gpu_scene_actor_gpu *rf_gpu_scene_actor_gpu_create(
    struct rf_gpu_graphics *graphics)
{
    struct rf_gpu_scene_actor_gpu *actor;
    if (!graphics) return NULL;
    actor=calloc(1,sizeof(*actor));
    if (actor) actor->graphics=graphics;
    return actor;
}
int rf_gpu_scene_actor_gpu_prepare(struct rf_gpu_scene_actor_gpu *actor,
    const struct rf_gpu_scene_pose_v1 *pose,const struct camera *camera,
    uint32_t width,uint32_t height,
    struct rf_gpu_graphics_batch_item *items,uint32_t capacity,uint32_t *count)
{
    if (!actor || !pose || !camera || !items || !count || actor->frame_active ||
        !width || !height || width>INT_MAX || height>INT_MAX) return -1;
    *count=0;
    if (scene_load(&actor->slot,pose,0)<0 ||
        scene_pack(&actor->slot,pose,camera,(int)width,(int)height,0)<0 ||
        actor->slot.draw_count>capacity) return -1;
    if (scene_prepare(&actor->slot,actor->graphics,0)<0) {
        rasterfall_resources_frame_complete(&actor->slot.registry);
        actor->slot.pinned=0;
        return -1;
    }
    actor->frame_active=1;
    memcpy(items,actor->slot.draws,
        actor->slot.draw_count*sizeof(*items));
    *count=actor->slot.draw_count;
    return 0;
}
void rf_gpu_scene_actor_gpu_finish(struct rf_gpu_scene_actor_gpu *actor)
{
    if (actor && actor->frame_active) {
        rasterfall_resources_frame_complete(&actor->slot.registry);
        actor->slot.pinned=actor->frame_active=0;
    }
}
void rf_gpu_scene_actor_gpu_destroy(struct rf_gpu_scene_actor_gpu *actor)
{
    if (!actor) return;
    rf_gpu_scene_actor_gpu_finish(actor);
    scene_device_free(&actor->slot,actor->graphics);
    rasterfall_resources_invalidate(&actor->slot.registry);
    for(uint32_t i=0;i<3+RASTERFALL_CHARACTER_RECIPE_ATTACHMENTS;++i)
        scene_backing_free(&actor->slot.mesh[i]);
    free(actor);
}
static int scene_grow(struct scene_mesh *m)
{
    uint32_t count=m->count;
    if (count>UINT32_MAX/2) return -1;
    if (scene_backing_reserve(m,count*2,m->palette_count,1)<0) return -1;
    memcpy(m->bind+count*22,m->bind,(size_t)count*22*sizeof(*m->bind));
    for(uint32_t i=0;i<count*2;++i) m->indices[i]=i;
    m->count=count*2;
    return 0;
}
static int scene_skin_diff(struct scene_slot *slot,struct rf_gpu_graphics *g,
    const struct rf_gpu_scene_pose_v1 *pose)
{
    struct scene_mesh *out=&slot->mesh[1];
    const struct rasterfall_model_asset *m=rasterfall_resources_resolve(&slot->registry,out->handle);
    uint64_t pm,nm,uv;uint32_t pd,nd;
    if (!m) return -1;
    for(uint32_t v=0;v<out->count;++v) {
        struct rf_gpu_graphics_vertex *dst=&out->vertices[v];
        uint32_t id=scene_u32(m->indices+v*4);
        if (rasterfall_model_skin_vertex_palette(m,pose->palette,pose->bone_count,id,dst->position,dst->normals)<0) return -1;
        for(int corner=0;corner<3;++corner) {
            int unused[3];uint32_t ci=scene_u32(m->indices+(v/3*3+corner)*4);
            if (rasterfall_model_skin_vertex_palette(m,pose->palette,pose->bone_count,ci,unused,dst->normals+corner*3)<0) return -1;
        }
    }
    if (rf_gpu_graphics_resource_diff_vertices(g,out->gpu,out->vertices,out->count,&pm,&nm,&uv,&pd,&nd)<0) return -1;
    __printf("SCENE vertex-diff position=%llu normal=%llu uv=%llu max_position=%u max_normal=%u diagnostic_readback=1\n",
        (unsigned long long)pm,(unsigned long long)nm,(unsigned long long)uv,pd,nd);
    /* All attributes must match for this frozen fixture. */
    return pm || nm || uv ? -1 : 0;
}
/* Independent per-object depth oracle: composite must select nearest depth
 * and exactly the corresponding color, with all three visible and overlaps. */
static int scene_occlusion(struct scene_slot *slot,struct rf_gpu_graphics *g,int w,int h)
{
    size_t pixels=(size_t)w*h;
    uint32_t *color=calloc(pixels*4,4);
    float *depth=calloc(pixels*4,sizeof(float));
    unsigned visible[3]={0}, overlap[3]={0}, mismatches=0;
    int result=-1;
    if (!color || !depth) goto done;
    if (rf_gpu_graphics_scene_capture(g,slot->draws,slot->draw_count,color,depth,(uint32_t)pixels)<0) goto done;
    for(int object=0;object<3;++object) {
        struct rf_gpu_graphics_batch_item items[SCENE_DRAWS];uint32_t count=0;
        for(uint32_t d=0;d<slot->draw_count;++d) if (slot->draws[d].resource==slot->mesh[object].gpu) items[count++]=slot->draws[d];
        if (rf_gpu_graphics_scene_capture(g,items,count,color+(object+1)*pixels,depth+(object+1)*pixels,(uint32_t)pixels)<0) goto done;
    }
    for(int object=0;object<3;++object) {
        char path[64];snprintf(path,sizeof(path),"scene-object-%d.ppm",object);
        FILE *f=fopen(path,"wb"); if(!f) goto done;
        fprintf(f,"P6\n%d %d\n255\n",w,h);
        for(size_t pixel=0;pixel<pixels;++pixel) {
            uint32_t c=color[(object+1)*pixels+pixel];unsigned char rgb[3]={c,c>>8,c>>16};fwrite(rgb,1,3,f);
        }
        fclose(f);
    }
    FILE *file=fopen("scene-native.ppm","wb");
    if (!file) goto done;
    fprintf(file,"P6\n%d %d\n255\n",w,h);
    for(size_t pixel=0;pixel<pixels;++pixel) {
        unsigned char rgb[3]={(unsigned char)color[pixel],(unsigned char)(color[pixel]>>8),(unsigned char)(color[pixel]>>16)};
        fwrite(rgb,1,3,file);
    }
    fclose(file);
    for(size_t pixel=0;pixel<pixels;++pixel) {
        float nearest=0;int winner=-1;
        for(int object=0;object<3;++object) if (depth[(object+1)*pixels+pixel]>0 && depth[(object+1)*pixels+pixel]>=nearest) {
            nearest=depth[(object+1)*pixels+pixel];winner=object;
        }
        if (depth[pixel]!=nearest || (winner>=0 && color[pixel]!=color[(winner+1)*pixels+pixel])) mismatches++;
        if(winner>=0) visible[winner]++;
        if(depth[pixels+pixel]>0 && depth[2*pixels+pixel]>0) overlap[0]++;
        if(depth[pixels+pixel]>0 && depth[3*pixels+pixel]>0) overlap[1]++;
        if(depth[2*pixels+pixel]>0 && depth[3*pixels+pixel]>0) overlap[2]++;
    }
    __printf("SCENE depth-color-mismatches=%u\n",mismatches);
    if (mismatches || !visible[0] || !visible[1] || !visible[2] || !overlap[0] || !overlap[2]) goto done;
    result=0;
done:
    __printf("SCENE occlusion=%s visible=%u/%u/%u overlap=%u/%u/%u diagnostic_readback=1\n",
        result ? "FAIL" : "PASS",visible[0],visible[1],visible[2],overlap[0],overlap[1],overlap[2]);
    free(color);free(depth);return result;
}
/* A later transparent surface must still pass the opaque depth test after an
 * earlier transparent surface, and transparent submission must preserve depth. */
static int scene_transparency(struct rf_gpu_graphics *g,int w,int h)
{
    const struct rf_gpu_graphics_vertex vertices[3]={
        {{-500,-500,0},{0,0},{0}},{{500,-500,0},{0,0},{0}},
        {{0,500,0},{0,0},{0}}
    };
    const uint32_t indices[3]={0,1,2},white=0xffffff;
    struct rf_gpu_graphics_resource *resource=NULL;
    struct rf_gpu_graphics_batch_item items[3];
    size_t pixels=(size_t)w*h,center=(size_t)(h/2)*w+w/2;
    uint32_t *opaque=NULL,*mixed=NULL;
    float *opaque_depth=NULL,*mixed_depth=NULL;
    uint32_t pixel;
    int result=-1;
    if (!pixels || pixels>UINT32_MAX) return -1;
    resource=rf_gpu_graphics_resource_create(g,vertices,3,indices,3,&white,1,1);
    if (!resource || rf_gpu_graphics_resource_bind(g,resource)<0) goto done;
    memset(items,0,sizeof(items));
    for(int i=0;i<3;++i) {
        struct rf_gpu_graphics_draw *d=&items[i].draw;
        items[i].resource=resource;
        d->translation_scale[2]=i==0 ? 2000 : i==1 ? 1700 : 1800;
        d->translation_scale[3]=1000;d->rotation[1]=1024;
        d->view[1]=d->view[3]=1024;
        d->projection[0]=w;d->projection[1]=h;
        d->projection[2]=64;d->projection[3]=w*3/4;
        d->material[0]=i==0 ? 0x0000ff : i==1 ? 0xff0000 : 0x00ff00;
        d->material[1]=256;
        d->texture[0]=d->texture[1]=1;
        d->texture[2]=i==0 ? 0 : i==1 ? 128 : 64;
        d->index_count=3;d->double_sided=1;
        if (rf_gpu_graphics_validate_draw(g,d)<0) goto done;
    }
    opaque=calloc(pixels,sizeof(*opaque));mixed=calloc(pixels,sizeof(*mixed));
    opaque_depth=calloc(pixels,sizeof(*opaque_depth));
    mixed_depth=calloc(pixels,sizeof(*mixed_depth));
    if (!opaque || !mixed || !opaque_depth || !mixed_depth ||
        rf_gpu_graphics_scene_capture(g,items,1,opaque,opaque_depth,(uint32_t)pixels)<0 ||
        rf_gpu_graphics_scene_capture(g,items,3,mixed,mixed_depth,(uint32_t)pixels)<0)
        goto done;
    pixel=mixed[center];
    __printf("SCENE transparency sample opaque=%08x mixed=%08x opaque_depth=%g mixed_depth=%g\n",
        opaque[center],pixel,opaque_depth[center],mixed_depth[center]);
    if (opaque_depth[center]<=0 || mixed_depth[center]!=opaque_depth[center] ||
        (opaque[center]&0xffffff)!=0xff0000 ||
        abs((int)(pixel&255)-96)>2 ||
        abs((int)((pixel>>8)&255)-64)>2 ||
        abs((int)((pixel>>16)&255)-95)>2 || (pixel>>24)!=255) goto done;
    __printf("SCENE transparency center=%08x opaque_depth=%g mixed_depth=%g\n",
        pixel,opaque_depth[center],mixed_depth[center]);
    result=0;
done:
    free(opaque);free(mixed);free(opaque_depth);free(mixed_depth);
    if (resource) rf_gpu_graphics_resource_destroy(g,resource);
    return result;
}

/* Exercise the normal Runtime Map value/registry path through the GPU cache
 * and an offscreen Scene WORLD draw before this fixture tears down graphics. */
static int scene_world_gpu_probe(struct rf_gpu_graphics *g,int w,int h)
{
    static struct rasterfall_session session;
    static struct rasterfall_render_context render_context;
    static struct rf_gpu_scene_local_frame frame;
    static struct rf_gpu_scene_world_render_frame_v1 render;
    static struct rf_gpu_scene_world_floor_frame_v1 floor;
    static struct rf_gpu_scene_world_prop_frame_v1 props;
    static struct rf_gpu_scene_world_resources owner;
    struct rf_gpu_scene_world_input_v2 world[RF_GPU_SCENE_MAX_WORLD_V2];
    struct rf_gpu_graphics_batch_item items[256];
    struct rf_gpu_resource_cache *cache=NULL;
    struct rf_gpu_resource_cache_stats cache_stats;
    struct camera camera={0};
    uint32_t *color=NULL,world_count=0,draw_count=0,covered=0,model_count=0;
    float *depth=NULL;
    size_t pixels=(size_t)w*h;
    int result=-1,frame_active=0;
    camera.cy=camera.pitch_cy=1024;camera.y=-400;
    if (rasterfall_session_load(&session,
            "rasterfall/assets/maps/gpu_scene_render_fixture.map")<0) goto done;
    render_context.session=&session;
    rasterfall_render_bind(&render_context);
    rasterfall_render_bake_lightmap();
    if (rf_gpu_scene_world_render_freeze(&session.map_ops,1,
            session.scene_local.frame_id+1,session.scene_local.world_generation,
            world,RF_GPU_SCENE_MAX_WORLD_V2,&world_count,&render)<0 ||
        rf_gpu_scene_world_floor_freeze(&session.map_ops,0,
            session.scene_local.frame_id+1,session.scene_local.world_generation,
            &floor)<0 ||
        rf_gpu_scene_world_prop_freeze(&session.map_ops,NULL,
            session.scene_local.frame_id+1,session.scene_local.world_generation,
            &props)<0 ||
        rf_gpu_scene_local_freeze_world(&session.scene_local,&session.game_state,
            &camera,(uint32_t)w,(uint32_t)h,1,world,world_count,&frame)<0 ||
        rf_gpu_scene_world_resources_prepare(&owner,&frame.snapshot,&render,&floor,&props,
            rasterfall_render_world_light_generation())<0 ||
        (cache=rf_gpu_resource_cache_create(g,&owner.registry))==NULL ||
        rasterfall_resources_frame_begin(&owner.registry)<0) goto done;
    frame_active=1;
    if (rf_gpu_scene_world_gpu_prepare(&owner,cache,g,&camera,(uint32_t)w,
            (uint32_t)h,items,(uint32_t)(sizeof(items)/sizeof(items[0])),
            &draw_count)<0) goto done;
    if (!draw_count || pixels>UINT32_MAX ||
        !(color=calloc(pixels,sizeof(*color))) ||
        !(depth=calloc(pixels,sizeof(*depth))) ||
        rf_gpu_graphics_scene_capture(g,items,draw_count,color,depth,(uint32_t)pixels)<0)
        goto done;
    for(size_t i=0;i<pixels;++i)
        if (depth[i]>0 && color[i]) covered++;
    if (!covered) goto done;
    for(uint32_t kind=0;kind<RF_GPU_SCENE_WORLD_OPAQUE_CLASS_COUNT;++kind) {
        struct rasterfall_resource_handle handle=owner.opaque[kind];
        const struct rasterfall_model_asset *model;
        if (!handle.generation) continue;
        model_count++;
        model=rasterfall_resources_resolve(&owner.registry,handle);
        if (!model) goto done;
        for(uint32_t p=0;p<model->primitive_count;++p) {
            struct rf_gpu_cached_submesh info;
            if (rf_gpu_resource_cache_prepare(cache,owner.registry.frame_epoch,
                    handle,p,RF_GPU_CACHE_FLAT_TEXTURE,&info)<0) goto done;
        }
    }
    rf_gpu_resource_cache_get_stats(cache,&cache_stats);
    if (cache_stats.uploads!=draw_count || cache_stats.hits!=draw_count ||
        cache_stats.entries!=draw_count) goto done;
    rasterfall_resources_frame_submitted(&owner.registry);
    rasterfall_resources_invalidate(&owner.registry);
    rf_gpu_resource_cache_collect(cache);
    rf_gpu_resource_cache_get_stats(cache,&cache_stats);
    if (cache_stats.entries!=draw_count || cache_stats.releases) goto done;
    rasterfall_resources_frame_complete(&owner.registry);frame_active=0;
    rf_gpu_resource_cache_collect(cache);
    rf_gpu_resource_cache_get_stats(cache,&cache_stats);
    if (cache_stats.entries || cache_stats.releases!=draw_count) goto done;
    __printf("SCENE world-gpu=PASS world_items=%u opaque=%u models=%u draws=%u covered=%u cache_uploads=%llu cache_hits=%llu retired_releases=%llu diagnostic_readback=1\n",
        world_count,owner.accepted,model_count,
        draw_count,covered,(unsigned long long)cache_stats.uploads,
        (unsigned long long)cache_stats.hits,(unsigned long long)cache_stats.releases);
    result=0;
done:
    if (result) __printf("SCENE world-gpu=FAIL world_items=%u draws=%u covered=%u\n",
        world_count,draw_count,covered);
    free(color);free(depth);
    if (cache) rf_gpu_resource_cache_destroy(cache);
    if (frame_active) rasterfall_resources_frame_complete(&owner.registry);
    rf_gpu_scene_world_resources_invalidate(&owner);
    rasterfall_session_unload(&session);
    return result;
}
int rf_gpu_scene_native_fixture(int frames,int fault,int fault_frame)
{
    static struct toy_game game;
    static struct rf_gpu_scene_local_frame frame;
    static struct rf_gpu_scene_pose_v1 pose;
    static struct scene_slot slot;
    struct rf_gpu_scene_local_source source={0};
    struct camera camera={0};
    struct toy_window *window=NULL;
    struct toy_native_window_handle native={0};
    struct rf_gpu_vulkan_context context={0};
    struct rf_gpu gpu={0};
    struct rf_gpu_graphics *g=NULL;
    struct rf_gpu_graphics_stats stats={0};
    struct rf_gpu_scene_timing timing={0};
    struct rasterfall_resource_stats resources;
    int result=3,initialized=0,rendered=0,w=960,h=540;
#define SCENE_CHECK(x) do { if (!(x)) { __printf("SCENE FAIL line=%d frame=%d\n",__LINE__,rendered+1); goto done; } } while (0)
    if (frames<=0) frames=120;
    memset(&slot,0,sizeof(slot));
    toy_game_init(&game,1);
    int id=toy_game_add_ai(&game,TOY_GAME_AI_LEVEL_1,-3840,3300,"SCENE");
    SCENE_CHECK(id>0);
    struct toy_game_actor *actor=&game.actors[id-1];
    actor->character_id=RASTERFALL_CHARACTER_RF_RIFLEMAN;
    actor->slots[0].weapon=TOY_GAME_WEAPON_AK;actor->current_slot=0;
    toy_game_actor_set_animation(actor,TOY_GAME_ANIM_MOVE);toy_game_actor_update_animation(actor,160);
    camera.x=-3850;camera.y=-420;camera.z=1800;camera.cy=camera.pitch_cy=1024;
    SCENE_CHECK(!rf_gpu_scene_local_world(&source) && !rf_gpu_scene_local_created(&source,actor));
    SCENE_CHECK((window=toy_window_open_native("Rasterfall Scene 1B",960,540))!=NULL);
    SCENE_CHECK(toy_window_get_native_handle(window,&native)>0);
    context.native_window.type=native.type;context.native_window.window=native.window;context.native_window.instance=native.instance;
    context.require_graphics=1;context.present_fault=fault;context.present_fault_frame=fault_frame;
    SCENE_CHECK(!rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context));initialized=1;
    SCENE_CHECK((g=rf_gpu_graphics_create(&context))!=NULL);
    __printf("SCENE adapter=%s vendor=%x device=%x\n",gpu.info.adapter_name,gpu.info.vendor_id,gpu.info.device_id);
    for(int n=0;n<frames;++n) {
        struct toy_window_events events={0};
        if (n==100) actor->character_id=RASTERFALL_CHARACTER_SQUAD_A_MEDIC;
        if (n==40 || n==60) {
            SCENE_CHECK(SetWindowPos((void *)(uintptr_t)native.window,NULL,0,0,n==40 ? 800 : 976,n==40 ? 480 : 579,0x0002|0x0004));
        }
        toy_window_poll(window,&events,0);
        if(events.resized && events.width>0 && events.height>0) { w=events.width;h=events.height; }

        SCENE_CHECK(!events.close_requested);
        int64_t t0=rf_core_clock_now_us(),t1,t2,t3,t4,t5;
        SCENE_CHECK(!rf_gpu_scene_local_freeze(&source,&game,&camera,w,h,1,&frame));
        t1=rf_core_clock_now_us();
        SCENE_CHECK(!rf_gpu_scene_pose_extract(&frame,&pose));
        t2=rf_core_clock_now_us();
        SCENE_CHECK(!scene_load(&slot,&pose,1));
        if (!n) {
            struct rasterfall_resource_handle saved=slot.mesh[1].handle;
            uint32_t bones=pose.bone_count;
            const struct rasterfall_model_asset *body=rasterfall_resources_resolve_active(&slot.registry,saved);
            unsigned char bad_material[40];
            SCENE_CHECK(body && body->material_bytes==sizeof(bad_material));
            memcpy(bad_material,body->materials,sizeof(bad_material));bad_material[4]=128;
            SCENE_CHECK(!scene_material_supported(body,bad_material,1));
            memcpy(bad_material,body->materials,sizeof(bad_material));bad_material[6]=1;
            SCENE_CHECK(!scene_material_supported(body,bad_material,1));
            pose.bone_count=RF_GPU_SCENE_POSE_BONES+1;
            SCENE_CHECK(scene_pack(&slot,&pose,&camera,w,h,1)<0);pose.bone_count=bones;
            slot.mesh[1].handle.generation++;
            SCENE_CHECK(scene_pack(&slot,&pose,&camera,w,h,1)<0);slot.mesh[1].handle=saved;
            rasterfall_resources_stats(&slot.registry,&resources);SCENE_CHECK(!resources.pinned);
            rf_gpu_graphics_get_stats(g,&stats);SCENE_CHECK(!stats.frames);
            __printf("SCENE preflight palette/generation/material rejected target_writes=0 pins=0\n");
        }
        uint32_t *prior_bind=slot.mesh[1].bind,*prior_palette=slot.mesh[1].palette;
        uint32_t *prior_indices[3]={slot.mesh[0].indices,slot.mesh[1].indices,slot.mesh[2].indices};
        struct rf_gpu_graphics_vertex *prior_vertices[3]={slot.mesh[0].vertices,slot.mesh[1].vertices,slot.mesh[2].vertices};
        SCENE_CHECK(!scene_pack(&slot,&pose,&camera,w,h,1));
        if (n) {
            SCENE_CHECK(slot.mesh[1].bind==prior_bind && slot.mesh[1].palette==prior_palette);
            for(int i=0;i<3;++i)
                SCENE_CHECK(slot.mesh[i].indices==prior_indices[i] && slot.mesh[i].vertices==prior_vertices[i]);
        }
        if(n==20) SCENE_CHECK(!scene_grow(&slot.mesh[1]));
        t3=rf_core_clock_now_us();
        SCENE_CHECK(!rf_gpu_graphics_resize(g,w,h));
        SCENE_CHECK(!scene_prepare(&slot,g,1));
        t4=rf_core_clock_now_us();
        if (!n) { SCENE_CHECK(!scene_skin_diff(&slot,g,&pose)); SCENE_CHECK(!scene_occlusion(&slot,g,w,h)); SCENE_CHECK(!scene_transparency(g,w,h)); }
        SCENE_CHECK(!rf_gpu_graphics_scene_present(g,slot.draws,slot.draw_count,pose.frame_id));
        t5=rf_core_clock_now_us();
        slot.submitted=1; rasterfall_resources_frame_submitted(&slot.registry);
        rasterfall_resources_stats(&slot.registry,&resources); SCENE_CHECK(resources.pinned==3);
        if (n==80) {
            rasterfall_resources_invalidate(&slot.registry);
            rasterfall_resources_stats(&slot.registry,&resources);
            SCENE_CHECK(resources.retired==3 && resources.pinned==3 && !resources.releases);
            for(int i=0;i<3;++i) SCENE_CHECK(rasterfall_resources_resolve(&slot.registry,slot.mesh[i].handle)!=NULL);
        }
        SCENE_CHECK(!rf_gpu_graphics_scene_retire(g));slot.submitted=0;
        rf_gpu_graphics_scene_timing(g,&timing);
        SCENE_CHECK(timing.frame_id==pose.frame_id && (!timing.supported ||
            (timing.valid && __builtin_isfinite(timing.world_draw_ms) &&
                __builtin_isfinite(timing.present_blit_ms))));
        rasterfall_resources_frame_complete(&slot.registry);slot.pinned=0;
        rasterfall_resources_stats(&slot.registry,&resources);SCENE_CHECK(!resources.pinned);
        if(n==80) {
            for(int i=0;i<3;++i) SCENE_CHECK(!rasterfall_resources_resolve(&slot.registry,slot.mesh[i].handle));
            rasterfall_resources_stats(&slot.registry,&resources);SCENE_CHECK(resources.releases==3);
            scene_device_free(&slot,g);
            for(int i=0;i<3;++i) memset(&slot.mesh[i].handle,0,sizeof(slot.mesh[i].handle));
            SCENE_CHECK(!rf_gpu_scene_local_world(&source) && !rf_gpu_scene_local_created(&source,actor));
            __printf("SCENE world-retirement PASS released=3 after_fence=1\n");
        }
        if(n) __printf("SCENE cost frame=%d snapshot_us=%lld pose_us=%lld pack_us=%lld prepare_us=%lld submit_present_us=%lld retire_us=%lld extent=%dx%d\n",
            n+1,(long long)(t1-t0),(long long)(t2-t1),(long long)(t3-t2),(long long)(t4-t3),
            (long long)(t5-t4),(long long)(rf_core_clock_now_us()-t5),w,h);
        if(n) __printf("SCENE gpu-time frame=%llu supported=%d valid=%d world_draw_ms=%.6f present_blit_ms=%.6f\n",
            (unsigned long long)timing.frame_id,timing.supported,timing.valid,
            timing.world_draw_ms,timing.present_blit_ms);
        if(n==81) __printf("SCENE cpu-backing=PASS reused_after_growth=1 reused_after_world_retire=1 body_capacity=%u\n",
            slot.mesh[1].vertex_capacity);
        rendered++;
        rf_gpu_graphics_get_stats(g,&stats);
        SCENE_CHECK(!stats.bridge_roundtrips && !stats.bridge_transfer_bytes && !stats.raster_bridge_transfers);
        __printf("SCENE frame=%d world=%llu draws=%u pinned_submit=3 pinned_retired=0 bridge=%llu native=1\n",
            rendered,(unsigned long long)pose.world_generation,slot.draw_count,(unsigned long long)stats.raster_bridge_transfers);
    }
    SCENE_CHECK(!scene_world_gpu_probe(g,w,h));
    result=0;
done:
    /* Failure after submit is not retirement. Teardown drains the queue before
     * CPU pins/backing are released, including acquired-but-unsubmitted faults. */
    if (g) {
        rasterfall_resources_stats(&slot.registry,&resources);
        unsigned int held=resources.pinned;
        rf_gpu_graphics_get_stats(g,&stats); rf_gpu_graphics_destroy(g);
        __printf("SCENE teardown gpu_drained=1 pins_held_until_drain=%u\n",held);
    }
    for(int i=0;i<3;++i) scene_backing_free(&slot.mesh[i]);
    if (initialized) rf_gpu_shutdown(&gpu);
    rasterfall_resources_frame_complete(&slot.registry);
    rasterfall_resources_invalidate(&slot.registry);
    rasterfall_resources_stats(&slot.registry,&resources);
    __printf("SCENE cleanup live=%u retired=%u pinned=%u\n",resources.live,resources.retired,resources.pinned);
    if(window) toy_window_close(window);
    __printf("SCENE result=%d rendered=%d bridge=%llu submits=%llu wait_ms=%.3f\n",result,rendered,
        (unsigned long long)stats.raster_bridge_transfers,(unsigned long long)stats.queue_submits,stats.fence_wait_wall_ms);
    return result;
#undef SCENE_CHECK
}
#endif
