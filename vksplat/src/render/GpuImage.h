#pragma once

#include <volk.h>

#include <vk_mem_alloc.h>

#include "core/Result.h"

namespace render {

class GpuImage {
public:
    GpuImage() = default;

    static core::Result<GpuImage> createColourTarget(VmaAllocator allocator, VkDevice device,
                                                     VkFormat format, VkExtent2D extent);

    GpuImage(GpuImage&& other) noexcept;
    GpuImage& operator=(GpuImage&& other) noexcept;
    GpuImage(const GpuImage&) = delete;
    GpuImage& operator=(const GpuImage&) = delete;
    ~GpuImage();

    VkImage handle() const { return image_; }
    VkImageView view() const { return view_; }

private:
    void destroy();

    VmaAllocator allocator_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkImage image_ = VK_NULL_HANDLE;
    VkImageView view_ = VK_NULL_HANDLE;
    VmaAllocation allocation_ = VK_NULL_HANDLE;
};

}
