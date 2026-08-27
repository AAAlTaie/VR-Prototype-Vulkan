#pragma once

#include <volk.h>

#include <VkBootstrap.h>

#include "core/Config.h"
#include "core/Result.h"
#include "platform/Window.h"

namespace render {

class VulkanContext {
public:
    static core::Result<VulkanContext> create(const platform::Window& window,
                                              const core::RendererConfig& config);

    VulkanContext(VulkanContext&& other) noexcept;
    VulkanContext& operator=(VulkanContext&& other) noexcept;
    VulkanContext(const VulkanContext&) = delete;
    VulkanContext& operator=(const VulkanContext&) = delete;
    ~VulkanContext();

    VkDevice device() const { return device_.device; }
    VkQueue graphicsQueue() const { return graphicsQueue_; }
    uint32_t graphicsQueueFamily() const { return graphicsQueueFamily_; }
    VkSurfaceKHR surface() const { return surface_; }
    const vkb::Device& bootstrapDevice() const { return device_; }
    const std::string& deviceName() const { return deviceName_; }

private:
    VulkanContext() = default;
    void destroy();

    vkb::Instance instance_{};
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    vkb::Device device_{};
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    uint32_t graphicsQueueFamily_ = 0;
    std::string deviceName_;
    bool owning_ = false;
};

}
