/* HG-2A/2B diagnostic executor, sharing the compute backend's device/queue.
 * Included after the buffer helpers; no Vulkan objects escape this owner. */
#include "rf_vulkan_graphics_min.h"
#include "rf_gpu_graphics.h"
#include "rf_gpu_graphics_spirv.inc"

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
struct rf_gpu_graphics {
    struct rf_gpu_vulkan_impl *owner;
    struct rf_graphics_api api;
    struct rf_graphics_target target;
    struct rf_gpu_vulkan_raster_buffer vertices, indices, compat_indices, texture;
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
    rf_vk_pipeline_layout bridge_layout;
    rf_vk_pipeline bridge_pipeline;
    rf_vk_command_pool pool;
    rf_vk_command_buffer cmd;
    rf_vk_fence fence;
    uint32_t index_count, texture_width, texture_height;
    int position_bound[3];
    int poisoned;
    struct rf_gpu_graphics_stats stats;
};

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

static int gfx_submit(struct rf_gpu_graphics *g)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    struct rf_vk_submit_info submit = {0};
    if (impl->api.end_command_buffer(g->cmd) != RF_VK_SUCCESS ||
        impl->api.reset_fences(impl->device, 1, &g->fence) != RF_VK_SUCCESS)
        goto failed;
    submit.s_type = RF_VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.command_buffer_count = 1; submit.command_buffers = &g->cmd;
    if (impl->api.queue_submit(impl->queue, 1, &submit, g->fence) != RF_VK_SUCCESS ||
        impl->api.wait_for_fences(impl->device, 1, &g->fence,
                                  RF_VK_TRUE, UINT64_MAX) != RF_VK_SUCCESS)
        goto failed;
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
    if (g->layout) impl->api.destroy_pipeline_layout(impl->device, g->layout, NULL);
    if (g->descriptor_pool) impl->api.destroy_descriptor_pool(impl->device, g->descriptor_pool, NULL);
    if (g->set_layout) impl->api.destroy_descriptor_set_layout(impl->device, g->set_layout, NULL);
    raster_buffer_destroy(impl, &g->vertices);
    raster_buffer_destroy(impl, &g->indices);
    raster_buffer_destroy(impl, &g->compat_indices);
    raster_buffer_destroy(impl, &g->texture);
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
    VkFramebufferCreateInfo fb = {0};
    VkImageView views[2];
    if (!g || g->poisoned || !width || !height || width > 4096 || height > 4096) return -1;
    if (g->target.width == width && g->target.height == height) return 0;
    t.width = width; t.height = height;
    if (gfx_image_create(g, &t.color, width, height, VK_FORMAT_R8G8B8A8_UNORM,
            VK_IMAGE_ASPECT_COLOR_BIT, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT) < 0 ||
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
    gfx_target_destroy(g, &g->target); g->target = t;
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
    deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
    deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
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
            attachments[i].initialLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
        }
        deps[0].srcStageMask = VK_PIPELINE_STAGE_TRANSFER_BIT;
        deps[0].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        deps[0].dstAccessMask |= VK_ACCESS_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT;
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
        gfx_pipeline_create(g, 0) < 0 || gfx_pipeline_create(g, 1) < 0 || gfx_bridge_create(g) < 0) goto fail;
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

