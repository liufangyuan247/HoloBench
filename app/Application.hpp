#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <string>
#include <vector>

#include <glm/glm.hpp>

#include "app/WaveDetectorUiState.hpp"
#include "app/RealLensWorkbenchPipeline.hpp"
#include "app/SamplingDebuggerPipeline.hpp"
#include "app/SlmInterferenceUiState.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/NumericalAperture.hpp"
#include "optics/scene/OpticalBenchScene.hpp"
#include "render/Camera.hpp"

struct SDL_GLContextState;
struct SDL_Window;

namespace holobench::render {
class OpticalBenchRenderer;
namespace gl {
class Texture2D;
}
}

namespace holobench::compute::fft {
class CpuFftBackend;
}

namespace holobench::app::wave {
struct WaveDetectorResult;
}

namespace holobench::app {

namespace gizmo {

struct ProjectedPoint {
    glm::vec2 screenPos {0.0F, 0.0F};
    float depth = 0.0F;
    bool visible = false;
};

struct AxisProjection {
    glm::vec2 screenDir {0.0F, 0.0F};
    double metresPerPixel = 0.0;
    bool isDegenerate = true;
};

[[nodiscard]] inline ProjectedPoint projectWorldToViewport(
    const glm::vec3& worldPos,
    const glm::mat4& viewProj,
    const glm::vec2& rectMin,
    const glm::vec2& rectSize) noexcept {
    if (rectSize.x <= 0.0F || rectSize.y <= 0.0F || !std::isfinite(rectSize.x) || !std::isfinite(rectSize.y)
        || !std::isfinite(rectMin.x) || !std::isfinite(rectMin.y)) {
        return {.screenPos = glm::vec2(0.0F, 0.0F), .depth = 0.0F, .visible = false};
    }
    const glm::vec4 clip = viewProj * glm::vec4(worldPos, 1.0F);
    if (!std::isfinite(clip.x) || !std::isfinite(clip.y) || !std::isfinite(clip.z) || !std::isfinite(clip.w)) {
        return {.screenPos = glm::vec2(0.0F, 0.0F), .depth = 0.0F, .visible = false};
    }
    // Reject points behind the camera or closer than the near-plane threshold
    if (clip.w <= 1e-4F) {
        return {.screenPos = glm::vec2(0.0F, 0.0F), .depth = clip.w, .visible = false};
    }
    const glm::vec3 ndc = glm::vec3(clip) / clip.w;
    if (!std::isfinite(ndc.x) || !std::isfinite(ndc.y) || !std::isfinite(ndc.z)) {
        return {.screenPos = glm::vec2(0.0F, 0.0F), .depth = clip.w, .visible = false};
    }
    // Strictly reject points outside the view frustum NDC bounds [-1, 1]
    if (ndc.x < -1.0F || ndc.x > 1.0F || ndc.y < -1.0F || ndc.y > 1.0F || ndc.z < -1.0F || ndc.z > 1.0F) {
        return {.screenPos = glm::vec2(0.0F, 0.0F), .depth = clip.w, .visible = false};
    }
    const float u = (ndc.x + 1.0F) * 0.5F;
    const float v = (1.0F - ndc.y) * 0.5F;
    const glm::vec2 screenPos(rectMin.x + u * rectSize.x, rectMin.y + v * rectSize.y);
    return {.screenPos = screenPos, .depth = clip.w, .visible = true};
}

[[nodiscard]] inline AxisProjection computeAxisProjection(
    const glm::vec3& worldOrigin,
    const glm::vec3& worldAxisDir,
    float deltaWorldMetres,
    const glm::mat4& viewProj,
    const glm::vec2& rectMin,
    const glm::vec2& rectSize) noexcept {
    if (rectSize.x <= 0.0F || rectSize.y <= 0.0F || deltaWorldMetres <= 0.0F || !std::isfinite(deltaWorldMetres)
        || !std::isfinite(rectMin.x) || !std::isfinite(rectMin.y)) {
        return {.screenDir = glm::vec2(0.0F, 0.0F), .metresPerPixel = 0.0, .isDegenerate = true};
    }

    const glm::vec4 clip0 = viewProj * glm::vec4(worldOrigin, 1.0F);
    const glm::vec4 clip1 = viewProj * glm::vec4(worldOrigin + worldAxisDir * deltaWorldMetres, 1.0F);

    if (!std::isfinite(clip0.x) || !std::isfinite(clip0.y) || !std::isfinite(clip0.z) || !std::isfinite(clip0.w)
        || !std::isfinite(clip1.x) || !std::isfinite(clip1.y) || !std::isfinite(clip1.z) || !std::isfinite(clip1.w)) {
        return {.screenDir = glm::vec2(0.0F, 0.0F), .metresPerPixel = 0.0, .isDegenerate = true};
    }

    if (clip0.w <= 1e-4F || clip1.w <= 1e-4F) {
        return {.screenDir = glm::vec2(0.0F, 0.0F), .metresPerPixel = 0.0, .isDegenerate = true};
    }

    const glm::vec2 ndc0 = glm::vec2(clip0.x / clip0.w, clip0.y / clip0.w);
    const glm::vec2 ndc1 = glm::vec2(clip1.x / clip1.w, clip1.y / clip1.w);

    const glm::vec2 p0(rectMin.x + (ndc0.x + 1.0F) * 0.5F * rectSize.x, rectMin.y + (1.0F - ndc0.y) * 0.5F * rectSize.y);
    const glm::vec2 p1(rectMin.x + (ndc1.x + 1.0F) * 0.5F * rectSize.x, rectMin.y + (1.0F - ndc1.y) * 0.5F * rectSize.y);

    const glm::vec2 delta = p1 - p0;
    const float len = std::hypot(delta.x, delta.y);

    if (!std::isfinite(len) || len < 1.0F) {
        return {.screenDir = glm::vec2(0.0F, 0.0F), .metresPerPixel = 0.0, .isDegenerate = true};
    }

    return {
        .screenDir = glm::vec2(delta.x / len, delta.y / len),
        .metresPerPixel = static_cast<double>(deltaWorldMetres) / static_cast<double>(len),
        .isDegenerate = false
    };
}

[[nodiscard]] inline double computeGizmoDeltaZ(
    const glm::vec2& mouseDelta,
    const AxisProjection& axisProj) noexcept {
    if (axisProj.isDegenerate || !std::isfinite(axisProj.metresPerPixel) || axisProj.metresPerPixel <= 0.0) {
        return 0.0;
    }
    if (!std::isfinite(mouseDelta.x) || !std::isfinite(mouseDelta.y)) {
        return 0.0;
    }
    const float deltaPx = mouseDelta.x * axisProj.screenDir.x + mouseDelta.y * axisProj.screenDir.y;
    if (!std::isfinite(deltaPx)) {
        return 0.0;
    }
    return static_cast<double>(deltaPx) * axisProj.metresPerPixel;
}

[[nodiscard]] inline bool hitTestHandle(
    const glm::vec2& mousePos,
    const ProjectedPoint& projPoint,
    float hitRadius) noexcept {
    if (!projPoint.visible || hitRadius <= 0.0F || !std::isfinite(hitRadius)) {
        return false;
    }
    if (!std::isfinite(mousePos.x) || !std::isfinite(mousePos.y)) {
        return false;
    }
    const float dist = std::hypot(mousePos.x - projPoint.screenPos.x, mousePos.y - projPoint.screenPos.y);
    return std::isfinite(dist) && dist <= hitRadius;
}

[[nodiscard]] inline double calculateCoplanarApertureZ(
    double currentLensZ,
    double initialLensZ,
    double initialApertureZ,
    bool wasCoplanar,
    double tolerance = 1e-4) noexcept {
    if (wasCoplanar || std::abs(initialApertureZ - initialLensZ) < tolerance) {
        return currentLensZ;
    }
    return initialApertureZ;
}

} // namespace gizmo

namespace docking {

struct DockLayoutConfig {
    static constexpr float kRightInspectorRatio = 0.25F;
    static constexpr float kBottomValidationRatio = 0.20F;
    static constexpr const char* kOpticalBenchWindowName = "Optical Bench";
    static constexpr const char* kInspectorWindowName = "Inspector";
    static constexpr const char* kValidationWindowName = "Validation";
    static constexpr const char* kWaveDetectorWindowName = "Wave Detector / Screen";
    static constexpr const char* kSamplingDebuggerWindowName = "Sampling Debugger";
    static constexpr const char* kRealLensWindowName = "Real Lens Workbench";
    static constexpr const char* kSlmInterferenceWindowName = "SLM & Interference Lab";
    static constexpr const char* kDockSpaceIdStr = "HoloBenchDockSpace";
};

struct Rect2D {
    float x = 0.0F;
    float y = 0.0F;
    float width = 0.0F;
    float height = 0.0F;

