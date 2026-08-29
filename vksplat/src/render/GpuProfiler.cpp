#include "render/GpuProfiler.h"

#include <algorithm>
#include <utility>

namespace render {

core::Result<GpuProfiler> GpuProfiler::create(const VulkanContext& context, uint32_t framesInFlight,
                                              uint32_t maximumStamps) {
    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(context.physicalDevice(), &properties);

    if (properties.limits.timestampPeriod == 0.0f) {
        return core::Error{"device does not support timestamp queries"};
    }

    GpuProfiler profiler;
    profiler.device_ = context.device();
    profiler.period_ = static_cast<double>(properties.limits.timestampPeriod);
    profiler.maximumStamps_ = maximumStamps;
    profiler.labels_.resize(framesInFlight);

    VkQueryPoolCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO;
    info.queryType = VK_QUERY_TYPE_TIMESTAMP;
    info.queryCount = framesInFlight * maximumStamps;

    if (vkCreateQueryPool(profiler.device_, &info, nullptr, &profiler.pool_) != VK_SUCCESS) {
        return core::Error{"vkCreateQueryPool failed"};
    }
    return core::Result<GpuProfiler>(std::move(profiler));
}

void GpuProfiler::beginFrame(VkCommandBuffer commandBuffer, uint32_t frameIndex) {
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }

    frameIndex_ = frameIndex;
    written_ = 0;
    labels_[frameIndex_].clear();

    vkCmdResetQueryPool(commandBuffer, pool_, frameIndex_ * maximumStamps_, maximumStamps_);
    stamp(commandBuffer, VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, "frame");
}

void GpuProfiler::stamp(VkCommandBuffer commandBuffer, VkPipelineStageFlags2 stage, const char* label) {
    if (pool_ == VK_NULL_HANDLE || written_ >= maximumStamps_) {
        return;
    }

    vkCmdWriteTimestamp2(commandBuffer, stage, pool_, frameIndex_ * maximumStamps_ + written_);
    labels_[frameIndex_].push_back(label);
    ++written_;
}

void GpuProfiler::collect(uint32_t frameIndex) {
    if (pool_ == VK_NULL_HANDLE) {
        return;
    }

    const std::vector<const char*>& labels = labels_[frameIndex];
    if (labels.size() < 2) {
        return;
    }

    std::vector<uint64_t> results(labels.size() * 2);
    const VkResult read = vkGetQueryPoolResults(
        device_, pool_, frameIndex * maximumStamps_, static_cast<uint32_t>(labels.size()),
        results.size() * sizeof(uint64_t), results.data(), sizeof(uint64_t) * 2,
        VK_QUERY_RESULT_64_BIT | VK_QUERY_RESULT_WITH_AVAILABILITY_BIT);

    if (read != VK_SUCCESS) {
        return;
    }

    for (size_t index = 1; index < labels.size(); ++index) {
        if (results[index * 2 + 1] == 0 || results[(index - 1) * 2 + 1] == 0) {
            return;
        }
    }

    for (size_t index = 1; index < labels.size(); ++index) {
        const double nanoseconds =
            static_cast<double>(results[index * 2] - results[(index - 1) * 2]) * period_;
        const std::string label = labels[index];

        const auto found = std::find(totalLabels_.begin(), totalLabels_.end(), label);
        if (found == totalLabels_.end()) {
            totalLabels_.push_back(label);
            totals_.push_back(nanoseconds / 1.0e6);
        } else {
            totals_[static_cast<size_t>(found - totalLabels_.begin())] += nanoseconds / 1.0e6;
        }
    }
    ++samples_;
}

std::vector<StageTiming> GpuProfiler::averages() const {
    std::vector<StageTiming> timings;
    if (samples_ == 0) {
        return timings;
    }

    timings.reserve(totalLabels_.size());
    for (size_t index = 0; index < totalLabels_.size(); ++index) {
        timings.push_back({totalLabels_[index], totals_[index] / static_cast<double>(samples_)});
    }
    return timings;
}

void GpuProfiler::resetAverages() {
    std::fill(totals_.begin(), totals_.end(), 0.0);
    samples_ = 0;
}

GpuProfiler::GpuProfiler(GpuProfiler&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      pool_(std::exchange(other.pool_, VK_NULL_HANDLE)),
      period_(other.period_),
      maximumStamps_(other.maximumStamps_),
      frameIndex_(other.frameIndex_),
      written_(other.written_),
      labels_(std::move(other.labels_)),
      totals_(std::move(other.totals_)),
      totalLabels_(std::move(other.totalLabels_)),
      samples_(other.samples_) {}

GpuProfiler& GpuProfiler::operator=(GpuProfiler&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        pool_ = std::exchange(other.pool_, VK_NULL_HANDLE);
        period_ = other.period_;
        maximumStamps_ = other.maximumStamps_;
        frameIndex_ = other.frameIndex_;
        written_ = other.written_;
        labels_ = std::move(other.labels_);
        totals_ = std::move(other.totals_);
        totalLabels_ = std::move(other.totalLabels_);
        samples_ = other.samples_;
    }
    return *this;
}

GpuProfiler::~GpuProfiler() {
    destroy();
}

void GpuProfiler::destroy() {
    if (pool_ != VK_NULL_HANDLE) {
        vkDestroyQueryPool(device_, pool_, nullptr);
        pool_ = VK_NULL_HANDLE;
    }
}

}
