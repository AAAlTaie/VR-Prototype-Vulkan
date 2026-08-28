#pragma once

#include <filesystem>
#include <vector>

#include <glm/glm.hpp>

#include "core/Result.h"

namespace scene {

struct Splat {
    glm::vec3 position;
    float opacity;
    glm::vec3 scale;
    float padding;
    glm::vec4 rotation;
    glm::vec3 colour;
    float padding2;
};

static_assert(sizeof(Splat) == 64, "Splat must stay 64 bytes for std430 alignment");

struct Bounds {
    glm::vec3 minimum;
    glm::vec3 maximum;

    glm::vec3 centre() const { return (minimum + maximum) * 0.5f; }
    float radius() const { return glm::length(maximum - minimum) * 0.5f; }
};

struct SplatScene {
    std::vector<Splat> splats;
    Bounds bounds;
    uint32_t sphericalHarmonicCoefficients = 0;

    size_t byteSize() const { return splats.size() * sizeof(Splat); }
};

core::Result<SplatScene> loadSplatPly(const std::filesystem::path& file);

}
