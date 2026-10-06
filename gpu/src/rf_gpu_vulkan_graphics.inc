/* HG-2A/2B diagnostic executor, sharing the compute backend's device/queue.
 * Included after the buffer helpers; no Vulkan objects escape this owner. */
#include "rf_vulkan_graphics_min.h"
#include "rf_vulkan_sky_min.h"
#include "rf_gpu_graphics.h"
#include "rf_gpu_graphics_spirv.inc"
#include <limits.h>
#include <math.h>
#define VK_FORMAT_R16G16B16A16_SFLOAT 97
#define VK_COMPARE_OP_LESS_OR_EQUAL 3

#define RF_GFX_FUNCTIONS(X) \
    X(GetPhysicalDeviceFormatProperties) X(CreateImage) X(DestroyImage) \
    X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
    X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateGraphicsPipelines) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindVertexBuffers) \
    X(CmdBindIndexBuffer) X(CmdDrawIndexed) X(CmdSetViewport) X(CmdSetScissor) \
    X(CmdPushConstants) X(CmdCopyImageToBuffer) X(CmdBlitImage) X(CmdClearAttachments) \
    X(CreateSampler) X(DestroySampler)

typedef struct rf_scene_blit {
    VkImageSubresourceLayers src; VkOffset3D src_offsets[2];
    VkImageSubresourceLayers dst; VkOffset3D dst_offsets[2];
} rf_scene_blit;
typedef void (RF_VK_CALL *rf_gfx_CmdBlitImage_fn)(VkCommandBuffer, VkImage,
    VkImageLayout, VkImage, VkImageLayout, uint32_t, const rf_scene_blit *, uint32_t);
typedef struct rf_scene_clear_attachment {
    uint32_t aspectMask, colorAttachment;
    VkClearValue clearValue;
} rf_scene_clear_attachment;
typedef struct rf_scene_clear_rect {
    VkRect2D rect;
    uint32_t baseArrayLayer, layerCount;
} rf_scene_clear_rect;
typedef void (RF_VK_CALL *rf_gfx_CmdClearAttachments_fn)(VkCommandBuffer,
    uint32_t,const rf_scene_clear_attachment *,uint32_t,const rf_scene_clear_rect *);

struct rf_graphics_api {
#define RF_GFX_FIELD(n) rf_gfx_##n##_fn n;
    RF_GFX_FUNCTIONS(RF_GFX_FIELD)
#undef RF_GFX_FIELD
};
struct rf_graphics_image {
    rf_vk_image image;
    rf_vk_device_memory memory;
    VkImageView view;
};
struct rf_graphics_target {
    struct rf_graphics_image color, depth, hdr;
    VkFramebuffer framebuffer, overlay_framebuffer;
    struct rf_gpu_vulkan_buffer readback;
    uint32_t width, height;
    int valid;
};
struct rf_graphics_texture_set {
    struct rf_gpu_vulkan_buffer buffer;
    uint32_t references,count,width[RF_GPU_GRAPHICS_TEXTURES],height[RF_GPU_GRAPHICS_TEXTURES];
};
struct rf_graphics_reader {
    struct rf_gpu_graphics *graphics;
    struct rf_gpu_graphics_resource *resource;
    struct rf_graphics_reader *resource_next,*graphics_next;
};
struct rf_gpu_graphics_resource {
    struct rf_gpu_graphics *owner;
    struct rf_gpu_graphics_resource *next;
    struct rf_gpu_vulkan_buffer vertices, indices, texture;
    uint32_t index_count, texture_width, texture_height;
    struct rf_graphics_texture_set *texture_set;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set descriptor_set;
    struct rf_graphics_reader *readers;
    struct rf_gpu_vulkan_buffer skin_bind, skin_palette;
    struct rf_gpu_vulkan_buffer skin_staging[2];
    uint32_t vertex_capacity, palette_word_capacity;
    rf_vk_descriptor_pool skin_descriptor_pool;
    rf_vk_descriptor_set skin_descriptor_set;
    int position_min[3],position_max[3];
    int frame_dynamic;
    int sequential_triangles;
    int scene_vertex_color;
    int vertex_alpha_valid;
    struct rf_gpu_vulkan_buffer triangle_staging;
    void *triangle_mapped;
    struct rf_gpu_graphics_resource *skin_pending_next;
    struct rf_gpu_vulkan_buffer *skin_pending_upload[2];
    uint64_t skin_pending_bytes[2];
    uint32_t skin_pending_vertices;
    uint32_t *skin_last_palette;
    uint32_t skin_last_words,skin_last_vertices;
    int skin_cached;
    void *skin_mapped[2];
    struct rf_gpu_vulkan_buffer *skin_mapped_buffer[2];
};
struct rf_gpu_graphics {
    struct rf_gpu_vulkan_impl *owner;
    struct rf_graphics_api api;
    struct rf_graphics_target target;
    struct rf_gpu_graphics_resource *resource, *resources;
    struct rf_graphics_reader *borrowed;
    int destroying;
    int legacy_skin,staging_skin,reuse_pose;
    rf_vk_descriptor_set_layout set_layout;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set set;
    rf_vk_pipeline_layout layout;
    rf_vk_pipeline pipelines[32];
    struct rf_gpu_lighting lighting;
    struct rf_gpu_vulkan_buffer architecture_buffer;
    struct rf_gpu_vulkan_buffer architecture_blas_buffer,architecture_tlas_buffer;
    rf_rt_as architecture_blas,architecture_tlas;
    uint32_t architecture_count;
    struct rf_gpu_vulkan_buffer roof_buffer,indirect_buffer;
    struct rf_gpu_vulkan_buffer indirect_geometry,indirect_sources;
    rf_vk_pipeline indirect_pipeline;
    uint32_t indirect_probe_count,indirect_cursor;
    int indirect_warmed;
    int roof_cache_enabled,indirect_enabled,indirect_bounces;
    int legacy_architecture_shadows;
    struct rf_gpu_vulkan_buffer light_buffer,shadow_buffer,sky_buffer;
    struct rf_gpu_vulkan_buffer light_tiles_buffer,light_profile_buffer;
    rf_vk_pipeline light_tiles_pipeline;
    unsigned light_tiles_width,light_tiles_height,light_ablation;
    int light_tiles_enabled;
    struct rf_graphics_image sky_noise;
    void *sky_sampler;
    unsigned sky_scale,sky_width,sky_height;
    int sky_reference_scan; /* Internal regression/benchmark control. */
    rf_vk_pipeline_layout sky_layout;
    rf_vk_pipeline sky_pipeline,sky_noise_pipeline;
    int sky_noise_ready;
    struct rf_graphics_image shadow_depth;
    VkFramebuffer shadow_framebuffer;
    unsigned shadow_count;
    float shadow_matrix[RF_GPU_SHADOW_MAPS][16];
    int disable_draw_cull;
    int legacy_origin_bounds;
    VkRenderPass shadow_pass,overlay_pass;
    rf_vk_descriptor_set_layout light_layout;
    rf_vk_descriptor_pool light_pool;
    rf_vk_descriptor_set light_set;
    rf_vk_pipeline_layout post_layout;
    rf_vk_pipeline tonemap;
    VkRenderPass render_pass;
    rf_vk_descriptor_set_layout skin_set_layout;
    rf_vk_pipeline_layout skin_layout;
    rf_vk_pipeline skin_pipeline;
    rf_vk_command_pool pool;
    rf_vk_command_buffer cmd;
    rf_vk_fence fence;
    int poisoned, scene_in_flight;
    int skin_batch;
    int profile_triangle_update;
    struct rf_gpu_graphics_resource *skin_pending, *skin_pending_tail;
    rf_vk_semaphore scene_acquire;
    rf_vk_query_pool scene_query_pool;
    int scene_query_active;
    struct rf_gpu_scene_timing scene_timing;
    struct rf_gpu_graphics_stats stats;
    struct rf_gpu_graphics *video[RF_GPU_GRAPHICS_VIDEO_SLOTS];
    int video_rect[RF_GPU_GRAPHICS_VIDEO_SLOTS][4];
    float background[3];
};

static void gfx_lighting_destroy(struct rf_gpu_graphics *g);

static int gfx_resource_compatible(const struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_resource *resource)
{
    return g && resource && resource->owner &&
        resource->owner->owner == g->owner;
}

/* Every Scene owner uses the same three storage-buffer bindings. Descriptor
 * layout definitions are identical; the resource descriptor is camera-neutral.
 * Track every consumer so mutations cannot race a borrowed in-flight draw. */
static int gfx_resource_readers_idle(const struct rf_gpu_graphics_resource *resource)
{
    for(const struct rf_graphics_reader *r=resource->readers;r;r=r->resource_next)
        if(r->graphics->scene_in_flight) return 0;
    return 1;
}
static void gfx_reader_release(struct rf_graphics_reader *r)
{
    if(r->graphics->resource==r->resource)r->graphics->resource=NULL;
    struct rf_graphics_reader **p=&r->resource->readers;
    while(*p && *p!=r)p=&(*p)->resource_next;
    if(*p)*p=r->resource_next;
    p=&r->graphics->borrowed;
    while(*p && *p!=r)p=&(*p)->graphics_next;
    if(*p)*p=r->graphics_next;
    free(r);
}
static rf_vk_descriptor_set gfx_resource_descriptor(
    const struct rf_gpu_graphics *g,const struct rf_gpu_graphics_resource *resource)
{
    if(!gfx_resource_compatible(g,resource))return NULL;
    if(resource->owner==g)return resource->descriptor_set;
    for(const struct rf_graphics_reader *r=resource->readers;r;r=r->resource_next)
        if(r->graphics==g)return resource->descriptor_set;
    return NULL;
}
static int gfx_resource_share(struct rf_gpu_graphics *g,struct rf_gpu_graphics_resource *resource)
{
    if(!gfx_resource_compatible(g,resource))return -1;
    if(resource->owner==g)return 0;
    if(resource->skin_pending_vertices || resource->owner->scene_in_flight)return -1;
    for(struct rf_graphics_reader *r=resource->readers;r;r=r->resource_next)
        if(r->graphics==g)return 0;
    struct rf_graphics_reader *r=calloc(1,sizeof(*r));
    if(!r)return -1;
    r->graphics=g;r->resource=resource;
    r->resource_next=resource->readers;resource->readers=r;
    r->graphics_next=g->borrowed;g->borrowed=r;
    return 0;
}

_Static_assert(sizeof(struct rf_gpu_graphics_vertex) == 56, "HG vertex ABI");
_Static_assert(sizeof(void *) == 8, "HG-2A supports the existing 64-bit Vulkan ABI");
_Static_assert(offsetof(struct rf_gpu_graphics_draw, first_index) == 128,
               "HG push-constant ABI");

static int gfx_begin(struct rf_gpu_graphics *g)
{
    struct rf_vk_command_buffer_begin_info begin = {0};
    if (g->poisoned || g->scene_in_flight || g->owner->api.reset_command_pool(
            g->owner->device, g->pool, 0) != RF_VK_SUCCESS) return -1;
    begin.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    return g->owner->api.begin_command_buffer(g->cmd, &begin) == RF_VK_SUCCESS ? 0 : -1;
}

static int gfx_submit(struct rf_gpu_graphics *g, int wait,
                      enum rf_gpu_graphics_submit_kind kind)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    struct rf_vk_submit_info submit = {0};
    double stamp;
    if (impl->api.end_command_buffer(g->cmd) != RF_VK_SUCCESS ||
        (wait && impl->api.reset_fences(impl->device, 1, &g->fence) != RF_VK_SUCCESS))
        goto failed;
    submit.s_type = RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.command_buffer_count = 1; submit.command_buffers = &g->cmd;
    stamp=now_ms();
    g->stats.queue_submits++;
    g->stats.submits_by_kind[kind]++;
    if (impl->api.queue_submit(impl->queue, 1, &submit, wait ? g->fence : NULL) != RF_VK_SUCCESS)
        goto failed;
    g->stats.submit_wall_ms+=now_ms()-stamp;
    if (!wait) return 0;
    stamp=now_ms();
    g->stats.fence_waits++;
    if (impl->api.wait_for_fences(impl->device, 1, &g->fence,
                                  RF_VK_TRUE, UINT64_MAX) != RF_VK_SUCCESS) goto failed;
    stamp=now_ms()-stamp;
    g->stats.fence_wait_wall_ms+=stamp;
    g->stats.wait_ms_by_kind[kind]+=stamp;
    return 0;
failed:
    g->poisoned = 1;
    return -1;
}

static void gfx_image_destroy(struct rf_gpu_graphics *g, struct rf_graphics_image *i)
{
    if (i->view) g->api.DestroyImageView(g->owner->device, i->view, NULL);
    if (i->image) g->api.DestroyImage(g->owner->device, i->image, NULL);
    if (i->memory) g->owner->api.free_memory(g->owner->device, i->memory, NULL);
    memset(i, 0, sizeof(*i));
}

static void gfx_target_destroy(struct rf_gpu_graphics *g, struct rf_graphics_target *t)
{
    if (t->framebuffer) g->api.DestroyFramebuffer(g->owner->device, t->framebuffer, NULL);
    if(t->overlay_framebuffer) g->api.DestroyFramebuffer(g->owner->device,t->overlay_framebuffer,NULL);
    gfx_image_destroy(g, &t->hdr);
    gfx_image_destroy(g, &t->color); gfx_image_destroy(g, &t->depth);
    gpu_buffer_destroy(g->owner, &t->readback);
    memset(t, 0, sizeof(*t));
}

