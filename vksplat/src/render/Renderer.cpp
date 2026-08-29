#include "render/Renderer.h"

#include <utility>

namespace render {
namespace {

VkImageMemoryBarrier2 makeLayoutBarrier(VkImage image, uint32_t layers, VkImageLayout oldLayout,
                                        VkImageLayout newLayout,
                                        VkPipelineStageFlags2 sourceStage, VkAccessFlags2 sourceAccess,
                                        VkPipelineStageFlags2 destinationStage,
                                        VkAccessFlags2 destinationAccess) {
    VkImageMemoryBarrier2 barrier{};
    barrier.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
    barrier.srcStageMask = sourceStage;
    barrier.srcAccessMask = sourceAccess;
    barrier.dstStageMask = destinationStage;
    barrier.dstAccessMask = destinationAccess;
    barrier.oldLayout = oldLayout;
    barrier.newLayout = newLayout;
    barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    barrier.image = image;
    barrier.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, layers};
    return barrier;
}

void submitBarrier(VkCommandBuffer commandBuffer, const VkImageMemoryBarrier2& barrier) {
    VkDependencyInfo dependency{};
    dependency.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
    dependency.imageMemoryBarrierCount = 1;
    dependency.pImageMemoryBarriers = &barrier;
    vkCmdPipelineBarrier2(commandBuffer, &dependency);
}

void beginColourPass(VkCommandBuffer commandBuffer, VkImageView view, VkExtent2D extent,
                     const std::array<float, 4>& color, uint32_t viewMask) {
    VkRenderingAttachmentInfo attachment{};
    attachment.sType = VK_STRUCTURE_TYPE_RENDERING_ATTACHMENT_INFO;
    attachment.imageView = view;
    attachment.imageLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
    attachment.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachment.storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachment.clearValue.color = {{color[0], color[1], color[2], color[3]}};

    VkRenderingInfo rendering{};
    rendering.sType = VK_STRUCTURE_TYPE_RENDERING_INFO;
    rendering.renderArea = {{0, 0}, extent};
    rendering.layerCount = 1;
    rendering.viewMask = viewMask;
    rendering.colorAttachmentCount = 1;
    rendering.pColorAttachments = &attachment;

    vkCmdBeginRendering(commandBuffer, &rendering);
}

}

core::Result<Renderer> Renderer::create(const VulkanContext& context, uint32_t framesInFlight,
                                       uint32_t viewCount) {
    Renderer renderer;
    renderer.viewCount_ = viewCount;
    renderer.device_ = context.device();
    renderer.queue_ = context.graphicsQueue();
    renderer.frames_.resize(framesInFlight);

    VkCommandPoolCreateInfo poolInfo{};
    poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    poolInfo.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    poolInfo.queueFamilyIndex = context.graphicsQueueFamily();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    VkFenceCreateInfo fenceInfo{};
    fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fenceInfo.flags = VK_FENCE_CREATE_SIGNALED_BIT;

    for (FrameResources& frame : renderer.frames_) {
        if (vkCreateCommandPool(renderer.device_, &poolInfo, nullptr, &frame.commandPool) != VK_SUCCESS) {
            return core::Error{"vkCreateCommandPool failed"};
        }

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = frame.commandPool;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;

        if (vkAllocateCommandBuffers(renderer.device_, &allocateInfo, &frame.commandBuffer) != VK_SUCCESS) {
            return core::Error{"vkAllocateCommandBuffers failed"};
        }
        if (vkCreateSemaphore(renderer.device_, &semaphoreInfo, nullptr, &frame.imageAvailable) != VK_SUCCESS) {
            return core::Error{"vkCreateSemaphore failed"};
        }
        if (vkCreateFence(renderer.device_, &fenceInfo, nullptr, &frame.inFlight) != VK_SUCCESS) {
            return core::Error{"vkCreateFence failed"};
        }
    }

    auto profiler = GpuProfiler::create(context, framesInFlight, 16);
    if (profiler) {
        renderer.profiler_ = profiler.take();
    }

    return core::Result<Renderer>(std::move(renderer));
}

