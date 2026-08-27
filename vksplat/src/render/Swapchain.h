#pragma once

#include <vector>

#include <volk.h>

#include "core/Result.h"
#include "render/VulkanContext.h"

namespace render {

class Swapchain {
public:
    static core::Result<Swapchain> create(const VulkanContext& context, VkExtent2D extent, bool vsync,
                                          VkSwapchainKHR previous = VK_NULL_HANDLE);

    Swapchain(Swapchain&& other) noexcept;
    Swapchain& operator=(Swapchain&& other) noexcept;
    Swapchain(const Swapchain&) = delete;
    Swapchain& operator=(const Swapchain&) = delete;
    ~Swapchain();

    VkSwapchainKHR handle() const { return swapchain_.swapchain; }
    VkFormat format() const { return swapchain_.image_format; }
    VkExtent2D extent() const { return swapchain_.extent; }
    uint32_t imageCount() const { return static_cast<uint32_t>(images_.size()); }
    VkImage image(uint32_t index) const { return images_[index]; }
    VkImageView imageView(uint32_t index) const { return imageViews_[index]; }

private:
    Swapchain() = default;
    void destroy();

    vkb::Swapchain swapchain_{};
    std::vector<VkImage> images_;
    std::vector<VkImageView> imageViews_;
    bool owning_ = false;
};

}
