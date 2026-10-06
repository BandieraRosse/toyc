/* DDGI probe placement, exact local-segment visibility and upward visibility.
 * Radiance is updated on the GPU; moments remain only for diagnostic comparison. */
#define RF_GI_PROBES 16384u
#define RF_GI_DAYLIGHT_PROBES 32768u
#define RF_GI_HASH 65536u
#define RF_GI_RAYS 128u
#define RF_GI_STRIDE 39u /* six irradiance lobes + 64 pairs of distance moments */
struct rf_gi_slot { int32_t cell[3];uint32_t index; };
struct rf_gi_cell { int32_t cell[3];uint32_t probes[8]; };
struct rf_gi_probe { float light[6][4],distance[64][2],position[4]; };
struct rf_gi_header { uint32_t layout[4];float params[4],minimum[4],maximum[4]; };
struct rf_roof_header { float grid[4];uint32_t extent[4]; };
_Static_assert(sizeof(struct rf_gi_probe)==RF_GI_STRIDE*16,"probe std430 ABI");

static int indirect_upload(struct rf_gpu_graphics *g,const void *data,size_t bytes,
    struct rf_gpu_vulkan_buffer *next)
{
    struct rf_gpu_vulkan_impl *p=g->owner;void *mapped=NULL;
    if(gpu_buffer_create(p,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT|RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,next)<0){gpu_buffer_destroy(p,next);return -1;}
    if(p->api.map_memory(p->device,next->memory,0,next->allocation_size,0,&mapped)!=RF_VK_SUCCESS) {
        gpu_buffer_destroy(p,next);return -1;
    }
    memcpy(mapped,data,bytes);p->api.unmap_memory(p->device,next->memory);return 0;
}
static void indirect_bind(struct rf_gpu_graphics *g,unsigned binding,
    struct rf_gpu_vulkan_buffer *current,struct rf_gpu_vulkan_buffer *next)
{
    struct rf_vk_descriptor_buffer_info info={0};struct rf_vk_write_descriptor_set write={0};
    info.buffer=next->buffer;info.range=next->size;
    write.s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;write.dst_set=g->light_set;
    write.dst_binding=binding;write.descriptor_count=1;write.descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
    write.buffer_info=&info;g->owner->api.update_descriptor_sets(g->owner->device,1,&write,0,NULL);
    gpu_buffer_destroy(g->owner,current);*current=*next;memset(next,0,sizeof(*next));
}
static float indirect_normalize(float *v)
{
    float length=(float)sqrt(v[0]*v[0]+v[1]*v[1]+v[2]*v[2]);
    if(length>0.000001f)for(unsigned k=0;k<3;++k)v[k]/=length;
    return length;
}
static int indirect_geometry_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *input,uint32_t count,
    struct rf_gpu_vulkan_buffer *next)
{
    uint32_t triangles=0;
    if(g->owner->ray_query_enabled)for(unsigned i=0;i<count;++i)triangles+=input[i].solid_box?12:1;
    size_t bytes=triangles?(size_t)triangles*48:16;float *data=calloc(1,bytes);
    if(!data)return -1;
    unsigned at=0;
    static const unsigned char faces[36]={0,2,3,0,3,1,4,5,7,4,7,6,
        0,1,5,0,5,4,2,6,7,2,7,3,0,4,6,0,6,2,1,3,7,1,7,5};
    for(unsigned i=0;triangles && i<count;++i) {
        const struct rf_gpu_occlusion_primitive *t=&input[i];
        for(unsigned j=0;j<(t->solid_box?12u:1u);++j) {
            for(unsigned v=0;v<3;++v)for(unsigned k=0;k<3;++k)
                data[at+v*4+k]=t->solid_box?t->p[(faces[j*3+v]>>k)&1][k]:t->p[v][k];
            data[at+3]=t->solid_box?1.0f:0.0f;
            uint32_t rgb=t->solid_box && j/2==2 && (t->bottom_rgb&0x1000000u)?t->bottom_rgb:t->diffuse_rgb;
            data[at+11]=(float)(rgb&0xffffffu);at+=12;
        }
    }
    int result=indirect_upload(g,data,bytes,next);free(data);return result;
}
/* Every full-cell classification is geometric. Corner samples of visibility
 * are NOT a coverage proof: only a convex triangle/box enclosing all corners
 * can provide an opaque height interval. Mixed cells use the original ray. */