    [[nodiscard]] float area() const noexcept {
        return (width > 0.0F && height > 0.0F) ? (width * height) : 0.0F;
    }
};

struct ComputedDockPanels {
    Rect2D opticalBench;
    Rect2D inspector;
    Rect2D validation;
};

[[nodiscard]] inline bool shouldInitializeDefaultDockLayout(
    bool dockNodeExists,
    bool isSplitNode,
    bool isEmpty) noexcept {
    if (!dockNodeExists) {
        return true;
    }
    return !isSplitNode && isEmpty;
}

[[nodiscard]] inline ComputedDockPanels computeDockPanelRectangles(
    float totalWidth,
    float totalHeight,
    float rightRatio = DockLayoutConfig::kRightInspectorRatio,
    float bottomRatio = DockLayoutConfig::kBottomValidationRatio) noexcept {
    if (totalWidth <= 0.0F || totalHeight <= 0.0F || !std::isfinite(totalWidth) || !std::isfinite(totalHeight)) {
        return {};
    }
    const float clampedRight = std::clamp(rightRatio, 0.05F, 0.95F);
    const float clampedBottom = std::clamp(bottomRatio, 0.05F, 0.95F);

    const float inspectorWidth = totalWidth * clampedRight;
    const float leftWidth = totalWidth - inspectorWidth;
    const float inspectorHeight = totalHeight;

    const float validationHeight = totalHeight * clampedBottom;
    const float opticalBenchHeight = totalHeight - validationHeight;
    const float opticalBenchWidth = leftWidth;

    ComputedDockPanels panels;
    panels.opticalBench = {0.0F, 0.0F, opticalBenchWidth, opticalBenchHeight};
    panels.validation = {0.0F, opticalBenchHeight, leftWidth, validationHeight};
    panels.inspector = {leftWidth, 0.0F, inspectorWidth, inspectorHeight};
    return panels;
}

} // namespace docking

enum class GizmoTarget {
    None,
    Lens,
    Screen
};

struct RunOptions {
    int smokeFrameLimit = 0;
    int benchmarkFrames = 0;
    int initialRayCount = 64;
    bool glSmoke = false;
};

class Application final {
public:
    Application();
    ~Application();

    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    int run(const RunOptions& options);
    int run(int smokeFrameLimit = 0, int initialRayCount = 64);

private:
    bool initialize(const RunOptions& options);
    void shutdown() noexcept;
    void drawWorkspace();
    void drawWaveDetectorPanel();
    void drawSamplingDebuggerPanel();
    void drawRealLensPanel();
    void drawSlmInterferencePanel();
    void updateWaveDetector();
    void updateSlmInterference();
    void refreshSamplingDebugger();
    void refreshRealLensWorkbench();
    void loadRealLensPrescription(bool csv);
    void saveRealLensPrescription(bool csv);
    void loadSlmCalibration();
    void saveSlmCalibration();
    void loadSlmExperimentProject();
    void saveSlmExperimentProject();
    bool applyScene(
        const optics::scene::OpticalBenchScene& candidateScene,
        const optics::ray::BenchTracerOptions& candidateOptions);
    void loadSceneFromPath(const char* pathStr);
    void saveSceneToPath(const char* pathStr);

