/* Bake once, reuse across frames and launches. The retained volume also lights
 * moving actors. Transient lamps are deliberately excluded by the GI shader. */
static uint64_t gfx_bake_key(struct rf_gpu_graphics *g)
{
    uint64_t h=g->bake_geometry_key;
    unsigned settings[]={3,(unsigned)g->indirect_bounces,(unsigned)g->indirect_visibility,
        (unsigned)g->receiver_fp32,(unsigned)g->daylight_enabled,(unsigned)g->indirect_reuse,
        (unsigned)g->owner->ray_query_enabled,g->indirect_source_count};
    h=bake_hash(h,settings,sizeof(settings));
    h=bake_hash(h,g->indirect_source_snapshot,g->indirect_source_count*sizeof(struct rf_gpu_light));
    h=bake_hash(h,g->lighting.sun_direction,16);h=bake_hash(h,g->lighting.sun_color,16);
    h=bake_hash(h,g->lighting.sky_cloud,16);h=bake_hash(h,g->lighting.sky_weather,16);
    h=bake_hash(h,&g->lighting.sky_illuminance,4);
    /* Compiled producer identity makes shader changes invalidate old results. */
    static uint64_t shader_key;
    if(!shader_key) {
        shader_key=bake_hash(bake_seed(),rf_graphics_indirect_comp_spirv,sizeof(rf_graphics_indirect_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_daylight_comp_spirv,sizeof(rf_graphics_daylight_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_receiver_comp_spirv,sizeof(rf_graphics_receiver_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_environment_comp_spirv,sizeof(rf_graphics_environment_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_baked_comp_spirv,sizeof(rf_graphics_baked_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_baked_ray_comp_spirv,sizeof(rf_graphics_baked_ray_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_indirect_ray_comp_spirv,sizeof(rf_graphics_indirect_ray_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_daylight_ray_comp_spirv,sizeof(rf_graphics_daylight_ray_comp_spirv));
        shader_key=bake_hash(shader_key,rf_graphics_receiver_ray_comp_spirv,sizeof(rf_graphics_receiver_ray_comp_spirv));
    }
    return bake_hash(h,&shader_key,sizeof(shader_key));
}
static int gfx_bake_prepare(struct rf_gpu_graphics *g)
{
    if(!g->baked_fixtures || g->lighting.disable_indirect || !g->architecture_count ||
       !g->receiver_samples || g->scene_in_flight || g->poisoned)return 0;
    struct rf_gpu_vulkan_buffer *fields[]={&g->indirect_buffer,&g->daylight_buffer,
        &g->environment_buffer,&g->receiver_buffer};
    uint64_t key=gfx_bake_key(g),sizes[4];size_t bytes=sizeof(sizes);
    for(unsigned i=0;i<4;++i){sizes[i]=fields[i]->size;if(sizes[i]>RF_BAKE_LIMIT-bytes)return 0;bytes+=(size_t)sizes[i];}
    if(g->bake_key && key!=g->bake_key) {
        g->bake_checked=g->bake_pending=0;
        g->indirect_warmed=g->daylight_warmed=g->bake_environment_ready=0;
        g->indirect_cursor=g->daylight_cursor=0;g->indirect_reset_history=1;
        g->receiver_full_refresh=1;
    }
    /* AUX clones deliberately allocate device-only fields. They already reuse
     * the parent's bake, and independent edits rebake in place. Do not map
     * device-only memory or add a readback submission just to persist a clone. */
    for(unsigned i=0;i<4;++i)if((fields[i]->memory_flags&
        (RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT))!=
        (RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        g->bake_key=key;g->bake_checked=1;g->bake_pending=0;return 0;
    }
    if(!g->bake_checked) {
        size_t read_bytes;unsigned char *data=bake_read(key,2,bytes,&read_bytes);
        int valid=data && !memcmp(data,sizes,sizeof(sizes));
        /* Layouts are rebuilt from current geometry. Check immutable headers
         * before installing payload; no file controls a shader buffer extent. */
        size_t at=sizeof(sizes);void *mapped[4]={0};
        for(unsigned i=0;i<4;++i) {
            if(g->owner->api.map_memory(g->owner->device,fields[i]->memory,0,fields[i]->allocation_size,0,&mapped[i])!=RF_VK_SUCCESS)valid=0;
            if(valid && i!=2 && memcmp(mapped[i],data+at,i==3?16:64))valid=0;
            at+=(size_t)sizes[i];
        }
        if(valid) {
            at=sizeof(sizes);
            for(unsigned i=0;i<4;++i){memcpy(mapped[i],data+at,(size_t)sizes[i]);at+=(size_t)sizes[i];}
            g->indirect_warmed=g->daylight_warmed=g->bake_environment_ready=g->receiver_ready=1;
            g->receiver_full_refresh=g->indirect_reset_history=0;
        }
        for(unsigned i=0;i<4;++i)if(mapped[i])g->owner->api.unmap_memory(g->owner->device,fields[i]->memory);
        free(data);g->bake_key=key;g->bake_checked=1;g->bake_pending=valid?0:1;
        fprintf(stderr,"rf-gpu-bake: %s key=%016llx bytes=%llu\n",valid?"hit":"cold",(unsigned long long)key,(unsigned long long)bytes);
    } else if(g->bake_pending==2) {
        unsigned char *data=malloc(bytes);if(!data){g->bake_pending=0;return 0;}
        memcpy(data,sizes,sizeof(sizes));size_t at=sizeof(sizes);int ok=1;
        for(unsigned i=0;i<4;++i) {
            void *mapped=NULL;
            if(g->owner->api.map_memory(g->owner->device,fields[i]->memory,0,fields[i]->allocation_size,0,&mapped)!=RF_VK_SUCCESS){ok=0;break;}
            memcpy(data+at,mapped,(size_t)sizes[i]);at+=(size_t)sizes[i];
            g->owner->api.unmap_memory(g->owner->device,fields[i]->memory);
        }
        if(ok)bake_write(key,2,data,bytes);
        free(data);g->bake_pending=0;
    }
    return 0;
}
static void gfx_bake_host_barrier(struct rf_gpu_graphics *g)
{
    if(g->bake_pending!=1 || !g->receiver_ready ||
       (g->indirect_probe_count && !g->indirect_warmed) ||
       (g->daylight_probe_count && g->lighting.sky_illuminance>0 && !g->daylight_warmed))return;
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_HOST_READ_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    g->bake_pending=2;
}

/* A small two-sided triangular lightmap for immutable architectural meshes.
 * Stores GI plus fixed-light visibility. The live BRDF and dynamic shadow maps
 * are retained, so moving highlights and actor shadows do not get baked in. */
static int gfx_baked_surface_eligible(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_batch_item *item)
{
    const struct rf_gpu_graphics_draw *d=&item->draw;const struct rf_gpu_graphics_resource *r=item->resource;
    return g->baked_fixtures && g->baked_surfaces && !g->lighting.disable_indirect && g->bake_checked &&
        d->architecture_occluder && d->scene_layer==RF_GPU_SCENE_WORLD && !d->texture[2] &&
        !d->texture[3] && !d->first_index && d->quality[2]==0 &&
        r->sequential_triangles && !r->frame_dynamic && !r->skin_bind.buffer && r->bake_mesh_key;
}
static int gfx_baked_surface_prepare(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,unsigned count)
{
    if(g->baked_surface_count!=count) {
        uint32_t *offsets=count?calloc(count,4):NULL;if(count && !offsets)return -1;
        free(g->baked_surface_offsets);g->baked_surface_offsets=offsets;g->baked_surface_count=count;
    } else if(count)memset(g->baked_surface_offsets,0,(size_t)count*4);
    uint64_t key=g->bake_key;uint32_t samples=0,draws=0,skipped=0;
    for(unsigned i=0;i<count;++i)if(gfx_baked_surface_eligible(g,&items[i])) {
        const struct rf_gpu_graphics_draw *d=&items[i].draw;
        uint32_t n=d->index_count/3u*32u;
        if(n>4u*1024u*1024u-samples){++skipped;continue;}
        g->baked_surface_offsets[i]=samples+1;samples+=n;++draws;
        key=bake_hash(key,&items[i].resource->bake_mesh_key,8);
        key=bake_hash(key,d->translation_scale,16);key=bake_hash(key,d->rotation,12);
        key=bake_hash(key,&d->quality[1],4);key=bake_hash(key,&d->index_count,4);
    }
    if(!samples)return 0;
    if(key==g->baked_surface_key) {
        if(g->baked_surface_pending==2) {
            void *mapped=NULL;
            if(g->owner->api.map_memory(g->owner->device,g->baked_surface.memory,0,g->baked_surface.allocation_size,0,&mapped)==RF_VK_SUCCESS) {
                bake_write(key,3,mapped,(size_t)g->baked_surface.size);
                g->owner->api.unmap_memory(g->owner->device,g->baked_surface.memory);
            }
            g->baked_surface_pending=0;
        }
        return 0;
    }
    size_t bytes=(size_t)samples*32,read_bytes=0;
    void *data=bake_read(key,3,bytes,&read_bytes);int hit=data!=NULL;
    if(!data)data=calloc(1,bytes);
    struct rf_gpu_vulkan_buffer next={0};
    if(!data || indirect_upload(g,data,bytes,&next)<0){free(data);return -1;}
    free(data);indirect_bind(g,18,&g->baked_surface,&next);
    g->baked_surface_key=key;g->baked_surface_samples=samples;g->baked_surface_pending=hit?0:1;
    fprintf(stderr,"rf-gpu-bake: surface=%s draws=%u samples=%u bytes=%llu budget-fallback=%u\n",
        hit?"hit":"cold",draws,samples,(unsigned long long)bytes,skipped);
    return 0;
}
static void gfx_baked_surface_record(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,unsigned count)
{
    if(g->baked_surface_pending!=1)return;
    struct rf_gpu_vulkan_impl *p=g->owner;
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->baked_surface_pipeline);
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_layout,1,1,&g->light_set,0,NULL);
    for(unsigned i=0;i<count;++i)if(g->baked_surface_offsets[i]) {
        struct rf_gpu_graphics_draw draw=items[i].draw;
        draw.rotation[3]=(int32_t)g->baked_surface_offsets[i];draw.camera[3]=(int32_t)(draw.index_count/3u*32u);
        rf_vk_descriptor_set set=items[i].resource->descriptor_set;
        p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_layout,0,1,&set,0,NULL);
        g->api.CmdPushConstants(g->cmd,g->sky_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,&draw);
        p->api.cmd_dispatch(g->cmd,((unsigned)draw.camera[3]+63u)/64u,1,1);
    }
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_HOST_READ_BIT;
    p->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    g->baked_surface_pending=2;
}