void rf_gpu_graphics_destroy(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *impl;
    if (!g) return;
    impl = g->owner;
    /* Also covers a failed submission/wait: never destroy pending resources. */
    impl->api.queue_wait_idle(impl->queue);
    g->scene_in_flight=0;g->destroying=1;
    rf_gpu_graphics_skin_batch_cancel(g);
    if (g->scene_acquire) impl->api.destroy_semaphore(impl->device,g->scene_acquire,NULL);
    if (g->scene_query_pool) impl->api.destroy_query_pool(impl->device,g->scene_query_pool,NULL);
    gfx_target_destroy(g, &g->target);
    if (g->pool) impl->api.destroy_command_pool(impl->device, g->pool, NULL);
    if (g->fence) impl->api.destroy_fence(impl->device, g->fence, NULL);
    for (unsigned i = 0; i < 32; ++i)
        if (g->pipelines[i]) impl->api.destroy_pipeline(impl->device, g->pipelines[i], NULL);
    gfx_lighting_destroy(g);
    if (g->render_pass) g->api.DestroyRenderPass(impl->device, g->render_pass, NULL);
    if (g->skin_pipeline) impl->api.destroy_pipeline(impl->device,g->skin_pipeline,NULL);
    if (g->skin_layout) impl->api.destroy_pipeline_layout(impl->device,g->skin_layout,NULL);
    if (g->skin_set_layout) impl->api.destroy_descriptor_set_layout(impl->device,g->skin_set_layout,NULL);
    if (g->layout) impl->api.destroy_pipeline_layout(impl->device, g->layout, NULL);
    if (g->descriptor_pool) impl->api.destroy_descriptor_pool(impl->device, g->descriptor_pool, NULL);
    while(g->borrowed)gfx_reader_release(g->borrowed);
    while (g->resources) rf_gpu_graphics_resource_destroy(g, g->resources);
    if (g->set_layout) impl->api.destroy_descriptor_set_layout(impl->device, g->set_layout, NULL);
    free(g);
}

static int gfx_image_create_extent(struct rf_gpu_graphics *g, struct rf_graphics_image *out,
    uint32_t width, uint32_t height, uint32_t depth, uint32_t format, uint32_t aspect, uint32_t usage)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    VkImageCreateInfo info = {0};
    VkImageViewCreateInfo view = {0};
    VkMemoryRequirements req;
    struct rf_vk_memory_allocate_info allocation = {0};
    int memory;
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = depth>1?RF_SKY_IMAGE_TYPE_3D:VK_IMAGE_TYPE_2D; info.format = format;
    info.extent.width = width; info.extent.height = height; info.extent.depth = depth;
    info.mipLevels = info.arrayLayers = 1; info.samples = VK_SAMPLE_COUNT_1_BIT;
    info.tiling = VK_IMAGE_TILING_OPTIMAL;
    info.usage = usage | VK_IMAGE_USAGE_TRANSFER_SRC_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    info.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    if (g->api.CreateImage(impl->device, &info, NULL, &out->image) != RF_VK_SUCCESS) return -1;
    g->api.GetImageMemoryRequirements(impl->device, out->image, &req);
    memory = framebuffer_memory_type(impl, req.memoryTypeBits,
        RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, NULL);
    if (memory < 0) return -1;
    allocation.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    allocation.allocation_size = req.size; allocation.memory_type_index = (uint32_t)memory;
    if (impl->api.allocate_memory(impl->device, &allocation, NULL, &out->memory) != RF_VK_SUCCESS ||
        g->api.BindImageMemory(impl->device, out->image, out->memory, 0) != RF_VK_SUCCESS) return -1;
    view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
    view.image = out->image; view.viewType = depth>1?RF_SKY_IMAGE_VIEW_TYPE_3D:VK_IMAGE_VIEW_TYPE_2D; view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
    return g->api.CreateImageView(impl->device, &view, NULL, &out->view) == RF_VK_SUCCESS ? 0 : -1;
}
static int gfx_image_create(struct rf_gpu_graphics *g, struct rf_graphics_image *out,
    uint32_t width,uint32_t height,uint32_t format,uint32_t aspect,uint32_t usage)
{ return gfx_image_create_extent(g,out,width,height,1,format,aspect,usage); }

/* Conservative resource bounds remain valid after dynamic CPU uploads. GPU
 * skinning can move vertices outside bind bounds, so never cull it here. */
static int gfx_draw_box(const struct rf_gpu_graphics_batch_item *item,float center[3],float extent[3])
{
    const struct rf_gpu_graphics_resource *r=item->resource;
    const struct rf_gpu_graphics_draw *d=&item->draw;
    if (r->skin_descriptor_set || d->texture[3]) return 0;
    float scale=d->translation_scale[3]/1000.0f*512.0f/(d->quality[1]>512?d->quality[1]:512);
    float sy=d->rotation[0]/1024.0f,cy=d->rotation[1]/1024.0f;
    float local_center[3],local_extent[3];
    for (unsigned k=0;k<3;++k) {
        local_center[k]=(r->position_min[k]+r->position_max[k])*0.5f;
        local_extent[k]=(r->position_max[k]-r->position_min[k])*0.5f;
        if (r->owner->legacy_origin_bounds) {
            local_center[k]=0;
            local_extent[k]=(float)(abs(r->position_min[k])>abs(r->position_max[k]) ?
                abs(r->position_min[k]) : abs(r->position_max[k]));
        }
    }
    /* Match the vertex transform, including an off-origin model's pivot.
     * Retain conservative slack for float evaluation at map-sized positions. */
    center[0]=d->translation_scale[0]+(cy*local_center[0]+sy*local_center[2])*scale;
    center[1]=d->translation_scale[1]+(local_center[1]-d->rotation[2])*scale;
    center[2]=d->translation_scale[2]+(cy*local_center[2]-sy*local_center[0])*scale;
    extent[0]=(__builtin_fabsf(cy)*local_extent[0]+__builtin_fabsf(sy)*local_extent[2])*scale+2;
    extent[1]=local_extent[1]*scale+2;
    extent[2]=(__builtin_fabsf(sy)*local_extent[0]+__builtin_fabsf(cy)*local_extent[2])*scale+2;
    return 1;
}
static int gfx_box_outside(const float c[3],const float e[3],const float plane[4])
{
    float maximum=plane[3];
    for (unsigned k=0;k<3;++k) maximum+=plane[k]*c[k]+__builtin_fabsf(plane[k])*e[k];
    return maximum<0;
}
static int gfx_draw_camera_outside(const struct rf_gpu_graphics_batch_item *item)
{
    const struct rf_gpu_graphics_draw *d=&item->draw;
    float c[3],e[3];
    if (!gfx_draw_box(item,c,e)) return 0;
    float sy=d->view[0]/1024.0f,cy=d->view[1]/1024.0f,ps=d->view[2]/1024.0f,pc=d->view[3]/1024.0f;
    float f[3]={sy*pc,ps,cy*pc},r[3]={cy,0,-sy},u[3]={-sy*ps,pc,-cy*ps};
    for (unsigned k=0;k<3;++k) c[k]-=d->camera[k];
    for (unsigned p=0;p<5;++p) {
        float plane[4]={0,0,0,p==0?-64.0f:0};
        float extent=d->projection[p<3?0:1]/(2.0f*d->projection[3]);
        for (unsigned k=0;k<3;++k)
            plane[k]=p==0?f[k]:f[k]*extent+((p&1)?1:-1)*(p<3?r[k]:u[k]);
        if (gfx_box_outside(c,e,plane)) return 1;
    }
    return 0;
}
#include "rf_gpu_architecture_ray.inc"
#include "rf_gpu_architecture_light.inc"
#include "rf_gpu_indirect.inc"
#include "rf_gpu_lighting.inc"

int rf_gpu_graphics_resize(struct rf_gpu_graphics *g, uint32_t width, uint32_t height)
{
    struct rf_graphics_target t = {0};
    VkFormatProperties properties;
    VkFramebufferCreateInfo fb = {0};
    VkImageView views[2];
    if (!g || g->poisoned || g->scene_in_flight || !width || !height || width > 4096 || height > 4096) return -1;
    if (g->target.width == width && g->target.height == height) return 0;
    t.width = width; t.height = height;
    memset(&properties,0,sizeof(properties));
    g->api.GetPhysicalDeviceFormatProperties(g->owner->physical_device,
        VK_FORMAT_R8G8B8A8_UNORM,&properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
        gfx_image_create(g, &t.color, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_STORAGE_BIT) < 0 ||
        gfx_image_create(g,&t.hdr,width,height,VK_FORMAT_R16G16B16A16_SFLOAT,
            VK_IMAGE_ASPECT_COLOR_BIT,VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT)<0 ||
        gfx_image_create(g, &t.depth, width, height, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) < 0) goto fail;
    views[0] = t.hdr.view; views[1] = t.depth.view;
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fb.renderPass = g->render_pass;
    fb.attachmentCount = 2; fb.pAttachments = views; fb.width = width; fb.height = height; fb.layers = 1;
    if (g->api.CreateFramebuffer(g->owner->device, &fb, NULL, &t.framebuffer) != RF_VK_SUCCESS) goto fail;
    views[0]=t.color.view;fb.renderPass=g->overlay_pass;
    if(g->api.CreateFramebuffer(g->owner->device,&fb,NULL,&t.overlay_framebuffer)!=RF_VK_SUCCESS) goto fail;
    if(gfx_sky_resize(g,width,height)<0) goto fail;
    gfx_target_destroy(g, &g->target); g->target = t;
    gfx_lighting_images(g);
    g->stats.target_builds++;
    return 0;
fail:
    gfx_target_destroy(g, &t);
    return -1;
}

