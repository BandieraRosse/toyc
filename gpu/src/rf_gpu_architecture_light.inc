/* Stackless, pre-order BVH of actual opaque building triangles and solids. The
 * buffers are per graphics owner and survive resize. No hardware RT required. */
struct rf_arch_node { float lo[4],hi[4],a[4],b[4],c[4]; };
struct rf_gi_arch_node { float lo[4],hi[4]; };
_Static_assert(sizeof(struct rf_arch_node)==80,"architecture std430 ABI");
static int architecture_roof_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *triangles,uint32_t count,
    struct rf_gpu_vulkan_buffer *next);
static int indirect_upload(struct rf_gpu_graphics *g,const void *data,size_t bytes,
    struct rf_gpu_vulkan_buffer *next);
static void indirect_bind(struct rf_gpu_graphics *g,unsigned binding,
    struct rf_gpu_vulkan_buffer *current,struct rf_gpu_vulkan_buffer *next);
static int indirect_geometry_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *triangles,uint32_t count,
    struct rf_gpu_vulkan_buffer *next);
static float arch_center(const struct rf_gpu_occlusion_primitive *t,unsigned axis)
{ return t->solid_box?1.5f*(t->p[0][axis]+t->p[1][axis]):t->p[0][axis]+t->p[1][axis]+t->p[2][axis]; }
static void arch_sort(struct rf_gpu_occlusion_primitive *t,int lo,int hi,unsigned axis)
{
    int a=lo,b=hi;float pivot=arch_center(&t[(lo+hi)/2],axis);
    while(a<=b) {
        while(arch_center(&t[a],axis)<pivot)++a;
        while(arch_center(&t[b],axis)>pivot)--b;
        if(a<=b) {struct rf_gpu_occlusion_primitive tmp=t[a];t[a++]=t[b];t[b--]=tmp;}
    }
    if(lo<b)arch_sort(t,lo,b,axis);
    if(a<hi)arch_sort(t,a,hi,axis);
}
static void arch_build(struct rf_arch_node *nodes,uint32_t *used,
    struct rf_gpu_occlusion_primitive *t,uint32_t count)
{
    struct rf_arch_node *n=&nodes[(*used)++];
    for(unsigned k=0;k<3;++k){n->lo[k]=1e30f;n->hi[k]=-1e30f;}
    for(unsigned i=0;i<count;++i)for(unsigned v=0;v<(t[i].solid_box?2u:3u);++v)for(unsigned k=0;k<3;++k){
        if(t[i].p[v][k]<n->lo[k])n->lo[k]=t[i].p[v][k];
        if(t[i].p[v][k]>n->hi[k])n->hi[k]=t[i].p[v][k];
    }
    if(count==1) {
        n->hi[3]=t->solid_box?2:1;
        for(unsigned k=0;k<3;++k){n->a[k]=t->p[0][k];n->b[k]=t->p[1][k]-t->p[0][k];n->c[k]=t->p[2][k]-t->p[0][k];}
        n->c[3]=(float)(t->diffuse_rgb&0xffffffu);
        n->a[3]=(float)(((t->bottom_rgb&0x1000000u)?t->bottom_rgb:t->diffuse_rgb)&0xffffffu);
    } else {
        unsigned axis=0;
        for(unsigned k=1;k<3;++k)if(n->hi[k]-n->lo[k]>n->hi[axis]-n->lo[axis])axis=k;
        arch_sort(t,0,(int)count-1,axis);
        arch_build(nodes,used,t,count/2);arch_build(nodes,used,t+count/2,count-count/2);
    }
    n->lo[3]=(float)*used;
}
int rf_gpu_graphics_set_architecture(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *triangles,uint32_t count)
{
    if(!g || g->scene_in_flight || g->poisoned || count>65536 || (count && !triangles))return -1;
    for(uint32_t i=0;i<count;++i)for(unsigned v=0;v<3;++v)for(unsigned k=0;k<3;++k)
        if(!__builtin_isfinite(triangles[i].p[v][k]) || fabs(triangles[i].p[v][k])>1e8)return -1;
    for(uint32_t i=0;i<count;++i)if(triangles[i].solid_box)
        for(unsigned k=0;k<3;++k)if(triangles[i].p[0][k]>=triangles[i].p[1][k])return -1;
    uint32_t software_count=count;
    size_t bytes=16+(software_count?2*software_count-1:0)*sizeof(struct rf_arch_node);
    unsigned char *data=calloc(1,bytes);
    struct rf_gpu_occlusion_primitive *copy=software_count?malloc(software_count*sizeof(*copy)):NULL;
    struct rf_gpu_vulkan_buffer next={0},roof={0},empty_gi={0},geometry={0},sources={0};void *mapped=NULL;
    if(!data || (software_count && !copy)){free(data);free(copy);return -1;}
    uint32_t used=0;
    if(software_count){memcpy(copy,triangles,count*sizeof(*copy));arch_build((struct rf_arch_node *)(data+16),&used,copy,count);}
    ((uint32_t *)data)[0]=used;free(copy);
    /* The cell builder needs only bounds, type and subtree skip, not triangle
     * vertices or reflectance. Retain the compact CPU index on both backends. */
    struct rf_gi_arch_node *cell_nodes=used?malloc((size_t)used*sizeof(*cell_nodes)):NULL;
    if(used && !cell_nodes){free(data);return -1;}
    for(unsigned i=0;i<used;++i) {
        const struct rf_arch_node *n=&((const struct rf_arch_node *)(data+16))[i];
        memcpy(cell_nodes[i].lo,n->lo,16);memcpy(cell_nodes[i].hi,n->hi,16);
    }
    struct rf_gpu_vulkan_impl *p=g->owner;
    uint32_t empty[16]={0};
    if(architecture_roof_build(g,triangles,count,&roof)<0 ||
       indirect_upload(g,empty,sizeof(empty),&empty_gi)<0 ||
       indirect_upload(g,empty,16,&sources)<0 ||
       indirect_geometry_build(g,triangles,count,&geometry)<0)goto fail;
    if(g->owner->ray_query_enabled) {
        if(architecture_ray_set(g,triangles,count)<0)goto fail;
    } else {
        if(gpu_buffer_create(p,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,&next)<0)goto fail;
        if(p->api.map_memory(p->device,next.memory,0,next.allocation_size,0,&mapped)!=RF_VK_SUCCESS)goto fail;
        memcpy(mapped,data,bytes);p->api.unmap_memory(p->device,next.memory);
        struct rf_vk_descriptor_buffer_info info={0};info.buffer=next.buffer;info.range=next.size;
        struct rf_vk_write_descriptor_set write={0};write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write.dst_set=g->light_set;write.dst_binding=7;write.descriptor_count=1;
        write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.buffer_info=&info;
        p->api.update_descriptor_sets(p->device,1,&write,0,NULL);
        gpu_buffer_destroy(p,&g->architecture_buffer);g->architecture_buffer=next;
    }
    free(g->indirect_architecture);
    g->indirect_architecture=cell_nodes;free(data);
    g->indirect_architecture_nodes=used;
    indirect_bind(g,10,&g->roof_buffer,&roof);
    indirect_bind(g,11,&g->indirect_buffer,&empty_gi);
    indirect_bind(g,12,&g->indirect_geometry,&geometry);
    indirect_bind(g,13,&g->indirect_sources,&sources);
    g->indirect_probe_count=g->indirect_cursor=0;g->indirect_warmed=0;
    g->architecture_count=count;
    return 0;
fail:
    free(cell_nodes);free(data);gpu_buffer_destroy(p,&next);gpu_buffer_destroy(p,&roof);
    gpu_buffer_destroy(p,&empty_gi);gpu_buffer_destroy(p,&geometry);gpu_buffer_destroy(p,&sources);return -1;
}
