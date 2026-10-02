#ifndef RF_VULKAN_SKY_MIN_H
#define RF_VULKAN_SKY_MIN_H
/* Vulkan 1.0 sampler ABI, alongside the generated graphics subset.
 * https://docs.vulkan.org/refpages/latest/refpages/source/VkSamplerCreateInfo.html */
struct rf_sky_sampler_info {
    uint32_t s_type;const void *next;
    uint32_t flags,mag_filter,min_filter,mipmap_mode,address_u,address_v,address_w;
    float mip_lod_bias;
    uint32_t anisotropy_enable;float max_anisotropy;
    uint32_t compare_enable,compare_op;float min_lod,max_lod;
    uint32_t border_color,unnormalized_coordinates;
};
typedef rf_vk_result (RF_VK_CALL *rf_gfx_CreateSampler_fn)(rf_vk_device,
    const struct rf_sky_sampler_info *,const void *,void **);
typedef void (RF_VK_CALL *rf_gfx_DestroySampler_fn)(rf_vk_device,void *,const void *);
#define RF_SKY_STRUCTURE_TYPE_SAMPLER_CREATE_INFO 31
#define RF_SKY_IMAGE_TYPE_3D 2
#define RF_SKY_IMAGE_VIEW_TYPE_3D 2
#define RF_SKY_IMAGE_USAGE_SAMPLED_BIT 4
#define RF_SKY_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT 0x1000
#define RF_SKY_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER 1
#endif
