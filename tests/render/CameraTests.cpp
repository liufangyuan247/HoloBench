#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <utility>

#include <glm/gtc/constants.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "render/Camera.hpp"

namespace {

using holobench::render::CameraPresetView;
using holobench::render::OrbitCamera;

constexpr float kEpsilon = 1e-5F;

[[nodiscard]] bool isFiniteMat4(const glm::mat4& m) noexcept {
    for (int col = 0; col < 4; ++col) {
        for (int row = 0; row < 4; ++row) {
            if (!std::isfinite(m[col][row])) {
                return false;
            }
        }
    }
    return true;
}

[[nodiscard]] bool isFiniteVec3(const glm::vec3& v) noexcept {
    return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
}

} // namespace

TEST_CASE("OrbitCamera default state: eye and forward point directly to target") {
    const OrbitCamera camera;

    CHECK(camera.target() == glm::vec3(0.0F, 0.0F, 0.6F));
    CHECK(camera.distance() == doctest::Approx(1.8F));
    CHECK(camera.yaw() == doctest::Approx(0.70F));
    CHECK(camera.pitch() == doctest::Approx(0.45F));
    CHECK(camera.fovDegrees() == doctest::Approx(45.0F).epsilon(1e-4F));
    CHECK(camera.nearPlane() == doctest::Approx(0.01F));
    CHECK(camera.farPlane() == doctest::Approx(50.0F));

    const glm::vec3 eye = camera.eyePosition();
    const glm::vec3 forward = camera.forwardVector();
    const glm::vec3 target = camera.target();

    CHECK(isFiniteVec3(eye));
    CHECK(isFiniteVec3(forward));

    // eye + forward * distance == target
    const glm::vec3 reconstructedTarget = eye + forward * camera.distance();
    CHECK(reconstructedTarget.x == doctest::Approx(target.x).epsilon(kEpsilon));
    CHECK(reconstructedTarget.y == doctest::Approx(target.y).epsilon(kEpsilon));
    CHECK(reconstructedTarget.z == doctest::Approx(target.z).epsilon(kEpsilon));

    // Vector from eye to target must match normalized forward vector with magnitude equal to distance
    const glm::vec3 eyeToTarget = target - eye;
    CHECK(glm::length(eyeToTarget) == doctest::Approx(camera.distance()).epsilon(kEpsilon));

    const glm::vec3 normalizedDir = glm::normalize(eyeToTarget);
    CHECK(normalizedDir.x == doctest::Approx(forward.x).epsilon(kEpsilon));
    CHECK(normalizedDir.y == doctest::Approx(forward.y).epsilon(kEpsilon));
    CHECK(normalizedDir.z == doctest::Approx(forward.z).epsilon(kEpsilon));
}

TEST_CASE("OrbitCamera basis vectors: right, up, and forward are orthonormal and right-handed") {
    OrbitCamera camera;

    const auto checkBasisOrthonormalAndRightHanded = [](const OrbitCamera& cam) {
        const glm::vec3 r = cam.rightVector();
        const glm::vec3 u = cam.upVector();
        const glm::vec3 f = cam.forwardVector();

        CHECK(isFiniteVec3(r));
        CHECK(isFiniteVec3(u));
        CHECK(isFiniteVec3(f));

        // Unit lengths
        CHECK(glm::length(r) == doctest::Approx(1.0F).epsilon(kEpsilon));
        CHECK(glm::length(u) == doctest::Approx(1.0F).epsilon(kEpsilon));
        CHECK(glm::length(f) == doctest::Approx(1.0F).epsilon(kEpsilon));

        // Mutual orthogonality (dot products == 0)
        CHECK(glm::dot(r, u) == doctest::Approx(0.0F).epsilon(kEpsilon));
        CHECK(glm::dot(r, f) == doctest::Approx(0.0F).epsilon(kEpsilon));
        CHECK(glm::dot(u, f) == doctest::Approx(0.0F).epsilon(kEpsilon));

        // Right-handed triad: right x up = forward
        const glm::vec3 crossRU = glm::cross(r, u);
        CHECK(crossRU.x == doctest::Approx(f.x).epsilon(kEpsilon));
        CHECK(crossRU.y == doctest::Approx(f.y).epsilon(kEpsilon));
        CHECK(crossRU.z == doctest::Approx(f.z).epsilon(kEpsilon));
        CHECK(glm::dot(crossRU, f) == doctest::Approx(1.0F).epsilon(kEpsilon));
    };

    SUBCASE("default perspective") {
        checkBasisOrthonormalAndRightHanded(camera);
    }

    SUBCASE("preset views") {
        for (const auto preset : {CameraPresetView::Perspective, CameraPresetView::TopXZ,
                                  CameraPresetView::SideYZ, CameraPresetView::FrontXY}) {
            camera.setPresetView(preset);
            checkBasisOrthonormalAndRightHanded(camera);
        }
    }

    SUBCASE("arbitrary orbit orientations") {
        const float yawAngles[] = {-2.5F, -1.0F, 0.0F, 0.8F, 2.3F};
        const float pitchAngles[] = {-1.4F, -0.6F, 0.0F, 0.5F, 1.4F};
        for (const float y : yawAngles) {
            for (const float p : pitchAngles) {
                camera.reset();
                camera.orbit(y - camera.yaw(), p - camera.pitch());
                checkBasisOrthonormalAndRightHanded(camera);
            }
        }
    }
}

