/* Spatial receiver cache. BSP leaves are convex free-space regions: opaque
 * BOX boundaries and intersecting triangle planes split interpolation domains.
 * Camera/cutaway never enter the topology. No fragment visibility queries. */
#define RF_RC_LEAVES 32768u
#define RF_RC_DEPTH 48u
#define RF_RC_STATIC_VECTORS 2u
#define RF_RC_LIGHT_VECTORS 6u
struct rf_rc_node { float plane[4];uint32_t child[4];float lo[4],hi[4]; };
struct rf_rc_builder {
    const struct rf_gpu_occlusion_primitive *geometry;
    unsigned count,used,leaves,failed,unresolved;
    float spacing;
    struct rf_rc_node *nodes;
    float (*positions)[4];
    float planes[RF_RC_DEPTH][4];
    int fixed_projection;
    uint64_t projection_iterations;
};
static float rc_dot(const float *a,const float *b)
{return a[0]*b[0]+a[1]*b[1]+a[2]*b[2];}
static unsigned rc_build_node(struct rf_rc_builder *b,const float *lo,const float *hi,unsigned depth,
    const unsigned *ids,unsigned count)
{
    if(b->failed)return 0;
    if(b->used==RF_RC_LEAVES*4u){b->failed=1;return 0;}
    unsigned id=b->used++;
    unsigned *local=count?malloc((size_t)count*sizeof(*local)):NULL,local_count=0;
    if(count && !local){b->failed=1;return id;}
    struct rf_rc_node *node=&b->nodes[id];
    memcpy(node->lo,lo,12);memcpy(node->hi,hi,12);
    float plane[4]={0};unsigned axis=0;int split=0,nearby=0;
    for(unsigned k=1;k<3;++k)if(hi[k]-lo[k]>hi[axis]-lo[axis])axis=k;
    /* Prune empty distant space; unrepresented exterior uses the proven
     * roof-column classification, never an unoccluded indoor sky fallback. */
    for(unsigned i=0;i<count;++i) {
        const struct rf_gpu_occlusion_primitive *t=&b->geometry[ids[i]];
        float mn[3]={1e30f,1e30f,1e30f},mx[3]={-1e30f,-1e30f,-1e30f};
        for(unsigned v=0;v<(t->solid_box?2u:3u);++v)for(unsigned k=0;k<3;++k) {
            mn[k]=__builtin_fminf(mn[k],t->p[v][k]);mx[k]=__builtin_fmaxf(mx[k],t->p[v][k]);
        }
        int close=1,overlap=1,inside=t->solid_box;
        for(unsigned k=0;k<3;++k) {
            if(mx[k]+768<lo[k] || mn[k]-768>hi[k])close=0;
            if(mx[k]<lo[k]+0.02f || mn[k]>hi[k]-0.02f)overlap=0;
            if(lo[k]<mn[k]-0.01f || hi[k]>mx[k]+0.01f)inside=0;
        }
        nearby|=close;
        if(close)local[local_count++]=ids[i];
        if(inside){free(local);return id;} /* Solid leaf, no light samples. */
        if(!overlap || split || hi[axis]-lo[axis]>b->spacing)continue;
        if(t->solid_box) {
            for(unsigned k=0;k<3 && !split;++k)for(unsigned s=0;s<2 && !split;++s) {
                float at=s?mx[k]:mn[k];
                if(at>lo[k]+0.05f && at<hi[k]-0.05f){plane[k]=1;plane[3]=at;split=1;}
            }
        } else {
            float a[3],c[3];for(unsigned k=0;k<3;++k){a[k]=t->p[1][k]-t->p[0][k];c[k]=t->p[2][k]-t->p[0][k];}
            float n[3]={a[1]*c[2]-a[2]*c[1],a[2]*c[0]-a[0]*c[2],a[0]*c[1]-a[1]*c[0]};
            if(indirect_normalize(n)<0.001f)continue;
            float at=rc_dot(n,t->p[0]),min_d=0,max_d=0;
            for(unsigned k=0;k<3;++k){min_d+=n[k]*(n[k]>0?lo[k]:hi[k]);max_d+=n[k]*(n[k]>0?hi[k]:lo[k]);}
            /* Other ancestor planes may already exclude this triangle's
             * infinite plane. Avoid repeatedly splitting the same plane. */
            int duplicate=0;
            for(unsigned j=0;j<depth;++j)if(__builtin_fabsf(__builtin_fabsf(rc_dot(n,b->planes[j]))-1)<0.00001f &&
                __builtin_fabsf(at-(rc_dot(n,b->planes[j])>0?b->planes[j][3]:-b->planes[j][3]))<0.05f)duplicate=1;
            if(!duplicate && at>min_d+0.05f && at<max_d-0.05f){memcpy(plane,n,12);plane[3]=at;split=1;}
        }
    }
    if(!nearby){free(local);return id;}
    if(hi[axis]-lo[axis]>b->spacing){memset(plane,0,sizeof(plane));plane[axis]=1;plane[3]=(lo[axis]+hi[axis])*0.5f;split=1;}
    if(split) {
        if(depth==RF_RC_DEPTH){free(local);++b->unresolved;return id;}
        memcpy(node->plane,plane,16);
        float left_lo[3],left_hi[3],right_lo[3],right_hi[3];
        memcpy(left_lo,lo,12);memcpy(left_hi,hi,12);
        memcpy(right_lo,lo,12);memcpy(right_hi,hi,12);
        /* Triangle winding can produce either sign of an axial normal. Both
         * signs must tighten the child bounds, especially stair undersides.
         * Keeping the parent AABB for a negative normal creates unreachable
         * corners and dark unresolved leaves next to otherwise valid leaves. */
        for(unsigned k=0;k<3;++k)if(__builtin_fabsf(plane[k])==1 &&
            plane[(k+1)%3]==0 && plane[(k+2)%3]==0) {
            float at=plane[3]/plane[k];
            if(plane[k]>0)left_hi[k]=right_lo[k]=at;
            else left_lo[k]=right_hi[k]=at;
        }
        memcpy(b->planes[depth],plane,16);
        node->child[0]=rc_build_node(b,left_lo,left_hi,depth+1,local,local_count)+1;
        for(unsigned k=0;k<4;++k)b->planes[depth][k]=-plane[k];
        node->child[1]=rc_build_node(b,right_lo,right_hi,depth+1,local,local_count)+1;
        free(local);
        return id;
    }
    free(local);
    if(b->leaves==RF_RC_LEAVES){b->failed=1;return id;}
    unsigned first=b->leaves*8;
    for(unsigned corner=0;corner<8;++corner) {
        float *p=b->positions[first+corner];
        for(unsigned k=0;k<3;++k)p[k]=(corner&(1u<<k))?hi[k]-__builtin_fminf(1.5f,(hi[k]-lo[k])*0.1f):lo[k]+__builtin_fminf(1.5f,(hi[k]-lo[k])*0.1f);
        /* Project corners into the convex BSP region; unlike a voxel filter,
         * samples across a thin wall or storey can never share a leaf. */
        for(unsigned iteration=0;iteration<64;++iteration) {
            float before[3];memcpy(before,p,12);++b->projection_iterations;
            for(unsigned j=0;j<depth;++j) {
                const float *q=b->planes[j];float d=rc_dot(q,p)-q[3]+0.01f;
                if(d>0)for(unsigned k=0;k<3;++k)p[k]-=q[k]*d;
            }
            /* A full sweep leaving every coordinate bit-identical is a fixed
             * point. Further identical sweeps cannot change the sample. */
            if(!b->fixed_projection && !memcmp(before,p,12))break;
        }
        for(unsigned j=0;j<depth;++j)if(rc_dot(b->planes[j],p)>b->planes[j][3]+0.02f){++b->unresolved;return id;}
    }
    node->child[3]=first+1;++b->leaves;return id;
}
static int receiver_cache_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *geometry,unsigned count,struct rf_gpu_vulkan_buffer *next,
    unsigned *sample_count,struct rf_rc_leaf **leaves)
{
    uint32_t header[4]={0};*sample_count=0;*leaves=NULL;
    if(g->indirect_mode!=1 || !g->indirect_enabled || !count)return indirect_upload(g,header,16,next);
    struct rf_rc_builder b={0};b.geometry=geometry;b.count=count;b.spacing=512;
    const char *fixed=getenv("RF_GPU_RECEIVER_PROJECTION_FIXED");
    b.fixed_projection=fixed && !strcmp(fixed,"1");
    b.nodes=calloc(RF_RC_LEAVES*4u,sizeof(*b.nodes));b.positions=calloc(RF_RC_LEAVES*8u,16);
    unsigned *ids=malloc((size_t)count*sizeof(*ids));
    if(!b.nodes || !b.positions || !ids){free(ids);free(b.nodes);free(b.positions);return -1;}
    for(unsigned i=0;i<count;++i)ids[i]=i;
    float lo[3]={1e30f,1e30f,1e30f},hi[3]={-1e30f,-1e30f,-1e30f};double started=now_ms();
    for(unsigned i=0;i<count;++i)for(unsigned v=0;v<(geometry[i].solid_box?2u:3u);++v)for(unsigned k=0;k<3;++k) {
        lo[k]=__builtin_fminf(lo[k],geometry[i].p[v][k]-768);hi[k]=__builtin_fmaxf(hi[k],geometry[i].p[v][k]+768);
    }
    for(unsigned attempt=0;attempt<6;++attempt) {
        b.used=b.leaves=b.failed=b.unresolved=0;memset(b.nodes,0,RF_RC_LEAVES*4u*sizeof(*b.nodes));
        rc_build_node(&b,lo,hi,0,ids,count);if(!b.failed)break;b.spacing*=2;
    }
    free(ids);
    if(b.failed){fprintf(stderr,"rf-gpu-receiver-cache: topology exceeded budget nodes=%u leaves=%u\n",b.used,b.leaves);free(b.nodes);free(b.positions);return -1;}
    header[0]=b.used;header[1]=b.leaves*8;header[2]=b.used*4;
    header[3]=header[2]+header[1]*RF_RC_STATIC_VECTORS;
    /* Static positions/cell references precede dynamic FP16 irradiance. No
     * per-sample copies of eight probe indices or trilinear weights. */
    unsigned light_vectors=g->receiver_fp32?12u:RF_RC_LIGHT_VECTORS;
    size_t bytes=16+(size_t)header[3]*16+(size_t)header[1]*light_vectors*16;
    if(g->receiver_fp32)header[0]|=0x80000000u;
    struct rf_rc_leaf *bounds=g->local_block_enabled && b.leaves?calloc(b.leaves,sizeof(*bounds)):NULL;
    if(g->local_block_enabled && b.leaves && !bounds){free(b.nodes);free(b.positions);return -1;}
    if(bounds)for(unsigned i=0;i<b.used;++i)if(b.nodes[i].child[3]) {
        struct rf_rc_leaf *leaf=&bounds[(b.nodes[i].child[3]-1)/8];
        memcpy(leaf->lo,b.nodes[i].lo,12);memcpy(leaf->hi,b.nodes[i].hi,12);
    }
    unsigned char *data=calloc(1,bytes);int result=-1;
    if(data) {
        memcpy(data,header,16);memcpy(data+16,b.nodes,(size_t)b.used*sizeof(*b.nodes));
        for(unsigned i=0;i<header[1];++i)memcpy(data+16+(size_t)header[2]*16+(size_t)i*RF_RC_STATIC_VECTORS*16,b.positions[i],12);
        result=indirect_upload(g,data,bytes,next);
        if(!result){*sample_count=header[1];*leaves=bounds;bounds=NULL;}
    }
    fprintf(stderr,"rf-gpu-receiver-cache: leaves=%u nodes=%u samples=%u spacing=%.0f bytes=%llu prepare-ms=%.3f unresolved=%u sample-bytes=%u precision=%s projection-sweeps=%llu\n",
        b.leaves,b.used,header[1],b.spacing,(unsigned long long)bytes,now_ms()-started,b.unresolved,
        (RF_RC_STATIC_VECTORS+light_vectors)*16,g->receiver_fp32?"fp32":"fp16",
        (unsigned long long)b.projection_iterations);
    free(bounds);free(data);free(b.nodes);free(b.positions);return result;
}
/* A whole AABB lies in one real BOX's shadow only if its entire perspective
 * projection falls strictly inside a BOX cross-section. Interval bounds cover
 * every receiver, not sampled visibility. The source cube includes the 1.5 RFU
 * normal shift: the existing ray aims along light-p from origin p+n*1.5.
 * Separation/margins keep a surface hit inside the existing finite interval. */
