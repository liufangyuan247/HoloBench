#include <doctest/doctest.h>

#include <cmath>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>

#include "app/Application.hpp"
#include "render/Camera.hpp"

TEST_SUITE("render::Gizmo") {

TEST_CASE("projectWorldToViewport projects world point inside frustum accurately") {
    // Standard perspective camera looking from (0, 0, 1) towards (0, 0, 0) with up (0, 1, 0)
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0F, 0.0F, 1.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 proj = glm::perspective(glm::radians(90.0F), 1.0F, 0.1F, 10.0F);
    const glm::mat4 viewProj = proj * view;

    const glm::vec2 rectMin(100.0F, 200.0F);
    const glm::vec2 rectSize(800.0F, 600.0F);

    // Target point is right at center of view (0, 0, 0) -> NDC (0, 0) -> Screen center
    const auto resCenter = holobench::app::gizmo::projectWorldToViewport(glm::vec3(0.0F, 0.0F, 0.0F), viewProj, rectMin, rectSize);
    CHECK(resCenter.visible);
    CHECK(resCenter.depth == doctest::Approx(1.0F));
    CHECK(resCenter.screenPos.x == doctest::Approx(500.0F));
    CHECK(resCenter.screenPos.y == doctest::Approx(500.0F));
}

TEST_CASE("projectWorldToViewport strictly rejects points behind camera and outside frustum") {
    const glm::mat4 view = glm::lookAt(glm::vec3(0.0F, 0.0F, 1.0F), glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 1.0F, 0.0F));
    const glm::mat4 proj = glm::perspective(glm::radians(90.0F), 1.0F, 0.1F, 10.0F);
    const glm::mat4 viewProj = proj * view;

    const glm::vec2 rectMin(0.0F, 0.0F);
    const glm::vec2 rectSize(1000.0F, 1000.0F);

    // Point behind camera (Z = 2.0 while camera is at Z = 1.0 looking towards -Z)
    const auto resBehind = holobench::app::gizmo::projectWorldToViewport(glm::vec3(0.0F, 0.0F, 2.0F), viewProj, rectMin, rectSize);
    CHECK_FALSE(resBehind.visible);

    // Point far to the left outside the 90 deg frustum
    const auto resFarLeft = holobench::app::gizmo::projectWorldToViewport(glm::vec3(-10.0F, 0.0F, 0.0F), viewProj, rectMin, rectSize);
    CHECK_FALSE(resFarLeft.visible);

    // Point beyond far plane (Z = -20.0 with far plane at 10.0)
    const auto resTooFar = holobench::app::gizmo::projectWorldToViewport(glm::vec3(0.0F, 0.0F, -20.0F), viewProj, rectMin, rectSize);
    CHECK_FALSE(resTooFar.visible);

    // Degenerate viewport sizes
    const auto resZeroSize = holobench::app::gizmo::projectWorldToViewport(glm::vec3(0.0F, 0.0F, 0.0F), viewProj, rectMin, glm::vec2(0.0F, 0.0F));
    CHECK_FALSE(resZeroSize.visible);

    // Non-finite world coordinates
    const auto resNan = holobench::app::gizmo::projectWorldToViewport(glm::vec3(NAN, 0.0F, 0.0F), viewProj, rectMin, rectSize);
    CHECK_FALSE(resNan.visible);
}

