/* Immutable opaque texture sets. Header is eight {w,h,offset,levels} entries;
 * each image then stores its complete RGB24 mip pyramid. Shader and builder
 * use the same floor-halving dimensions. No file-format or gameplay state. */
static void gfx_texture_set_release(struct rf_gpu_graphics *g,struct rf_graphics_texture_set *set)
{
    if (set && !--set->references) {
        gpu_buffer_destroy(g->owner,&set->buffer);free(set);
    }
}
static int gfx_texture_set_bind(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,struct rf_graphics_texture_set *set)
{
    struct rf_vk_descriptor_buffer_info info={0};
    struct rf_vk_write_descriptor_set write={0};
    if (!g || g->poisoned || g->scene_in_flight || !resource || resource->owner!=g ||
        resource->shared_binding.owner || !set) return -1;
    info.buffer=set->buffer.buffer;info.range=set->buffer.size;
    write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dst_set=resource->descriptor_set;write.dst_binding=0;write.descriptor_count=1;
    write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.buffer_info=&info;
    g->owner->api.update_descriptor_sets(g->owner->device,1,&write,0,NULL);
    ++set->references;
    gfx_texture_set_release(g,resource->texture_set);resource->texture_set=set;
    return 0;
}
static unsigned gfx_texture_encode(float linear)
{
    float value=linear<=0.0031308f ? linear*12.92f : 1.055f*(float)pow(linear,1.0/2.4)-0.055f;
    return (unsigned)((value<0?0:value>1?1:value)*255.0f+0.5f);
}
static uint32_t *gfx_texture_set_pack(const struct rf_gpu_graphics_texture_image *images,
    uint32_t count,uint32_t *word_count)
{
    uint32_t words=RF_GPU_GRAPHICS_TEXTURES*4,offset=words;
    float linear[256];
    if (!images || !count || count>RF_GPU_GRAPHICS_TEXTURES) return NULL;
    for (uint32_t i=0;i<count;++i) {
        uint32_t w=images[i].width,h=images[i].height;
        if (!images[i].rgb || !w || !h || w>1024 || h>1024) return NULL;
        for (;;) { words+=w*h;if(w==1 && h==1) break;w=w>1?w/2:1;h=h>1?h/2:1; }
    }
    uint32_t *data=calloc(words,sizeof(*data));if(!data)return NULL;
    for(unsigned i=0;i<256;++i) {
        float s=i/255.0f;linear[i]=s<=0.04045f?s/12.92f:(float)pow((s+0.055f)/1.055f,2.4);
    }
    for(uint32_t image=0;image<count;++image) {
        uint32_t w=images[image].width,h=images[image].height,levels=1;
        data[image*4]=w;data[image*4+1]=h;data[image*4+2]=offset;
        memcpy(data+offset,images[image].rgb,(size_t)w*h*4);
        uint32_t previous=offset;offset+=w*h;
        while(w>1 || h>1) {
            uint32_t nw=w>1?w/2:1,nh=h>1?h/2:1;
            for(uint32_t y=0;y<nh;++y) for(uint32_t x=0;x<nw;++x) {
                /* Area bins include the last row/column of odd-size images. */
                uint32_t x0=x*w/nw,x1=(x+1)*w/nw,y0=y*h/nh,y1=(y+1)*h/nh;
                float color[3]={0};unsigned samples=(x1-x0)*(y1-y0);
                for(uint32_t sy=y0;sy<y1;++sy) for(uint32_t sx=x0;sx<x1;++sx) {
                    uint32_t p=data[previous+sy*w+sx];
                    color[0]+=linear[(p>>16)&255];color[1]+=linear[(p>>8)&255];color[2]+=linear[p&255];
                }
                data[offset+y*nw+x]=gfx_texture_encode(color[0]/samples)<<16 |
                    gfx_texture_encode(color[1]/samples)<<8 | gfx_texture_encode(color[2]/samples);
            }
            previous=offset;offset+=nw*nh;w=nw;h=nh;++levels;
        }
        data[image*4+3]=levels;
    }
    *word_count=words;return data;
}
int rf_gpu_graphics_resource_texture_set(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_texture_image *images,uint32_t count)
{
    struct rf_graphics_texture_set *set=NULL;
    struct rf_gpu_vulkan_buffer staging={0};
    struct rf_vk_buffer_copy copy={0};struct rf_vk_memory_barrier barrier={0};
    uint32_t words=0,*data=NULL;void *mapped=NULL;int result=-1;
    if (!g || g->poisoned || g->scene_in_flight || !resource || resource->owner!=g ||
        resource->shared_binding.owner || !(data=gfx_texture_set_pack(images,count,&words))) return -1;
    set=calloc(1,sizeof(*set));if(!set)goto done;
    set->references=1;set->count=count;
    for(uint32_t i=0;i<count;++i){set->width[i]=images[i].width;set->height[i]=images[i].height;}
    if (gpu_buffer_create(g->owner,(uint64_t)words*4,RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&staging)<0 ||
        gpu_buffer_create(g->owner,(uint64_t)words*4,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,&set->buffer)<0 ||
        g->owner->api.map_memory(g->owner->device,staging.memory,0,staging.allocation_size,0,&mapped)!=RF_VK_SUCCESS)
        goto done;
    memcpy(mapped,data,(size_t)words*4);
    if (!(staging.memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        struct rf_vk_mapped_memory_range range={0};range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=staging.memory;range.size=staging.allocation_size;
        if(g->owner->api.flush_mapped_memory_ranges(g->owner->device,1,&range)!=RF_VK_SUCCESS)goto done;
    }
    g->owner->api.unmap_memory(g->owner->device,staging.memory);mapped=NULL;
    copy.size=(uint64_t)words*4;if(gfx_begin(g)<0)goto done;
    g->owner->api.cmd_copy_buffer(g->cmd,staging.buffer,set->buffer.buffer,1,&copy);
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    if(gfx_submit(g,1,RF_GPU_SUBMIT_UPLOAD)<0)goto done;
    result=gfx_texture_set_bind(g,resource,set);
    if(!result)g->stats.texture_upload_bytes+=(uint64_t)words*4;
done:
    if(mapped)g->owner->api.unmap_memory(g->owner->device,staging.memory);
    if(g->poisoned)g->owner->api.queue_wait_idle(g->owner->queue);
    gpu_buffer_destroy(g->owner,&staging);free(data);
    if(set)gfx_texture_set_release(g,set);
    return result;
}
int rf_gpu_graphics_resource_share_textures(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,const struct rf_gpu_graphics_resource *source)
{
    if(!source || source->owner!=g || !source->texture_set)return -1;
    return gfx_texture_set_bind(g,resource,source->texture_set);
}