/* Device-local VB/IB/texels; host staging is discarded after fence completion. */
static int gfx_upload_buffer(struct rf_gpu_graphics *g, const void *data, uint64_t bytes,
    uint32_t usage, struct rf_gpu_vulkan_raster_buffer *out)
{
    struct rf_gpu_vulkan_impl *impl = g->owner;
    struct rf_gpu_vulkan_raster_buffer staging = {0};
    struct rf_vk_buffer_copy copy = {0,0,bytes};
    struct rf_vk_memory_barrier barrier = {0};
    void *mapped = NULL;
    int result = -1;
    if (raster_buffer_create(impl, bytes, RF_VK_BUFFER_USAGE_TRANSFER_SRC_BIT,
            RF_VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT, RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, &staging) < 0 ||
        raster_buffer_create(impl, bytes, usage | RF_VK_BUFFER_USAGE_TRANSFER_DST_BIT,
            RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, RF_VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, out) < 0) goto done;
    if (impl->api.map_memory(impl->device, staging.memory, 0, staging.allocation_size, 0, &mapped) != RF_VK_SUCCESS) goto done;
    memcpy(mapped, data, (size_t)bytes);
    if (!(staging.memory_flags & RF_VK_MEMORY_PROPERTY_HOST_COHERENT_BIT)) {
        struct rf_vk_mapped_memory_range range = {0};
        range.s_type = RF_VK_STRUCTURE_TYPE_MAPPED_MEMORY_RANGE;
        range.memory = staging.memory; range.size = staging.allocation_size;
        if (impl->api.flush_mapped_memory_ranges(impl->device, 1, &range) != RF_VK_SUCCESS) goto done;
    }
    impl->api.unmap_memory(impl->device, staging.memory); mapped = NULL;
    if (gfx_begin(g) < 0) goto done;
    impl->api.cmd_copy_buffer(g->cmd, staging.buffer, out->buffer, 1, &copy);
    barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
    barrier.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT;
    barrier.dst_access_mask = VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT | VK_ACCESS_INDEX_READ_BIT | VK_ACCESS_SHADER_READ_BIT;
    impl->api.cmd_pipeline_barrier(g->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT,
        VK_PIPELINE_STAGE_VERTEX_INPUT_BIT | VK_PIPELINE_STAGE_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT,
        0, 1, &barrier, 0, NULL, 0, NULL);
    result = gfx_submit(g);
done:
    if (mapped) impl->api.unmap_memory(impl->device, staging.memory);
    if (g->poisoned) impl->api.queue_wait_idle(impl->queue);
    raster_buffer_destroy(impl, &staging);
    return result;
}

int rf_gpu_graphics_upload(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_vertex *vertices, uint32_t vertex_count,
    const uint32_t *indices, uint32_t index_count,
    const uint32_t *rgb_texels, uint32_t texture_width, uint32_t texture_height)
{
    struct rf_vk_descriptor_buffer_info buffer[3] = {{0}};
    struct rf_vk_write_descriptor_set write[3] = {{0}};
    uint32_t *expanded;
    if (!g || g->poisoned || g->vertices.buffer || !vertices || !indices ||
        !vertex_count || vertex_count > 65536 || !index_count || index_count > 196608 || index_count % 3 ||
        !rgb_texels || !texture_width || !texture_height || texture_width > 1024 || texture_height > 1024) return -1;
    for (uint32_t i = 0; i < index_count; ++i) if (indices[i] >= vertex_count) return -1;
    for (uint32_t i = 0; i < vertex_count; ++i) {
        for (unsigned j = 0; j < 3; ++j)
            if (vertices[i].position[j] < -32767 || vertices[i].position[j] > 32767) return -1;
        for (unsigned j = 0; j < 9; ++j)
            if (vertices[i].normals[j] < -32767 || vertices[i].normals[j] > 32767) return -1;
        for (unsigned j = 0; j < 2; ++j)
            if (vertices[i].uv[j] < 0 || vertices[i].uv[j] > 65535) return -1;
    }
    /* All three corners must describe the same source primitive. */
    for (uint32_t i = 0; i < index_count; i += 3)
        if (memcmp(vertices[indices[i]].normals, vertices[indices[i+1]].normals, 36) ||
            memcmp(vertices[indices[i]].normals, vertices[indices[i+2]].normals, 36)) return -1;
    expanded = malloc((size_t)index_count*2*sizeof(*expanded));
    if (!expanded) return -1;
    for (uint32_t i = 0; i < index_count*2; ++i) expanded[i] = i;
    int upload_failed =
        gfx_upload_buffer(g, vertices, (uint64_t)vertex_count*sizeof(*vertices), VK_BUFFER_USAGE_VERTEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g->vertices) < 0 ||
        gfx_upload_buffer(g, indices, (uint64_t)index_count*4, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g->indices) < 0 ||
        gfx_upload_buffer(g, expanded, (uint64_t)index_count*8, VK_BUFFER_USAGE_INDEX_BUFFER_BIT, &g->compat_indices) < 0 ||
        gfx_upload_buffer(g, rgb_texels, (uint64_t)texture_width*texture_height*4, RF_VK_BUFFER_USAGE_STORAGE_BUFFER_BIT, &g->texture) < 0;
    free(expanded);
    if (upload_failed) {
        g->poisoned = 1;
        return -1;
    }
    g->index_count = index_count; g->texture_width = texture_width; g->texture_height = texture_height;
    for (uint32_t i = 0; i < vertex_count; ++i)
        for (unsigned j = 0; j < 3; ++j)
            if (abs(vertices[i].position[j]) > g->position_bound[j])
                g->position_bound[j] = abs(vertices[i].position[j]);
    g->stats.mesh_upload_bytes += (uint64_t)vertex_count*sizeof(*vertices) + (uint64_t)index_count*12;
    g->stats.texture_upload_bytes += (uint64_t)texture_width*texture_height*4;
    buffer[0].buffer = g->texture.buffer; buffer[0].range = g->texture.size;
    buffer[1].buffer = g->vertices.buffer; buffer[1].range = g->vertices.size;
    buffer[2].buffer = g->indices.buffer; buffer[2].range = g->indices.size;
    for (unsigned i = 0; i < 3; ++i) {
        write[i].s_type = RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        write[i].dst_set = g->set; write[i].dst_binding = i; write[i].descriptor_count = 1;
        write[i].descriptor_type = RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; write[i].buffer_info = &buffer[i];
    }
    g->owner->api.update_descriptor_sets(g->owner->device, 3, write, 0, NULL);
    return 0;
}

