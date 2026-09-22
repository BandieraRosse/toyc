/* HG-2A/2B diagnostic executor, sharing the compute backend's device/queue.
 * Included after the buffer helpers; no Vulkan objects escape this owner. */
#include "rf_vulkan_graphics_min.h"
#include "rf_gpu_graphics.h"
#include "rf_gpu_graphics_spirv.inc"
#include <limits.h>

#define RF_GFX_FUNCTIONS(X) \
    X(GetPhysicalDeviceFormatProperties) X(CreateImage) X(DestroyImage) \
    X(GetImageMemoryRequirements) X(BindImageMemory) \
    X(CreateImageView) X(DestroyImageView) X(CreateRenderPass) X(DestroyRenderPass) \
    X(CreateFramebuffer) X(DestroyFramebuffer) X(CreateGraphicsPipelines) \
    X(CmdBeginRenderPass) X(CmdEndRenderPass) X(CmdBindVertexBuffers) \
    X(CmdBindIndexBuffer) X(CmdDrawIndexed) X(CmdSetViewport) X(CmdSetScissor) \
    X(CmdPushConstants) X(CmdCopyImageToBuffer)

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
    struct rf_graphics_image color, depth;
    VkFramebuffer framebuffer;
    struct rf_gpu_vulkan_raster_buffer readback;
    struct rf_gpu_vulkan_raster_buffer bridge_transfer, bridge_raster;
    uint32_t width, height;
    int valid, bridge_valid;
};
struct rf_gpu_graphics_resource {
    struct rf_gpu_graphics *owner;
    struct rf_gpu_graphics_resource *next;
    struct rf_gpu_vulkan_raster_buffer vertices, indices, compat_indices, texture;
    uint32_t index_count, texture_width, texture_height;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set descriptor_set;
    struct {
        struct rf_gpu_graphics *owner;
        rf_vk_descriptor_pool pool;
        rf_vk_descriptor_set set;
    } shared_binding;
    struct rf_gpu_vulkan_raster_buffer skin_bind, skin_palette;
    rf_vk_descriptor_pool skin_descriptor_pool;
    rf_vk_descriptor_set skin_descriptor_set;
    int position_bound[3];
    int frame_dynamic;
};
struct rf_gpu_graphics {
    struct rf_gpu_vulkan_impl *owner;
    struct rf_graphics_api api;
    struct rf_graphics_target target;
    struct rf_gpu_vulkan_raster *shared_rasters;
    struct rf_gpu_graphics_resource *resource, *resources;
    rf_vk_descriptor_set_layout set_layout;
    rf_vk_descriptor_pool descriptor_pool;
    rf_vk_descriptor_set set;
    rf_vk_pipeline_layout layout;
    rf_vk_pipeline pipelines[4];
    VkRenderPass render_pass;
    VkRenderPass load_pass;
    rf_vk_descriptor_set_layout bridge_set_layout;
    rf_vk_descriptor_pool bridge_pool;
    rf_vk_descriptor_set bridge_set;
    rf_vk_buffer bridge_bound_buffers[2];
    uint64_t bridge_bound_ranges[2];
    rf_vk_pipeline_layout bridge_layout;
    rf_vk_pipeline bridge_pipeline;
    rf_vk_descriptor_set_layout skin_set_layout;
    rf_vk_pipeline_layout skin_layout;
    rf_vk_pipeline skin_pipeline;
    rf_vk_command_pool pool;
    rf_vk_command_buffer cmd;
    rf_vk_fence fence;
    int poisoned;
    struct rf_gpu_graphics_stats stats;
};

static void gfx_raster_unshare(struct rf_gpu_vulkan_raster *r)
{
    if (r->shared_color_owner) {
        struct rf_gpu_vulkan_raster **link = &r->shared_color_owner->shared_rasters;
        while (*link && *link != r) link = &(*link)->shared_color_next;
        if (*link) *link = r->shared_color_next;
    }
    r->shared_color_owner = NULL; r->shared_color_next = NULL;
    r->shared_color_image = 0; r->shared_color_view = 0;
    r->shared_color_enabled = r->shared_color_layout_valid = 0;
}

/* Extent replacement invalidates recordings borrowing the old attachment.
 * Submitted work must finish before that attachment can be destroyed. */
static int gfx_rasters_invalidate(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_raster *r;
    for (r = g->shared_rasters; r; r = r->shared_color_next)
        if (raster_recycle_frame(r, UINT64_MAX) != 0) return -1;
    for (r = g->shared_rasters; r; r = r->shared_color_next) {
        r->frame_recording = r->segment_valid = 0;
        r->shared_color_image = 0; r->shared_color_view = 0;
        r->shared_color_enabled = r->shared_color_layout_valid = 0;
        r->binned_stream = r->uploaded_stream = NULL;
        r->binned_stream_size = r->uploaded_stream_size = 0;
        r->preflight_reuse = 0;
    }
    return 0;
}

static int gfx_resource_compatible(const struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_resource *resource)
{
    return g && resource && resource->owner &&
        resource->owner->owner == g->owner;
}

static rf_vk_descriptor_set gfx_resource_descriptor(
    const struct rf_gpu_graphics *g, const struct rf_gpu_graphics_resource *resource)
{
    if (!gfx_resource_compatible(g,resource)) return NULL;
    if (resource->owner==g) return resource->descriptor_set;
    return resource->shared_binding.owner==g ? resource->shared_binding.set : NULL;
}

