#pragma once

#include <array>
#include <cstdint>
#include <filesystem>
#include <string>

#include "core/Result.h"

namespace core {

struct WindowConfig {
    uint32_t width;
    uint32_t height;
    std::string title;
    bool vsync;
};

struct RendererConfig {
    bool validation;
    std::string preferredDevice;
    uint32_t framesInFlight;
    std::array<float, 4> clearColor;
};

struct SceneConfig {
    std::string path;
};

struct Config {
    WindowConfig window;
    RendererConfig renderer;
    SceneConfig scene;
};

Result<Config> loadConfig(const std::filesystem::path& file);

}
