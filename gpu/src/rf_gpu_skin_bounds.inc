/* Immutable bind influence boxes plus current palette. Convex weights keep
 * blended positions inside the AABB enclosing both transformed bone boxes. */
static void gfx_skin_boxes_bind(struct rf_gpu_graphics_resource *r,
    const uint32_t *bind,uint32_t vertices,uint32_t words)
{
    free(r->skin_boxes);r->skin_boxes=NULL;r->skin_box_count=0;
    r->skin_bounds_valid=0;
    if(!bind || !words || words%15 || words/15>65536)return;
    uint32_t bones=words/15;
    struct rf_skin_bone_box *boxes=calloc(bones,sizeof(*boxes));
    if(!boxes)return;
    for(uint32_t v=0;v<vertices;++v) {
        const uint32_t *p=bind+(size_t)v*22;
        uint32_t ids[2]={p[14]&65535u,p[14]>>16};
        unsigned n=((p[15]>>16)&255u)?2:1;
        for(unsigned j=0;j<n;++j) {
            if(ids[j]>=bones){free(boxes);return;}
            struct rf_skin_bone_box *b=&boxes[ids[j]];
            for(unsigned k=0;k<3;++k) {
                int q=(int32_t)p[k];
                if(!b->used || q<b->lo[k])b->lo[k]=q;
                if(!b->used || q>b->hi[k])b->hi[k]=q;
            }
            b->used=1;
        }
    }
    r->skin_boxes=boxes;r->skin_box_count=bones;r->skin_box_vertices=vertices;
}
static void gfx_skin_boxes_pose(struct rf_gpu_graphics_resource *r,
    const uint32_t *palette,uint32_t words,uint32_t vertices)
{
    r->skin_bounds_valid=0;
    if(!r->skin_boxes || !palette || words/15!=r->skin_box_count || words%15 ||
       vertices>r->skin_box_vertices)return;
    double lo[3]={0},hi[3]={0};int used=0;
    for(uint32_t i=0;i<r->skin_box_count;++i) {
        const struct rf_skin_bone_box *b=&r->skin_boxes[i];
        if(!b->used)continue;
        const uint32_t *p=palette+(size_t)i*15;float f[12];memcpy(f,p,sizeof(f));
        double c[3],e[3],input[3];
        for(unsigned k=0;k<3;++k) {
            c[k]=((double)b->lo[k]+b->hi[k])*0.5-(int32_t)p[12+k];
            e[k]=((double)b->hi[k]-b->lo[k])*0.5;
            input[k]=__builtin_fabs((double)b->lo[k]);
            if(__builtin_fabs((double)b->hi[k])>input[k])
                input[k]=__builtin_fabs((double)b->hi[k]);
            input[k]+=__builtin_fabs((double)(int32_t)p[12+k]);
        }
        for(unsigned k=0;k<3;++k) {
            double center=f[9+k],extent=0,error=__builtin_fabs(center);
            for(unsigned j=0;j<3;++j) {
                center+=f[k*3+j]*c[j];
                extent+=__builtin_fabs(f[k*3+j])*e[j];
                error+=__builtin_fabs(f[k*3+j])*(input[j]+__builtin_fabs(c[j])+e[j]);
            }
            /* Covers FP32 subtract/dot/blend and integer round; fallback for
             * nonfinite or unrepresentable public palette inputs. */
            extent+=4.0+error*0.00001;
            double a=center-extent,z=center+extent;
            if(!__builtin_isfinite(a) || !__builtin_isfinite(z) ||
               a<-2147483000.0 || z>2147483000.0)return;
            if(!used || a<lo[k])lo[k]=a;
            if(!used || z>hi[k])hi[k]=z;
        }
        used=1;
    }
    if(!used)return;
    for(unsigned k=0;k<3;++k) {
        r->position_min[k]=(int)__builtin_floor(lo[k]);
        r->position_max[k]=(int)__builtin_ceil(hi[k]);
    }
    r->skin_bounds_valid=1;
}
