#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <memory>
#include <numbers>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

#include <glm/glm.hpp>

#include "app/WaveDetectorUiState.hpp"
#include "app/WaveWorkbenchProject.hpp"
#include "app/BenchEditHistory.hpp"
#include "app/BenchProject.hpp"
#include "app/ChimeraRecipe.hpp"
#include "app/LessonEditHistory.hpp"
#include "app/HolographyLabPipeline.hpp"
#include "app/HolographyUiState.hpp"
#include "app/RealLensWorkbenchPipeline.hpp"
#include "app/ReflectionRefractionWorkbench.hpp"
#include "app/SamplingDebuggerPipeline.hpp"
#include "app/SlmInterferenceUiState.hpp"
#include "app/lessons/LearnSession.hpp"
#include "app/lessons/Localization.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/holography/BenchHologramReplay.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"
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

class UiFontAsset;

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

struct PlaneIntersection {
    glm::vec3 worldPosition {0.0F, 0.0F, 0.0F};
    bool hit = false;
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

[[nodiscard]] inline PlaneIntersection unprojectScreenToHorizontalPlane(
    const glm::vec2& screenPosition,
    float planeY,
    const glm::mat4& viewProj,
    const glm::vec2& rectMin,
    const glm::vec2& rectSize) noexcept {
    if (!std::isfinite(screenPosition.x) || !std::isfinite(screenPosition.y)
        || !std::isfinite(planeY)
        || !std::isfinite(rectMin.x) || !std::isfinite(rectMin.y)
        || !std::isfinite(rectSize.x) || !std::isfinite(rectSize.y)
        || rectSize.x <= 0.0F || rectSize.y <= 0.0F
        || screenPosition.x < rectMin.x || screenPosition.y < rectMin.y
        || screenPosition.x > rectMin.x + rectSize.x
        || screenPosition.y > rectMin.y + rectSize.y) {
        return {};
    }
    const float determinant = glm::determinant(viewProj);
    if (!std::isfinite(determinant) || std::abs(determinant) < 1e-12F) {
        return {};
    }
    const glm::mat4 inverseViewProj = glm::inverse(viewProj);
    const float ndcX = 2.0F * (screenPosition.x - rectMin.x) / rectSize.x - 1.0F;
    const float ndcY = 1.0F - 2.0F * (screenPosition.y - rectMin.y) / rectSize.y;
    glm::vec4 nearWorld = inverseViewProj * glm::vec4(ndcX, ndcY, -1.0F, 1.0F);
    glm::vec4 farWorld = inverseViewProj * glm::vec4(ndcX, ndcY, 1.0F, 1.0F);
    if (!std::isfinite(nearWorld.x) || !std::isfinite(nearWorld.y)
        || !std::isfinite(nearWorld.z) || !std::isfinite(nearWorld.w)
        || !std::isfinite(farWorld.x) || !std::isfinite(farWorld.y)
        || !std::isfinite(farWorld.z) || !std::isfinite(farWorld.w)
        || std::abs(nearWorld.w) < 1e-12F
        || std::abs(farWorld.w) < 1e-12F) {
        return {};
    }
    nearWorld /= nearWorld.w;
    farWorld /= farWorld.w;
    const glm::vec3 direction = glm::vec3(farWorld - nearWorld);
    if (!std::isfinite(direction.x) || !std::isfinite(direction.y)
        || !std::isfinite(direction.z) || std::abs(direction.y) < 1e-7F) {
        return {};
    }
    const float distance = (planeY - nearWorld.y) / direction.y;
    if (!std::isfinite(distance) || distance < 0.0F) {
        return {};
    }
    const glm::vec3 result = glm::vec3(nearWorld) + direction * distance;
    if (!std::isfinite(result.x) || !std::isfinite(result.y)
        || !std::isfinite(result.z)) {
        return {};
    }
    return {.worldPosition = result, .hit = true};
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

[[nodiscard]] inline double quantizeGizmoDelta(
    double value,
    double step) noexcept {
    if (!std::isfinite(value) || !std::isfinite(step) || step <= 0.0) {
        return value;
    }
    const double scaled = value / step;
    if (!std::isfinite(scaled)) {
        return value;
    }
    return std::round(scaled) * step;
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

enum class LocalRotationAxis {
    X,
    Y,
    Z,
};

[[nodiscard]] inline math::RigidTransform3d rotateRigidTransformLocally(
    const math::RigidTransform3d& transform,
    LocalRotationAxis localAxis,
    double angleRadians) {
    if (!std::isfinite(angleRadians)) {
        throw std::invalid_argument("gizmo rotation angle must be finite");
    }
    math::validateRigidTransform(transform);
    math::Vec3d worldAxis = transform.localZAxisInWorld;
    if (localAxis == LocalRotationAxis::X) {
        worldAxis = transform.localXAxisInWorld;
    } else if (localAxis == LocalRotationAxis::Y) {
        worldAxis = transform.localYAxisInWorld;
    }
    const auto rotateVector = [worldAxis, angleRadians](math::Vec3d vector) {
        return vector * std::cos(angleRadians)
            + math::cross(worldAxis, vector) * std::sin(angleRadians)
            + worldAxis * (math::dot(worldAxis, vector) * (1.0 - std::cos(angleRadians)));
    };
    math::RigidTransform3d result = transform;
    result.localXAxisInWorld = math::normalized(
        rotateVector(result.localXAxisInWorld));
    const math::Vec3d rotatedY = rotateVector(result.localYAxisInWorld);
    result.localYAxisInWorld = math::normalized(
        rotatedY - result.localXAxisInWorld
            * math::dot(rotatedY, result.localXAxisInWorld));
    result.localZAxisInWorld = math::cross(
        result.localXAxisInWorld, result.localYAxisInWorld);
    math::validateRigidTransform(result);
    return result;
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
    static constexpr const char* kHolographyWindowName = "Holography Lab";
    static constexpr const char* kLearnWindowName = "Learn";
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

enum class ViewportMode {
    Sandbox,
    LegacyReference,
};

enum class SandboxGizmoMode {
    Translate,
    Rotate,
};

enum class SandboxGizmoConstraint {
    None,
    ViewPlane,
    AxisX,
    AxisY,
    AxisZ,
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
    void drawReflectionRefractionPanel();
    void drawWaveDetectorPanel();
    void drawSamplingDebuggerPanel();
    void drawRealLensPanel();
    void drawSlmInterferencePanel();
    void drawHolographyPanel();
    void drawLearnPanel();
    void loadLessonTemplate(std::string_view lessonId);
    void updateWaveDetector();
    void updateSlmInterference();
    void updateHolography();
    void refreshSamplingDebugger();
    void refreshRealLensWorkbench();
    void loadRealLensPrescription(bool csv);
    void saveRealLensPrescription(bool csv);
    void loadSlmCalibration();
    void saveSlmCalibration();
    void loadReflectionRefractionProject();
    void saveReflectionRefractionProject();
    void loadSlmExperimentProject();
    void saveSlmExperimentProject();
    void loadWaveWorkbenchProject();
    void saveWaveWorkbenchProject();
    void loadHolographyProject();
    void saveHolographyProject();
    void loadLessonProgress();
    void saveLessonProgress();
    [[nodiscard]] LessonEditState captureLessonEditState() const;
    void recordLessonEdit();
    void undoLessonEdit();
    void redoLessonEdit();
    [[nodiscard]] bool restoreLessonEditState(const LessonEditState& state);
    bool applyReflectionRefractionConfig(
        const reflection::ReflectionRefractionConfig& config);
    bool applySceneProject(
        const optics::scene::OpticalBenchScene& candidateScene,
        const optics::ray::BenchTracerOptions& candidateOptions,
        const project::ProjectProvenance& provenance);
    bool applyScene(
        const optics::scene::OpticalBenchScene& candidateScene,
        const optics::ray::BenchTracerOptions& candidateOptions);
    bool applyBenchScene(
        optics::scene::BenchScene candidateScene,
        std::string statusMessage,
        bool recordHistory = true);
    bool applyDynamicBenchProject(
        BenchProject candidateProject,
        std::string statusMessage,
        bool recordHistory = true);
    void recordBenchEdit();
    void undoBenchEdit();
    void redoBenchEdit();
    [[nodiscard]] bool restoreBenchEditState(const BenchProject& state);
    bool showSandboxViewport();
    bool showLegacyViewport();
    bool placeSandboxComponent(
        optics::scene::BenchComponentKind kind,
        const math::Vec3d& positionMetres,
        std::string statusMessage);
    void drawSandboxComponentShelf();
    void drawSandboxInspector();
    void loadBenchProjectFromPath();
    void saveBenchProjectToPath();
    void autosaveBenchProjectAfterEdit();
    void buildChimeraBench(
        const chimera::ChimeraRecipe& recipe,
        std::string sourceLabel);
    void recomputeRecordingRecipe(
        const optics::holography::PlateIncidentFieldSet& fields,
        const HologramRecordingRecipe& recipe);
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

    ViewportMode viewportMode_ = ViewportMode::Sandbox;
    SandboxGizmoMode sandboxGizmoMode_ = SandboxGizmoMode::Translate;
    SandboxGizmoConstraint sandboxGizmoConstraint_
        = SandboxGizmoConstraint::ViewPlane;
    bool sandboxGizmoDragging_ = false;
    bool sandboxGizmoChanged_ = false;
    math::RigidTransform3d sandboxDragInitialTransform_ {};
    math::Vec3d sandboxDragAccumulatedTranslationMetres_ {};
    double sandboxDragAccumulatedAngleRadians_ = 0.0;
    std::string selectedBenchComponentId_;
    std::size_t sandboxNextComponentOrdinal_ = 1;
    int sandboxLibraryKindIndex_ = 0;
    char sandboxComponentSearch_[64] {};
    float sandboxTranslationSnapMillimetres_ = 1.0F;
    float sandboxRotationStepDegrees_ = 5.0F;
    int sandboxPlateSampleSize_ = 256;
    float sandboxPlateWindowMillimetres_ = 1.0F;
    float sandboxPlateRelativeReferenceKilowattsPerSquareMetre_ = 100.0F;
    int sandboxPlateReplayKindIndex_ = 1;
    int sandboxPlateReplayViewIndex_ = 0;
    int sandboxRgbReplayViewIndex_ = 0;
    float sandboxRgbDisplayGains_[3] {1.0F, 1.0F, 1.0F};
    float sandboxRgbDisplayGamma_ = 2.2F;
    float sandboxVolumeAverageRefractiveIndex_ = 1.5F;
    float sandboxVolumeIndexModulation_ = 0.01F;
    float sandboxVolumeShrinkagePercent_ = 0.0F;
    float sandboxVolumeReplayWavelengthNanometres_ = 532.0F;
    float sandboxVolumeReplayAngleDegrees_ = 0.0F;
    std::vector<chimera::ConstraintReportEntry> chimeraConstraintReport_;

    GizmoTarget selectedTarget_ = GizmoTarget::None;
    GizmoTarget draggedTarget_ = GizmoTarget::None;
    bool isGizmoDragging_ = false;
    double dragInitialLensZ_ = 0.0;
    double dragInitialApertureZ_ = 0.0;
    double dragInitialScreenZ_ = 0.0;
    bool dragApertureWasCoplanar_ = false;
    bool gizmoDragChanged_ = false;

    render::OrbitCamera camera_;
    std::unique_ptr<render::OpticalBenchRenderer> renderer_;
    std::unique_ptr<UiFontAsset> uiFont_;
    std::unique_ptr<compute::fft::CpuFftBackend> detectorFftBackend_;
    std::unique_ptr<render::gl::Texture2D> detectorTexture_;
    std::unique_ptr<render::gl::Texture2D> samplingSpectrumTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFObjectTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFBeforeFilterTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFAfterFilterTexture_;
    std::unique_ptr<render::gl::Texture2D> fourFImageTexture_;
    std::unique_ptr<render::gl::Texture2D> slmInterferenceTexture_;
    std::unique_ptr<render::gl::Texture2D> holographyTexture_;
    std::unique_ptr<render::gl::Texture2D> sandboxPlateTexture_;
    std::unique_ptr<render::gl::Texture2D> sandboxReplayTexture_;
    std::unique_ptr<render::gl::Texture2D> sandboxVolumeReplayTexture_;
    std::unique_ptr<render::gl::Texture2D> sandboxRgbReplayTexture_;
    std::unique_ptr<wave::WaveDetectorResult> detectorResult_;
    std::unique_ptr<samplingdebug::SamplingDebuggerResult> samplingDebuggerResult_;
    std::unique_ptr<reallens::RealLensWorkbenchResult> realLensResult_;
    std::unique_ptr<slmexperiment::SlmInterferenceExperimentResult> slmInterferenceResult_;
    std::unique_ptr<holographylab::HolographyLabResult> holographyResult_;
    std::unique_ptr<optics::holography::ThinPlateRecordingResult>
        sandboxPlateRecording_;
    std::unique_ptr<optics::holography::ThinPlateReplayResult>
        sandboxPlateReplay_;
    std::unique_ptr<optics::holography::VolumePlateRecordingResult>
        sandboxVolumeRecording_;
    std::unique_ptr<optics::holography::VolumePlateReplayResult>
        sandboxVolumeReplay_;
    std::unique_ptr<optics::holography::VolumePlateObservationReplayResult>
        sandboxVolumeObservationReplay_;
    std::unique_ptr<optics::holography::RgbThinPlateRecordingResult>
        sandboxRgbRecording_;
    std::unique_ptr<optics::holography::RgbThinPlateReplayResult>
        sandboxRgbReplay_;
    reflection::ReflectionRefractionConfig reflectionRefractionConfig_;
    reflection::ReflectionRefractionResult reflectionRefractionResult_;
    project::ProjectProvenance reflectionProjectProvenance_;
    std::string reflectionProjectName_
        = "Reflection & Refraction Workbench";
    waveui::WaveDetectorUiState detectorUiState_;
    samplingdebug::SamplingDebuggerConfig samplingDebuggerConfig_;
    project::ProjectProvenance waveProjectProvenance_;
    std::string waveProjectName_ = "Wave & Sampling Workbench";
    reallens::RealLensWorkbenchConfig realLensConfig_;
    slmui::SlmInterferenceUiState slmInterferenceUiState_;
    project::ProjectProvenance slmProjectProvenance_;
    std::string slmProjectName_ = "SLM & Interference Experiment";
    holographyui::HolographyUiState holographyUiState_;
    project::ProjectProvenance holographyProjectProvenance_;
    std::string holographyProjectName_ = "Holography Lab Experiment";
    lessons::LearnSession learnSession_;
    lessons::LocalizationCatalog lessonLocalization_;
    lessons::LessonLocale lessonLocale_ = lessons::LessonLocale::English;
    LessonEditHistory lessonEditHistory_;
    bool lessonEditHistoryReady_ = false;
    bool restoringLessonEdit_ = false;
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
    std::string holographyErrorMessage_;
    std::string holographyStatusMessage_;
    std::string reflectionErrorMessage_;
    std::string reflectionStatusMessage_;
    std::string lessonErrorMessage_;
    std::string lessonStatusMessage_;
    std::string selectedLessonId_ = "reflection_refraction";
    optics::scene::ImageNature lessonImageClassification_
        = optics::scene::ImageNature::Real;
    lessons::FourierPlaneIdentification lessonFourierPlaneIdentification_
        = lessons::FourierPlaneIdentification::ObjectPlane;
    lessons::SpatialFilteringEffect lessonSpatialFilteringEffect_
        = lessons::SpatialFilteringEffect::Sharper;
    lessons::PsfWidthChange lessonPsfWidthChange_
        = lessons::PsfWidthChange::Wider;
    lessons::FringeVisibilityChange lessonFringeVisibilityChange_
        = lessons::FringeVisibilityChange::Higher;
    lessons::HolographyReplayContents lessonHolographyReplayContents_
        = lessons::HolographyReplayContents::DesiredImageOnly;
    holography::H2ImagePlacement lessonH1H2Placement_
        = holography::H2ImagePlacement::PositiveSide;
    bool realLensDirty_ = true;
    std::size_t selectedRealLensSurface_ = 0;

    optics::scene::OpticalBenchScene scene_;
    project::ProjectProvenance sceneProjectProvenance_;
    optics::scene::ThinLensImagePrediction prediction_;
    optics::scene::NumericalApertureResult naResult_;
    std::vector<optics::ray::RaySegment> raySegments_;
    std::vector<optics::ray::RaySegment> stagingRaySegments_;
    optics::ray::BenchTracerOptions tracerOptions_;

    BenchProject benchProject_;
    BenchEditHistory benchEditHistory_;
    bool benchEditHistoryReady_ = false;
    optics::scene::BenchTraceGraph benchTraceGraph_;
    optics::scene::TraceBudget benchTraceBudget_;

    std::string errorMessage_;
    std::string statusMessage_;
    char projectPathBuffer_[512] = "holobench_scene.json";
    char benchProjectPathBuffer_[512] = "holobench_bench.json";
    char chimeraRecipePathBuffer_[512] = "chimera_recipe.json";
    char reflectionProjectPathBuffer_[512] = "reflection_workbench.json";
    char realLensPathBuffer_[512] = "holobench_lens.json";
    char slmCalibrationPathBuffer_[512] = "slm_response.json";
    char slmProjectPathBuffer_[512] = "slm_experiment.json";
    char waveProjectPathBuffer_[512] = "wave_workbench.json";
    char holographyProjectPathBuffer_[512] = "holography_experiment.json";
    char lessonProgressPathBuffer_[512] = "holobench_lesson_progress.json";

    bool isBenchmark_ = false;
    int vsyncInterval_ = 1;
    int lastViewportWidth_ = 0;
    int lastViewportHeight_ = 0;
    bool glSmokeMode_ = false;
    bool localizedSmokeTextSubmitted_ = false;
};

} // namespace holobench::app