static int gfx_draw_valid(struct rf_gpu_graphics *g, const struct rf_gpu_graphics_draw *d)
{
    if (!d->index_count || d->index_count % 3 || d->first_index % 3 ||
        d->first_index > g->index_count || d->index_count > g->index_count-d->first_index ||
        d->integer_depth > 1 || d->double_sided > 1 || d->material[1] > 256 || d->material[2] > 1 ||
        d->material[3] || d->camera[3] || d->texture[2] || d->texture[3] ||
        d->translation_scale[3] < 1 || d->translation_scale[3] > 4000 ||
        d->rotation[2] < -32767 || d->rotation[2] > 32767 ||
        d->rotation[3] < 0 || d->rotation[3] > 1 ||
        d->projection[0] != (int)g->target.width || d->projection[1] != (int)g->target.height ||
        d->projection[2] != 64 || d->projection[3] != (int)g->target.width*3/4 ||
        d->texture[0] != (int)g->texture_width || d->texture[1] != (int)g->texture_height) return 0;
    for (unsigned i = 0; i < 3; ++i)
        if (d->translation_scale[i] < -131072 || d->translation_scale[i] > 131072 ||
            d->camera[i] < -131072 || d->camera[i] > 131072) return 0;
    for (unsigned i = 0; i < 2; ++i)
        if (d->rotation[i] < -1024 || d->rotation[i] > 1024) return 0;
    for (unsigned i = 0; i < 4; ++i)
        if (d->view[i] < -1024 || d->view[i] > 1024) return 0;
    if (d->integer_depth) {
        /* O(1) conservative mesh bounds, before recording any draw. With
         * projected coordinates in [-16384,16384], edge*UV sums fit int64.
         * Deliberately conservative hosted-proof limit, not silent clipping. */
        int64_t x = ((int64_t)g->position_bound[0]*abs(d->rotation[1]) +
                     (int64_t)g->position_bound[2]*abs(d->rotation[0]))/1024;
        int64_t z = ((int64_t)g->position_bound[2]*abs(d->rotation[1]) +
                     (int64_t)g->position_bound[0]*abs(d->rotation[0]))/1024;
        int64_t y = g->position_bound[1]+abs(d->rotation[2]);
        x=x*d->translation_scale[3]/1000+abs(d->translation_scale[0]-d->camera[0]);
        y=y*d->translation_scale[3]/1000+abs(d->translation_scale[1]-d->camera[1]);
        z=z*d->translation_scale[3]/1000+abs(d->translation_scale[2]-d->camera[2]);
        int64_t vx=(x*abs(d->view[1])+z*abs(d->view[0]))/1024;
        int64_t vz=(x*abs(d->view[0])+z*abs(d->view[1]))/1024;
        int64_t vy=(y*abs(d->view[3])+vz*abs(d->view[2]))/1024;
        if (vx*d->projection[3]/64+d->projection[0]/2 > 16384 ||
            vy*d->projection[3]/64+d->projection[1]/2 > 16384) return 0;
    }
    return 1;
}