static int architecture_roof_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_occlusion_primitive *triangles,uint32_t count,
    struct rf_gpu_vulkan_buffer *next)
{
    struct rf_roof_header h={0};float lo[2]={1e30f,1e30f},hi[2]={-1e30f,-1e30f};
    for(unsigned i=0;i<count;++i)for(unsigned v=0;v<(triangles[i].solid_box?2u:3u);++v)
        for(unsigned k=0;k<2;++k){float p=triangles[i].p[v][k*2];lo[k]=__builtin_fminf(lo[k],p);hi[k]=__builtin_fmaxf(hi[k],p);}
    float step=128.0f;
    if(count && g->roof_cache_enabled) {
        for(;;) {
            h.grid[0]=__builtin_floorf(lo[0]/step)*step;h.grid[1]=__builtin_floorf(lo[1]/step)*step;h.grid[2]=step;
            h.extent[0]=(uint32_t)__builtin_ceilf((hi[0]-h.grid[0])/step)+1;
            h.extent[1]=(uint32_t)__builtin_ceilf((hi[1]-h.grid[1])/step)+1;
            if(h.extent[0]<=1024 && h.extent[1]<=1024)break;
            step*=2;
        }
    }
    size_t cells=(size_t)h.extent[0]*h.extent[1],bytes=sizeof(h)+cells*16;
    unsigned char *data=calloc(1,bytes);if(!data)return -1;memcpy(data,&h,sizeof(h));
    float (*grid)[4]=(float (*)[4])(data+sizeof(h));
    for(size_t i=0;i<cells;++i){grid[i][0]=grid[i][1]=grid[i][2]=-1e30f;grid[i][3]=1e30f;}
    for(unsigned i=0;cells && i<count;++i) {
        const struct rf_gpu_occlusion_primitive *t=&triangles[i];
        float mn[3]={1e30f,1e30f,1e30f},mx[3]={-1e30f,-1e30f,-1e30f};
        for(unsigned v=0;v<(t->solid_box?2u:3u);++v)for(unsigned k=0;k<3;++k) {
            mn[k]=__builtin_fminf(mn[k],t->p[v][k]);mx[k]=__builtin_fmaxf(mx[k],t->p[v][k]);
        }
        int x0=(int)__builtin_floorf((mn[0]-h.grid[0]-0.01f)/step),x1=(int)__builtin_floorf((mx[0]-h.grid[0]+0.01f)/step);
        int z0=(int)__builtin_floorf((mn[2]-h.grid[1]-0.01f)/step),z1=(int)__builtin_floorf((mx[2]-h.grid[1]+0.01f)/step);
        if(x0<0)x0=0;
        if(z0<0)z0=0;
        if(x1>=(int)h.extent[0])x1=(int)h.extent[0]-1;
        if(z1>=(int)h.extent[1])z1=(int)h.extent[1]-1;
        float b[3],c[3];for(unsigned k=0;k<3;++k){b[k]=t->p[1][k]-t->p[0][k];c[k]=t->p[2][k]-t->p[0][k];}
        float det=b[0]*c[2]-b[2]*c[0];
        for(int z=z0;z<=z1;++z)for(int x=x0;x<=x1;++x) {
            float *cell=grid[(size_t)z*h.extent[0]+x];
            cell[2]=__builtin_fmaxf(cell[2],mx[1]);cell[3]=__builtin_fminf(cell[3],mn[1]);
            float ylo=1e30f,yhi=-1e30f;int full=1;
            for(unsigned corner=0;corner<4;++corner) {
                float px=h.grid[0]+(x+(corner&1))*step,pz=h.grid[1]+(z+(corner>>1))*step,y;
                if(t->solid_box) {
                    if(px<mn[0]+0.02f || px>mx[0]-0.02f || pz<mn[2]+0.02f || pz>mx[2]-0.02f){full=0;break;}
                    y=mx[1];
                } else {
                    if(fabs(det)<1e-6f){full=0;break;}
                    float dx=px-t->p[0][0],dz=pz-t->p[0][2];
                    float u=(dx*c[2]-dz*c[0])/det,v=(b[0]*dz-b[2]*dx)/det;
                    if(u<0.00001f || v<0.00001f || u+v>0.99999f){full=0;break;}
                    y=t->p[0][1]+u*b[1]+v*c[1];
                }
                ylo=__builtin_fminf(ylo,y);yhi=__builtin_fmaxf(yhi,y);
            }
            if(full && ylo>cell[0]){cell[0]=ylo;cell[1]=yhi;}
        }
    }
    int result=indirect_upload(g,data,bytes,next);free(data);return result;
}
static uint32_t gi_hash(int32_t x,int32_t y,int32_t z)
{uint32_t h=(uint32_t)x*73856093u^(uint32_t)y*19349663u^(uint32_t)z*83492791u;return h^(h>>16);}
static int gi_insert(struct rf_gi_slot *slots,int32_t (*coords)[3],unsigned *count,unsigned limit,int x,int y,int z)
{
    unsigned at=gi_hash(x,y,z)&(RF_GI_HASH-1);
    for(unsigned n=0;n<RF_GI_HASH;++n,at=(at+1)&(RF_GI_HASH-1)) {
        struct rf_gi_slot *s=&slots[at];
        if(!s->index) {
            if(*count==limit)return -1;
            s->cell[0]=x;s->cell[1]=y;s->cell[2]=z;s->index=++*count;
            memcpy(coords[*count-1],s->cell,12);return 0;
        }
        if(s->cell[0]==x && s->cell[1]==y && s->cell[2]==z)return 0;
    }
    return -1;
}
/* A segment between a relocated corner probe and any receiver in the cell is
 * contained in this expanded box. Store every overlapping solid, or fall back
 * to the complete architecture query if a triangle/large list is encountered.
 * This is a conservative geometric proof, never a sampled visibility mask. */
