#pragma once

#include "core/Config.h"
#include "core/Result.h"
#include "render/VulkanContext.h"

namespace render {

struct TonemapConstants {
    float exposure;
    uint32_t operatorIndex;
};

class TonemapPass {
public:
    static core::Result<TonemapPass> create(const VulkanContext& context, VkFormat outputFormat);

    TonemapPass(TonemapPass&& other) noexcept;
    TonemapPass& operator=(TonemapPass&& other) noexcept;
    TonemapPass(const TonemapPass&) = delete;
    TonemapPass& operator=(const TonemapPass&) = delete;
    ~TonemapPass();

    void record(VkCommandBuffer commandBuffer, VkExtent2D extent, VkImageView source,
                const core::TonemapConfig& settings) const;

private:
    TonemapPass() = default;
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout descriptorLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout layout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
};

}