core::Result<bool> Renderer::bindSwapchain(const VulkanContext& context, const Swapchain& swapchain) {
    releasePresentSemaphores();

    hdrExtent_ = {swapchain.extent().width / viewCount_, swapchain.extent().height};
    auto target = GpuImage::createColourTarget(context.allocator(), device_, kHdrFormat, hdrExtent_,
                                               viewCount_);
    if (!target) {
        return core::Error{target.error()};
    }
    hdrTarget_ = target.take();

    VkSemaphoreCreateInfo semaphoreInfo{};
    semaphoreInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;

    presentReady_.resize(swapchain.imageCount(), VK_NULL_HANDLE);
    for (VkSemaphore& semaphore : presentReady_) {
        if (vkCreateSemaphore(device_, &semaphoreInfo, nullptr, &semaphore) != VK_SUCCESS) {
            return core::Error{"vkCreateSemaphore failed for present signal"};
        }
    }
    return true;
}

core::Result<FrameStatus> Renderer::drawFrame(const Swapchain& swapchain,
                                              const std::array<float, 4>& clearColor,
                                              const RecordFunction& beforePass,
                                              const RecordFunction& scenePass,
                                              const RecordFunction& compositePass) {
    FrameResources& frame = frames_[frameIndex_];

    if (vkWaitForFences(device_, 1, &frame.inFlight, VK_TRUE, UINT64_MAX) != VK_SUCCESS) {
        return core::Error{"vkWaitForFences failed"};
    }

    uint32_t imageIndex = 0;
    const VkResult acquired = vkAcquireNextImageKHR(device_, swapchain.handle(), UINT64_MAX,
                                                    frame.imageAvailable, VK_NULL_HANDLE, &imageIndex);
    if (acquired == VK_ERROR_OUT_OF_DATE_KHR) {
        return FrameStatus::SwapchainOutOfDate;
    }
    if (acquired != VK_SUCCESS && acquired != VK_SUBOPTIMAL_KHR) {
        return core::Error{"vkAcquireNextImageKHR failed"};
    }

    profiler_.collect(frameIndex_);
    vkResetFences(device_, 1, &frame.inFlight);
    vkResetCommandPool(device_, frame.commandPool, 0);

    VkCommandBufferBeginInfo beginInfo{};
    beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    vkBeginCommandBuffer(frame.commandBuffer, &beginInfo);
    profiler_.beginFrame(frame.commandBuffer, frameIndex_);
    const VkExtent2D extent = swapchain.extent();

    if (beforePass) {
        beforePass(frame.commandBuffer, extent);
    }

    submitBarrier(frame.commandBuffer,
                  makeLayoutBarrier(hdrTarget_.handle(), viewCount_, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));

    beginColourPass(frame.commandBuffer, hdrTarget_.view(), hdrExtent_, clearColor,
                    (1u << viewCount_) - 1u);
    if (scenePass) {
        scenePass(frame.commandBuffer, hdrExtent_);
    }
    vkCmdEndRendering(frame.commandBuffer);
    profiler_.stamp(frame.commandBuffer, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, "raster");

    submitBarrier(frame.commandBuffer,
                  makeLayoutBarrier(hdrTarget_.handle(), viewCount_,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
                                    VK_ACCESS_2_SHADER_SAMPLED_READ_BIT));

    submitBarrier(frame.commandBuffer,
                  makeLayoutBarrier(swapchain.image(imageIndex), 1, VK_IMAGE_LAYOUT_UNDEFINED,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT, 0,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT));

    beginColourPass(frame.commandBuffer, swapchain.imageView(imageIndex), extent, clearColor, 0);
    if (compositePass) {
        compositePass(frame.commandBuffer, extent);
    }
    vkCmdEndRendering(frame.commandBuffer);
    profiler_.stamp(frame.commandBuffer, VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, "tonemap");

    submitBarrier(frame.commandBuffer,
                  makeLayoutBarrier(swapchain.image(imageIndex), 1,
                                    VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
                                    VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
                                    VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
                                    VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
                                    VK_PIPELINE_STAGE_2_BOTTOM_OF_PIPE_BIT, 0));
    if (vkEndCommandBuffer(frame.commandBuffer) != VK_SUCCESS) {
        return core::Error{"vkEndCommandBuffer failed"};
    }

    VkSemaphoreSubmitInfo waitInfo{};
    waitInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    waitInfo.semaphore = frame.imageAvailable;
    waitInfo.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;

    VkSemaphoreSubmitInfo signalInfo{};
    signalInfo.sType = VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO;
    signalInfo.semaphore = presentReady_[imageIndex];
    signalInfo.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;

    VkCommandBufferSubmitInfo commandInfo{};
    commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
    commandInfo.commandBuffer = frame.commandBuffer;

    VkSubmitInfo2 submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
    submit.waitSemaphoreInfoCount = 1;
    submit.pWaitSemaphoreInfos = &waitInfo;
    submit.commandBufferInfoCount = 1;
    submit.pCommandBufferInfos = &commandInfo;
    submit.signalSemaphoreInfoCount = 1;
    submit.pSignalSemaphoreInfos = &signalInfo;

    if (vkQueueSubmit2(queue_, 1, &submit, frame.inFlight) != VK_SUCCESS) {
        return core::Error{"vkQueueSubmit2 failed"};
    }

    const VkSwapchainKHR handle = swapchain.handle();
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &presentReady_[imageIndex];
    present.swapchainCount = 1;
    present.pSwapchains = &handle;
    present.pImageIndices = &imageIndex;

    const VkResult presented = vkQueuePresentKHR(queue_, &present);
    frameIndex_ = (frameIndex_ + 1) % static_cast<uint32_t>(frames_.size());

    if (presented == VK_ERROR_OUT_OF_DATE_KHR || presented == VK_SUBOPTIMAL_KHR) {
        return FrameStatus::SwapchainOutOfDate;
    }
    if (presented != VK_SUCCESS) {
        return core::Error{"vkQueuePresentKHR failed"};
    }
    return FrameStatus::Presented;
}

