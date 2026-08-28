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
    glm::mat4 eyeView(uint32_t eye, uint32_t viewCount, float interpupillaryDistance) const;
    float fieldOfViewDegrees() const { return config_.fieldOfViewDegrees; }
    float nearPlane() const { return config_.nearPlane; }
    float distance() const { return distance_; }

private:
    core::CameraConfig config_;
    glm::vec3 target_;
    float distance_;
    float angle_ = 0.0f;
};

}