/* mode: 0 diagnostic roundtrip, 1 raster -> attachments, 2 attachments -> raster. */
static int gfx_bridge(struct rf_gpu_graphics *g, struct rf_gpu_vulkan_raster *r, int mode)
{
    struct rf_gpu_vulkan_impl *impl=g->owner;
    uint64_t bytes=(uint64_t)g->target.width*g->target.height*4;
    struct rf_vk_descriptor_buffer_info buffers[2]={{0}};
    struct rf_vk_write_descriptor_set writes[2]={{0}};
    struct rf_vk_memory_barrier barrier={0};
    VkBufferImageCopy copy={0};
    struct rf_vk_image_memory_barrier images[2]={{0}};
    struct rf_vk_buffer_image_copy import={0};
    uint32_t params[3]={g->target.width,g->target.height,0};
    buffers[0].buffer=g->target.bridge_transfer.buffer;buffers[0].range=bytes*2;
    buffers[1].buffer=g->target.bridge_raster.buffer;buffers[1].range=bytes*2;
    for(unsigned i=0;i<2;++i) {
        writes[i].s_type=RF_VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        writes[i].dst_set=g->bridge_set;writes[i].dst_binding=i;writes[i].descriptor_count=1;
        writes[i].descriptor_type=RF_VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;writes[i].buffer_info=&buffers[i];
    }
    impl->api.update_descriptor_sets(impl->device,2,writes,0,NULL);
    if(gfx_begin(g)<0)return -1;
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
        impl->api.cmd_copy_buffer(g->cmd,r->color.buffer,g->target.bridge_raster.buffer,1,&region);
        region.dst_offset=bytes;
        impl->api.cmd_copy_buffer(g->cmd,r->depth.buffer,g->target.bridge_raster.buffer,1,&region);
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
    params[2]=mode==1?1:0;
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
        impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_raster.buffer,r->color.buffer,1,&region);
        region.src_offset=bytes;
        impl->api.cmd_copy_buffer(g->cmd,g->target.bridge_raster.buffer,r->depth.buffer,1,&region);
    } else {
        for(unsigned i=0;i<2;++i) {
            images[i].s_type=RF_VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
            images[i].old_layout=(mode==1 && !g->target.valid)?VK_IMAGE_LAYOUT_UNDEFINED:VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;images[i].new_layout=VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            images[i].src_access_mask=VK_ACCESS_TRANSFER_READ_BIT;images[i].dst_access_mask=VK_ACCESS_TRANSFER_WRITE_BIT;
            images[i].src_queue_family_index=images[i].dst_queue_family_index=UINT32_MAX;
            images[i].image=i?g->target.depth.image:g->target.color.image;
            images[i].subresource_range.aspect_mask=i?VK_IMAGE_ASPECT_DEPTH_BIT:VK_IMAGE_ASPECT_COLOR_BIT;
            images[i].subresource_range.level_count=images[i].subresource_range.layer_count=1;
        }
        impl->api.cmd_pipeline_barrier(g->cmd,VK_PIPELINE_STAGE_TRANSFER_BIT,VK_PIPELINE_STAGE_TRANSFER_BIT,0,0,NULL,0,NULL,2,images);
        import.image_subresource.aspect_mask=VK_IMAGE_ASPECT_COLOR_BIT;import.image_subresource.layer_count=1;
        import.image_extent.width=params[0];import.image_extent.height=params[1];import.image_extent.depth=1;
        impl->api.cmd_copy_buffer_to_image(g->cmd,g->target.bridge_transfer.buffer,g->target.color.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&import);
        import.buffer_offset=bytes;import.image_subresource.aspect_mask=VK_IMAGE_ASPECT_DEPTH_BIT;
        impl->api.cmd_copy_buffer_to_image(g->cmd,g->target.bridge_transfer.buffer,g->target.depth.image,VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,1,&import);
    }
    if(gfx_submit(g)<0)return -1;
    if(mode==0)g->stats.bridge_roundtrips++;
    else g->stats.raster_bridge_transfers++;
    g->stats.bridge_transfer_bytes+=bytes*4;
    g->target.bridge_valid=1;
    return 0;
}

