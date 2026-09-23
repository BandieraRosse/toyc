/* Explicit Windows Scene fixture. Normal runtime never enters this executor. */
#include "rf_gpu_scene_pose.h"
#ifndef TOYC_WINDOWS
int rf_gpu_scene_native_fixture(int frames,int fault,int fault_frame)
{ (void)frames; (void)fault; (void)fault_frame; return 3; }
#else
#include "tlibc_everything.h"
#include "rf_gpu_graphics.h"
#include "rasterfall_render_resources.h"
#include "toy_window.h"
#include "rasterfall_units.h"
#include "rf_core_host.h"
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

__declspec(dllimport) int __stdcall SetWindowPos(void *, void *, int, int, int, int, unsigned int);
#define SCENE_DRAWS 64
static int scene_round(double v) { return (int)(v<0 ? v-0.5 : v+0.5); }
struct scene_mesh {
    struct rasterfall_resource_handle handle;
    uint32_t *bind, *palette, *indices;
    struct rf_gpu_graphics_vertex *vertices;
    uint32_t count, palette_count;
    struct rf_gpu_graphics_resource *gpu;
};
struct scene_slot {
    struct rasterfall_resource_registry registry;
    struct scene_mesh mesh[3];
    struct rf_gpu_graphics_batch_item draws[SCENE_DRAWS];
    uint32_t draw_count;
    int pinned, submitted;
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
}
static void scene_device_free(struct scene_slot *slot,struct rf_gpu_graphics *g)
{
    for (int i=0;i<3;++i) {
        if (slot->mesh[i].gpu) rf_gpu_graphics_resource_destroy(g,slot->mesh[i].gpu);
        slot->mesh[i].gpu=NULL; scene_backing_free(&slot->mesh[i]);
    }
}
static int scene_load(struct scene_slot *slot,const struct rf_gpu_scene_pose_v1 *pose)
{
    char path[256];
    struct rasterfall_model_asset *map;
    const char *names[2];
    if (pose->body_resource_id!=RASTERFALL_BODY_RF_HUMANOID_V2 || !pose->attachment_count ||
        pose->attachments[0].resource_id!=RASTERFALL_GEAR_RIFLEMAN_HEAD) return -1;
    names[0]=rasterfall_character_body_resource_name(pose->body_resource_id);
    names[1]=rasterfall_character_gear_resource_name(pose->attachments[0].resource_id);
    if (!slot->mesh[0].handle.generation) {
        map=rf_gpu_scene_fixture_map();
        if (!map) return -1;
        if (rasterfall_resources_adopt(&slot->registry,"scene:opaque_box",map,&slot->mesh[0].handle)<0) {
            rasterfall_model_unload(map); tlibc_free(map); return -1;
        }
    }
    for (int i=0;i<2;++i) {
        if (!names[i] || snprintf(path,sizeof(path),"rasterfall/private-assets/models/%s.rmesh",names[i])>=(int)sizeof(path) ||
            rasterfall_resources_load(&slot->registry,path,&slot->mesh[i+1].handle)<0) return -1;
    }
    return 0;
}
static int scene_material_supported(const struct rasterfall_model_asset *m,
    const unsigned char *material,int object)
{
    return scene_u32(material+8)==UINT32_MAX && !scene_u32(material+12) &&
        (m->format_version<4 || material[4]==255) &&
        (m->format_version<5 || !material[6]) &&
        (m->format_version<7 || !(material[7]&~1u)) &&
        (m->material_bytes<24 || (!scene_u32(material+16)&&!scene_u32(material+20))) &&
        (!object || m->material_bytes<40 || (!scene_u32(material+24)&&!scene_u32(material+28)&&!scene_u32(material+32)));
}
/* Whole-frame validation and CPU packing precede pin and every target write.
 * Palette/rigid transforms are values, never mutable instance pointers. */
