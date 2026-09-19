/* Copyright 2015-2024 The Khronos Group Inc.
 * SPDX-License-Identifier: Apache-2.0 OR MIT
 * Generated from Khronos Vulkan-Headers v1.3.290 registry/vk.xml.
 * Regenerate with tools/generate_gpu_graphics_abi.py. */
#ifndef RF_VULKAN_GRAPHICS_MIN_H
#define RF_VULKAN_GRAPHICS_MIN_H
#include "rf_vulkan_min.h"
typedef uint32_t VkShaderStageFlags;
typedef struct VkPushConstantRange {
    VkShaderStageFlags     stageFlags;
    uint32_t               offset;
    uint32_t               size;
} VkPushConstantRange;
typedef rf_vk_physical_device VkPhysicalDevice;
typedef uint32_t VkFormat;
typedef uint32_t VkFormatFeatureFlags;
typedef struct VkFormatProperties {
    VkFormatFeatureFlags   linearTilingFeatures;
    VkFormatFeatureFlags   optimalTilingFeatures;
    VkFormatFeatureFlags   bufferFeatures;
} VkFormatProperties;
typedef void (RF_VK_CALL *rf_gfx_GetPhysicalDeviceFormatProperties_fn)(VkPhysicalDevice physicalDevice, VkFormat format, VkFormatProperties* pFormatProperties);
typedef rf_vk_result VkResult;
typedef rf_vk_device VkDevice;
typedef uint32_t VkStructureType;
typedef uint32_t VkImageCreateFlags;
typedef uint32_t VkImageType;
typedef struct VkExtent3D {
    uint32_t        width;
    uint32_t        height;
    uint32_t        depth;
} VkExtent3D;
typedef uint32_t VkSampleCountFlagBits;
typedef uint32_t VkImageTiling;
typedef uint32_t VkImageUsageFlags;
typedef uint32_t VkSharingMode;
typedef uint32_t VkImageLayout;
typedef struct VkImageCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkImageCreateFlags     flags;
    VkImageType            imageType;
    VkFormat               format;
    VkExtent3D             extent;
    uint32_t               mipLevels;
    uint32_t               arrayLayers;
    VkSampleCountFlagBits  samples;
    VkImageTiling          tiling;
    VkImageUsageFlags      usage;
    VkSharingMode          sharingMode;
    uint32_t               queueFamilyIndexCount;
    const uint32_t*        pQueueFamilyIndices;
    VkImageLayout          initialLayout;
} VkImageCreateInfo;
typedef void VkAllocationCallbacks;
typedef rf_vk_image VkImage;
typedef VkResult (RF_VK_CALL *rf_gfx_CreateImage_fn)(VkDevice device, const VkImageCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImage* pImage);
typedef void (RF_VK_CALL *rf_gfx_DestroyImage_fn)(VkDevice device, VkImage image, const VkAllocationCallbacks* pAllocator);
typedef uint64_t VkDeviceSize;
typedef struct VkMemoryRequirements {
    VkDeviceSize           size;
    VkDeviceSize           alignment;
    uint32_t               memoryTypeBits;
} VkMemoryRequirements;
typedef void (RF_VK_CALL *rf_gfx_GetImageMemoryRequirements_fn)(VkDevice device, VkImage image, VkMemoryRequirements* pMemoryRequirements);
typedef rf_vk_device_memory VkDeviceMemory;
typedef VkResult (RF_VK_CALL *rf_gfx_BindImageMemory_fn)(VkDevice device, VkImage image, VkDeviceMemory memory, VkDeviceSize memoryOffset);
typedef uint32_t VkImageViewCreateFlags;
typedef uint32_t VkImageViewType;
typedef uint32_t VkComponentSwizzle;
typedef struct VkComponentMapping {
    VkComponentSwizzle r;
    VkComponentSwizzle g;
    VkComponentSwizzle b;
    VkComponentSwizzle a;
} VkComponentMapping;
typedef uint32_t VkImageAspectFlags;
typedef struct VkImageSubresourceRange {
    VkImageAspectFlags     aspectMask;
    uint32_t               baseMipLevel;
    uint32_t               levelCount;
    uint32_t               baseArrayLayer;
    uint32_t               layerCount;
} VkImageSubresourceRange;
typedef struct VkImageViewCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkImageViewCreateFlags flags;
    VkImage                image;
    VkImageViewType        viewType;
    VkFormat               format;
    VkComponentMapping     components;
    VkImageSubresourceRange subresourceRange;
} VkImageViewCreateInfo;
typedef struct VkImageView_T *VkImageView;
typedef VkResult (RF_VK_CALL *rf_gfx_CreateImageView_fn)(VkDevice device, const VkImageViewCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkImageView* pView);
typedef void (RF_VK_CALL *rf_gfx_DestroyImageView_fn)(VkDevice device, VkImageView imageView, const VkAllocationCallbacks* pAllocator);
typedef uint32_t VkRenderPassCreateFlags;
typedef uint32_t VkAttachmentDescriptionFlags;
typedef uint32_t VkAttachmentLoadOp;
typedef uint32_t VkAttachmentStoreOp;
typedef struct VkAttachmentDescription {
    VkAttachmentDescriptionFlags flags;
    VkFormat               format;
    VkSampleCountFlagBits  samples;
    VkAttachmentLoadOp     loadOp;
    VkAttachmentStoreOp    storeOp;
    VkAttachmentLoadOp     stencilLoadOp;
    VkAttachmentStoreOp    stencilStoreOp;
    VkImageLayout          initialLayout;
    VkImageLayout          finalLayout;
} VkAttachmentDescription;
typedef uint32_t VkSubpassDescriptionFlags;
typedef uint32_t VkPipelineBindPoint;
typedef struct VkAttachmentReference {
    uint32_t               attachment;
    VkImageLayout          layout;
} VkAttachmentReference;
typedef struct VkSubpassDescription {
    VkSubpassDescriptionFlags flags;
    VkPipelineBindPoint    pipelineBindPoint;
    uint32_t               inputAttachmentCount;
    const VkAttachmentReference* pInputAttachments;
    uint32_t               colorAttachmentCount;
    const VkAttachmentReference* pColorAttachments;
    const VkAttachmentReference* pResolveAttachments;
    const VkAttachmentReference* pDepthStencilAttachment;
    uint32_t               preserveAttachmentCount;
    const uint32_t* pPreserveAttachments;
} VkSubpassDescription;
typedef uint32_t VkPipelineStageFlags;
typedef uint32_t VkAccessFlags;
typedef uint32_t VkDependencyFlags;
typedef struct VkSubpassDependency {
    uint32_t               srcSubpass;
    uint32_t               dstSubpass;
    VkPipelineStageFlags   srcStageMask;
    VkPipelineStageFlags   dstStageMask;
    VkAccessFlags          srcAccessMask;
    VkAccessFlags          dstAccessMask;
    VkDependencyFlags      dependencyFlags;
} VkSubpassDependency;
typedef struct VkRenderPassCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkRenderPassCreateFlags flags;
    uint32_t   attachmentCount;
    const VkAttachmentDescription* pAttachments;
    uint32_t               subpassCount;
    const VkSubpassDescription* pSubpasses;
    uint32_t       dependencyCount;
    const VkSubpassDependency* pDependencies;
} VkRenderPassCreateInfo;
typedef struct VkRenderPass_T *VkRenderPass;
typedef VkResult (RF_VK_CALL *rf_gfx_CreateRenderPass_fn)(VkDevice device, const VkRenderPassCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkRenderPass* pRenderPass);
typedef void (RF_VK_CALL *rf_gfx_DestroyRenderPass_fn)(VkDevice device, VkRenderPass renderPass, const VkAllocationCallbacks* pAllocator);
typedef uint32_t VkFramebufferCreateFlags;
typedef struct VkFramebufferCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkFramebufferCreateFlags    flags;
    VkRenderPass                           renderPass;
    uint32_t               attachmentCount;
    const VkImageView*     pAttachments;
    uint32_t               width;
    uint32_t               height;
    uint32_t               layers;
} VkFramebufferCreateInfo;
typedef struct VkFramebuffer_T *VkFramebuffer;
typedef VkResult (RF_VK_CALL *rf_gfx_CreateFramebuffer_fn)(VkDevice device, const VkFramebufferCreateInfo* pCreateInfo, const VkAllocationCallbacks* pAllocator, VkFramebuffer* pFramebuffer);
typedef void (RF_VK_CALL *rf_gfx_DestroyFramebuffer_fn)(VkDevice device, VkFramebuffer framebuffer, const VkAllocationCallbacks* pAllocator);
typedef rf_vk_pipeline_cache VkPipelineCache;
typedef uint32_t VkPipelineCreateFlags;
typedef uint32_t VkPipelineShaderStageCreateFlags;
typedef uint32_t VkShaderStageFlagBits;
typedef rf_vk_shader_module VkShaderModule;
typedef struct VkSpecializationMapEntry {
    uint32_t                     constantID;
    uint32_t                     offset;
    size_t size;
} VkSpecializationMapEntry;
typedef struct VkSpecializationInfo {
    uint32_t               mapEntryCount;
    const VkSpecializationMapEntry* pMapEntries;
    size_t                 dataSize;
    const void*            pData;
} VkSpecializationInfo;
typedef struct VkPipelineShaderStageCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineShaderStageCreateFlags    flags;
    VkShaderStageFlagBits  stage;
    VkShaderModule module;
    const char* pName;
    const VkSpecializationInfo* pSpecializationInfo;
} VkPipelineShaderStageCreateInfo;
typedef uint32_t VkPipelineVertexInputStateCreateFlags;
typedef uint32_t VkVertexInputRate;
typedef struct VkVertexInputBindingDescription {
    uint32_t               binding;
    uint32_t               stride;
    VkVertexInputRate      inputRate;
} VkVertexInputBindingDescription;
typedef struct VkVertexInputAttributeDescription {
    uint32_t               location;
    uint32_t               binding;
    VkFormat               format;
    uint32_t               offset;
} VkVertexInputAttributeDescription;
typedef struct VkPipelineVertexInputStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineVertexInputStateCreateFlags    flags;
    uint32_t               vertexBindingDescriptionCount;
    const VkVertexInputBindingDescription* pVertexBindingDescriptions;
    uint32_t               vertexAttributeDescriptionCount;
    const VkVertexInputAttributeDescription* pVertexAttributeDescriptions;
} VkPipelineVertexInputStateCreateInfo;
typedef uint32_t VkPipelineInputAssemblyStateCreateFlags;
typedef uint32_t VkPrimitiveTopology;
typedef rf_vk_bool32 VkBool32;
typedef struct VkPipelineInputAssemblyStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineInputAssemblyStateCreateFlags    flags;
    VkPrimitiveTopology    topology;
    VkBool32               primitiveRestartEnable;
} VkPipelineInputAssemblyStateCreateInfo;
typedef uint32_t VkPipelineTessellationStateCreateFlags;
typedef struct VkPipelineTessellationStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineTessellationStateCreateFlags    flags;
    uint32_t               patchControlPoints;
} VkPipelineTessellationStateCreateInfo;
typedef uint32_t VkPipelineViewportStateCreateFlags;
typedef struct VkViewport {
    float x;
    float y;
    float width;
    float height;
    float                       minDepth;
    float                       maxDepth;
} VkViewport;
typedef struct VkOffset2D {
    int32_t        x;
    int32_t        y;
} VkOffset2D;
typedef struct VkExtent2D {
    uint32_t        width;
    uint32_t        height;
} VkExtent2D;
typedef struct VkRect2D {
    VkOffset2D     offset;
    VkExtent2D     extent;
} VkRect2D;
typedef struct VkPipelineViewportStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineViewportStateCreateFlags    flags;
    uint32_t               viewportCount;
    const VkViewport*      pViewports;
    uint32_t               scissorCount;
    const VkRect2D*        pScissors;
} VkPipelineViewportStateCreateInfo;
typedef uint32_t VkPipelineRasterizationStateCreateFlags;
typedef uint32_t VkPolygonMode;
typedef uint32_t VkCullModeFlags;
typedef uint32_t VkFrontFace;
typedef struct VkPipelineRasterizationStateCreateInfo {
    VkStructureType sType;
    const void* pNext;
    VkPipelineRasterizationStateCreateFlags    flags;
    VkBool32               depthClampEnable;
    VkBool32               rasterizerDiscardEnable;
    VkPolygonMode          polygonMode;
    VkCullModeFlags        cullMode;
    VkFrontFace            frontFace;
    VkBool32               depthBiasEnable;
    float                  depthBiasConstantFactor;
    float                  depthBiasClamp;
    float                  depthBiasSlopeFactor;
    float                  lineWidth;
} VkPipelineRasterizationStateCreateInfo;
typedef uint32_t VkPipelineMultisampleStateCreateFlags;
typedef uint32_t VkSampleMask;
typedef struct VkPipelineMultisampleStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineMultisampleStateCreateFlags    flags;
    VkSampleCountFlagBits  rasterizationSamples;
    VkBool32               sampleShadingEnable;
    float                  minSampleShading;
    const VkSampleMask*    pSampleMask;
    VkBool32               alphaToCoverageEnable;
    VkBool32               alphaToOneEnable;
} VkPipelineMultisampleStateCreateInfo;
typedef uint32_t VkPipelineDepthStencilStateCreateFlags;
typedef uint32_t VkCompareOp;
typedef uint32_t VkStencilOp;
typedef struct VkStencilOpState {
    VkStencilOp            failOp;
    VkStencilOp            passOp;
    VkStencilOp            depthFailOp;
    VkCompareOp            compareOp;
    uint32_t               compareMask;
    uint32_t               writeMask;
    uint32_t               reference;
} VkStencilOpState;
typedef struct VkPipelineDepthStencilStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineDepthStencilStateCreateFlags    flags;
    VkBool32               depthTestEnable;
    VkBool32               depthWriteEnable;
    VkCompareOp            depthCompareOp;
    VkBool32               depthBoundsTestEnable;
    VkBool32               stencilTestEnable;
    VkStencilOpState       front;
    VkStencilOpState       back;
    float                  minDepthBounds;
    float                  maxDepthBounds;
} VkPipelineDepthStencilStateCreateInfo;
typedef uint32_t VkPipelineColorBlendStateCreateFlags;
typedef uint32_t VkLogicOp;
typedef uint32_t VkBlendFactor;
typedef uint32_t VkBlendOp;
typedef uint32_t VkColorComponentFlags;
typedef struct VkPipelineColorBlendAttachmentState {
    VkBool32               blendEnable;
    VkBlendFactor          srcColorBlendFactor;
    VkBlendFactor          dstColorBlendFactor;
    VkBlendOp              colorBlendOp;
    VkBlendFactor          srcAlphaBlendFactor;
    VkBlendFactor          dstAlphaBlendFactor;
    VkBlendOp              alphaBlendOp;
    VkColorComponentFlags  colorWriteMask;
} VkPipelineColorBlendAttachmentState;
typedef struct VkPipelineColorBlendStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineColorBlendStateCreateFlags    flags;
    VkBool32               logicOpEnable;
    VkLogicOp              logicOp;
    uint32_t               attachmentCount;
    const VkPipelineColorBlendAttachmentState* pAttachments;
    float                  blendConstants[4];
} VkPipelineColorBlendStateCreateInfo;
typedef uint32_t VkPipelineDynamicStateCreateFlags;
typedef uint32_t VkDynamicState;
typedef struct VkPipelineDynamicStateCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineDynamicStateCreateFlags    flags;
    uint32_t               dynamicStateCount;
    const VkDynamicState*  pDynamicStates;
} VkPipelineDynamicStateCreateInfo;
typedef rf_vk_pipeline_layout VkPipelineLayout;
typedef rf_vk_pipeline VkPipeline;
typedef struct VkGraphicsPipelineCreateInfo {
    VkStructureType sType;
    const void*            pNext;
    VkPipelineCreateFlags  flags;
    uint32_t stageCount;
    const VkPipelineShaderStageCreateInfo* pStages;
    const VkPipelineVertexInputStateCreateInfo* pVertexInputState;
    const VkPipelineInputAssemblyStateCreateInfo* pInputAssemblyState;
    const VkPipelineTessellationStateCreateInfo* pTessellationState;
    const VkPipelineViewportStateCreateInfo* pViewportState;
    const VkPipelineRasterizationStateCreateInfo* pRasterizationState;
    const VkPipelineMultisampleStateCreateInfo* pMultisampleState;
    const VkPipelineDepthStencilStateCreateInfo* pDepthStencilState;
    const VkPipelineColorBlendStateCreateInfo* pColorBlendState;
    const VkPipelineDynamicStateCreateInfo* pDynamicState;
    VkPipelineLayout       layout;
    VkRenderPass           renderPass;
    uint32_t               subpass;
    VkPipeline      basePipelineHandle;
    int32_t                basePipelineIndex;
} VkGraphicsPipelineCreateInfo;
typedef VkResult (RF_VK_CALL *rf_gfx_CreateGraphicsPipelines_fn)(VkDevice device, VkPipelineCache pipelineCache, uint32_t createInfoCount, const VkGraphicsPipelineCreateInfo* pCreateInfos, const VkAllocationCallbacks* pAllocator, VkPipeline* pPipelines);
typedef rf_vk_command_buffer VkCommandBuffer;
typedef union VkClearColorValue {
    float                  float32[4];
    int32_t                int32[4];
    uint32_t               uint32[4];
} VkClearColorValue;
typedef struct VkClearDepthStencilValue {
    float                  depth;
    uint32_t               stencil;
} VkClearDepthStencilValue;
typedef union VkClearValue {
    VkClearColorValue      color;
    VkClearDepthStencilValue depthStencil;
} VkClearValue;
typedef struct VkRenderPassBeginInfo {
    VkStructureType sType;
    const void*            pNext;
    VkRenderPass           renderPass;
    VkFramebuffer          framebuffer;
    VkRect2D               renderArea;
    uint32_t               clearValueCount;
    const VkClearValue*    pClearValues;
} VkRenderPassBeginInfo;
typedef uint32_t VkSubpassContents;
typedef void (RF_VK_CALL *rf_gfx_CmdBeginRenderPass_fn)(VkCommandBuffer commandBuffer, const VkRenderPassBeginInfo* pRenderPassBegin, VkSubpassContents contents);
typedef void (RF_VK_CALL *rf_gfx_CmdEndRenderPass_fn)(VkCommandBuffer commandBuffer);
typedef rf_vk_buffer VkBuffer;
typedef void (RF_VK_CALL *rf_gfx_CmdBindVertexBuffers_fn)(VkCommandBuffer commandBuffer, uint32_t firstBinding, uint32_t bindingCount, const VkBuffer* pBuffers, const VkDeviceSize* pOffsets);
typedef uint32_t VkIndexType;
typedef void (RF_VK_CALL *rf_gfx_CmdBindIndexBuffer_fn)(VkCommandBuffer commandBuffer, VkBuffer buffer, VkDeviceSize offset, VkIndexType indexType);
typedef void (RF_VK_CALL *rf_gfx_CmdDrawIndexed_fn)(VkCommandBuffer commandBuffer, uint32_t indexCount, uint32_t instanceCount, uint32_t firstIndex, int32_t vertexOffset, uint32_t firstInstance);
typedef void (RF_VK_CALL *rf_gfx_CmdSetViewport_fn)(VkCommandBuffer commandBuffer, uint32_t firstViewport, uint32_t viewportCount, const VkViewport* pViewports);
typedef void (RF_VK_CALL *rf_gfx_CmdSetScissor_fn)(VkCommandBuffer commandBuffer, uint32_t firstScissor, uint32_t scissorCount, const VkRect2D* pScissors);
typedef void (RF_VK_CALL *rf_gfx_CmdPushConstants_fn)(VkCommandBuffer commandBuffer, VkPipelineLayout layout, VkShaderStageFlags stageFlags, uint32_t offset, uint32_t size, const void* pValues);
typedef struct VkImageSubresourceLayers {
    VkImageAspectFlags     aspectMask;
    uint32_t               mipLevel;
    uint32_t               baseArrayLayer;
    uint32_t               layerCount;
} VkImageSubresourceLayers;
typedef struct VkOffset3D {
    int32_t        x;
    int32_t        y;
    int32_t        z;
} VkOffset3D;
typedef struct VkBufferImageCopy {
    VkDeviceSize           bufferOffset;
    uint32_t               bufferRowLength;
    uint32_t               bufferImageHeight;
    VkImageSubresourceLayers imageSubresource;
    VkOffset3D             imageOffset;
    VkExtent3D             imageExtent;
} VkBufferImageCopy;
typedef void (RF_VK_CALL *rf_gfx_CmdCopyImageToBuffer_fn)(VkCommandBuffer commandBuffer, VkImage srcImage, VkImageLayout srcImageLayout, VkBuffer dstBuffer, uint32_t regionCount, const VkBufferImageCopy* pRegions);
#define VK_ACCESS_COLOR_ATTACHMENT_READ_BIT 128
#define VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT 256
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_READ_BIT 512
#define VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT 1024
#define VK_ACCESS_HOST_READ_BIT 8192
#define VK_ACCESS_INDEX_READ_BIT 2
#define VK_ACCESS_SHADER_READ_BIT 32
#define VK_ACCESS_SHADER_WRITE_BIT 64
#define VK_ACCESS_TRANSFER_READ_BIT 2048
#define VK_ACCESS_TRANSFER_WRITE_BIT 4096
#define VK_ACCESS_VERTEX_ATTRIBUTE_READ_BIT 4
#define VK_ATTACHMENT_LOAD_OP_CLEAR 1
#define VK_ATTACHMENT_LOAD_OP_DONT_CARE 2
#define VK_ATTACHMENT_LOAD_OP_LOAD 0
#define VK_ATTACHMENT_STORE_OP_DONT_CARE 1
#define VK_ATTACHMENT_STORE_OP_STORE 0
#define VK_BUFFER_USAGE_INDEX_BUFFER_BIT 64
#define VK_BUFFER_USAGE_VERTEX_BUFFER_BIT 128
#define VK_COLOR_COMPONENT_A_BIT 8
#define VK_COLOR_COMPONENT_B_BIT 4
#define VK_COLOR_COMPONENT_G_BIT 2
#define VK_COLOR_COMPONENT_R_BIT 1
#define VK_COMPARE_OP_GREATER_OR_EQUAL 6
#define VK_CULL_MODE_BACK_BIT 2
#define VK_CULL_MODE_NONE 0
#define VK_DYNAMIC_STATE_SCISSOR 1
#define VK_DYNAMIC_STATE_VIEWPORT 0
#define VK_FORMAT_D32_SFLOAT 126
#define VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT 128
#define VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT 512
#define VK_FORMAT_R32G32B32_SINT 105
#define VK_FORMAT_R32G32_SINT 102
#define VK_FORMAT_R8G8B8A8_UNORM 37
#define VK_FRONT_FACE_COUNTER_CLOCKWISE 0
#define VK_IMAGE_ASPECT_COLOR_BIT 1
#define VK_IMAGE_ASPECT_DEPTH_BIT 2
#define VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL 2
#define VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL 3
#define VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL 7
#define VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL 6
#define VK_IMAGE_LAYOUT_UNDEFINED 0
#define VK_IMAGE_TILING_OPTIMAL 0
#define VK_IMAGE_TYPE_2D 1
#define VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT 16
#define VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT 32
#define VK_IMAGE_USAGE_TRANSFER_DST_BIT 2
#define VK_IMAGE_USAGE_TRANSFER_SRC_BIT 1
#define VK_IMAGE_VIEW_TYPE_2D 1
#define VK_INDEX_TYPE_UINT32 1
#define VK_PIPELINE_BIND_POINT_GRAPHICS 0
#define VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT 1024
#define VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT 2048
#define VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT 256
#define VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT 128
#define VK_PIPELINE_STAGE_HOST_BIT 16384
#define VK_PIPELINE_STAGE_LATE_FRAGMENT_TESTS_BIT 512
#define VK_PIPELINE_STAGE_TRANSFER_BIT 4096
#define VK_PIPELINE_STAGE_VERTEX_INPUT_BIT 4
#define VK_PIPELINE_STAGE_VERTEX_SHADER_BIT 8
#define VK_POLYGON_MODE_FILL 0
#define VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST 3
#define VK_QUEUE_GRAPHICS_BIT 1
#define VK_SAMPLE_COUNT_1_BIT 1
#define VK_SHADER_STAGE_COMPUTE_BIT 32
#define VK_SHADER_STAGE_FRAGMENT_BIT 16
#define VK_SHADER_STAGE_VERTEX_BIT 1
#define VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO 37
#define VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO 28
#define VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO 14
#define VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO 15
#define VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO 26
#define VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO 25
#define VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO 27
#define VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO 20
#define VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO 24
#define VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO 23
#define VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO 18
#define VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO 19
#define VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO 22
#define VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO 43
#define VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO 38
#define VK_SUBPASS_CONTENTS_INLINE 0
#define VK_SUBPASS_EXTERNAL (~0U)
#define VK_VERTEX_INPUT_RATE_VERTEX 0
#endif
