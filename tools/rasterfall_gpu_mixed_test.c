#include "tlibc_everything.h"
#include "rf_gpu_mixed_executor.h"
#include "rf_gpu_raster_pack.h"
#include "rf_gpu_raster_cpu_ref.h"
#include "toy_window.h"
#ifdef TOYC_WINDOWS
struct native_rect { long left, top, right, bottom; };
__declspec(dllimport) int __stdcall GetClientRect(void *, struct native_rect *);
__declspec(dllimport) int __stdcall SetWindowPos(void *, void *, int, int, int, int, unsigned int);
#define NATIVE_SWP_FLAGS 0x16U
#endif
static void put32(unsigned char *p, uint32_t v)
{
    for (unsigned i = 0; i < 4; ++i) p[i] = (unsigned char)(v>>(i*8));
}
static int fixture(struct rasterfall_resource_registry *r, unsigned slot,
    struct rasterfall_resource_handle *h, unsigned char red)
{
    struct rasterfall_model_asset *m = tlibc_malloc(sizeof(*m));
    unsigned char *data;
    const int positions[3][3] = {{-1024,-1024,0},{2048,-1024,0},{-1024,2048,0}};
    if (!m) return -1;
    memset(m, 0, sizeof(*m));
    r->slots[slot].model = m; r->slots[slot].active = 1;
    r->slots[slot].generation = 1;
    h->slot = slot; h->generation = 1;
    data = tlibc_malloc(100);
    if (!data) return -1;
    memset(data, 0, 100);
    m->data = data; m->data_size = 100;
    m->primitives = data; m->primitive_count = 1;
    m->indices = data+16; m->index_count = 3;
    m->vertices = data+28; m->vertex_count = 3; m->vertex_bytes = 24;
    m->format_version = 2; m->position_scale = 1000;
    put32(data+4, 3);
    for (unsigned i = 0; i < 3; ++i) {
        put32(data+16+i*4, i);
        for (unsigned j = 0; j < 3; ++j) put32(data+28+i*24+j*4, (uint32_t)positions[i][j]);
        data[28+i*24+14] = 255; data[28+i*24+15] = 127;
    }
    m->textures.assets = tlibc_malloc(sizeof(*m->textures.assets));
    m->textures.views = tlibc_malloc(sizeof(*m->textures.views));
    if (m->textures.assets) memset(m->textures.assets, 0, sizeof(*m->textures.assets));
    if (!m->textures.assets || !m->textures.views) return -1;
    m->textures.count = 1;
    memset(m->textures.views, 0, sizeof(*m->textures.views));
    data = tlibc_malloc(4);
    if (!data) return -1;
    data[0] = red; data[1] = 37; data[2] = 91; data[3] = 255;
    m->textures.assets[0].data = data;
    m->textures.views[0].data = data; m->textures.views[0].data_size = 4;
    m->textures.views[0].channels = 4;
    m->textures.views[0].width = m->textures.views[0].height = 1;
    return 0;
}