TEST_CASE("computeAxisProjection and computeGizmoDeltaZ convert screen delta to world +Z movement") {
    holobench::render::OrbitCamera cam;
    cam.reset();
    cam.setViewportSize(1280, 720);
    // Camera is looking at origin with perspective
    const glm::mat4 viewProj = cam.viewProjectionMatrix();
    const glm::vec2 rectMin(0.0F, 0.0F);
    const glm::vec2 rectSize(1280.0F, 720.0F);

    const glm::vec3 origin(0.0F, 0.0F, 0.0F);
    const glm::vec3 plusZDir(0.0F, 0.0F, 1.0F);

    const auto axisProj = holobench::app::gizmo::computeAxisProjection(origin, plusZDir, 0.05F, viewProj, rectMin, rectSize);
    CHECK_FALSE(axisProj.isDegenerate);
    CHECK(axisProj.metresPerPixel > 0.0);
    CHECK(std::hypot(axisProj.screenDir.x, axisProj.screenDir.y) == doctest::Approx(1.0F));

    // Moving 50 pixels along screenDir gives positive delta Z in world space
    const glm::vec2 mouseDeltaAlong = axisProj.screenDir * 50.0F;
    const double deltaZ = holobench::app::gizmo::computeGizmoDeltaZ(mouseDeltaAlong, axisProj);
    CHECK(deltaZ > 0.0);
    CHECK(deltaZ == doctest::Approx(50.0 * axisProj.metresPerPixel));

    // Moving 50 pixels against screenDir gives negative delta Z
    const glm::vec2 mouseDeltaOpposite = -axisProj.screenDir * 50.0F;
    const double deltaZNeg = holobench::app::gizmo::computeGizmoDeltaZ(mouseDeltaOpposite, axisProj);
    CHECK(deltaZNeg < 0.0);
    CHECK(deltaZNeg == doctest::Approx(-50.0 * axisProj.metresPerPixel));

    // Moving perpendicular to screenDir gives 0 delta Z
    const glm::vec2 perp(-axisProj.screenDir.y * 50.0F, axisProj.screenDir.x * 50.0F);
    const double deltaZPerp = holobench::app::gizmo::computeGizmoDeltaZ(perp, axisProj);
    CHECK(deltaZPerp == doctest::Approx(0.0));
}

TEST_CASE("computeAxisProjection correctly identifies degenerate views") {
    // In FrontXY view, camera looks directly along the Z axis (parallel to +Z axis)
    holobench::render::OrbitCamera cam;
    cam.setPresetView(holobench::render::CameraPresetView::FrontXY);
    const glm::mat4 viewProj = cam.viewProjectionMatrix();
    const glm::vec2 rectMin(0.0F, 0.0F);
    const glm::vec2 rectSize(800.0F, 600.0F);

    const auto axisProj = holobench::app::gizmo::computeAxisProjection(glm::vec3(0.0F), glm::vec3(0.0F, 0.0F, 1.0F), 0.05F, viewProj, rectMin, rectSize);
    // Because displacement on screen is 0, axis projection must be marked degenerate
    CHECK(axisProj.isDegenerate);

    // Degenerate projection computes 0 delta Z safely
    const double deltaZ = holobench::app::gizmo::computeGizmoDeltaZ(glm::vec2(10.0F, 20.0F), axisProj);
    CHECK(deltaZ == 0.0);
}

TEST_CASE("hitTestHandle accurately tests click position against projected handle") {
    holobench::app::gizmo::ProjectedPoint pt;
    pt.screenPos = glm::vec2(300.0F, 400.0F);
    pt.depth = 1.5F;
    pt.visible = true;

    constexpr float hitRadius = 14.0F;

    // Direct hit
    CHECK(holobench::app::gizmo::hitTestHandle(glm::vec2(300.0F, 400.0F), pt, hitRadius));
    // Hit within radius
    CHECK(holobench::app::gizmo::hitTestHandle(glm::vec2(310.0F, 400.0F), pt, hitRadius));
    CHECK(holobench::app::gizmo::hitTestHandle(glm::vec2(300.0F, 412.0F), pt, hitRadius));
    // Miss outside radius
    CHECK_FALSE(holobench::app::gizmo::hitTestHandle(glm::vec2(320.0F, 400.0F), pt, hitRadius));
    CHECK_FALSE(holobench::app::gizmo::hitTestHandle(glm::vec2(300.0F, 420.0F), pt, hitRadius));

    // Invisible handle cannot be hit
    pt.visible = false;
    CHECK_FALSE(holobench::app::gizmo::hitTestHandle(glm::vec2(300.0F, 400.0F), pt, hitRadius));

    // Non-finite mouse position
    pt.visible = true;
    CHECK_FALSE(holobench::app::gizmo::hitTestHandle(glm::vec2(NAN, 400.0F), pt, hitRadius));
}