static int gfx_render(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws, uint32_t count,
    uint32_t *rgba, float *depth, uint32_t pixel_capacity,int load,
    struct rf_gpu_vulkan_raster *r)
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
    if (!g || g->poisoned || !g->index_count || !g->target.framebuffer ||
        !draws || !count || count > 4096 ||
        (!r && (!rgba || !depth || (uint64_t)g->target.width*g->target.height > pixel_capacity))) return -1;
    for (uint32_t i = 0; i < count; ++i) if (!gfx_draw_valid(g, &draws[i])) return -1;
    if(r) {
        if(r->owner!=g->owner || !r->segment_valid || !r->segment_graphics_compatible ||
           r->width!=g->target.width || r->height!=g->target.height)return -1;
        for(uint32_t i=0;i<count;++i)if(!draws[i].integer_depth)return -1;
    } else if(load && !g->target.valid)return -1;
    impl = g->owner; bytes = (uint64_t)g->target.width*g->target.height*4;
    g->target.bridge_valid=0;
    if(r)r->segment_valid=0;
    if(load && gfx_bridge(g,r,r?1:0)<0){g->poisoned=1;g->target.valid=0;return -1;}
    g->target.valid=0;
    memset(clear, 0, sizeof(clear));
    if (gfx_begin(g) < 0) { g->poisoned=1; return -1; }
    clear[0].color.float32[3] = 1.0f;
    begin.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    begin.renderPass = load?g->load_pass:g->render_pass; begin.framebuffer = g->target.framebuffer;
    begin.renderArea.extent.width = g->target.width; begin.renderArea.extent.height = g->target.height;
    begin.clearValueCount = 2; begin.pClearValues = clear;
    g->api.CmdBeginRenderPass(g->cmd, &begin, VK_SUBPASS_CONTENTS_INLINE);
    viewport.width = (float)g->target.width; viewport.height = (float)g->target.height; viewport.maxDepth = 1.0f;
    scissor.extent = begin.renderArea.extent;
    g->api.CmdSetViewport(g->cmd, 0, 1, &viewport); g->api.CmdSetScissor(g->cmd, 0, 1, &scissor);
    g->api.CmdBindVertexBuffers(g->cmd, 0, 1, &g->vertices.buffer, &offset);
    g->api.CmdBindIndexBuffer(g->cmd, g->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
    impl->api.cmd_bind_descriptor_sets(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->layout, 0, 1, &g->set, 0, NULL);
    for (uint32_t i = 0; i < count; ++i) {
        uint32_t factor = draws[i].integer_depth ? 2 : 1;
        g->api.CmdBindIndexBuffer(g->cmd, draws[i].integer_depth ? g->compat_indices.buffer : g->indices.buffer, 0, VK_INDEX_TYPE_UINT32);
        impl->api.cmd_bind_pipeline(g->cmd, VK_PIPELINE_BIND_POINT_GRAPHICS, g->pipelines[draws[i].double_sided+draws[i].integer_depth*2]);
        g->api.CmdPushConstants(g->cmd, g->layout, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, 112, &draws[i]);
        g->api.CmdDrawIndexed(g->cmd, draws[i].index_count*factor, 1, draws[i].first_index*factor, 0, 0);
    }
    g->api.CmdEndRenderPass(g->cmd);
    if(r) {
        if(gfx_submit(g)<0 || gfx_bridge(g,r,2)<0){g->poisoned=1;return -1;}
        r->segment_valid=1;
    } else {
        copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        copy.imageSubresource.layerCount = 1;
        copy.imageExtent.width = g->target.width; copy.imageExtent.height = g->target.height; copy.imageExtent.depth = 1;
        g->api.CmdCopyImageToBuffer(g->cmd, g->target.color.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->target.readback.buffer, 1, &copy);
        copy.bufferOffset = bytes; copy.imageSubresource.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        g->api.CmdCopyImageToBuffer(g->cmd, g->target.depth.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, g->target.readback.buffer, 1, &copy);
        barrier.s_type = RF_VK_STRUCTURE_TYPE_MEMORY_BARRIER;
        barrier.src_access_mask = VK_ACCESS_TRANSFER_WRITE_BIT; barrier.dst_access_mask = VK_ACCESS_HOST_READ_BIT;
        impl->api.cmd_pipeline_barrier(g->cmd, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_HOST_BIT,
            0, 1, &barrier, 0, NULL, 0, NULL);
        if (gfx_submit(g) < 0) return -1;
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
{ return gfx_render(g,draws,count,rgba,depth,pixel_capacity,0,NULL); }

int rf_gpu_graphics_continue(struct rf_gpu_graphics *g,
    const struct rf_gpu_graphics_draw *draws,uint32_t count,
    uint32_t *rgba,float *depth,uint32_t pixel_capacity)
{ return gfx_render(g,draws,count,rgba,depth,pixel_capacity,1,NULL); }

int rf_gpu_graphics_raster_draw(struct rf_gpu_graphics *g, void *raster,
    const struct rf_gpu_graphics_draw *draws, uint32_t count)
{
    if(!raster)return -1;
    return gfx_render(g,draws,count,NULL,NULL,0,1,raster);
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
    if(gfx_submit(g)<0 || impl->api.map_memory(impl->device,g->target.readback.memory,0,
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