static struct toy_raster_cmd triangle(int width, int height, int inverse, uint32_t color)
{
    struct toy_raster_cmd c;
    memset(&c,0,sizeof(c));
    c.a.x=-width; c.a.y=-height;
    c.b.x=5*width; c.b.y=-height;
    c.c.x=-width; c.c.y=5*height;
    c.a.inv_z=c.b.inv_z=c.c.inv_z=inverse;
    c.area=-(long long)(6*width)*(6*height);
    c.bbox_maxx=width-1; c.bbox_maxy=height-1;
    c.color=color; c.light=256; c.material_alpha=255;
    return c;
}
static int native_window_test(void)
{
#ifdef TOYC_WINDOWS
    struct toy_window *window=NULL;
    struct toy_native_window_handle handle={0};
    struct rf_gpu_vulkan_context context={0};
    struct rf_gpu gpu={0};
    struct rasterfall_resource_registry *registry=NULL;
    struct rasterfall_resource_handle resource;
    struct rf_gpu_mixed_executor *executor=NULL;
    struct rf_core_mixed_frame frame={0};
    struct rf_gpu_mixed_stats before,after;
    struct toy_window_events events;
    struct rf_gpu_native_present_timing timing;
    unsigned int overlay[160*120]={0};
    unsigned char coverage[160*120]={0};
    struct native_rect client;
    int initialized=0, failure=0, previous_width=0, previous_height=0;
#define NATIVE_CHECK(x) do { if (!(x)) { __printf("native FAIL line %d: %s\n",__LINE__,#x); failure=__LINE__; goto native_done; } } while(0)
    window=toy_window_open_native("Rasterfall HG-2B mixed native",96,72);
    NATIVE_CHECK(window && toy_window_get_native_handle(window,&handle)==1);
    context.native_window.type=handle.type;
    context.native_window.window=handle.window;
    context.native_window.instance=handle.instance;
    context.require_graphics=1;
    registry=tlibc_malloc(sizeof(*registry));
    NATIVE_CHECK(registry);memset(registry,0,sizeof(*registry));
    NATIVE_CHECK(fixture(registry,0,&resource,211)==0);
    NATIVE_CHECK(rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context)==0);
    initialized=1;
    __printf("HG-2B adapter=%s vendor=%x device=%x type=%u queue=%u\n",gpu.info.adapter_name,
        gpu.info.vendor_id,gpu.info.device_id,gpu.info.adapter_type,gpu.info.queue_family);
    NATIVE_CHECK(gpu.info.capabilities.native_presentation_v1);
    executor=rf_gpu_mixed_create(&gpu,&context,registry);
    NATIVE_CHECK(executor);
    for (int pass=0;pass<3;++pass) {
        struct rasterfall_draw_view view={0};
        struct rasterfall_draw_instance instance={0};
        struct rasterfall_draw_item item={0};
        struct rf_gpu_mixed_output output={0};
        struct toy_raster_cmd background;
        int width,height;
        if(pass==1) NATIVE_CHECK(SetWindowPos((void *)(uintptr_t)handle.window,NULL,0,0,128,96,
            NATIVE_SWP_FLAGS));
        if(pass==2) NATIVE_CHECK(SetWindowPos((void *)(uintptr_t)handle.window,NULL,0,0,96,72,
            NATIVE_SWP_FLAGS));
        NATIVE_CHECK(toy_window_poll(window,&events,20)>=0);
        NATIVE_CHECK(GetClientRect((void *)(uintptr_t)handle.window,&client));
        width=client.right-client.left;height=client.bottom-client.top;
        NATIVE_CHECK(width>0 && width<=160 && height>0 && height<=120);
        if(pass) NATIVE_CHECK(width!=previous_width || height!=previous_height);
        previous_width=width;previous_height=height;
        NATIVE_CHECK(rasterfall_resources_frame_begin(registry)==0);
        NATIVE_CHECK(rf_core_mixed_begin(&frame,registry,width,height)==0);
        NATIVE_CHECK(rf_core_mixed_require_draws(&frame,1)==0);
        background=triangle(width,height,512,0x102030);
        NATIVE_CHECK(rf_core_mixed_raster(&frame,RF_RENDER_LAYER_WORLD,&background,1)==0);
        view.width=width;view.height=height;view.near_z=64;view.focal=width*3/4;
        view.camera.cy=view.camera.pitch_cy=1024;
        instance.mesh=registry->slots[0].model;instance.mesh_handle=resource;
        instance.z=256;instance.scale_milli=1000;instance.yaw_cos_q10=1024;
        instance.scene_light_q8=256;
        item.index_count=3;item.material.color=0x2266aa;item.material.double_sided=1;
        NATIVE_CHECK(rf_core_mixed_draw(&frame,&view,&instance,&item)==0);
        NATIVE_CHECK(rf_core_mixed_freeze(&frame)==0);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x) {
            unsigned p=(unsigned)y*width+x;
            overlay[p]=0xffee4422;coverage[p]=(x<width/8 && y<height/8);
        }
        output.clear_color=0xff070809;output.overlay_color=overlay;
        output.overlay_coverage=coverage;output.overlay_stride=output.coverage_stride=width;
        output.present_timing=&timing;output.strict_native=1;
        if(pass==1){output.post.mode=RF_GPU_POST_DEPTH_FOG_V0;
            output.post.fog_color=0x334455;output.post.fog_near_inv_z=8192;
            output.post.fog_far_inv_z=1;output.post.max_density_q8=128;}
        rf_gpu_mixed_get_stats(executor,&before);
        NATIVE_CHECK(rf_gpu_mixed_render(executor,&frame,&output)==0);
        rf_gpu_mixed_get_stats(executor,&after);
        NATIVE_CHECK(after.clears==before.clears+1 && after.draws==before.draws+1 &&
            after.finishes==before.finishes+1 && after.readback_bytes==0 &&
            timing.color_readback_bytes==0 && timing.cpu_framebuffer_copy_bytes==0 &&
            timing.overlay_upload_bytes>0);
        if(pass) NATIVE_CHECK(after.graphics.mesh_upload_bytes==before.graphics.mesh_upload_bytes &&
            after.graphics.texture_upload_bytes==before.graphics.texture_upload_bytes);
        __printf("native pass=%d extent=%dx%d overlay=%u readback=%u copy=%u\n",pass,
            width,height,timing.overlay_upload_bytes,timing.color_readback_bytes,
            timing.cpu_framebuffer_copy_bytes);
        rf_core_mixed_reset(&frame);rasterfall_resources_frame_complete(registry);
    }
