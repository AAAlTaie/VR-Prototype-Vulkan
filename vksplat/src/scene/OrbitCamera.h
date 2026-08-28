#pragma once

#include <glm/glm.hpp>

#include "core/Config.h"
#include "scene/SplatScene.h"

namespace scene {

class OrbitCamera {
public:
    OrbitCamera(const core::CameraConfig& config, const Bounds& bounds);

    void advance(float deltaSeconds);
    glm::mat4 view() const;
    float fieldOfViewDegrees() const { return config_.fieldOfViewDegrees; }
    float nearPlane() const { return config_.nearPlane; }

private:
    core::CameraConfig config_;
    glm::vec3 target_;
    float distance_;
    float angle_ = 0.0f;
};

}
