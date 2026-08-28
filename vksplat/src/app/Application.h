#pragma once

#include <chrono>

#include "core/Config.h"
#include "core/Result.h"
#include "platform/Window.h"
#include "render/GpuBuffer.h"
#include "render/SplatPass.h"
#include "render/TonemapPass.h"
#include "render/Renderer.h"
#include "render/Swapchain.h"
#include "render/VulkanContext.h"
#include "scene/OrbitCamera.h"

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
                render::Swapchain swapchain, render::Renderer renderer, render::GpuBuffer splats,
                render::SplatPass pass, render::TonemapPass tonemap, scene::OrbitCamera camera, uint32_t splatCount, float sceneRadius)
        : config_(std::move(config)),
          window_(std::move(window)),
          context_(std::move(context)),
          swapchain_(std::move(swapchain)),
          renderer_(std::move(renderer)),
          splats_(std::move(splats)),
          pass_(std::move(pass)),
          tonemap_(std::move(tonemap)),
          camera_(std::move(camera)),
          splatCount_(splatCount),
          sceneRadius_(sceneRadius) {}

    core::Result<bool> recreateSwapchain();

    core::Config config_;
    platform::Window window_;
    render::VulkanContext context_;
    render::Swapchain swapchain_;
    render::Renderer renderer_;
    render::GpuBuffer splats_;
    render::SplatPass pass_;
    render::TonemapPass tonemap_;
    scene::OrbitCamera camera_;
    uint32_t splatCount_ = 0;
    float sceneRadius_ = 0.0f;
    std::chrono::steady_clock::time_point lastReport_{};
    uint32_t framesSinceReport_ = 0;
};

}
