#include "app/Application.h"

#include "core/Logger.h"
#include "platform/Paths.h"
#include <algorithm>
#include <cmath>

#include <glm/trigonometric.hpp>

#include "render/Upload.h"
#include "scene/SplatScene.h"

namespace app {

core::Result<Application> Application::create(const core::Config& config) {
    auto scenePath = platform::resolveResource(config.scene.path);
    if (!scenePath) {
        return core::Error{scenePath.error()};
    }

    auto loaded = scene::loadSplatPly(scenePath.value());
    if (!loaded) {
        return core::Error{loaded.error()};
    }

    const scene::SplatScene& splatScene = loaded.value();
    const scene::Bounds& bounds = splatScene.bounds;
    spdlog::info("scene: {} splats, {:.1f} MiB, {} SH coefficients", splatScene.splats.size(),
                 static_cast<double>(splatScene.byteSize()) / 1048576.0,
                 splatScene.sphericalHarmonicCoefficients);
    spdlog::info("bounds: min [{:.3f}, {:.3f}, {:.3f}] max [{:.3f}, {:.3f}, {:.3f}] radius {:.3f}",
                 bounds.minimum.x, bounds.minimum.y, bounds.minimum.z, bounds.maximum.x, bounds.maximum.y,
                 bounds.maximum.z, bounds.radius());

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

    auto splats = render::uploadDeviceLocal(context.value(), splatScene.splats.data(),
                                            splatScene.byteSize(),
                                            VK_BUFFER_USAGE_STORAGE_BUFFER_BIT |
                                                VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT);
    if (!splats) {
        return core::Error{splats.error()};
    }
    spdlog::info("uploaded {:.1f} MiB to device-local memory",
                 static_cast<double>(splats.value().size()) / 1048576.0);

    auto pass = render::SplatPass::create(context.value(), render::Renderer::kHdrFormat,
                                          static_cast<uint32_t>(splatScene.splats.size()));
    if (!pass) {
        return core::Error{pass.error()};
    }

    auto tonemap = render::TonemapPass::create(context.value(), swapchain.value().format());
    if (!tonemap) {
        return core::Error{tonemap.error()};
    }

    Application application(config, window.take(), context.take(), swapchain.take(), renderer.take(),
                            splats.take(), pass.take(), tonemap.take(),
                            scene::OrbitCamera(config.camera, bounds),
                            static_cast<uint32_t>(splatScene.splats.size()), bounds.radius());

    auto bound = application.renderer_.bindSwapchain(application.context_, application.swapchain_);
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
    return renderer_.bindSwapchain(context_, swapchain_);
}

core::Result<bool> Application::run() {
    const VkDeviceAddress splatAddress = splats_.deviceAddress(context_.device());
    auto previous = std::chrono::steady_clock::now();
    lastReport_ = previous;

    while (!window_.shouldClose()) {
        window_.pollEvents();

        const auto now = std::chrono::steady_clock::now();
        const float delta = std::chrono::duration<float>(now - previous).count();
        previous = now;
        camera_.advance(delta);

        const glm::mat4 view = camera_.view();

        auto status = renderer_.drawFrame(
            swapchain_, config_.renderer.clearColor,
            [this, &view, splatAddress](VkCommandBuffer commandBuffer, VkExtent2D area) {
                const glm::vec2 viewport{static_cast<float>(area.width), static_cast<float>(area.height)};
                const float focalLength =
                    viewport.y * 0.5f / std::tan(glm::radians(camera_.fieldOfViewDegrees()) * 0.5f);
                const float distance = camera_.distance();
                const float depthMinimum = std::max(camera_.nearPlane(), distance - sceneRadius_);
                const float depthMaximum = distance + sceneRadius_;
                pass_.recordProjection(commandBuffer, view, glm::vec2{focalLength}, viewport, splatAddress,
                                       splatCount_, depthMinimum, depthMaximum);
                pass_.recordSort(commandBuffer);
            },
            [this](VkCommandBuffer commandBuffer, VkExtent2D area) {
                pass_.recordRaster(commandBuffer, area);
            },
            [this](VkCommandBuffer commandBuffer, VkExtent2D area) {
                tonemap_.record(commandBuffer, area, renderer_.hdrView(), config_.tonemap);
            });

        if (!status) {
            return core::Error{status.error()};
        }
        ++framesSinceReport_;
        if (config_.renderer.logStatistics && now - lastReport_ >= std::chrono::seconds(1)) {
            const float seconds = std::chrono::duration<float>(now - lastReport_).count();
            const uint32_t visible = pass_.visibleSplats();
            spdlog::info("{:.1f} fps | {} / {} splats visible ({:.1f}%)",
                         static_cast<float>(framesSinceReport_) / seconds, visible, splatCount_,
                         100.0f * static_cast<float>(visible) / static_cast<float>(splatCount_));
            framesSinceReport_ = 0;
            lastReport_ = now;
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