#define RF_GI_CELL_BOXES 32u
static unsigned gi_cell_boxes(struct rf_gpu_graphics *g,const int32_t *cell,float spacing,uint32_t *ids)
{
    const struct rf_gi_arch_node *nodes=g->indirect_architecture;
    float lo[3],hi[3];unsigned found=0;
    for(unsigned k=0;k<3;++k){lo[k]=(cell[k]+0.37f-0.45f)*spacing-0.1f;hi[k]=(cell[k]+1.37f+0.45f)*spacing+0.1f;}
    for(unsigned i=0;i<g->indirect_architecture_nodes;) {
        const struct rf_gi_arch_node *n=&nodes[i];int overlaps=1;
        for(unsigned k=0;k<3;++k)if(n->hi[k]<lo[k] || n->lo[k]>hi[k])overlaps=0;
        if(!overlaps){i=(unsigned)n->lo[3];continue;}
        if(n->hi[3]>0) {
            if(n->hi[3]<1.5f || found==RF_GI_CELL_BOXES)return UINT32_MAX;
            ids[found++]=i;
        }
        ++i;
    }
    return found;
}
static void gfx_indirect_record(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    if(!g->indirect_enabled || g->lighting.disable_indirect || !g->indirect_probe_count)return;
    uint32_t budget=g->indirect_warmed?256:g->indirect_probe_count;
    if(budget>g->indirect_probe_count)budget=g->indirect_probe_count;
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->indirect_pipeline);
    uint32_t control=g->indirect_cursor|(g->indirect_reset_history?0x80000000u:0u);
    g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&control);
    p->api.cmd_dispatch(g->cmd,budget,1,1);
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    p->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    g->indirect_cursor=(g->indirect_cursor+budget)%g->indirect_probe_count;g->indirect_warmed=1;
    g->indirect_reset_history=0;
}
static int indirect_field_build(struct rf_gpu_graphics *g,
    const struct rf_gpu_light *sources,uint32_t count,int daylight)
{
    if(!g || g->scene_in_flight || g->poisoned || count>RF_GPU_INDIRECT_LIGHT_CAP || (count && !sources))return -1;
    struct rf_gpu_light lights[RF_GPU_INDIRECT_LIGHT_CAP];
    for(unsigned i=0;i<count;++i) {
        lights[i]=sources[i];const float *f=(const float *)&lights[i];
        for(unsigned k=0;k<16;++k)if(!__builtin_isfinite(f[k]))return -1;
        if(lights[i].position_radius[3]<=0 || lights[i].position_radius[3]>65536 || lights[i].color_intensity[3]<0)return -1;
        for(unsigned k=0;k<3;++k)if(fabs(lights[i].position_radius[k])>1e8 || lights[i].color_intensity[k]<0)return -1;
        if(lights[i].color_intensity[3]>0 && light_luminance(lights[i].color_intensity)<=0)return -1;
        if(lights[i].direction_outer[3]>=0 &&
            (lights[i].direction_outer[3]>=1 || lights[i].inner_shadow[0]<=lights[i].direction_outer[3] ||
             lights[i].inner_shadow[0]>1 || indirect_normalize(lights[i].direction_outer)<0.01f))return -1;
        light_chromaticity(lights[i].color_intensity);
        lights[i].color_intensity[3]/=RF_PHOTOMETRIC_SCALE;
    }
    struct rf_gi_header header={0};struct rf_gpu_vulkan_buffer next={0},source_buffer={0};
    unsigned char source_data[16+RF_GPU_INDIRECT_LIGHT_CAP*sizeof(struct rf_gpu_light)]={0};
    memcpy(source_data,&count,4);memcpy(source_data+16,lights,count*sizeof(*lights));
    if(!daylight && indirect_upload(g,source_data,16+count*sizeof(*lights),&source_buffer)<0)return -1;
    if(!daylight && g->indirect_mode==1 && g->indirect_probe_count && count==g->indirect_source_count) {
        int same_layout=1;
        for(unsigned i=0;i<count;++i)if(memcmp(lights[i].position_radius,g->indirect_source_snapshot[i].position_radius,16))same_layout=0;
        if(same_layout) {
            indirect_bind(g,13,&g->indirect_sources,&source_buffer);
            memcpy(g->indirect_source_snapshot,lights,count*sizeof(*lights));
            g->indirect_warmed=0;g->indirect_reset_history=1;g->receiver_full_refresh=1;
            return 0;
        }
    }
    unsigned binding=daylight?15:11;
    struct rf_gpu_vulkan_buffer *field=daylight?&g->daylight_buffer:&g->indirect_buffer;
    if((!count && !daylight) || !g->indirect_enabled || !g->architecture_count || (daylight && !g->daylight_enabled)) {
        if(indirect_upload(g,&header,sizeof(header),&next)<0){gpu_buffer_destroy(g->owner,&source_buffer);return -1;}
        indirect_bind(g,binding,field,&next);
        g->receiver_ready=0;
        if(daylight){g->daylight_probe_count=0;g->daylight_cursor=0;g->daylight_warmed=0;}
        else {indirect_bind(g,13,&g->indirect_sources,&source_buffer);
            g->indirect_probe_count=0;g->indirect_cursor=0;g->indirect_warmed=0;}
        return 0;
    }
    double started=now_ms();unsigned probes=0;float spacing=768;
    struct rf_gi_slot *slots=calloc(RF_GI_HASH,sizeof(*slots));
    unsigned limit=daylight?RF_GI_DAYLIGHT_PROBES:RF_GI_PROBES;
    int32_t (*coords)[3]=malloc(limit*sizeof(*coords));
    struct rf_gi_slot *cell_slots=NULL;struct rf_gi_cell *cells=NULL;
    unsigned char *data=NULL;int result=-1;if(!slots || !coords)goto done;
    /* Budget applies to the whole stable source list. Coarsen uniformly if a
     * very large world exceeds it, rather than dropping camera-dependent lamps. */
    for(unsigned attempt=0;attempt<8;++attempt,spacing*=2) {
        int overflow=0;probes=0;memset(slots,0,RF_GI_HASH*sizeof(*slots));
        for(unsigned i=0;i<count && !overflow;++i) {
            int lo[3],hi[3];float radius=lights[i].position_radius[3]+spacing;
            for(unsigned k=0;k<3;++k){lo[k]=(int)__builtin_floorf((lights[i].position_radius[k]-radius)/spacing-0.37f);hi[k]=(int)__builtin_ceilf((lights[i].position_radius[k]+radius)/spacing-0.37f);}
            for(int z=lo[2];z<=hi[2] && !overflow;++z)for(int y=lo[1];y<=hi[1] && !overflow;++y)for(int x=lo[0];x<=hi[0];++x) {
                float dx=(x+0.37f)*spacing-lights[i].position_radius[0],dy=(y+0.37f)*spacing-lights[i].position_radius[1];
                float dz=(z+0.37f)*spacing-lights[i].position_radius[2];
                if(dx*dx+dy*dy+dz*dz>radius*radius)continue;
                if(gi_insert(slots,coords,&probes,limit,x,y,z)<0){overflow=1;break;}
            }
        }
        /* Daylight has an independent budget, seeded around actual architectural
         * surfaces (including floors/slabs), never lamps, rooms or camera bounds.
         * A coarse outdoor field therefore cannot reduce fixture-GI resolution. */
        for(unsigned i=0;daylight && i<g->indirect_architecture_nodes && !overflow;++i) {
            const struct rf_gi_arch_node *n=&g->indirect_architecture[i];
            if(n->hi[3]==0)continue;
            int lo[3],hi[3];
            for(unsigned k=0;k<3;++k) {
                lo[k]=(int)__builtin_floorf(n->lo[k]/spacing-1.37f);
                hi[k]=(int)__builtin_ceilf(n->hi[k]/spacing+0.63f);
            }
            for(int z=lo[2];z<=hi[2] && !overflow;++z)for(int y=lo[1];y<=hi[1] && !overflow;++y)for(int x=lo[0];x<=hi[0];++x) {
                int c[3]={x,y,z};int inside=n->hi[3]>1.5f;
                for(unsigned k=0;k<3;++k) {
                    float p=(c[k]+0.37f)*spacing;
                    if(p<=n->lo[k] || p>=n->hi[k])inside=0;
                }
                if(inside)continue;
                if(gi_insert(slots,coords,&probes,limit,x,y,z)<0){overflow=1;break;}
            }
        }
        if(!overflow)break;
        if(attempt==7)goto done;
    }
    /* Freeze each interpolation cell's eight neighbours once. A fragment now
     * performs one sparse lookup and two adjacent vector loads, not eight
     * independent hash walks. The temporary table cannot exceed half full. */
    unsigned cell_hash=1,cell_count=0;
    while(cell_hash<probes*16u)cell_hash*=2;
    cell_slots=calloc(cell_hash,sizeof(*cell_slots));cells=calloc(probes*8u,sizeof(*cells));
    if(!cell_slots || !cells)goto done;
    for(unsigned i=0;i<probes;++i)for(unsigned corner=0;corner<8;++corner) {
        int32_t c[3];for(unsigned k=0;k<3;++k)c[k]=coords[i][k]-((corner>>k)&1);
        unsigned at=gi_hash(c[0],c[1],c[2])&(cell_hash-1);
        while(cell_slots[at].index && memcmp(cell_slots[at].cell,c,sizeof(c)))at=(at+1)&(cell_hash-1);
        if(!cell_slots[at].index) {
            cell_slots[at].index=++cell_count;memcpy(cell_slots[at].cell,c,sizeof(c));
            memcpy(cells[cell_count-1].cell,c,sizeof(c));
        }
        cells[cell_slots[at].index-1].probes[corner]=i+1;
    }
    unsigned compact_hash=1;while(compact_hash<cell_count*2u)compact_hash*=2;
    unsigned box_vectors=0,cached_cells=0;uint32_t boxes[RF_GI_CELL_BOXES];
    for(unsigned i=0;i<cell_count;++i) {
        unsigned n=gi_cell_boxes(g,cells[i].cell,spacing,boxes);
        if(n!=UINT32_MAX){box_vectors+=n*2;++cached_cells;}
    }
    unsigned boxes_offset=compact_hash+cell_count*3;
    unsigned probe_offset=boxes_offset+box_vectors;
    unsigned stride=g->indirect_visibility?7:RF_GI_STRIDE;
    size_t bytes=sizeof(header)+(size_t)probe_offset*16+(size_t)probes*stride*16;
    data=calloc(1,bytes);if(!data)goto done;
    header.layout[0]=compact_hash;header.layout[1]=probes;header.layout[2]=stride;header.layout[3]=probe_offset;
    header.params[0]=spacing;header.params[1]=32;header.params[2]=8192;header.params[3]=0.85f;
    header.minimum[3]=(float)g->indirect_bounces;
    header.maximum[3]=(float)g->indirect_visibility;
    for(unsigned k=0;k<3;++k){header.minimum[k]=1e30f;header.maximum[k]=-1e30f;}
    struct rf_gi_slot *lookup=(struct rf_gi_slot *)(data+sizeof(header));
    uint32_t (*neighbors)[12]=(uint32_t (*)[12])(lookup+compact_hash);
    float (*box_data)[4]=(float (*)[4])(data+sizeof(header));
    const struct rf_gi_arch_node *nodes=g->indirect_architecture;
    unsigned box_at=boxes_offset;
    for(unsigned i=0;i<cell_count;++i) {
        int32_t *c=cells[i].cell;unsigned at=gi_hash(c[0],c[1],c[2])&(compact_hash-1);
        while(lookup[at].index)at=(at+1)&(compact_hash-1);
        memcpy(lookup[at].cell,c,12);lookup[at].index=compact_hash+i*3;
        memcpy(neighbors[i],cells[i].probes,32);
        unsigned n=gi_cell_boxes(g,c,spacing,boxes);
        neighbors[i][8]=box_at;neighbors[i][9]=n;
        if(n!=UINT32_MAX)for(unsigned j=0;j<n;++j) {
            memcpy(box_data[box_at++],nodes[boxes[j]].lo,16);
            memcpy(box_data[box_at++],nodes[boxes[j]].hi,16);
        }
    }
    float *output=(float *)(data+sizeof(header)+(size_t)probe_offset*16);
    for(unsigned i=0;i<probes;++i)for(unsigned k=0;k<3;++k) {
        float p=(coords[i][k]+0.37f)*spacing;output[i*stride*4+(stride-1)*4+k]=p;
        header.minimum[k]=__builtin_fminf(header.minimum[k],p-spacing);
        header.maximum[k]=__builtin_fmaxf(header.maximum[k],p+spacing);
    }
    memcpy(data,&header,sizeof(header));
    if(indirect_upload(g,data,bytes,&next)<0)goto done;
    indirect_bind(g,binding,field,&next);
    if(daylight){g->daylight_probe_count=probes;g->daylight_cursor=0;g->daylight_warmed=0;}
    else {indirect_bind(g,13,&g->indirect_sources,&source_buffer);
        g->indirect_probe_count=probes;g->indirect_cursor=0;g->indirect_warmed=0;}
    fprintf(stderr,"rf-gpu-gi: field=%s static-lights=%u probes=%u spacing=%.0f rays=%u prepare-ms=%.3f bytes=%llu cached-cells=%u/%u visibility=%d cpu-index-bytes=%llu\n",
        daylight?"daylight":"fixtures",count,probes,spacing,RF_GI_RAYS,now_ms()-started,(unsigned long long)bytes,cached_cells,cell_count,g->indirect_visibility,
        (unsigned long long)g->indirect_architecture_nodes*sizeof(struct rf_gi_arch_node));
    g->receiver_ready=0;
    if(!daylight){g->indirect_source_count=count;memcpy(g->indirect_source_snapshot,lights,count*sizeof(*lights));}
    result=0;
done:
    free(data);free(cells);free(cell_slots);free(coords);free(slots);gpu_buffer_destroy(g->owner,&next);gpu_buffer_destroy(g->owner,&source_buffer);return result;
}