TEST_CASE("calculateCoplanarApertureZ correctly tracks lens movement when coplanar") {
    const double initialLensZ = 0.20;
    const double initialApertureZ = 0.20; // exactly coplanar
    const double newLensZ = 0.35;

    // When coplanar, aperture tracks lens position exactly
    const double apZ = holobench::app::gizmo::calculateCoplanarApertureZ(newLensZ, initialLensZ, initialApertureZ, true);
    CHECK(apZ == doctest::Approx(0.35));

    // When independent, aperture remains at its initial position
    const double independentApertureZ = 0.15;
    const double apZIndependent = holobench::app::gizmo::calculateCoplanarApertureZ(newLensZ, initialLensZ, independentApertureZ, false);
    CHECK(apZIndependent == doctest::Approx(0.15));
}

TEST_CASE("projectWorldToViewport correctly aligns with renderer FBO projection under non-uniform framebuffer scale") {
    // Simulate non-uniform DPI framebuffer scale (e.g. scaleX = 2.0, scaleY = 1.0)
    const glm::vec2 logicalRectSize(800.0F, 600.0F);
    const float scaleX = 2.0F;
    const float scaleY = 1.0F;
    const int fboWidth = static_cast<int>(std::round(logicalRectSize.x * scaleX));   // 1600
    const int fboHeight = static_cast<int>(std::round(logicalRectSize.y * scaleY));  // 600

    const float fboAspect = static_cast<float>(fboWidth) / static_cast<float>(fboHeight);
    const float logicalAspect = logicalRectSize.x / logicalRectSize.y;

    holobench::render::OrbitCamera cam;
    cam.reset();
    cam.setViewportSize(fboWidth, fboHeight);

    const glm::mat4 viewProjFbo = cam.projectionMatrix(fboAspect) * cam.viewMatrix();
    const glm::mat4 viewProjLogical = cam.projectionMatrix(logicalAspect) * cam.viewMatrix();

    const glm::vec2 rectMin(100.0F, 150.0F);
    // Off-center world position to test horizontal aspect effect
    const glm::vec3 worldPoint(0.2F, 0.1F, 0.6F);

    const auto projFbo = holobench::app::gizmo::projectWorldToViewport(worldPoint, viewProjFbo, rectMin, logicalRectSize);
    const auto projLogical = holobench::app::gizmo::projectWorldToViewport(worldPoint, viewProjLogical, rectMin, logicalRectSize);

    CHECK(projFbo.visible);
    CHECK(projLogical.visible);

    // With non-uniform scale (scaleX != scaleY), logical aspect != FBO aspect, causing a screen position mismatch
    CHECK(projFbo.screenPos.x != doctest::Approx(projLogical.screenPos.x));

    // projFbo matches the NDC coordinates produced by the camera's FBO projection mapped to the logical rect
    const glm::vec4 clip = viewProjFbo * glm::vec4(worldPoint, 1.0F);
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    const float expectedU = (ndc.x + 1.0F) * 0.5F;
    const float expectedV = (1.0F - ndc.y) * 0.5F;
    const glm::vec2 expectedScreenPos(rectMin.x + expectedU * logicalRectSize.x, rectMin.y + expectedV * logicalRectSize.y);

    CHECK(projFbo.screenPos.x == doctest::Approx(expectedScreenPos.x));
    CHECK(projFbo.screenPos.y == doctest::Approx(expectedScreenPos.y));
}

} // TEST_SUITE("render::Gizmo")

TEST_SUITE("app::Docking") {

TEST_CASE("DockLayoutConfig defines valid constants and window identifiers") {
    CHECK(holobench::app::docking::DockLayoutConfig::kRightInspectorRatio == doctest::Approx(0.25F));
    CHECK(holobench::app::docking::DockLayoutConfig::kBottomValidationRatio == doctest::Approx(0.20F));
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kOpticalBenchWindowName) == "Optical Bench");
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kInspectorWindowName) == "Inspector");
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kValidationWindowName) == "Validation");
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kSamplingDebuggerWindowName) == "Sampling Debugger");
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kSlmInterferenceWindowName) == "SLM & Interference Lab");
    CHECK(std::string(holobench::app::docking::DockLayoutConfig::kDockSpaceIdStr) == "HoloBenchDockSpace");
}