static int gfx_pipeline_create(struct rf_gpu_graphics *g, int compatible)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    VkAttachmentDescription attachments[2] = {{0}};
    VkAttachmentReference color = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth = {1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass = {0};
    VkSubpassDependency deps[2] = {{0}};
    VkRenderPassCreateInfo pass = {0};
    struct rf_vk_pipeline_layout_create_info layout = {0};
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 128};
    struct rf_vk_shader_module_create_info shader_info = {0};
    rf_vk_shader_module shaders[2] = {0};
    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    VkVertexInputBindingDescription binding = {0, sizeof(struct rf_gpu_graphics_vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[5] = {
        {0,0,VK_FORMAT_R32G32B32_SINT,0}, {1,0,VK_FORMAT_R32G32_SINT,12},
        {2,0,VK_FORMAT_R32G32B32_SINT,20}, {3,0,VK_FORMAT_R32G32B32_SINT,32},
        {4,0,VK_FORMAT_R32G32B32_SINT,44}};
    /* Scene color feeds position and {light,RGB24} to shader locations 0/1.
     * Its draw contract disables form light, so normal inputs are unused. */
    int color_vertex=compatible==2 || compatible==4 || compatible==6;
    int overlay=compatible==3 || compatible==4, shadow=compatible>=5;
    if (color_vertex) binding.stride=sizeof(struct rf_gpu_scene_color_vertex);
    VkPipelineVertexInputStateCreateInfo vertex = {0};
    VkPipelineInputAssemblyStateCreateInfo assembly = {0};
    VkPipelineViewportStateCreateInfo viewport = {0};
    VkPipelineRasterizationStateCreateInfo raster = {0};
    VkPipelineMultisampleStateCreateInfo multisample = {0};
    VkPipelineDepthStencilStateCreateInfo depth_state = {0};
    VkPipelineColorBlendAttachmentState blend_attachment = {0};
    VkPipelineColorBlendStateCreateInfo blend = {0};
    VkDynamicState dynamics[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    VkPipelineDynamicStateCreateInfo dynamic = {0};
    VkGraphicsPipelineCreateInfo pipeline = {0};
    int result = -1;
    /* Scene attachments are written by the graphics pass and copied for present. */
    attachments[0].format = overlay ? VK_FORMAT_R8G8B8A8_UNORM : VK_FORMAT_R16G16B16A16_SFLOAT;
    attachments[1].format = VK_FORMAT_D32_SFLOAT;
    for (unsigned i = 0; i < 2; ++i) {
        attachments[i].samples = VK_SAMPLE_COUNT_1_BIT;
        attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
        attachments[i].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
        attachments[i].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        attachments[i].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        attachments[i].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        attachments[i].finalLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    if(!overlay) attachments[0].finalLayout=VK_IMAGE_LAYOUT_GENERAL;
    if(overlay) {
        attachments[0].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[0].initialLayout=VK_IMAGE_LAYOUT_GENERAL;
        attachments[1].loadOp=VK_ATTACHMENT_LOAD_OP_LOAD;attachments[1].initialLayout=VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
    }
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    deps[0].srcStageMask = RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    deps[0].srcAccessMask = 0;
    deps[0].dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT |
        VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT;
    deps[0].dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT |
        VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
    deps[1].srcSubpass = 0; deps[1].dstSubpass = VK_SUBPASS_EXTERNAL;
    deps[1].srcStageMask = deps[0].dstStageMask;
    deps[1].dstStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[1].srcAccessMask = deps[0].dstAccessMask; deps[1].dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT;
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = 2; pass.pAttachments = attachments;
    pass.subpassCount = 1; pass.pSubpasses = &subpass;
    pass.dependencyCount = 2; pass.pDependencies = deps;
    if(overlay) {
        deps[0].srcStageMask=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[0].srcAccessMask=VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    } else if(!shadow) {
        deps[1].dstStageMask|=VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
        deps[1].dstAccessMask|=VK_ACCESS_SHADER_READ_BIT;
    }
    if(shadow) {
        attachments[0]=attachments[1];depth.attachment=0;
        pass.attachmentCount=1;subpass.colorAttachmentCount=0;
        deps[0].srcStageMask=VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[0].srcAccessMask=VK_ACCESS_TRANSFER_READ_BIT;
    }
    VkRenderPass *selected=shadow?&g->shadow_pass:overlay?&g->overlay_pass:&g->render_pass;
    if(!*selected && g->api.CreateRenderPass(impl->device,&pass,NULL,selected)!=RF_VK_SUCCESS) return -1;
    layout.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    rf_vk_descriptor_set_layout sets[2]={g->set_layout,g->light_layout};
    layout.set_layout_count = 2; layout.set_layouts = sets;
    layout.push_constant_range_count = 1; layout.push_constant_ranges = &push;
    if (!compatible && impl->api.create_pipeline_layout(impl->device, &layout, NULL, &g->layout) != RF_VK_SUCCESS) return -1;
    shader_info.s_type = RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    for (unsigned i = 0; i < 2; ++i) {
        if(shadow && i) break;
        shader_info.code_size=i?sizeof(rf_graphics_scene_frag_spirv):sizeof(rf_graphics_scene_vert_spirv);
        shader_info.code=i?rf_graphics_scene_frag_spirv:rf_graphics_scene_vert_spirv;
        if(i && impl->ray_query_enabled) {
            shader_info.code=rf_graphics_scene_ray_frag_spirv;
            shader_info.code_size=sizeof(rf_graphics_scene_ray_frag_spirv);
        }
        if(i && impl->light_profile_enabled) {
            shader_info.code=impl->ray_query_enabled?rf_graphics_scene_ray_profile_frag_spirv:rf_graphics_scene_profile_frag_spirv;
            shader_info.code_size=impl->ray_query_enabled?sizeof(rf_graphics_scene_ray_profile_frag_spirv):sizeof(rf_graphics_scene_profile_frag_spirv);
        }
        if(color_vertex && !i) {shader_info.code_size=sizeof(rf_graphics_color_vert_spirv);shader_info.code=rf_graphics_color_vert_spirv;}
        if(shadow) {shader_info.code_size=sizeof(rf_graphics_shadow_vert_spirv);shader_info.code=rf_graphics_shadow_vert_spirv;}
        if (impl->api.create_shader_module(impl->device, &shader_info, NULL, &shaders[i]) != RF_VK_SUCCESS) goto done;
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = shaders[i]; stages[i].pName = "main";
    }
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex.vertexBindingDescriptionCount = 1; vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = shadow ? 1 : color_vertex ? 2 : 5;
    vertex.pVertexAttributeDescriptions = attrs;
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0f;
    /* Rasterfall projects Y down and accepts (c-a) x (b-a) < 0.
     * With a positive-height viewport this is Vulkan CLOCKWISE. */
    raster.frontFace = VK_FRONT_FACE_CLOCKWISE;
    multisample.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    multisample.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
    depth_state.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
    depth_state.depthTestEnable = depth_state.depthWriteEnable = RF_VK_TRUE;
    depth_state.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
    blend_attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
        VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    blend.attachmentCount = 1; blend.pAttachments = &blend_attachment;
    dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
    dynamic.dynamicStateCount = 2; dynamic.pDynamicStates = dynamics;
    pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    pipeline.stageCount = shadow ? 1 : 2; pipeline.pStages = stages; pipeline.pVertexInputState = &vertex;
    pipeline.pInputAssemblyState = &assembly; pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster; pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth_state; pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic; pipeline.layout = g->layout; pipeline.renderPass = *selected;
    pipeline.basePipelineIndex = -1;
    if(shadow) {
        raster.cullMode=VK_CULL_MODE_NONE;depth_state.depthCompareOp=VK_COMPARE_OP_LESS_OR_EQUAL;
        raster.depthBiasEnable=1;raster.depthBiasConstantFactor=1.25f;raster.depthBiasSlopeFactor=1.75f;
        blend.attachmentCount=0;
        if(g->api.CreateGraphicsPipelines(impl->device,NULL,1,&pipeline,NULL,&g->pipelines[color_vertex?31:30])!=RF_VK_SUCCESS) goto done;
        result=0;goto done;
    }
    for (unsigned alpha=0;alpha<3u;++alpha) {
        depth_state.depthTestEnable=alpha==2 ? 0 : RF_VK_TRUE;
        depth_state.depthWriteEnable=alpha ? 0 : RF_VK_TRUE;
        blend_attachment.blendEnable=alpha ? RF_VK_TRUE : 0;
        blend_attachment.srcColorBlendFactor=VK_BLEND_FACTOR_SRC_ALPHA;
        blend_attachment.dstColorBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.colorBlendOp=VK_BLEND_OP_ADD;
        blend_attachment.srcAlphaBlendFactor=VK_BLEND_FACTOR_ONE;
        blend_attachment.dstAlphaBlendFactor=VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        blend_attachment.alphaBlendOp=VK_BLEND_OP_ADD;
        for (unsigned i=0;i<2;++i) {
            raster.cullMode=i ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
            if (g->api.CreateGraphicsPipelines(impl->device,NULL,1,&pipeline,NULL,
                    &g->pipelines[overlay ? 18+(color_vertex?6:0)+alpha*2+i : i+(color_vertex ? 12 : compatible*2)+alpha*(color_vertex ? 2 : 4)])!=RF_VK_SUCCESS) goto done;
        }
    }
    result = 0;
done:
    for (unsigned i = 0; i < 2; ++i)
        if (shaders[i]) impl->api.destroy_shader_module(impl->device, shaders[i], NULL);
    return result;
}

static int gfx_skin_create(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    struct rf_vk_descriptor_set_layout_binding bindings[3]={{0}};
    struct rf_vk_descriptor_set_layout_create_info set_layout={0};
    struct rf_vk_pipeline_layout_create_info layout={0};
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,4};
    struct rf_vk_shader_module_create_info module={0};
    struct rf_vk_compute_pipeline_create_info pipeline={0};
    rf_vk_shader_module shader=NULL;
    int result=-1;
    for(unsigned i=0;i<3;++i) {
        bindings[i].binding=i; bindings[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptor_count=1; bindings[i].stage_flags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    set_layout.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.binding_count=3; set_layout.bindings=bindings;
    if(impl->api.create_descriptor_set_layout(impl->device,&set_layout,NULL,&g->skin_set_layout)!=RF_VK_SUCCESS) return -1;
    layout.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.set_layout_count=1; layout.set_layouts=&g->skin_set_layout;
    layout.push_constant_range_count=1; layout.push_constant_ranges=&push;
    if(impl->api.create_pipeline_layout(impl->device,&layout,NULL,&g->skin_layout)!=RF_VK_SUCCESS) return -1;
    module.s_type=RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module.code_size=sizeof(rf_graphics_skin_comp_spirv); module.code=rf_graphics_skin_comp_spirv;
    if(impl->api.create_shader_module(impl->device,&module,NULL,&shader)!=RF_VK_SUCCESS) return -1;
    pipeline.s_type=RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.stage.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT; pipeline.stage.module=shader; pipeline.stage.name="main";
    pipeline.layout=g->skin_layout; pipeline.base_pipeline_index=-1;
    if(impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline,NULL,&g->skin_pipeline)==RF_VK_SUCCESS) result=0;
    impl->api.destroy_shader_module(impl->device,shader,NULL);
    return result;
}

struct rf_gpu_graphics *rf_gpu_graphics_create(struct rf_gpu_vulkan_context *ctx)
{
    struct rf_gpu_graphics *g;
    struct rf_gpu_vulkan_impl *impl = ctx ? ctx->implementation : NULL;
    VkFormatProperties color, depth;
    struct rf_vk_descriptor_set_layout_binding binding[3] = {{0}};
    struct rf_vk_descriptor_set_layout_create_info layout = {0};
    struct rf_vk_descriptor_pool_size size = {RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    struct rf_vk_descriptor_pool_create_info pool = {0};
    struct rf_vk_descriptor_set_allocate_info set = {0};
    struct rf_vk_command_pool_create_info command_pool = {0};
    struct rf_vk_command_buffer_allocate_info command = {0};
    struct rf_vk_fence_create_info fence = {0};
    if (!impl || !(impl->queue_flags & VK_QUEUE_GRAPHICS_BIT) ||
        !impl->api.queue_wait_idle || !impl->api.reset_fences) return NULL;
    g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->owner = impl;
    {
        const char *skin=getenv("RF_GPU_SKIN_LEGACY_UPLOAD");
        const char *staging=getenv("RF_GPU_SKIN_STAGING_UPLOAD");
        const char *pose=getenv("RF_GPU_SCENE_LEGACY_POSE_REUSE");
        g->legacy_skin=skin && skin[0]=='1';g->staging_skin=staging && staging[0]=='1';
        g->reuse_pose=!(pose && !strcmp(pose,"1"));
        const char *profile=getenv("RF_GPU_PROFILE_TRIANGLE_UPDATE");
        g->profile_triangle_update=profile && !strcmp(profile,"1");
        const char *no_cull=getenv("RF_GPU_SCENE_DISABLE_DRAW_CULL");
        g->disable_draw_cull=no_cull && no_cull[0]=='1';
        const char *legacy_shadows=getenv("RF_GPU_ARCHITECTURE_SHADOW_MAPS");
        g->legacy_architecture_shadows=legacy_shadows && !strcmp(legacy_shadows,"1");
        const char *legacy_bounds=getenv("RF_GPU_SCENE_LEGACY_ORIGIN_BOUNDS");
        g->legacy_origin_bounds=legacy_bounds && !strcmp(legacy_bounds,"1");
    }
#define RF_GFX_LOAD(n) do { \
    void *p = load_instance(&impl->api, impl->instance, "vk" #n); \
    if (!p) goto fail; \
    memcpy(&g->api.n, &p, sizeof(g->api.n)); \
} while (0);
    RF_GFX_FUNCTIONS(RF_GFX_LOAD)
#undef RF_GFX_LOAD
    g->api.GetPhysicalDeviceFormatProperties(impl->physical_device, VK_FORMAT_R8G8B8A8_UNORM, &color);
    g->api.GetPhysicalDeviceFormatProperties(impl->physical_device, VK_FORMAT_D32_SFLOAT, &depth);
    if (!(color.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
        !(color.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        !(depth.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT)) goto fail;
    g->api.GetPhysicalDeviceFormatProperties(impl->physical_device,VK_FORMAT_R16G16B16A16_SFLOAT,&color);
    if(!(color.optimalTilingFeatures&VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
       !(color.optimalTilingFeatures&VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT)) goto fail;
    for (unsigned i = 0; i < 3; ++i) {
        binding[i].binding = i; binding[i].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        binding[i].descriptor_count = 1;
        binding[i].stage_flags = i ? VK_SHADER_STAGE_VERTEX_BIT : VK_SHADER_STAGE_FRAGMENT_BIT;
    }
    layout.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    layout.binding_count = 3; layout.bindings = binding;
    if (impl->api.create_descriptor_set_layout(impl->device, &layout, NULL, &g->set_layout) != RF_VK_SUCCESS) goto fail;
    pool.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.max_sets = 1; pool.pool_size_count = 1; pool.pool_sizes = &size;
    if (impl->api.create_descriptor_pool(impl->device, &pool, NULL, &g->descriptor_pool) != RF_VK_SUCCESS) goto fail;
    set.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptor_pool = g->descriptor_pool; set.descriptor_set_count = 1; set.set_layouts = &g->set_layout;
    if (impl->api.allocate_descriptor_sets(impl->device, &set, &g->set) != RF_VK_SUCCESS ||
        gfx_lighting_create(g)<0 || gfx_pipeline_create(g, 0) < 0 ||
        gfx_pipeline_create(g, 2) < 0 ||
        gfx_pipeline_create(g,3)<0 || gfx_pipeline_create(g,4)<0 ||
        gfx_pipeline_create(g,5)<0 || gfx_pipeline_create(g,6)<0 ||
        gfx_lighting_pipelines(g)<0 || gfx_skin_create(g) < 0) goto fail;
    command_pool.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    command_pool.queue_family_index = impl->queue_family;
    if (impl->api.create_command_pool(impl->device, &command_pool, NULL, &g->pool) != RF_VK_SUCCESS) goto fail;
    command.s_type = RF_VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    command.command_pool = g->pool; command.command_buffer_count = 1;
    if (impl->api.allocate_command_buffers(impl->device, &command, &g->cmd) != RF_VK_SUCCESS) goto fail;
    fence.s_type = RF_VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    if (impl->api.create_fence(impl->device, &fence, NULL, &g->fence) != RF_VK_SUCCESS) goto fail;
    return g;
fail:
    rf_gpu_graphics_destroy(g);
    return NULL;
}

static int gfx_resource_upload(struct rf_gpu_graphics *g, struct rf_gpu_graphics_resource *resource,
    const void *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    struct rf_gpu_vulkan_buffer staging[3] = {{0}};
    struct rf_gpu_vulkan_buffer *target[3] = {
        &resource->vertices, &resource->indices, &resource->texture
    };
    const void *data[3];
    uint64_t bytes[3];
    uint32_t usage[3] = {
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    };
    struct rf_vk_buffer_copy copy[3] = {{0}};
    struct rf_vk_memory_barrier barrier = {0};
    void *mapped = NULL;
    unsigned mapped_index = 0;
    int upload_failed = 1;
    int alpha_valid=resource->scene_vertex_color;
    if (!g || g->poisoned || resource->vertices.buffer || !indices ||
        !vertex_count || vertex_count > 65536 || !index_count || index_count > 196608 || index_count % 3 ||
        !rgb_texels || !texture_width || !texture_height || texture_width > 1024 || texture_height > 1024) return -1;
    for (uint32_t i = 0; i < index_count; ++i) if (indices[i] >= vertex_count) return -1;
    for (uint32_t i = 0; vertices && i < vertex_count; ++i) {
        const int32_t *position=resource->scene_vertex_color ?
            ((const struct rf_gpu_scene_color_vertex *)vertices)[i].position :
            ((const struct rf_gpu_graphics_vertex *)vertices)[i].position;
        for (unsigned j=0;j<3;++j)
            if (position[j]<-32767 || position[j]>32767) return -1;
        if (resource->scene_vertex_color) {
            const struct rf_gpu_scene_color_vertex *v=&((const struct rf_gpu_scene_color_vertex *)vertices)[i];
            if (v->light_q8<0 || v->light_q8>384 || v->rgb24>0xffffff) return -1;
            if (v->light_q8>255) alpha_valid=0;
        } else {
            const struct rf_gpu_graphics_vertex *v=&((const struct rf_gpu_graphics_vertex *)vertices)[i];
            for (unsigned j=0;j<9;++j)
                if (v->normals[j]<-32767 || v->normals[j]>32767) return -1;
            for (unsigned j=0;j<2;++j)
                if (v->uv[j]<0 || v->uv[j]>65535) return -1;
        }
    }
    /* All three corners must describe the same source primitive. */
    for (uint32_t i=0;vertices && i<index_count;i+=3) {
        if (resource->scene_vertex_color) {
            const struct rf_gpu_scene_color_vertex *v=vertices;
            if (v[indices[i]].rgb24!=v[indices[i+1]].rgb24 ||
                v[indices[i]].rgb24!=v[indices[i+2]].rgb24) return -1;
            if (v[indices[i]].light_q8!=v[indices[i+1]].light_q8 ||
                v[indices[i]].light_q8!=v[indices[i+2]].light_q8) alpha_valid=0;
        } else {
            const struct rf_gpu_graphics_vertex *v=vertices;
            if (memcmp(v[indices[i]].normals,v[indices[i+1]].normals,36) ||
                memcmp(v[indices[i]].normals,v[indices[i+2]].normals,36)) return -1;
        }
    }
    data[0]=vertices; data[1]=indices; data[2]=rgb_texels;
    bytes[0]=(uint64_t)vertex_count*(resource->scene_vertex_color ?
        sizeof(struct rf_gpu_scene_color_vertex) : sizeof(struct rf_gpu_graphics_vertex));
    bytes[1]=(uint64_t)index_count*4;
    bytes[2]=(uint64_t)texture_width*texture_height*4;
    /* Integrated GPUs commonly expose device-local host-visible memory.  Use
     * it for per-frame geometry so upload does not submit behind the previous
     * frame and synchronously wait for the graphics queue.  Discrete devices
     * without that memory type retain the staging-copy path below. */
    for (unsigned n=0;n<3;++n) {
        if (gpu_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,target[n])<0) {
            for (unsigned k=0;k<=n;++k) gpu_buffer_destroy(impl,target[k]);
            break;
        }
        if (!data[n]) {
            if (n==2) { upload_failed=0; goto upload_done; }
            continue;
        }
        if (impl->api.map_memory(impl->device,target[n]->memory,0,
                target[n]->allocation_size,0,&mapped)!=RF_VK_SUCCESS) {
            for (unsigned k=0;k<=n;++k) gpu_buffer_destroy(impl,target[k]);
            mapped=NULL;
            break;
        }
        mapped_index=n;
        memcpy(mapped,data[n],(size_t)bytes[n]);
        if (!(target[n]->memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            struct rf_vk_mapped_memory_range range={0};
            range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=target[n]->memory; range.size=target[n]->allocation_size;
            if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) {
                impl->api.unmap_memory(impl->device,target[n]->memory); mapped=NULL;
                for (unsigned k=0;k<=n;++k) gpu_buffer_destroy(impl,target[k]);
                break;
            }
        }
        impl->api.unmap_memory(impl->device,target[n]->memory); mapped=NULL;
        if (n==2) { upload_failed=0; goto upload_done; }
    }
    for (unsigned n=0;n<3;++n) {
        if (!data[n]) {
            if (gpu_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    target[n])<0) goto upload_done;
            continue;
        }
        if (gpu_buffer_create(impl,bytes[n],RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &staging[n])<0 ||
            gpu_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                target[n])<0 ||
            impl->api.map_memory(impl->device,staging[n].memory,0,
                staging[n].allocation_size,0,&mapped)!=RF_VK_SUCCESS) goto upload_done;
        mapped_index=n;
        memcpy(mapped,data[n],(size_t)bytes[n]);
        if (!(staging[n].memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            struct rf_vk_mapped_memory_range range={0};
            range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=staging[n].memory; range.size=staging[n].allocation_size;
            if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS)
                goto upload_done;
        }
        impl->api.unmap_memory(impl->device,staging[n].memory); mapped=NULL;
        copy[n].size=bytes[n];
    }
    if (gfx_begin(g)<0) goto upload_done;
    for (unsigned n=0;n<3;++n) if (data[n])
        impl->api.cmd_copy_buffer(g->cmd,staging[n].buffer,target[n]->buffer,1,&copy[n]);
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT |
        VK_ACCESS_SHADER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
            VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    if (gfx_submit(g,1,RF_GPU_SUBMIT_UPLOAD)<0) goto upload_done;
    upload_failed=0;
upload_done:
    if (mapped) impl->api.unmap_memory(impl->device,staging[mapped_index].memory);
    if (g->poisoned) impl->api.queue_wait_idle(impl->queue);
    for (unsigned n=0;n<3;++n) gpu_buffer_destroy(impl,&staging[n]);
    if (upload_failed) {
        g->poisoned = 1;
        return -1;
    }
    resource->index_count = index_count; resource->texture_width = texture_width; resource->texture_height = texture_height;
    resource->vertex_capacity = vertex_count;
    resource->vertex_alpha_valid=alpha_valid;
    resource->sequential_triangles = index_count==vertex_count;
    for (uint32_t i=0;resource->sequential_triangles && i<index_count;++i)
        if (indices[i]!=i) resource->sequential_triangles=0;
    for (uint32_t i=0;vertices && i<vertex_count;++i) {
        const int32_t *position=resource->scene_vertex_color ?
            ((const struct rf_gpu_scene_color_vertex *)vertices)[i].position :
            ((const struct rf_gpu_graphics_vertex *)vertices)[i].position;
        for (unsigned j=0;j<3;++j) {
            if (!i || position[j]<resource->position_min[j]) resource->position_min[j]=position[j];
            if (!i || position[j]>resource->position_max[j]) resource->position_max[j]=position[j];
        }
    }
    g->stats.mesh_upload_bytes += (vertices ?
        bytes[0] : 0) + (uint64_t)index_count*4;
    g->stats.texture_upload_bytes += (uint64_t)texture_width*texture_height*4;
    return 0;
}

int rf_gpu_graphics_resource_diff_vertices(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource,
    const struct rf_gpu_graphics_vertex *reference, uint32_t vertex_count,
    uint64_t *position_mismatches, uint64_t *normal_mismatches,
    uint64_t *uv_mismatches, uint32_t *max_position_delta,
    uint32_t *max_normal_delta)
{
    struct rf_gpu_vulkan_impl *impl;
    struct rf_gpu_vulkan_buffer readback={0};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_mapped_memory_range range={0};
    const struct rf_gpu_graphics_vertex *actual;
    void *mapped=NULL;
    uint64_t bytes;
    int result=-1;
    if (!g || g->poisoned || g->skin_batch || !resource || resource->owner!=g ||
        resource->scene_vertex_color || !reference ||
        !vertex_count || !position_mismatches || !normal_mismatches ||
        !uv_mismatches || !max_position_delta || !max_normal_delta) return -1;
    bytes=(uint64_t)vertex_count*sizeof(*reference);
    if (bytes>resource->vertices.size) return -1;
    impl=g->owner;
    if (gpu_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
            &readback)<0 || gfx_begin(g)<0) goto done;
    copy.size=bytes;
    impl->api.cmd_copy_buffer(g->cmd,resource->vertices.buffer,readback.buffer,1,&copy);
    if (gfx_submit(g,1,RF_GPU_SUBMIT_VERTEX_DIFF)<0 || impl->api.map_memory(impl->device,readback.memory,0,
            readback.allocation_size,0,&mapped)!=RF_VK_SUCCESS) goto done;
    if (!(readback.memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=readback.memory;range.size=readback.allocation_size;
        if (impl->api.invalidate_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS)
            goto done;
    }
    *position_mismatches=*normal_mismatches=*uv_mismatches=0;
    *max_position_delta=*max_normal_delta=0;
    actual=mapped;
    for (uint32_t n=0;n<vertex_count;++n) {
        for (unsigned k=0;k<3;++k) {
            uint32_t delta=(uint32_t)llabs((long long)actual[n].position[k]-reference[n].position[k]);
            if (delta) (*position_mismatches)++;
            if (delta>*max_position_delta) *max_position_delta=delta;
        }
        for (unsigned k=0;k<2;++k) if (actual[n].uv[k]!=reference[n].uv[k]) (*uv_mismatches)++;
        for (unsigned k=0;k<9;++k) {
            uint32_t delta=(uint32_t)llabs((long long)actual[n].normals[k]-reference[n].normals[k]);
            if (delta) (*normal_mismatches)++;
            if (delta>*max_normal_delta) *max_normal_delta=delta;
        }
    }
    result=0;
done:
    if (mapped) impl->api.unmap_memory(impl->device,readback.memory);
    gpu_buffer_destroy(impl,&readback);
    return result;
}

static struct rf_gpu_graphics_resource *gfx_resource_create(
    struct rf_gpu_graphics *g,
    const void *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height,
    int scene_vertex_color)
{
    struct rf_gpu_graphics_resource *resource;
    struct rf_vk_descriptor_pool_size size = {RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    struct rf_vk_descriptor_pool_create_info pool = {0};
    struct rf_vk_descriptor_set_allocate_info set = {0};
    struct rf_vk_descriptor_buffer_info buffer[3] = {{0}};
    struct rf_vk_write_descriptor_set write[3] = {{0}};
    if (!g || g->poisoned) return NULL;
    resource = calloc(1, sizeof(*resource));
    if (!resource) return NULL;
    resource->owner = g;
    resource->scene_vertex_color=scene_vertex_color;
    resource->next = g->resources;
    g->resources = resource;
    if (gfx_resource_upload(g, resource, vertices, vertex_count, indices,
            index_count, rgb_texels, texture_width, texture_height) < 0) {
        rf_gpu_graphics_resource_destroy(g, resource);
        return NULL;
    }
    if (resource->sequential_triangles &&
        (resource->vertices.memory_flags&RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) &&
        g->owner->api.map_memory(g->owner->device,resource->vertices.memory,0,
            resource->vertices.allocation_size,0,&resource->triangle_mapped)!=RF_VK_SUCCESS)
        goto descriptor_failed;
    pool.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.max_sets = 1; pool.pool_size_count = 1; pool.pool_sizes = &size;
    set.s_type = RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptor_set_count = 1; set.set_layouts = &g->set_layout;
    if (g->owner->api.create_descriptor_pool(g->owner->device, &pool, NULL,
            &resource->descriptor_pool) != RF_VK_SUCCESS) goto descriptor_failed;
    set.descriptor_pool = resource->descriptor_pool;
    if (g->owner->api.allocate_descriptor_sets(g->owner->device, &set,
            &resource->descriptor_set) != RF_VK_SUCCESS) goto descriptor_failed;
    buffer[0].buffer = resource->texture.buffer; buffer[0].range = resource->texture.size;
    buffer[1].buffer = resource->vertices.buffer; buffer[1].range = resource->vertices.size;
    buffer[2].buffer = resource->indices.buffer; buffer[2].range = resource->indices.size;
    for (unsigned i = 0; i < 3; ++i) {
        write[i].s_type = RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dst_set = resource->descriptor_set; write[i].dst_binding = i;
        write[i].descriptor_count = 1;
        write[i].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write[i].buffer_info = &buffer[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device, 3, write, 0, NULL);
    return resource;
descriptor_failed:
    rf_gpu_graphics_resource_destroy(g, resource);
    return NULL;
}

struct rf_gpu_graphics_resource *rf_gpu_graphics_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices,uint32_t vertex_count,
    const uint32_t *indices,uint32_t index_count,
    const uint32_t *rgb_texels,uint32_t texture_width,uint32_t texture_height)
{
    return gfx_resource_create(g,vertices,vertex_count,indices,index_count,
        rgb_texels,texture_width,texture_height,0);
}

struct rf_gpu_graphics_resource *rf_gpu_graphics_scene_color_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_scene_color_vertex *vertices,uint32_t vertex_count,
    const uint32_t *indices,uint32_t index_count)
{
    const uint32_t white=0xffffff;
    if (!vertices) return NULL;
    return gfx_resource_create(g,vertices,vertex_count,indices,index_count,&white,1,1,1);
}

#include "rf_gpu_texture_set.inc"

static int gfx_triangle_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r,
    const void *vertices,uint32_t vertex_count,int scene_color)
{
    struct rf_gpu_vulkan_impl *impl;
    struct rf_gpu_vulkan_buffer *upload;
    struct rf_vk_mapped_memory_range range={0};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    int minimum[3]={0},maximum[3]={0};
    int alpha_valid=scene_color;
    void *mapped;
    uint64_t bytes=(uint64_t)vertex_count*(scene_color ?
        sizeof(struct rf_gpu_scene_color_vertex) : sizeof(struct rf_gpu_graphics_vertex));
    double mark=0,validated=0,prepared=0,copied=0,flushed=0;
    uint64_t flush_bytes=0;
    if (!g || g->poisoned || g->scene_in_flight || !r || r->owner!=g ||
        !gfx_resource_readers_idle(r) || r->skin_descriptor_set || !r->sequential_triangles ||
        r->scene_vertex_color!=scene_color || !vertices || !vertex_count ||
        vertex_count>65536 || vertex_count%3) return -1;
    if (g->profile_triangle_update) mark=now_ms();
    for (uint32_t i=0;i<vertex_count;++i) {
        const int32_t *position=scene_color ?
            ((const struct rf_gpu_scene_color_vertex *)vertices)[i].position :
            ((const struct rf_gpu_graphics_vertex *)vertices)[i].position;
        for (unsigned k=0;k<3;++k) {
            int p=position[k];
            if (p < -32767 || p > 32767) return -1;
            if (!i || p<minimum[k]) minimum[k]=p;
            if (!i || p>maximum[k]) maximum[k]=p;
        }
        if (scene_color) {
            const struct rf_gpu_scene_color_vertex *v=vertices;
            if (v[i].light_q8<0 || v[i].light_q8>384 || v[i].rgb24>0xffffff ||
                (i%3 && v[i].rgb24!=v[i-i%3].rgb24)) return -1;
            if (v[i].light_q8>255 || (i%3 && v[i].light_q8!=v[i-i%3].light_q8)) alpha_valid=0;
        } else {
            const struct rf_gpu_graphics_vertex *v=vertices;
            for (unsigned k=0;k<9;++k)
                if (v[i].normals[k]<-32767 || v[i].normals[k]>32767) return -1;
            for (unsigned k=0;k<2;++k)
                if (v[i].uv[k]<0 || v[i].uv[k]>65535) return -1;
            if (i%3 && memcmp(v[i].normals,v[i-i%3].normals,sizeof(v[i].normals))) return -1;
        }
    }
    if (vertex_count>r->vertex_capacity) return 1;
    if (g->profile_triangle_update) validated=now_ms();
    impl=g->owner;
    upload=&r->vertices;
    if (!(upload->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT)) {
        upload=&r->triangle_staging;
        if (!upload->buffer && gpu_buffer_create(impl,r->vertices.size,
                RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,upload)<0) return -1;
    }
    if (!r->triangle_mapped &&
        impl->api.map_memory(impl->device,upload->memory,0,upload->allocation_size,0,
            &r->triangle_mapped)!=RF_VK_SUCCESS) return -1;
    mapped=r->triangle_mapped;
    if (g->profile_triangle_update) prepared=now_ms();
    memcpy(mapped,vertices,(size_t)bytes);
    if (g->profile_triangle_update) copied=now_ms();
    if (!(upload->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        uint64_t atom=impl->non_coherent_atom_size;
        if (!atom) { g->poisoned=1; return -1; }
        flush_bytes=bytes + (atom-1);
        flush_bytes-=flush_bytes%atom;
        if (flush_bytes>upload->allocation_size) flush_bytes=upload->allocation_size;
        range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory=upload->memory;range.size=flush_bytes;
        if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) {
            g->poisoned=1;return -1;
        }
    }
    if (g->profile_triangle_update) flushed=now_ms();
    if (upload!=&r->vertices) {
        if (gfx_begin(g)<0) return -1;
        copy.size=bytes;
        impl->api.cmd_copy_buffer(g->cmd,upload->buffer,r->vertices.buffer,1,&copy);
        barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dst_access_mask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_SHADER_READ_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
            VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_VERTEX_SHADER_BIT,
            0,1,&barrier,0,NULL,0,NULL);
        if (gfx_submit(g,1,RF_GPU_SUBMIT_UPLOAD)<0) return -1;
    }
    r->index_count=vertex_count;
    r->vertex_alpha_valid=alpha_valid;
    memcpy(r->position_min,minimum,sizeof(minimum));
    memcpy(r->position_max,maximum,sizeof(maximum));
    g->stats.mesh_upload_bytes+=bytes;
    if (g->profile_triangle_update) {
        g->stats.triangle_validate_ms+=validated-mark;
        g->stats.triangle_map_ms+=prepared-validated;
        g->stats.triangle_copy_ms+=copied-prepared;
        g->stats.triangle_flush_ms+=flushed-copied;
        g->stats.triangle_transfer_ms+=now_ms()-flushed;
        g->stats.triangle_updates++;
        g->stats.triangle_update_bytes+=bytes;
        g->stats.triangle_flush_bytes+=flush_bytes;
        g->stats.triangle_staging_updates+=upload!=&r->vertices;
        if (upload==&r->vertices) g->stats.triangle_direct_flags|=upload->memory_flags;
        else g->stats.triangle_staging_flags|=upload->memory_flags;
    }
    return 0;
}

int rf_gpu_graphics_triangle_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r,
    const struct rf_gpu_graphics_vertex *vertices,uint32_t vertex_count)
{
    return gfx_triangle_resource_update(g,r,vertices,vertex_count,0);
}

int rf_gpu_graphics_scene_color_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r,
    const struct rf_gpu_scene_color_vertex *vertices,uint32_t vertex_count)
{
    return gfx_triangle_resource_update(g,r,vertices,vertex_count,1);
}

