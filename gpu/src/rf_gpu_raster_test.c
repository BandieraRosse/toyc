#include "rf_gpu.h"
#include "rf_gpu_raster_pack.h"
#include "rf_gpu_vulkan_backend.h"
#include "rf_gpu_graphics.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(c) do { if (!(c)) { fprintf(stderr, "raster test failed:%d\n", \
    __LINE__); goto done; } } while (0)

struct fixture {
    unsigned char *bytes;
    size_t size;
    struct rf_gpu_raster_stream_header_v1 *header;
    struct rf_gpu_raster_cmd_v1 *commands;
};

static int fixture_init(struct fixture *f, uint32_t width, uint32_t height,
                        uint32_t triangle_capacity, uint32_t clear_color)
{
    uint32_t count = triangle_capacity + 2;
    memset(f, 0, sizeof(*f));
    f->size = rf_gpu_raster_stream_size_v1(count);
    f->bytes = calloc(1, f->size);
    if (!f->bytes) return -1;
    f->header = (void *)f->bytes;
    f->commands = (void *)(f->header + 1);
    f->header->magic = RF_GPU_RASTER_ABI_MAGIC;
    f->header->version = RF_GPU_RASTER_ABI_VERSION;
    f->header->header_size = RF_GPU_RASTER_STREAM_HEADER_V1_SIZE;
    f->header->command_size = RF_GPU_RASTER_CMD_V1_SIZE;
    f->header->command_count = count;
    f->header->framebuffer_width = width;
    f->header->framebuffer_height = height;
    f->header->endian_tag = RF_GPU_RASTER_ENDIAN_LITTLE;
    f->commands[0].kind = RF_GPU_RASTER_CMD_CLEAR_COLOR_V1;
    f->commands[0].byte_size = RF_GPU_RASTER_CMD_V1_SIZE;
    f->commands[0].payload.clear.value = clear_color;
    f->commands[1].kind = RF_GPU_RASTER_CMD_CLEAR_DEPTH_V1;
    f->commands[1].byte_size = RF_GPU_RASTER_CMD_V1_SIZE;
    f->commands[1].payload.clear.value = 0;
    return 0;
}

static void triangle(struct fixture *f, uint32_t index,
                     int ax, int ay, int az, int bx, int by, int bz,
                     int cx, int cy, int cz, uint32_t color,
                     int light, int fog)
{
    struct rf_gpu_raster_cmd_v1 *cmd = &f->commands[index + 2];
    struct rf_gpu_raster_flat_triangle_v1 *t = &cmd->payload.flat_triangle;
    int minx = ax < bx ? (ax < cx ? ax : cx) : (bx < cx ? bx : cx);
    int maxx = ax > bx ? (ax > cx ? ax : cx) : (bx > cx ? bx : cx);
    int miny = ay < by ? (ay < cy ? ay : cy) : (by < cy ? by : cy);
    int maxy = ay > by ? (ay > cy ? ay : cy) : (by > cy ? by : cy);
    int width = (int)f->header->framebuffer_width;
    int height = (int)f->header->framebuffer_height;
    cmd->kind = RF_GPU_RASTER_CMD_FLAT_TRIANGLE_V1;
    cmd->byte_size = RF_GPU_RASTER_CMD_V1_SIZE;
    cmd->flags = RF_GPU_RASTER_FLAG_DEPTH_TEST_V1 |
                 RF_GPU_RASTER_FLAG_DEPTH_WRITE_V1 |
                 RF_GPU_RASTER_FLAG_OPAQUE_V1;
    if (fog) cmd->flags |= RF_GPU_RASTER_FLAG_FOG_V1;
    t->a.x = ax; t->a.y = ay; t->a.inv_z = az;
    t->b.x = bx; t->b.y = by; t->b.inv_z = bz;
    t->c.x = cx; t->c.y = cy; t->c.inv_z = cz;
    t->area = ((int64_t)cx - ax) * ((int64_t)by - ay) -
              ((int64_t)cy - ay) * ((int64_t)bx - ax);
    t->bbox_minx = minx < 0 ? 0 : minx;
    t->bbox_maxx = maxx >= width ? width - 1 : maxx;
    t->bbox_miny = miny < 0 ? 0 : miny;
    t->bbox_maxy = maxy >= height ? height - 1 : maxy;
    t->color = color; t->light_q8 = light; t->fog_q8 = fog;
}