native_done:
    rf_core_mixed_destroy(&frame);rf_gpu_mixed_destroy(executor);
    if(initialized)rf_gpu_shutdown(&gpu);
    if(registry){rasterfall_resources_invalidate(registry);rasterfall_resources_frame_complete(registry);tlibc_free(registry);}
    if(window)toy_window_close(window);
    __printf("core-mixed-native: %s line=%d\n",failure?"FAIL":"PASS",failure);
    return failure?1:0;
#else
    return 1;
#endif
}
int main(int argc, char **argv)
{
    if(argc==2 && !strcmp(argv[1],"--native-window")) return native_window_test();
    struct rf_gpu gpu;
    struct rf_gpu_vulkan_context context={0};
    struct rasterfall_resource_registry *r=tlibc_malloc(sizeof(*r));
    struct rasterfall_resource_handle handle;
    struct rf_core_mixed_frame f={0},stale={0};
    struct rf_gpu_mixed_executor *e=NULL;
    struct rf_gpu_raster reference={0};
    struct rf_gpu_mixed_stats before,after;
    unsigned int color[96*72],expected[96*72];
    int depth[96*72],expected_depth[96*72];
    int failure=0,initialized=0;
#define CHECK(x) do { if (!(x)) { __printf("FAIL line %d: %s\n",__LINE__,#x); failure=__LINE__; goto done; } } while(0)
    CHECK(r); memset(r,0,sizeof(*r));
    CHECK(fixture(r,0,&handle,211)==0);
    context.require_graphics=1;
    CHECK(rf_gpu_init(&gpu,RF_GPU_POLICY_REQUIRED,&rf_gpu_vulkan_backend,&context)==0);
    initialized=1;
    __printf("HG-2B adapter=%s vendor=%x device=%x type=%u queue=%u\n",gpu.info.adapter_name,
        gpu.info.vendor_id,gpu.info.device_id,gpu.info.adapter_type,gpu.info.queue_family);
    CHECK((e=rf_gpu_mixed_create(&gpu,&context,r))!=NULL);
    CHECK(rf_gpu_raster_init(&gpu,&reference,64,48)==0);
    for (int iteration=0;iteration<16;++iteration) {
        int width=iteration<8 ? 64 : 65, height=iteration<8 ? 48 : 49, stride=width+3;
        int fog=iteration&1, vm=(iteration>>1)&1, textured=(iteration>>2)&1;
        int middle_depth=(iteration&4)?2048:4096;
        int later_z=(iteration&4)?512:256;
        struct rasterfall_draw_view view={0};
        struct rasterfall_draw_instance instance={0};
        struct rasterfall_draw_item item={0};
        struct rf_gpu_mixed_output out={0};
        struct toy_raster_cmd cmds[8],c;
        struct toy_renderer renderer;
        struct rf_gpu_texture_resources_v1 textures={0};
        unsigned char stream[32+12*96];
        size_t size=0;
        int count=0;
        CHECK(rasterfall_resources_frame_begin(r)==0);
        CHECK(rf_core_mixed_begin(&f,r,width,height)==0);
        CHECK(rf_core_mixed_require_draws(&f,3)==0);
        view.width=width;view.height=height;view.near_z=64;view.focal=width*3/4;
        view.camera.cy=view.camera.pitch_cy=1024;
        instance.mesh=r->slots[0].model;instance.mesh_handle=handle;
        instance.z=256;instance.scale_milli=1000;instance.yaw_cos_q10=1024;instance.scene_light_q8=256;
        item.index_count=3;item.material.color=0x2266aa;item.material.double_sided=1;
        if(textured)item.material.texture=&instance.mesh->textures.views[0];
        /* Submit transparency early: freeze must move it behind every opaque draw. */
        c=triangle(width,height,8192,0x8899aa);c.material_alpha=128;c.transparent=1;
        CHECK(rf_core_mixed_raster(&f,RF_RENDER_LAYER_WORLD,&c,1)==0);
        c=triangle(width,height,1024,0x112233);cmds[count++]=c;
        CHECK(rf_core_mixed_raster(&f,RF_RENDER_LAYER_WORLD,&c,1)==0);
        CHECK(rf_core_mixed_draw(&f,&view,&instance,&item)==0);
        cmds[count++]=triangle(width,height,4096,textured?0xd3255b:0x2266aa);
        /* Further compute and graphics at equal depth: last submission wins. */
        c=triangle(width,height,middle_depth,0x557799);cmds[count++]=c;
        CHECK(rf_core_mixed_raster(&f,RF_RENDER_LAYER_WORLD,&c,1)==0);
        instance.z=later_z;
        item.material.color=0x336699;item.material.texture=NULL;
        CHECK(rf_core_mixed_draw(&f,&view,&instance,&item)==0);
        cmds[count++]=triangle(width,height,1048576/later_z,0x336699);
        CHECK(rf_core_mixed_freeze(&f)<0 && f.state==RF_CORE_MIXED_RECORDING);
        /* Consecutive draws share a span and still preserve submission order. */
        item.material.color=0x6688aa;
        CHECK(rf_core_mixed_draw(&f,&view,&instance,&item)==0);
        cmds[count++]=triangle(width,height,1048576/later_z,0x6688aa);
        c=triangle(width,height,8192,0x8899aa);c.material_alpha=128;c.transparent=1;cmds[count++]=c;
        c=triangle(width,height,2048,0xff0000);cmds[count++]=c;
        CHECK(rf_core_mixed_raster(&f,RF_RENDER_LAYER_EFFECTS,&c,1)==0);
        if(vm){
            c=triangle(width,height,512,0xabcdef);c.bbox_maxx=width/2;
            /* bbox must describe geometry for both production and reference. */
            c.b.x=width/2;c.c.y=height/2;
            c.area=-(long long)(c.b.x-c.a.x)*(c.c.y-c.a.y);
            c.bbox_maxy=height/2;cmds[count++]=c;
            CHECK(rf_core_mixed_raster(&f,RF_RENDER_LAYER_VIEWMODEL,&c,1)==0);
        }
        CHECK(rf_core_mixed_freeze(&f)==0);
        out.color=color;out.depth=depth;out.color_stride=out.depth_stride=stride;
        out.clear_color=0xff070809;
        if(fog){out.post.mode=RF_GPU_POST_DEPTH_FOG_V0;out.post.fog_color=0x123456;
            out.post.fog_near_inv_z=8192;out.post.fog_far_inv_z=1;out.post.max_density_q8=128;}
        rf_gpu_mixed_get_stats(e,&before);
        /* Late invalid Draw and RasterCmd must reject before the first CLEAR,
         * leaving the immutable frame retryable and host output untouched. */
        if(iteration==0){
            out.strict_native=1;
            color[0]=0xdeadbeef;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            CHECK(color[0]==0xdeadbeef);
            rf_gpu_mixed_get_stats(e,&after);CHECK(after.clears==before.clears && after.draws==before.draws);
            out.strict_native=0;
            if (!gpu.info.capabilities.native_presentation_v1) {
                unsigned char overlay_coverage[64*48]={0};
                struct rf_gpu_native_present_timing timing;
                out.color=NULL;out.depth=NULL;
                out.overlay_color=color;out.overlay_coverage=overlay_coverage;
                out.overlay_stride=out.coverage_stride=64;out.present_timing=&timing;
                CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
                rf_gpu_mixed_get_stats(e,&after);CHECK(after.clears==before.clears && after.draws==before.draws);
                out.color=color;out.depth=depth;out.present_timing=NULL;
            }
            f.draws[2].instance.scale_milli=9000;color[0]=0xdeadbeef;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            CHECK(color[0]==0xdeadbeef);
            rf_gpu_mixed_get_stats(e,&after);CHECK(after.clears==before.clears && after.draws==before.draws);
            f.draws[2].instance.scale_milli=1000;
            f.raster[f.raster_count-1].overlay=1;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            f.raster[f.raster_count-1].overlay=0;
            f.raster[f.raster_count-1].edge=1;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            f.raster[f.raster_count-1].edge=0;
            f.raster[f.raster_count-1].textured=1;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            f.raster[f.raster_count-1].textured=0;
            f.raster[1].a.inv_z=20000;
            CHECK(rf_gpu_mixed_render(e,&f,&out)<0 && f.state==RF_CORE_MIXED_FROZEN);
            f.raster[1].a.inv_z=1024;
        }
        CHECK(rf_gpu_mixed_render(e,&f,&out)==0 && f.state==RF_CORE_MIXED_COMPLETE);
        rf_gpu_mixed_get_stats(e,&after);
        CHECK(after.clears==before.clears+1 && after.finishes==before.finishes+1 && after.draws==before.draws+3);
        CHECK(after.draw_spans==before.draw_spans+2 &&
            after.graphics.raster_bridge_transfers==before.graphics.raster_bridge_transfers+4);
        if(iteration>4) CHECK(after.graphics.queue_submits==before.graphics.queue_submits+2);
        if(iteration>4)CHECK(after.graphics.mesh_upload_bytes==before.graphics.mesh_upload_bytes &&
            after.graphics.texture_upload_bytes==before.graphics.texture_upload_bytes);
        memset(&renderer,0,sizeof(renderer));renderer.surface.width=width;renderer.surface.height=height;
        renderer.cmds=cmds;renderer.cmd_count=count;
        CHECK(rf_gpu_raster_pack_toy_textured_spans_v2(&renderer,out.clear_color,0,stream,sizeof(stream),&size,&textures,5,vm?7:UINT32_MAX)==0);
        CHECK(rf_gpu_raster_resize(&gpu,&reference,width,height)==0);
        CHECK(rf_gpu_raster_set_post(&reference,&out.post)==0);
        CHECK(rf_gpu_raster_render(&gpu,&reference,stream,(unsigned long)size,expected,expected_depth,width,height,stride,stride)==0);
        for(int y=0;y<height;++y)for(int x=0;x<width;++x){int p=y*stride+x;
            if(color[p]!=expected[p] || depth[p]!=expected_depth[p]){
                __printf("pixel iteration=%d x=%d y=%d got=%x/%d expected=%x/%d\n",iteration,x,y,color[p],depth[p],expected[p],expected_depth[p]);
                CHECK(0);
            }
        }
        if(!fog){
            CHECK(rf_gpu_raster_cpu_reference_v1(stream,size,expected,expected_depth,stride,stride,NULL)==0);
            for(int y=0;y<height;++y)for(int x=0;x<width;++x){int p=y*stride+x;
                CHECK(color[p]==expected[p] && depth[p]==expected_depth[p]);}
        }
        CHECK(rf_gpu_mixed_render(e,&f,&out)<0);
        rf_core_mixed_reset(&f);rasterfall_resources_frame_complete(r);
    }
    /* Draw-first and draw-last frame; retirement retains the pinned resource,
     * then completion invalidates the frozen plan even after a new epoch. */
    {
        struct rasterfall_draw_view view={0};
        struct rasterfall_draw_instance instance={0};
        struct rasterfall_draw_item item={0};
        struct rf_gpu_mixed_output out={0};
        out.color=color;out.depth=depth;out.color_stride=out.depth_stride=64;
        view.width=64;view.height=48;view.near_z=64;view.focal=48;
        view.camera.cy=view.camera.pitch_cy=1024;
        instance.mesh=r->slots[0].model;instance.mesh_handle=handle;
        instance.z=256;instance.scale_milli=1000;instance.yaw_cos_q10=1024;instance.scene_light_q8=256;
        item.index_count=3;item.material.color=0x123456;item.material.double_sided=1;
        CHECK(rasterfall_resources_frame_begin(r)==0 && rf_core_mixed_begin(&f,r,64,48)==0);
        CHECK(rf_core_mixed_draw(&f,&view,&instance,&item)==0 && rf_core_mixed_freeze(&f)==0);
        CHECK(rf_core_mixed_begin(&stale,r,64,48)==0);
        CHECK(rf_core_mixed_draw(&stale,&view,&instance,&item)==0 && rf_core_mixed_freeze(&stale)==0);
        rasterfall_resources_invalidate(r);
        CHECK(rf_gpu_mixed_render(e,&f,&out)==0);
        for(int p=0;p<64*48;++p)CHECK(color[p]==0xff123456 && depth[p]==4096);
        rf_core_mixed_reset(&f);
        /* A frozen frame cannot cross frame-complete, even with the same handle. */
        rasterfall_resources_frame_complete(r);
        CHECK(rasterfall_resources_frame_begin(r)==0);
        CHECK(rf_gpu_mixed_render(e,&stale,&out)<0 && stale.state==RF_CORE_MIXED_FROZEN);
        rf_core_mixed_reset(&stale);rasterfall_resources_frame_complete(r);
    }
    /* Empty frame still clears and finishes once. */
    {
        struct rf_gpu_mixed_output out={0};out.color=color;out.depth=depth;
        out.color_stride=out.depth_stride=64;out.clear_color=0xffabcdef;
        CHECK(rasterfall_resources_frame_begin(r)==0);
        CHECK(rf_core_mixed_begin(&f,r,64,48)==0 && rf_core_mixed_freeze(&f)==0);
        CHECK(rf_gpu_mixed_render(e,&f,&out)==0);
        CHECK(color[0]==0xffabcdef && depth[0]==0);
    }
 done:
    rf_gpu_mixed_destroy(e);rf_gpu_raster_shutdown(&reference);rf_core_mixed_destroy(&f);rf_core_mixed_destroy(&stale);
    if(initialized)rf_gpu_shutdown(&gpu);
    if(r){rasterfall_resources_invalidate(r);rasterfall_resources_frame_complete(r);tlibc_free(r);}
    __printf("core-mixed-executor: %s line=%d\n",failure?"FAIL":"PASS",failure);
    return failure?1:0;
}