TEST_CASE("shouldInitializeDefaultDockLayout handles blank, non-existent, split, and occupied nodes correctly") {
    // 1. Non-existent node -> must initialize default layout
    CHECK(holobench::app::docking::shouldInitializeDefaultDockLayout(false, false, false));
    CHECK(holobench::app::docking::shouldInitializeDefaultDockLayout(false, true, false));
    CHECK(holobench::app::docking::shouldInitializeDefaultDockLayout(false, false, true));
    CHECK(holobench::app::docking::shouldInitializeDefaultDockLayout(false, true, true));

    // 2. Blank fresh node (exists, not split, empty) -> must initialize default layout
    CHECK(holobench::app::docking::shouldInitializeDefaultDockLayout(true, false, true));

    // 3. Node already split (e.g. restored from imgui.ini) -> must NOT initialize, retain existing split
    CHECK_FALSE(holobench::app::docking::shouldInitializeDefaultDockLayout(true, true, false));
    CHECK_FALSE(holobench::app::docking::shouldInitializeDefaultDockLayout(true, true, true));

    // 4. Node with already docked window (exists, not split, not empty) -> must NOT initialize, retain docked window
    CHECK_FALSE(holobench::app::docking::shouldInitializeDefaultDockLayout(true, false, false));
}

TEST_CASE("computeDockPanelRectangles partitions viewport accurately with default ratios") {
    constexpr float width = 1920.0F;
    constexpr float height = 1080.0F;

    const auto panels = holobench::app::docking::computeDockPanelRectangles(width, height);

    // Inspector is placed on the right (25% width = 480 px, full height = 1080 px)
    CHECK(panels.inspector.x == doctest::Approx(1440.0F));
    CHECK(panels.inspector.y == doctest::Approx(0.0F));
    CHECK(panels.inspector.width == doctest::Approx(480.0F));
    CHECK(panels.inspector.height == doctest::Approx(1080.0F));
    CHECK(panels.inspector.area() == doctest::Approx(480.0F * 1080.0F));

    // Validation is placed on bottom left (75% width = 1440 px, 20% height = 216 px)
    CHECK(panels.validation.x == doctest::Approx(0.0F));
    CHECK(panels.validation.y == doctest::Approx(864.0F));
    CHECK(panels.validation.width == doctest::Approx(1440.0F));
    CHECK(panels.validation.height == doctest::Approx(216.0F));
    CHECK(panels.validation.area() == doctest::Approx(1440.0F * 216.0F));

    // Optical Bench occupies central top-left (75% width = 1440 px, 80% height = 864 px)
    CHECK(panels.opticalBench.x == doctest::Approx(0.0F));
    CHECK(panels.opticalBench.y == doctest::Approx(0.0F));
    CHECK(panels.opticalBench.width == doctest::Approx(1440.0F));
    CHECK(panels.opticalBench.height == doctest::Approx(864.0F));
    CHECK(panels.opticalBench.area() == doctest::Approx(1440.0F * 864.0F));

    // Conservation of total workspace area
    const float totalArea = width * height;
    const float sumArea = panels.opticalBench.area() + panels.inspector.area() + panels.validation.area();
    CHECK(sumArea == doctest::Approx(totalArea));

    // Spatial non-overlapping partition checks
    CHECK(panels.opticalBench.x + panels.opticalBench.width == doctest::Approx(panels.inspector.x));
    CHECK(panels.validation.x + panels.validation.width == doctest::Approx(panels.inspector.x));
    CHECK(panels.inspector.x + panels.inspector.width == doctest::Approx(width));
    CHECK(panels.opticalBench.y + panels.opticalBench.height == doctest::Approx(panels.validation.y));
    CHECK(panels.validation.y + panels.validation.height == doctest::Approx(height));
}

