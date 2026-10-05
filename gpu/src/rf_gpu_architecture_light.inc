/* Stackless, pre-order BVH of actual opaque building triangles and solids. The
 * buffers are per graphics owner and survive resize. No hardware RT required. */
struct rf_arch_node { float lo[4],hi[4],a[4],b[4],c[4]; };
_Static_assert(sizeof(struct rf_arch_node)==80,"architecture std430 ABI");
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
    size_t bytes=16+(count?2*count-1:0)*sizeof(struct rf_arch_node);
    unsigned char *data=calloc(1,bytes);
    struct rf_gpu_occlusion_primitive *copy=count?malloc(count*sizeof(*copy)):NULL;
    struct rf_gpu_vulkan_buffer next={0};void *mapped=NULL;
    if(!data || (count && !copy)){free(data);free(copy);return -1;}
    uint32_t used=0;
    if(count){memcpy(copy,triangles,count*sizeof(*copy));arch_build((struct rf_arch_node *)(data+16),&used,copy,count);}
    ((uint32_t *)data)[0]=used;free(copy);
    struct rf_gpu_vulkan_impl *p=g->owner;
    if(gpu_buffer_create(p,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,0,&next)<0){free(data);return -1;}
    if(p->api.map_memory(p->device,next.memory,0,next.allocation_size,0,&mapped)!=RF_VK_SUCCESS){
        free(data);gpu_buffer_destroy(p,&next);return -1;
    }
    memcpy(mapped,data,bytes);p->api.unmap_memory(p->device,next.memory);free(data);
    struct rf_vk_descriptor_buffer_info info={0};info.buffer=next.buffer;info.range=next.size;
    struct rf_vk_write_descriptor_set write={0};write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    write.dst_set=g->light_set;write.dst_binding=7;write.descriptor_count=1;
    write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;write.buffer_info=&info;
    p->api.update_descriptor_sets(p->device,1,&write,0,NULL);
    gpu_buffer_destroy(p,&g->architecture_buffer);g->architecture_buffer=next;
    return 0;
}
