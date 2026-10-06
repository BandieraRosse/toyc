/* Per-frame-slot lighting owner. The depth-to-buffer copies remain entirely on
 * the device; manual PCF avoids sampled-depth format/filter dependencies. */
#define RF_LIGHT_DEPTH_SLICES 32u
struct rf_lighting_upload {
    float sun_direction[4],sun_color[4],environment[4],counts[4];
    float shadow_matrix[RF_GPU_SHADOW_MAPS][16],cascade_center[3][4];
    struct rf_gpu_light lights[RF_GPU_LIGHT_CAP];
    float sky_cloud[4],sky_weather[4];
    float cutaway_bounds[4],cutaway_height[4];
    uint32_t tile_grid[4];
    int32_t tile_camera[4],tile_view[4],tile_projection[4];
    uint32_t light_control[4];
    float daylight[4];
};
_Static_assert(offsetof(struct rf_lighting_upload,lights)==432,"lighting std430 ABI");
_Static_assert(offsetof(struct rf_lighting_upload,sky_cloud)==10672,"sky std430 ABI");
_Static_assert(offsetof(struct rf_lighting_upload,tile_grid)==10736,"light tiles std430 ABI");

/* One internal linear-radiance unit represents 100 cd/m^2. All lighting,
 * including the persistent GI field, uses this same exposure-independent scale. */
static float light_luminance(const float *rgb)
{ return rgb[0]*0.2126f+rgb[1]*0.7152f+rgb[2]*0.0722f; }
static void light_chromaticity(float *rgb)
{
    float y=light_luminance(rgb);
    if(y>0.0f)for(unsigned k=0;k<3;++k)rgb[k]/=y;
}
float rf_gpu_light_peak_candela(float lumens,float outer_cosine,float inner_cosine)
{
    if(!__builtin_isfinite(lumens) || lumens<0 || !__builtin_isfinite(outer_cosine) ||
       !__builtin_isfinite(inner_cosine))return -1;
    if(outer_cosine<0)return lumens/(4.0f*3.14159265359f);
    if(outer_cosine>=1 || inner_cosine<=outer_cosine || inner_cosine>1)return -1;
    float peak=lumens/(3.14159265359f*(2.0f-inner_cosine-outer_cosine));
    return __builtin_isfinite(peak)?peak:-1;
}