static int gfx_resource_share(struct rf_gpu_graphics *g,
    struct rf_gpu_graphics_resource *resource)
{
    struct rf_vk_descriptor_pool_size size = {RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
    struct rf_vk_descriptor_pool_create_info pool = {0};
    struct rf_vk_descriptor_set_allocate_info set = {0};
    struct rf_vk_descriptor_buffer_info buffer[3] = {{0}};
    struct rf_vk_write_descriptor_set write[3] = {{0}};
    if (!gfx_resource_compatible(g,resource)) return -1;
    if (resource->owner==g || resource->shared_binding.owner==g) return 0;
    if (resource->shared_binding.owner) return -1;
    pool.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.max_sets=1;pool.pool_size_count=1;pool.pool_sizes=&size;
    if (g->owner->api.create_descriptor_pool(g->owner->device,&pool,NULL,
            &resource->shared_binding.pool)!=RF_VK_SUCCESS) return -1;
    set.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptor_pool=resource->shared_binding.pool;set.descriptor_set_count=1;
    set.set_layouts=&g->set_layout;
    if (g->owner->api.allocate_descriptor_sets(g->owner->device,&set,
            &resource->shared_binding.set)!=RF_VK_SUCCESS) goto failed;
    buffer[0].buffer=resource->texture.buffer;buffer[0].range=resource->texture.size;
    buffer[1].buffer=resource->vertices.buffer;buffer[1].range=resource->vertices.size;
    buffer[2].buffer=resource->indices.buffer;buffer[2].range=resource->indices.size;
    for(unsigned i=0;i<3;++i){
        write[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dst_set=resource->shared_binding.set;write[i].dst_binding=i;
        write[i].descriptor_count=1;write[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        write[i].buffer_info=&buffer[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device,3,write,0,NULL);
    resource->shared_binding.owner=g;
    return 0;
failed:
    g->owner->api.destroy_descriptor_pool(g->owner->device,
        resource->shared_binding.pool,NULL);
    memset(&resource->shared_binding,0,sizeof(resource->shared_binding));
    return -1;
}

_Static_assert(sizeof(struct rf_gpu_graphics_vertex) == 56, "HG vertex ABI");
_Static_assert(sizeof(void *) == 8, "HG-2A supports the existing 64-bit Vulkan ABI");
_Static_assert(offsetof(struct rf_gpu_graphics_draw, first_index) == 112,
               "HG push-constant ABI");

static int gfx_begin(struct rf_gpu_graphics *g)
{
    struct rf_vk_command_buffer_begin_info begin = {0};
    if (g->poisoned || g->owner->api.reset_command_pool(
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
    if (g->stats.wait_frame!=impl->mixed_cpu_frame)
        g->stats.wait_predecessor_frame=0;
    g->stats.wait_frame=impl->mixed_cpu_frame;
    if (impl->next_frame_number>impl->mixed_completed_frame)
        g->stats.wait_predecessor_frame=impl->next_frame_number;
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
    impl->mixed_completed_frame=impl->next_frame_number;
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
    memset(g->bridge_bound_buffers, 0, sizeof(g->bridge_bound_buffers));
    memset(g->bridge_bound_ranges, 0, sizeof(g->bridge_bound_ranges));
    if (t->framebuffer) g->api.DestroyFramebuffer(g->owner->device, t->framebuffer, NULL);
    gfx_image_destroy(g, &t->color); gfx_image_destroy(g, &t->depth);
    raster_buffer_destroy(g->owner, &t->readback);
    raster_buffer_destroy(g->owner, &t->bridge_transfer);
    raster_buffer_destroy(g->owner, &t->bridge_raster);
    memset(t, 0, sizeof(*t));
}

void rf_gpu_graphics_destroy(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *impl;
    if (!g) return;
    impl = g->owner;
    /* Also covers a failed submission/wait: never destroy pending resources. */
    impl->api.queue_wait_idle(impl->queue);
    gfx_rasters_invalidate(g);
    while (g->shared_rasters) gfx_raster_unshare(g->shared_rasters);
    gfx_target_destroy(g, &g->target);
    if (g->pool) impl->api.destroy_command_pool(impl->device, g->pool, NULL);
    if (g->fence) impl->api.destroy_fence(impl->device, g->fence, NULL);
    for (unsigned i = 0; i < 4; ++i)
        if (g->pipelines[i]) impl->api.destroy_pipeline(impl->device, g->pipelines[i], NULL);
    if (g->render_pass) g->api.DestroyRenderPass(impl->device, g->render_pass, NULL);
    if (g->load_pass) g->api.DestroyRenderPass(impl->device, g->load_pass, NULL);
    if (g->bridge_pipeline) impl->api.destroy_pipeline(impl->device, g->bridge_pipeline, NULL);
    if (g->bridge_layout) impl->api.destroy_pipeline_layout(impl->device, g->bridge_layout, NULL);
    if (g->bridge_pool) impl->api.destroy_descriptor_pool(impl->device, g->bridge_pool, NULL);
    if (g->bridge_set_layout) impl->api.destroy_descriptor_set_layout(impl->device, g->bridge_set_layout, NULL);
    if (g->skin_pipeline) impl->api.destroy_pipeline(impl->device,g->skin_pipeline,NULL);
    if (g->skin_layout) impl->api.destroy_pipeline_layout(impl->device,g->skin_layout,NULL);
    if (g->skin_set_layout) impl->api.destroy_descriptor_set_layout(impl->device,g->skin_set_layout,NULL);
    if (g->layout) impl->api.destroy_pipeline_layout(impl->device, g->layout, NULL);
    if (g->descriptor_pool) impl->api.destroy_descriptor_pool(impl->device, g->descriptor_pool, NULL);
    while (g->resources) rf_gpu_graphics_resource_destroy(g, g->resources);
    if (g->set_layout) impl->api.destroy_descriptor_set_layout(impl->device, g->set_layout, NULL);
    free(g);
}

static int gfx_image_create(struct rf_gpu_graphics *g, struct rf_graphics_image *out,
    uint32_t width, uint32_t height, uint32_t format, uint32_t aspect, uint32_t usage)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    VkImageCreateInfo info = {0};
    VkImageViewCreateInfo view = {0};
    VkMemoryRequirements req;
    struct rf_vk_memory_allocate_info allocation = {0};
    int memory;
    info.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
    info.imageType = VK_IMAGE_TYPE_2D; info.format = format;
    info.extent.width = width; info.extent.height = height; info.extent.depth = 1;
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
    view.image = out->image; view.viewType = VK_IMAGE_VIEW_TYPE_2D; view.format = format;
    view.subresourceRange.aspectMask = aspect;
    view.subresourceRange.levelCount = view.subresourceRange.layerCount = 1;
    return g->api.CreateImageView(impl->device, &view, NULL, &out->view) == RF_VK_SUCCESS ? 0 : -1;
}

int rf_gpu_graphics_resize(struct rf_gpu_graphics *g, uint32_t width, uint32_t height)
{
    struct rf_graphics_target t = {0};
    VkFormatProperties properties;
    VkFramebufferCreateInfo fb = {0};
    VkImageView views[2];
    if (!g || g->poisoned || !width || !height || width > 4096 || height > 4096) return -1;
    if (g->target.width == width && g->target.height == height) return 0;
    t.width = width; t.height = height;
    memset(&properties,0,sizeof(properties));
    g->api.GetPhysicalDeviceFormatProperties(g->owner->physical_device,
        VK_FORMAT_R8G8B8A8_UNORM,&properties);
    if (!(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_STORAGE_IMAGE_BIT) ||
        !(properties.optimalTilingFeatures & VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT) ||
        gfx_image_create(g, &t.color, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT,
            VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT|VK_IMAGE_USAGE_STORAGE_BIT) < 0 ||
        gfx_image_create(g, &t.depth, width, height, VK_FORMAT_D32_SFLOAT,
            VK_IMAGE_ASPECT_DEPTH_BIT, VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT) < 0 ||
        raster_buffer_create(g->owner, (uint64_t)width*height*8,
            RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT, RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &t.readback) < 0 ||
        raster_buffer_create(g->owner, (uint64_t)width*height*8,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &t.bridge_transfer) < 0 ||
        raster_buffer_create(g->owner, (uint64_t)width*height*8,
            RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT, RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, &t.bridge_raster) < 0) goto fail;
    views[0] = t.color.view; views[1] = t.depth.view;
    fb.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO; fb.renderPass = g->render_pass;
    fb.attachmentCount = 2; fb.pAttachments = views; fb.width = width; fb.height = height; fb.layers = 1;
    if (g->api.CreateFramebuffer(g->owner->device, &fb, NULL, &t.framebuffer) != RF_VK_SUCCESS) goto fail;
    if (gfx_rasters_invalidate(g) < 0) goto fail;
    gfx_target_destroy(g, &g->target); g->target = t;
    for (struct rf_gpu_vulkan_raster *r = g->shared_rasters; r; r = r->shared_color_next)
        if (r->width == width && r->height == height) {
            r->shared_color_image = t.color.image;
            r->shared_color_view = (rf_vk_image_view)t.color.view;
            r->shared_color_enabled = 1;
        }
    g->stats.target_builds++;
    return 0;
fail:
    gfx_target_destroy(g, &t);
    return -1;
}

int rf_gpu_graphics_share_color(struct rf_gpu_graphics *g, void *raster)
{
    struct rf_gpu_vulkan_raster *r=raster;
    if (!g || !r || g->poisoned || r->owner!=g->owner ||
        (!g->target.valid && !g->target.framebuffer) ||
        r->width!=g->target.width || r->height!=g->target.height ||
        !g->target.color.image || !g->target.color.view) return -1;
    if (r->shared_color_owner != g) {
        /* Rebinding a live recording to a different target is not valid. */
        if (r->shared_color_owner && (r->frame_recording || r->render_in_flight)) return -1;
        gfx_raster_unshare(r);
        r->shared_color_owner = g;
        r->shared_color_next = g->shared_rasters; g->shared_rasters = r;
    }
    if (r->shared_color_image!=g->target.color.image)
        r->shared_color_layout_valid=0;
    r->shared_color_image=g->target.color.image;
    r->shared_color_view=(rf_vk_image_view)g->target.color.view;
    r->shared_color_enabled=1;
    return 0;
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
    VkPushConstantRange push = {VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112};
    struct rf_vk_shader_module_create_info shader_info = {0};
    rf_vk_shader_module shaders[2] = {0};
    VkPipelineShaderStageCreateInfo stages[2] = {{0}};
    VkVertexInputBindingDescription binding = {0, sizeof(struct rf_gpu_graphics_vertex), VK_VERTEX_INPUT_RATE_VERTEX};
    VkVertexInputAttributeDescription attrs[5] = {
        {0,0,VK_FORMAT_R32G32B32_SINT,0}, {1,0,VK_FORMAT_R32G32_SINT,12},
        {2,0,VK_FORMAT_R32G32B32_SINT,20}, {3,0,VK_FORMAT_R32G32B32_SINT,32},
        {4,0,VK_FORMAT_R32G32B32_SINT,44}};
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
    /* Normal mixed uses this attachment as a storage image shared by compute
     * Raster, indexed Draw, and Post. */
    attachments[0].format = VK_FORMAT_R8G8B8A8_UNORM;
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
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1; subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    deps[0].srcSubpass = VK_SUBPASS_EXTERNAL; deps[0].dstSubpass = 0;
    /* Keep the clear and load render passes compatible.  Normal mixed frames
     * enter the load pass after either a compute Raster segment or a transfer
     * bridge, while the standalone clear pass has no stronger predecessor.
     * Using the conservative mixed dependency for both passes is valid for
     * the clear case and lets one framebuffer/pipeline serve both passes. */
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT |
        VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT |
        VK_ACCESS_SHADER_WRITE_BIT;
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
    if (!compatible && g->api.CreateRenderPass(impl->device, &pass, NULL, &g->render_pass) != RF_VK_SUCCESS) return -1;
    if (!compatible) {
        for (unsigned i = 0; i < 2; ++i) {
            attachments[i].loadOp = VK_ATTACHMENT_LOAD_OP_LOAD;
            attachments[i].initialLayout = i ? VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL : VK_IMAGE_LAYOUT_GENERAL;
        }
        attachments[0].finalLayout = VK_IMAGE_LAYOUT_GENERAL;
        if (g->api.CreateRenderPass(impl->device, &pass, NULL, &g->load_pass) != RF_VK_SUCCESS) return -1;
    }
    layout.s_type = RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.set_layout_count = 1; layout.set_layouts = &g->set_layout;
    layout.push_constant_range_count = 1; layout.push_constant_ranges = &push;
    if (!compatible && impl->api.create_pipeline_layout(impl->device, &layout, NULL, &g->layout) != RF_VK_SUCCESS) return -1;
    shader_info.s_type = RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    for (unsigned i = 0; i < 2; ++i) {
        shader_info.code_size = i ? sizeof(rf_graphics_frag_spirv) : sizeof(rf_graphics_vert_spirv);
        shader_info.code = i ? rf_graphics_frag_spirv : rf_graphics_vert_spirv;
        if (compatible) {
            shader_info.code_size = i ? sizeof(rf_graphics_compat_frag_spirv) : sizeof(rf_graphics_compat_vert_spirv);
            shader_info.code = i ? rf_graphics_compat_frag_spirv : rf_graphics_compat_vert_spirv;
        }
        if (impl->api.create_shader_module(impl->device, &shader_info, NULL, &shaders[i]) != RF_VK_SUCCESS) goto done;
        stages[i].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        stages[i].stage = i ? VK_SHADER_STAGE_FRAGMENT_BIT : VK_SHADER_STAGE_VERTEX_BIT;
        stages[i].module = shaders[i]; stages[i].pName = "main";
    }
    vertex.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    vertex.vertexBindingDescriptionCount = compatible ? 0 : 1; vertex.pVertexBindingDescriptions = &binding;
    vertex.vertexAttributeDescriptionCount = compatible ? 0 : 5; vertex.pVertexAttributeDescriptions = attrs;
    assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    viewport.viewportCount = viewport.scissorCount = 1;
    raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    raster.polygonMode = VK_POLYGON_MODE_FILL; raster.lineWidth = 1.0f;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
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
    pipeline.stageCount = 2; pipeline.pStages = stages; pipeline.pVertexInputState = &vertex;
    pipeline.pInputAssemblyState = &assembly; pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster; pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth_state; pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic; pipeline.layout = g->layout; pipeline.renderPass = g->render_pass;
    pipeline.basePipelineIndex = -1;
    for (unsigned i = 0; i < 2; ++i) {
        raster.cullMode = i ? VK_CULL_MODE_NONE : VK_CULL_MODE_BACK_BIT;
        if (g->api.CreateGraphicsPipelines(impl->device, NULL, 1, &pipeline, NULL, &g->pipelines[i+compatible*2]) != RF_VK_SUCCESS) goto done;
    }
    result = 0;
done:
    for (unsigned i = 0; i < 2; ++i)
        if (shaders[i]) impl->api.destroy_shader_module(impl->device, shaders[i], NULL);
    return result;
}

static int gfx_bridge_create(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    struct rf_vk_descriptor_set_layout_binding bindings[2]={{0}};
    struct rf_vk_descriptor_set_layout_create_info set_layout={0};
    struct rf_vk_descriptor_pool_size size={RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER,2};
    struct rf_vk_descriptor_pool_create_info pool={0};
    struct rf_vk_descriptor_set_allocate_info set={0};
    struct rf_vk_pipeline_layout_create_info layout={0};
    VkPushConstantRange push={VK_SHADER_STAGE_COMPUTE_BIT,0,12};
    struct rf_vk_shader_module_create_info module={0};
    struct rf_vk_compute_pipeline_create_info pipeline={0};
    rf_vk_shader_module shader=NULL;
    for(unsigned i=0;i<2;++i) {
        bindings[i].binding=i;bindings[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
        bindings[i].descriptor_count=1;bindings[i].stage_flags=VK_SHADER_STAGE_COMPUTE_BIT;
    }
    set_layout.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    set_layout.binding_count=2;set_layout.bindings=bindings;
    if(impl->api.create_descriptor_set_layout(impl->device,&set_layout,NULL,&g->bridge_set_layout)!=RF_VK_SUCCESS)return -1;
    pool.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    pool.max_sets=1;pool.pool_size_count=1;pool.pool_sizes=&size;
    if(impl->api.create_descriptor_pool(impl->device,&pool,NULL,&g->bridge_pool)!=RF_VK_SUCCESS)return -1;
    set.s_type=RF_VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    set.descriptor_pool=g->bridge_pool;set.descriptor_set_count=1;set.set_layouts=&g->bridge_set_layout;
    if(impl->api.allocate_descriptor_sets(impl->device,&set,&g->bridge_set)!=RF_VK_SUCCESS)return -1;
    layout.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.set_layout_count=1;layout.set_layouts=&g->bridge_set_layout;
    layout.push_constant_range_count=1;layout.push_constant_ranges=&push;
    if(impl->api.create_pipeline_layout(impl->device,&layout,NULL,&g->bridge_layout)!=RF_VK_SUCCESS)return -1;
    module.s_type=RF_VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    module.code_size=sizeof(rf_graphics_bridge_comp_spirv);module.code=rf_graphics_bridge_comp_spirv;
    if(impl->api.create_shader_module(impl->device,&module,NULL,&shader)!=RF_VK_SUCCESS)return -1;
    pipeline.s_type=RF_VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
    pipeline.stage.s_type=RF_VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
    pipeline.stage.stage=VK_SHADER_STAGE_COMPUTE_BIT;pipeline.stage.module=shader;pipeline.stage.name="main";
    pipeline.layout=g->bridge_layout;pipeline.base_pipeline_index=-1;
    int result=impl->api.create_compute_pipelines(impl->device,NULL,1,&pipeline,NULL,&g->bridge_pipeline);
    impl->api.destroy_shader_module(impl->device,shader,NULL);
    return result==RF_VK_SUCCESS?0:-1;
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
    if (!impl || !impl->shader_int64_enabled || !(impl->queue_flags & VK_QUEUE_GRAPHICS_BIT) ||
        !impl->api.queue_wait_idle || !impl->api.reset_fences) return NULL;
    g = calloc(1, sizeof(*g));
    if (!g) return NULL;
    g->owner = impl;
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
        gfx_pipeline_create(g, 0) < 0 || gfx_pipeline_create(g, 1) < 0 ||
        gfx_bridge_create(g) < 0 || gfx_skin_create(g) < 0) goto fail;
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
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    struct rf_gpu_vulkan_raster_buffer staging[4] = {{0}};
    struct rf_gpu_vulkan_raster_buffer *target[4] = {
        &resource->vertices, &resource->indices, &resource->compat_indices, &resource->texture
    };
    const void *data[4];
    uint64_t bytes[4];
    uint32_t usage[4] = {
        VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
            RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT,
        VK_BUFFER_USAGE_INDEX_BUFFER_BIT,
        RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT
    };
    struct rf_vk_buffer_copy copy[4] = {{0}};
    struct rf_vk_memory_barrier barrier = {0};
    void *mapped = NULL;
    unsigned mapped_index = 0;
    uint32_t *expanded;
    int upload_failed = 1;
    if (!g || g->poisoned || resource->vertices.buffer || !indices ||
        !vertex_count || vertex_count > 65536 || !index_count || index_count > 196608 || index_count % 3 ||
        !rgb_texels || !texture_width || !texture_height || texture_width > 1024 || texture_height > 1024) return -1;
    for (uint32_t i = 0; i < index_count; ++i) if (indices[i] >= vertex_count) return -1;
    for (uint32_t i = 0; vertices && i < vertex_count; ++i) {
        for (unsigned j = 0; j < 3; ++j)
            if (vertices[i].position[j] < -32767 || vertices[i].position[j] > 32767) return -1;
        for (unsigned j = 0; j < 9; ++j)
            if (vertices[i].normals[j] < -32767 || vertices[i].normals[j] > 32767) return -1;
        for (unsigned j = 0; j < 2; ++j)
            if (vertices[i].uv[j] < 0 || vertices[i].uv[j] > 65535) return -1;
    }
    /* All three corners must describe the same source primitive. */
    for (uint32_t i = 0; vertices && i < index_count; i += 3)
        if (memcmp(vertices[indices[i]].normals, vertices[indices[i+1]].normals, 36) ||
            memcmp(vertices[indices[i]].normals, vertices[indices[i+2]].normals, 36)) return -1;
    expanded = malloc((size_t)index_count*2*sizeof(*expanded));
    if (!expanded) return -1;
    for (uint32_t i = 0; i < index_count*2; ++i) expanded[i] = i;
    data[0]=vertices; data[1]=indices; data[2]=expanded; data[3]=rgb_texels;
    bytes[0]=(uint64_t)vertex_count*sizeof(*vertices);
    bytes[1]=(uint64_t)index_count*4;
    bytes[2]=(uint64_t)index_count*8;
    bytes[3]=(uint64_t)texture_width*texture_height*4;
    /* Integrated GPUs commonly expose device-local host-visible memory.  Use
     * it for per-frame geometry so upload does not submit behind the previous
     * frame and synchronously wait for the graphics queue.  Discrete devices
     * without that memory type retain the staging-copy path below. */
    for (unsigned n=0;n<4;++n) {
        if (raster_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT | RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,target[n])<0) {
            for (unsigned k=0;k<=n;++k) raster_buffer_destroy(impl,target[k]);
            break;
        }
        if (!data[n]) {
            if (n==3) { upload_failed=0; goto upload_done; }
            continue;
        }
        if (impl->api.map_memory(impl->device,target[n]->memory,0,
                target[n]->allocation_size,0,&mapped)!=RF_VK_SUCCESS) {
            for (unsigned k=0;k<=n;++k) raster_buffer_destroy(impl,target[k]);
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
                for (unsigned k=0;k<=n;++k) raster_buffer_destroy(impl,target[k]);
                break;
            }
        }
        impl->api.unmap_memory(impl->device,target[n]->memory); mapped=NULL;
        if (n==3) { upload_failed=0; goto upload_done; }
    }
    for (unsigned n=0;n<4;++n) {
        if (!data[n]) {
            if (raster_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT,
                    target[n])<0) goto upload_done;
            continue;
        }
        if (raster_buffer_create(impl,bytes[n],RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
                RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                &staging[n])<0 ||
            raster_buffer_create(impl,bytes[n],usage[n] | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
    for (unsigned n=0;n<4;++n) if (data[n])
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
    for (unsigned n=0;n<4;++n) raster_buffer_destroy(impl,&staging[n]);
    free(expanded);
    if (upload_failed) {
        g->poisoned = 1;
        return -1;
    }
    resource->index_count = index_count; resource->texture_width = texture_width; resource->texture_height = texture_height;
    for (uint32_t i = 0; vertices && i < vertex_count; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (abs(vertices[i].position[j]) > resource->position_bound[j])
                resource->position_bound[j] = abs(vertices[i].position[j]);
    g->stats.mesh_upload_bytes += (vertices ?
        (uint64_t)vertex_count*sizeof(*vertices) : 0) + (uint64_t)index_count*12;
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
    struct rf_gpu_vulkan_raster_buffer readback={0};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_mapped_memory_range range={0};
    const struct rf_gpu_graphics_vertex *actual;
    void *mapped=NULL;
    uint64_t bytes;
    int result=-1;
    if (!g || g->poisoned || !resource || resource->owner!=g || !reference ||
        !vertex_count || !position_mismatches || !normal_mismatches ||
        !uv_mismatches || !max_position_delta || !max_normal_delta) return -1;
    bytes=(uint64_t)vertex_count*sizeof(*reference);
    if (bytes>resource->vertices.size) return -1;
    impl=g->owner;
    if (raster_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
    raster_buffer_destroy(impl,&readback);
    return result;
}

struct rf_gpu_graphics_resource *rf_gpu_graphics_resource_create(
    struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height)
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
    resource->next = g->resources;
    g->resources = resource;
    if (gfx_resource_upload(g, resource, vertices, vertex_count, indices,
            index_count, rgb_texels, texture_width, texture_height) < 0) {
        rf_gpu_graphics_resource_destroy(g, resource);
        return NULL;
    }
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

static int gfx_upload_skin_input(struct rf_gpu_graphics *g,
    struct rf_gpu_vulkan_raster_buffer *target,const uint32_t *words,uint64_t bytes)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    struct rf_gpu_vulkan_raster_buffer staging={0};
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    void *mapped=NULL;
    if (!words || !bytes) return -1;
    if (raster_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|
            RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT|RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,target)==0) {
        if (impl->api.map_memory(impl->device,target->memory,0,target->allocation_size,0,&mapped)!=RF_VK_SUCCESS)
            { raster_buffer_destroy(impl,target); return -1; }
        memcpy(mapped,words,(size_t)bytes);
        if (!(target->memory_flags&RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
            struct rf_vk_mapped_memory_range range={0};
            range.s_type=RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE; range.memory=target->memory;
            range.size=target->allocation_size;
            if (impl->api.flush_mapped_memory_ranges(impl->device,1,&range)!=RF_VK_SUCCESS) {
                impl->api.unmap_memory(impl->device,target->memory);
                raster_buffer_destroy(impl,target); return -1;
            }
        }
        impl->api.unmap_memory(impl->device,target->memory);
        return 0;
    }
    if (raster_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT,RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,&staging)<0 ||
        raster_buffer_create(impl,bytes,RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT|RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
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
    raster_buffer_destroy(impl,&staging); return 0;
fail:
    if (mapped) impl->api.unmap_memory(impl->device,staging.memory);
    raster_buffer_destroy(impl,&staging); raster_buffer_destroy(impl,target); return -1;
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
    g->stats.mesh_upload_bytes+=(uint64_t)(bind_word_count+palette_word_count)*4;
    return r;
fail:
    rf_gpu_graphics_resource_destroy(g,r); return NULL;
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
    if (!g || !resource || resource->owner != g) return -1;
    /* Successful submissions already waited. A failed wait may leave work. */
    if (g->poisoned) g->owner->api.queue_wait_idle(g->owner->queue);
    for (link = &g->resources; *link != resource; link = &(*link)->next)
        if (!*link) return -1;
    *link = resource->next;
    if (g->resource == resource) g->resource = NULL;
    if (resource->descriptor_pool)
        g->owner->api.destroy_descriptor_pool(g->owner->device, resource->descriptor_pool, NULL);
    if (resource->shared_binding.pool)
        g->owner->api.destroy_descriptor_pool(g->owner->device,
            resource->shared_binding.pool,NULL);
    if (resource->skin_descriptor_pool)
        g->owner->api.destroy_descriptor_pool(g->owner->device,resource->skin_descriptor_pool,NULL);
    raster_buffer_destroy(g->owner,&resource->skin_bind);
    raster_buffer_destroy(g->owner,&resource->skin_palette);
    raster_buffer_destroy(g->owner, &resource->vertices);
    raster_buffer_destroy(g->owner, &resource->indices);
    raster_buffer_destroy(g->owner, &resource->compat_indices);
    raster_buffer_destroy(g->owner, &resource->texture);
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
    if (!d->index_count || d->index_count % 3 || d->first_index % 3 ||
        d->first_index > g->resource->index_count || d->index_count > g->resource->index_count-d->first_index ||
        d->integer_depth > 1 || d->double_sided > 1 || d->material[1] > 256 ||
        d->material[2] > 1 || d->material[3] > 1 ||
        d->camera[3] || d->texture[2] || d->texture[3] ||
        d->translation_scale[3] < 1 || d->translation_scale[3] > 8000 ||
        d->rotation[2] < -32767 || d->rotation[2] > 32767 ||
        d->rotation[3] < 0 || d->rotation[3] > 1 ||
        d->projection[0] != (int)g->target.width || d->projection[1] != (int)g->target.height ||
        d->projection[2] != 64 || d->projection[3] != (int)g->target.width*3/4 ||
        d->texture[0] != (int)g->resource->texture_width || d->texture[1] != (int)g->resource->texture_height) return 0;
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
    if (validate_resource_bounds && d->integer_depth && !d->material[3]) {
        /* O(1) conservative mesh bounds, before recording any draw. With
         * projected coordinates in [-16384,16384], edge*UV sums fit int64.
         * Deliberately conservative hosted-proof limit, not silent clipping. */
        int64_t rotated_x=(int64_t)g->resource->position_bound[0]*abs(d->rotation[1]) +
            (int64_t)g->resource->position_bound[2]*abs(d->rotation[0]);
        int64_t rotated_z=(int64_t)g->resource->position_bound[2]*abs(d->rotation[1]) +
            (int64_t)g->resource->position_bound[0]*abs(d->rotation[0]);
        if (rotated_x>INT_MAX || rotated_z>INT_MAX) return 0;
        int64_t x=rotated_x/1024;
        int64_t z=rotated_z/1024;
        int64_t y = (int64_t)g->resource->position_bound[1]+abs(d->rotation[2]);
        if (x*d->translation_scale[3] > INT_MAX ||
            y*d->translation_scale[3] > INT_MAX ||
            z*d->translation_scale[3] > INT_MAX) return 0;
        x=x*d->translation_scale[3]/1000;
        y=y*d->translation_scale[3]/1000;
        z=z*d->translation_scale[3]/1000;
        /* A distant mesh cannot reach the near plane. Bound its closest
         * possible depth before dividing the screen bounds; retain near=64
         * whenever the box could cross that plane. */
        int64_t center_x=d->translation_scale[0]-d->camera[0];
        int64_t center_y=d->translation_scale[1]-d->camera[1];
        int64_t center_z=d->translation_scale[2]-d->camera[2];
        int64_t axial=(center_x*d->view[0]+center_z*d->view[1])/1024;
        int64_t depth=(center_y*d->view[2]+axial*d->view[3])/1024;
        int64_t axial_radius=(x*abs(d->view[0])+z*abs(d->view[1])+1023)/1024;
        int64_t depth_radius=(y*abs(d->view[2])+axial_radius*abs(d->view[3])+1023)/1024;
        int64_t min_depth=depth-depth_radius-4;
        if (min_depth<64) min_depth=64;
        x+=abs(d->translation_scale[0]-d->camera[0]);
        y+=abs(d->translation_scale[1]-d->camera[1]);
        z+=abs(d->translation_scale[2]-d->camera[2]);
        int64_t vx=(x*abs(d->view[1])+z*abs(d->view[0]))/1024;
        int64_t vz=(x*abs(d->view[0])+z*abs(d->view[1]))/1024;
        int64_t vy=(y*abs(d->view[3])+vz*abs(d->view[2]))/1024;
        if (vx*d->projection[3]/min_depth+d->projection[0]/2 > 16384 ||
            vy*d->projection[3]/min_depth+d->projection[1]/2 > 16384) return 0;
    }
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
     * instances, so its aggregate position_bound is not a valid bound for one
     * Draw. The producer has already range-checked each bind vertex and the
     * common validator still enforces transforms, projection and index range. */
    return g && !g->poisoned && g->resource && g->target.framebuffer && draw &&
        gfx_draw_valid(g, draw, 0) ? 0 : -1;
}

static void gfx_bridge_bind(struct rf_gpu_graphics *g, uint64_t range)
{
    struct rf_vk_descriptor_buffer_info buffers[2]={{0}};
    struct rf_vk_write_descriptor_set writes[2]={{0}};
    buffers[0].buffer=g->target.bridge_transfer.buffer;buffers[0].range=range;
    buffers[1].buffer=g->target.bridge_raster.buffer;buffers[1].range=range;
    if (g->bridge_bound_buffers[0]==buffers[0].buffer &&
        g->bridge_bound_buffers[1]==buffers[1].buffer &&
        g->bridge_bound_ranges[0]==buffers[0].range &&
        g->bridge_bound_ranges[1]==buffers[1].range) return;
    for(unsigned i=0;i<2;++i) {
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dst_set=g->bridge_set;writes[i].dst_binding=i;writes[i].descriptor_count=1;
        writes[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].buffer_info=&buffers[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device,2,writes,0,NULL);
    for(unsigned i=0;i<2;++i) {
        g->bridge_bound_buffers[i]=buffers[i].buffer;
        g->bridge_bound_ranges[i]=buffers[i].range;
    }
}

/* mode: 0 diagnostic roundtrip, 1 raster -> attachments, 2 attachments -> raster. */
static int gfx_bridge(struct rf_gpu_graphics *g, struct rf_gpu_vulkan_raster *r,
                      int mode, int append_to_draw, int record_only)
{
    uint32_t timestamp=UINT32_MAX;
    double bridge_start=now_ms();
    struct rf_gpu_vulkan_impl *impl=g->owner;
    uint64_t bytes=(uint64_t)g->target.width*g->target.height*4;
    struct rf_vk_memory_barrier barrier={0};
    VkBufferImageCopy copy={0};
    struct rf_vk_image_memory_barrier images[2]={{0}};
    struct rf_vk_buffer_image_copy import={0};
    uint32_t params[3]={g->target.width,g->target.height,0};
    gfx_bridge_bind(g,mode?bytes:bytes*2);
    if(!append_to_draw && gfx_begin(g)<0)return -1;
    if (r) timestamp=timestamp_begin(r,g->cmd,
        mode==1?RF_GPU_TS_IMPORT:RF_GPU_TS_EXPORT);
    /* Prior render pass stores both images in TRANSFER_SRC. Prior bridge
     * buffers may still have shader/transfer readers; order their reuse. */
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT|VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT|VK_ACCESS_TRANSFER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT|VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    if (mode == 1) {
        struct rf_vk_buffer_copy region={0};
        region.size=bytes;
        impl->api.cmd_copy_buffer(g->cmd,r->depth.buffer,g->target.bridge_raster.buffer,1,&region);
    } else if (mode == 2) {
        copy.imageSubresource.layerCount=1;
        copy.imageExtent.width=g->target.width;copy.imageExtent.height=g->target.height;copy.imageExtent.depth=1;
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        g->api.CmdCopyImageToBuffer(g->cmd,g->target.depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,g->target.bridge_transfer.buffer,1,&copy);
    } else {
        copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_COLOR_BIT;copy.imageSubresource.layerCount=1;
        copy.imageExtent.width=g->target.width;copy.imageExtent.height=g->target.height;copy.imageExtent.depth=1;
        g->api.CmdCopyImageToBuffer(g->cmd,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,g->target.bridge_transfer.buffer,1,&copy);
        copy.bufferOffset=bytes;copy.imageSubresource.aspectMask=VK_IMAGE_ASPECT_DEPTH_BIT;
        g->api.CmdCopyImageToBuffer(g->cmd,g->target.depth.image,VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL,g->target.bridge_transfer.buffer,1,&copy);
    }
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
    impl->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->bridge_pipeline);
    impl->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->bridge_layout,0,1,&g->bridge_set,0,NULL);
    params[2]=mode==1?3:(mode==2?2:0);
    g->api.CmdPushConstants(g->cmd,g->bridge_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,12,params);
    impl->api.cmd_dispatch(g->cmd,(params[0]+15)/16,(params[1]+15)/16,1);
    if (mode == 0) {
        barrier.src_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,0,1,&barrier,0,NULL,0,NULL);
        params[2]=1;
        g->api.CmdPushConstants(g->cmd,g->bridge_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,12,params);
        impl->api.cmd_dispatch(g->cmd,(params[0]+15)/16,(params[1]+15)/16,1);
    }
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_TRANSFER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,1,&barrier,0,NULL,0,NULL);
    if (mode == 2) {
        struct rf_vk_buffer_copy region={0};
        region.size=bytes;
        impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_raster.buffer,r->depth.buffer,1,&region);
    } else {
        unsigned first_image=(mode==1 && r && r->shared_color_enabled)?1:0;
        for(unsigned i=first_image;i<2;++i) {
            images[i].s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            images[i].old_layout=(mode==1 && !g->target.valid)?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;images[i].new_layout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            images[i].src_access_mask=VK_ACCESS_TRANSFER_READ_BIT;images[i].dst_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
            images[i].src_queue_family_index=images[i].dst_queue_family_index=UINT32_MAX;
            images[i].image=i?g->target.depth.image:g->target.color.image;
            images[i].subresource_range.aspect_mask=i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
            images[i].subresource_range.level_count=images[i].subresource_range.layer_count=1;
        }
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2-first_image,images+first_image);
        import.image_subresource.aspect_mask=VK_IMAGE_ASPECT_COLOR_BIT;
        import.image_subresource.layer_count=1;
        import.image_extent.width=params[0];import.image_extent.height=params[1];import.image_extent.depth=1;
        if (mode==1 && !(r && r->shared_color_enabled))
            impl->api.cmd_copy_buffer_to_image(g->cmd,r->color.buffer,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&import);
        else if (mode!=1)
            impl->api.cmd_copy_buffer_to_image(g->cmd,g->target.bridge_transfer.buffer,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&import);
        import.image_subresource.aspect_mask=VK_IMAGE_ASPECT_DEPTH_BIT;
        import.buffer_offset=mode==1?0:bytes;
        impl->api.cmd_copy_buffer_to_image(g->cmd,g->target.bridge_transfer.buffer,g->target.depth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&import);
    }
    /* Mixed records this bridge into Raster's frame command buffer. Hosted
     * diagnostics still own and synchronously submit the graphics buffer. */
    timestamp_end(r,g->cmd,timestamp);
    if(!record_only && gfx_submit(g,1,RF_GPU_SUBMIT_BRIDGE)<0)return -1;
    if(mode==0)g->stats.bridge_roundtrips++;
    else g->stats.raster_bridge_transfers++;
    g->stats.bridge_transfer_bytes+=bytes*(mode?(r&&r->shared_color_enabled?2:3):4);
    g->stats.bridge_wall_ms+=now_ms()-bridge_start;
    g->target.bridge_valid=1;
    return 0;
}

static int gfx_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t pixel_capacity,int load,
    struct rf_gpu_vulkan_raster *r,
    const struct rf_gpu_graphics_batch_item *items)
{
    struct rf_gpu_vulkan_impl *impl;
    VkClearValue clear[2];
    VkRenderPassBeginInfo begin = {0};
    VkViewport viewport = {0};
    VkRect2D scissor = {{0,0},{0,0}};
    struct rf_vk_memory_barrier barrier = {0};
    VkBufferImageCopy copy = {0};
    uint64_t offset = 0, bytes;
    void *mapped = NULL;
    if (!g || g->poisoned || (!items && (!g->resource || !g->resource->index_count)) || !g->target.framebuffer ||
        (!draws && !items) || !count || count > 65536 ||
        (!r && (!rgba || !depth || (uint64_t)g->target.width*g->target.height > pixel_capacity))) return -1;
    {
        struct rf_gpu_graphics_resource *saved = g->resource;
        int valid = 1;
        for (uint32_t i = 0; i < count; ++i) {
            const struct rf_gpu_graphics_draw *d = items ? &items[i].draw : &draws[i];
            if (items) g->resource = items[i].resource;
            if (!gfx_resource_compatible(g,g->resource) ||
                !gfx_draw_valid(g, d, !g->resource->frame_dynamic) ||
                (r && !d->integer_depth)) { valid = 0; break; }
        }
        g->resource = saved;
        if (!valid) return -1;
    }
    if(r) {
        if(r->owner!=g->owner || !r->segment_valid || !r->frame_recording ||
           !r->segment_graphics_compatible ||
           r->width!=g->target.width || r->height!=g->target.height)return -1;
    } else if(load && !g->target.valid)return -1;
    impl = g->owner; bytes = (uint64_t)g->target.width*g->target.height*4;
    g->target.bridge_valid=0;
    if(r)r->segment_valid=0;
    if(load && gfx_bridge(g,r,r?1:0,r!=NULL,r!=NULL)<0){g->poisoned=1;g->target.valid=0;return -1;}
    g->target.valid=0;
    memset(clear, 0, sizeof(clear));
    if (!r && gfx_begin(g) < 0) { g->poisoned=1; return -1; }
    clear[0].color.float32[3] = 1.0f;
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = load?g->load_pass:g->render_pass; begin.framebuffer = g->target.framebuffer;
    begin.renderArea.extent.width = g->target.width; begin.renderArea.extent.height = g->target.height;
    begin.clearValueCount = 2; begin.pClearValues = clear;
    g->api.CmdBeginRenderPass(g->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = (float)g->target.width; viewport.height = (float)g->target.height; viewport.maxDepth = 1.0f;
    scissor.extent = begin.renderArea.extent;
    g->api.CmdSetViewport(g->cmd, 0, 1, &viewport); g->api.CmdSetScissor(g->cmd, 0, 1, &scissor);
    uint32_t draw_timestamp=timestamp_begin(r,g->cmd,RF_GPU_TS_DRAW);
    for (uint32_t i = 0; i < count; ++i) {
        const struct rf_gpu_graphics_draw *d = items ? &items[i].draw : &draws[i];
        struct rf_gpu_graphics_resource *resource = items ? items[i].resource : g->resource;
        uint32_t factor = d->integer_depth ? 2 : 1;
        g->api.CmdBindVertexBuffers(g->cmd, 0, 1, &resource->vertices.buffer, &offset);
        g->api.CmdBindIndexBuffer(g->cmd, d->integer_depth ? resource->compat_indices.buffer : resource->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        rf_vk_descriptor_set descriptor=gfx_resource_descriptor(g,resource);
        if(!descriptor){g->poisoned=1;return -1;}
        impl->api.cmd_bind_descriptor_sets(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS,
            g->layout, 0, 1, &descriptor, 0, NULL);
        impl->api.cmd_bind_pipeline(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipelines[d->double_sided+d->integer_depth*2]);
        g->api.CmdPushConstants(g->cmd, g->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112, d);
        g->api.CmdDrawIndexed(g->cmd, d->index_count*factor, 1, d->first_index*factor, 0, 0);
    }
    g->api.CmdEndRenderPass(g->cmd);
    timestamp_end(r,g->cmd,draw_timestamp);
    if(r) {
        /* The render pass ends in TRANSFER_SRC with an external transfer
         * dependency. Record export after it in this command buffer so the
         * Draw and bridge share one queue submit and fence. */
        if(gfx_bridge(g,r,2,1,1)<0){g->poisoned=1;return -1;}
        r->segment_valid=1;
    } else {
        struct rf_vk_buffer_copy color_readback={0},depth_readback={0};
        uint32_t params[3]={g->target.width,g->target.height,0};
        gfx_bridge_bind(g,bytes*2);
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent.width = g->target.width; copy.imageExtent.height = g->target.height; copy.imageExtent.depth = 1;
        g->api.CmdCopyImageToBuffer(g->cmd, g->target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->target.bridge_transfer.buffer, 1, &copy);
        copy.bufferOffset = bytes; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        g->api.CmdCopyImageToBuffer(g->cmd, g->target.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->target.bridge_transfer.buffer, 1, &copy);
        barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
        barrier.dst_access_mask=VK_ACCESS_SHADER_READ_BIT|VK_ACCESS_SHADER_WRITE_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,
            0,1,&barrier,0,NULL,0,NULL);
        impl->api.cmd_bind_pipeline(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->bridge_pipeline);
        impl->api.cmd_bind_descriptor_sets(g->cmd,RF_VK_PIPELINE_BIND_POINT_COMPUTE,g->bridge_layout,0,1,&g->bridge_set,0,NULL);
        g->api.CmdPushConstants(g->cmd,g->bridge_layout,VK_SHADER_STAGE_COMPUTE_BIT,0,12,params);
        impl->api.cmd_dispatch(g->cmd,(params[0]+15)/16,(params[1]+15)/16,1);
        barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_TRANSFER_READ_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
            0,1,&barrier,0,NULL,0,NULL);
        color_readback.size=bytes;
        depth_readback.src_offset=bytes;depth_readback.dst_offset=bytes;depth_readback.size=bytes;
        impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_raster.buffer,g->target.readback.buffer,1,&color_readback);
        impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_transfer.buffer,g->target.readback.buffer,1,&depth_readback);
        barrier.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dst_access_mask = VK_ACCESS_HOST_READ_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        if (gfx_submit(g, 1,RF_GPU_SUBMIT_DRAW) < 0) return -1;
        if (impl->api.map_memory(impl->device, g->target.readback.memory, 0,
                g->target.readback.allocation_size, 0, &mapped) != RF_VK_SUCCESS) return -1;
        if (raster_invalidate(impl, &g->target.readback) < 0) {
            impl->api.unmap_memory(impl->device, g->target.readback.memory); return -1;
        }
        memcpy(rgba, mapped, (size_t)bytes);
        memcpy(depth, (unsigned char *)mapped+bytes, (size_t)bytes);
        impl->api.unmap_memory(impl->device, g->target.readback.memory);
    }
    g->stats.instance_upload_bytes += (uint64_t)count*112;
    g->stats.indexed_draws += count; g->stats.frames++;
    g->target.valid=1;
    return 0;
}