static int gfx_upload_skin_input(struct rf_gpu_graphics *g,
    struct rf_gpu_vulkan_buffer *target,const uint32_t *words,uint64_t bytes)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    struct rf_gpu_vulkan_buffer staging={0};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    void *mapped=NULL;
    if (!words || !bytes) return -1;
    if (gpu_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
            RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,target)==0) {
        if (impl->api.map_memory(impl->device,target->memory,0,target->allocation_size,0,&mapped)!=RF_VK_SUCCESS)
            { gpu_buffer_destroy(impl,target); return -1; }
        memcpy(mapped,words,(size_t)bytes);
        if (!(target->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            struct rf_vk_mapped_memory_range range={0};
            range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; range.memory=target->memory;
            range.size=target->allocation_size;
            if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) {
                impl->api.unmap_memory(impl->device,target->memory);
                gpu_buffer_destroy(impl,target); return -1;
            }
        }
        impl->api.unmap_memory(impl->device,target->memory);
        return 0;
    }
    if (gpu_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&staging)<0 ||
        gpu_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,target)<0 ||
        impl->api.map_memory(impl->device,staging.memory,0,staging.allocation_size,0,&mapped)!=RF_VK_SUCCESS) goto fail;
    memcpy(mapped,words,(size_t)bytes);
    if (!(staging.memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        struct rf_vk_mapped_memory_range range={0};
        range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; range.memory=staging.memory;
        range.size=staging.allocation_size;
        if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) goto fail;
    }
    impl->api.unmap_memory(impl->device,staging.memory); mapped=NULL;
    if (gfx_begin(g)<0) goto fail;
    copy.size=bytes; impl->api.cmd_copy_buffer(g->cmd,staging.buffer,target->buffer,1,&copy);
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    if (gfx_submit(g,1,RF_GPU_SUBMIT_SKIN_INPUT)<0) goto fail;
    gpu_buffer_destroy(impl,&staging); return 0;
