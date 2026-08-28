#include "render/GpuBuffer.h"

#include <utility>

namespace render {

core::Result<GpuBuffer> GpuBuffer::createDeviceLocal(VmaAllocator allocator, VkDeviceSize size,
                                                     VkBufferUsageFlags usage) {
    return allocate(allocator, size, usage | VK_BUFFER_USAGE_TRANSFER_DST_BIT,
                    VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE, 0);
}

core::Result<GpuBuffer> GpuBuffer::createStaging(VmaAllocator allocator, VkDeviceSize size) {
    return allocate(allocator, size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VMA_MEMORY_USAGE_AUTO,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT |
                        VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

core::Result<GpuBuffer> GpuBuffer::createReadback(VmaAllocator allocator, VkDeviceSize size) {
    return allocate(allocator, size, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VMA_MEMORY_USAGE_AUTO,
                    VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT);
}

core::Result<GpuBuffer> GpuBuffer::allocate(VmaAllocator allocator, VkDeviceSize size,
                                            VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                                            VmaAllocationCreateFlags flags) {
    VkBufferCreateInfo bufferInfo{};
    bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bufferInfo.size = size;
    bufferInfo.usage = usage;
    bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;

    VmaAllocationCreateInfo allocationInfo{};
    allocationInfo.usage = memoryUsage;
    allocationInfo.flags = flags;

    GpuBuffer buffer;
    buffer.allocator_ = allocator;
    buffer.size_ = size;

    if (vmaCreateBuffer(allocator, &bufferInfo, &allocationInfo, &buffer.buffer_, &buffer.allocation_,
                        &buffer.info_) != VK_SUCCESS) {
        return core::Error{"vmaCreateBuffer failed for " + std::to_string(size) + " bytes"};
    }
    return core::Result<GpuBuffer>(std::move(buffer));
}

VkDeviceAddress GpuBuffer::deviceAddress(VkDevice device) const {
    VkBufferDeviceAddressInfo info{};
    info.sType = VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO;
    info.buffer = buffer_;
    return vkGetBufferDeviceAddress(device, &info);
}

GpuBuffer::GpuBuffer(GpuBuffer&& other) noexcept
    : allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      buffer_(std::exchange(other.buffer_, VK_NULL_HANDLE)),
      allocation_(std::exchange(other.allocation_, VK_NULL_HANDLE)),
      info_(other.info_),
      size_(std::exchange(other.size_, 0)) {}

GpuBuffer& GpuBuffer::operator=(GpuBuffer&& other) noexcept {
    if (this != &other) {
        destroy();
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        buffer_ = std::exchange(other.buffer_, VK_NULL_HANDLE);
        allocation_ = std::exchange(other.allocation_, VK_NULL_HANDLE);
        info_ = other.info_;
        size_ = std::exchange(other.size_, 0);
    }
    return *this;
}

GpuBuffer::~GpuBuffer() {
    destroy();
}

void GpuBuffer::destroy() {
    if (buffer_ != VK_NULL_HANDLE) {
        vmaDestroyBuffer(allocator_, buffer_, allocation_);
        buffer_ = VK_NULL_HANDLE;
        allocation_ = VK_NULL_HANDLE;
    }
}

}
