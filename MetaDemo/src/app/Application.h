#pragma once

#include "core/Config.h"
#include "core/Result.h"
#include "platform/Window.h"
#include "render/Renderer.h"
#include "render/Swapchain.h"
#include "render/VulkanContext.h"

namespace app {

class Application {
public:
    static core::Result<Application> create(const core::Config& config);

    Application(Application&&) noexcept = default;
    Application& operator=(Application&&) noexcept = default;
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    core::Result<bool> run();

private:
    Application(core::Config config, platform::Window window, render::VulkanContext context,
                render::Swapchain swapchain, render::Renderer renderer)
        : config_(std::move(config)),
          window_(std::move(window)),
          context_(std::move(context)),
          swapchain_(std::move(swapchain)),
          renderer_(std::move(renderer)) {}

    core::Result<bool> recreateSwapchain();

    core::Config config_;
    platform::Window window_;
    render::VulkanContext context_;
    render::Swapchain swapchain_;
    render::Renderer renderer_;
};

}
