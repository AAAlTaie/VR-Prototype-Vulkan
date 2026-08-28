#pragma once

#include <array>
#include <functional>
#include <vector>

#include <volk.h>

#include "core/Result.h"
#include "render/GpuImage.h"
#include "render/Swapchain.h"
#include "render/VulkanContext.h"

namespace render {

enum class FrameStatus { Presented, SwapchainOutOfDate };

class Renderer {
public:
    static core::Result<Renderer> create(const VulkanContext& context, uint32_t framesInFlight);

    Renderer(Renderer&& other) noexcept;
    Renderer& operator=(Renderer&& other) noexcept;
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;
    ~Renderer();

    static constexpr VkFormat kHdrFormat = VK_FORMAT_R16G16B16A16_SFLOAT;

    core::Result<bool> bindSwapchain(const VulkanContext& context, const Swapchain& swapchain);
    VkImageView hdrView() const { return hdrTarget_.view(); }
    using RecordFunction = std::function<void(VkCommandBuffer, VkExtent2D)>;

    core::Result<FrameStatus> drawFrame(const Swapchain& swapchain, const std::array<float, 4>& clearColor,
                                        const RecordFunction& beforePass, const RecordFunction& scenePass,
                                        const RecordFunction& compositePass);
    void waitIdle() const;

private:
    struct FrameResources {
        VkCommandPool commandPool = VK_NULL_HANDLE;
        VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
        VkSemaphore imageAvailable = VK_NULL_HANDLE;
        VkFence inFlight = VK_NULL_HANDLE;
    };

    Renderer() = default;
    void destroy();
    void releasePresentSemaphores();

    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue queue_ = VK_NULL_HANDLE;
    std::vector<FrameResources> frames_;
    std::vector<VkSemaphore> presentReady_;
    GpuImage hdrTarget_;
    uint32_t frameIndex_ = 0;
};

}
