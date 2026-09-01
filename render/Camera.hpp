#pragma once

#include <glm/glm.hpp>

#include "optics/scene/NumericalAperture.hpp"

namespace holobench::optics::scene {
using NumericalApertureResult = ObjectSideNumericalApertureResult;
}

namespace holobench::render {

enum class CameraPresetView {
    Perspective,
    TopXZ,
    SideYZ,
    FrontXY
};

class OrbitCamera final {
public:
    OrbitCamera() noexcept;

    void orbit(float deltaYaw, float deltaPitch) noexcept;
    void pan(float deltaX, float deltaY) noexcept;
    void moveLocal(
        float deltaRight,
        float deltaUp,
        float deltaForward) noexcept;
    void zoom(float deltaFactor) noexcept;
    void focusOn(const glm::vec3& target, float framingRadius) noexcept;

    void setViewportSize(int width, int height) noexcept;
    void setPresetView(CameraPresetView preset) noexcept;
    void reset() noexcept;

    [[nodiscard]] glm::mat4 viewMatrix() const noexcept;
    [[nodiscard]] glm::mat4 projectionMatrix() const noexcept;
    [[nodiscard]] glm::mat4 projectionMatrix(float aspect) const noexcept;
    [[nodiscard]] glm::mat4 viewProjectionMatrix() const noexcept;

    [[nodiscard]] glm::vec3 eyePosition() const noexcept;
    [[nodiscard]] glm::vec3 forwardVector() const noexcept;
    [[nodiscard]] glm::vec3 rightVector() const noexcept;
    [[nodiscard]] glm::vec3 upVector() const noexcept;

    [[nodiscard]] const glm::vec3& target() const noexcept {
        return target_;
    }
    [[nodiscard]] float distance() const noexcept {
        return distance_;
    }
    [[nodiscard]] float yaw() const noexcept {
        return yaw_;
    }
    [[nodiscard]] float pitch() const noexcept {
        return pitch_;
    }
    [[nodiscard]] float fovDegrees() const noexcept;
    [[nodiscard]] float nearPlane() const noexcept {
        return nearZ_;
    }
    [[nodiscard]] float farPlane() const noexcept {
        return farZ_;
    }

    void setTarget(const glm::vec3& target) noexcept;
    void setDistance(float distance) noexcept;

private:
    glm::vec3 target_ {0.0F, 0.0F, 0.6F};
    float distance_ = 1.8F;
    float yaw_ = 0.70F;      // ~40 degrees
    float pitch_ = 0.45F;    // ~26 degrees
    float fovY_ = 0.785398F; // 45 degrees
    float nearZ_ = 0.01F;
    float farZ_ = 50.0F;
    int viewportWidth_ = 1280;
    int viewportHeight_ = 720;
};

} // namespace holobench::render