static uint64_t hash_words(const uint32_t *words, uint32_t width,
                           uint32_t height, uint32_t stride)
{
    uint64_t hash = 1469598103934665603ULL;
    uint32_t x, y, byte;
    for (y = 0; y < height; ++y) for (x = 0; x < width; ++x) {
        uint32_t value = words[(size_t)y * stride + x];
        for (byte = 0; byte < 4; ++byte) {
            hash ^= (value >> (byte * 8)) & 255u;
            hash *= 1099511628211ULL;
        }
    }
    return hash;
}

static int execute(struct rf_gpu *gpu, struct rf_gpu_raster *raster,
                   struct fixture *f, uint32_t **color_out, int32_t **depth_out,
                   uint64_t *color_hash, uint64_t *depth_hash)
{
    uint32_t width = f->header->framebuffer_width;
    uint32_t height = f->header->framebuffer_height;
    uint32_t stride = width + 5;
    uint32_t *color = malloc((size_t)stride * height * 4);
    int32_t *depth = malloc((size_t)stride * height * 4);
    uint32_t x, y;
    if (!color || !depth) { free(color); free(depth); return -1; }
    for (y = 0; y < height; ++y) for (x = 0; x < stride; ++x) {
        color[(size_t)y * stride + x] = 0x13579bdfu;
        depth[(size_t)y * stride + x] = 0x12345678;
    }
    if (rf_gpu_raster_render(gpu, raster, f->bytes, f->size, color, depth,
                             width, height, stride, stride) < 0) {
        free(color); free(depth); return -1;
    }
    for (y = 0; y < height; ++y) for (x = width; x < stride; ++x)
        if (color[(size_t)y * stride + x] != 0x13579bdfu ||
            depth[(size_t)y * stride + x] != 0x12345678) {
            free(color); free(depth); return -1;
        }
    *color_hash = hash_words(color, width, height, stride);
    *depth_hash = hash_words((const uint32_t *)depth, width, height, stride);
    *color_out = color; *depth_out = depth;
    return (int)stride;
}

static uint32_t expected_shade(uint32_t color, int light, int fog)
{
    int r = (color >> 16) & 255, g = (color >> 8) & 255, b = color & 255;
    if (light < 0) light = 0;
    if (light > 384) light = 384;
    r = r * light / 256; g = g * light / 256; b = b * light / 256;
    if (r > 255) r = 255;
    if (g > 255) g = 255;
    if (b > 255) b = 255;
    if (fog < 0) fog = 0;
    if (fog > 256) fog = 256;
    r = (r * (256 - fog) + 28 * fog) / 256;
    g = (g * (256 - fog) + 33 * fog) / 256;
    b = (b * (256 - fog) + 40 * fog) / 256;
    return 0xff000000u | (uint32_t)r << 16 | (uint32_t)g << 8 | (uint32_t)b;
}