static int rc_box_blocks_leaf(const struct rf_gi_arch_node *box,
    const struct rf_rc_leaf *leaf,const float *light)
{
    double magnitude=0;
    for(unsigned k=0;k<3;++k) {
        magnitude=__builtin_fmax(magnitude,__builtin_fabs(light[k]));
        magnitude=__builtin_fmax(magnitude,__builtin_fabs(leaf->lo[k]));
        magnitude=__builtin_fmax(magnitude,__builtin_fabs(leaf->hi[k]));
        magnitude=__builtin_fmax(magnitude,__builtin_fabs(box->lo[k]));
        magnitude=__builtin_fmax(magnitude,__builtin_fabs(box->hi[k]));
    }
    double margin=8.0+magnitude*0.000002;
    for(unsigned axis=0;axis<3;++axis) {
        double sign;
        if(light[axis]+2<box->lo[axis]-margin)sign=1;
        else if(light[axis]-2>box->hi[axis]+margin)sign=-1;
        else continue;
        double plane=sign*((double)box->lo[axis]+box->hi[axis])*0.5;
        double a=sign*light[axis]-2,b=sign*light[axis]+2;
        double qlo=sign*(sign>0?leaf->lo[axis]:leaf->hi[axis]);
        double qhi=sign*(sign>0?leaf->hi[axis]:leaf->lo[axis]);
        if(qlo<=plane+margin)continue;
        double tlo=(plane-b)/(qhi-b),thi=(plane-a)/(qlo-a);
        int covered=1;
        for(unsigned k=0;k<3 && covered;++k)if(k!=axis) {
            double s0=light[k]-2,s1=light[k]+2;
            double lo=__builtin_fmin(s0+(leaf->lo[k]-s0)*tlo,s0+(leaf->lo[k]-s0)*thi);
            double hi=__builtin_fmax(s1+(leaf->hi[k]-s1)*tlo,s1+(leaf->hi[k]-s1)*thi);
            covered=lo>box->lo[k]+margin && hi<box->hi[k]-margin;
        }
        if(covered)return 1;
    }
    return 0;
}
static int rc_leaf_blocked(struct rf_gpu_graphics *g,const struct rf_rc_leaf *leaf,
    const float *light,unsigned *visits)
{
    float lo[3],hi[3];
    for(unsigned k=0;k<3;++k) {
        lo[k]=__builtin_fminf(leaf->lo[k],light[k]-2);
        hi[k]=__builtin_fmaxf(leaf->hi[k],light[k]+2);
    }
    unsigned i=0;
    while(i<g->indirect_architecture_nodes && *visits<262144u) {
        const struct rf_gi_arch_node *node=&g->indirect_architecture[i];
        ++*visits;int outside=0;
        for(unsigned k=0;k<3;++k)if(node->lo[k]>hi[k] || node->hi[k]<lo[k])outside=1;
        if(outside){i=(unsigned)node->lo[3];continue;}
        if(node->hi[3]>1.5f && rc_box_blocks_leaf(node,leaf,light))return 1;
        ++i;
    }
    return 0;
}
static int receiver_block_cache_update(struct rf_gpu_graphics *g,
    const struct rf_gpu_light *lights,unsigned count)
{
    if(!g->local_block_enabled || !g->receiver_leaves || !g->receiver_samples || !count) {
        g->local_block_count=0;return 0;
    }
    int same=count==g->local_block_count;
    for(unsigned i=0;same && i<count;++i)
        if(memcmp(lights[i].position_radius,g->local_block_keys[i],16))same=0;
    if(same)return 0;
    double started=now_ms();unsigned leaves=g->receiver_samples/8,blocked=0,limited=0;
    size_t bytes=16+(size_t)leaves*16;
    uint32_t *data=calloc(1,bytes);struct rf_gpu_vulkan_buffer next={0};
    if(!data)return -1;
    data[0]=count;data[1]=leaves;
    uint32_t *masks=data+4;
    uint64_t visited=0;
    for(unsigned i=0;i<count;++i) {
        const float *light=lights[i].position_radius;unsigned visits=0;
        for(unsigned j=0;j<leaves && visits<262144u;++j) {
            const struct rf_rc_leaf *leaf=&g->receiver_leaves[j];float r2=0;
            for(unsigned k=0;k<3;++k) {
                float d=__builtin_fmaxf(leaf->lo[k]-light[k],__builtin_fmaxf(light[k]-leaf->hi[k],0));r2+=d*d;
            }
            if(r2>(light[3]+2)*(light[3]+2))continue;
            if(rc_leaf_blocked(g,leaf,light,&visits)){masks[j*4+i/32]|=1u<<(i%32);++blocked;}
        }
        visited+=visits;limited+=visits>=262144u;
    }
    int result=indirect_upload(g,data,bytes,&next);free(data);
    if(result)return result;
    indirect_bind(g,17,&g->local_block_buffer,&next);g->local_block_count=count;
    for(unsigned i=0;i<count;++i)memcpy(g->local_block_keys[i],lights[i].position_radius,16);
    fprintf(stderr,"rf-gpu-local-block: leaves=%u lights=%u blocked-pairs=%u visits=%llu limited-lights=%u bytes=%llu prepare-ms=%.3f\n",
        leaves,count,blocked,(unsigned long long)visited,limited,(unsigned long long)bytes,now_ms()-started);
    return 0;
}
static void gfx_receiver_cache_record(struct rf_gpu_graphics *g)
{
    if(g->indirect_mode!=1 || !g->indirect_enabled || g->lighting.disable_indirect || !g->receiver_samples)return;
    struct rf_gpu_vulkan_impl *p=g->owner;
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->receiver_pipeline);
    for(unsigned pass=g->receiver_ready?1:0;pass<2;++pass) {
        unsigned control=pass,budget=g->receiver_samples;
        if(g->receiver_ready && !g->receiver_full_refresh) {
            budget=budget>8192?8192:budget;control=3u|(g->receiver_cursor<<2);
            g->receiver_cursor=(g->receiver_cursor+budget)%g->receiver_samples;
        } else g->receiver_cursor=0;
        g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&control);
        p->api.cmd_dispatch(g->cmd,(budget+63)/64,1,1);
        struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        p->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    }
    g->receiver_ready=1;
    g->receiver_full_refresh=0;
}
