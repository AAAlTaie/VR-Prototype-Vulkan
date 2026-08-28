#include "render/Upload.h"

#include <cstring>

namespace render {
namespace {

class TransientCommands {
public:
    TransientCommands(VkDevice device, uint32_t queueFamily) : device_(device) {
        VkCommandPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        poolInfo.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT;
        poolInfo.queueFamilyIndex = queueFamily;
        vkCreateCommandPool(device_, &poolInfo, nullptr, &pool_);

        VkCommandBufferAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateInfo.commandPool = pool_;
        allocateInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateInfo.commandBufferCount = 1;
        vkAllocateCommandBuffers(device_, &allocateInfo, &commandBuffer_);

        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        vkCreateFence(device_, &fenceInfo, nullptr, &fence_);
    }

    ~TransientCommands() {
        vkDestroyFence(device_, fence_, nullptr);
        vkDestroyCommandPool(device_, pool_, nullptr);
    }

    TransientCommands(const TransientCommands&) = delete;
    TransientCommands& operator=(const TransientCommands&) = delete;

    bool valid() const {
        return pool_ != VK_NULL_HANDLE && commandBuffer_ != VK_NULL_HANDLE && fence_ != VK_NULL_HANDLE;
    }

    VkCommandBuffer begin() const {
        VkCommandBufferBeginInfo beginInfo{};
        beginInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        beginInfo.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commandBuffer_, &beginInfo);
        return commandBuffer_;
    }

    bool submitAndWait(VkQueue queue) const {
        if (vkEndCommandBuffer(commandBuffer_) != VK_SUCCESS) {
            return false;
        }

        VkCommandBufferSubmitInfo commandInfo{};
        commandInfo.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO;
        commandInfo.commandBuffer = commandBuffer_;

        VkSubmitInfo2 submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO_2;
        submit.commandBufferInfoCount = 1;
        submit.pCommandBufferInfos = &commandInfo;

        if (vkQueueSubmit2(queue, 1, &submit, fence_) != VK_SUCCESS) {
            return false;
        }
        return vkWaitForFences(device_, 1, &fence_, VK_TRUE, UINT64_MAX) == VK_SUCCESS;
    }

private:
    VkDevice device_ = VK_NULL_HANDLE;
    VkCommandPool pool_ = VK_NULL_HANDLE;
    VkCommandBuffer commandBuffer_ = VK_NULL_HANDLE;
    VkFence fence_ = VK_NULL_HANDLE;
};

}

core::Result<GpuBuffer> uploadDeviceLocal(const VulkanContext& context, const void* data, size_t size,
                                          VkBufferUsageFlags usage) {
    auto staging = GpuBuffer::createStaging(context.allocator(), size);
    if (!staging) {
        return core::Error{staging.error()};
    }
    std::memcpy(staging.value().mappedData(), data, size);

    auto destination = GpuBuffer::createDeviceLocal(context.allocator(), size, usage);
    if (!destination) {
        return core::Error{destination.error()};
    }

    TransientCommands commands(context.device(), context.graphicsQueueFamily());
    if (!commands.valid()) {
        return core::Error{"failed to create transient upload commands"};
    }

    const VkCommandBuffer commandBuffer = commands.begin();
    VkBufferCopy region{};
    region.size = size;
    vkCmdCopyBuffer(commandBuffer, staging.value().handle(), destination.value().handle(), 1, &region);

    if (!commands.submitAndWait(context.graphicsQueue())) {
        return core::Error{"buffer upload submission failed"};
    }
    return core::Result<GpuBuffer>(destination.take());
}

}