static int segmented_test(struct rf_gpu_vulkan_context *context, unsigned int group,
                          int full_scan, int fog)
{
    enum { W=19, H=13, S=24 };
    struct fixture f = {0};
    struct rf_gpu_post_params_v1 post = {0};
    void *r = NULL;
    uint32_t expected[S*H], actual[S*H];
    int expected_depth[S*H], actual_depth[S*H];
    char message[256];
    int result = -1;
    unsigned int i;
#define SEG(a,b,mode,last) rf_gpu_vulkan_raster_segment(context,r,f.bytes, \
    (unsigned long)f.size,NULL,0,NULL,0,a,b,mode,last, \
    last?actual:NULL,last?actual_depth:NULL,W,H,S,S,message,sizeof(message))
    CHECK(!fixture_init(&f,W,H,8,0x102030));
    for (i=0;i<8;i++) triangle(&f,i,2,2,100,16,2,100,2,11,100,
                               0x102030*(i+1),256,0);
    /* Far rejection, equal-depth replacement, transparent no-depth-write,
     * then a VIEWMODEL triangle behind the world in its independent domain. */
    f.commands[3].payload.flat_triangle.a.inv_z=50;
    f.commands[3].payload.flat_triangle.b.inv_z=50;
    f.commands[3].payload.flat_triangle.c.inv_z=50;
    memset(&f.commands[5],0,sizeof(f.commands[5]));
    f.commands[5].kind=RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1;
    f.commands[5].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
    for(i=6;i<8;i++) {
        f.commands[i].flags=RF_GPU_RASTER_FLAG_DEPTH_TEST_V1|RF_GPU_RASTER_FLAG_SOURCE_OVER_V1;
        f.commands[i].payload.flat_triangle.reserved[0]=128;
    }
    memset(&f.commands[8],0,sizeof(f.commands[8]));
    f.commands[8].kind=RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1;
    f.commands[8].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
    triangle(&f,7,4,4,1,10,4,1,4,9,1,0xff0080,256,0);
    CHECK(!rf_gpu_raster_validate_v1(f.bytes,f.size));
    CHECK(!rf_gpu_vulkan_backend.raster_create(context,W,H,group,group,&r,message,sizeof(message)));
    rf_gpu_vulkan_backend.raster_set_full_scan_diagnostic(context,r,full_scan);
    post.mode=fog?RF_GPU_POST_DEPTH_FOG_V0:RF_GPU_POST_DISABLED;
    post.fog_near_inv_z=200;post.fog_far_inv_z=0;
    post.fog_color=0x90a0b0;post.max_density_q8=192;
    CHECK(!rf_gpu_vulkan_backend.raster_set_post(context,r,&post));
    memset(expected,0x35,sizeof(expected));memset(actual,0x35,sizeof(actual));
    memset(expected_depth,0x47,sizeof(expected_depth));memset(actual_depth,0x47,sizeof(actual_depth));
    CHECK(SEG(2,10,RF_GPU_RASTER_LOAD_EXISTING,1)<0);
    CHECK(!rf_gpu_vulkan_backend.raster_render(context,r,f.bytes,(unsigned long)f.size,
        NULL,0,NULL,0,expected,expected_depth,W,H,S,S,NULL,message,sizeof(message)));
    CHECK(SEG(2,10,RF_GPU_RASTER_LOAD_EXISTING,1)<0);
    CHECK(SEG(0,9,RF_GPU_RASTER_CLEAR,0)<0); /* illegal VIEWMODEL cut */
    CHECK(!SEG(0,3,RF_GPU_RASTER_CLEAR,0));
    /* Poison consumed commands, while leaving a fully valid ABI stream.
     * Replaying the prefix (or an old shader ignoring ranges) must fail. */
    f.commands[0].payload.clear.value=0xabcdef;
    f.commands[2].payload.flat_triangle.color=0xff0000;
    f.commands[9].byte_size=0; /* invalid OUTSIDE selected segment */
    CHECK(SEG(3,4,RF_GPU_RASTER_LOAD_EXISTING,0)<0);
    f.commands[9].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
    f.header->endian_tag=0;
    CHECK(SEG(3,4,RF_GPU_RASTER_LOAD_EXISTING,0)<0);
    f.header->endian_tag=RF_GPU_RASTER_ENDIAN_LITTLE;
    CHECK(SEG(0,3,RF_GPU_RASTER_LOAD_EXISTING,0)<0);
    CHECK(SEG(3,11,RF_GPU_RASTER_LOAD_EXISTING,0)<0);
    CHECK(SEG(9,10,RF_GPU_RASTER_LOAD_EXISTING,1)<0);
    CHECK(!SEG(3,5,RF_GPU_RASTER_LOAD_EXISTING,0));
    CHECK(!SEG(5,7,RF_GPU_RASTER_LOAD_EXISTING,0));
    CHECK(!SEG(7,7,RF_GPU_RASTER_LOAD_EXISTING,0)); /* empty preserves target */
    CHECK(!SEG(7,10,RF_GPU_RASTER_LOAD_EXISTING,1));
    CHECK(!memcmp(expected,actual,sizeof(actual)));
    CHECK(!memcmp(expected_depth,actual_depth,sizeof(actual_depth)));
    CHECK(SEG(7,10,RF_GPU_RASTER_LOAD_EXISTING,1)<0);
    f.commands[0].payload.clear.value=0x102030;
    f.commands[2].payload.flat_triangle.color=0x102030;
    CHECK(!SEG(0,10,RF_GPU_RASTER_CLEAR,1));
    CHECK(!memcmp(expected,actual,sizeof(actual)));
    printf("segmented-clear-load: group=%u full_scan=%d fog=%d exact color/depth/stride PASS\n",group,full_scan,fog);
    result=0;
done:
    rf_gpu_vulkan_backend.raster_destroy(context,r);
    free(f.bytes);
    return result;
#undef SEG
}

/* Each tile sees gaps in the global command IDs and segments with no local
 * entries. Exercise lower_bound at the beginning, middle and end of a list. */