static int scene_pack(struct scene_slot *slot,const struct rf_gpu_scene_pose_v1 *pose,int w,int h)
{
    if (pose->abi_version!=1 || pose->byte_size!=sizeof(*pose) || pose->actor_count!=1 ||
        !pose->frame_id || !pose->world_generation || !pose->bone_count ||
        pose->bone_count>RF_GPU_SCENE_POSE_BONES || pose->bind_normals>1) return -1;
    slot->draw_count=0;
    for (int object=0;object<3;++object) {
        struct scene_mesh *out=&slot->mesh[object];
        const struct rasterfall_model_asset *m=rasterfall_resources_resolve_active(&slot->registry,out->handle);
        const struct rasterfall_rigid_transform *transform=object==1 ? &pose->body_to_world : &pose->attachments[0].model_to_world;
        scene_backing_free(out);
        if (!m || !m->index_count || m->index_count>65536 || m->index_count%3 || m->vertex_bytes<24 ||
            !scene_range(m,m->vertices,(uint64_t)m->vertex_count*m->vertex_bytes) ||
            !scene_range(m,m->indices,(uint64_t)m->index_count*4) ||
            !scene_range(m,m->primitives,(uint64_t)m->primitive_count*16) ||
            !scene_range(m,m->materials,(uint64_t)m->material_count*m->material_bytes) || m->material_bytes<16 ||
            (object==1 && (m->bone_count!=pose->bone_count ||
                !scene_range(m,m->skin_vertices,(uint64_t)m->vertex_count*8))) ||
            (object!=1 && m->bone_count)) return -1;
        out->count=m->index_count;
        out->indices=calloc(out->count,4);
        out->vertices=calloc(out->count,sizeof(*out->vertices));
        if (object) {
            out->palette_count=object==1 ? pose->bone_count*15 : 15;
            out->bind=calloc((size_t)out->count*22,4);
            out->palette=calloc(out->palette_count,4);
            if (!out->bind || !out->palette) return -1;
            if (!m->position_scale || transform->scale_milli<1 || transform->scale_milli>8000) return -1;
            for(int k=0;k<9;++k) if (!__builtin_isfinite(transform->rotation[k]) || fabs(transform->rotation[k])>1.01) return -1;
            for(int k=0;k<3;++k) if (!__builtin_isfinite(transform->translation[k]) || fabs(transform->translation[k])>262144) return -1;
            for(uint32_t bone=0;bone<out->palette_count/15;++bone) {
                const struct rasterfall_model_skin_palette_bone *p=&pose->palette[bone];
                uint32_t *dst=out->palette+bone*15;
                for(int k=0;k<9;++k) {
                    double v=object==1 ? p->rotation[k] : transform->rotation[k]*(((int64_t)RASTERFALL_RFU_PER_METER*transform->scale_milli+m->position_scale/2)/m->position_scale)/1000.0;
                    if (!__builtin_isfinite(v) || fabs(v)>8) return -1;
                    dst[k]=scene_float(v);
                }
                for(int k=0;k<3;++k) {
                    double v=object==1 ? p->position[k] : transform->translation[k];
                    if (!__builtin_isfinite(v) || fabs(v)>262144) return -1;
                    dst[9+k]=scene_float(v); dst[12+k]=object==1 ? (uint32_t)p->rest[k] : 0;
                }
            }
        }
        if (!out->indices || !out->vertices) return -1;
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
            d->camera[0]=-3850; d->camera[1]=-420; d->camera[2]=1800;
            d->view[1]=d->view[3]=1024;
            d->projection[0]=w;d->projection[1]=h;d->projection[2]=64;d->projection[3]=w*3/4;
            d->material[0]=scene_u32(material)&0xffffff; d->material[1]=256;
            if (object==1) {
                const struct rasterfall_character_visual_recipe *recipe=
                    rasterfall_character_visual_recipe(RASTERFALL_MODULAR_RIFLEMAN);
                if (!recipe) return -1;
                if (mi==0) d->material[0]=recipe->pants_color;
                if (mi==1) d->material[0]=recipe->shirt_color;
            }
            d->rotation[3]=object!=0; d->texture[0]=d->texture[1]=1;
            d->first_index=first;d->index_count=count; d->double_sided=m->format_version>=7 ? material[7]&1u : 1u;
            /* Until device preparation, resource index is held by draw grouping. */
        }
    }
    return 0;
}
static int scene_prepare(struct scene_slot *slot,struct rf_gpu_graphics *g)
{
    uint32_t white=0xffffff,draw=0;
    if (rasterfall_resources_frame_begin(&slot->registry)<0) return -1;
    slot->pinned=1;
    for(int i=0;i<3;++i) if (rasterfall_resources_pin(&slot->registry,slot->mesh[i].handle)<0) return -1;
    for(int i=0;i<3;++i) {
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
static int scene_grow(struct scene_mesh *m)
{
    uint32_t count=m->count;
    uint32_t *bind=malloc((size_t)count*44*4), *indices=malloc((size_t)count*2*4);
    if (!bind || !indices) { free(bind);free(indices);return -1; }
    memcpy(bind,m->bind,(size_t)count*22*4);
    memcpy(bind+count*22,m->bind,(size_t)count*22*4);
    for(uint32_t i=0;i<count*2;++i) indices[i]=i;
    free(m->bind);free(m->indices);m->bind=bind;m->indices=indices;m->count=count*2;
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
        SCENE_CHECK(!scene_load(&slot,&pose));
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
            SCENE_CHECK(scene_pack(&slot,&pose,w,h)<0);pose.bone_count=bones;
            slot.mesh[1].handle.generation++;
            SCENE_CHECK(scene_pack(&slot,&pose,w,h)<0);slot.mesh[1].handle=saved;
            rasterfall_resources_stats(&slot.registry,&resources);SCENE_CHECK(!resources.pinned);
            rf_gpu_graphics_get_stats(g,&stats);SCENE_CHECK(!stats.frames);
            __printf("SCENE preflight palette/generation/material rejected target_writes=0 pins=0\n");
        }
        SCENE_CHECK(!scene_pack(&slot,&pose,w,h));
        if(n==20) SCENE_CHECK(!scene_grow(&slot.mesh[1]));
        t3=rf_core_clock_now_us();
        SCENE_CHECK(!rf_gpu_graphics_resize(g,w,h));
        SCENE_CHECK(!scene_prepare(&slot,g));
        t4=rf_core_clock_now_us();
        if (!n) { SCENE_CHECK(!scene_skin_diff(&slot,g,&pose)); SCENE_CHECK(!scene_occlusion(&slot,g,w,h)); }
        SCENE_CHECK(!rf_gpu_graphics_scene_present(g,slot.draws,slot.draw_count));
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
        rendered++;
        rf_gpu_graphics_get_stats(g,&stats);
        SCENE_CHECK(!stats.bridge_roundtrips && !stats.bridge_transfer_bytes && !stats.raster_bridge_transfers);
        __printf("SCENE frame=%d world=%llu draws=%u pinned_submit=3 pinned_retired=0 bridge=%llu native=1\n",
            rendered,(unsigned long long)pose.world_generation,slot.draw_count,(unsigned long long)stats.raster_bridge_transfers);
    }
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
