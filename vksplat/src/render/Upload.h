#pragma once

#include "render/GpuBuffer.h"
#include "render/VulkanContext.h"

namespace render {

core::Result<GpuBuffer> uploadDeviceLocal(const VulkanContext& context, const void* data, size_t size,
                                          VkBufferUsageFlags usage);

}