int rf_gpu_graphics_set_indirect_lights(struct rf_gpu_graphics *g,
    const struct rf_gpu_light *sources,uint32_t count)
{ return indirect_field_build(g,sources,count,0); }
int rf_gpu_graphics_prepare_daylight(struct rf_gpu_graphics *g)
{ return indirect_field_build(g,NULL,0,1); }

static void gfx_daylight_record(struct rf_gpu_graphics *g)
{
    if(!g->daylight_enabled || !g->indirect_enabled || g->lighting.disable_indirect ||
       g->lighting.sky_illuminance<=0 || !g->daylight_probe_count)return;
    struct rf_gpu_vulkan_impl *p=g->owner;
    uint32_t budget=g->daylight_warmed?128:g->daylight_probe_count;
    if(budget>g->daylight_probe_count)budget=g->daylight_probe_count;
    p->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->post_layout,1,1,&g->light_set,0,NULL);
    p->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->daylight_pipeline);
    g->api.CmdPushConstants(g->cmd,g->post_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&g->daylight_cursor);
    p->api.cmd_dispatch(g->cmd,budget,1,1);
    struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    p->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT|RF_VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    g->daylight_cursor=(g->daylight_cursor+budget)%g->daylight_probe_count;g->daylight_warmed=1;
}
