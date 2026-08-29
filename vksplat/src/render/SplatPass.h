#pragma once

#include <array>

#include <glm/glm.hpp>

#include "core/Result.h"
#include "render/GpuBuffer.h"
#include "render/UniqueHandle.h"
#include "render/VulkanContext.h"

namespace render {

inline constexpr uint32_t kMaxViews = 2;

struct ProjectionConstants {
    glm::mat4 view;
    glm::vec2 focal;
    glm::vec2 viewport;
    VkDeviceAddress splats;
    VkDeviceAddress projected;
    VkDeviceAddress draw;
    VkDeviceAddress keys;
    uint32_t splatCount;
    float depthMinimum;
    float depthMaximum;
    float extentSigma;
};

struct ClearConstants {
    VkDeviceAddress histogram;
    VkDeviceAddress draw;
    VkDeviceAddress dispatch;
};

struct HistogramConstants {
    VkDeviceAddress keys;
    VkDeviceAddress histogram;
    VkDeviceAddress draw;
};

struct ScanConstants {
    VkDeviceAddress histogram;
};

struct ScatterConstants {
    VkDeviceAddress keys;
    VkDeviceAddress histogram;
    VkDeviceAddress sorted;
    VkDeviceAddress draw;
};

struct CombineConstants {
    VkDeviceAddress left;
    VkDeviceAddress right;
    VkDeviceAddress combined;
    uint32_t viewCount;
};

struct RasterConstants {
    glm::vec2 viewport;
    VkDeviceAddress projected[kMaxViews];
    VkDeviceAddress sorted[kMaxViews];
    VkDeviceAddress counts[kMaxViews];
};

class SplatPass {
public:
    static core::Result<SplatPass> create(const VulkanContext& context, VkFormat colourFormat,
                                          uint32_t splatCount, uint32_t viewCount);

    SplatPass(SplatPass&&) noexcept = default;
    SplatPass& operator=(SplatPass&&) noexcept = default;
    SplatPass(const SplatPass&) = delete;
    SplatPass& operator=(const SplatPass&) = delete;

    void recordProjectionOnly(VkCommandBuffer commandBuffer, uint32_t eye, const glm::mat4& view,
                              glm::vec2 focal, glm::vec2 viewport, VkDeviceAddress splats,
                              uint32_t splatCount, float depthMinimum, float depthMaximum,
                              float extentSigma) const;
    void recordSortOnly(VkCommandBuffer commandBuffer, uint32_t eye) const;
    void recordCombine(VkCommandBuffer commandBuffer) const;
    void recordRaster(VkCommandBuffer commandBuffer, VkExtent2D extent) const;

    uint32_t visibleSplats() const;

private:
    struct Stage {
        UniquePipelineLayout layout;
        UniquePipeline pipeline;
    };

    struct EyeResources {
        GpuBuffer projected;
        GpuBuffer keys;
        GpuBuffer sorted;
        GpuBuffer drawArguments;
        VkDeviceAddress projectedAddress = 0;
        VkDeviceAddress keysAddress = 0;
        VkDeviceAddress sortedAddress = 0;
        VkDeviceAddress drawAddress = 0;
    };

    SplatPass() = default;

    void recordProjection(VkCommandBuffer commandBuffer, const EyeResources& eye, const glm::mat4& view,
                          glm::vec2 focal, glm::vec2 viewport, VkDeviceAddress splats,
                          uint32_t splatCount, float depthMinimum, float depthMaximum,
                          float extentSigma) const;
    void recordSort(VkCommandBuffer commandBuffer, const EyeResources& eye) const;

    VkDevice device_ = VK_NULL_HANDLE;
    Stage projection_;
    Stage clear_;
    Stage histogram_;
    Stage scan_;
    Stage scatter_;
    Stage combine_;
    Stage raster_;

    std::array<EyeResources, kMaxViews> eyes_;
    GpuBuffer combinedArguments_;
    GpuBuffer statistics_;
    GpuBuffer histogramBuffer_;
    GpuBuffer dispatchArguments_;
    VkDeviceAddress combinedAddress_ = 0;
    VkDeviceAddress histogramAddress_ = 0;
    VkDeviceAddress dispatchAddress_ = 0;
    uint32_t viewCount_ = 1;
};

}