int rf_gpu_graphics_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t pixel_capacity)
{ return gfx_render(g,draws,count,rgba,depth,pixel_capacity,0,NULL,NULL); }

int rf_gpu_graphics_continue(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t pixel_capacity)
{ return gfx_render(g,draws,count,rgba,depth,pixel_capacity,1,NULL,NULL); }

int rf_gpu_graphics_raster_draw(struct rf_gpu_graphics *g, void *raster,
    const struct rf_gpu_graphics_draw *draws, uint32_t count)
{
    rf_vk_command_buffer saved;
    int result;
    if(!raster || rf_gpu_graphics_share_color(g,raster)<0)return -1;
    saved=g->cmd;g->cmd=((struct rf_gpu_vulkan_raster *)raster)->command_buffer;
    result=gfx_render(g,draws,count,NULL,NULL,0,1,raster,NULL);
    g->cmd=saved;
    return result;
}

int rf_gpu_graphics_raster_batch(struct rf_gpu_graphics *g, void *raster,
    const struct rf_gpu_graphics_batch_item *items, uint32_t count)
{
    rf_vk_command_buffer saved;
    int result;
    if (!raster || !items || rf_gpu_graphics_share_color(g,raster)<0) return -1;
    saved=g->cmd;g->cmd=((struct rf_gpu_vulkan_raster *)raster)->command_buffer;
    result=gfx_render(g,NULL,count,NULL,NULL,0,1,raster,items);
    g->cmd=saved;
    return result;
}

