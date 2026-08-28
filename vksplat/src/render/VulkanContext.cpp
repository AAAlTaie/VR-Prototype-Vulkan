#include "render/VulkanContext.h"

#include <utility>

#include "core/Logger.h"

namespace render {
namespace {

VkBool32 VKAPI_PTR forwardValidationMessage(VkDebugUtilsMessageSeverityFlagBitsEXT severity,
                                            VkDebugUtilsMessageTypeFlagsEXT,
                                            const VkDebugUtilsMessengerCallbackDataEXT* data, void*) {
    if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
        spdlog::error("[vulkan] {}", data->pMessage);
    } else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
        spdlog::warn("[vulkan] {}", data->pMessage);
    } else {
        spdlog::debug("[vulkan] {}", data->pMessage);
    }
    return VK_FALSE;
}

}

core::Result<VulkanContext> VulkanContext::create(const platform::Window& window,
                                                  const core::RendererConfig& config) {
    if (volkInitialize() != VK_SUCCESS) {
        return core::Error{"volkInitialize failed: no Vulkan loader found"};
    }

    auto instanceResult = vkb::InstanceBuilder()
                              .set_app_name("vksplat")
                              .require_api_version(1, 3, 0)
                              .request_validation_layers(config.validation)
                              .set_debug_callback(forwardValidationMessage)
                              .build();
    if (!instanceResult) {
        return core::Error{"instance creation failed: " + instanceResult.error().message()};
    }

    VulkanContext context;
    context.instance_ = instanceResult.value();
    context.owning_ = true;
    volkLoadInstanceOnly(context.instance_.instance);

    auto surfaceResult = window.createSurface(context.instance_.instance);
    if (!surfaceResult) {
        return core::Error{surfaceResult.error()};
    }
    context.surface_ = surfaceResult.value();

    VkPhysicalDeviceVulkan13Features features13{};
    features13.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES;
    features13.dynamicRendering = VK_TRUE;
    features13.synchronization2 = VK_TRUE;
    features13.shaderDemoteToHelperInvocation = VK_TRUE;

    VkPhysicalDeviceVulkan11Features features11{};
    features11.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_1_FEATURES;
    features11.multiview = VK_TRUE;

    VkPhysicalDeviceVulkan12Features features12{};
    features12.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES;
    features12.bufferDeviceAddress = VK_TRUE;
    features12.scalarBlockLayout = VK_TRUE;

    VkPhysicalDeviceFeatures features{};
    features.shaderInt64 = VK_TRUE;

    vkb::PhysicalDeviceSelector selector(context.instance_, context.surface_);
    selector.add_required_extension(VK_KHR_PUSH_DESCRIPTOR_EXTENSION_NAME);
    selector.set_minimum_version(1, 3)
        .set_required_features_13(features13)
        .set_required_features_12(features12)
        .set_required_features_11(features11)
        .set_required_features(features);
    if (!config.preferredDevice.empty()) {
        selector.set_name(config.preferredDevice);
    }

    auto physicalResult = selector.select();
    if (!physicalResult) {
        return core::Error{"no suitable GPU: " + physicalResult.error().message()};
    }

    auto deviceResult = vkb::DeviceBuilder(physicalResult.value()).build();
    if (!deviceResult) {
        return core::Error{"device creation failed: " + deviceResult.error().message()};
    }

    context.device_ = deviceResult.value();
    volkLoadDevice(context.device_.device);

    auto queueResult = context.device_.get_queue(vkb::QueueType::graphics);
    auto familyResult = context.device_.get_queue_index(vkb::QueueType::graphics);
    if (!queueResult || !familyResult) {
        return core::Error{"no graphics queue available"};
    }

    context.graphicsQueue_ = queueResult.value();
    context.graphicsQueueFamily_ = familyResult.value();
    context.deviceName_ = physicalResult.value().name;

    VmaVulkanFunctions functions{};
    functions.vkGetInstanceProcAddr = vkGetInstanceProcAddr;
    functions.vkGetDeviceProcAddr = vkGetDeviceProcAddr;

    VmaAllocatorCreateInfo allocatorInfo{};
    allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
    allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
    allocatorInfo.instance = context.instance_.instance;
    allocatorInfo.physicalDevice = physicalResult.value().physical_device;
    allocatorInfo.device = context.device_.device;
    allocatorInfo.pVulkanFunctions = &functions;

    if (vmaCreateAllocator(&allocatorInfo, &context.allocator_) != VK_SUCCESS) {
        return core::Error{"vmaCreateAllocator failed"};
    }

    return core::Result<VulkanContext>(std::move(context));
}

VulkanContext::VulkanContext(VulkanContext&& other) noexcept
    : instance_(other.instance_),
      surface_(std::exchange(other.surface_, VK_NULL_HANDLE)),
      device_(other.device_),
      graphicsQueue_(std::exchange(other.graphicsQueue_, VK_NULL_HANDLE)),
      graphicsQueueFamily_(other.graphicsQueueFamily_),
      deviceName_(std::move(other.deviceName_)),
      allocator_(std::exchange(other.allocator_, VK_NULL_HANDLE)),
      owning_(std::exchange(other.owning_, false)) {}

VulkanContext& VulkanContext::operator=(VulkanContext&& other) noexcept {
    if (this != &other) {
        destroy();
        instance_ = other.instance_;
        surface_ = std::exchange(other.surface_, VK_NULL_HANDLE);
        device_ = other.device_;
        graphicsQueue_ = std::exchange(other.graphicsQueue_, VK_NULL_HANDLE);
        graphicsQueueFamily_ = other.graphicsQueueFamily_;
        deviceName_ = std::move(other.deviceName_);
        allocator_ = std::exchange(other.allocator_, VK_NULL_HANDLE);
        owning_ = std::exchange(other.owning_, false);
    }
    return *this;
}

VulkanContext::~VulkanContext() {
    destroy();
}

void VulkanContext::destroy() {
    if (!owning_) {
        return;
    }
    if (allocator_ != VK_NULL_HANDLE) {
        vmaDestroyAllocator(allocator_);
        allocator_ = VK_NULL_HANDLE;
    }
    if (device_.device != VK_NULL_HANDLE) {
        vkb::destroy_device(device_);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkb::destroy_surface(instance_, surface_);
    }
    vkb::destroy_instance(instance_);
    owning_ = false;
}

}