TEST_CASE("computeDockPanelRectangles adapts correctly across different viewport resolutions") {
    // 1440 x 900 resolution
    {
        const auto panels = holobench::app::docking::computeDockPanelRectangles(1440.0F, 900.0F);
        CHECK(panels.inspector.width == doctest::Approx(360.0F)); // 25% of 1440
        CHECK(panels.inspector.height == doctest::Approx(900.0F));
        CHECK(panels.opticalBench.width == doctest::Approx(1080.0F)); // 75% of 1440
        CHECK(panels.opticalBench.height == doctest::Approx(720.0F));  // 80% of 900
        CHECK(panels.validation.width == doctest::Approx(1080.0F));
        CHECK(panels.validation.height == doctest::Approx(180.0F));   // 20% of 900
        CHECK(panels.opticalBench.area() + panels.inspector.area() + panels.validation.area() == doctest::Approx(1440.0F * 900.0F));
    }

    // 800 x 600 resolution
    {
        const auto panels = holobench::app::docking::computeDockPanelRectangles(800.0F, 600.0F);
        CHECK(panels.inspector.width == doctest::Approx(200.0F));
        CHECK(panels.inspector.height == doctest::Approx(600.0F));
        CHECK(panels.opticalBench.width == doctest::Approx(600.0F));
        CHECK(panels.opticalBench.height == doctest::Approx(480.0F));
        CHECK(panels.validation.width == doctest::Approx(600.0F));
        CHECK(panels.validation.height == doctest::Approx(120.0F));
        CHECK(panels.opticalBench.area() + panels.inspector.area() + panels.validation.area() == doctest::Approx(800.0F * 600.0F));
    }
}

TEST_CASE("computeDockPanelRectangles supports custom ratios and clamps extreme values safely") {
    constexpr float width = 1000.0F;
    constexpr float height = 500.0F;

    // Custom 30% right, 25% bottom
    {
        const auto panels = holobench::app::docking::computeDockPanelRectangles(width, height, 0.30F, 0.25F);
        CHECK(panels.inspector.width == doctest::Approx(300.0F));
        CHECK(panels.inspector.height == doctest::Approx(500.0F));
        CHECK(panels.opticalBench.width == doctest::Approx(700.0F));
        CHECK(panels.opticalBench.height == doctest::Approx(375.0F));
        CHECK(panels.validation.width == doctest::Approx(700.0F));
        CHECK(panels.validation.height == doctest::Approx(125.0F));
        CHECK(panels.opticalBench.area() + panels.inspector.area() + panels.validation.area() == doctest::Approx(width * height));
    }

    // Ratio underflow clamped to 0.05
    {
        const auto panels = holobench::app::docking::computeDockPanelRectangles(width, height, 0.01F, 0.01F);
        CHECK(panels.inspector.width == doctest::Approx(50.0F)); // 5% clamp
        CHECK(panels.validation.height == doctest::Approx(25.0F)); // 5% clamp
    }

    // Ratio overflow clamped to 0.95
    {
        const auto panels = holobench::app::docking::computeDockPanelRectangles(width, height, 0.99F, 0.99F);
        CHECK(panels.inspector.width == doctest::Approx(950.0F)); // 95% clamp
        CHECK(panels.validation.height == doctest::Approx(475.0F)); // 95% clamp
    }
}

TEST_CASE("computeDockPanelRectangles rejects degenerate inputs gracefully") {
    // Zero dimensions
    const auto zeroRes = holobench::app::docking::computeDockPanelRectangles(0.0F, 0.0F);
    CHECK(zeroRes.opticalBench.area() == 0.0F);
    CHECK(zeroRes.inspector.area() == 0.0F);
    CHECK(zeroRes.validation.area() == 0.0F);

    // Negative dimensions
    const auto negRes = holobench::app::docking::computeDockPanelRectangles(-1920.0F, 1080.0F);
    CHECK(negRes.opticalBench.area() == 0.0F);

    // Non-finite dimensions (NaN, Inf)
    const auto nanRes = holobench::app::docking::computeDockPanelRectangles(NAN, 1080.0F);
    CHECK(nanRes.opticalBench.area() == 0.0F);

    const auto infRes = holobench::app::docking::computeDockPanelRectangles(1920.0F, INFINITY);
    CHECK(infRes.opticalBench.area() == 0.0F);
}

} // TEST_SUITE("app::Docking")