fail:
    if (mapped) impl->api.unmap_memory(impl->device,staging.memory);
    gpu_buffer_destroy(impl,&staging); gpu_buffer_destroy(impl,target); return -1;
}

struct rf_gpu_graphics_resource *rf_gpu_graphics_skinned_resource_create(
    struct rf_gpu_graphics *g,const struct rf_gpu_graphics_vertex *reference,
    uint32_t vertex_count,const uint32_t *indices,uint32_t index_count,
    const uint32_t *bind_words,uint32_t bind_word_count,
    const uint32_t *palette_words,uint32_t palette_word_count,
    const uint32_t *rgb_texels,uint32_t texture_width,uint32_t texture_height)
{
    struct rf_gpu_graphics_resource *r;
    struct rf_vk_descriptor_pool_size size={RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,3};
    struct rf_vk_descriptor_pool_create_info pool={0};
    struct rf_vk_descriptor_set_allocate_info allocate={0};
    struct rf_vk_descriptor_buffer_info buffers[3]={{0}};
    struct rf_vk_write_descriptor_set writes[3]={{0}};
    struct rf_vk_memory_barrier barrier={0};
    if (!g || bind_word_count!=(uint64_t)vertex_count*22 || !palette_word_count) return NULL;
    r=rf_gpu_graphics_resource_create(g,reference,vertex_count,indices,index_count,
        rgb_texels,texture_width,texture_height);
    if (!r) return NULL;
    if (gfx_upload_skin_input(g,&r->skin_bind,bind_words,(uint64_t)bind_word_count*4)<0 ||
        gfx_upload_skin_input(g,&r->skin_palette,palette_words,(uint64_t)palette_word_count*4)<0) goto fail;
    pool.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO; pool.max_sets=1;
    pool.pool_size_count=1; pool.pool_sizes=&size;
    if (g->owner->api.create_descriptor_pool(g->owner->device,&pool,NULL,&r->skin_descriptor_pool)!=RF_VK_SUCCESS) goto fail;
    allocate.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    allocate.descriptor_pool=r->skin_descriptor_pool; allocate.descriptor_set_count=1;
    allocate.set_layouts=&g->skin_set_layout;
    if (g->owner->api.allocate_descriptor_sets(g->owner->device,&allocate,&r->skin_descriptor_set)!=RF_VK_SUCCESS) goto fail;
    buffers[0].buffer=r->skin_bind.buffer; buffers[0].range=r->skin_bind.size;
    buffers[1].buffer=r->skin_palette.buffer; buffers[1].range=r->skin_palette.size;
    buffers[2].buffer=r->vertices.buffer; buffers[2].range=r->vertices.size;
    for(unsigned i=0;i<3;++i) {
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET; writes[i].dst_set=r->skin_descriptor_set;
        writes[i].dst_binding=i; writes[i].descriptor_count=1;
        writes[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; writes[i].buffer_info=&buffers[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device,3,writes,0,NULL);
    if (gfx_begin(g)<0) goto fail;
    g->owner->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->skin_pipeline);
    g->owner->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,
        g->skin_layout,0,1,&r->skin_descriptor_set,0,NULL);
    g->owner->api.cmd_push_constants(g->cmd,g->skin_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&vertex_count);
    g->owner->api.cmd_dispatch(g->cmd,(vertex_count+63)/64,1,1);
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    if (gfx_submit(g,1,RF_GPU_SUBMIT_SKINNING)<0) goto fail;
    r->palette_word_capacity=palette_word_count;
    g->stats.mesh_upload_bytes+=(uint64_t)(bind_word_count+palette_word_count)*4;
    return r;
fail:
    rf_gpu_graphics_resource_destroy(g,r); return NULL;
}

static void gfx_skin_record(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r,
    struct rf_gpu_vulkan_buffer **upload,const uint64_t *bytes,
    uint32_t vertex_count)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    struct rf_gpu_vulkan_buffer *target[2]={&r->skin_bind,&r->skin_palette};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    for (unsigned i=0;i<2;++i) {
        if (!upload[i] || upload[i]==target[i]) continue;
        copy.size=bytes[i];
        impl->api.cmd_copy_buffer(g->cmd,upload[i]->buffer,target[i]->buffer,1,&copy);
    }
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    impl->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->skin_pipeline);
    impl->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,
        g->skin_layout,0,1,&r->skin_descriptor_set,0,NULL);
    impl->api.cmd_push_constants(g->cmd,g->skin_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,4,&vertex_count);
    impl->api.cmd_dispatch(g->cmd,(vertex_count+63)/64,1,1);
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT|VK_ACCESS_TRANSFER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
}

int rf_gpu_graphics_skin_batch_begin(struct rf_gpu_graphics *g)
{
    if (!g || g->poisoned || g->scene_in_flight || g->skin_batch) return -1;
    g->skin_batch=1;return 0;
}
void rf_gpu_graphics_skin_batch_cancel(struct rf_gpu_graphics *g)
{
    if (!g) return;
    while (g->skin_pending) {
        struct rf_gpu_graphics_resource *r=g->skin_pending;
        g->skin_pending=r->skin_pending_next;
        r->skin_pending_next=NULL;r->skin_pending_vertices=0;
    }
    g->skin_pending_tail=NULL;g->skin_batch=0;
}
int rf_gpu_graphics_skin_batch_end(struct rf_gpu_graphics *g)
{
    int result=0;
    if (!g || !g->skin_batch || g->poisoned || g->scene_in_flight) return -1;
    if (g->skin_pending) {
        if (gfx_begin(g)<0) { g->poisoned=1;return -1; }
        for (struct rf_gpu_graphics_resource *r=g->skin_pending;r;r=r->skin_pending_next)
            gfx_skin_record(g,r,r->skin_pending_upload,r->skin_pending_bytes,r->skin_pending_vertices);
        result=gfx_submit(g,1,RF_GPU_SUBMIT_SKINNING);
        if(!result) for(struct rf_gpu_graphics_resource *r=g->skin_pending;r;r=r->skin_pending_next)
            r->skin_cached=r->skin_last_palette!=NULL;
    }
    rf_gpu_graphics_skin_batch_cancel(g);
    return result;
}

