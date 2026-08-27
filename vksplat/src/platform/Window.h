#pragma once

#include <volk.h>

#include "core/Config.h"
#include "core/Result.h"

struct GLFWwindow;

namespace platform {

class Window {
public:
    static core::Result<Window> create(const core::WindowConfig& config);

    Window(Window&& other) noexcept;
    Window& operator=(Window&& other) noexcept;
    Window(const Window&) = delete;
    Window& operator=(const Window&) = delete;
    ~Window();

    bool shouldClose() const;
    void pollEvents() const;
    VkExtent2D framebufferExtent() const;
    VkExtent2D waitForNonZeroExtent() const;
    core::Result<VkSurfaceKHR> createSurface(VkInstance instance) const;

private:
    explicit Window(GLFWwindow* handle) : handle_(handle) {}

    GLFWwindow* handle_ = nullptr;
};

}
