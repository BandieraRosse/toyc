/* Retained Block geometry. Uses the existing fused skin batch, per-consumer
 * visibility, resource reader tracking and synchronous frame retirement. */
#include "rf_gpu_scene_block.h"
struct scene_block_run { unsigned first,count,color,flash; };
struct scene_block_key { unsigned body,leg,skin,hair;int profession,weapon; };
struct scene_block_workspace {
    uint32_t bind[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3*22];
    uint32_t indices[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3];
    struct scene_block_run runs[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES];
    unsigned count,run_count;
};
struct scene_block_gpu {
    struct rf_gpu_graphics_resource *resource;
    struct scene_block_key key;
    struct scene_block_run *runs;
    unsigned count,run_count;
    uint64_t world,frame;
    struct rf_gpu_scene_procedural_item_v1 item;
    struct rf_gpu_scene_block_pose pose;
    int valid;
};
static int scene_block_triangle(void *opaque,const struct rf_gpu_scene_block_vertex *v,
    unsigned color,int flash)
{
    struct scene_block_workspace *mesh=opaque;
    if(mesh->count>RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3-3)return -1;
    struct scene_block_run *run=mesh->run_count?&mesh->runs[mesh->run_count-1]:NULL;
    if(!run || run->color!=color || run->flash!=(unsigned)flash) {
        run=&mesh->runs[mesh->run_count++];
        *run=(struct scene_block_run){mesh->count,0,color,(unsigned)flash};
    }
    for(unsigned k=0;k<3;++k) {
        uint32_t *dst=mesh->bind+(mesh->count+k)*22;
        memset(dst,0,22*4);memcpy(dst,v[k].position,12);
        for(unsigned j=0;j<4;++j) {
            const struct rf_gpu_scene_block_vertex *point=&v[j?j-1:k];
            dst[14+j*2]=point->bone0|(point->bone1<<16);
            dst[15+j*2]=point->weight|(point->type<<16);
        }
        mesh->indices[mesh->count+k]=mesh->count+k;
    }
    mesh->count+=3;run->count+=3;return 0;
}
static void scene_block_gpu_free(struct rf_gpu_graphics *graphics,struct scene_block_gpu *block)
{
    if(!block)return;
    if(block->resource)rf_gpu_graphics_resource_destroy(graphics,block->resource);
    free(block->runs);free(block);
}
static int scene_block_prepare(struct rf_gpu_scene_world_gpu_probe *probe,unsigned ordinal,
    const struct rf_gpu_scene_enemy_frame_v1 *frame,const struct rf_gpu_scene_procedural_item_v1 *item,
    const struct camera *camera,unsigned width,unsigned height,struct rf_gpu_graphics_batch_item *items,
    unsigned capacity,unsigned *count,int64_t *extract_us,int64_t *upload_us,int64_t *draw_us,
    unsigned *created,unsigned *reused,unsigned *triangles)
{
    struct scene_block_gpu *block=NULL;
    if(probe->shared_parent) {
        struct scene_block_gpu *shared=probe->shared_parent->block[ordinal];
        if(shared && shared->valid && shared->world==frame->world_generation &&
            shared->frame==frame->frame_id && !memcmp(&shared->item,item,sizeof(*item)))block=shared;
    }
    if(!block) {
        block=probe->block[ordinal];
        if(!block) {
            block=calloc(1,sizeof(*block));if(!block)return -1;
            probe->block[ordinal]=block;
        }
        const struct scene_block_key key={item->body_color,item->leg_color,item->skin_color,item->hair_color,
            item->state.profession_id,item->state.weapon};
        int bind=!block->resource || block->world!=frame->world_generation || memcmp(&block->key,&key,sizeof(key));
        int64_t start=rf_core_clock_now_us();
        if(bind) {
            if(!probe->block_workspace)probe->block_workspace=calloc(1,sizeof(*probe->block_workspace));
            if(!probe->block_workspace)return -1;
            probe->block_workspace->count=probe->block_workspace->run_count=0;
            if(rf_gpu_scene_block_bind(item,scene_block_triangle,probe->block_workspace)<0)return -1;
        }
        if(bind || !block->valid || memcmp(&block->item,item,sizeof(*item))) {
            block->valid=0;
            if(rf_gpu_scene_block_sample(item,&block->pose)<0)return -1;
        }
        *extract_us+=rf_core_clock_now_us()-start;
        uint32_t palette[RF_GPU_SCENE_BLOCK_BONES*15];
        for(unsigned i=0;i<block->pose.bone_count;++i) {
            const struct rasterfall_model_skin_palette_bone *bone=&block->pose.palette[i];
            for(unsigned j=0;j<12;++j) {
                float value=(float)(j<9?bone->rotation[j]:bone->position[j-9]);
                memcpy(palette+i*15+j,&value,4);
            }
            for(unsigned j=0;j<3;++j)palette[i*15+12+j]=(uint32_t)bone->rest[j];
        }
        start=rf_core_clock_now_us();
        struct scene_block_workspace *mesh=probe->block_workspace;
        unsigned vertices=bind?mesh->count:block->count;
        int grow=block->resource?rf_gpu_graphics_skinned_resource_update(probe->graphics,block->resource,
            vertices,bind?mesh->bind:NULL,bind?vertices*22:0,palette,block->pose.bone_count*15):1;
        if(grow<0)return -1;
        if(grow) {
            const uint32_t white=0xffffff;
            struct rf_gpu_graphics_resource *next=rf_gpu_graphics_skinned_resource_create(probe->graphics,
                NULL,vertices,mesh->indices,vertices,mesh->bind,vertices*22,
                palette,block->pose.bone_count*15,&white,1,1);
            if(!next)return -1;
            if(block->resource && rf_gpu_graphics_resource_destroy(probe->graphics,block->resource)<0) {
                rf_gpu_graphics_resource_destroy(probe->graphics,next);return -1;
            }
            block->resource=next;(*created)++;
        } else (*reused)++;
        if(bind) {
            void *runs=realloc(block->runs,mesh->run_count*sizeof(*block->runs));
            if(!runs)return -1;
            block->runs=runs;memcpy(block->runs,mesh->runs,mesh->run_count*sizeof(*block->runs));
            block->count=mesh->count;block->run_count=mesh->run_count;block->key=key;
        }
        block->world=frame->world_generation;block->frame=frame->frame_id;block->item=*item;block->valid=1;
        *upload_us+=rf_core_clock_now_us()-start;
    } else (*reused)++;
    int64_t start=rf_core_clock_now_us();
    if(rf_gpu_graphics_resource_bind(probe->graphics,block->resource)<0)return -1;
    for(unsigned i=0;i<block->run_count;++i) {
        const struct scene_block_run *run=&block->runs[i];
        if(run->flash && !block->pose.flash)continue;
        if(*count>=capacity)return -1;
        struct rf_gpu_graphics_batch_item *entry=&items[(*count)++];
        memset(entry,0,sizeof(*entry));entry->resource=block->resource;
        struct rf_gpu_graphics_draw *d=&entry->draw;
        memcpy(d->translation_scale,block->pose.origin,12);d->translation_scale[3]=1000;d->rotation[1]=1024;
        d->camera[0]=camera->x;d->camera[1]=camera->y;d->camera[2]=camera->z;
        d->view[0]=camera->sy;d->view[1]=camera->cy;d->view[2]=camera->pitch_sy;d->view[3]=camera->pitch_cy;
        d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
        d->material[0]=run->color;d->material[1]=256;d->material[3]=1;
        d->texture[0]=d->texture[1]=1;d->double_sided=item->double_sided;
        d->first_index=run->first;d->index_count=run->count;
        if(rf_gpu_graphics_validate_draw(probe->graphics,d)<0)return -1;
        *triangles+=run->count/3;
    }
    *draw_us+=rf_core_clock_now_us()-start;
    return 0;
}