int rf_gpu_graphics_skinned_resource_update(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *r, uint32_t vertex_count,
    const uint32_t *bind_words, uint32_t bind_word_count,
    const uint32_t *palette_words, uint32_t palette_word_count)
{
    struct rf_gpu_vulkan_impl *impl;
    struct rf_gpu_vulkan_buffer staging[2]={{0}};
    struct rf_gpu_vulkan_buffer *upload[2]={NULL,NULL};
    const uint32_t *data[2]={bind_words,palette_words};
    struct rf_gpu_vulkan_buffer *target[2];
    uint64_t bytes[2];
    void *mapped=NULL;
    unsigned mapped_index=0;
    int result=-1;
    int legacy=g && g->legacy_skin;
    int force_staging=g && g->staging_skin;
    if (!g || g->poisoned || g->scene_in_flight || !r || r->owner!=g || !r->skin_descriptor_set ||
        !vertex_count || (bind_words ? bind_word_count!=(uint64_t)vertex_count*22 : bind_word_count!=0) ||
        !palette_word_count ||
        !palette_words || r->skin_pending_vertices ||
        !gfx_resource_readers_idle(r) || (g->skin_batch && legacy)) return -1;
    if (vertex_count>r->vertex_capacity ||
        palette_word_count>r->palette_word_capacity) return 1;
    int reuse=g->reuse_pose;
    if(reuse && !bind_words && r->skin_cached && r->skin_last_vertices==vertex_count &&
        r->skin_last_words==palette_word_count &&
        !memcmp(r->skin_last_palette,palette_words,(size_t)palette_word_count*4)) {
        g->stats.skin_reused++;return 0;
    }
    r->skin_cached=0;
    if(reuse) {
        void *next=r->skin_last_words==palette_word_count && r->skin_last_palette ?
            r->skin_last_palette : realloc(r->skin_last_palette,(size_t)palette_word_count*4);
        if(next) {
            r->skin_last_palette=next;r->skin_last_words=palette_word_count;r->skin_last_vertices=vertex_count;
            memcpy(next,palette_words,(size_t)palette_word_count*4);
        } else {free(r->skin_last_palette);r->skin_last_palette=NULL;}
    } else {free(r->skin_last_palette);r->skin_last_palette=NULL;}
    impl=g->owner;
    target[0]=&r->skin_bind; target[1]=&r->skin_palette;
    if(!reuse || legacy) for(unsigned i=0;i<2;++i) if(r->skin_mapped[i]) {
        impl->api.unmap_memory(impl->device,r->skin_mapped_buffer[i]->memory);
        r->skin_mapped[i]=NULL;r->skin_mapped_buffer[i]=NULL;
    }
    bytes[0]=(uint64_t)bind_word_count*4; bytes[1]=(uint64_t)palette_word_count*4;
    for (unsigned i=0;i<2;++i) {
        if (!data[i]) continue;
        /* Previous skin dispatch and Scene use have retired before host writes.
         * Prefer the host-visible device allocation; retain fallback staging. */
        upload[i]=!legacy && !force_staging && (target[i]->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT) ?
            target[i] : legacy ? &staging[i] : &r->skin_staging[i];
        if (!upload[i]->memory && gpu_buffer_create(impl,target[i]->size,RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                upload[i])<0) goto done;
        if(reuse && !legacy) {
            if(r->skin_mapped[i] && r->skin_mapped_buffer[i]!=upload[i]) {
                impl->api.unmap_memory(impl->device,r->skin_mapped_buffer[i]->memory);
                r->skin_mapped[i]=NULL;
            }
            if(!r->skin_mapped[i] && impl->api.map_memory(impl->device,upload[i]->memory,0,
                upload[i]->allocation_size,0,&r->skin_mapped[i])!=RF_VK_SUCCESS) goto done;
            r->skin_mapped_buffer[i]=upload[i];mapped=r->skin_mapped[i];
        } else if (impl->api.map_memory(impl->device,upload[i]->memory,0,
                upload[i]->allocation_size,0,&mapped)!=RF_VK_SUCCESS) goto done;
        mapped_index=i;
        memcpy(mapped,data[i],(size_t)bytes[i]);
        if (!(upload[i]->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            struct rf_vk_mapped_memory_range range={0};
            range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
            range.memory=upload[i]->memory; range.size=upload[i]->allocation_size;
            if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) goto done;
        }
        if(mapped!=r->skin_mapped[i]) impl->api.unmap_memory(impl->device,upload[i]->memory);
        mapped=NULL;
    }
    if (g->skin_batch) {
        r->skin_pending_vertices=vertex_count;
        memcpy(r->skin_pending_upload,upload,sizeof(upload));
        memcpy(r->skin_pending_bytes,bytes,sizeof(bytes));
        if (g->skin_pending_tail) g->skin_pending_tail->skin_pending_next=r;
        else g->skin_pending=r;
        g->skin_pending_tail=r;
    } else {
        if (gfx_begin(g)<0) goto done;
        gfx_skin_record(g,r,upload,bytes,vertex_count);
        if (gfx_submit(g,1,RF_GPU_SUBMIT_SKINNING)<0) goto done;
        r->skin_cached=r->skin_last_palette!=NULL;
    }
    g->stats.mesh_upload_bytes+=bytes[0]+bytes[1];
    result=0;
done:
    if (mapped && mapped!=r->skin_mapped[mapped_index])
        impl->api.unmap_memory(impl->device,upload[mapped_index]->memory);
    for (unsigned i=0;i<2;++i) gpu_buffer_destroy(impl,&staging[i]);
    return result;
}

int rf_gpu_graphics_resource_bind(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource)
{
    if (!g || g->poisoned || !gfx_resource_compatible(g,resource) ||
        gfx_resource_share(g,resource)<0 ||
        !resource->index_count) return -1;
    g->resource = resource;
    return 0;
}

int rf_gpu_graphics_resource_set_frame_dynamic(
    struct rf_gpu_graphics_resource *resource)
{
    if (!resource || !resource->owner) return -1;
    resource->frame_dynamic=1;
    return 0;
}

int rf_gpu_graphics_resource_destroy(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource)
{
    struct rf_gpu_graphics_resource **link;
    if (!g || g->scene_in_flight || !resource || resource->owner != g ||
        resource->skin_pending_vertices || (!g->destroying && !gfx_resource_readers_idle(resource))) return -1;
    /* Successful submissions already waited. A failed wait may leave work. */
    if (g->poisoned) g->owner->api.queue_wait_idle(g->owner->queue);
    for (link = &g->resources; *link != resource; link = &(*link)->next)
        if (!*link) return -1;
    *link = resource->next;
    if (g->resource == resource) g->resource = NULL;
    if (resource->descriptor_pool)
        g->owner->api.destroy_descriptor_pool(g->owner->device, resource->descriptor_pool, NULL);
    while(resource->readers)gfx_reader_release(resource->readers);
    if (resource->skin_descriptor_pool)
        g->owner->api.destroy_descriptor_pool(g->owner->device,resource->skin_descriptor_pool,NULL);
    for(unsigned i=0;i<2;++i) if(resource->skin_mapped[i])
        g->owner->api.unmap_memory(g->owner->device,resource->skin_mapped_buffer[i]->memory);
    free(resource->skin_last_palette);
    gpu_buffer_destroy(g->owner,&resource->skin_bind);
    gpu_buffer_destroy(g->owner,&resource->skin_palette);
    for (unsigned i=0;i<2;++i) gpu_buffer_destroy(g->owner,&resource->skin_staging[i]);
    if (resource->triangle_mapped) {
        struct rf_gpu_vulkan_buffer *mapped_buffer=
            resource->triangle_staging.memory ? &resource->triangle_staging : &resource->vertices;
        g->owner->api.unmap_memory(g->owner->device,mapped_buffer->memory);
    }
    gpu_buffer_destroy(g->owner,&resource->triangle_staging);
    gpu_buffer_destroy(g->owner, &resource->vertices);
    gpu_buffer_destroy(g->owner, &resource->indices);
    gpu_buffer_destroy(g->owner, &resource->texture);
    gfx_texture_set_release(g,resource->texture_set);
    free(resource);
    return 0;
}

int rf_gpu_graphics_upload(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height)
{
    struct rf_gpu_graphics_resource *resource;
    if (!g || g->resources) return -1;
    resource = rf_gpu_graphics_resource_create(g, vertices, vertex_count,
        indices, index_count, rgb_texels, texture_width, texture_height);
    return resource ? rf_gpu_graphics_resource_bind(g, resource) : -1;
}

static int gfx_draw_valid(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *d, int validate_resource_bounds)
{
    uint32_t width=g->resource->texture_width,height=g->resource->texture_height;
    if (d->quality[3]==2) {
        const struct rf_graphics_texture_set *set=g->resource->texture_set;
        if (d->material[2]) {
            uint32_t index=d->material[2]-1;
            if (!set || index>=set->count) return 0;
            width=set->width[index];height=set->height[index];
        } else { width=height=1; }
    } else if (d->material[2]>1 || (d->material[2] && g->resource->texture_set)) return 0;
    if (!__builtin_isfinite(d->roughness) || !__builtin_isfinite(d->metallic) ||
        !__builtin_isfinite(d->emissive) || !d->index_count || d->index_count % 3 || d->first_index % 3 ||
        d->first_index > g->resource->index_count || d->index_count > g->resource->index_count-d->first_index ||
        d->integer_depth != 0 || d->double_sided > 1 || d->material[1] > 256 ||
        d->material[3] > 2 ||
        ((d->material[3]==2) != g->resource->scene_vertex_color) ||
        (d->material[3]==2 && (d->material[2] || d->integer_depth || d->rotation[3])) ||
        d->quality[0]!=0 ||
        (d->quality[1]!=0 && (d->quality[1]<512 || d->quality[1]>65536)) ||
        (d->quality[1]>512 && (d->quality[0]&2)) ||
        d->quality[2]<0 || d->quality[2]>3 || d->quality[3]<0 || d->quality[3]>2 ||
        (d->integer_depth && (d->quality[0] || d->quality[1] || d->quality[2] || d->quality[3])) ||
        (d->quality[2] && d->material[3] && !(d->quality[2]==3 && d->material[3]==2)) ||
        ((d->quality[2]==1 || d->quality[2]==2) && !g->resource->sequential_triangles) ||
        d->camera[3] || d->texture[2]<0 || d->texture[2]>256 ||
        (d->texture[2]==256 && (d->material[3]!=2 || !g->resource->vertex_alpha_valid)) ||
        (d->texture[2] && d->integer_depth) ||
        d->texture[3]<0 || d->texture[3]>2 ||
        (d->texture[3] && d->integer_depth) ||
        d->scene_layer>=RF_GPU_SCENE_LAYER_COUNT ||
        d->translation_scale[3] < 1 || d->translation_scale[3] > 8000 ||
        d->rotation[2] < -32767 || d->rotation[2] > 32767 ||
        d->rotation[3] < 0 || d->rotation[3] > 1 ||
        d->projection[0] != (int)g->target.width || d->projection[1] != (int)g->target.height ||
        d->projection[2] != 64 || d->projection[3] != (int)g->target.width*3/4 ||
        d->texture[0] != (int)width || d->texture[1] != (int)height) return 0;
    /* Runtime Map permits a 240000 RFU room. Keep one signed power-of-two
     * envelope beyond that contract so chunk-local meshes can place their
     * origins at the WHU south edge without weakening the int32 shader ABI. */
    for (unsigned i = 0; i < 3; ++i)
        if (d->translation_scale[i] < -262144 || d->translation_scale[i] > 262144 ||
            d->camera[i] < -262144 || d->camera[i] > 262144) return 0;
    for (unsigned i = 0; i < 2; ++i)
        if (d->rotation[i] < -1024 || d->rotation[i] > 1024) return 0;
    for (unsigned i = 0; i < 4; ++i)
        if (d->view[i] < -1024 || d->view[i] > 1024) return 0;
    (void)validate_resource_bounds;
    return 1;
}

int rf_gpu_graphics_validate_draw(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draw)
{
    return g && !g->poisoned && g->resource && g->target.framebuffer && draw &&
        gfx_draw_valid(g, draw, 1) ? 0 : -1;
}

int rf_gpu_graphics_validate_dynamic_draw(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draw)
{
    /* A frame-slot character resource combines many independently translated
     * instances, so its aggregate position bounds are not a valid bound for one
     * Draw. The producer has already range-checked each bind vertex and the
     * common validator still enforces transforms, projection and index range. */
    return g && !g->poisoned && g->resource && g->target.framebuffer && draw &&
        gfx_draw_valid(g, draw, 0) ? 0 : -1;
}

int rf_gpu_graphics_scene_background(struct rf_gpu_graphics *g,float red,float green,float blue)
{
    if(!g || g->scene_in_flight || g->poisoned ||
        !__builtin_isfinite(red) || !__builtin_isfinite(green) || !__builtin_isfinite(blue) ||
        red<0 || green<0 || blue<0 || red>1 || green>1 || blue>1)return -1;
    g->background[0]=red;g->background[1]=green;g->background[2]=blue;return 0;
}

int rf_gpu_graphics_scene_video_at(struct rf_gpu_graphics *g,unsigned slot,
    struct rf_gpu_graphics *source,int x,int y,int width,int height)
{
    if(slot>=RF_GPU_GRAPHICS_VIDEO_SLOTS || !g || g->scene_in_flight || g->poisoned)return -1;
    g->video[slot]=NULL;
    if(!source)return 0;
    if(source==g || source->owner!=g->owner || source->poisoned ||
        source->scene_in_flight || !source->target.valid || x<0 || y<0 ||
        width<=0 || height<=0 || (uint64_t)x+width>g->target.width ||
        (uint64_t)y+height>g->target.height)return -1;
    g->video[slot]=source;g->video_rect[slot][0]=x;g->video_rect[slot][1]=y;
    g->video_rect[slot][2]=width;g->video_rect[slot][3]=height;return 0;
}

int rf_gpu_graphics_scene_video(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics *source,int x,int y,int width,int height)
{ return rf_gpu_graphics_scene_video_at(g,0,source,x,y,width,height); }