static int sparse_segment_test(struct rf_gpu_vulkan_context *context,
                               unsigned int group, int full_scan)
{
    enum { W=65, H=17, S=68, N=18 };
    struct fixture f={0};
    void *r=NULL;
    uint32_t expected[S*H],actual[S*H];
    int expected_depth[S*H],actual_depth[S*H];
    char message[256];
    int result=-1;
    CHECK(!fixture_init(&f,W,H,N,0x102030));
    for(unsigned i=0;i<N;++i) {
        int x=(i%3)*24;
        triangle(&f,i,x,1,100+(int)i,x+12,1,100+(int)i,x,15,100+(int)i,
            0x010203*(i+1),256,0);
    }
    CHECK(!rf_gpu_vulkan_backend.raster_create(context,W,H,group,group,&r,message,sizeof(message)));
    rf_gpu_vulkan_backend.raster_set_full_scan_diagnostic(context,r,full_scan);
    memset(expected,0x35,sizeof(expected));memset(actual,0x35,sizeof(actual));
    memset(expected_depth,0x47,sizeof(expected_depth));memset(actual_depth,0x47,sizeof(actual_depth));
    CHECK(!rf_gpu_vulkan_backend.raster_render(context,r,f.bytes,(unsigned long)f.size,
        NULL,0,NULL,0,expected,expected_depth,W,H,S,S,NULL,message,sizeof(message)));
    for(unsigned first=0;first<N+2;) {
        unsigned end=first ? first+1 : 2;
        int final=end==N+2;
        CHECK(!rf_gpu_vulkan_raster_segment(context,r,f.bytes,(unsigned long)f.size,
            NULL,0,NULL,0,first,end,first?RF_GPU_RASTER_LOAD_EXISTING:RF_GPU_RASTER_CLEAR,
            final,final?actual:NULL,final?actual_depth:NULL,W,H,S,S,message,sizeof(message)));
        first=end;
    }
    CHECK(!memcmp(expected,actual,sizeof(actual)));
    CHECK(!memcmp(expected_depth,actual_depth,sizeof(actual_depth)));
    printf("sparse-segments: group=%u full_scan=%d exact color/depth/stride PASS\n",group,full_scan);
    result=0;
done:
    rf_gpu_vulkan_backend.raster_destroy(context,r);
    free(f.bytes);
    return result;
}

/* Full-viewport graphics quad has no exterior edge inside the target.
 * Its reference placeholders use one oversized raster triangle, allowing
 * byte-exact comparisons without masking hardware edge differences. */
