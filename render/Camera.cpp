#include "render/Camera.hpp"

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include <algorithm>
#include <cmath>

namespace holobench::render {

namespace {

constexpr float kMinDistance = 0.05F;
constexpr float kMaxDistance = 100.0F;
constexpr float kMaxPitch = glm::radians(89.0F);
constexpr float kDefaultFovY = glm::radians(45.0F);

[[nodiscard]] bool isFiniteVec3(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

OrbitCamera::OrbitCamera() noexcept
    : target_(0.0F, 0.0F, 0.6F)
    , distance_(1.8F)
    , yaw_(0.70F)
    , pitch_(0.45F)
    , fovY_(kDefaultFovY)
    , nearZ_(0.01F)
    , farZ_(50.0F)
    , viewportWidth_(1280)
    , viewportHeight_(720) {
}

void OrbitCamera::orbit(float deltaYaw, float deltaPitch) noexcept {
    if (!std::isfinite(deltaYaw) || !std::isfinite(deltaPitch)) {
        return;
    }

    yaw_ += deltaYaw;
    constexpr float pi = glm::pi<float>();
    constexpr float twoPi = 2.0F * pi;
    yaw_ = std::remainder(yaw_, twoPi);
    if (yaw_ > pi) {
        yaw_ -= twoPi;
    } else if (yaw_ < -pi) {
        yaw_ += twoPi;
    }

    pitch_ += deltaPitch;
    pitch_ = std::clamp(pitch_, -kMaxPitch, kMaxPitch);
}

void OrbitCamera::pan(float deltaX, float deltaY) noexcept {
    if (!std::isfinite(deltaX) || !std::isfinite(deltaY)) {
        return;
    }

    const glm::vec3 right = rightVector();
    const glm::vec3 up = upVector();
    const glm::vec3 newTarget = target_ + right * deltaX + up * deltaY;
    if (isFiniteVec3(newTarget)) {
        target_ = newTarget;
    }
}

void OrbitCamera::moveLocal(
    float deltaRight,
    float deltaUp,
    float deltaForward) noexcept {
    if (!std::isfinite(deltaRight) || !std::isfinite(deltaUp)
        || !std::isfinite(deltaForward)) {
        return;
    }
    const glm::vec3 translation = rightVector() * deltaRight
        + upVector() * deltaUp + forwardVector() * deltaForward;
    const glm::vec3 newTarget = target_ + translation;
    if (isFiniteVec3(newTarget)) {
        target_ = newTarget;
    }
}

void OrbitCamera::zoom(float deltaFactor) noexcept {
    if (!std::isfinite(deltaFactor)) {
        return;
    }

    const float newDistance = distance_ * std::exp(-deltaFactor * 0.1F);
    if (!std::isfinite(newDistance)) {
        if (deltaFactor < 0.0F) {
            distance_ = kMaxDistance;
        } else {
            distance_ = kMinDistance;
        }
        return;
    }
    distance_ = std::clamp(newDistance, kMinDistance, kMaxDistance);
}

void OrbitCamera::focusOn(
    const glm::vec3& target,
    float framingRadius) noexcept {
    if (!isFiniteVec3(target) || !std::isfinite(framingRadius)
        || framingRadius <= 0.0F) {
        return;
    }
    const float requiredDistance = 1.25F * framingRadius
        / std::tan(0.5F * fovY_);
    if (!std::isfinite(requiredDistance)) {
        return;
    }
    target_ = target;
    distance_ = std::clamp(requiredDistance, kMinDistance, kMaxDistance);
}

void OrbitCamera::setViewportSize(int width, int height) noexcept {
    if (width > 0 && height > 0) {
        viewportWidth_ = width;
        viewportHeight_ = height;
    }
}

void OrbitCamera::setPresetView(CameraPresetView preset) noexcept {
    switch (preset) {
    case CameraPresetView::Perspective:
        target_ = glm::vec3(0.0F, 0.0F, 0.6F);
        distance_ = 1.8F;
        yaw_ = 0.70F;
        pitch_ = 0.45F;
        fovY_ = kDefaultFovY;
        break;
    case CameraPresetView::TopXZ:
        target_ = glm::vec3(0.0F, 0.0F, 0.6F);
        yaw_ = 0.0F;
        pitch_ = glm::radians(89.0F);
        break;
    case CameraPresetView::SideYZ:
        target_ = glm::vec3(0.0F, 0.0F, 0.6F);
        yaw_ = glm::half_pi<float>();
        pitch_ = 0.0F;
        break;
    case CameraPresetView::FrontXY:
        target_ = glm::vec3(0.0F, 0.0F, 0.6F);
        yaw_ = 0.0F;
        pitch_ = 0.0F;
        break;
    }
}

void OrbitCamera::reset() noexcept {
    setPresetView(CameraPresetView::Perspective);
}

glm::mat4 OrbitCamera::viewMatrix() const noexcept {
    const glm::vec3 eye = eyePosition();
    const glm::vec3 r = rightVector();
    const glm::vec3 u = upVector();
    const glm::vec3 f = forwardVector();

    // OpenGL right-handed view matrix (column-major)
    // Maps world space to view space where +X is right, +Y is up, -Z is forward
    glm::mat4 view(1.0F);
    view[0][0] = r.x;
    view[1][0] = r.y;
    view[2][0] = r.z;
    view[3][0] = -glm::dot(r, eye);

    view[0][1] = u.x;
    view[1][1] = u.y;
    view[2][1] = u.z;
    view[3][1] = -glm::dot(u, eye);

    view[0][2] = -f.x;
    view[1][2] = -f.y;
    view[2][2] = -f.z;
    view[3][2] = glm::dot(f, eye);

    return view;
}

glm::mat4 OrbitCamera::projectionMatrix() const noexcept {
    const float aspect = (viewportHeight_ > 0 && viewportWidth_ > 0)
        ? (static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_))
        : (16.0F / 9.0F);
    return projectionMatrix(aspect);
}

glm::mat4 OrbitCamera::projectionMatrix(float aspect) const noexcept {
    const bool valid = std::isfinite(aspect) && aspect > 0.0F;
    const float chosenAspect = valid
        ? aspect
        : ((viewportHeight_ > 0 && viewportWidth_ > 0)
            ? (static_cast<float>(viewportWidth_) / static_cast<float>(viewportHeight_))
            : (16.0F / 9.0F));
    return glm::perspectiveRH_NO(fovY_, chosenAspect, nearZ_, farZ_);
}

glm::mat4 OrbitCamera::viewProjectionMatrix() const noexcept {
    return projectionMatrix() * viewMatrix();
}

glm::vec3 OrbitCamera::eyePosition() const noexcept {
    const float cosPitch = std::cos(pitch_);
    const float sinPitch = std::sin(pitch_);
    const float cosYaw = std::cos(yaw_);
    const float sinYaw = std::sin(yaw_);

    const glm::vec3 offset(
        sinYaw * cosPitch,
        sinPitch,
        -cosYaw * cosPitch
    );
    return target_ + offset * distance_;
}

glm::vec3 OrbitCamera::forwardVector() const noexcept {
    const float cosPitch = std::cos(pitch_);
    const float sinPitch = std::sin(pitch_);
    const float cosYaw = std::cos(yaw_);
    const float sinYaw = std::sin(yaw_);

    return glm::vec3(
        -sinYaw * cosPitch,
        -sinPitch,
        cosYaw * cosPitch
    );
}

glm::vec3 OrbitCamera::rightVector() const noexcept {
    return glm::vec3(
        std::cos(yaw_),
        0.0F,
        std::sin(yaw_)
    );
}

glm::vec3 OrbitCamera::upVector() const noexcept {
    return glm::cross(forwardVector(), rightVector());
}

float OrbitCamera::fovDegrees() const noexcept {
    return glm::degrees(fovY_);
}

void OrbitCamera::setTarget(const glm::vec3& target) noexcept {
    if (isFiniteVec3(target)) {
        target_ = target;
    }
}

void OrbitCamera::setDistance(float distance) noexcept {
    if (!std::isfinite(distance)) {
        return;
    }
    distance_ = std::clamp(distance, kMinDistance, kMaxDistance);
}

} // namespace holobench::render