int rf_gpu_graphics_read_bridge(struct rf_gpu_graphics *g,
    uint32_t *argb,int32_t *inverse_depth,uint32_t pixel_capacity)
{
    struct rf_vk_buffer_copy copy={0};
    struct rf_vk_memory_barrier barrier={0};
    void *mapped=NULL;
    if(!g || g->poisoned || !g->target.valid || !g->target.bridge_valid || !argb || !inverse_depth ||
        (uint64_t)g->target.width*g->target.height>pixel_capacity)return -1;
    uint64_t bytes=(uint64_t)g->target.width*g->target.height*4;
    struct rf_gpu_vulkan_impl *impl=g->owner;
    if(gfx_begin(g)<0)return -1;
    barrier.s_type=RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask=VK_ACCESS_SHADER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_TRANSFER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    copy.size=bytes*2;
    impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_raster.buffer,g->target.readback.buffer,1,&copy);
    barrier.src_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;barrier.dst_access_mask=VK_ACCESS_HOST_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_HOST_BIT,
        0,1,&barrier,0,NULL,0,NULL);
    if(gfx_submit(g,1,RF_GPU_SUBMIT_READBACK)<0 || impl->api.map_memory(impl->device,g->target.readback.memory,0,
        g->target.readback.allocation_size,0,&mapped)!=RF_VK_SUCCESS)return -1;
    int result=raster_invalidate(impl,&g->target.readback);
    if(result==0){memcpy(argb,mapped,(size_t)bytes);memcpy(inverse_depth,(unsigned char *)mapped+bytes,(size_t)bytes);}
    impl->api.unmap_memory(impl->device,g->target.readback.memory);
    return result;
}

void rf_gpu_graphics_get_stats(const struct rf_gpu_graphics *g, struct rf_gpu_graphics_stats *stats)
{
    if (g && stats) *stats = g->stats;
}
#undef RF_GFX_FUNCTIONS