static int mixed_test(struct rf_gpu_vulkan_context *context, unsigned group,
                      int full_scan, int fog, unsigned width)
{
    enum { MAX=48*29 };
    unsigned height=width==19?13:23, stride=width+5;
    struct fixture f={0};
    struct rf_gpu_graphics *g=NULL;
    struct rf_gpu_graphics_vertex v[4]={0};
    const uint32_t indices[]={0,1,2,0,2,3}, texel=0xffffff;
    struct rf_gpu_graphics_draw d={0}, bad[2];
    struct rf_gpu_graphics_stats before,after;
    struct rf_gpu_post_params_v1 post={0};
    uint32_t expected[MAX],actual[MAX];
    int expected_depth[MAX],actual_depth[MAX];
    void *r=NULL,*fresh=NULL;
    char message[256];
    int result=-1;
#define MIX(a,b,mode,last) rf_gpu_vulkan_raster_segment(context,r,f.bytes, \
    (unsigned long)f.size,NULL,0,NULL,0,a,b,mode,last,last?actual:NULL, \
    last?actual_depth:NULL,width,height,stride,stride,message,sizeof(message))
    CHECK(!fixture_init(&f,width,height,10,0x102030));
    /* Two graphics placeholders at 3/5, interleaved compute at 2/4. */
    for(unsigned i=0;i<10;++i)
        triangle(&f,i,-64,-64,4096,128,-64,4096,-64,128,4096,0x983721,256,0);
    triangle(&f,0,1,1,8192,8,1,8192,1,9,8192,0x1256ab,256,0);
    triangle(&f,2,10,2,8192,17,2,8192,10,10,8192,0x56ab12,256,0);
    f.commands[5].payload.flat_triangle.color=0x219837;
    /* Equal depth compute wins after graphics; far compute loses. */
    triangle(&f,4,3,8,4096,9,8,4096,3,12,4096,0x371298,256,0);
    triangle(&f,5,1,1,100,17,1,100,1,11,100,0xff00ff,256,0);
    memset(&f.commands[8],0,sizeof(f.commands[8]));
    f.commands[8].kind=RF_GPU_RASTER_CMD_BEGIN_TRANSPARENT_V1;
    f.commands[8].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
    triangle(&f,7,5,4,6000,14,4,6000,5,11,6000,0xabcdef,256,0);
    f.commands[9].flags=RF_GPU_RASTER_FLAG_DEPTH_TEST_V1|RF_GPU_RASTER_FLAG_SOURCE_OVER_V1;
    f.commands[9].payload.flat_triangle.reserved[0]=128;
    memset(&f.commands[10],0,sizeof(f.commands[10]));
    f.commands[10].kind=RF_GPU_RASTER_CMD_BEGIN_VIEWMODEL_V1;
    f.commands[10].byte_size=RF_GPU_RASTER_CMD_V1_SIZE;
    triangle(&f,9,2,2,1,6,2,1,2,6,1,0xfedcba,256,0);
    CHECK(!rf_gpu_raster_validate_v1(f.bytes,f.size));
    CHECK(!rf_gpu_vulkan_backend.raster_create(context,width,height,group,group,&r,message,sizeof(message)));
    CHECK(!rf_gpu_vulkan_backend.raster_create(context,width,height,group,group,&fresh,message,sizeof(message)));
    rf_gpu_vulkan_backend.raster_set_full_scan_diagnostic(context,r,full_scan);
    post.mode=fog?RF_GPU_POST_DEPTH_FOG_V0:RF_GPU_POST_DISABLED;
    post.fog_near_inv_z=10000;post.fog_far_inv_z=0;
    post.fog_color=0x90a0b0;post.max_density_q8=192;
    CHECK(!rf_gpu_vulkan_backend.raster_set_post(context,r,&post));
    CHECK((g=rf_gpu_graphics_create(context))!=NULL);
    for(unsigned i=0;i<4;++i) {
        v[i].position[0]=(i==1 || i==2)?256:-256;
        v[i].position[1]=i>=2?256:-256;
    }
    CHECK(!rf_gpu_graphics_upload(g,v,4,indices,6,&texel,1,1));
    CHECK(!rf_gpu_graphics_resize(g,width,height));
    d.translation_scale[2]=256;d.translation_scale[3]=1000;
    d.rotation[1]=1024;d.view[1]=d.view[3]=1024;
    d.projection[0]=width;d.projection[1]=height;
    d.projection[2]=64;d.projection[3]=width*3/4;
    d.material[0]=0x983721;d.material[1]=256;
    d.texture[0]=d.texture[1]=1;
    d.index_count=6;d.double_sided=1;d.integer_depth=1;
    CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
    CHECK(rf_gpu_graphics_raster_draw(g,fresh,&d,1)<0);
    rf_gpu_graphics_get_stats(g,&before);
    memset(expected,0x35,sizeof(expected));memset(actual,0x35,sizeof(actual));
    memset(expected_depth,0x47,sizeof(expected_depth));memset(actual_depth,0x47,sizeof(actual_depth));
    CHECK(!rf_gpu_vulkan_backend.raster_render(context,r,f.bytes,(unsigned long)f.size,
        NULL,0,NULL,0,expected,expected_depth,width,height,stride,stride,NULL,message,sizeof(message)));
    CHECK(expected[3*stride+3]==0xfffedcba && expected_depth[3*stride+3]==8192);
    for(unsigned repeat=0;repeat<2;++repeat) {
        d.material[0]=0x983721;
        CHECK(!MIX(0,3,RF_GPU_RASTER_CLEAR,0));
        bad[0]=bad[1]=d;bad[1].index_count=7;
        CHECK(rf_gpu_graphics_raster_draw(g,r,bad,2)<0); /* all draws preflight */
        bad[1]=d;bad[1].integer_depth=0;
        CHECK(rf_gpu_graphics_raster_draw(g,r,bad,2)<0);
        CHECK(!rf_gpu_graphics_resize(g,width+1,height));
        CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
        CHECK(!rf_gpu_graphics_resize(g,width,height));
        /* Shared color target resize invalidates the prior Raster image. */
        CHECK(MIX(3,4,RF_GPU_RASTER_LOAD_EXISTING,0)<0);
        CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
        CHECK(!MIX(0,3,RF_GPU_RASTER_CLEAR,0));
        CHECK(!rf_gpu_graphics_raster_draw(g,r,&d,1));
        /* Change consumed prefix: replaying it would destroy the result. */
        f.commands[2].payload.flat_triangle.color=0xff00ff;
        CHECK(!MIX(4,5,RF_GPU_RASTER_LOAD_EXISTING,0));
        d.material[0]=0x219837;
        CHECK(!rf_gpu_graphics_raster_draw(g,r,&d,1));
        /* Back-to-back graphics reuse, farther geometry must not overwrite. */
        d.translation_scale[2]=512;d.material[0]=0xff00ff;
        CHECK(!rf_gpu_graphics_raster_draw(g,r,&d,1));
        d.translation_scale[2]=256;
        CHECK(!MIX(6,12,RF_GPU_RASTER_LOAD_EXISTING,1));
        CHECK(!memcmp(expected,actual,sizeof(actual)));
        CHECK(!memcmp(expected_depth,actual_depth,sizeof(actual_depth)));
        CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
        f.commands[2].payload.flat_triangle.color=0x1256ab;
    }
    rf_gpu_graphics_get_stats(g,&after);
    CHECK(after.mesh_upload_bytes==before.mesh_upload_bytes && after.texture_upload_bytes==before.texture_upload_bytes);
    CHECK(after.indexed_draws-before.indexed_draws==6);
    CHECK(after.raster_bridge_transfers-before.raster_bridge_transfers==12);
    CHECK(after.bridge_transfer_bytes-before.bridge_transfer_bytes==(uint64_t)width*height*8*12);
    /* More than the historical 16 intervals: explicit truncation must never
     * report valid; reserving from a plan must cover even the final suffix. */
    for (unsigned expanded=0;expanded<2;++expanded) {
        struct rf_gpu_mixed_gpu_timing timing;
        if (expanded) CHECK(!rf_gpu_vulkan_timestamp_reserve(r,40));
        CHECK(!MIX(0,3,RF_GPU_RASTER_CLEAR,0));
        for (unsigned batch=0;batch<8;++batch)
            CHECK(!rf_gpu_graphics_raster_draw(g,r,&d,1));
        CHECK(!MIX(3,12,RF_GPU_RASTER_LOAD_EXISTING,1));
        rf_gpu_vulkan_mixed_gpu_timing(r,&timing);
        if (timing.supported) {
            CHECK(timing.requested>16);
            CHECK(timing.requested==timing.recorded+timing.dropped);
            if (expanded) CHECK(timing.valid && !timing.dropped);
            else CHECK(!timing.valid && timing.dropped && timing.recorded==16);
            printf("timestamp-coverage: expanded=%u requested=%u recorded=%u dropped=%u valid=%u PASS\n",
                expanded,timing.requested,timing.recorded,timing.dropped,timing.valid);
        }
    }
    /* Unsafe inverse-depth remains sticky across later valid/empty segments. */
    f.commands[2].payload.flat_triangle.a.inv_z=16385;
    CHECK(!MIX(0,3,RF_GPU_RASTER_CLEAR,0));
    CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
    f.commands[2].payload.flat_triangle.a.inv_z=8192;
    CHECK(!MIX(3,3,RF_GPU_RASTER_LOAD_EXISTING,0));
    CHECK(rf_gpu_graphics_raster_draw(g,r,&d,1)<0);
    CHECK(!MIX(0,3,RF_GPU_RASTER_CLEAR,0));
    CHECK(!rf_gpu_graphics_raster_draw(g,r,&d,1));
    /* Destroy the lender with an unfinished recording. A new raw frame must
     * use its own color target, never a stale shared image/view. */
    rf_gpu_graphics_destroy(g); g=NULL;
    CHECK(MIX(3,12,RF_GPU_RASTER_LOAD_EXISTING,1)<0);
    CHECK(!MIX(0,12,RF_GPU_RASTER_CLEAR,1));
    CHECK(!memcmp(expected,actual,sizeof(actual)));
    CHECK(!memcmp(expected_depth,actual_depth,sizeof(actual_depth)));
    printf("mixed-compute-graphics: group=%u full_scan=%d fog=%d extent=%ux%u exact color/depth/stride PASS\n",group,full_scan,fog,width,height);
    result=0;