static void gfx_video_record(struct rf_gpu_graphics *g)
{
    struct rf_vk_image_memory_barrier barrier={0};
    rf_scene_blit blit={0};
    int any=0;for(unsigned slot=0;slot<RF_GPU_GRAPHICS_VIDEO_SLOTS;++slot)any|=g->video[slot]!=NULL;
    if(!any)return;
    barrier.s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.old_layout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.new_layout=RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;
    barrier.dst_access_mask=RF_VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.src_queue_family_index=barrier.dst_queue_family_index=UINT32_MAX;
    barrier.image=g->target.color.image;
    barrier.subresource_range.aspect_mask=VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresource_range.level_count=barrier.subresource_range.layer_count=1;
    g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
        RF_VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    blit.src.aspectMask=blit.dst.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    blit.src.layerCount=blit.dst.layerCount=1;
    for(unsigned slot=0;slot<RF_GPU_GRAPHICS_VIDEO_SLOTS;++slot)if(g->video[slot]) {
        blit.src_offsets[1].x=g->video[slot]->target.width;
        blit.src_offsets[1].y=g->video[slot]->target.height;blit.src_offsets[1].z=1;
        blit.dst_offsets[0].x=g->video_rect[slot][0];blit.dst_offsets[0].y=g->video_rect[slot][1];
        blit.dst_offsets[1].x=g->video_rect[slot][0]+g->video_rect[slot][2];
        blit.dst_offsets[1].y=g->video_rect[slot][1]+g->video_rect[slot][3];blit.dst_offsets[1].z=1;
        g->api.CmdBlitImage(g->cmd,g->video[slot]->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
            g->target.color.image,RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,0);
    }
    barrier.old_layout=RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.new_layout=VK_IMAGE_LAYOUT_GENERAL;
    barrier.src_access_mask=RF_VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_COLOR_ATTACHMENT_READ_BIT|VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,0,0,NULL,0,NULL,1,&barrier);
}

static void gfx_main_timestamps(struct rf_gpu_graphics *g,unsigned *next,unsigned through)
{
    while(*next<=through) {
        if(g->scene_query_active)g->owner->api.cmd_write_timestamp(g->cmd,
            RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,*next);
        ++*next;
    }
}
static int gfx_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count)
{
    struct rf_gpu_vulkan_impl *impl;
    uint64_t offset = 0;
    VkClearValue clear[2];
    VkRenderPassBeginInfo begin = {0};
    VkViewport viewport = {0};
    VkRect2D scissor = {{0,0},{0,0}};
    if (!g || g->poisoned || g->skin_batch || !items || !count ||
        count > 65536 || !g->target.framebuffer) return -1;
    {
        struct rf_gpu_graphics_resource *saved = g->resource;
        int valid = 1,transparent_started=0,last_layer=-1;
        for (uint32_t i = 0; i < count; ++i) {
            const struct rf_gpu_graphics_draw *d = &items[i].draw;
            int layer=d->scene_layer==RF_GPU_SCENE_SKY ? 0 :
                d->scene_layer==RF_GPU_SCENE_WORLD ? 1 : (int)d->scene_layer;
            if (layer!=last_layer) transparent_started=0;
            g->resource = items[i].resource;
            if (!gfx_resource_compatible(g,g->resource) ||
                !gfx_draw_valid(g, d, !g->resource->frame_dynamic) ||
                (layer<last_layer ||
                    (transparent_started && !d->texture[2]) ||
                    ((d->scene_layer==RF_GPU_SCENE_SKY ||
                      d->scene_layer==RF_GPU_SCENE_OVERLAY) && d->texture[3]!=2) ||
                    (d->scene_layer==RF_GPU_SCENE_TRANSPARENT && !d->texture[2]))) { valid = 0; break; }
            if (d->texture[2]) transparent_started=1;
            last_layer=layer;
        }
        g->resource = saved;
        if (!valid) return -1;
    }
    impl = g->owner;
    memset(clear, 0, sizeof(clear));
    if(gfx_lighting_upload(g,items,count)<0) return -1;
    if (gfx_begin(g) < 0) { g->poisoned=1; return -1; }
    if (g->scene_query_active) {
        impl->api.cmd_reset_query_pool(g->cmd,g->scene_query_pool,0,12);
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,g->scene_query_pool,0);
    }
    gfx_sky_record(g,items,count);
    if (g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,4);
    gfx_light_tiles_record(g);
    if(g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,6);
    gfx_indirect_record(g);
    if(g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,11);
    gfx_shadow_record(g,items,count);
    if (g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,5);
    for(unsigned channel=0;channel<3;++channel)clear[0].color.float32[channel]=g->background[channel];
    clear[0].color.float32[3] = 1.0f;
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = g->render_pass; begin.framebuffer = g->target.framebuffer;
    begin.renderArea.extent.width = g->target.width; begin.renderArea.extent.height = g->target.height;
    begin.clearValueCount = 2; begin.pClearValues = clear;
    g->api.CmdBeginRenderPass(g->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = (float)g->target.width; viewport.height = (float)g->target.height; viewport.maxDepth = 1.0f;
    scissor.extent = begin.renderArea.extent;
    g->api.CmdSetViewport(g->cmd, 0, 1, &viewport); g->api.CmdSetScissor(g->cmd, 0, 1, &scissor);
    impl->api.cmd_bind_descriptor_sets(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->layout,1,1,&g->light_set,0,NULL);
    int viewmodel_started=0,overlay_started=0;
    struct rf_gpu_graphics_resource *bound_resource=NULL;
    int bound_integer=-1,bound_pipeline=-1;
    const char *legacy_bind=getenv("RF_GPU_SCENE_LEGACY_BIND");
    int reuse_bind=!(legacy_bind && !strcmp(legacy_bind,"1"));
    unsigned drawn=0;
    unsigned next_timestamp=7;
    for (uint32_t i = 0; i < count; ++i) {
        const struct rf_gpu_graphics_draw *d = &items[i].draw;
        if(d->scene_layer>=RF_GPU_SCENE_TRANSPARENT)gfx_main_timestamps(g,&next_timestamp,7);
        if(d->scene_layer>=RF_GPU_SCENE_VIEWMODEL)gfx_main_timestamps(g,&next_timestamp,8);
        if(d->scene_layer>=RF_GPU_SCENE_OVERLAY)gfx_main_timestamps(g,&next_timestamp,9);
        if (!g->disable_draw_cull && (d->scene_layer==RF_GPU_SCENE_WORLD || d->scene_layer==RF_GPU_SCENE_TRANSPARENT) &&
            gfx_draw_camera_outside(&items[i])) continue;
        if(d->scene_layer==RF_GPU_SCENE_OVERLAY && !overlay_started) {
            g->api.CmdEndRenderPass(g->cmd);gfx_tonemap_record(g);gfx_video_record(g);
            gfx_main_timestamps(g,&next_timestamp,10);
            begin.renderPass=g->overlay_pass;begin.framebuffer=g->target.overlay_framebuffer;
            g->api.CmdBeginRenderPass(g->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);
            impl->api.cmd_bind_descriptor_sets(g->cmd,VK_PIPELINE_BIND_POINT_GRAPHICS,g->layout,1,1,&g->light_set,0,NULL);
            bound_resource=NULL;bound_pipeline=bound_integer=-1;overlay_started=1;
        }
        if (d->scene_layer==RF_GPU_SCENE_VIEWMODEL && !viewmodel_started) {
            rf_scene_clear_attachment attachment={0};
            rf_scene_clear_rect rect={0};
            attachment.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
            rect.rect.extent=begin.renderArea.extent;rect.layerCount=1;
            g->api.CmdClearAttachments(g->cmd,1,&attachment,1,&rect);
            viewmodel_started=1;
        }
        struct rf_gpu_graphics_resource *resource = items[i].resource;
        uint32_t factor = 1;
        /* State is local to this render pass/command buffer. ClearAttachments
         * does not invalidate bindings; every draw still pushes its constants. */
        if (!reuse_bind || bound_resource!=resource) {
            g->api.CmdBindVertexBuffers(g->cmd, 0, 1, &resource->vertices.buffer, &offset);
            rf_vk_descriptor_set descriptor=gfx_resource_descriptor(g,resource);
            if(!descriptor){g->poisoned=1;return -1;}
            impl->api.cmd_bind_descriptor_sets(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
                g->layout, 0, 1, &descriptor, 0, NULL);
        }
        if (!reuse_bind || bound_resource!=resource || bound_integer!=(int)d->integer_depth)
            g->api.CmdBindIndexBuffer(g->cmd, resource->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        int pipeline=resource->scene_vertex_color ? 12+(d->texture[3]==2?4:(d->texture[2]?2:0))+
            d->double_sided : d->double_sided+
            (d->texture[3]==2?8:(d->texture[2]?4:0));
        if(overlay_started) pipeline=18+(resource->scene_vertex_color?6:0)+(d->texture[3]==2?4:d->texture[2]?2:0)+d->double_sided;
        if (!reuse_bind || bound_pipeline!=pipeline)
            impl->api.cmd_bind_pipeline(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,g->pipelines[pipeline]);
        bound_resource=resource;bound_integer=d->integer_depth;bound_pipeline=pipeline;
        struct rf_gpu_graphics_draw modern=*d;
        float rough=d->roughness>0?d->roughness:0.65f;
        float metal=d->metallic<0?0:d->metallic>1?1:d->metallic;
        float emission=d->emissive<0?0:d->emissive>15?15:d->emissive;
        modern.material[1]=(uint32_t)((rough>1?1:rough)*255)|((uint32_t)(metal*255)<<8)|((uint32_t)(emission*16)<<16);
        if(resource->scene_vertex_color && modern.quality[2]!=3) modern.quality[2]=0;
        if(d->scene_layer==RF_GPU_SCENE_EFFECTS) modern.quality[2]=3;
        if(d->scene_layer==RF_GPU_SCENE_VIEWMODEL) modern.quality[0]|=32;
        if(d->scene_layer==RF_GPU_SCENE_SKY) modern.quality[0]|=64;
        if(overlay_started) modern.quality[0]|=16;
        g->api.CmdPushConstants(g->cmd,g->layout,VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT,0,128,&modern);
        g->api.CmdDrawIndexed(g->cmd, d->index_count*factor, 1, d->first_index*factor, 0, 0);
        drawn++;
    }
    gfx_main_timestamps(g,&next_timestamp,9);
    g->api.CmdEndRenderPass(g->cmd);
    if(!overlay_started) {
        gfx_tonemap_record(g);gfx_video_record(g);gfx_main_timestamps(g,&next_timestamp,10);
        begin.renderPass=g->overlay_pass;begin.framebuffer=g->target.overlay_framebuffer;
        g->api.CmdBeginRenderPass(g->cmd,&begin,VK_SUBPASS_CONTENTS_INLINE);g->api.CmdEndRenderPass(g->cmd);
    }
    if (g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,1);
    if(impl->light_profile_enabled) {
        struct rf_vk_memory_barrier barrier={0};barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_HOST_READ_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
            VK_PIPELINE_STAGE_HOST_BIT,0,1,&barrier,0,NULL,0,NULL);
    }
    g->stats.instance_upload_bytes += (uint64_t)drawn*128;
    g->stats.indexed_draws += drawn; g->stats.frames++;
    g->target.valid=1;
    return 0;
}

void rf_gpu_graphics_get_stats(const struct rf_gpu_graphics *g, struct rf_gpu_graphics_stats *stats)
{
    if (g && stats) {
        *stats = g->stats;
        stats->lights=g->lighting.count;stats->shadow_maps=g->shadow_count;
        stats->present_mode=g->owner->present_mode;
    }
}
#undef RF_GFX_FUNCTIONS

/* Read only queries written by this submission. Offscreen/capture never write
 * the present pair (2,3); waiting on those would wait indefinitely. */
static int gfx_scene_timing_read(struct rf_gpu_graphics *g,int presented)
{
    struct rf_gpu_vulkan_impl *p=g->owner;
    uint64_t ticks[12]={0},mask=p->timestamp_valid_bits>=64?UINT64_MAX:
        ((1ULL<<p->timestamp_valid_bits)-1ULL);
    unsigned count=presented?12:2;
    if(p->api.get_query_pool_results(p->device,g->scene_query_pool,0,count,
        count*sizeof(ticks[0]),ticks,sizeof(ticks[0]),
        RF_VK_QUERY_RESULT_64_BIT|RF_VK_QUERY_RESULT_WAIT_BIT)!=RF_VK_SUCCESS)return -1;
    if(!presented && p->api.get_query_pool_results(p->device,g->scene_query_pool,4,8,
        8*sizeof(ticks[0]),ticks+4,sizeof(ticks[0]),
        RF_VK_QUERY_RESULT_64_BIT|RF_VK_QUERY_RESULT_WAIT_BIT)!=RF_VK_SUCCESS)return -1;
    double ms=p->timestamp_period/1000000.0;
    g->scene_timing.world_draw_ms=(double)((ticks[1]-ticks[0])&mask)*ms;
    g->scene_timing.present_blit_ms=(double)((ticks[3]-ticks[2])&mask)*ms;
    g->scene_timing.sky_compute_ms=(double)((ticks[4]-ticks[0])&mask)*ms;
    g->scene_timing.shadow_ms=(double)((ticks[5]-ticks[11])&mask)*ms;
    g->scene_timing.main_scene_ms=(double)((ticks[1]-ticks[5])&mask)*ms;
    const unsigned starts[6]={4,5,7,8,9,10},ends[6]={6,7,8,9,10,1};
    for(unsigned i=0;i<6;++i)g->scene_timing.detail_ms[i]=(double)((ticks[ends[i]]-ticks[starts[i]])&mask)*ms;
    g->scene_timing.detail_ms[6]=(double)((ticks[11]-ticks[6])&mask)*ms;
    g->scene_timing.valid=1;
    return 0;
}

/* Scene owns its command/fence/acquire semaphore. The backend still owns the
 * unique swapchain and image-indexed present semaphores. */