    SDL_Window* window_ = nullptr;
    SDL_GLContextState* glContext_ = nullptr;
    bool sdlInitialized_ = false;
    bool initialized_ = false;
    bool imguiContextCreated_ = false;
    bool imguiSdlInitialized_ = false;
    bool imguiGlInitialized_ = false;
    bool dockLayoutInitialized_ = false;
    bool isOrbiting_ = false;
    bool isPanning_ = false;

    GizmoTarget selectedTarget_ = GizmoTarget::None;
    GizmoTarget draggedTarget_ = GizmoTarget::None;
    bool isGizmoDragging_ = false;
    double dragInitialLensZ_ = 0.0;
    double dragInitialApertureZ_ = 0.0;
    double dragInitialScreenZ_ = 0.0;
    bool dragApertureWasCoplanar_ = false;

    render::OrbitCamera camera_;
    std::unique_ptr<render::OpticalBenchRenderer> renderer_;
    std::unique_ptr<compute::fft::CpuFftBackend> detectorFftBackend_;
    std::unique_ptr<render::gl::Texture2D> detectorTexture_;
    std::unique_ptr<render::gl::Texture2D> samplingSpectrumTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFObjectTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFBeforeFilterTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFAfterFilterTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFImageTexture_;
    std::unique_ptr<render::gl::Texture2D> slmInterferenceTexture_;
    std::unique_ptr<wave::WaveDetectorResult> detectorResult_;
    std::unique_ptr<samplingdebug::SamplingDebuggerResult> samplingDebuggerResult_;
    std::unique_ptr<reallens::RealLensWorkbenchResult> realLensResult_;
    std::unique_ptr<slmexperiment::SlmInterferenceExperimentResult> slmInterferenceResult_;
    waveui::WaveDetectorUiState detectorUiState_;
    samplingdebug::SamplingDebuggerConfig samplingDebuggerConfig_;
    reallens::RealLensWorkbenchConfig realLensConfig_;
    slmui::SlmInterferenceUiState slmInterferenceUiState_;
    waveui::DetectorPixel detectorProbe_;
    bool hasDetectorProbe_ = false;
    bool detectorProbeLocked_ = false;
    std::string detectorErrorMessage_;
    std::string detectorStatusMessage_;
    std::string samplingDebuggerErrorMessage_;
    std::string samplingDebuggerStatusMessage_;
    std::string realLensErrorMessage_;
    std::string realLensStatusMessage_;
    std::string slmInterferenceErrorMessage_;
    std::string slmInterferenceStatusMessage_;
    bool realLensDirty_ = true;
    std::size_t selectedRealLensSurface_ = 0;

    optics::scene::OpticalBenchScene scene_;
    optics::scene::ThinLensImagePrediction prediction_;
    optics::scene::NumericalApertureResult naResult_;
    std::vector<optics::ray::RaySegment> raySegments_;
    std::vector<optics::ray::RaySegment> stagingRaySegments_;
    optics::ray::BenchTracerOptions tracerOptions_;

    std::string errorMessage_;
    std::string statusMessage_;
    char projectPathBuffer_[512] = "holobench_scene.json";
    char realLensPathBuffer_[512] = "holobench_lens.json";
    char slmCalibrationPathBuffer_[512] = "slm_response.json";
    char slmProjectPathBuffer_[512] = "slm_experiment.json";

    bool isBenchmark_ = false;
    int vsyncInterval_ = 1;
    int lastViewportWidth_ = 0;
    int lastViewportHeight_ = 0;
    bool glSmokeMode_ = false;
};

} // namespace holobench::app
