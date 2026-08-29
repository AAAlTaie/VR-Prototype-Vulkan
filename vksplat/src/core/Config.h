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
    bool logStatistics;
    float splatExtentSigma;
};

struct StereoConfig {
    bool enabled;
    float interpupillaryDistance;
};

struct TonemapConfig {
    float exposure;
    uint32_t operatorIndex;
};

struct CameraConfig {
    float fieldOfViewDegrees;
    float nearPlane;
    float farPlane;
    float orbitDegreesPerSecond;
    float elevationDegrees;
    float distanceMultiplier;
};

struct SceneConfig {
    std::string path;
};

struct Config {
    WindowConfig window;
    RendererConfig renderer;
    StereoConfig stereo;
    TonemapConfig tonemap;
    CameraConfig camera;
    SceneConfig scene;
};

Result<Config> loadConfig(const std::filesystem::path& file);

}