void rf_gpu_lighting_default(struct rf_gpu_lighting *l)
{
    memset(l,0,sizeof(*l));
    l->sun_direction[0]=-0.4082483f;l->sun_direction[1]=0.8164966f;l->sun_direction[2]=-0.4082483f;
    l->sun_color[0]=1.0f;l->sun_color[1]=0.94f;l->sun_color[2]=0.84f;
    /* Authored low-contrast outdoor preset, not a clear-sky solar measurement. */
    l->sun_color[3]=290.0f*light_luminance(l->sun_color);
    l->environment[0]=50.265482f;l->environment[1]=59.690260f;l->environment[2]=75.398224f;l->environment[3]=1.0f;
    l->sky_cloud[0]=0.42f;l->sky_cloud[1]=1.3f;l->sky_cloud[2]=1.5f;l->sky_cloud[3]=1.1f;
    l->sky_weather[2]=11.0f;l->sky_weather[3]=1.0f;
}
int rf_gpu_graphics_set_lighting(struct rf_gpu_graphics *g,const struct rf_gpu_lighting *l)
{
    if(!g || !l || g->scene_in_flight || g->poisoned || l->count>RF_GPU_LIGHT_CAP) return -1;
    for(unsigned k=0;k<4;++k)
        if(!__builtin_isfinite(l->sky_cloud[k]) || !__builtin_isfinite(l->sky_weather[k])) return -1;
    for(unsigned k=0;k<4;++k)
        if(!__builtin_isfinite(l->cutaway_bounds[k]) || !__builtin_isfinite(l->cutaway_height[k]))return -1;
    if(l->cutaway_height[1]<0 || l->cutaway_height[1]>1)return -1;
    if(!__builtin_isfinite(l->sky_illuminance) || l->sky_illuminance<0)return -1;
    if(l->sky_cloud[0]<0 || l->sky_cloud[0]>1 || l->sky_cloud[1]<0 || l->sky_cloud[1]>8 ||
        l->sky_cloud[2]<0.1f || l->sky_cloud[2]>10 || l->sky_cloud[3]<0.1f || l->sky_cloud[3]>5 ||
        l->sky_weather[3]<0.1f || l->sky_weather[3]>8) return -1;
    if(fabs(l->sky_weather[0])>100000 || fabs(l->sky_weather[1])>100000 ||
        fabs(l->sky_weather[2])>65536) return -1;
    for(unsigned k=0;k<4;++k)
        if(!__builtin_isfinite(l->sun_direction[k]) || !__builtin_isfinite(l->sun_color[k]) ||
            !__builtin_isfinite(l->environment[k]) || l->sun_color[k]<0 || l->environment[k]<0) return -1;
    float length=0;for(unsigned k=0;k<3;++k) length+=l->sun_direction[k]*l->sun_direction[k];
    if(!__builtin_isfinite(length) || length<0.01f || l->environment[3]<=0) return -1;
    if(l->sun_color[3]>0 && light_luminance(l->sun_color)<=0)return -1;
    for(unsigned i=0;i<l->count;++i) {
        const float *p=(const float *)&l->lights[i];
        for(unsigned k=0;k<16;++k) if(!__builtin_isfinite(p[k])) return -1;
        if(l->lights[i].position_radius[3]<=0 || l->lights[i].color_intensity[3]<0) return -1;
        for(unsigned k=0;k<3;++k) if(l->lights[i].color_intensity[k]<0) return -1;
        if(l->lights[i].color_intensity[3]>0 && light_luminance(l->lights[i].color_intensity)<=0)return -1;
        if(l->lights[i].direction_outer[3]>=0) {
            float norm=0;for(unsigned k=0;k<3;++k) norm+=l->lights[i].direction_outer[k]*l->lights[i].direction_outer[k];
            if(norm<0.01f || l->lights[i].direction_outer[3]>=1 ||
                l->lights[i].inner_shadow[0]<=l->lights[i].direction_outer[3] ||
                l->lights[i].inner_shadow[0]>1) return -1;
        }
    }
    g->lighting=*l;length=(float)sqrt(length);
    light_chromaticity(g->lighting.sun_color);
    for(unsigned k=0;k<3;++k) g->lighting.sun_direction[k]/=length;
    for(unsigned i=0;i<g->lighting.count;++i) {
        struct rf_gpu_light *light=&g->lighting.lights[i];
        light_chromaticity(light->color_intensity);
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
    if(g->light_tiles_pipeline)p->api.destroy_pipeline(p->device,g->light_tiles_pipeline,NULL);
    if(g->indirect_pipeline)p->api.destroy_pipeline(p->device,g->indirect_pipeline,NULL);
    if(g->daylight_pipeline)p->api.destroy_pipeline(p->device,g->daylight_pipeline,NULL);
    if(g->receiver_pipeline)p->api.destroy_pipeline(p->device,g->receiver_pipeline,NULL);
    gpu_buffer_destroy(p,&g->receiver_buffer);
    if(g->environment_pipeline)p->api.destroy_pipeline(p->device,g->environment_pipeline,NULL);
    gpu_buffer_destroy(p,&g->daylight_buffer);gpu_buffer_destroy(p,&g->environment_buffer);
    if(g->sky_pipeline) p->api.destroy_pipeline(p->device,g->sky_pipeline,NULL);
    if(g->sky_noise_pipeline) p->api.destroy_pipeline(p->device,g->sky_noise_pipeline,NULL);
    if(g->sky_layout) p->api.destroy_pipeline_layout(p->device,g->sky_layout,NULL);
    if(g->post_layout) p->api.destroy_pipeline_layout(p->device,g->post_layout,NULL);
    if(g->light_pool) p->api.destroy_descriptor_pool(p->device,g->light_pool,NULL);
    if(g->light_layout) p->api.destroy_descriptor_set_layout(p->device,g->light_layout,NULL);
    gpu_buffer_destroy(p,&g->light_buffer);gpu_buffer_destroy(p,&g->shadow_buffer);
    gpu_buffer_destroy(p,&g->architecture_buffer);
    gpu_buffer_destroy(p,&g->roof_buffer);gpu_buffer_destroy(p,&g->indirect_buffer);
    gpu_buffer_destroy(p,&g->indirect_geometry);gpu_buffer_destroy(p,&g->indirect_sources);
    free(g->indirect_architecture);g->indirect_architecture=NULL;
    architecture_ray_destroy(g);
    gpu_buffer_destroy(p,&g->sky_buffer);
    gpu_buffer_destroy(p,&g->light_tiles_buffer);gpu_buffer_destroy(p,&g->light_profile_buffer);
    gfx_image_destroy(g,&g->sky_noise);
    if(g->sky_sampler) g->api.DestroySampler(p->device,g->sky_sampler,NULL);
}
static int gfx_lighting_create(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    struct rf_vk_descriptor_set_layout_binding b[17]={{0}};
    struct rf_vk_descriptor_set_layout_create_info layout={0};
    struct rf_vk_descriptor_pool_size sizes[3]={{RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,13},{3,3},
        {RF_SKY_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,1}};
    struct rf_vk_descriptor_pool_create_info pool={0};
    struct rf_vk_descriptor_set_allocate_info set={0};
    struct rf_vk_descriptor_buffer_info buffers[2]={{0}};
    struct rf_vk_write_descriptor_set writes[4]={{0}};
    rf_gpu_lighting_default(&g->lighting);
    const char *tiles=getenv("RF_GPU_LIGHT_TILES"),*ablation=getenv("RF_GPU_LIGHT_ABLATION");
    if(tiles && strcmp(tiles,"0") && strcmp(tiles,"1"))return -1;
    g->light_tiles_enabled=!(tiles && !strcmp(tiles,"0"));
    const char *depth=getenv("RF_GPU_LIGHT_DEPTH");
    if(depth && strcmp(depth,"0") && strcmp(depth,"1"))return -1;
    g->light_depth_enabled=!(depth && !strcmp(depth,"0"));
    const char *clusters=getenv("RF_GPU_LIGHT_CLUSTERS");
    if(clusters && strcmp(clusters,"0") && strcmp(clusters,"1"))return -1;
    g->light_clusters_enabled=clusters && !strcmp(clusters,"1");
    fprintf(stderr,"rf-gpu-light: clusters=%d\n",g->light_clusters_enabled);
    const char *fp32=getenv("RF_GPU_RECEIVER_FP32");
    if(fp32 && strcmp(fp32,"0") && strcmp(fp32,"1"))return -1;
    g->receiver_fp32=fp32 && !strcmp(fp32,"1");
    fprintf(stderr,"rf-gpu-light: depth-slices=%u\n",g->light_depth_enabled?RF_LIGHT_DEPTH_SLICES:1u);
    const char *roof=getenv("RF_GPU_ROOF_CACHE"),*gi=getenv("RF_GPU_GI");
    if((roof && strcmp(roof,"0") && strcmp(roof,"1")) || (gi && strcmp(gi,"0") && strcmp(gi,"1")))return -1;
    g->roof_cache_enabled=!(roof && !strcmp(roof,"0"));
    g->indirect_enabled=!(gi && !strcmp(gi,"0"));
    const char *indirect_mode=getenv("RF_GPU_INDIRECT_MODE");
    if(indirect_mode && strcmp(indirect_mode,"reference") && strcmp(indirect_mode,"fast") && strcmp(indirect_mode,"direct_only_diag"))return -1;
    g->indirect_mode=!indirect_mode || !strcmp(indirect_mode,"fast")?1:
        indirect_mode && !strcmp(indirect_mode,"direct_only_diag")?2:0;
    if(g->indirect_mode==2)g->indirect_enabled=0;
    fprintf(stderr,"rf-gpu-indirect-mode: %s\n",g->indirect_mode==1?"fast":g->indirect_mode==2?"direct_only_diag":"reference");
    const char *daylight=getenv("RF_GPU_DAYLIGHT");
    if(daylight && strcmp(daylight,"0") && strcmp(daylight,"1"))return -1;
    g->daylight_enabled=!(daylight && !strcmp(daylight,"0"));
    fprintf(stderr,"rf-gpu-daylight: enabled=%d\n",g->daylight_enabled);
    const char *bounces=getenv("RF_GPU_GI_BOUNCES");
    if(bounces && strcmp(bounces,"1") && strcmp(bounces,"2"))return -1;
    g->indirect_bounces=bounces && !strcmp(bounces,"1")?1:2;
    const char *gi_debug=getenv("RF_GPU_GI_DEBUG");
    if(gi_debug && strcmp(gi_debug,"0") && strcmp(gi_debug,"1") && strcmp(gi_debug,"2") && strcmp(gi_debug,"3"))return -1;
    g->indirect_debug=gi_debug?atoi(gi_debug):0;
    const char *visibility=getenv("RF_GPU_GI_VISIBILITY");
    if(visibility && strcmp(visibility,"0") && strcmp(visibility,"1") && strcmp(visibility,"2"))return -1;
    g->indirect_visibility=visibility?atoi(visibility):1;
    const char *reuse=getenv("RF_GPU_GI_REUSE");
    if(reuse && strcmp(reuse,"0") && strcmp(reuse,"1"))return -1;
    g->indirect_reuse=!(reuse && !strcmp(reuse,"0"));
    fprintf(stderr,"rf-gpu-gi: shared-visibility=%d\n",g->indirect_reuse);
    fprintf(stderr,"rf-gpu-indirect: roof-cache=%d probe-gi=%d bounces=%d\n",g->roof_cache_enabled,g->indirect_enabled,g->indirect_bounces);
    const char *modes[]={"none","roof","sun","local","pcf","brdf","rays","receiver","receiver_read"};
    const unsigned masks[]={0,1,2,4,8,16,7,32,64};
    if(ablation && *ablation) {
        unsigned i=0;for(;i<sizeof(masks)/sizeof(masks[0]) && strcmp(ablation,modes[i]);++i){}
        if(i==sizeof(masks)/sizeof(masks[0]))return -1;
        g->light_ablation=masks[i];
    }
    fprintf(stderr,"rf-gpu-light: tiles=%d profile=%d ablation=%u\n",
        g->light_tiles_enabled,p->light_profile_enabled,g->light_ablation);
    g->sky_scale=4;
    const char *reference=getenv("RF_GPU_SKY_REFERENCE_SCAN");
    // Keep the measured baseline until empty-space skipping wins reliably.
    g->sky_reference_scan=!(reference && reference[0]=='0' && !reference[1]);
    const char *scale=getenv("RF_GPU_SKY_SCALE");
    if(scale && scale[0]) {
        if(strcmp(scale,"1") && strcmp(scale,"2") && strcmp(scale,"4")) return -1;
        g->sky_scale=(unsigned)(scale[0]-'0');
    }
    for(unsigned i=0;i<7;++i) {
        b[i].binding=i;b[i].descriptor_count=1;b[i].descriptor_type=(i<2 || i==4)?RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER:3;
        if(i==5) b[i].descriptor_type=RF_SKY_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b[i].stage_flags=(i<2 || i==4)?VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT:VK_SHADER_STAGE_COMPUTE_BIT;
    }
    b[7].binding=7;b[7].descriptor_count=1;b[7].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    b[7].stage_flags=VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    if(p->ray_query_enabled) {
        b[7].descriptor_type=RF_RT_DESCRIPTOR;
        sizes[0].descriptor_count=12;
    }
    for(unsigned i=8;i<17;++i) {
        b[i].binding=i;b[i].descriptor_count=1;b[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        b[i].stage_flags=VK_SHADER_STAGE_FRAGMENT_BIT|VK_SHADER_STAGE_COMPUTE_BIT;
    }
    layout.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;layout.binding_count=17;layout.bindings=b;
    if(p->api.create_descriptor_set_layout(p->device,&layout,NULL,&g->light_layout)!=RF_VK_SUCCESS) return -1;
    struct rf_vk_descriptor_pool_size rt_sizes[4];memcpy(rt_sizes,sizes,sizeof(sizes));
    rt_sizes[3].type=RF_RT_DESCRIPTOR;rt_sizes[3].descriptor_count=1;
    pool.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;pool.max_sets=1;
    pool.pool_size_count=p->ray_query_enabled?4:3;pool.pool_sizes=rt_sizes;
    if(p->api.create_descriptor_pool(p->device,&pool,NULL,&g->light_pool)!=RF_VK_SUCCESS) return -1;
    set.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;set.descriptor_pool=g->light_pool;
    set.descriptor_set_count=1;set.set_layouts=&g->light_layout;
    if(p->api.allocate_descriptor_sets(p->device,&set,&g->light_set)!=RF_VK_SUCCESS ||
        gpu_buffer_create(p,sizeof(struct rf_lighting_upload),RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,&g->light_buffer)<0 ||
        gfx_image_create_extent(g,&g->sky_noise,64,64,64,VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_USAGE_STORAGE_BIT|RF_SKY_IMAGE_USAGE_SAMPLED_BIT)<0 ||
        gpu_buffer_create(p,(uint64_t)RF_GPU_SHADOW_MAPS*1024*1024*4,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&g->shadow_buffer)<0) return -1;
    buffers[0].buffer=g->light_buffer.buffer;buffers[0].range=g->light_buffer.size;
    buffers[1].buffer=g->shadow_buffer.buffer;buffers[1].range=g->shadow_buffer.size;
    for(unsigned i=0;i<2;++i) {
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i].dst_set=g->light_set;
        writes[i].dst_binding=i;writes[i].descriptor_count=1;writes[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        writes[i].buffer_info=&buffers[i];
    }
    VkFormatProperties format;
    g->api.GetPhysicalDeviceFormatProperties(p->physical_device,VK_FORMAT_R16G16B16A16_SFLOAT,&format);
    if(!(format.optimalTilingFeatures&RF_SKY_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT)) return -1;
    struct rf_sky_sampler_info sampler={0};
    sampler.s_type=RF_SKY_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;sampler.mag_filter=sampler.min_filter=1;
    if(g->api.CreateSampler(p->device,&sampler,NULL,&g->sky_sampler)!=RF_VK_SUCCESS) return -1;
    struct rf_vk_descriptor_image_info images[2]={{0}};
    for(unsigned i=0;i<2;++i) {
        images[i].image_view=(rf_vk_image_view)g->sky_noise.view;images[i].image_layout=VK_IMAGE_LAYOUT_GENERAL;
        if(!i) images[i].sampler=g->sky_sampler;
        writes[i+2].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;writes[i+2].dst_set=g->light_set;
        writes[i+2].dst_binding=i+5;writes[i+2].descriptor_count=1;
        writes[i+2].descriptor_type=i?3:RF_SKY_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        writes[i+2].image_info=&images[i];
    }
    p->api.update_descriptor_sets(p->device,4,writes,0,NULL);
    struct rf_gpu_vulkan_buffer environment={0};float empty_environment[(512+7)*4]={0};
    if(indirect_upload(g,empty_environment,sizeof(empty_environment),&environment)<0)return -1;
    indirect_bind(g,14,&g->environment_buffer,&environment);
    return rf_gpu_graphics_set_architecture(g,NULL,0);
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
    int result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->tonemap);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS) return -1;
    shader.code_size=sizeof(rf_graphics_light_tiles_comp_spirv);shader.code=rf_graphics_light_tiles_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS)return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->light_tiles_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS)return -1;
    shader.code_size=p->ray_query_enabled?sizeof(rf_graphics_indirect_ray_comp_spirv):sizeof(rf_graphics_indirect_comp_spirv);
    shader.code=p->ray_query_enabled?rf_graphics_indirect_ray_comp_spirv:rf_graphics_indirect_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS)return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->indirect_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS)return -1;
    shader.code_size=p->ray_query_enabled?sizeof(rf_graphics_daylight_ray_comp_spirv):sizeof(rf_graphics_daylight_comp_spirv);
    shader.code=p->ray_query_enabled?rf_graphics_daylight_ray_comp_spirv:rf_graphics_daylight_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS)return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->daylight_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS)return -1;
    shader.code_size=p->ray_query_enabled?sizeof(rf_graphics_receiver_ray_comp_spirv):sizeof(rf_graphics_receiver_comp_spirv);
    shader.code=p->ray_query_enabled?rf_graphics_receiver_ray_comp_spirv:rf_graphics_receiver_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS)return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->receiver_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS)return -1;
    shader.code_size=sizeof(rf_graphics_environment_comp_spirv);shader.code=rf_graphics_environment_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS)return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->environment_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS)return -1;
    push.size=128;
    if(p->api.create_pipeline_layout(p->device,&layout,NULL,&g->sky_layout)!=RF_VK_SUCCESS) return -1;
    shader.code_size=sizeof(rf_graphics_sky_comp_spirv);shader.code=rf_graphics_sky_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS) return -1;
    pipeline.stage.module=module;pipeline.layout=g->sky_layout;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->sky_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    if(result!=RF_VK_SUCCESS) return -1;
    shader.code_size=sizeof(rf_graphics_sky_noise_comp_spirv);shader.code=rf_graphics_sky_noise_comp_spirv;
    if(p->api.create_shader_module(p->device,&shader,NULL,&module)!=RF_VK_SUCCESS) return -1;
    pipeline.stage.module=module;
    result=p->api.create_compute_pipelines(p->device,p->pipeline_cache,1,&pipeline,NULL,&g->sky_noise_pipeline);
    p->api.destroy_shader_module(p->device,module,NULL);
    return result==RF_VK_SUCCESS?0:-1;
}
/* Per-slot targets: retired before resize. Only diagnostic counters read back.
 * Allocate every buffer before replacing any live descriptor/extent. */
