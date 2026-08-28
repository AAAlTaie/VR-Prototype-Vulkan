#include "scene/OrbitCamera.h"

#include <glm/gtc/matrix_transform.hpp>

namespace scene {

OrbitCamera::OrbitCamera(const core::CameraConfig& config, const Bounds& bounds)
    : config_(config),
      target_(bounds.centre()),
      distance_(bounds.radius() * config.distanceMultiplier) {}

void OrbitCamera::advance(float deltaSeconds) {
    angle_ += glm::radians(config_.orbitDegreesPerSecond) * deltaSeconds;
}

glm::mat4 OrbitCamera::view() const {
    const float elevation = glm::radians(config_.elevationDegrees);
    const glm::vec3 offset{std::cos(angle_) * std::cos(elevation), std::sin(elevation),
                           std::sin(angle_) * std::cos(elevation)};

    return glm::lookAt(target_ + offset * distance_, target_, glm::vec3{0.0f, 1.0f, 0.0f});
}

glm::mat4 OrbitCamera::eyeView(uint32_t eye, uint32_t viewCount, float interpupillaryDistance) const {
    if (viewCount < 2) {
        return view();
    }

    const float offset = (eye == 0 ? -0.5f : 0.5f) * interpupillaryDistance;
    return glm::translate(glm::mat4(1.0f), glm::vec3{-offset, 0.0f, 0.0f}) * view();
}

}
