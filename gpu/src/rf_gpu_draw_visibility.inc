/* Per-consumer masks: camera plus independent shadow views. Compute resource
 * transforms once, before skin dispatch or any pass records draw commands. */
static int gfx_draw_visibility_prepare(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count)
{
    if(count>g->draw_visibility_capacity) {
        unsigned char *next=realloc(g->draw_visibility,count);
        if(!next)return -1;
        g->draw_visibility=next;g->draw_visibility_capacity=count;
    }
    for(struct rf_gpu_graphics_resource *r=g->skin_pending;r;r=r->skin_pending_next)
        r->skin_needed_by=NULL;
    for(struct rf_graphics_reader *r=g->borrowed;r;r=r->graphics_next)
        r->resource->skin_needed_by=NULL;
    float planes[RF_GPU_SHADOW_MAPS][6][4];
    for(unsigned s=0;s<g->shadow_count;++s)for(unsigned p=0;p<6;++p) {
        const float *m=g->shadow_matrix[s];unsigned row=p/2;
        for(unsigned k=0;k<4;++k)
            planes[s][p][k]=p==4?m[k*4+2]:m[k*4+3]+((p&1)?-1:1)*m[k*4+row];
    }
    for(uint32_t i=0;i<count;++i) {
        const struct rf_gpu_graphics_draw *d=&items[i].draw;
        float c[3],e[3];unsigned mask=1;
        int world=d->scene_layer==RF_GPU_SCENE_WORLD;
        int camera=world || d->scene_layer==RF_GPU_SCENE_TRANSPARENT;
        int shadow=world && !d->texture[2] && !d->texture[3] &&
            !(d->architecture_occluder && g->architecture_count && !g->legacy_architecture_shadows);
        int box=!g->disable_draw_cull && (camera || shadow) && gfx_draw_box(&items[i],c,e);
        if(box && camera && gfx_camera_box_outside(d,c,e))mask=0;
        if(shadow)for(unsigned s=0;s<g->shadow_count;++s) {
            int outside=0;
            for(unsigned p=0;box && p<6 && !outside;++p)
                outside=gfx_box_outside(c,e,planes[s][p]);
            if(!outside)mask|=2u<<s;
        }
        g->draw_visibility[i]=(unsigned char)mask;
        if(mask && d->scene_layer!=RF_GPU_SCENE_SKY)
            items[i].resource->skin_needed_by=g;
    }
    return 0;
}
