#include "render/Swapchain.h"

#include <utility>

namespace render {

core::Result<Swapchain> Swapchain::create(const VulkanContext& context, VkExtent2D extent, bool vsync,
                                          VkSwapchainKHR previous) {
    const VkPresentModeKHR presentMode = vsync ? VK_PRESENT_MODE_FIFO_KHR : VK_PRESENT_MODE_MAILBOX_KHR;

    auto result = vkb::SwapchainBuilder(context.bootstrapDevice())
                      .set_desired_present_mode(presentMode)
                      .set_desired_extent(extent.width, extent.height)
                      .add_image_usage_flags(VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT)
                      .set_old_swapchain(previous)
                      .build();
    if (!result) {
        return core::Error{"swapchain creation failed: " + result.error().message()};
    }

    auto images = result.value().get_images();
    auto views = result.value().get_image_views();
    if (!images || !views) {
        return core::Error{"failed to retrieve swapchain images"};
    }

    Swapchain swapchain;
    swapchain.swapchain_ = result.value();
    swapchain.images_ = images.value();
    swapchain.imageViews_ = views.value();
    swapchain.owning_ = true;
    return std::move(swapchain);
}

Swapchain::Swapchain(Swapchain&& other) noexcept
    : swapchain_(other.swapchain_),
      images_(std::move(other.images_)),
      imageViews_(std::move(other.imageViews_)),
      owning_(std::exchange(other.owning_, false)) {}

Swapchain& Swapchain::operator=(Swapchain&& other) noexcept {
    if (this != &other) {
        destroy();
        swapchain_ = other.swapchain_;
        images_ = std::move(other.images_);
        imageViews_ = std::move(other.imageViews_);
        owning_ = std::exchange(other.owning_, false);
    }
    return *this;
}

Swapchain::~Swapchain() {
    destroy();
}

void Swapchain::destroy() {
    if (!owning_) {
        return;
    }
    swapchain_.destroy_image_views(imageViews_);
    vkb::destroy_swapchain(swapchain_);
    imageViews_.clear();
    images_.clear();
    owning_ = false;
}

}