int rf_gpu_graphics_scene_retire(struct rf_gpu_graphics *g)
{
    double start;
    if (!g) return -1;
    if (!g->scene_in_flight) return g->poisoned ? -1 : 0;
    start=now_ms();
    if (g->owner->api.wait_for_fences(g->owner->device,1,&g->fence,
            RF_VK_TRUE,UINT64_MAX)!=RF_VK_SUCCESS) { g->poisoned=1; return -1; }
    g->stats.fence_waits++;
    g->stats.fence_wait_wall_ms+=now_ms()-start;
    g->scene_in_flight=0;
    if (g->scene_query_active) {
        gfx_scene_timing_read(g,1);
        g->scene_query_active=0;
    }
    return gfx_light_profile_read(g);
}

void rf_gpu_graphics_scene_timing(const struct rf_gpu_graphics *g,
    struct rf_gpu_scene_timing *timing)
{
    if (timing) {
        memset(timing,0,sizeof(*timing));
        if (g) *timing=g->scene_timing;
    }
}

int rf_gpu_graphics_scene_present(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,uint64_t frame_id)
{
    struct rf_gpu_vulkan_impl *impl;
    struct rf_vk_semaphore_create_info sem={0};
    struct rf_vk_image_memory_barrier barrier={0};
    struct rf_vk_submit_info submit={0};
    struct rf_vk_present_info present={0};
    rf_scene_blit blit={0};
    static const rf_vk_flags wait_stage=RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    uint32_t index=0;
    int result, acquired=0;
    double start;
    VkFormatProperties src,dst;
    if (!g || g->poisoned || g->skin_batch || g->scene_in_flight || !items || !count || !frame_id) return -1;
    impl=g->owner;
    if (!impl->native_presentation_supported || impl->presenter_poisoned) return -1;
    /* Vulkan 1.0 blit performs RGBA -> BGRA conversion without a bridge. */
    g->api.GetPhysicalDeviceFormatProperties(impl->physical_device,VK_FORMAT_R8G8B8A8_UNORM,&src);
    g->api.GetPhysicalDeviceFormatProperties(impl->physical_device,RF_VK_FORMAT_B8G8R8A8_UNORM,&dst);
    if (!(src.optimalTilingFeatures&0x400) || !(dst.optimalTilingFeatures&0x800)) return -1;
    if (!impl->swapchain || impl->swapchain_width!=g->target.width ||
            impl->swapchain_height!=g->target.height)
        if (presenter_swapchain_create(impl,g->target.width,g->target.height)<0) return -1;
    if (!g->scene_acquire) {
        sem.s_type=RF_VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        if (impl->api.create_semaphore(impl->device,&sem,NULL,&g->scene_acquire)!=RF_VK_SUCCESS) return -1;
    }
    if (impl->timestamp_supported && !g->scene_query_pool) {
        struct rf_vk_query_pool_create_info query={0};
        query.s_type=RF_VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.query_type=RF_VK_QUERY_TYPE_TIMESTAMP;query.query_count=12;
        if (impl->api.create_query_pool(impl->device,&query,NULL,&g->scene_query_pool)!=RF_VK_SUCCESS) return -1;
    }
    memset(&g->scene_timing,0,sizeof(g->scene_timing));
    g->scene_timing.frame_id=frame_id;
    g->scene_timing.supported=impl->timestamp_supported;
    g->scene_query_active=g->scene_query_pool!=NULL;
    /* gfx_render validates the entire batch before beginning any recording. */
    start=now_ms();
    if (gfx_render(g,items,count)<0) return -1;
    g->scene_timing.record_ms=now_ms()-start;
    start=now_ms();
    impl->present_attempt++;
    if (present_fault_take(impl,RF_GPU_PRESENT_FAULT_ACQUIRE_OUT_OF_DATE)) {
        if (presenter_swapchain_create(impl,g->target.width,g->target.height)<0) goto failed;
    }
    result=impl->api.acquire_next_image(impl->device,impl->swapchain,UINT64_MAX,
        g->scene_acquire,NULL,&index);
    if (result==RF_VK_ERROR_OUT_OF_DATE_KHR) {
        if (presenter_swapchain_create(impl,g->target.width,g->target.height)<0) goto failed;
        result=impl->api.acquire_next_image(impl->device,impl->swapchain,UINT64_MAX,
            g->scene_acquire,NULL,&index);
    }
    if (result!=RF_VK_SUCCESS && result!=RF_VK_SUBOPTIMAL_KHR) goto failed;
    g->scene_timing.acquire_ms=now_ms()-start;
    acquired=1;
    if (index>=impl->swapchain_image_count ||
        impl->present_images[index].generation!=impl->presenter_generation) goto failed;
    /* Reacquiring this image proves completion of its previous present wait. */
    if (impl->present_images[index].state==RF_GPU_PRESENT_AUDIT_PENDING && impl->outstanding_presents)
        impl->outstanding_presents--;
    impl->present_images[index].state=RF_GPU_PRESENT_AUDIT_REUSABLE;
    impl->present_images[index].render_finished_state=RF_GPU_PRESENT_AUDIT_REUSABLE;
    barrier.s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    barrier.old_layout=RF_VK_IMAGE_LAYOUT_UNDEFINED;
    barrier.new_layout=RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.dst_access_mask=RF_VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.src_queue_family_index=barrier.dst_queue_family_index=UINT32_MAX;
    barrier.image=impl->swapchain_images[index];
    barrier.subresource_range.aspect_mask=RF_VK_IMAGE_ASPECT_COLOR_BIT;
    barrier.subresource_range.level_count=barrier.subresource_range.layer_count=1;
    impl->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        RF_VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,1,&barrier);
    if (g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,g->scene_query_pool,2);
    blit.src.aspectMask=blit.dst.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;
    blit.src.layerCount=blit.dst.layerCount=1;
    blit.src_offsets[1].x=g->target.width; blit.src_offsets[1].y=g->target.height;
    blit.dst_offsets[1].x=impl->swapchain_width; blit.dst_offsets[1].y=impl->swapchain_height;
    blit.src_offsets[1].z=blit.dst_offsets[1].z=1;
    g->api.CmdBlitImage(g->cmd,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        impl->swapchain_images[index],RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&blit,0);
    barrier.src_access_mask=RF_VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dst_access_mask=0;
    barrier.old_layout=RF_VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    barrier.new_layout=RF_VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    impl->api.cmd_pipeline_barrier(g->cmd,RF_VK_PIPELINE_STAGE_TRANSFER_BIT,
        RF_VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,0,0,NULL,0,NULL,1,&barrier);
    if (g->scene_query_active)
        impl->api.cmd_write_timestamp(g->cmd,RF_VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,g->scene_query_pool,3);
    if (present_fault_take(impl,RF_GPU_PRESENT_FAULT_RECORD_FAILURE) ||
        impl->api.end_command_buffer(g->cmd)!=RF_VK_SUCCESS ||
        impl->api.reset_fences(impl->device,1,&g->fence)!=RF_VK_SUCCESS) goto failed;
    submit.s_type=RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.wait_semaphore_count=submit.signal_semaphore_count=submit.command_buffer_count=1;
    submit.wait_semaphores=&g->scene_acquire; submit.wait_dst_stage_mask=&wait_stage;
    submit.signal_semaphores=&impl->present_images[index].render_finished;
    submit.command_buffers=&g->cmd;
    start=now_ms();
    if (present_fault_take(impl,RF_GPU_PRESENT_FAULT_SUBMIT_FAILURE) ||
        impl->api.queue_submit(impl->queue,1,&submit,g->fence)!=RF_VK_SUCCESS) goto failed;
    g->scene_in_flight=1;
    g->stats.queue_submits++; g->stats.submits_by_kind[RF_GPU_SUBMIT_DRAW]++;
    g->scene_timing.queue_submit_ms=now_ms()-start;
    g->stats.submit_wall_ms+=g->scene_timing.queue_submit_ms;
    present.s_type=RF_VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.wait_semaphore_count=present.swapchain_count=1;
    present.wait_semaphores=submit.signal_semaphores; present.swapchains=&impl->swapchain;
    present.image_indices=&index;
    start=now_ms();
    if (present_fault_take(impl,RF_GPU_PRESENT_FAULT_PRESENT_OUT_OF_DATE)) result=RF_VK_ERROR_OUT_OF_DATE_KHR;
    else result=impl->api.queue_present(impl->queue,&present);
    g->scene_timing.present_ms=now_ms()-start;
    if (result==RF_VK_SUCCESS && present_fault_take(impl,RF_GPU_PRESENT_FAULT_PRESENT_SUBOPTIMAL))
        result=RF_VK_SUBOPTIMAL_KHR;
    if (result==RF_VK_SUCCESS || result==RF_VK_SUBOPTIMAL_KHR) {
        impl->present_images[index].state=RF_GPU_PRESENT_AUDIT_PENDING;
        impl->present_images[index].render_finished_state=RF_GPU_PRESENT_AUDIT_PENDING;
        impl->outstanding_presents++;
    }
    if (result==RF_VK_ERROR_OUT_OF_DATE_KHR || result==RF_VK_SUBOPTIMAL_KHR) {
        if (rf_gpu_graphics_scene_retire(g)<0 ||
            presenter_swapchain_create(impl,g->target.width,g->target.height)<0) goto failed;
    } else if (result!=RF_VK_SUCCESS) goto failed;
    return 0;
failed:
    g->poisoned=1;
    if (acquired) impl->presenter_poisoned=1;
    return -1;
}

int rf_gpu_graphics_scene_offscreen(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,uint64_t frame_id)
{
    if(!g || g->poisoned || g->skin_batch || g->scene_in_flight || !frame_id)return -1;
    memset(&g->scene_timing,0,sizeof(g->scene_timing));
    g->scene_timing.frame_id=frame_id;
    g->scene_timing.supported=g->owner->timestamp_supported;
    if(g->scene_timing.supported && !g->scene_query_pool) {
        struct rf_vk_query_pool_create_info query={0};
        query.s_type=RF_VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.query_type=RF_VK_QUERY_TYPE_TIMESTAMP;query.query_count=12;
        if(g->owner->api.create_query_pool(g->owner->device,&query,NULL,
            &g->scene_query_pool)!=RF_VK_SUCCESS)return -1;
    }
    g->scene_query_active=g->scene_timing.supported;
    double start=now_ms();
    if(gfx_render(g,items,count)<0){g->scene_query_active=0;return -1;}
    g->scene_timing.record_ms=now_ms()-start;
    /* Queue and image dependencies stay on the device; this wait protects the
     * existing single-slot resource mutation contract, never maps the target. */
    int result=gfx_submit(g,1,RF_GPU_SUBMIT_DRAW);
    if(!result && g->scene_query_active) {
        result=gfx_scene_timing_read(g,0);
    }
    if(!result)result=gfx_light_profile_read(g);
    g->scene_query_active=0;return result;
}

int rf_gpu_graphics_scene_capture_at(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t capacity,uint64_t frame_id)
{
    VkBufferImageCopy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    void *mapped=NULL;
    uint64_t bytes;
    if (!g || !rgba || !depth || g->skin_batch || g->scene_in_flight || g->poisoned ||
        (uint64_t)g->target.width*g->target.height>capacity) return -1;
    /* Normal main/auxiliary targets allocate only device-local attachments.
     * Explicit capture is the sole owner of this diagnostic staging buffer. */
    if(!g->target.readback.buffer && gpu_buffer_create(g->owner,
        (uint64_t)g->target.width*g->target.height*8,RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
        RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
        &g->target.readback)<0)return -1;
    memset(&g->scene_timing,0,sizeof(g->scene_timing));
    g->scene_timing.frame_id=frame_id;
    g->scene_timing.supported=frame_id && g->owner->timestamp_supported;
    if (g->scene_timing.supported && !g->scene_query_pool) {
        struct rf_vk_query_pool_create_info query={0};
        query.s_type=RF_VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
        query.query_type=RF_VK_QUERY_TYPE_TIMESTAMP;query.query_count=12;
        if (g->owner->api.create_query_pool(g->owner->device,&query,NULL,
                &g->scene_query_pool)!=RF_VK_SUCCESS) return -1;
    }
    g->scene_query_active=g->scene_timing.supported;
    if (gfx_render(g,items,count)<0) {
        g->scene_query_active=0;return -1;
    }
    bytes=(uint64_t)g->target.width*g->target.height*4;
    copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;
    copy.imageExtent.width=g->target.width;copy.imageExtent.height=g->target.height;copy.imageExtent.depth=1;
    g->api.CmdCopyImageToBuffer(g->cmd,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        g->target.readback.buffer,1,&copy);
    copy.bufferOffset=bytes;copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
    g->api.CmdCopyImageToBuffer(g->cmd,g->target.depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,
        g->target.readback.buffer,1,&copy);
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_HOST_READ_BIT;
    g->owner->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    if (gfx_submit(g,1,RF_GPU_SUBMIT_READBACK)<0 ||
        g->owner->api.map_memory(g->owner->device,g->target.readback.memory,0,
            g->target.readback.allocation_size,0,&mapped)!=RF_VK_SUCCESS) return -1;
    int result=gpu_buffer_invalidate(g->owner,&g->target.readback);
    if (g->scene_query_active) {
        if(gfx_scene_timing_read(g,0)<0)result=-1;
        g->scene_query_active=0;
    }
    if (!result) { memcpy(rgba,mapped,(size_t)bytes);memcpy(depth,(unsigned char *)mapped+bytes,(size_t)bytes); }
    g->owner->api.unmap_memory(g->owner->device,g->target.readback.memory);
    if(!result)result=gfx_light_profile_read(g);
    return result;
}

int rf_gpu_graphics_scene_capture(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_batch_item *items,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t capacity)
{
    return rf_gpu_graphics_scene_capture_at(g,items,count,rgba,depth,capacity,0);
}

#include "rf_gpu_lighting_test.inc"
