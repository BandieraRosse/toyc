/* Retained static-building BLAS/TLAS. Upload/build is synchronous only when
 * the architecture generation changes, never in the normal frame loop. */
static uint64_t architecture_buffer_address(struct rf_gpu_vulkan_impl *p,
                                            const struct rf_gpu_vulkan_buffer *buffer)
{
    struct rf_rt_buffer_address info={1000244001,NULL,buffer->buffer};
    return p->rt.buffer_address(p->device,&info);
}
static void architecture_ray_destroy(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    if(g->architecture_tlas)p->rt.destroy(p->device,g->architecture_tlas,NULL);
    if(g->architecture_blas)p->rt.destroy(p->device,g->architecture_blas,NULL);
    g->architecture_tlas=g->architecture_blas=NULL;
    gpu_buffer_destroy(p,&g->architecture_tlas_buffer);
    gpu_buffer_destroy(p,&g->architecture_blas_buffer);
}
static int architecture_ray_upload(struct rf_gpu_vulkan_impl *p,const void *data,
                                   uint64_t bytes,struct rf_gpu_vulkan_buffer *buffer)
{
    void *mapped=NULL;
    if(gpu_buffer_create(p,bytes,RF_RT_ADDRESS_USAGE|RF_RT_INPUT_USAGE,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,buffer)<0)return -1;
    if(p->api.map_memory(p->device,buffer->memory,0,bytes,0,&mapped)!=RF_VK_SUCCESS)return -1;
    memcpy(mapped,data,(size_t)bytes);p->api.unmap_memory(p->device,buffer->memory);
    return 0;
}
static int architecture_ray_allocate(struct rf_gpu_vulkan_impl *p,struct rf_rt_build *build,
    uint32_t count,struct rf_gpu_vulkan_buffer *storage,struct rf_gpu_vulkan_buffer *scratch)
{
    struct rf_rt_sizes sizes={0};sizes.s_type=1000150020;
    p->rt.sizes(p->device,1,build,&count,&sizes); /* DEVICE build */
    if(!sizes.size)return -1;
    if(gpu_buffer_create(p,sizes.size,RF_RT_STORAGE_USAGE|RF_RT_ADDRESS_USAGE,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,storage)<0)return -1;
    struct rf_rt_create create={0};create.s_type=1000150017;create.buffer=storage->buffer;
    create.size=sizes.size;create.type=build->type;
    if(p->rt.create(p->device,&create,NULL,&build->dst)!=RF_VK_SUCCESS)return -1;
    uint64_t alignment=p->rt_properties.scratch_alignment;
    if(gpu_buffer_create(p,sizes.build_scratch+alignment,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_RT_ADDRESS_USAGE,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,0,scratch)<0)return -1;
    uint64_t address=architecture_buffer_address(p,scratch);
    if(!address)return -1;
    build->scratch.device=(address+alignment-1)&~(alignment-1);
    return 0;
}
static int architecture_ray_set(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *primitives,uint32_t count)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    uint32_t triangles=0;
    for(uint32_t i=0;i<count;++i)triangles+=primitives[i].solid_box?12:1;
    if(triangles>p->rt_properties.max_primitive || !p->rt_properties.max_geometry ||
       !p->rt_properties.max_instance)return -1;
    double started=now_ms();int result=-1,submitted=0;
    float *vertices=triangles?malloc((size_t)triangles*9*sizeof(float)):NULL;
    struct rf_gpu_vulkan_buffer input={0},instances={0},storage[2]={{0}},scratch[2]={{0}};
    struct rf_rt_geometry geometry[2]={{0}};
    struct rf_rt_build build[2]={{0}};
    struct rf_rt_range ranges[2]={{triangles,0,0,0},{count?1:0,0,0,0}};
    rf_vk_command_pool pool=NULL;rf_vk_command_buffer cmd=NULL;rf_vk_fence fence=NULL;
    if(triangles && !vertices)goto done;
    uint32_t written=0;
    for(uint32_t i=0;i<count;++i) {
        const struct rf_gpu_occlusion_primitive *t=&primitives[i];
        if(!t->solid_box){memcpy(vertices+written,t->p,9*sizeof(float));written+=9;continue;}
        /* Exact BOX render surfaces, not mesh bounds or gameplay collision. */
        static const unsigned char faces[36]={0,2,3,0,3,1,4,5,7,4,7,6,
            0,1,5,0,5,4,2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
        for(unsigned v=0;v<36;++v)for(unsigned k=0;k<3;++k)
            vertices[written++]=t->p[(faces[v]>>k)&1][k];
    }
    for(unsigned i=0;i<2;++i) {
        geometry[i].s_type=1000150006;geometry[i].type=i?2:0;geometry[i].flags=1; /* OPAQUE */
        build[i].s_type=1000150000;build[i].type=i?0:1; /* TLAS / BLAS */
        build[i].flags=4; /* PREFER_FAST_TRACE */
        build[i].geometry_count=1;build[i].geometries=&geometry[i];
    }
    struct rf_rt_instance instance={0};
    instance.transform[0]=instance.transform[5]=instance.transform[10]=1;
    instance.index_mask=0xff000000U;
    instance.offset_flags=0x01000000U; /* TRIANGLE_FACING_CULL_DISABLE */
    if(triangles) {
        if(architecture_ray_upload(p,vertices,(uint64_t)triangles*9*sizeof(float),&input)<0)goto done;
        struct rf_rt_triangles *t=&geometry[0].data.triangles;
        t->s_type=1000150005;t->format=106; /* R32G32B32_SFLOAT */
        t->vertices.device=architecture_buffer_address(p,&input);t->stride=12;
        t->max_vertex=triangles*3-1;t->index_type=1000165000; /* NONE_KHR */
        if(!t->vertices.device || architecture_ray_allocate(p,&build[0],triangles,&storage[0],&scratch[0])<0)goto done;
        struct rf_rt_as_address info={1000150002,NULL,build[0].dst};
        instance.reference=p->rt.as_address(p->device,&info);
        if(!instance.reference)goto done;
    }
    /* An empty, built TLAS is a valid descriptor even before a map is loaded. */
    if(architecture_ray_upload(p,&instance,sizeof(instance),&instances)<0)goto done;
    geometry[1].data.instances.s_type=1000150004;
    geometry[1].data.instances.data.device=architecture_buffer_address(p,&instances);
    if(!geometry[1].data.instances.data.device ||
       architecture_ray_allocate(p,&build[1],ranges[1].count,&storage[1],&scratch[1])<0)goto done;
    struct rf_vk_command_pool_create_info pool_info={0};
    pool_info.s_type=RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;pool_info.queue_family_index=p->queue_family;
    if(p->api.create_command_pool(p->device,&pool_info,NULL,&pool)!=RF_VK_SUCCESS)goto done;
    struct rf_vk_command_buffer_allocate_info allocate={0};
    allocate.s_type=RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;allocate.command_pool=pool;allocate.command_buffer_count=1;
    if(p->api.allocate_command_buffers(p->device,&allocate,&cmd)!=RF_VK_SUCCESS)goto done;
    struct rf_vk_command_buffer_begin_info begin={0};begin.s_type=RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    if(p->api.begin_command_buffer(cmd,&begin)!=RF_VK_SUCCESS)goto done;
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=RF_RT_WRITE_ACCESS;barrier.dst_access_mask=RF_RT_READ_ACCESS;
    if(triangles) {
        const struct rf_rt_range *range=&ranges[0];p->rt.build(cmd,1,&build[0],&range);
        p->api.cmd_pipeline_barrier(cmd,RF_RT_BUILD_STAGE,RF_RT_BUILD_STAGE,0,1,&barrier,0,NULL,0,NULL);
    }
    const struct rf_rt_range *range=&ranges[1];p->rt.build(cmd,1,&build[1],&range);
    p->api.cmd_pipeline_barrier(cmd,RF_RT_BUILD_STAGE,0x880,0,1,&barrier,0,NULL,0,NULL); /* FRAGMENT | COMPUTE */
    if(p->api.end_command_buffer(cmd)!=RF_VK_SUCCESS)goto done;
    struct rf_vk_fence_create_info fence_info={0};fence_info.s_type=RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if(p->api.create_fence(p->device,&fence_info,NULL,&fence)!=RF_VK_SUCCESS)goto done;
    struct rf_vk_submit_info submit={0};submit.s_type=RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.command_buffer_count=1;submit.command_buffers=&cmd;
    if(p->api.queue_submit(p->queue,1,&submit,fence)!=RF_VK_SUCCESS){g->poisoned=1;goto done;}
    submitted=1;
    if(p->api.wait_for_fences(p->device,1,&fence,RF_VK_TRUE,UINT64_MAX)!=RF_VK_SUCCESS){g->poisoned=1;goto done;}
    submitted=0;
    struct rf_rt_write as_write={1000150007,NULL,1,&build[1].dst};
    struct rf_vk_write_descriptor_set write={0};write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.next=&as_write;write.dst_set=g->light_set;write.dst_binding=7;
    write.descriptor_count=1;write.descriptor_type=RF_RT_DESCRIPTOR;
    p->api.update_descriptor_sets(p->device,1,&write,0,NULL);
    architecture_ray_destroy(g);
    g->architecture_blas=build[0].dst;g->architecture_tlas=build[1].dst;
    g->architecture_blas_buffer=storage[0];g->architecture_tlas_buffer=storage[1];
    memset(storage,0,sizeof(storage));build[0].dst=build[1].dst=NULL;
    fprintf(stderr,"rf-gpu-ray: architecture primitives=%u triangles=%u build-ms=%.3f retained-bytes=%llu\n",
        count,triangles,now_ms()-started,(unsigned long long)(g->architecture_blas_buffer.size+g->architecture_tlas_buffer.size));
    result=0;
done:
    if(submitted)p->api.queue_wait_idle(p->queue);
    if(fence)p->api.destroy_fence(p->device,fence,NULL);
    if(pool)p->api.destroy_command_pool(p->device,pool,NULL);
    for(int i=1;i>=0;--i) {
        if(build[i].dst)p->rt.destroy(p->device,build[i].dst,NULL);
        gpu_buffer_destroy(p,&storage[i]);gpu_buffer_destroy(p,&scratch[i]);
    }
    gpu_buffer_destroy(p,&input);gpu_buffer_destroy(p,&instances);free(vertices);
    if(result<0)fprintf(stderr,"rf-gpu-ray: architecture build failed\n");
    return result;
}
