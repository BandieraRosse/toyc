#include "rf_gpu_raster_bin.h"
#include "rf_gpu_raster_abi.h"
#include "rf_gpu_raster_pack.h"

#include <limits.h>
#include <stdlib.h>
#include <string.h>

#ifdef RF_GPU_RASTER_BIN_TESTING
static int bin_fail_after=-1;
void rf_gpu_raster_bin_test_fail_after(int count){bin_fail_after=count;}
static void *bin_malloc(size_t size){if(bin_fail_after==0)return NULL;if(bin_fail_after>0)bin_fail_after--;return malloc(size);}
static void *bin_calloc(size_t n,size_t size){if(bin_fail_after==0)return NULL;if(bin_fail_after>0)bin_fail_after--;return calloc(n,size);}
#else
#define bin_malloc malloc
#define bin_calloc calloc
#endif

static int command_tiles(const struct rf_gpu_raster_cmd_v1 *cmd,
                         uint32_t width, uint32_t height,
                         uint32_t tw, uint32_t th,
                         uint32_t *x0, uint32_t *x1,
                         uint32_t *y0, uint32_t *y1)
{
    if (cmd->kind == RF_GPU_RASTER_CMD_CLEAR_COLOR_V1 ||
        cmd->kind == RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1) {
        *x0 = *y0 = 0; *x1 = (width - 1) / tw; *y1 = (height - 1) / th;
        return 1;
    }
    if (cmd->kind == RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1 ||
        cmd->kind == RF_GPU_RASTER_CMD_VERTEX_LIT_TRIANGLE_V1 ||
        cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1) {
        const struct rf_gpu_raster_flat_triangle_v1 *t = &cmd->payload.flat_triangle;
        int64_t minx, maxx, miny, maxy;
        if (cmd->kind == RF_GPU_RASTER_CMD_TEXTURED_TRIANGLE_V1) {
            minx=t->a.x<t->b.x?(t->a.x<t->c.x?t->a.x:t->c.x):(t->b.x<t->c.x?t->b.x:t->c.x);
            maxx=(t->a.x>t->b.x?(t->a.x>t->c.x?t->a.x:t->c.x):(t->b.x>t->c.x?t->b.x:t->c.x))+1;
            miny=t->a.y<t->b.y?(t->a.y<t->c.y?t->a.y:t->c.y):(t->b.y<t->c.y?t->b.y:t->c.y);
            maxy=(t->a.y>t->b.y?(t->a.y>t->c.y?t->a.y:t->c.y):(t->b.y>t->c.y?t->b.y:t->c.y))+1;
        } else {
            minx=t->bbox_minx; maxx=t->bbox_maxx;
            miny=t->bbox_miny; maxy=t->bbox_maxy;
        }
        if (maxx < 0 || maxy < 0 || minx >= width || miny >= height) return 0;
        if (minx < 0) minx=0;
        if (miny < 0) miny=0;
        if (maxx >= width) maxx=width-1;
        if (maxy >= height) maxy=height-1;
        *x0=(uint32_t)minx/tw; *x1=(uint32_t)maxx/tw;
        *y0=(uint32_t)miny/th; *y1=(uint32_t)maxy/th;
        return 1;
    }
    return -1;
}

