/* Per-frame-slot lighting owner. The depth-to-buffer copies remain entirely on
 * the device; manual PCF avoids sampled-depth format/filter dependencies. */
struct rf_lighting_upload {
    float sun_direction[4],sun_color[4],environment[4],counts[4];
    float shadow_matrix[RF_GPU_SHADOW_MAPS][16],cascade_center[3][4];
    struct rf_gpu_light lights[RF_GPU_LIGHT_CAP];
};
_Static_assert(offsetof(struct rf_lighting_upload,lights)==432,"lighting std430 ABI");

void rf_gpu_lighting_default(struct rf_gpu_lighting *l)
{
    memset(l,0,sizeof(*l));
    l->sun_direction[0]=-0.4082483f;l->sun_direction[1]=0.8164966f;l->sun_direction[2]=-0.4082483f;
    l->sun_color[0]=1.0f;l->sun_color[1]=0.94f;l->sun_color[2]=0.84f;l->sun_color[3]=3.5f;
    l->environment[0]=0.10f;l->environment[1]=0.13f;l->environment[2]=0.18f;l->environment[3]=1.0f;
}
int rf_gpu_graphics_set_lighting(struct rf_gpu_graphics *g,const struct rf_gpu_lighting *l)
{
    if(!g || !l || g->scene_in_flight || g->poisoned || l->count>RF_GPU_LIGHT_CAP) return -1;
    for(unsigned k=0;k<4;++k)
        if(!__builtin_isfinite(l->sun_direction[k]) || !__builtin_isfinite(l->sun_color[k]) ||
            !__builtin_isfinite(l->environment[k]) || l->sun_color[k]<0 || l->environment[k]<0) return -1;
    float length=0;for(unsigned k=0;k<3;++k) length+=l->sun_direction[k]*l->sun_direction[k];
    if(length<0.01f || l->environment[3]<=0) return -1;
    for(unsigned i=0;i<l->count;++i) {
        const float *p=(const float *)&l->lights[i];
        for(unsigned k=0;k<16;++k) if(!__builtin_isfinite(p[k])) return -1;
        if(l->lights[i].position_radius[3]<=0 || l->lights[i].color_intensity[3]<0) return -1;
        for(unsigned k=0;k<3;++k) if(l->lights[i].color_intensity[k]<0) return -1;
        if(l->lights[i].direction_outer[3]>=0) {
            float norm=0;for(unsigned k=0;k<3;++k) norm+=l->lights[i].direction_outer[k]*l->lights[i].direction_outer[k];
            if(norm<0.01f || l->lights[i].direction_outer[3]>=1 ||
                l->lights[i].inner_shadow[0]<=l->lights[i].direction_outer[3] ||
                l->lights[i].inner_shadow[0]>1) return -1;
        }
    }
    g->lighting=*l;length=(float)sqrt(length);
    for(unsigned k=0;k<3;++k) g->lighting.sun_direction[k]/=length;
    for(unsigned i=0;i<g->lighting.count;++i) {
        struct rf_gpu_light *light=&g->lighting.lights[i];
        if(light->direction_outer[3]<0) continue;
        float norm=0;for(unsigned k=0;k<3;++k) norm+=light->direction_outer[k]*light->direction_outer[k];
        norm=(float)sqrt(norm);for(unsigned k=0;k<3;++k) light->direction_outer[k]/=norm;
    }
    return 0;
}
static void gfx_lighting_destroy(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    if(g->shadow_framebuffer) g->api.DestroyFramebuffer(p->device,g->shadow_framebuffer,NULL);
    gfx_image_destroy(g,&g->shadow_depth);
    if(g->shadow_pass) g->api.DestroyRenderPass(p->device,g->shadow_pass,NULL);
    if(g->overlay_pass) g->api.DestroyRenderPass(p->device,g->overlay_pass,NULL);
    if(g->tonemap) p->api.destroy_pipeline(p->device,g->tonemap,NULL);
    if(g->post_layout) p->api.destroy_pipeline_layout(p->device,g->post_layout,NULL);
    if(g->light_pool) p->api.destroy_descriptor_pool(p->device,g->light_pool,NULL);
    if(g->light_layout) p->api.destroy_descriptor_set_layout(p->device,g->light_layout,NULL);
    gpu_buffer_destroy(p,&g->light_buffer);gpu_buffer_destroy(p,&g->shadow_buffer);
}
static int gfx_lighting_create(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    struct rf_vk_descriptor_set_layout_binding b[4]={{0}};
    struct rf_vk_descriptor_set_layout_create_info layout={0};
    struct rf_vk_descriptor_pool_size sizes[2]={{RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2},{3,2}};
    struct rf_vk_descriptor_pool_create_info pool={0};
    struct rf_vk_descriptor_set_allocate_info set={0};
    struct rf_vk_descriptor_buffer_info buffers[2]={{0}};
    struct rf_vk_write_descriptor_set writes[2]={{0}};
    rf_gpu_lighting_default(&g->lighting);
    for(unsigned i=0;i<4;++i) {
        b[i].binding=i;b[i].descriptor_count=1;b[i].descriptor_type=i<2?RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:3;
        b[i].stage_flags=i<2?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT:VK_SHADER_STAGE_COMPUTE_BIT;
    }
    layout.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;layout.binding_count=4;layout.bindings=b;
    if(p->api.create_descriptor_set_layout(p->device,&layout,NULL,&g->light_layout)!=RF_VK_SUCCESS) return -1;
    pool.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.max_sets=1;pool.pool_size_count=2;pool.pool_sizes=sizes;
    if(p->api.create_descriptor_pool(p->device,&pool,NULL,&g->light_pool)!=RF_VK_SUCCESS) return -1;
    set.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set.descriptor_pool=g->light_pool;
    set.descriptor_set_count=1;set.set_layouts=&g->light_layout;
    if(p->api.allocate_descriptor_sets(p->device,&set,&g->light_set)!=RF_VK_SUCCESS ||
        gpu_buffer_create(p,sizeof(struct rf_lighting_upload),RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,&g->light_buffer)<0 ||
        gpu_buffer_create(p,(uint64_t)RF_GPU_SHADOW_MAPS*1024*1024*4,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&g->shadow_buffer)<0) return -1;
    buffers[0].buffer=g->light_buffer.buffer;buffers[0].range=g->light_buffer.size;
    buffers[1].buffer=g->shadow_buffer.buffer;buffers[1].range=g->shadow_buffer.size;
    for(unsigned i=0;i<2;++i) {
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dst_set=g->light_set;
        writes[i].dst_binding=i;writes[i].descriptor_count=1;writes[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].buffer_info=&buffers[i];
    }
    p->api.update_descriptor_sets(p->device,2,writes,0,NULL);
    return 0;
}
static void gfx_lighting_images(struct rf_gpu_graphics *g)
{
    struct rf_vk_descriptor_image_info images[2]={{0}};
    struct rf_vk_write_descriptor_set writes[2]={{0}};
    images[0].image_view=(rf_vk_image_view)g->target.hdr.view;
    images[1].image_view=(rf_vk_image_view)g->target.color.view;
    for(unsigned i=0;i<2;++i) {
        images[i].image_layout=VK_IMAGE_LAYOUT_GENERAL;
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dst_set=g->light_set;
        writes[i].dst_binding=i+2;writes[i].descriptor_count=1;writes[i].descriptor_type=3;
        writes[i].image_info=&images[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device,2,writes,0,NULL);
}
static int gfx_lighting_pipelines(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    VkFramebufferCreateInfo fb={0};
    struct rf_vk_shader_module_create_info shader={0};rf_vk_shader_module module=NULL;
    struct rf_vk_pipeline_layout_create_info layout={0};
    struct rf_vk_compute_pipeline_create_info pipeline={0};
    rf_vk_descriptor_set_layout sets[2]={g->set_layout,g->light_layout};
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    if(gfx_image_create(g,&g->shadow_depth,1024,1024,VK_FORMAT_D32_SFLOAT,VK_IMAGE_ASPECT_DEPTH_BIT,
            VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT)<0) return -1;
    fb.sType=VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;fb.renderPass=g->shadow_pass;
    fb.attachmentCount=1;fb.pAttachments=&g->shadow_depth.view;fb.width=fb.height=1024;fb.layers=1;
    if(g->api.CreateFramebuffer(p->device,&fb,NULL,&g->shadow_framebuffer)!=RF_VK_SUCCESS) return -1;
    layout.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;layout.set_layout_count=2;layout.set_layouts=sets;
    layout.push_constant_range_count=1;layout.push_constant_ranges=&push;
    if(p->api.create_pipeline_layout(p->device,&layout,NULL,&g->post_layout)!=RF_VK_SUCCESS) return -1;
    shader.s_type=RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    shader.code_size=sizeof(rf_graphics_tonemap_comp_spirv);shader.code=rf_graphics_tonemap_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS) return -1;
    pipeline.s_type=RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.stage.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=module;pipeline.stage.name="main";
    pipeline.layout=g->post_layout;pipeline.base_pipeline_index=-1;
    int result=p->api.create_compute_pipelines(p->device,NULL,1,&pipeline,NULL,&g->tonemap);
    p->api.destroy_shader_module(p->device,module,NULL);
    return result==RF_VK_SUCCESS?0:-1;
}
static int gfx_lighting_upload(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_batch_item *items,uint32_t count)
{
    struct rf_lighting_upload u={0};void *mapped=NULL;
    const struct rf_gpu_graphics_draw *camera=&items[0].draw;
    for(unsigned i=0;i<count;++i) if(!items[i].draw.texture[3]) {camera=&items[i].draw;break;}
    memcpy(u.sun_direction,g->lighting.sun_direction,16);memcpy(u.sun_color,g->lighting.sun_color,16);
    memcpy(u.environment,g->lighting.environment,16);u.counts[0]=(float)g->lighting.count;
    memcpy(u.lights,g->lighting.lights,sizeof(u.lights));
    float *l=u.sun_direction,right[3]={l[2],0,-l[0]},up[3];
    float length=(float)sqrt(right[0]*right[0]+right[2]*right[2]);
    if(length<0.01f) {right[0]=1;right[2]=0;} else {right[0]/=length;right[2]/=length;}
    up[0]=l[1]*right[2];up[1]=l[2]*right[0]-l[0]*right[2];up[2]=-l[1]*right[0];
    const float radii[3]={6144,20480,65536};
    for(unsigned c=0;c<3;++c) {
        float r=radii[c],zrange=131072.0f,cx=0,cy=0,cz=0;
        for(unsigned k=0;k<3;++k) {cx+=right[k]*camera->camera[k];cy+=up[k]*camera->camera[k];cz+=l[k]*camera->camera[k];}
        float texel=2*r/1024;cx=(float)__builtin_floor(cx/texel)*texel;cy=(float)__builtin_floor(cy/texel)*texel;
        for(unsigned k=0;k<3;++k) {u.shadow_matrix[c][k*4]=right[k]/r;u.shadow_matrix[c][k*4+1]=up[k]/r;u.shadow_matrix[c][k*4+2]=-l[k]/zrange;}
        u.shadow_matrix[c][12]=-cx/r;u.shadow_matrix[c][13]=-cy/r;
        u.shadow_matrix[c][14]=0.5f+cz/zrange;u.shadow_matrix[c][15]=1;
        u.cascade_center[c][3]=r;
    }
    g->shadow_count=3;
    for(unsigned i=0;i<g->lighting.count;++i) {
        struct rf_gpu_light *light=&u.lights[i];light->inner_shadow[1]=0;
        if(light->direction_outer[3]<0 || g->shadow_count>=RF_GPU_SHADOW_MAPS) continue;
        float f[3],r[3],v[3],len=0;
        for(unsigned k=0;k<3;++k) len+=light->direction_outer[k]*light->direction_outer[k];
        if(len<0.01f) continue;
        len=(float)sqrt(len);for(unsigned k=0;k<3;++k) f[k]=light->direction_outer[k]/len;
        if(fabs(f[1])>0.99) {r[0]=1;r[1]=r[2]=0;}
        else {len=(float)sqrt(f[0]*f[0]+f[2]*f[2]);r[0]=f[2]/len;r[1]=0;r[2]=-f[0]/len;}
        v[0]=f[1]*r[2]-f[2]*r[1];v[1]=f[2]*r[0]-f[0]*r[2];v[2]=f[0]*r[1]-f[1]*r[0];
        float cosine=light->direction_outer[3];if(cosine<0.05f) cosine=0.05f;if(cosine>0.99f) cosine=0.99f;
        float scale=cosine/(float)sqrt(1-cosine*cosine);
        float depth_far=light->position_radius[3],depth_near=16;
        if(depth_far<=depth_near) continue;
        unsigned slot=g->shadow_count++;light->inner_shadow[1]=(float)slot;
        float *m=u.shadow_matrix[slot];
        for(unsigned k=0;k<3;++k) {
            m[k*4]=r[k]*scale;m[k*4+1]=v[k]*scale;m[k*4+2]=f[k]*depth_far/(depth_far-depth_near);m[k*4+3]=f[k];
            for(unsigned row=0;row<4;++row) m[12+row]-=m[k*4+row]*light->position_radius[k];
        }
        m[14]-=depth_far*depth_near/(depth_far-depth_near);
    }
    if(g->owner->api.map_memory(g->owner->device,g->light_buffer.memory,0,g->light_buffer.allocation_size,0,&mapped)!=RF_VK_SUCCESS) return -1;
    memcpy(mapped,&u,sizeof(u));g->owner->api.unmap_memory(g->owner->device,g->light_buffer.memory);
    return 0;
}
static void gfx_shadow_record(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_batch_item *items,uint32_t count)
{
    VkClearValue clear={0};clear.depthStencil.depth=1;
    VkRenderPassBeginInfo begin={0};VkViewport viewport={0,0,1024,1024,0,1};
    VkRect2D scissor={{0,0},{1024,1024}};uint64_t offset=0;
    begin.sType=VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;begin.renderPass=g->shadow_pass;
    begin.framebuffer=g->shadow_framebuffer;begin.renderArea.extent=scissor.extent;
    begin.clearValueCount=1;begin.pClearValues=&clear;
    for(unsigned cascade=0;cascade<g->shadow_count;++cascade) {
        g->api.CmdBeginRenderPass(g->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
        g->api.CmdSetViewport(g->cmd,0,1,&viewport);g->api.CmdSetScissor(g->cmd,0,1,&scissor);
        g->owner->api.cmd_bind_descriptor_sets(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->layout,1,1,&g->light_set,0,NULL);
        for(unsigned i=0;i<count;++i) {
            struct rf_gpu_graphics_draw d=items[i].draw;
            if(d.scene_layer!=RF_GPU_SCENE_WORLD || d.texture[2] || d.texture[3]) continue;
            struct rf_gpu_graphics_resource *r=items[i].resource;d.camera[3]=(int)cascade;
            g->owner->api.cmd_bind_pipeline(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->pipelines[r->scene_vertex_color?31:30]);
            g->api.CmdBindVertexBuffers(g->cmd,0,1,&r->vertices.buffer,&offset);
            g->api.CmdBindIndexBuffer(g->cmd,r->indices.buffer,0,VK_INDEX_TYPE_UINT32);
            g->api.CmdPushConstants(g->cmd,g->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128,&d);
            g->api.CmdDrawIndexed(g->cmd,d.index_count,1,d.first_index,0,0);
        }
        g->api.CmdEndRenderPass(g->cmd);
        VkBufferImageCopy copy={0};copy.bufferOffset=(uint64_t)cascade*1024*1024*4;
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;copy.imageSubresource.layerCount=1;
        copy.imageExtent.width=copy.imageExtent.height=1024;copy.imageExtent.depth=1;
        g->api.CmdCopyImageToBuffer(g->cmd,g->shadow_depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,g->shadow_buffer.buffer,1,&copy);
    }
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
}
static void gfx_tonemap_record(struct rf_gpu_graphics *g)
{
    struct rf_vk_image_memory_barrier barrier={0};
    barrier.s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;barrier.old_layout=VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.new_layout=VK_IMAGE_LAYOUT_GENERAL;barrier.dst_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.src_queue_family_index=barrier.dst_queue_family_index=UINT32_MAX;
    barrier.image=g->target.color.image;barrier.subresource_range.aspect_mask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresource_range.level_count=barrier.subresource_range.layer_count=1;
    g->owner->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&barrier);
    g->owner->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->tonemap);
    g->owner->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&g->lighting.environment[3]);
    g->owner->api.cmd_dispatch(g->cmd,(g->target.width+7)/8,(g->target.height+7)/8,1);
}