TEST_CASE("OrbitCamera orbit pitch clamping and finite matrix generation") {
    OrbitCamera camera;
    constexpr float kMaxPitch = glm::radians(89.0F);

    SUBCASE("orbit excessive positive pitch clamps safely") {
        camera.orbit(0.0F, 100.0F);
        CHECK(camera.pitch() == doctest::Approx(kMaxPitch).epsilon(kEpsilon));
        CHECK(isFiniteMat4(camera.viewMatrix()));
        CHECK(isFiniteMat4(camera.projectionMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
        CHECK(isFiniteVec3(camera.eyePosition()));
    }

    SUBCASE("orbit excessive negative pitch clamps safely") {
        camera.orbit(0.0F, -100.0F);
        CHECK(camera.pitch() == doctest::Approx(-kMaxPitch).epsilon(kEpsilon));
        CHECK(isFiniteMat4(camera.viewMatrix()));
        CHECK(isFiniteMat4(camera.projectionMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
        CHECK(isFiniteVec3(camera.eyePosition()));
    }

    SUBCASE("orbit yaw wraps within [-pi, pi]") {
        constexpr float pi = glm::pi<float>();
        camera.orbit(20.0F, 0.0F);
        CHECK(camera.yaw() >= -pi);
        CHECK(camera.yaw() <= pi);
        CHECK(isFiniteMat4(camera.viewMatrix()));

        camera.orbit(-40.0F, 0.0F);
        CHECK(camera.yaw() >= -pi);
        CHECK(camera.yaw() <= pi);
        CHECK(isFiniteMat4(camera.viewMatrix()));
    }

    SUBCASE("orbit ignores non-finite deltaYaw and deltaPitch") {
        const float initialYaw = camera.yaw();
        const float initialPitch = camera.pitch();

        camera.orbit(std::numeric_limits<float>::quiet_NaN(), 0.1F);
        CHECK(camera.yaw() == initialYaw);
        CHECK(camera.pitch() == initialPitch);

        camera.orbit(0.1F, std::numeric_limits<float>::infinity());
        CHECK(camera.yaw() == initialYaw);
        CHECK(camera.pitch() == initialPitch);

        CHECK(isFiniteMat4(camera.viewMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
    }
}

TEST_CASE("OrbitCamera zoom distance maintains bounds and monotonicity") {
    OrbitCamera camera;
    constexpr float kMinDistance = 0.05F;
    constexpr float kMaxDistance = 100.0F;

    SUBCASE("zoom in decreases distance monotonically") {
        float prevDistance = camera.distance();
        for (int i = 0; i < 5; ++i) {
            camera.zoom(1.0F);
            CHECK(camera.distance() < prevDistance);
            prevDistance = camera.distance();
        }
    }

    SUBCASE("zoom out increases distance monotonically") {
        float prevDistance = camera.distance();
        for (int i = 0; i < 5; ++i) {
            camera.zoom(-1.0F);
            CHECK(camera.distance() > prevDistance);
            prevDistance = camera.distance();
        }
    }

    SUBCASE("extreme zoom in is clamped at minimum distance") {
        for (int i = 0; i < 100; ++i) {
            camera.zoom(10.0F);
        }
        CHECK(camera.distance() == doctest::Approx(kMinDistance).epsilon(kEpsilon));
    }

    SUBCASE("extreme zoom out is clamped at maximum distance") {
        for (int i = 0; i < 100; ++i) {
            camera.zoom(-10.0F);
        }
        CHECK(camera.distance() == doctest::Approx(kMaxDistance).epsilon(kEpsilon));
    }

    SUBCASE("setDistance enforces range and ignores non-finite values") {
        camera.setDistance(2.5F);
        CHECK(camera.distance() == doctest::Approx(2.5F));

        camera.setDistance(0.001F);
        CHECK(camera.distance() == doctest::Approx(kMinDistance).epsilon(kEpsilon));

        camera.setDistance(500.0F);
        CHECK(camera.distance() == doctest::Approx(kMaxDistance).epsilon(kEpsilon));

        camera.setDistance(3.0F);
        camera.setDistance(std::numeric_limits<float>::quiet_NaN());
        CHECK(camera.distance() == doctest::Approx(3.0F));

        camera.setDistance(std::numeric_limits<float>::infinity());
        CHECK(camera.distance() == doctest::Approx(3.0F));
    }

    SUBCASE("zoom ignores non-finite factor") {
        const float distBefore = camera.distance();
        camera.zoom(std::numeric_limits<float>::quiet_NaN());
        CHECK(camera.distance() == distBefore);

        camera.zoom(std::numeric_limits<float>::infinity());
        CHECK(camera.distance() == distBefore);
    }
}

TEST_CASE("OrbitCamera pan translates target and eye along camera right and up axes") {
    OrbitCamera camera;
    const glm::vec3 initialTarget = camera.target();
    const glm::vec3 initialEye = camera.eyePosition();
    const glm::vec3 r = camera.rightVector();
    const glm::vec3 u = camera.upVector();
    const glm::vec3 f = camera.forwardVector();
    const float initialYaw = camera.yaw();
    const float initialPitch = camera.pitch();
    const float initialDist = camera.distance();

    const float deltaX = 0.45F;
    const float deltaY = -0.30F;
    camera.pan(deltaX, deltaY);

    const glm::vec3 expectedShift = r * deltaX + u * deltaY;

    // Target displacement
    CHECK((camera.target().x - initialTarget.x) == doctest::Approx(expectedShift.x).epsilon(kEpsilon));
    CHECK((camera.target().y - initialTarget.y) == doctest::Approx(expectedShift.y).epsilon(kEpsilon));
    CHECK((camera.target().z - initialTarget.z) == doctest::Approx(expectedShift.z).epsilon(kEpsilon));

    // Eye displacement must match target displacement exactly
    CHECK((camera.eyePosition().x - initialEye.x) == doctest::Approx(expectedShift.x).epsilon(kEpsilon));
    CHECK((camera.eyePosition().y - initialEye.y) == doctest::Approx(expectedShift.y).epsilon(kEpsilon));
    CHECK((camera.eyePosition().z - initialEye.z) == doctest::Approx(expectedShift.z).epsilon(kEpsilon));

    // Camera orientation and distance must remain unchanged after panning
    CHECK(camera.yaw() == doctest::Approx(initialYaw));
    CHECK(camera.pitch() == doctest::Approx(initialPitch));
    CHECK(camera.distance() == doctest::Approx(initialDist));
    CHECK(camera.rightVector().x == doctest::Approx(r.x).epsilon(kEpsilon));
    CHECK(camera.upVector().y == doctest::Approx(u.y).epsilon(kEpsilon));
    CHECK(camera.forwardVector().z == doctest::Approx(f.z).epsilon(kEpsilon));

    SUBCASE("pan rejects non-finite delta inputs") {
        const glm::vec3 savedTarget = camera.target();
        camera.pan(std::numeric_limits<float>::quiet_NaN(), 1.0F);
        CHECK(camera.target() == savedTarget);

        camera.pan(1.0F, std::numeric_limits<float>::infinity());
        CHECK(camera.target() == savedTarget);
    }

    SUBCASE("setTarget handles valid and non-finite values") {
        camera.setTarget(glm::vec3(1.5F, -2.0F, 0.8F));
        CHECK(camera.target() == glm::vec3(1.5F, -2.0F, 0.8F));

        camera.setTarget(glm::vec3(std::numeric_limits<float>::quiet_NaN(), 0.0F, 0.0F));
        CHECK(camera.target() == glm::vec3(1.5F, -2.0F, 0.8F));

        camera.setTarget(glm::vec3(0.0F, std::numeric_limits<float>::infinity(), 0.0F));
        CHECK(camera.target() == glm::vec3(1.5F, -2.0F, 0.8F));
    }
}

TEST_CASE("OrbitCamera viewport and projection matrices are finite") {
    OrbitCamera camera;

    CHECK(isFiniteMat4(camera.viewMatrix()));
    CHECK(isFiniteMat4(camera.projectionMatrix()));
    CHECK(isFiniteMat4(camera.viewProjectionMatrix()));

    SUBCASE("viewProjectionMatrix equals projectionMatrix * viewMatrix") {
        const glm::mat4 expectedVP = camera.projectionMatrix() * camera.viewMatrix();
        const glm::mat4 actualVP = camera.viewProjectionMatrix();
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                CHECK(actualVP[col][row] == doctest::Approx(expectedVP[col][row]).epsilon(kEpsilon));
            }
        }
    }

    SUBCASE("various viewport aspect ratios remain finite") {
        const std::pair<int, int> resolutions[] = {
            {1920, 1080},
            {1280, 720},
            {800, 600},
            {100, 100},
            {1, 1},
            {3840, 2160}
        };

        for (const auto& [w, h] : resolutions) {
            camera.setViewportSize(w, h);
            CHECK(isFiniteMat4(camera.projectionMatrix()));
            CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
        }
    }

    SUBCASE("non-positive viewport dimensions do not corrupt projection matrices") {
        camera.setViewportSize(0, 0);
        CHECK(isFiniteMat4(camera.projectionMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));

        camera.setViewportSize(-640, 480);
        CHECK(isFiniteMat4(camera.projectionMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
    }

    SUBCASE("preset views and reset maintain finite matrices") {
        for (const auto preset : {CameraPresetView::Perspective, CameraPresetView::TopXZ,
                                  CameraPresetView::SideYZ, CameraPresetView::FrontXY}) {
            camera.setPresetView(preset);
            CHECK(isFiniteMat4(camera.viewMatrix()));
            CHECK(isFiniteMat4(camera.projectionMatrix()));
            CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
            CHECK(isFiniteVec3(camera.eyePosition()));
        }

        camera.reset();
        CHECK(camera.yaw() == doctest::Approx(0.70F));
        CHECK(camera.pitch() == doctest::Approx(0.45F));
        CHECK(camera.distance() == doctest::Approx(1.8F));
        CHECK(isFiniteMat4(camera.viewMatrix()));
        CHECK(isFiniteMat4(camera.projectionMatrix()));
        CHECK(isFiniteMat4(camera.viewProjectionMatrix()));
    }
}

TEST_CASE("OrbitCamera projection matrix with explicit aspect and invalid aspect fallback") {
    OrbitCamera camera;

    SUBCASE("explicit valid aspect produces correct perspective matrix independent of viewport") {
        camera.setViewportSize(1920, 1080);
        const float explicitAspects[] = {1.0F, 4.0F / 3.0F, 16.0F / 9.0F, 21.0F / 9.0F, 0.5F};

        for (const float aspect : explicitAspects) {
            const glm::mat4 proj = camera.projectionMatrix(aspect);
            CHECK(isFiniteMat4(proj));

            const glm::mat4 expectedProj = glm::perspectiveRH_NO(
                glm::radians(camera.fovDegrees()),
                aspect,
                camera.nearPlane(),
                camera.farPlane());

            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    CHECK(proj[col][row] == doctest::Approx(expectedProj[col][row]).epsilon(kEpsilon));
                }
            }
        }

        // Changing viewport size does not change explicit aspect projection
        const glm::mat4 projSquareBefore = camera.projectionMatrix(1.0F);
        camera.setViewportSize(800, 600);
        const glm::mat4 projSquareAfter = camera.projectionMatrix(1.0F);
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                CHECK(projSquareBefore[col][row] == doctest::Approx(projSquareAfter[col][row]).epsilon(kEpsilon));
            }
        }
    }

    SUBCASE("parameterless projectionMatrix delegates to viewport aspect ratio") {
        camera.setViewportSize(1600, 900);
        const float expectedAspect = 1600.0F / 900.0F;
        const glm::mat4 projDefault = camera.projectionMatrix();
        const glm::mat4 projExplicit = camera.projectionMatrix(expectedAspect);

        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                CHECK(projDefault[col][row] == doctest::Approx(projExplicit[col][row]).epsilon(kEpsilon));
            }
        }
    }

    SUBCASE("invalid aspect falls back to current viewport aspect and remains finite") {
        camera.setViewportSize(1280, 720);
        const glm::mat4 expectedViewportProj = camera.projectionMatrix();

        const float invalidAspects[] = {
            0.0F,
            -0.0F,
            -1.0F,
            -16.0F / 9.0F,
            std::numeric_limits<float>::quiet_NaN(),
            std::numeric_limits<float>::infinity(),
            -std::numeric_limits<float>::infinity()
        };

        for (const float invalidAspect : invalidAspects) {
            const glm::mat4 fallbackProj = camera.projectionMatrix(invalidAspect);
            CHECK(isFiniteMat4(fallbackProj));

            for (int col = 0; col < 4; ++col) {
                for (int row = 0; row < 4; ++row) {
                    CHECK(fallbackProj[col][row] == doctest::Approx(expectedViewportProj[col][row]).epsilon(kEpsilon));
                }
            }
        }

        // When viewport changes, fallback reflects the new viewport
        camera.setViewportSize(800, 600);
        const glm::mat4 expectedNewViewportProj = camera.projectionMatrix();
        const glm::mat4 fallbackChanged = camera.projectionMatrix(-1.0F);
        CHECK(isFiniteMat4(fallbackChanged));
        for (int col = 0; col < 4; ++col) {
            for (int row = 0; row < 4; ++row) {
                CHECK(fallbackChanged[col][row] == doctest::Approx(expectedNewViewportProj[col][row]).epsilon(kEpsilon));
            }
        }
    }
}