done:
    rf_gpu_graphics_destroy(g);
    rf_gpu_vulkan_backend.raster_destroy(context,r);
    rf_gpu_vulkan_backend.raster_destroy(context,fresh);
    free(f.bytes);
    return result;
#undef MIX
}

int main(int argc, char **argv)
{
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context;
    struct rf_gpu_raster raster;
    struct rf_gpu_status status;
    struct fixture f;
    uint32_t *color = NULL;
    int32_t *depth = NULL;
    uint64_t ch, dh;
    int stride;
    int result = 1;
    memset(&context, 0, sizeof(context)); memset(&raster, 0, sizeof(raster));
    memset(&f, 0, sizeof(f));
    int mixed=argc==2 && !strcmp(argv[1],"--mixed-gate");
    if(argc>1 && !mixed){fprintf(stderr,"usage: %s [--mixed-gate]\n",argv[0]);return 2;}
    context.require_graphics=mixed;
    CHECK(rf_gpu_init(&gpu, RF_GPU_POLICY_REQUIRED, &rf_gpu_vulkan_backend,
                      &context) == 0);
    CHECK(rf_gpu_get_status(&gpu, &status) == 0 && status.renderer.raster_v1);
    CHECK(status.info.capabilities.shader_int64);
    if(mixed)printf("HG-2B adapter=%s vendor=%x device=%x type=%u queue=%u\n",
        gpu.info.adapter_name,gpu.info.vendor_id,gpu.info.device_id,
        gpu.info.adapter_type,gpu.info.queue_family);
    for(unsigned int group=8;group<=16;group+=8)
        for(int full=0;full<2;full++) for(int fog=0;fog<2;fog++)
        {
            CHECK(!segmented_test(&context,group,full,fog));
            if(!fog) CHECK(!sparse_segment_test(&context,group,full));
            if(mixed) {
                CHECK(!mixed_test(&context,group,full,fog,19));
                CHECK(!mixed_test(&context,group,full,fog,37));
            }
        }
    CHECK((status.renderer.raster_work_group_x == 16 &&
           status.renderer.raster_work_group_y == 16) ||
          (status.renderer.raster_work_group_x == 8 &&
           status.renderer.raster_work_group_y == 8));
    CHECK(rf_gpu_raster_init(&gpu, &raster, 19, 13) == 0);
    CHECK(raster.work_group_x == status.renderer.raster_work_group_x);

    CHECK(fixture_init(&f, 19, 13, 0, 0x00102030u) == 0);
    CHECK(rf_gpu_raster_validate_v1(f.bytes, f.size) == 0);
    stride = execute(&gpu, &raster, &f, &color, &depth, &ch, &dh);
    CHECK(stride > 0 && color[0] == 0xff102030u && depth[0] == 0);
    CHECK(ch == 0xd6503e0cf0c79d4aULL && dh == 0x387963cacae7df53ULL);
    printf("clear: color=%016llx depth=%016llx PASS\n",
           (unsigned long long)ch, (unsigned long long)dh);
    free(color); free(depth); color = NULL; depth = NULL; free(f.bytes);

    CHECK(fixture_init(&f, 19, 13, 5, 0x00010203u) == 0);
    triangle(&f, 0, 2, 2, 100, 16, 2, 200, 2, 10, 300,
             0x004080c0u, 256, 0);
    triangle(&f, 1, 4, 3, 50, 14, 3, 50, 4, 9, 50,
             0x00ff0000u, 256, 0);
    triangle(&f, 2, 4, 3, 400, 14, 3, 400, 4, 9, 400,
             0x0000ff00u, 256, 0);
    triangle(&f, 3, 4, 3, 400, 14, 3, 400, 4, 9, 400,
             0x000000ffu, 256, 0);
    triangle(&f, 4, -5, 5, 250, 5, 5, 250, 0, 12, 250,
             0x00ffffffu, 256, 0);
    CHECK(rf_gpu_raster_validate_v1(f.bytes, f.size) == 0);
    stride = execute(&gpu, &raster, &f, &color, &depth, &ch, &dh);
    CHECK(stride > 0);
    CHECK(color[0] == 0xff010203u && depth[0] == 0);
    CHECK(color[3 * stride + 4] == 0xff0000ffu &&
          depth[3 * stride + 4] == 400);
    CHECK(depth[2 * stride + 2] == 100);
    CHECK(depth[3 * stride + 3] == 132);
    CHECK(color[5 * stride] == 0xffffffffu && depth[5 * stride] == 250);
    CHECK(ch == 0xdd6142f5b6ad5c67ULL && dh == 0x16119c6f707d5b59ULL);
    printf("geometry-depth-order: color=%016llx depth=%016llx PASS\n",
           (unsigned long long)ch, (unsigned long long)dh);
    free(color); free(depth); color = NULL; depth = NULL; free(f.bytes);

    CHECK(fixture_init(&f, 19, 13, 2, 0) == 0);
    triangle(&f, 0, 2, 2, 77, 12, 2, 77, 2, 10, 77,
             0x00ff0000u, 256, 0);
    triangle(&f, 1, 12, 2, 77, 12, 10, 77, 2, 10, 77,
             0x0000ff00u, 256, 0);
    CHECK(rf_gpu_raster_validate_v1(f.bytes, f.size) == 0);
    stride = execute(&gpu, &raster, &f, &color, &depth, &ch, &dh);
    CHECK(stride > 0);
    CHECK(color[3 * stride + 3] == 0xffff0000u);
    CHECK(color[9 * stride + 11] == 0xff00ff00u);
    CHECK(color[6 * stride + 7] == 0xff00ff00u &&
          depth[6 * stride + 7] == 77);
    CHECK(ch == 0xee38b1413d008f3bULL && dh == 0x3375accfe7333fbeULL);
    printf("shared-edge: color=%016llx depth=%016llx PASS\n",
           (unsigned long long)ch, (unsigned long long)dh);
    free(color); free(depth); color = NULL; depth = NULL; free(f.bytes);

    CHECK(fixture_init(&f, 19, 13, 5, 0) == 0);
    triangle(&f, 0, 0, 0, 10, 8, 0, 10, 0, 8, 10,
             0x0080c0ffu, 0, 0);
    triangle(&f, 1, 9, 0, 20, 17, 0, 20, 9, 8, 20,
             0x0080c0ffu, 256, 128);
    triangle(&f, 2, 0, 9, 30, 8, 9, 30, 0, 12, 30,
             0x00c08040u, 384, 0);
    triangle(&f, 3, 9, 9, 40, 17, 9, 40, 9, 12, 40,
             0x00abcdefu, 999, 999);
    triangle(&f, 4, 1, 8, 60, 17, 8, 60, 1, 9, 60,
             0x00ffffffu, 256, 0);
    CHECK(rf_gpu_raster_validate_v1(f.bytes, f.size) == 0);
    stride = execute(&gpu, &raster, &f, &color, &depth, &ch, &dh);
    CHECK(stride > 0);
    CHECK(color[0] == expected_shade(0x0080c0ffu, 0, 0));
    CHECK(color[9] == expected_shade(0x0080c0ffu, 256, 128));
    CHECK(color[9 * stride] == expected_shade(0x00c08040u, 384, 0));
    CHECK(color[9 * stride + 9] == expected_shade(0x00abcdefu, 999, 999));
    CHECK(color[8 * stride + 1] == 0xffffffffu);
    CHECK(ch == 0x4667c8a436021fa5ULL && dh == 0x0eec6dc089548671ULL);
    printf("light-fog-thin: color=%016llx depth=%016llx PASS\n",
           (unsigned long long)ch, (unsigned long long)dh);
    free(color); free(depth); color = NULL; depth = NULL; free(f.bytes);

    CHECK(rf_gpu_raster_resize(&gpu, &raster, 37, 23) == 0);
    CHECK(fixture_init(&f, 37, 23, 50, 0x00334455u) == 0);
    for (uint32_t i = 0; i < 50; ++i)
        triangle(&f, i, 1, 1, 100 + (int)i, 35, 1, 100 + (int)i,
                 1, 21, 100 + (int)i, 0x00010200u + i, 256, 0);
    CHECK(f.size > 4096 && rf_gpu_raster_validate_v1(f.bytes, f.size) == 0);
    stride = execute(&gpu, &raster, &f, &color, &depth, &ch, &dh);
    CHECK(stride > 0 && depth[stride + 1] == 149 &&
          color[stride + 1] == (0xff010200u | 49u));
    CHECK(ch == 0x2e350fa4265ec3e4ULL && dh == 0xe448d03e52c4c816ULL);
    printf("resize-growth: color=%016llx depth=%016llx PASS\n",
           (unsigned long long)ch, (unsigned long long)dh);
    free(color); free(depth); color = NULL; depth = NULL; free(f.bytes);

    CHECK(fixture_init(&f, 37, 23, 1, 0) == 0);
    triangle(&f, 0, 2, 2, 1, 3, 3, 1, 4, 4, 1, 0xffffffu, 256, 0);
    CHECK(f.commands[2].payload.flat_triangle.area == 0);
    CHECK(rf_gpu_raster_validate_v1(f.bytes, f.size) != 0);
    CHECK(rf_gpu_raster_render(&gpu, &raster, f.bytes, f.size,
                               (unsigned int *)f.bytes, (int *)f.bytes,
                               37, 23, 37, 37) < 0);
    puts("degenerate-rejection: PASS");
    free(f.bytes); f.bytes = NULL;
    result = 0;
done:
    free(color); free(depth); free(f.bytes);
    rf_gpu_raster_shutdown(&raster);
    rf_gpu_shutdown(&gpu);
    if (context.implementation) result = 1;
    if(mixed)puts(result?"HG-2B mixed bridge: FAIL":"HG-2B mixed bridge: PASS");
    puts(result ? "GPU Raster V1: FAIL" : "shutdown: PASS\nGPU Raster V1: PASS");
    return result;
}
