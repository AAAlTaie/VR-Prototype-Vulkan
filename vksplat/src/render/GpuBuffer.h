#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/Result.h"

namespace render {

class GpuBuffer {
public:
    GpuBuffer() = default;

    static core::Result<GpuBuffer> createDeviceLocal(VmaAllocator allocator, VkDeviceSize size,
                                                     VkBufferUsageFlags usage);
    static core::Result<GpuBuffer> createStaging(VmaAllocator allocator, VkDeviceSize size);
    static core::Result<GpuBuffer> createReadback(VmaAllocator allocator, VkDeviceSize size);

    GpuBuffer(GpuBuffer&& other) noexcept;
    GpuBuffer& operator=(GpuBuffer&& other) noexcept;
    GpuBuffer(const GpuBuffer&) = delete;
    GpuBuffer& operator=(const GpuBuffer&) = delete;
    ~GpuBuffer();

    VkBuffer handle() const { return buffer_; }
    VkDeviceSize size() const { return size_; }
    void* mappedData() const { return info_.pMappedData; }
    VkDeviceAddress deviceAddress(VkDevice device) const;

private:
    void destroy();

    static core::Result<GpuBuffer> allocate(VmaAllocator allocator, VkDeviceSize size,
                                            VkBufferUsageFlags usage, VmaMemoryUsage memoryUsage,
                                            VmaAllocationCreateFlags flags);

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkBuffer buffer_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
    VmaAllocationInfo info_{};
    VkDeviceSize size_ = 0;
};

}
