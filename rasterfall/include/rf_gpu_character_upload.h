#ifndef RF_GPU_CHARACTER_UPLOAD_H
#define RF_GPU_CHARACTER_UPLOAD_H
/* Shared replacement transaction for all character producers. Caller retains
 * the immutable bind words, even when only a palette is being updated. The
 * graphics owner enforces reader retirement and caches completed skin work. */
static int rf_gpu_character_upload(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource **resource,unsigned count,const uint32_t *indices,
    const uint32_t *bind,int changed,const uint32_t *palette,unsigned palette_count)
{
    int grow=*resource?rf_gpu_graphics_skinned_resource_update(g,*resource,count,
        changed?bind:NULL,changed?count*22:0,palette,palette_count):1;
    if(grow<=0)return grow;
    const uint32_t white=0xffffff;
    struct rf_gpu_graphics_resource *next=rf_gpu_graphics_skinned_resource_create(g,
        NULL,count,indices,count,bind,count*22,palette,palette_count,&white,1,1);
    if(!next)return -1;
    if(*resource && rf_gpu_graphics_resource_destroy(g,*resource)<0) {
        rf_gpu_graphics_resource_destroy(g,next);return -1;
    }
    *resource=next;return 1;
}
#endif