static int gfx_sky_resize(struct rf_gpu_graphics *g,unsigned width,unsigned height)
{
    struct rf_gpu_vulkan_buffer tiles={0},profile={0},next={0};
    unsigned tw=(width+15)/16,th=(height+15)/16;
    unsigned clusters=g->light_clusters_enabled?RF_LIGHT_DEPTH_SLICES:0;
    if(gpu_buffer_create(g->owner,((uint64_t)tw*th*(1+clusters)+RF_LIGHT_DEPTH_SLICES)*RF_GPU_LIGHT_WORDS*4,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&tiles)<0)return -1;
    if(gpu_buffer_create(g->owner,g->owner->light_profile_enabled?(uint64_t)tw*th*7*4:4,
        RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|
        RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,&profile)<0) {
        gpu_buffer_destroy(g->owner,&tiles);return -1;
    }
    unsigned w=(width+g->sky_scale-1)/g->sky_scale,h=(height+g->sky_scale-1)/g->sky_scale;
    if(gpu_buffer_create(g->owner,(uint64_t)w*h*16,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,&next)<0) {
        gpu_buffer_destroy(g->owner,&tiles);gpu_buffer_destroy(g->owner,&profile);return -1;
    }
    gpu_buffer_destroy(g->owner,&g->light_tiles_buffer);g->light_tiles_buffer=tiles;
    gpu_buffer_destroy(g->owner,&g->light_profile_buffer);g->light_profile_buffer=profile;
    g->light_tiles_width=tw;g->light_tiles_height=th;
    for(unsigned i=0;i<2;++i) {
        struct rf_gpu_vulkan_buffer *b=i?&profile:&tiles;
        struct rf_vk_descriptor_buffer_info info={0};info.buffer=b->buffer;info.range=b->size;
        struct rf_vk_write_descriptor_set write={0};write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dst_set=g->light_set;write.dst_binding=8+i;write.descriptor_count=1;
        write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.buffer_info=&info;
        g->owner->api.update_descriptor_sets(g->owner->device,1,&write,0,NULL);
    }
    gpu_buffer_destroy(g->owner,&g->sky_buffer);g->sky_buffer=next;g->sky_width=w;g->sky_height=h;
    struct rf_vk_descriptor_buffer_info buffer={0};struct rf_vk_write_descriptor_set write={0};
    buffer.buffer=next.buffer;buffer.range=next.size;
    write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dst_set=g->light_set;
    write.dst_binding=4;write.descriptor_count=1;write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer_info=&buffer;g->owner->api.update_descriptor_sets(g->owner->device,1,&write,0,NULL);
    return 0;
}
static void gfx_sky_noise_record(struct rf_gpu_graphics *g)
{
    g->owner->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_layout,1,1,&g->light_set,0,NULL);
    if(!g->sky_noise_ready) {
        struct rf_vk_image_memory_barrier image_barrier={0};
        image_barrier.s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
        image_barrier.old_layout=VK_IMAGE_LAYOUT_UNDEFINED;image_barrier.new_layout=VK_IMAGE_LAYOUT_GENERAL;
        image_barrier.dst_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
        image_barrier.src_queue_family_index=image_barrier.dst_queue_family_index=UINT32_MAX;
        image_barrier.image=g->sky_noise.image;image_barrier.subresource_range.aspect_mask=VK_IMAGE_ASPECT_COLOR_BIT;
        image_barrier.subresource_range.level_count=image_barrier.subresource_range.layer_count=1;
        g->owner->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,0,NULL,0,NULL,1,&image_barrier);
        g->owner->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_noise_pipeline);
        g->owner->api.cmd_dispatch(g->cmd,8,8,64);
        struct rf_vk_memory_barrier noise_barrier={0};noise_barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        noise_barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;noise_barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
        g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&noise_barrier,0,NULL,0,NULL);
        g->sky_noise_ready=1;
    }
}
static void gfx_environment_record(struct rf_gpu_graphics *g)
{
    if(g->indirect_mode==2 || (g->indirect_mode==1 && !g->indirect_enabled) || !g->daylight_enabled ||
        g->lighting.sky_illuminance<=0 || g->lighting.disable_indirect)return;
    gfx_sky_noise_record(g);
    struct rf_gpu_vulkan_impl *p=g->owner;
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->environment_pipeline);
    for(uint32_t phase=0;phase<2;++phase) {
        g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&phase);
        p->api.cmd_dispatch(g->cmd,phase?1:8,1,1);
        struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
        p->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    }
}
static void gfx_sky_record(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_batch_item *items,unsigned count)
{
    for(unsigned i=0;i<count;++i) if(items[i].draw.scene_layer==RF_GPU_SCENE_SKY) {
        g->owner->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_layout,1,1,&g->light_set,0,NULL);
        gfx_sky_noise_record(g);
        g->owner->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->sky_pipeline);
        struct rf_gpu_graphics_draw sky_draw=items[i].draw;
        if(g->sky_reference_scan) sky_draw.quality[0]|=128;
        g->api.CmdPushConstants(g->cmd,g->sky_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,128,&sky_draw);
        g->owner->api.cmd_dispatch(g->cmd,(g->sky_width+7)/8,(g->sky_height+7)/8,1);
        struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
        g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
        break;
    }
}
static int gfx_lighting_upload(struct rf_gpu_graphics *g,const struct rf_gpu_graphics_batch_item *items,uint32_t count)
{
    struct rf_lighting_upload u={0};void *mapped=NULL;
    const struct rf_gpu_graphics_draw *camera=&items[0].draw;
    for(unsigned i=0;i<count;++i) if(!items[i].draw.texture[3]) {camera=&items[i].draw;break;}
    u.tile_grid[0]=g->light_tiles_width;u.tile_grid[1]=g->light_tiles_height;
    u.tile_grid[2]=g->light_tiles_enabled && camera->projection[3]>0?
        (g->light_depth_enabled?RF_LIGHT_DEPTH_SLICES:1u):0u;
    u.tile_grid[3]=g->owner->light_profile_enabled|
        (g->light_tiles_enabled && g->light_depth_enabled && g->light_clusters_enabled?2u:0u);
    memcpy(u.tile_camera,camera->camera,16);memcpy(u.tile_view,camera->view,16);
    u.tile_camera[3]=2048; /* 4 m depth bands; first/last include the unbounded tails. */
    memcpy(u.tile_projection,camera->projection,16);u.light_control[0]=g->light_ablation;
    u.light_control[1]=g->roof_cache_enabled;
    u.light_control[2]=g->indirect_enabled && !g->lighting.disable_indirect && !g->lighting_clone_invalid;
    u.light_control[3]=(uint32_t)g->indirect_debug|(g->indirect_visibility?256u:0u)|
        (g->indirect_visibility==1?512u:0u)|(g->indirect_reuse?1024u:0u)|((uint32_t)g->indirect_mode<<11);
    u.daylight[0]=g->daylight_enabled && !g->lighting.disable_indirect?g->lighting.sky_illuminance/RF_PHOTOMETRIC_SCALE:0;
    u.daylight[1]=131072.0f;u.daylight[2]=(float)g->architecture_count;
    if(g->indirect_architecture_nodes) {
        const struct rf_gi_arch_node *root=g->indirect_architecture;float diameter=0;
        for(unsigned k=0;k<3;++k)diameter+=root->hi[k]-root->lo[k];
        u.daylight[1]=__builtin_fmaxf(131072,diameter+8192);
    }
    memcpy(u.sun_direction,g->lighting.sun_direction,16);memcpy(u.sun_color,g->lighting.sun_color,16);
    memcpy(u.environment,g->lighting.environment,16);u.counts[0]=(float)g->lighting.count;
    u.counts[1]=(float)g->sky_scale;u.counts[2]=(float)g->sky_width;u.counts[3]=(float)g->sky_height;
    memcpy(u.lights,g->lighting.lights,sizeof(u.lights));
    u.sun_color[3]/=RF_PHOTOMETRIC_SCALE;
    for(unsigned k=0;k<3;++k)u.environment[k]/=3.14159265359f*RF_PHOTOMETRIC_SCALE;
    for(unsigned i=0;i<g->lighting.count;++i)u.lights[i].color_intensity[3]/=RF_PHOTOMETRIC_SCALE;
    memcpy(u.sky_cloud,g->lighting.sky_cloud,16);memcpy(u.sky_weather,g->lighting.sky_weather,16);
    memcpy(u.cutaway_bounds,g->lighting.cutaway_bounds,16);
    memcpy(u.cutaway_height,g->lighting.cutaway_height,16);
    float *l=u.sun_direction,right[3]={l[2],0,-l[0]},up[3];
    float length=(float)sqrt(right[0]*right[0]+right[2]*right[2]);
    if(length<0.01f) {right[0]=1;right[2]=0;} else {right[0]/=length;right[2]/=length;}
    up[0]=l[1]*right[2];up[1]=l[2]*right[0]-l[0]*right[2];up[2]=-l[1]*right[0];
    /* Keep the first 1024 map dense around nearby contacts (8 m radius).
     * The shader's texel-relative normal offset follows this same scale. */
    const float radii[3]={4096,20480,65536};
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
    for(unsigned i=0;i<g->lighting.count;++i)u.lights[i].inner_shadow[1]=0;
    /* Only dynamic shadow-map ownership follows the camera. Every fixed
     * fixture remains in the direct and indirect source sets on every floor. */
    for(unsigned slot_index=3;slot_index<RF_GPU_SHADOW_MAPS;++slot_index) {
        unsigned selected=RF_GPU_LIGHT_CAP;float best=-1;
        for(unsigned i=0;i<g->lighting.count;++i) {
            const struct rf_gpu_light *candidate=&u.lights[i];float r2=0;
            if(candidate->direction_outer[3]<0 || candidate->inner_shadow[1]!=0 ||
                candidate->position_radius[3]<=16 || candidate->color_intensity[3]<=0)continue;
            for(unsigned k=0;k<3;++k){float d=candidate->position_radius[k]-camera->camera[k];r2+=d*d;}
            float score=candidate->color_intensity[3]/(r2+512.0f*512.0f);
            if(score>best){best=score;selected=i;}
        }
        if(selected==RF_GPU_LIGHT_CAP)break;
        struct rf_gpu_light *light=&u.lights[selected];
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
    memcpy(g->shadow_matrix,u.shadow_matrix,sizeof(g->shadow_matrix));
    if(g->owner->api.map_memory(g->owner->device,g->light_buffer.memory,0,g->light_buffer.allocation_size,0,&mapped)!=RF_VK_SUCCESS) return -1;
    memcpy(mapped,&u,sizeof(u));g->owner->api.unmap_memory(g->owner->device,g->light_buffer.memory);
    return 0;
}
static void gfx_light_tiles_record(struct rf_gpu_graphics *g)
{
    if(!g->light_tiles_enabled && !g->owner->light_profile_enabled)return;
    struct rf_gpu_vulkan_impl *p=g->owner;
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->light_tiles_pipeline);
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    unsigned columns=g->light_tiles_width<RF_LIGHT_DEPTH_SLICES?RF_LIGHT_DEPTH_SLICES:g->light_tiles_width;
    unsigned pass=0;
    g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&pass);
    p->api.cmd_dispatch(g->cmd,(columns+7)/8,(g->light_tiles_height+7)/8,1);
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    if(g->light_tiles_enabled && g->light_depth_enabled && g->light_clusters_enabled) {
        p->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
        pass=1;g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&pass);
        p->api.cmd_dispatch(g->cmd,(g->light_tiles_width+7)/8,(g->light_tiles_height+7)/8,RF_LIGHT_DEPTH_SLICES);
    }
    p->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
}
static int gfx_light_profile_read(struct rf_gpu_graphics *g)
{
    g->stats.light_profile=g->owner->light_profile_enabled;
    g->stats.light_tiles=g->light_tiles_enabled;g->stats.light_ablation=g->light_ablation;
    memset(g->stats.light_counts,0,sizeof(g->stats.light_counts));
    if(!g->owner->light_profile_enabled)return 0;
    void *mapped=NULL;struct rf_gpu_vulkan_impl *p=g->owner;
    if(p->api.map_memory(p->device,g->light_profile_buffer.memory,0,
        g->light_profile_buffer.allocation_size,0,&mapped)!=RF_VK_SUCCESS)return -1;
    const uint32_t *data=mapped;
    for(unsigned i=0;i<g->light_tiles_width*g->light_tiles_height;++i)
        for(unsigned k=0;k<7;++k)g->stats.light_counts[k]+=data[i*7+k];
    p->api.unmap_memory(p->device,g->light_profile_buffer.memory);
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
    const char *legacy_bind=getenv("RF_GPU_SCENE_LEGACY_BIND");
    int reuse_bind=!(legacy_bind && legacy_bind[0]=='1');
    for(unsigned cascade=0;cascade<g->shadow_count;++cascade) {
        struct rf_gpu_graphics_resource *bound=NULL;
        int bound_pipeline=-1;
        g->api.CmdBeginRenderPass(g->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
        g->api.CmdSetViewport(g->cmd,0,1,&viewport);g->api.CmdSetScissor(g->cmd,0,1,&scissor);
        g->owner->api.cmd_bind_descriptor_sets(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->layout,1,1,&g->light_set,0,NULL);
        for(unsigned i=0;i<count;++i) {
            struct rf_gpu_graphics_draw d=items[i].draw;
            if(d.scene_layer!=RF_GPU_SCENE_WORLD || d.texture[2] || d.texture[3]) continue;
            /* The current architectural query already covers this source for
             * every light. Props/actors remain unmarked and cast normally. */
            if(d.architecture_occluder && g->architecture_count &&
               !g->legacy_architecture_shadows)continue;
            /* Cull in light clip space, independently of the camera: an
             * offscreen building can still cast an onscreen shadow. */
            float center[3],extent[3];int outside=0;
            if (!g->disable_draw_cull && gfx_draw_box(&items[i],center,extent)) {
                const float *m=g->shadow_matrix[cascade];
                for (unsigned p=0;p<6 && !outside;++p) {
                    float plane[4];unsigned row=p/2;
                    for (unsigned k=0;k<4;++k)
                        plane[k]=p==4?m[k*4+2]:m[k*4+3]+((p&1)?-1:1)*m[k*4+row];
                    outside=gfx_box_outside(center,extent,plane);
                }
            }
            if (outside) continue;
            struct rf_gpu_graphics_resource *r=items[i].resource;d.camera[3]=(int)cascade;
            int pipeline=r->scene_vertex_color?31:30;
            if (!reuse_bind || bound_pipeline!=pipeline)
                g->owner->api.cmd_bind_pipeline(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->pipelines[pipeline]);
            if (!reuse_bind || bound!=r) {
                g->api.CmdBindVertexBuffers(g->cmd,0,1,&r->vertices.buffer,&offset);
                g->api.CmdBindIndexBuffer(g->cmd,r->indices.buffer,0,VK_INDEX_TYPE_UINT32);
            }
            bound=r;bound_pipeline=pipeline;
            g->api.CmdPushConstants(g->cmd,g->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128,&d);
            g->api.CmdDrawIndexed(g->cmd,d.index_count,1,d.first_index,0,0);
            g->stats.shadow_draws++;
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
