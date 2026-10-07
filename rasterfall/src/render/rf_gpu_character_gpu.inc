/* Common retained character geometry. Uses the fused skin batch, per-consumer
 * visibility, resource reader tracking and synchronous frame retirement. */
#include "rf_gpu_scene_block.h"
#include "rf_gpu_character_upload.h"
struct scene_character_run { unsigned first,count,color,flash; };
struct scene_character_key { unsigned body,leg,skin,hair;int profession,weapon,recipe; };
struct scene_character_asset {
    struct scene_character_key key;
    uint32_t *bind,*indices;
    struct scene_character_run *runs;
    unsigned count,run_count,refs;
    uint64_t world;
};
struct scene_character_workspace {
    uint32_t bind[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3*22];
    uint32_t indices[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3];
    struct scene_character_run runs[RF_GPU_SCENE_ENEMY_MAX_TRIANGLES];
    unsigned count,run_count;
};
struct scene_character_gpu {
    struct rf_gpu_graphics_resource *resource;
    struct scene_character_asset *asset;
    uint64_t world,frame;
    struct rf_gpu_scene_procedural_item_v1 item;
    struct rf_gpu_scene_enemy_item_v1 enemy;
    struct rf_gpu_scene_procedural_item_v1 pose_item;
    struct rf_gpu_scene_enemy_item_v1 pose_enemy;
    struct rf_gpu_character_pose pose;
    int valid,bind_dirty;
};
static int scene_character_triangle(void *opaque,const struct rf_gpu_character_vertex *v,
    unsigned color,int flash)
{
    struct scene_character_workspace *mesh=opaque;
    if(mesh->count>RF_GPU_SCENE_ENEMY_MAX_TRIANGLES*3-3)return -1;
    struct scene_character_run *run=mesh->run_count?&mesh->runs[mesh->run_count-1]:NULL;
    if(!run || run->color!=color || run->flash!=(unsigned)flash) {
        run=&mesh->runs[mesh->run_count++];
        *run=(struct scene_character_run){mesh->count,0,color,(unsigned)flash};
    }
    for(unsigned k=0;k<3;++k) {
        uint32_t *dst=mesh->bind+(mesh->count+k)*22;
        memset(dst,0,22*4);memcpy(dst,v[k].position,12);
        for(unsigned j=0;j<4;++j) {
            const struct rf_gpu_character_vertex *point=&v[j?j-1:k];
            dst[14+j*2]=point->bone0|(point->bone1<<16);
            dst[15+j*2]=point->weight|(point->type<<16);
        }
        for(unsigned n=0;n<3;++n)for(unsigned a=0;a<3;++a)
            dst[5+n*3+a]=(uint32_t)v[n].normal[a];
        mesh->indices[mesh->count+k]=mesh->count+k;
    }
    mesh->count+=3;run->count+=3;return 0;
}
static void scene_character_asset_release(struct scene_character_asset *asset)
{
    if(asset && !--asset->refs) {
        free(asset->bind);free(asset->indices);free(asset->runs);free(asset);
    }
}
static void scene_character_gpu_free(struct rf_gpu_graphics *graphics,struct scene_character_gpu *character)
{
    if(!character)return;
    if(character->resource)rf_gpu_graphics_resource_destroy(graphics,character->resource);
    scene_character_asset_release(character->asset);free(character);
}
static struct scene_character_asset *scene_character_asset_acquire(
    struct rf_gpu_scene_world_gpu_probe *probe,const struct scene_character_key *key,
    uint64_t world,const struct rf_gpu_scene_procedural_item_v1 *item,
    const struct rf_gpu_scene_enemy_item_v1 *enemy)
{
    for(unsigned i=0;i<RF_GPU_SCENE_ENEMY_CAPACITY+TOY_GAME_MAX_ACTORS;++i) {
        struct scene_character_gpu *c=probe->character[i];
        struct scene_character_asset *a=c?c->asset:NULL;
        if(a && a->world==world && !memcmp(&a->key,key,sizeof(*key))) {
            a->refs++;probe->character_cost.assets_shared++;return a;
        }
    }
    if(!probe->character_workspace)probe->character_workspace=calloc(1,sizeof(*probe->character_workspace));
    struct scene_character_workspace *mesh=probe->character_workspace;
    if(!mesh)return NULL;
    mesh->count=mesh->run_count=0;
    int result=enemy?rf_gpu_scene_infected_bind(enemy,scene_character_triangle,mesh):
        rf_gpu_scene_block_bind(item,scene_character_triangle,mesh);
    if(result<0 || !mesh->count)return NULL;
    struct scene_character_asset *a=calloc(1,sizeof(*a));
    if(!a)return NULL;
    a->refs=1;a->key=*key;a->world=world;a->count=mesh->count;a->run_count=mesh->run_count;
    a->bind=malloc(a->count*22*sizeof(uint32_t));a->indices=malloc(a->count*sizeof(uint32_t));
    a->runs=malloc(a->run_count*sizeof(*a->runs));
    if(!a->bind || !a->indices || !a->runs) {scene_character_asset_release(a);return NULL;}
    memcpy(a->bind,mesh->bind,a->count*22*sizeof(uint32_t));
    memcpy(a->indices,mesh->indices,a->count*sizeof(uint32_t));
    memcpy(a->runs,mesh->runs,a->run_count*sizeof(*a->runs));
    probe->character_cost.assets_built++;return a;
}
static int scene_character_prepare(struct rf_gpu_scene_world_gpu_probe *probe,unsigned ordinal,
    const struct rf_gpu_scene_enemy_frame_v1 *frame,const struct rf_gpu_scene_procedural_item_v1 *item,
    const struct rf_gpu_scene_enemy_item_v1 *enemy,
    const struct camera *camera,unsigned width,unsigned height,struct rf_gpu_graphics_batch_item *items,
    unsigned capacity,unsigned *count,int64_t *extract_us,int64_t *upload_us,int64_t *draw_us,
    unsigned *created,unsigned *reused,unsigned *triangles)
{
    struct scene_character_gpu *block=NULL;
    if(probe->shared_parent) {
        struct scene_character_gpu *shared=probe->shared_parent->character[ordinal];
        if(shared && shared->valid && shared->world==frame->world_generation &&
            shared->frame==frame->frame_id && (enemy?!memcmp(&shared->enemy,enemy,sizeof(*enemy)):!memcmp(&shared->item,item,sizeof(*item))))block=shared;
    }
    if(!block) {
        block=probe->character[ordinal];
        if(!block) {
            block=calloc(1,sizeof(*block));if(!block)return -1;
            probe->character[ordinal]=block;
        }
        struct scene_character_key key={0};
        if(enemy)key.recipe=enemy->infected_recipe;
        else {
            key.body=item->body_color;key.leg=item->leg_color;key.skin=item->skin_color;key.hair=item->hair_color;
            key.profession=item->state.profession_id;key.weapon=item->state.weapon;
        }
        int bind=!block->asset || block->asset->world!=frame->world_generation ||
            memcmp(&block->asset->key,&key,sizeof(key));
        int64_t start=rf_core_clock_now_us();
        if(bind) {
            struct scene_character_asset *next=scene_character_asset_acquire(probe,&key,frame->world_generation,item,enemy);
            if(!next)return -1;
            scene_character_asset_release(block->asset);block->asset=next;block->valid=0;block->bind_dirty=1;
        }
        probe->character_cost.pack_us+=rf_core_clock_now_us()-start;
        int64_t sample_start=rf_core_clock_now_us();
        /* Local evaluation excludes translation and presentation-only values.
         * Identity/source remains in the key; no mutable solver is shared. */
        struct rf_gpu_scene_procedural_item_v1 pose_item={0};
        struct rf_gpu_scene_enemy_item_v1 pose_enemy={0};
        if(enemy) {
            pose_enemy=*enemy;pose_enemy.x=pose_enemy.z=pose_enemy.lift=0;
            pose_enemy.pivot_x=pose_enemy.pivot_z=0;
            pose_enemy.pivot_y=enemy->transformed?enemy->pivot_y-enemy->lift:0;
            pose_enemy.feedback=0;pose_enemy.alpha=255;pose_enemy.transparent=0;
            pose_enemy.scene_light_q8=256;
        } else {
            pose_item=*item;pose_item.state.x=pose_item.state.z=pose_item.state.lift=0;
            pose_item.scene_light_q8=256;pose_item.auxiliary_only=0;
        }
        const char *reuse=getenv("RF_GPU_CPU_POSE_REUSE");
        if(bind || !block->valid || (reuse && !strcmp(reuse,"0")) ||
            (enemy?memcmp(&block->pose_enemy,&pose_enemy,sizeof(pose_enemy)):memcmp(&block->pose_item,&pose_item,sizeof(pose_item)))) {
            block->valid=0;
            if((enemy?rf_gpu_scene_infected_sample(&pose_enemy,&block->pose):rf_gpu_scene_block_sample(&pose_item,&block->pose))<0)return -1;
            block->pose_enemy=pose_enemy;block->pose_item=pose_item;
            probe->character_cost.poses_sampled++;
        } else probe->character_cost.poses_reused++;
        block->pose.origin[0]=enemy?enemy->x:item->state.x;
        block->pose.origin[1]=enemy?enemy->lift:item->state.lift;
        block->pose.origin[2]=enemy?enemy->z:item->state.z;
        probe->character_cost.sample_us+=rf_core_clock_now_us()-sample_start;
        *extract_us+=rf_core_clock_now_us()-start;
        uint32_t palette[RF_GPU_CHARACTER_BONES*15];
        for(unsigned i=0;i<block->pose.bone_count;++i) {
            const struct rasterfall_model_skin_palette_bone *bone=&block->pose.palette[i];
            for(unsigned j=0;j<12;++j) {
                float value=(float)(j<9?bone->rotation[j]:bone->position[j-9]);
                memcpy(palette+i*15+j,&value,4);
            }
            for(unsigned j=0;j<3;++j)palette[i*15+12+j]=(uint32_t)bone->rest[j];
        }
        start=rf_core_clock_now_us();
        struct scene_character_asset *asset=block->asset;
        int uploaded=rf_gpu_character_upload(probe->graphics,&block->resource,asset->count,
            asset->indices,asset->bind,block->bind_dirty,palette,block->pose.bone_count*15);
        if(uploaded<0)return -1;
        if(uploaded || block->bind_dirty)probe->character_cost.bind_uploads++;
        probe->character_cost.palette_uploads++;
        if(uploaded)(*created)++;else (*reused)++;
        block->world=frame->world_generation;block->frame=frame->frame_id;
        if(enemy)block->enemy=*enemy;else block->item=*item;
        block->valid=1;block->bind_dirty=0;
        *upload_us+=rf_core_clock_now_us()-start;
        probe->character_cost.upload_us+=rf_core_clock_now_us()-start;
    } else {(*reused)++;probe->character_cost.views_reused++;}
    int64_t start=rf_core_clock_now_us();
    if(rf_gpu_graphics_resource_bind(probe->graphics,block->resource)<0)return -1;
    for(unsigned i=0;i<block->asset->run_count;++i) {
        const struct scene_character_run *run=&block->asset->runs[i];
        if((run->flash&RF_GPU_CHARACTER_FLASH) && !block->pose.flash)continue;
        if(*count>=capacity)return -1;
        struct rf_gpu_graphics_batch_item *entry=&items[(*count)++];
        memset(entry,0,sizeof(*entry));entry->resource=block->resource;
        struct rf_gpu_graphics_draw *d=&entry->draw;
        memcpy(d->translation_scale,block->pose.origin,12);d->translation_scale[3]=1000;d->rotation[1]=1024;
        d->camera[0]=camera->x;d->camera[1]=camera->y;d->camera[2]=camera->z;
        d->view[0]=camera->sy;d->view[1]=camera->cy;d->view[2]=camera->pitch_sy;d->view[3]=camera->pitch_cy;
        d->projection[0]=width;d->projection[1]=height;d->projection[2]=64;d->projection[3]=width*3/4;
        d->material[0]=run->color;d->material[1]=256;d->material[3]=1;
        d->texture[0]=d->texture[1]=1;d->double_sided=enemy?(enemy->transformed || (run->flash&RF_GPU_CHARACTER_DOUBLE_SIDED)!=0):item->double_sided;
        if(enemy) {
            if(enemy->feedback)d->material[0]=enemy->feedback;
            if(run->flash&RF_GPU_CHARACTER_FORM_LIGHT) {d->quality[2]=4;d->material[3]=0;}
            if(enemy->transparent) {d->texture[2]=enemy->alpha;d->scene_layer=RF_GPU_SCENE_TRANSPARENT;}
        }
        d->first_index=run->first;d->index_count=run->count;
        if(rf_gpu_graphics_validate_draw(probe->graphics,d)<0)return -1;
        *triangles+=run->count/3;
    }
    *draw_us+=rf_core_clock_now_us()-start;
    return 0;
}
