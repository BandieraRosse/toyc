#include "rf_gpu_raster_abi.h"
#include "rf_gpu_raster_bin.h"
#include "rf_gpu_raster_pack.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(x) do{if(!(x)){fprintf(stderr,"bin test failed:%d: %s\n",__LINE__,#x);goto done;}}while(0)
void rf_gpu_raster_bin_test_fail_after(int count);

static void init_stream(unsigned char *bytes,uint32_t w,uint32_t h,uint32_t n)
{
    struct rf_gpu_raster_stream_header_v1 *s=(void*)bytes;
    struct rf_gpu_raster_cmd_v1 *c=(void*)(s+1);memset(bytes,0,rf_gpu_raster_stream_size_v1(n));
    s->magic=RF_GPU_RASTER_ABI_MAGIC;s->version=1;s->header_size=32;s->command_size=96;
    s->command_count=n;s->framebuffer_width=w;s->framebuffer_height=h;s->endian_tag=RF_GPU_RASTER_ENDIAN_LITTLE;
    c[0].kind=RF_GPU_RASTER_CMD_CLEAR_COLOR_V1;c[0].byte_size=96;
    c[1].kind=RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1;c[1].byte_size=96;
}
static void tri(struct rf_gpu_raster_cmd_v1*c,int minx,int maxx,int miny,int maxy)
{
    c->kind=RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1;c->byte_size=96;
    c->flags=RF_GPU_RASTER_FLAG_DEPTH_TEST_V1|RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1|RF_GPU_RASTER_FLAG_OPAQUE_V1;
    c->payload.flat_triangle.a.x=minx;c->payload.flat_triangle.a.y=miny;
    c->payload.flat_triangle.b.x=maxx;c->payload.flat_triangle.b.y=miny;
    c->payload.flat_triangle.c.x=minx;c->payload.flat_triangle.c.y=maxy;
    c->payload.flat_triangle.area=-(int64_t)(maxx-minx)*(maxy-miny);
    c->payload.flat_triangle.bbox_minx=minx;c->payload.flat_triangle.bbox_maxx=maxx;
    c->payload.flat_triangle.bbox_miny=miny;c->payload.flat_triangle.bbox_maxy=maxy;
    c->payload.flat_triangle.light_q8=256;
}
int main(void)
{
    struct rf_gpu_raster_tile_lists l={0};unsigned char *b=NULL;int result=1;
    size_t size=rf_gpu_raster_stream_size_v1(6);b=malloc(size);CHECK(b);
    init_stream(b,33,31,6);struct rf_gpu_raster_cmd_v1*c=(void*)((struct rf_gpu_raster_stream_header_v1*)b+1);
    tri(&c[2],1,7,1,7);tri(&c[3],8,16,7,16);tri(&c[4],0,32,0,30);tri(&c[5],15,17,0,30);
    CHECK(rf_gpu_raster_validate_v1(b,size)==0);CHECK(rf_gpu_raster_bin_v1(b,size,16,16,&l)==0);
    CHECK(l.stats.tile_count==6&&l.stats.command_count==6&&l.stats.total_refs==27&&l.stats.max_refs_per_tile==6);
    for(uint32_t t=0;t<l.stats.tile_count;t++){CHECK(l.indices[l.offsets[t]]==0&&l.indices[l.offsets[t]+1]==1);for(uint32_t i=l.offsets[t]+1;i+1<l.offsets[t+1];i++)CHECK(l.indices[i]<l.indices[i+1]);}
    uint32_t *old_offsets=l.offsets,*old_indices=l.indices;uint64_t old_oc=l.offsets_capacity,old_ic=l.indices_capacity;
    CHECK(rf_gpu_raster_bin_v1(b,size,8,8,&l)==0);CHECK(l.stats.tile_count==20&&l.stats.total_refs==75);
    CHECK(l.offsets_capacity>=old_oc&&l.indices_capacity>=old_ic);
    old_offsets=l.offsets;old_indices=l.indices;
    /* Frozen V1 only permits the two leading clears; a mid-stream clear must reject. */
    c[3].kind=RF_GPU_RASTER_CMD_CLEAR_COLOR_V1;c[3].flags=0;memset(&c[3].payload,0,sizeof(c[3].payload));
    CHECK(rf_gpu_raster_bin_v1(b,size,8,8,&l)<0);
    CHECK(l.offsets==old_offsets&&l.indices==old_indices&&l.stats.total_refs==75);
    c[3].kind=RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1;tri(&c[3],8,16,7,16);
    ((struct rf_gpu_raster_stream_header_v1*)b)->framebuffer_width=257;
    ((struct rf_gpu_raster_stream_header_v1*)b)->framebuffer_height=257;
    rf_gpu_raster_bin_test_fail_after(3);
    CHECK(rf_gpu_raster_bin_v1(b,size,16,16,&l)<0);
    CHECK(l.offsets==old_offsets&&l.indices==old_indices&&l.stats.total_refs==75);
    rf_gpu_raster_bin_test_fail_after(-1);
    puts("GPU Raster tile binning: PASS (single/boundary/multi/full/thin/non-multiple/8x8/16x16/order/growth/oversize/partial-allocation-cleanup)");result=0;
done:free(b);rf_gpu_raster_tile_lists_destroy(&l);return result;
}
