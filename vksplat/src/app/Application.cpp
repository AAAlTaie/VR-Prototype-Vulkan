#include "app/Application.h"

#include "core/Logger.h"

namespace app {

core::Result<Application> Application::create(const core::Config& config) {
    auto window = platform::Window::create(config.window);
    if (!window) {
        return core::Error{window.error()};
    }

    auto context = render::VulkanContext::create(window.value(), config.renderer);
    if (!context) {
        return core::Error{context.error()};
    }
    spdlog::info("device: {}", context.value().deviceName());

    auto swapchain = render::Swapchain::create(context.value(), window.value().framebufferExtent(),
                                               config.window.vsync);
    if (!swapchain) {
        return core::Error{swapchain.error()};
    }
    spdlog::info("swapchain: {}x{}, {} images", swapchain.value().extent().width,
                 swapchain.value().extent().height, swapchain.value().imageCount());

    auto renderer = render::Renderer::create(context.value(), config.renderer.framesInFlight);
    if (!renderer) {
        return core::Error{renderer.error()};
    }

    Application application(config, window.take(), context.take(), swapchain.take(), renderer.take());

    auto bound = application.renderer_.bindSwapchain(application.swapchain_);
    if (!bound) {
        return core::Error{bound.error()};
    }
    return core::Result<Application>(std::move(application));
}

core::Result<bool> Application::recreateSwapchain() {
    const VkExtent2D extent = window_.waitForNonZeroExtent();
    renderer_.waitIdle();

    auto replacement = render::Swapchain::create(context_, extent, config_.window.vsync, swapchain_.handle());
    if (!replacement) {
        return core::Error{replacement.error()};
    }

    swapchain_ = replacement.take();
    spdlog::info("swapchain resized to {}x{}", swapchain_.extent().width, swapchain_.extent().height);
    return renderer_.bindSwapchain(swapchain_);
}

core::Result<bool> Application::run() {
    while (!window_.shouldClose()) {
        window_.pollEvents();

        auto status = renderer_.drawFrame(swapchain_, config_.renderer.clearColor);
        if (!status) {
            return core::Error{status.error()};
        }
        if (status.value() == render::FrameStatus::SwapchainOutOfDate) {
            auto recreated = recreateSwapchain();
            if (!recreated) {
                return core::Error{recreated.error()};
            }
        }
    }

    renderer_.waitIdle();
    return true;
}

}