void Renderer::waitIdle() const {
    if (device_ != VK_NULL_HANDLE) {
        vkDeviceWaitIdle(device_);
    }
}

Renderer::Renderer(Renderer&& other) noexcept
    : device_(std::exchange(other.device_, VK_NULL_HANDLE)),
      queue_(std::exchange(other.queue_, VK_NULL_HANDLE)),
      frames_(std::move(other.frames_)),
      presentReady_(std::move(other.presentReady_)),
      hdrTarget_(std::move(other.hdrTarget_)),
      profiler_(std::move(other.profiler_)),
      hdrExtent_(other.hdrExtent_),
      viewCount_(other.viewCount_),
      frameIndex_(other.frameIndex_) {
    other.frames_.clear();
    other.presentReady_.clear();
}

Renderer& Renderer::operator=(Renderer&& other) noexcept {
    if (this != &other) {
        destroy();
        device_ = std::exchange(other.device_, VK_NULL_HANDLE);
        queue_ = std::exchange(other.queue_, VK_NULL_HANDLE);
        frames_ = std::move(other.frames_);
        presentReady_ = std::move(other.presentReady_);
        hdrTarget_ = std::move(other.hdrTarget_);
        profiler_ = std::move(other.profiler_);
        hdrExtent_ = other.hdrExtent_;
        viewCount_ = other.viewCount_;
        frameIndex_ = other.frameIndex_;
        other.frames_.clear();
        other.presentReady_.clear();
    }
    return *this;
}

Renderer::~Renderer() {
    destroy();
}

void Renderer::releasePresentSemaphores() {
    for (VkSemaphore semaphore : presentReady_) {
        if (semaphore != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, semaphore, nullptr);
        }
    }
    presentReady_.clear();
}

void Renderer::destroy() {
    if (device_ == VK_NULL_HANDLE) {
        return;
    }
    releasePresentSemaphores();
    for (FrameResources& frame : frames_) {
        if (frame.inFlight != VK_NULL_HANDLE) {
            vkDestroyFence(device_, frame.inFlight, nullptr);
        }
        if (frame.imageAvailable != VK_NULL_HANDLE) {
            vkDestroySemaphore(device_, frame.imageAvailable, nullptr);
        }
        if (frame.commandPool != VK_NULL_HANDLE) {
            vkDestroyCommandPool(device_, frame.commandPool, nullptr);
        }
    }
    frames_.clear();
    device_ = VK_NULL_HANDLE;
}

}
