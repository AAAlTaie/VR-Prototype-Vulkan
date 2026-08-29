#pragma once

#include <array>
#include <string>
#include <vector>

#include <volk.h>

#include "core/Result.h"
#include "render/VulkanContext.h"

namespace render {

struct StageTiming {
    std::string label;
    double milliseconds = 0.0;
};

class GpuProfiler {
public:
    GpuProfiler() = default;

    static core::Result<GpuProfiler> create(const VulkanContext& context, uint32_t framesInFlight,
                                            uint32_t maximumStamps);

    GpuProfiler(GpuProfiler&& other) noexcept;
    GpuProfiler& operator=(GpuProfiler&& other) noexcept;
    GpuProfiler(const GpuProfiler&) = delete;
    GpuProfiler& operator=(const GpuProfiler&) = delete;
    ~GpuProfiler();

    void beginFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex);
    void stamp(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, const char* label);
    void collect(uint32_t frameIndex);

    std::vector<StageTiming> averages() const;
    void resetAverages();
    bool enabled() const { return pool_ != VK_NULL_HANDLE; }

private:
    void destroy();

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueryPool pool_ = VK_NULL_HANDLE;
    double period_ = 1.0;
    uint32_t maximumStamps_ = 0;
    uint32_t frameIndex_ = 0;
    uint32_t written_ = 0;

    std::vector<std::vector<const char*>> labels_;
    std::vector<double> totals_;
    std::vector<std::string> totalLabels_;
    uint32_t samples_ = 0;
};

}
