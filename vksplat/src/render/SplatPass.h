#pragma once

#include <glm/glm.hpp>

#include "core/Result.h"
#include "render/GpuBuffer.h"
#include "render/VulkanContext.h"

namespace render {

struct ProjectionConstants {
    glm::mat4 view;
    glm::vec2 focal;
    glm::vec2 viewport;
    VkDeviceAddress splats;
    VkDeviceAddress projected;
    VkDeviceAddress draw;
    uint32_t splatCount;
    float nearPlane;
};

struct RasterConstants {
    glm::vec2 viewport;
    VkDeviceAddress projected;
};

class SplatPass {
public:
    static core::Result<SplatPass> create(const VulkanContext& context, VkFormat colourFormat,
                                          uint32_t splatCount);

    SplatPass(SplatPass&& other) noexcept;
    SplatPass& operator=(SplatPass&& other) noexcept;
    SplatPass(const SplatPass&) = delete;
    SplatPass& operator=(const SplatPass&) = delete;
    ~SplatPass();

    void recordProjection(VkCommandBuffer commandBuffer, const glm::mat4& view, glm::vec2 focal,
                          glm::vec2 viewport, VkDeviceAddress splats, uint32_t splatCount,
                          float nearPlane) const;
    void recordRaster(VkCommandBuffer commandBuffer, VkExtent2D extent) const;

    VkDeviceAddress projectedAddress() const { return projectedAddress_; }
    uint32_t visibleSplats() const;

private:
    SplatPass() = default;
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkPipelineLayout projectionLayout_ = VK_NULL_HANDLE;
    VkPipeline projectionPipeline_ = VK_NULL_HANDLE;
    VkPipelineLayout rasterLayout_ = VK_NULL_HANDLE;
    VkPipeline rasterPipeline_ = VK_NULL_HANDLE;
    GpuBuffer projected_;
    GpuBuffer drawArguments_;
    GpuBuffer statistics_;
    VkDeviceAddress projectedAddress_ = 0;
    VkDeviceAddress drawAddress_ = 0;
};

}