int rf_gpu_raster_bin_v1(const void *stream, unsigned long stream_size,
                         uint32_t tw, uint32_t th,
                         struct rf_gpu_raster_tile_lists *lists)
{
    const struct rf_gpu_raster_stream_header_v1 *h=stream;
    const struct rf_gpu_raster_cmd_v1 *cmds;
    uint32_t tx,ty,tile_count,*counts=NULL,*cursor=NULL,*new_offsets=NULL,*new_indices=NULL;
    uint64_t total=0, offsets_bytes, indices_bytes,new_offsets_capacity,new_indices_capacity;
    int replace_offsets=0,replace_indices=0;
    if (!lists || !tw || !th || rf_gpu_raster_validate_v1(stream,stream_size)) return -1;
    tx=(h->framebuffer_width+tw-1)/tw; ty=(h->framebuffer_height+th-1)/th;
    if (!tx || !ty || (uint64_t)tx*ty > 0xffffffffULL) return -1;
    tile_count=tx*ty; cmds=(const void *)(h+1);
    if ((uint64_t)(tile_count+1)*4 > (size_t)-1) return -1;
    counts=bin_calloc(tile_count,sizeof(*counts)); cursor=bin_malloc((size_t)tile_count*4);
    if (!counts || !cursor) goto fail;
    for (uint32_t i=0;i<h->command_count;i++) {
        uint32_t x0,x1,y0,y1; int covered=command_tiles(&cmds[i],h->framebuffer_width,h->framebuffer_height,tw,th,&x0,&x1,&y0,&y1);
        if (covered < 0) goto fail;
        if (!covered) continue;
        for(uint32_t y=y0;y<=y1;y++)for(uint32_t x=x0;x<=x1;x++){
            uint32_t tile=y*tx+x; if(counts[tile]==0xffffffffU||total==0xffffffffULL)goto fail;
            counts[tile]++; total++;
        }
    }
    offsets_bytes=(uint64_t)(tile_count+1)*4; indices_bytes=total*4;
    if(offsets_bytes>(size_t)-1||indices_bytes>(size_t)-1)goto fail;
    new_offsets=lists->offsets;new_indices=lists->indices;
    new_offsets_capacity=lists->offsets_capacity;new_indices_capacity=lists->indices_capacity;
    if(offsets_bytes>new_offsets_capacity){
        new_offsets_capacity=new_offsets_capacity?new_offsets_capacity:64;
        while(new_offsets_capacity<offsets_bytes){if(new_offsets_capacity>(uint64_t)(size_t)-1/2){new_offsets_capacity=offsets_bytes;break;}new_offsets_capacity*=2;}
        new_offsets=bin_malloc((size_t)new_offsets_capacity);if(!new_offsets)goto fail;replace_offsets=1;
    }
    if(indices_bytes>new_indices_capacity){
        new_indices_capacity=new_indices_capacity?new_indices_capacity:256;
        while(new_indices_capacity<indices_bytes){if(new_indices_capacity>(uint64_t)(size_t)-1/2){new_indices_capacity=indices_bytes;break;}new_indices_capacity*=2;}
        new_indices=bin_malloc((size_t)new_indices_capacity);if(!new_indices)goto fail;replace_indices=1;
    }
    new_offsets[0]=0;
    for(uint32_t i=0;i<tile_count;i++){new_offsets[i+1]=new_offsets[i]+counts[i];cursor[i]=new_offsets[i];}
    for (uint32_t i=0;i<h->command_count;i++) {
        uint32_t x0,x1,y0,y1; int covered=command_tiles(&cmds[i],h->framebuffer_width,h->framebuffer_height,tw,th,&x0,&x1,&y0,&y1);
        if(covered<=0)continue;
        for(uint32_t y=y0;y<=y1;y++)for(uint32_t x=x0;x<=x1;x++)new_indices[cursor[y*tx+x]++]=i;
    }
    if(replace_offsets){free(lists->offsets);lists->offsets=new_offsets;lists->offsets_capacity=new_offsets_capacity;}
    if(replace_indices){free(lists->indices);lists->indices=new_indices;lists->indices_capacity=new_indices_capacity;}
    lists->stats.command_count=h->command_count;lists->stats.tile_count=tile_count;lists->stats.total_refs=total;lists->stats.max_refs_per_tile=0;
    for(uint32_t i=0;i<tile_count;i++)if(counts[i]>lists->stats.max_refs_per_tile)lists->stats.max_refs_per_tile=counts[i];
    free(counts);free(cursor);return 0;
fail:
    free(counts);free(cursor);if(replace_offsets)free(new_offsets);if(replace_indices)free(new_indices);return -1;
}

void rf_gpu_raster_tile_lists_destroy(struct rf_gpu_raster_tile_lists *lists)
{
    if(!lists)return;
    free(lists->offsets);free(lists->indices);memset(lists,0,sizeof(*lists));
}
