#pragma once

#include <utility>

#include <volk.h>

namespace render {

template <typename T, typename Deleter>
class UniqueHandle {
public:
    UniqueHandle() = default;
    UniqueHandle(VkDevice device, T handle) : device_(device), handle_(handle) {}

    UniqueHandle(UniqueHandle&& other) noexcept
        : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
          handle_(std::exchange(other.handle_, VK_NULL_HANDLE)) {}

    UniqueHandle& operator=(UniqueHandle&& other) noexcept {
        if (this != &other) {
            reset();
            device_ = std::exchange(other.device_, VK_NULL_HANDLE);
            handle_ = std::exchange(other.handle_, VK_NULL_HANDLE);
        }
        return *this;
    }

    UniqueHandle(const UniqueHandle&) = delete;
    UniqueHandle& operator=(const UniqueHandle&) = delete;
    ~UniqueHandle() { reset(); }

    T get() const { return handle_; }

private:
    void reset() {
        if (handle_ != VK_NULL_HANDLE && device_ != VK_NULL_HANDLE) {
            Deleter{}(device_, handle_);
        }
        handle_ = VK_NULL_HANDLE;
    }

    VkDevice device_ = VK_NULL_HANDLE;
    T handle_ = VK_NULL_HANDLE;
};

struct PipelineDeleter {
    void operator()(VkDevice device, VkPipeline handle) const {
        vkDestroyPipeline(device, handle, nullptr);
    }
};

struct PipelineLayoutDeleter {
    void operator()(VkDevice device, VkPipelineLayout handle) const {
        vkDestroyPipelineLayout(device, handle, nullptr);
    }
};

struct SamplerDeleter {
    void operator()(VkDevice device, VkSampler handle) const {
        vkDestroySampler(device, handle, nullptr);
    }
};

struct DescriptorSetLayoutDeleter {
    void operator()(VkDevice device, VkDescriptorSetLayout handle) const {
        vkDestroyDescriptorSetLayout(device, handle, nullptr);
    }
};

using UniquePipeline = UniqueHandle<VkPipeline, PipelineDeleter>;
using UniquePipelineLayout = UniqueHandle<VkPipelineLayout, PipelineLayoutDeleter>;
using UniqueSampler = UniqueHandle<VkSampler, SamplerDeleter>;
using UniqueDescriptorSetLayout = UniqueHandle<VkDescriptorSetLayout, DescriptorSetLayoutDeleter>;

}
