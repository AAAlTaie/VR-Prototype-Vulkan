#include "platform/Window.h"

#include <utility>

#include <GLFW/glfw3.h>

namespace platform {
namespace {

class GlfwRuntime {
public:
    static core::Result<bool> ensureInitialized() {
        static GlfwRuntime runtime;
        if (!runtime.initialized_) {
            return core::Error{"glfwInit failed"};
        }
        return true;
    }

private:
    GlfwRuntime() : initialized_(glfwInit() == GLFW_TRUE) {}
    ~GlfwRuntime() {
        if (initialized_) {
            glfwTerminate();
        }
    }

    bool initialized_ = false;
};

}

core::Result<Window> Window::create(const core::WindowConfig& config) {
    const auto runtime = GlfwRuntime::ensureInitialized();
    if (!runtime) {
        return core::Error{runtime.error()};
    }

    if (glfwVulkanSupported() != GLFW_TRUE) {
        return core::Error{"no Vulkan loader available to GLFW"};
    }

    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);

    GLFWwindow* handle = glfwCreateWindow(static_cast<int>(config.width), static_cast<int>(config.height),
                                          config.title.c_str(), nullptr, nullptr);
    if (!handle) {
        return core::Error{"glfwCreateWindow failed"};
    }

    return Window(handle);
}

Window::Window(Window&& other) noexcept : handle_(std::exchange(other.handle_, nullptr)) {}

Window& Window::operator=(Window&& other) noexcept {
    if (this != &other) {
        if (handle_) {
            glfwDestroyWindow(handle_);
        }
        handle_ = std::exchange(other.handle_, nullptr);
    }
    return *this;
}

Window::~Window() {
    if (handle_) {
        glfwDestroyWindow(handle_);
    }
}

bool Window::shouldClose() const {
    return glfwWindowShouldClose(handle_) == GLFW_TRUE;
}

void Window::pollEvents() const {
    glfwPollEvents();
}

VkExtent2D Window::framebufferExtent() const {
    int width = 0;
    int height = 0;
    glfwGetFramebufferSize(handle_, &width, &height);
    return {static_cast<uint32_t>(width), static_cast<uint32_t>(height)};
}

VkExtent2D Window::waitForNonZeroExtent() const {
    VkExtent2D extent = framebufferExtent();
    while (extent.width == 0 || extent.height == 0) {
        glfwWaitEvents();
        extent = framebufferExtent();
    }
    return extent;
}

core::Result<VkSurfaceKHR> Window::createSurface(VkInstance instance) const {
    VkSurfaceKHR surface = VK_NULL_HANDLE;
    if (glfwCreateWindowSurface(instance, handle_, nullptr, &surface) != VK_SUCCESS) {
        return core::Error{"glfwCreateWindowSurface failed"};
    }
    return surface;
}

}
