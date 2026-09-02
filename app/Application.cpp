#include "app/Application.hpp"

#include <glad/gl.h>
#include <SDL3/SDL.h>
#include <backends/imgui_impl_opengl3.h>
#include <backends/imgui_impl_sdl3.h>
#include <imgui.h>
#include <imgui_internal.h>
#include <glm/trigonometric.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cctype>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <optional>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldVisualization.hpp"
#include "core/field/RgbFieldVisualization.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/io/LensPrescriptionIO.hpp"
#include "optics/slm/SlmResponseIO.hpp"
#include "app/HolographyProject.hpp"
#include "app/BenchAlignment.hpp"
#include "app/BenchHolographyPresets.hpp"
#include "app/BenchObservationOverlay.hpp"
#include "app/BenchWaveObservation.hpp"
#include "app/BenchWavePresets.hpp"
#include "app/BenchRecordingRecipe.hpp"
#include "app/BenchSourceSpectrum.hpp"
#include "app/ChimeraBenchWorkflow.hpp"
#include "app/ChimeraPerspectiveManifest.hpp"
#include "app/DetectorResponseAssets.hpp"
#include "app/SlmResponseAssets.hpp"
#include "app/SlmInterferenceProject.hpp"
#include "app/UiFont.hpp"
#include "app/lessons/LessonProgress.hpp"
#include "app/lessons/LessonTemplateRepository.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/NumericalAperture.hpp"
#include "optics/scene/OpticalBenchScene.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"
#include "render/OpticalBenchRenderer.hpp"
#include "render/gl/GlDebug.hpp"
#include "render/gl/Texture2D.hpp"

namespace holobench::app {
namespace {

constexpr const char* kBenchComponentDragPayload
    = "HOLOBENCH_BENCH_COMPONENT_KIND";

constexpr std::array<std::size_t, 4> kWavePreviewSampleLimits {
    64U, 128U, 256U, 512U};
constexpr std::array<std::size_t, 4> kWaveCommittedSampleLimits {
    128U, 256U, 512U, 1024U};
constexpr std::array<const char*, 4> kWavePreviewSampleLimitLabels {
    "64", "128", "256", "512"};
constexpr std::array<const char*, 4> kWaveCommittedSampleLimitLabels {
    "128", "256", "512", "1024"};

[[nodiscard]] std::size_t waveSampleLimit(
    const std::array<std::size_t, 4>& limits,
    int index) noexcept {
    return limits[static_cast<std::size_t>(std::clamp(
        index, 0, static_cast<int>(limits.size() - 1U)))];
}

[[nodiscard]] std::string joinedBenchIdentifiers(
    const std::vector<std::string>& identifiers) {
    std::string result;
    for (const auto& identifier : identifiers) {
        if (!result.empty()) {
            result += " -> ";
        }
        result += identifier;
    }
    return result;
}

[[nodiscard]] std::string waveChannelLabel(
    const BenchWaveObservationResult& observation) {
    std::array<char, 256> label {};
    std::snprintf(
        label.data(),
        label.size(),
        "%.3f nm | %s",
        observation.fieldAtObservation.vacuumWavelengthMetres() * 1e9,
        observation.coherenceId.c_str());
    return label.data();
}

template <std::size_t Size>
void copyTextToBuffer(std::string_view text, char (&buffer)[Size]) noexcept {
    static_assert(Size > 0U);
    std::fill(std::begin(buffer), std::end(buffer), '\0');
    const auto count = std::min(text.size(), Size - 1U);
    std::copy_n(text.begin(), count, buffer);
}

[[nodiscard]] std::optional<std::string> optionalInstrumentText(
    const char* text) {
    if (text == nullptr || text[0] == '\0') {
        return std::nullopt;
    }
    return std::string(text);
}

optics::sensor::CalibratedCameraSpectralResponse
makeNominalChimeraCameraResponse() {
    return {"nominal-rgb-camera-preview-v1", {
        {450e-9, {0.05, 0.15, 1.0}},
        {532e-9, {0.10, 1.0, 0.10}},
        {638e-9, {1.0, 0.10, 0.05}},
    }};
}

bool containsAsciiCaseInsensitive(
    std::string_view text,
    std::string_view query) {
    if (query.empty()) {
        return true;
    }
    const auto lower = [](char value) {
        return static_cast<char>(std::tolower(
            static_cast<unsigned char>(value)));
    };
    return std::search(text.begin(), text.end(), query.begin(), query.end(),
        [lower](char lhs, char rhs) { return lower(lhs) == lower(rhs); })
        != text.end();
}

void captureLastItemBounds(UiItemBounds& bounds) noexcept {
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    bounds = {
        .minimum = {minimum.x, minimum.y},
        .maximum = {maximum.x, maximum.y},
        .current = maximum.x > minimum.x && maximum.y > minimum.y,
    };
}

int sandboxConstraintAxisIndex(SandboxGizmoConstraint constraint) noexcept {
    switch (constraint) {
    case SandboxGizmoConstraint::AxisX: return 0;
    case SandboxGizmoConstraint::AxisY: return 1;
    case SandboxGizmoConstraint::AxisZ: return 2;
    case SandboxGizmoConstraint::None:
    case SandboxGizmoConstraint::ViewPlane: return -1;
    }
    return -1;
}

gizmo::LocalRotationAxis localRotationAxisForIndex(int index) {
    switch (index) {
    case 0: return gizmo::LocalRotationAxis::X;
    case 1: return gizmo::LocalRotationAxis::Y;
    case 2: return gizmo::LocalRotationAxis::Z;
    default: throw std::logic_error("invalid sandbox gizmo axis index");
    }
}

[[nodiscard]] float sandboxComponentFocusRadius(
    const optics::scene::BenchComponent& component) {
    namespace bench = optics::scene;
    double width = 0.05;
    double height = 0.05;
    switch (component.kind) {
    case bench::BenchComponentKind::LaserSource: {
        const auto& value = std::get<bench::LaserSourceParameters>(
            component.parameters);
        width = height = 2.0 * value.beamRadiusMetres;
        break;
    }
    case bench::BenchComponentKind::ObjectWavefrontSource: {
        const auto& value = std::get<bench::ObjectWavefrontSourceParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::PlanarMirror: {
        const auto& value = std::get<bench::PlanarMirrorParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::BeamSplitterCombiner: {
        const auto& value = std::get<bench::BeamSplitterParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::IdealThinLens: {
        const auto& value = std::get<bench::IdealThinLensParameters>(
            component.parameters);
        width = height = value.clearApertureDiameterMetres;
        break;
    }
    case bench::BenchComponentKind::RealLensAssembly: {
        const auto& value = std::get<bench::RealLensAssemblyParameters>(
            component.parameters);
        width = height = value.clearApertureDiameterMetres;
        break;
    }
    case bench::BenchComponentKind::Aperture: {
        const auto& value = std::get<bench::ApertureParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::SpatialFilter: {
        const auto& value = std::get<bench::SpatialFilterParameters>(
            component.parameters);
        width = height = value.clearApertureDiameterMetres;
        break;
    }
    case bench::BenchComponentKind::SpatialLightModulator: {
        const auto& value = std::get<bench::SpatialLightModulatorParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::ScreenDetector: {
        const auto& value = std::get<bench::ScreenDetectorParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::FieldProbe: {
        const auto& value = std::get<bench::FieldProbeParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::HolographicPlate: {
        const auto& value = std::get<bench::HolographicPlateParameters>(
            component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    }
    return static_cast<float>(std::max(
        0.03, 0.5 * std::hypot(width, height)));
}

GLADapiproc loadOpenGlProcedure(const char* name) {
    return reinterpret_cast<GLADapiproc>(SDL_GL_GetProcAddress(name));
}

template <typename T = ImTextureID>
constexpr T toImTextureID(GLuint textureId) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(static_cast<std::uintptr_t>(textureId));
    } else {
        return static_cast<T>(textureId);
    }
}

[[nodiscard]] bool drawPlacedObservationTexture(
    ImDrawList& drawList,
    GLuint texture,
    const optics::scene::BenchComponent& observation,
    const glm::mat4& viewProjection,
    const glm::vec2& viewportMinimum,
    const glm::vec2& viewportSize,
    std::string_view label,
    std::string& diagnostic) {
    const auto quad
        = observationoverlay::makeObservationPlaneQuad(observation);
    std::array<gizmo::ProjectedPoint, 4> projected;
    bool allVisible = true;
    for (std::size_t corner = 0; corner < quad.worldCorners.size(); ++corner) {
        const auto world = quad.worldCorners[corner];
        projected[corner] = gizmo::projectWorldToViewport(
            glm::vec3(
                static_cast<float>(world.x),
                static_cast<float>(world.y),
                static_cast<float>(world.z)),
            viewProjection,
            viewportMinimum,
            viewportSize);
        allVisible = allVisible && projected[corner].visible;
    }
    if (!allVisible) {
        diagnostic
            = "placed Screen/Probe is not fully visible in the current camera";
        return false;
    }
    const std::array<ImVec2, 4> points {{
        {projected[0].screenPos.x, projected[0].screenPos.y},
        {projected[1].screenPos.x, projected[1].screenPos.y},
        {projected[2].screenPos.x, projected[2].screenPos.y},
        {projected[3].screenPos.x, projected[3].screenPos.y},
    }};
    drawList.AddImageQuad(
        toImTextureID(texture),
        points[0],
        points[1],
        points[2],
        points[3],
        ImVec2(0.0F, 0.0F),
        ImVec2(1.0F, 0.0F),
        ImVec2(1.0F, 1.0F),
        ImVec2(0.0F, 1.0F),
        IM_COL32(255, 255, 255, 235));
    drawList.AddPolyline(
        points.data(),
        static_cast<int>(points.size()),
        IM_COL32(80, 238, 170, 255),
        ImDrawFlags_Closed,
        2.5F);
    const std::string completeLabel
        = "CURRENT FIELD | " + std::string(label);
    drawList.AddText(
        ImVec2(points[3].x + 6.0F, points[3].y - 18.0F),
        IM_COL32(100, 255, 190, 255),
        completeLabel.c_str());
    diagnostic = "current field quad submitted";
    return true;
}

void drawGizmoHandle(
    ImDrawList* drawList,
    const gizmo::ProjectedPoint& projCenter,
    const gizmo::AxisProjection& axisProj,
    bool isHovered,
    bool isSelected,
    bool isDragged,
    const char* label,
    double zMetres,
    ImU32 primaryColor,
    ImU32 hoverColor) {
    if (!projCenter.visible || drawList == nullptr) {
        return;
    }

    const ImVec2 center(projCenter.screenPos.x, projCenter.screenPos.y);
    const bool isHighlighted = isHovered || isSelected || isDragged;
    const ImU32 activeColor = isHighlighted ? hoverColor : primaryColor;

    // +Z Axis Arrow & Drag Hint
    if (!axisProj.isDegenerate) {
        constexpr float kArrowShaftLength = 40.0F;
        const ImVec2 shaftEnd(
            center.x + axisProj.screenDir.x * kArrowShaftLength,
            center.y + axisProj.screenDir.y * kArrowShaftLength);
        const float shaftThickness = isDragged ? 3.0F : (isHighlighted ? 2.2F : 1.4F);
        drawList->AddLine(center, shaftEnd, activeColor, shaftThickness);

        // Arrowhead
        const ImVec2 perp(-axisProj.screenDir.y, axisProj.screenDir.x);
        constexpr float kHeadLen = 8.0F;
        constexpr float kHeadWidth = 5.0F;
        const ImVec2 tip = shaftEnd;
        const ImVec2 left(
            tip.x - axisProj.screenDir.x * kHeadLen + perp.x * kHeadWidth,
            tip.y - axisProj.screenDir.y * kHeadLen + perp.y * kHeadWidth);
        const ImVec2 right(
            tip.x - axisProj.screenDir.x * kHeadLen - perp.x * kHeadWidth,
            tip.y - axisProj.screenDir.y * kHeadLen - perp.y * kHeadWidth);
        drawList->AddTriangleFilled(tip, left, right, activeColor);

        // "+Z" Label
        const ImVec2 textPos(
            tip.x + axisProj.screenDir.x * 5.0F + perp.x * 3.0F,
            tip.y + axisProj.screenDir.y * 5.0F + perp.y * 3.0F - 6.0F);
        drawList->AddText(textPos, activeColor, "+Z");
    }

    // Outer Ring
    const float ringRadius = isDragged ? 13.0F : (isHovered ? 12.0F : (isSelected ? 11.0F : 9.5F));
    const float ringThickness = isHighlighted ? 2.5F : 1.8F;
    drawList->AddCircle(center, ringRadius, activeColor, 24, ringThickness);

    if (isSelected || isDragged) {
        // Halo ring
        const ImU32 haloColor = (activeColor & 0x00FFFFFF) | 0x55000000;
        drawList->AddCircle(center, ringRadius + 4.0F, haloColor, 24, 1.5F);
    }

    // Inner Dot
    const ImU32 innerDotColor = isDragged ? IM_COL32(255, 255, 255, 255) : (isHighlighted ? activeColor : primaryColor);
    drawList->AddCircleFilled(center, isDragged ? 5.0F : 4.0F, innerDotColor, 16);

    // Component Label Pill
    char labelBuf[96];
    std::snprintf(labelBuf, sizeof(labelBuf), "%s (Z: %.1f mm)", label, zMetres * 1000.0);
    const ImVec2 txtSize = ImGui::CalcTextSize(labelBuf);
    const ImVec2 pillPos(center.x + 15.0F, center.y - txtSize.y * 0.5F);
    const ImVec2 pillMin(pillPos.x - 4.0F, pillPos.y - 2.0F);
    const ImVec2 pillMax(pillPos.x + txtSize.x + 4.0F, pillPos.y + txtSize.y + 2.0F);

    drawList->AddRectFilled(pillMin, pillMax, IM_COL32(15, 23, 42, 220), 4.0F);
    const ImU32 borderColor = isHighlighted ? activeColor : IM_COL32(100, 116, 139, 160);
    drawList->AddRect(pillMin, pillMax, borderColor, 4.0F, 0, 1.0F);
    drawList->AddText(pillPos, IM_COL32(241, 245, 249, 255), labelBuf);
}

void drawViewportHud(
    ImDrawList* drawList,
    const ImVec2& imagePosMin,
    GizmoTarget selectedTarget,
    GizmoTarget draggedTarget,
    bool isGizmoDragging) {
    if (drawList == nullptr) {
        return;
    }

    const char* selStr = (selectedTarget == GizmoTarget::Lens) ? "Lens"
        : (selectedTarget == GizmoTarget::Screen) ? "Screen" : "None";

    char statusBuf[160];
    if (isGizmoDragging) {
        const char* dName = (draggedTarget == GizmoTarget::Lens) ? "Lens" : "Screen";
        std::snprintf(statusBuf, sizeof(statusBuf), "Dragging %s (+Z optical axis) | ESC to cancel", dName);
    } else {
        std::snprintf(statusBuf, sizeof(statusBuf), "Gizmo: %s selected | LMB on handle to drag +Z | RMB: Orbit | MMB: Pan", selStr);
    }

    const ImVec2 hudPos(imagePosMin.x + 12.0F, imagePosMin.y + 12.0F);
    const ImVec2 txtSize = ImGui::CalcTextSize(statusBuf);
    const ImVec2 hudMin(hudPos.x - 6.0F, hudPos.y - 4.0F);
    const ImVec2 hudMax(hudPos.x + txtSize.x + 6.0F, hudPos.y + txtSize.y + 4.0F);

    drawList->AddRectFilled(hudMin, hudMax, IM_COL32(15, 23, 42, 200), 5.0F);
    drawList->AddRect(hudMin, hudMax, isGizmoDragging ? IM_COL32(56, 189, 248, 200) : IM_COL32(71, 85, 105, 160), 5.0F, 0, 1.0F);
    drawList->AddText(hudPos, isGizmoDragging ? IM_COL32(125, 211, 252, 255) : IM_COL32(226, 232, 240, 240), statusBuf);
}

[[nodiscard]] ImU32 wavelengthColor(double wavelengthMetres, int alpha = 210) {
    if (wavelengthMetres < 540e-9) {
        return IM_COL32(96, 165, 250, alpha);
    }
    if (wavelengthMetres < 620e-9) {
        return IM_COL32(74, 222, 128, alpha);
    }
    return IM_COL32(248, 113, 113, alpha);
}

[[nodiscard]] const char* lessonStatusName(lessons::LessonStatus status) noexcept {
    switch (status) {
    case lessons::LessonStatus::Locked:
        return "Locked";
    case lessons::LessonStatus::Available:
        return "Available";
    case lessons::LessonStatus::InProgress:
        return "In progress";
    case lessons::LessonStatus::Completed:
        return "Completed";
    }
    return "Unknown";
}

[[nodiscard]] const char* imageNatureName(
    optics::scene::ImageNature nature) noexcept {
    switch (nature) {
    case optics::scene::ImageNature::Real:
        return "Real";
    case optics::scene::ImageNature::Virtual:
        return "Virtual";
    case optics::scene::ImageNature::AtInfinity:
        return "At infinity";
    }
    return "Unknown";
}

[[nodiscard]] std::filesystem::path lessonTemplateRoot() {
    const char* basePath = SDL_GetBasePath();
    return std::filesystem::path(
        basePath != nullptr && basePath[0] != '\0' ? basePath : ".")
        / "lesson_templates";
}

[[nodiscard]] std::filesystem::path packagedFontPath() {
    const char* basePath = SDL_GetBasePath();
    return std::filesystem::path(
        basePath != nullptr && basePath[0] != '\0' ? basePath : ".")
        / "assets" / "fonts" / "NotoSansCJKsc-Regular.otf";
}

[[nodiscard]] math::Vec3d rotateAroundAxis(
    math::Vec3d value,
    math::Vec3d unitAxis,
    double angleRadians) {
    return value * std::cos(angleRadians)
        + math::cross(unitAxis, value) * std::sin(angleRadians)
        + unitAxis * math::dot(unitAxis, value) * (1.0 - std::cos(angleRadians));
}

void tiltSurfaceFrame(
    optics::ray::PrescriptionSurface& surface,
    std::size_t localAxisIndex,
    double angleRadians) {
    auto& frame = surface.localToWorld;
    const std::array<math::Vec3d, 3> original {
        frame.localXAxisInWorld,
        frame.localYAxisInWorld,
        frame.localZAxisInWorld,
    };
    const math::Vec3d axis = original.at(localAxisIndex);
    frame.localXAxisInWorld = math::normalized(rotateAroundAxis(original[0], axis, angleRadians));
    frame.localYAxisInWorld = math::normalized(rotateAroundAxis(original[1], axis, angleRadians));
    frame.localZAxisInWorld = math::normalized(rotateAroundAxis(original[2], axis, angleRadians));
}

void drawRealLensSystemPlot(
    const reallens::RealLensWorkbenchConfig& config,
    const reallens::RealLensWorkbenchResult& result,
    const ImVec2& requestedSize) {
    const ImVec2 size(std::max(requestedSize.x, 160.0F), std::max(requestedSize.y, 180.0F));
    ImGui::InvisibleButton("##real_lens_system_plot", size);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(minimum, maximum, IM_COL32(10, 15, 26, 255), 4.0F);
    drawList->AddRect(minimum, maximum, IM_COL32(71, 85, 105, 255), 4.0F);

    std::vector<std::vector<math::Vec3d>> surfaceProfiles;
    surfaceProfiles.reserve(config.prescription.surfaces.size());
    double minimumZ = std::numeric_limits<double>::infinity();
    double maximumZ = -std::numeric_limits<double>::infinity();
    double minimumX = std::numeric_limits<double>::infinity();
    double maximumX = -std::numeric_limits<double>::infinity();
    const auto includePoint = [&](math::Vec3d point) {
        minimumZ = std::min(minimumZ, point.z);
        maximumZ = std::max(maximumZ, point.z);
        minimumX = std::min(minimumX, point.x);
        maximumX = std::max(maximumX, point.x);
    };
    for (const reallens::TracePolyline& polyline : result.tracePolylines) {
        for (const math::Vec3d point : polyline.worldPointsMetres) {
            includePoint(point);
        }
    }
    try {
        for (const optics::ray::PrescriptionSurface& surface : config.prescription.surfaces) {
            auto& profile = surfaceProfiles.emplace_back();
            constexpr std::size_t kSamples = 65;
            profile.reserve(kSamples);
            for (std::size_t index = 0; index < kSamples; ++index) {
                const double localX = -surface.geometry.clearSemiDiameterMetres
                    + 2.0 * surface.geometry.clearSemiDiameterMetres
                        * static_cast<double>(index) / static_cast<double>(kSamples - 1);
                const auto sag = optics::ray::evaluateSurfaceSag(
                    surface.geometry, std::abs(localX));
                const math::Vec3d world = math::transformPointLocalToWorld(
                    surface.localToWorld, {localX, 0.0, sag.sagMetres});
                profile.push_back(world);
                includePoint(world);
            }
        }
    } catch (const std::exception&) {
        surfaceProfiles.clear();
    }
    includePoint(config.imagePlaneLocalToWorld.translationMetres);
    if (!std::isfinite(minimumZ) || !std::isfinite(minimumX)) {
        return;
    }
    const double zPadding = std::max((maximumZ - minimumZ) * 0.04, 1e-4);
    const double xExtent = std::max({std::abs(minimumX), std::abs(maximumX), 1e-4}) * 1.15;
    minimumZ -= zPadding;
    maximumZ += zPadding;
    minimumX = -xExtent;
    maximumX = xExtent;
    const auto project = [&](math::Vec3d point) {
        const float u = static_cast<float>((point.z - minimumZ) / (maximumZ - minimumZ));
        const float v = static_cast<float>((point.x - minimumX) / (maximumX - minimumX));
        return ImVec2(
            minimum.x + 8.0F + u * (size.x - 16.0F),
            maximum.y - 8.0F - v * (size.y - 16.0F));
    };
    const ImVec2 axisStart = project({0.0, 0.0, minimumZ});
    const ImVec2 axisEnd = project({0.0, 0.0, maximumZ});
    drawList->AddLine(axisStart, axisEnd, IM_COL32(100, 116, 139, 150), 1.0F);

    for (const reallens::TracePolyline& polyline : result.tracePolylines) {
        for (std::size_t index = 1; index < polyline.worldPointsMetres.size(); ++index) {
            drawList->AddLine(
                project(polyline.worldPointsMetres[index - 1]),
                project(polyline.worldPointsMetres[index]),
                wavelengthColor(polyline.vacuumWavelengthMetres, 45),
                1.0F);
        }
    }
    for (const auto& profile : surfaceProfiles) {
        for (std::size_t index = 1; index < profile.size(); ++index) {
            drawList->AddLine(
                project(profile[index - 1]),
                project(profile[index]),
                IM_COL32(226, 232, 240, 255),
                2.0F);
        }
    }
    const double imageZ = config.imagePlaneLocalToWorld.translationMetres.z;
    drawList->AddLine(
        project({-xExtent, 0.0, imageZ}),
        project({xExtent, 0.0, imageZ}),
        IM_COL32(251, 191, 36, 220),
        1.5F);
    drawList->AddText(
        ImVec2(minimum.x + 8.0F, minimum.y + 6.0F),
        IM_COL32(203, 213, 225, 255),
        "XZ sequential trace (SI geometry)");
}

void drawRealLensSpotPlot(
    const optics::analysis::SpotDiagramResult& spot,
    const ImVec2& requestedSize) {
    const ImVec2 size(std::max(requestedSize.x, 160.0F), std::max(requestedSize.y, 180.0F));
    ImGui::InvisibleButton("##real_lens_spot_plot", size);
    const ImVec2 minimum = ImGui::GetItemRectMin();
    const ImVec2 maximum = ImGui::GetItemRectMax();
    ImDrawList* drawList = ImGui::GetWindowDrawList();
    drawList->AddRectFilled(minimum, maximum, IM_COL32(10, 15, 26, 255), 4.0F);
    drawList->AddRect(minimum, maximum, IM_COL32(71, 85, 105, 255), 4.0F);
    const double centerX = spot.statistics.centroidXMetres;
    const double centerY = spot.statistics.centroidYMetres;
    double extent = 1e-6;
    for (const optics::analysis::SpotSample& sample : spot.samples) {
        extent = std::max({
            extent,
            std::abs(sample.imageXMetres - centerX),
            std::abs(sample.imageYMetres - centerY),
        });
    }
    extent *= 1.10;
    const ImVec2 center((minimum.x + maximum.x) * 0.5F, (minimum.y + maximum.y) * 0.5F);
    const float scale = 0.45F * std::min(size.x, size.y) / static_cast<float>(extent);
    drawList->AddLine(
        ImVec2(minimum.x + 6.0F, center.y), ImVec2(maximum.x - 6.0F, center.y),
        IM_COL32(100, 116, 139, 130));
    drawList->AddLine(
        ImVec2(center.x, minimum.y + 6.0F), ImVec2(center.x, maximum.y - 6.0F),
        IM_COL32(100, 116, 139, 130));
    for (const optics::analysis::SpotSample& sample : spot.samples) {
        const float x = center.x + static_cast<float>(sample.imageXMetres - centerX) * scale;
        const float y = center.y - static_cast<float>(sample.imageYMetres - centerY) * scale;
        drawList->AddCircleFilled(
            ImVec2(x, y), 1.8F, wavelengthColor(sample.vacuumWavelengthMetres), 8);
    }
    char label[128];
    std::snprintf(
        label, sizeof(label), "Spot: RMS %.2f um | scale +/- %.2f um",
        spot.statistics.rmsRadiusMetres * 1e6, extent * 1e6);
    drawList->AddText(
        ImVec2(minimum.x + 8.0F, minimum.y + 6.0F),
        IM_COL32(203, 213, 225, 255), label);
}

BenchProject makeDefaultSandboxProject() {
    namespace bench = optics::scene;
    BenchProject project;
    project.projectId = "starter-rgb-branch-bench";
    project.name = "Starter RGB Branch Bench";

    auto laser = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::LaserSource, "laser-rgb");
    laser.transform.translationMetres = {0.0, 0.0, 0.15};
    auto laserParameters = std::get<bench::LaserSourceParameters>(laser.parameters);
    laserParameters.channels = {
        {.wavelengthMetres = 638e-9, .powerWatts = 0.30, .coherenceId = "rgb-red"},
        {.wavelengthMetres = 532e-9, .powerWatts = 0.30, .coherenceId = "rgb-green"},
        {.wavelengthMetres = 450e-9, .powerWatts = 0.30, .coherenceId = "rgb-blue"},
    };
    laser.parameters = laserParameters;
    project.scene.add(std::move(laser));

    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    auto splitter = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::BeamSplitterCombiner, "splitter-main");
    splitter.transform = {
        .translationMetres = {0.0, 0.0, 0.45},
        .localXAxisInWorld = {inverseSqrtTwo, 0.0, inverseSqrtTwo},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {-inverseSqrtTwo, 0.0, inverseSqrtTwo},
    };
    project.scene.add(std::move(splitter));

    auto mirror = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::PlanarMirror, "mirror-object-arm");
    mirror.transform = {
        .translationMetres = {0.25, 0.0, 0.45},
        .localXAxisInWorld = {-inverseSqrtTwo, 0.0, -inverseSqrtTwo},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {inverseSqrtTwo, 0.0, -inverseSqrtTwo},
    };
    project.scene.add(std::move(mirror));

    auto lens = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::IdealThinLens, "lens-reference-arm");
    lens.transform.translationMetres = {0.0, 0.0, 0.62};
    project.scene.add(std::move(lens));

    auto transmittedScreen = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::ScreenDetector, "screen-reference-arm");
    transmittedScreen.transform.translationMetres = {0.0, 0.0, 0.85};
    project.scene.add(std::move(transmittedScreen));

    auto reflectedScreen = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::ScreenDetector, "screen-object-arm");
    reflectedScreen.transform.translationMetres = {0.25, 0.0, 0.75};
    project.scene.add(std::move(reflectedScreen));

    const std::array<bench::BenchComponentKind, 7> shelfKinds {
        bench::BenchComponentKind::ObjectWavefrontSource,
        bench::BenchComponentKind::RealLensAssembly,
        bench::BenchComponentKind::Aperture,
        bench::BenchComponentKind::SpatialFilter,
        bench::BenchComponentKind::SpatialLightModulator,
        bench::BenchComponentKind::FieldProbe,
        bench::BenchComponentKind::HolographicPlate,
    };
    for (std::size_t index = 0; index < shelfKinds.size(); ++index) {
        const auto kind = shelfKinds[index];
        auto component = bench::makeDefaultBenchComponent(
            kind, "shelf-" + std::string(bench::benchComponentKindName(kind)));
        component.transform.translationMetres = {
            -0.35,
            0.10,
            0.22 + static_cast<double>(index) * 0.105,
        };
        project.scene.add(std::move(component));
    }
    return project;
}

} // namespace

Application::Application()
    : renderer_(std::make_unique<render::OpticalBenchRenderer>())
    , detectorFftBackend_(std::make_unique<compute::fft::CpuFftBackend>())
    , detectorTexture_(std::make_unique<render::gl::Texture2D>())
    , samplingSpectrumTexture_(std::make_unique<render::gl::Texture2D>())
    , fourFObjectTexture_(std::make_unique<render::gl::Texture2D>())
    , fourFBeforeFilterTexture_(std::make_unique<render::gl::Texture2D>())
    , fourFAfterFilterTexture_(std::make_unique<render::gl::Texture2D>())
    , fourFImageTexture_(std::make_unique<render::gl::Texture2D>())
    , slmInterferenceTexture_(std::make_unique<render::gl::Texture2D>())
    , holographyTexture_(std::make_unique<render::gl::Texture2D>())
    , sandboxPlateTexture_(std::make_unique<render::gl::Texture2D>())
    , sandboxReplayTexture_(std::make_unique<render::gl::Texture2D>())
    , sandboxVolumeReplayTexture_(std::make_unique<render::gl::Texture2D>())
    , sandboxRgbReplayTexture_(std::make_unique<render::gl::Texture2D>())
    , sandboxWaveTexture_(std::make_unique<render::gl::Texture2D>())
    , chimeraCameraTexture_(std::make_unique<render::gl::Texture2D>())
    , reflectionRefractionResult_(
          reflection::evaluateReflectionRefraction(
              reflectionRefractionConfig_))
    , realLensConfig_(reallens::makeDefaultRealLensWorkbenchConfig())
    , realLensPrescriptionCatalog_(
          std::vector<optics::ray::SequentialLensPrescription> {
              realLensConfig_.prescription})
    , lessonLocalization_(lessons::makeDefaultLessonLocalization())
    , scene_(optics::scene::createDefaultRealImageScene())
    , naResult_(optics::scene::computeObjectSideNumericalAperture(scene_))
    , benchProject_(makeDefaultSandboxProject()) {
    tracerOptions_.rayCount = 64;
    tracerOptions_.pattern = optics::ray::RaySamplingPattern::FibonacciDisk;
    tracerOptions_.maxPropagationDistanceMetres = 2.0;
    tracerOptions_.includeVirtualExtensions = true;
    tracerOptions_.virtualExtensionDistanceMetres = 1.0;
    constexpr char kDefaultProjectPath[] = "holobench_scene.json";
    static_assert(sizeof(kDefaultProjectPath) <= sizeof(projectPathBuffer_));
    std::copy_n(kDefaultProjectPath, sizeof(kDefaultProjectPath), projectPathBuffer_);
}

Application::~Application() {
    shutdown();
}

bool Application::applyScene(
    const optics::scene::OpticalBenchScene& candidateScene,
    const optics::ray::BenchTracerOptions& candidateOptions) {
    static_assert(std::is_nothrow_move_assignable_v<optics::scene::OpticalBenchScene>);
    static_assert(std::is_nothrow_move_assignable_v<optics::scene::ThinLensImagePrediction>);
    static_assert(std::is_nothrow_move_assignable_v<optics::ray::BenchTracerOptions>);
    static_assert(std::is_nothrow_swappable_v<std::vector<optics::ray::RaySegment>>);
    static_assert(std::is_nothrow_move_assignable_v<std::string>);

    try {
        optics::scene::OpticalBenchScene sceneCopy = candidateScene;
        optics::scene::validateScene(sceneCopy);
        const auto pred = optics::scene::predictThinLensImage(sceneCopy);
        const auto na = optics::scene::computeObjectSideNumericalAperture(sceneCopy);
        optics::ray::traceBench(sceneCopy, candidateOptions, stagingRaySegments_);
        std::string newStatus = "Scene updated (" + std::to_string(stagingRaySegments_.size()) + " displayed segments)";

        if (viewportMode_ == ViewportMode::LegacyReference
            && (!renderer_ || !renderer_->updateScene(sceneCopy, pred, stagingRaySegments_))) {
            throw std::runtime_error("Renderer rejected scene geometry update");
        }

        scene_ = std::move(sceneCopy);
        prediction_ = pred;
        naResult_ = na;
        tracerOptions_ = candidateOptions;
        raySegments_.swap(stagingRaySegments_);
        errorMessage_.clear();
        statusMessage_ = std::move(newStatus);
        if (lessonEditHistoryReady_ && !restoringLessonEdit_
            && !isGizmoDragging_) {
            recordLessonEdit();
        }
        return true;
    } catch (const std::exception& ex) {
        errorMessage_ = ex.what();
        statusMessage_.clear();
        return false;
    }
}

bool Application::applyBenchScene(
    optics::scene::BenchScene candidateScene,
    std::string newStatusMessage,
    bool recordHistory) {
    BenchProject candidateProject = benchProject_;
    candidateProject.scene = std::move(candidateScene);
    return applyDynamicBenchProject(
        std::move(candidateProject), std::move(newStatusMessage), recordHistory);
}

bool Application::placeSandboxComponent(
    optics::scene::BenchComponentKind kind,
    const math::Vec3d& positionMetres,
    std::string statusMessage) {
    namespace bench = optics::scene;
    auto candidate = benchProject_.scene;
    std::string newId;
    do {
        newId = std::string(bench::benchComponentKindName(kind))
            + "-" + std::to_string(sandboxNextComponentOrdinal_++);
    } while (candidate.find(newId) != nullptr);
    auto component = bench::makeDefaultBenchComponent(kind, newId);
    auto mechanical = bench::MechanicalAssemblyState {};
    component.transform.translationMetres = positionMetres
        + component.transform.localYAxisInWorld
            * mechanical.postHeightMetres;
    mechanical = bench::makeDefaultMechanicalAssembly(component);
    bench::applyMechanicalAssembly(component, mechanical);
    candidate.add(component);
    const std::string previousSelection = selectedBenchComponentId_;
    selectedBenchComponentId_ = newId;
    if (!applyBenchScene(std::move(candidate), std::move(statusMessage))) {
        selectedBenchComponentId_ = previousSelection;
        return false;
    }
    return true;
}

bool Application::applyDynamicBenchProject(
    BenchProject candidateProject,
    std::string newStatusMessage,
    bool recordHistory) {
    try {
        validateBenchProject(candidateProject);
        validateLensPrescriptionAssetBindings(
            candidateProject.scene, realLensPrescriptionCatalog_);
        validateDetectorResponseAssetBindings(
            candidateProject.scene, detectorResponseCatalog_);
        validateSlmResponseAssetBindings(
            candidateProject.scene, slmResponseCatalog_);
        if (benchEditHistoryReady_
            && candidateProject.scene.revision() <= benchProject_.scene.revision()
            && !sameBenchEditState(candidateProject, benchProject_)) {
            candidateProject = rebaseBenchEditStateRevision(
                candidateProject, benchProject_.scene.revision());
        }
        const std::string selection
            = candidateProject.scene.find(selectedBenchComponentId_) != nullptr
            ? selectedBenchComponentId_ : std::string {};
        const auto traceGraph = optics::ray::traceDynamicBench(
            candidateProject.scene,
            benchTraceBudget_,
            &realLensPrescriptionCatalog_);
        if (!renderer_ || !renderer_->updateDynamicScene(
                candidateProject.scene, traceGraph, selection)) {
            throw std::runtime_error("renderer rejected dynamic bench geometry");
        }
        benchProject_ = std::move(candidateProject);
        benchTraceGraph_ = traceGraph;
        selectedBenchComponentId_ = selection;
        viewportMode_ = ViewportMode::Sandbox;
        if (recordHistory && benchEditHistoryReady_) {
            static_cast<void>(benchEditHistory_.record(benchProject_));
        }
        errorMessage_.clear();
        statusMessage_ = std::move(newStatusMessage);
        if (recordHistory && benchEditHistoryReady_) {
            autosaveBenchProjectAfterEdit();
        }
        return true;
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
        return false;
    }
}

bool Application::showSandboxViewport() {
    try {
        const auto traceGraph = optics::ray::traceDynamicBench(
            benchProject_.scene,
            benchTraceBudget_,
            &realLensPrescriptionCatalog_);
        if (!renderer_ || !renderer_->updateDynamicScene(
                benchProject_.scene, traceGraph, selectedBenchComponentId_)) {
            throw std::runtime_error("renderer rejected dynamic bench geometry");
        }
        benchTraceGraph_ = traceGraph;
        viewportMode_ = ViewportMode::Sandbox;
        errorMessage_.clear();
        statusMessage_ = "Free-form optical sandbox active";
        return true;
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
        return false;
    }
}

bool Application::showLegacyViewport() {
    if (!renderer_ || !renderer_->updateScene(scene_, prediction_, raySegments_)) {
        errorMessage_ = "renderer rejected legacy reference geometry";
        statusMessage_.clear();
        return false;
    }
    viewportMode_ = ViewportMode::LegacyReference;
    errorMessage_.clear();
    statusMessage_ = "Fixed-axis reference scene active";
    return true;
}

void Application::loadBenchProjectFromPath() {
    try {
        auto recovered = loadBenchProjectWithRecovery(benchProjectPathBuffer_);
        optics::ray::LensPrescriptionCatalog recoveredCatalog({
            optics::ray::makeDefaultNBk7BiconvexPrescription()});
        restoreLensPrescriptionAssets(
            recovered.project.scene,
            std::filesystem::path(benchProjectPathBuffer_),
            recoveredCatalog);
        DetectorResponseCatalog recoveredDetectorCatalog;
        restoreDetectorResponseAssets(
            recovered.project.scene,
            std::filesystem::path(benchProjectPathBuffer_),
            recoveredDetectorCatalog);
        SlmResponseCatalog recoveredSlmCatalog;
        restoreSlmResponseAssets(
            recovered.project.scene,
            std::filesystem::path(benchProjectPathBuffer_),
            recoveredSlmCatalog);
        const std::string loadedName = recovered.project.name;
        const auto source = recovered.source;
        const bool ignoredInvalidAutosave = recovered.ignoredInvalidAutosave;
        const std::string previousSelection = selectedBenchComponentId_;
        auto previousCatalog = std::move(realLensPrescriptionCatalog_);
        auto previousDetectorCatalog
            = std::move(detectorResponseCatalog_);
        auto previousSlmCatalog = std::move(slmResponseCatalog_);
        realLensPrescriptionCatalog_ = std::move(recoveredCatalog);
        detectorResponseCatalog_ = std::move(recoveredDetectorCatalog);
        slmResponseCatalog_ = std::move(recoveredSlmCatalog);
        selectedBenchComponentId_.clear();
        if (applyDynamicBenchProject(
                std::move(recovered.project),
                "Loaded optical bench: " + loadedName)) {
            activeLensPrescriptionAsset_.reset();
            activeDetectorResponseAsset_.reset();
            activeSlmResponseAsset_.reset();
            if (source == BenchProjectRecoverySource::Primary) {
                discardBenchProjectAutosave(benchProjectPathBuffer_);
                statusMessage_ = ignoredInvalidAutosave
                    ? "Loaded optical bench; ignored an invalid autosave: "
                        + loadedName
                    : "Loaded optical bench: " + loadedName;
            } else {
                statusMessage_ = "Recovered unsaved optical bench edits: "
                    + loadedName;
            }
        } else {
            realLensPrescriptionCatalog_ = std::move(previousCatalog);
            detectorResponseCatalog_
                = std::move(previousDetectorCatalog);
            slmResponseCatalog_ = std::move(previousSlmCatalog);
            selectedBenchComponentId_ = previousSelection;
        }
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
    }
}

void Application::recordBenchEdit() {
    if (!benchEditHistoryReady_) {
        return;
    }
    try {
        static_cast<void>(benchEditHistory_.record(benchProject_));
        autosaveBenchProjectAfterEdit();
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
    }
}

bool Application::restoreBenchEditState(const BenchProject& state) {
    try {
        auto restored = rebaseBenchEditStateRevision(
            state, benchProject_.scene.revision());
        return applyDynamicBenchProject(
            std::move(restored), "Restored optical bench edit", false);
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
        return false;
    }
}

void Application::undoBenchEdit() {
    if (!benchEditHistoryReady_ || !benchEditHistory_.canUndo()) {
        return;
    }
    const BenchProject state = benchEditHistory_.undo();
    if (!restoreBenchEditState(state)) {
        static_cast<void>(benchEditHistory_.redo());
    } else {
        autosaveBenchProjectAfterEdit();
    }
}

void Application::redoBenchEdit() {
    if (!benchEditHistoryReady_ || !benchEditHistory_.canRedo()) {
        return;
    }
    const BenchProject state = benchEditHistory_.redo();
    if (!restoreBenchEditState(state)) {
        static_cast<void>(benchEditHistory_.undo());
    } else {
        autosaveBenchProjectAfterEdit();
    }
}

void Application::saveBenchProjectToPath() {
    try {
        saveBenchProject(benchProject_, benchProjectPathBuffer_);
        discardBenchProjectAutosave(benchProjectPathBuffer_);
        errorMessage_.clear();
        statusMessage_ = "Saved optical bench: " + std::string(benchProjectPathBuffer_);
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
    }
}

void Application::autosaveBenchProjectAfterEdit() {
    if (glSmokeMode_) {
        return;
    }
    try {
        saveBenchProjectAutosave(benchProject_, benchProjectPathBuffer_);
    } catch (const std::exception& error) {
        errorMessage_ = std::string("Bench autosave failed: ") + error.what();
        if (!statusMessage_.empty()) {
            statusMessage_ += " (autosave unavailable)";
        }
    }
}

void Application::buildChimeraBench(
    const chimera::ChimeraRecipe& recipe,
    std::string sourceLabel) {
    try {
        auto compiled = chimera::compileChimeraRecipe(recipe);
        const bool feasible = compiled.feasible();
        chimeraConstraintReport_ = compiled.constraints;
        selectedBenchComponentId_ = "chimera-plate";
        chimeraCameraLensComponentId_ = "chimera-camera-lens";
        sandboxObservationComponentId_ = "chimera-reconstruction-probe";
        const std::string status = "Compiled " + std::move(sourceLabel)
            + " to an editable CHIMERA-like bench"
            + (feasible ? "" : " with unsupported constraints");
        if (applyDynamicBenchProject(std::move(compiled.project), status)) {
            chimeraRecipe_ = recipe;
            chimeraWorkflow_.reset();
            chimeraBatch_.reset();
            chimeraSweepResult_.reset();
            sandboxRecordedExperiment_ = SandboxRecordedExperiment::None;
            sandboxPlateRecording_.reset();
            sandboxPlateReplay_.reset();
            sandboxVolumeRecording_.reset();
            sandboxVolumeReplay_.reset();
            sandboxVolumeObservationReplay_.reset();
            sandboxRgbRecording_.reset();
            sandboxRgbReplay_.reset();
            sandboxRgbVolumeRecording_.reset();
            sandboxRgbVolumeReplay_.reset();
            if (chimeraCameraTexture_) {
                chimeraCameraTexture_->destroy();
            }
        }
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
    }
}

void Application::prepareChimeraAutomation() {
    auto workflow = chimera::prepareChimeraBenchWorkflow(
        chimeraRecipe_, benchProject_);
    chimeraHogelX_ = std::clamp(
        chimeraHogelX_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countX - 1U));
    chimeraHogelY_ = std::clamp(
        chimeraHogelY_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countY - 1U));
    chimeraViewIndex_ = std::clamp(
        chimeraViewIndex_, 0,
        static_cast<int>(workflow.dataset.sourceViews.size() - 1U));
    const std::size_t eventCount = workflow.plan.events.size();
    const std::string datasetHash = workflow.dataset.contentHash;
    chimeraWorkflow_ = std::make_unique<chimera::ChimeraBenchWorkflow>(
        std::move(workflow));
    chimeraBatch_.reset();
    chimeraSweepResult_.reset();
    if (chimeraCameraTexture_) {
        chimeraCameraTexture_->destroy();
    }
    errorMessage_.clear();
    statusMessage_ = "Prepared CHIMERA dataset " + datasetHash
        + " and " + std::to_string(eventCount)
        + " deterministic exposure events from the current Bench";
}

void Application::prepareChimeraAutomationFromManifest() {
    auto views = chimera::loadPerspectiveViewManifest(
        chimeraViewManifestPathBuffer_, chimeraRecipe_);
    auto workflow = chimera::prepareChimeraBenchWorkflow(
        chimeraRecipe_, benchProject_, std::move(views));
    chimeraHogelX_ = std::clamp(
        chimeraHogelX_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countX - 1U));
    chimeraHogelY_ = std::clamp(
        chimeraHogelY_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countY - 1U));
    chimeraViewIndex_ = std::clamp(
        chimeraViewIndex_, 0,
        static_cast<int>(workflow.dataset.sourceViews.size() - 1U));
    const std::size_t viewCount = workflow.dataset.sourceViews.size();
    const std::size_t eventCount = workflow.plan.events.size();
    const std::string datasetHash = workflow.dataset.contentHash;
    chimeraWorkflow_ = std::make_unique<chimera::ChimeraBenchWorkflow>(
        std::move(workflow));
    chimeraBatch_.reset();
    chimeraSweepResult_.reset();
    if (chimeraCameraTexture_) {
        chimeraCameraTexture_->destroy();
    }
    errorMessage_.clear();
    statusMessage_ = "Prepared " + std::to_string(viewCount)
        + " real manifest views as dataset " + datasetHash + " and "
        + std::to_string(eventCount) + " exposure events";
}

void Application::executeSelectedChimeraHogel() {
    if (!chimeraWorkflow_ || !detectorFftBackend_) {
        throw std::runtime_error(
            "prepare CHIMERA data and an FFT backend before exposure");
    }
    const auto x = static_cast<std::size_t>(std::clamp(
        chimeraHogelX_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countX - 1U)));
    const auto y = static_cast<std::size_t>(std::clamp(
        chimeraHogelY_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countY - 1U)));
    chimera::HogelExposureExecutionOptions executionOptions;
    executionOptions.slmResponses = &slmResponseCatalog_;
    executionOptions.environmentTemperatureKelvin = 293.15;
    chimera::executeChimeraHogel(
        *chimeraWorkflow_,
        benchProject_,
        *detectorFftBackend_,
        x,
        y,
        executionOptions);
    const auto& channels = chimeraWorkflow_->exposures.back().channels;
    const bool m8Evidence = channels.size() == 3U
        && std::all_of(channels.begin(), channels.end(), [](const auto& channel) {
            return channel.m8VolumeRecordingInvoked
                && channel.sparseSlmRasterTransferredToPlacedWavePath;
        });
    if (!m8Evidence) {
        throw std::runtime_error(
            "selected CHIMERA hogel produced incomplete RGB M8 recording evidence");
    }
    errorMessage_.clear();
    statusMessage_ = "Exposed hogel (" + std::to_string(x) + ", "
        + std::to_string(y)
        + ") through three independent RGB M8 volume recordings";
}

void Application::reconstructSelectedChimeraHogel() {
    if (!chimeraWorkflow_) {
        throw std::runtime_error("prepare and expose a CHIMERA hogel first");
    }
    const auto x = static_cast<std::size_t>(std::clamp(
        chimeraHogelX_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countX - 1U)));
    const auto y = static_cast<std::size_t>(std::clamp(
        chimeraHogelY_, 0,
        static_cast<int>(chimeraRecipe_.hogels.countY - 1U)));
    const int maximumView = static_cast<int>(
        chimeraWorkflow_->dataset.sourceViews.size() - 1U);
    chimeraViewIndex_ = std::clamp(chimeraViewIndex_, 0, maximumView);
    const auto& view = chimeraWorkflow_->dataset.sourceViews[
        static_cast<std::size_t>(chimeraViewIndex_)];
    const std::array hogels {chimera::HogelSelection {.x = x, .y = y}};
    const std::array views {view.viewId};
    chimera::reconstructChimeraViews(
        *chimeraWorkflow_, benchProject_, hogels, views);

    chimera::CameraSensorRequest request;
    request.jobId = "chimera-bench-camera-preview";
    request.pixelWidth = 129U;
    request.pixelHeight = 129U;
    request.pixelPitchXMetres = 10e-6;
    request.pixelPitchYMetres = 10e-6;
    const auto nominalDetectorResponse
        = makeNominalChimeraCameraResponse();
    const std::array detectorWavelengths {
        chimeraRecipe_.rgb[0].wavelengthMetres,
        chimeraRecipe_.rgb[1].wavelengthMetres,
        chimeraRecipe_.rgb[2].wavelengthMetres,
    };
    const auto detectorSelection = selectPlacedDetectorResponse(
        benchProject_.scene,
        sandboxObservationComponentId_,
        detectorResponseCatalog_,
        nominalDetectorResponse,
        detectorWavelengths,
        293.15);
    chimera::captureChimeraCameraImage(
        *chimeraWorkflow_,
        benchProject_,
        request,
        detectorSelection,
        realLensPrescriptionCatalog_,
        chimeraCameraLensComponentId_,
        sandboxObservationComponentId_);
    const auto display = chimera::renderChimeraCameraImage(
        *chimeraWorkflow_->cameraImage,
        {1.0, 1.0, 1.0},
        static_cast<double>(chimeraCameraDisplayGamma_));
    if (!chimeraCameraTexture_
        || !chimeraCameraTexture_->uploadImage(display)) {
        throw std::runtime_error(
            "OpenGL rejected the CHIMERA camera reconstruction texture");
    }
    errorMessage_.clear();
    statusMessage_ = "Reconstructed " + view.viewId + " from hogel ("
        + std::to_string(x) + ", " + std::to_string(y)
        + ") through placed lens " + chimeraCameraLensComponentId_
        + " onto " + sandboxObservationComponentId_;
}

void Application::createChimeraBatch() {
    if (!chimeraWorkflow_
        || !chimera::isChimeraBenchWorkflowCurrent(
            *chimeraWorkflow_, benchProject_)) {
        throw std::runtime_error(
            "prepare current CHIMERA data before creating a batch");
    }
    chimeraBatch_ = std::make_unique<chimera::ChimeraBatchArtifact>(
        chimera::createChimeraBatchArtifact(
            "chimera-print-batch",
            chimeraWorkflow_->recipe,
            chimeraWorkflow_->dataset,
            chimeraWorkflow_->plan,
            benchProject_));
    chimera::saveChimeraBatchArtifact(
        *chimeraBatch_, chimeraBatchPathBuffer_);
    errorMessage_.clear();
    statusMessage_ = "Created atomic CHIMERA batch checkpoint: "
        + std::string(chimeraBatchPathBuffer_);
}

void Application::runChimeraBatchSlice() {
    if (!chimeraWorkflow_ || !chimeraBatch_ || !detectorFftBackend_) {
        throw std::runtime_error(
            "create a current CHIMERA batch before running it");
    }
    const std::size_t slice = static_cast<std::size_t>(
        std::clamp(chimeraBatchSliceHogels_, 1, 4));
    chimera::HogelExposureExecutionOptions executionOptions;
    executionOptions.slmResponses = &slmResponseCatalog_;
    executionOptions.environmentTemperatureKelvin = 293.15;
    const auto executed = chimera::runChimeraBatchSlice(
        *chimeraBatch_,
        chimeraWorkflow_->recipe,
        chimeraWorkflow_->dataset,
        chimeraWorkflow_->plan,
        benchProject_,
        *detectorFftBackend_,
        slice,
        executionOptions);
    for (auto exposure : executed.executedHogels) {
        const auto existing = std::find_if(
            chimeraWorkflow_->exposures.begin(),
            chimeraWorkflow_->exposures.end(),
            [&](const auto& value) {
                return value.hogelX == exposure.hogelX
                    && value.hogelY == exposure.hogelY;
            });
        if (existing == chimeraWorkflow_->exposures.end()) {
            chimeraWorkflow_->exposures.push_back(std::move(exposure));
        } else {
            *existing = std::move(exposure);
        }
    }
    chimera::saveChimeraBatchArtifact(
        *chimeraBatch_, chimeraBatchPathBuffer_);
    errorMessage_.clear();
    statusMessage_ = "Checkpointed CHIMERA batch at "
        + std::to_string(executed.completedHogelCount) + "/"
        + std::to_string(executed.totalHogelCount) + " complete hogels";
}

void Application::pauseChimeraBatch() {
    if (!chimeraWorkflow_ || !chimeraBatch_ || !detectorFftBackend_) {
        throw std::runtime_error("no current CHIMERA batch can be paused");
    }
    std::atomic_bool cancelled {true};
    chimera::HogelExposureExecutionOptions executionOptions;
    executionOptions.slmResponses = &slmResponseCatalog_;
    executionOptions.environmentTemperatureKelvin = 293.15;
    static_cast<void>(chimera::runChimeraBatchSlice(
        *chimeraBatch_,
        chimeraWorkflow_->recipe,
        chimeraWorkflow_->dataset,
        chimeraWorkflow_->plan,
        benchProject_,
        *detectorFftBackend_,
        1U,
        executionOptions,
        &cancelled));
    chimera::saveChimeraBatchArtifact(
        *chimeraBatch_, chimeraBatchPathBuffer_);
    errorMessage_.clear();
    statusMessage_ = "Paused CHIMERA batch at a complete RGB hogel boundary";
}

void Application::reconstructChimeraBatchRegion() {
    if (!chimeraWorkflow_ || !chimeraBatch_
        || chimeraBatch_->completedHogels.empty()) {
        throw std::runtime_error(
            "the current CHIMERA batch has no completed hogel region");
    }
    chimera::validateChimeraBatchForWorkflow(
        *chimeraBatch_,
        chimeraWorkflow_->recipe,
        chimeraWorkflow_->dataset,
        chimeraWorkflow_->plan,
        benchProject_);
    const std::size_t count = std::min(
        chimeraBatch_->completedHogels.size(),
        static_cast<std::size_t>(std::clamp(
            chimeraReconstructionRegionHogels_, 1, 64)));
    std::vector<chimera::HogelSelection> hogels;
    hogels.reserve(count);
    for (std::size_t index = 0; index < count; ++index) {
        hogels.push_back({
            .x = chimeraBatch_->completedHogels[index].hogelX,
            .y = chimeraBatch_->completedHogels[index].hogelY,
        });
    }
    chimeraViewIndex_ = std::clamp(
        chimeraViewIndex_, 0,
        static_cast<int>(
            chimeraWorkflow_->dataset.sourceViews.size() - 1U));
    const auto& view = chimeraWorkflow_->dataset.sourceViews[
        static_cast<std::size_t>(chimeraViewIndex_)];
    const std::array views {view.viewId};
    chimera::reconstructChimeraViews(
        *chimeraWorkflow_,
        benchProject_,
        hogels,
        views,
        "chimera-batch-region-preview");

    chimera::CameraSensorRequest request;
    request.jobId = "chimera-batch-region-camera-preview";
    request.pixelWidth = 129U;
    request.pixelHeight = 129U;
    request.pixelPitchXMetres = 10e-6;
    request.pixelPitchYMetres = 10e-6;
    const auto nominalDetectorResponse
        = makeNominalChimeraCameraResponse();
    const std::array detectorWavelengths {
        chimeraRecipe_.rgb[0].wavelengthMetres,
        chimeraRecipe_.rgb[1].wavelengthMetres,
        chimeraRecipe_.rgb[2].wavelengthMetres,
    };
    const auto detectorSelection = selectPlacedDetectorResponse(
        benchProject_.scene,
        sandboxObservationComponentId_,
        detectorResponseCatalog_,
        nominalDetectorResponse,
        detectorWavelengths,
        293.15);
    chimera::captureChimeraCameraImage(
        *chimeraWorkflow_,
        benchProject_,
        request,
        detectorSelection,
        realLensPrescriptionCatalog_,
        chimeraCameraLensComponentId_,
        sandboxObservationComponentId_);
    const auto display = chimera::renderChimeraCameraImage(
        *chimeraWorkflow_->cameraImage,
        {1.0, 1.0, 1.0},
        static_cast<double>(chimeraCameraDisplayGamma_));
    if (!chimeraCameraTexture_
        || !chimeraCameraTexture_->uploadImage(display)) {
        throw std::runtime_error(
            "OpenGL rejected the CHIMERA batch-region camera texture");
    }
    errorMessage_.clear();
    statusMessage_ = "Reconstructed bounded " + std::to_string(count)
        + "-hogel batch region for " + view.viewId + " through "
        + chimeraCameraLensComponentId_ + " onto "
        + sandboxObservationComponentId_;
}

void Application::runChimeraRelaySweep() {
    chimera::ChimeraSweepDefinition definition;
    definition.sweepId = "bench-relay-sweep";
    definition.baseRecipe = chimeraRecipe_;
    definition.axes.relayFocalLengthMetres.reserve(3U);
    for (const float value : chimeraSweepRelayFocalLengthsMillimetres_) {
        if (!std::isfinite(value) || value <= 0.0F) {
            throw std::invalid_argument(
                "CHIMERA relay sweep focal lengths must be positive");
        }
        definition.axes.relayFocalLengthMetres.push_back(
            static_cast<double>(value) * 1e-3);
    }
    if (!std::isfinite(chimeraSweepMaximumCrosstalk_)
        || chimeraSweepMaximumCrosstalk_ < 0.0F
        || chimeraSweepMaximumCrosstalk_ > 1.0F) {
        throw std::invalid_argument(
            "CHIMERA sweep cross-talk constraint must be in [0, 1]");
    }
    definition.constraints.maximumNearestViewCrosstalkFraction
        = static_cast<double>(chimeraSweepMaximumCrosstalk_);
    definition.maximumCandidateCount = 3U;
    chimeraSweepResult_ = std::make_unique<chimera::ChimeraSweepResult>(
        chimera::runChimeraParameterSweep(definition));
    errorMessage_.clear();
    statusMessage_ = "Evaluated three transparent CHIMERA relay candidates"
        + std::string(chimeraSweepResult_->bestCandidate() != nullptr
                ? " and selected a feasible best recipe"
                : " with no candidate satisfying every hard constraint");
}

void Application::applyBestChimeraSweepCandidate() {
    if (!chimeraSweepResult_ || chimeraSweepResult_->bestCandidate() == nullptr) {
        throw std::runtime_error(
            "CHIMERA sweep has no feasible best candidate to apply");
    }
    buildChimeraBench(
        chimeraSweepResult_->bestCandidate()->recipe,
        "best transparent relay-sweep candidate");
}

void Application::recomputeRecordingRecipe(
    const optics::holography::PlateIncidentFieldSet& fields,
    const HologramRecordingRecipe& recipe) {
    if (!detectorFftBackend_) {
        throw std::runtime_error("CPU FFT backend is unavailable");
    }
    const auto resolved = resolveRecordingRecipe(fields, recipe);
    sandboxPlateSampleSize_ = static_cast<int>(recipe.sampling.sampleWidth);
    sandboxPlateWindowMillimetres_ = static_cast<float>(
        recipe.sampling.extentWidthMetres * 1e3);
    sandboxPlateRelativeReferenceKilowattsPerSquareMetre_
        = static_cast<float>(
            recipe.relativeIntensityReferenceWattsPerSquareMetre * 1e-3);

    if (recipe.model == HologramRecordingModel::ThinTransmission) {
        optics::holography::ThinPlateRecordingOptions options;
        options.sampling = recipe.sampling;
        options.relativeIntensityReferenceWattsPerSquareMetre
            = recipe.relativeIntensityReferenceWattsPerSquareMetre;
        options.response = recipe.thinResponse;
        if (resolved.channels.size() == 1U) {
            const auto& channel = resolved.channels.front();
            auto recording = optics::holography::recordThinTransmissionPlate(
                benchProject_.scene,
                fields,
                channel.objectBranchId,
                channel.referenceBranchId,
                options,
                *detectorFftBackend_,
                &realLensPrescriptionCatalog_,
                &slmResponseCatalog_,
                293.15);
            field::FieldVisualizationOptions viewOptions;
            viewOptions.colormap = field::ColormapKind::Inferno;
            const auto image = field::renderLinearIntensity(
                recording.hologram.recordedRelativeIntensity, viewOptions);
            if (!sandboxPlateTexture_
                || !sandboxPlateTexture_->uploadImage(image)) {
                throw std::runtime_error(
                    "OpenGL rejected the sampled plate exposure texture");
            }
            sandboxPlateRecording_ = std::make_unique<
                optics::holography::ThinPlateRecordingResult>(
                    std::move(recording));
            sandboxRecordedExperiment_
                = SandboxRecordedExperiment::ThinTransmission;
            sandboxActiveRecordingRecipeId_ = recipe.recipeId;
            sandboxPlateReplay_.reset();
            if (sandboxReplayTexture_) {
                sandboxReplayTexture_->destroy();
            }
            statusMessage_ = "Recomputed saved thin recording recipe "
                + recipe.recipeId;
            return;
        }
        if (resolved.channels.size() == 3U) {
            const std::array selections {
                resolved.channels[0],
                resolved.channels[1],
                resolved.channels[2],
            };
            auto recording
                = optics::holography::recordRgbThinTransmissionPlate(
                    benchProject_.scene,
                    fields,
                    selections,
                    options,
                    *detectorFftBackend_,
                    &realLensPrescriptionCatalog_,
                    &slmResponseCatalog_,
                    293.15);
            sandboxRgbRecording_ = std::make_unique<
                optics::holography::RgbThinPlateRecordingResult>(
                    std::move(recording));
            sandboxRecordedExperiment_
                = SandboxRecordedExperiment::RgbFullColour;
            sandboxActiveRecordingRecipeId_ = recipe.recipeId;
            sandboxRgbReplay_.reset();
            if (sandboxRgbReplayTexture_) {
                sandboxRgbReplayTexture_->destroy();
            }
            statusMessage_ = "Recomputed saved RGB recording recipe "
                + recipe.recipeId;
            return;
        }
        throw std::invalid_argument(
            "thin recording recipe must contain one or three channels");
    }

    if (resolved.channels.size() != 1U
        && resolved.channels.size() != 3U) {
        throw std::invalid_argument(
            "volume recording recipe must contain one or three channels");
    }
    sandboxVolumeAverageRefractiveIndex_ = static_cast<float>(
        recipe.volumeMaterial.averageRefractiveIndex);
    sandboxVolumeIndexModulation_ = static_cast<float>(
        recipe.volumeMaterial.refractiveIndexModulation);
    sandboxVolumeShrinkagePercent_ = static_cast<float>(
        recipe.volumeMaterial.isotropicLinearShrinkageFraction * 100.0);
    if (resolved.channels.size() == 3U) {
        const std::array selections {
            resolved.channels[0],
            resolved.channels[1],
            resolved.channels[2],
        };
        auto recording
            = optics::holography::recordRgbReflectionVolumePlate(
                benchProject_.scene,
                fields,
                selections,
                recipe.volumeMaterial,
                recipe.sampling,
                *detectorFftBackend_,
                &realLensPrescriptionCatalog_,
                &slmResponseCatalog_,
                293.15);
        sandboxRgbVolumeRecording_ = std::make_unique<
            optics::holography::RgbVolumePlateRecordingResult>(
                std::move(recording));
        sandboxRecordedExperiment_
            = SandboxRecordedExperiment::RgbReflectionDenisyuk;
        sandboxActiveRecordingRecipeId_ = recipe.recipeId;
        sandboxRgbVolumeReplay_.reset();
        if (sandboxRgbReplayTexture_) {
            sandboxRgbReplayTexture_->destroy();
        }
        statusMessage_ = "Recomputed saved RGB reflection recording recipe "
            + recipe.recipeId;
        return;
    }
    const auto& channel = resolved.channels.front();
    auto recording = optics::holography::recordVolumePlate(
        benchProject_.scene,
        fields,
        channel.objectBranchId,
        channel.referenceBranchId,
        recipe.volumeMaterial,
        recipe.sampling,
        *detectorFftBackend_,
        {},
        &realLensPrescriptionCatalog_,
        &slmResponseCatalog_,
        293.15);
    sandboxVolumeReplayWavelengthNanometres_ = static_cast<float>(
        recording.pair.wavelengthMetres * 1e9);
    sandboxVolumeReplayAngleDegrees_ = static_cast<float>(
        recording.equivalentSymmetricBraggAngleInMediumRadians * 180.0
        / std::numbers::pi_v<double>);
    sandboxVolumeRecording_ = std::make_unique<
        optics::holography::VolumePlateRecordingResult>(
            std::move(recording));
    sandboxRecordedExperiment_
        = SandboxRecordedExperiment::ReflectionDenisyuk;
    sandboxActiveRecordingRecipeId_ = recipe.recipeId;
    sandboxVolumeReplay_.reset();
    sandboxVolumeObservationReplay_.reset();
    if (sandboxVolumeReplayTexture_) {
        sandboxVolumeReplayTexture_->destroy();
    }
    statusMessage_ = "Recomputed saved volume recording recipe "
        + recipe.recipeId;
}

void Application::recordSelectedPlateExperiment(bool recordHistory) {
    namespace holography = optics::holography;
    namespace bench = optics::scene;

    const auto* plate = benchProject_.scene.find(selectedBenchComponentId_);
    if (plate == nullptr
        || plate->kind != bench::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "select a holographic plate before recording");
    }
    const auto fields = holography::collectPlateIncidentFields(
        benchProject_.scene, benchTraceGraph_, plate->id);

    std::vector<const HologramRecordingRecipe*> matchingRecipes;
    for (const auto& recipe : benchProject_.recordingRecipes) {
        if (recipe.plateComponentId != plate->id) {
            continue;
        }
        const bool matches = sandboxExperimentMode_
                == SandboxExperimentMode::Auto
            || (sandboxExperimentMode_
                    == SandboxExperimentMode::ThinTransmission
                && recipe.model
                    == HologramRecordingModel::ThinTransmission
                && recipe.channels.size() == 1U)
            || (sandboxExperimentMode_
                    == SandboxExperimentMode::ReflectionDenisyuk
                && recipe.model == HologramRecordingModel::VolumeGrating
                && recipe.channels.size() == 1U)
            || (sandboxExperimentMode_
                    == SandboxExperimentMode::RgbFullColour
                && recipe.model
                    == HologramRecordingModel::ThinTransmission
                && recipe.channels.size() == 3U)
            || (sandboxExperimentMode_
                    == SandboxExperimentMode::RgbReflectionDenisyuk
                && recipe.model == HologramRecordingModel::VolumeGrating
                && recipe.channels.size() == 3U);
        if (matches) {
            matchingRecipes.push_back(&recipe);
        }
    }
    if (matchingRecipes.size() > 1U) {
        throw std::invalid_argument(
            "multiple saved recording recipes match this mode; choose the exact recipe in the Inspector");
    }
    if (matchingRecipes.size() == 1U) {
        const auto& recipe = *matchingRecipes.front();
        const auto resolved = resolveRecordingRecipe(fields, recipe);
        if (sandboxExperimentMode_
                == SandboxExperimentMode::ReflectionDenisyuk
            || sandboxExperimentMode_
                == SandboxExperimentMode::RgbReflectionDenisyuk) {
            const std::size_t requiredChannels = sandboxExperimentMode_
                    == SandboxExperimentMode::RgbReflectionDenisyuk
                ? 3U : 1U;
            if (resolved.channels.size() != requiredChannels) {
                throw std::invalid_argument(
                    "reflection recording recipe has the wrong channel count");
            }
            for (const auto& channel : resolved.channels) {
                const auto pair = holography::makePlateRecordingPair(
                    fields,
                    channel.objectBranchId,
                    channel.referenceBranchId);
                if (pair.geometry
                    != holography::PlateRecordingGeometry::Reflection) {
                    throw std::invalid_argument(
                        "the saved volume recipe is not an opposite-side reflection/Denisyuk geometry");
                }
            }
        }
        recomputeRecordingRecipe(fields, recipe);
        errorMessage_.clear();
        return;
    }

    if (sandboxPlateSampleSize_ < 2 || sandboxPlateSampleSize_ > 4096) {
        throw std::invalid_argument(
            "plate sample size must be in [2, 4096]");
    }
    if (!std::isfinite(sandboxPlateWindowMillimetres_)
        || sandboxPlateWindowMillimetres_ <= 0.0F) {
        throw std::invalid_argument(
            "plate analysis window must be positive");
    }
    if (!std::isfinite(
            sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
        || sandboxPlateRelativeReferenceKilowattsPerSquareMetre_ <= 0.0F) {
        throw std::invalid_argument(
            "relative intensity reference must be positive");
    }
    const double extentMetres
        = static_cast<double>(sandboxPlateWindowMillimetres_) * 1e-3;
    holography::ThinPlateRecordingOptions thinOptions;
    thinOptions.sampling = {
        .sampleWidth = static_cast<std::size_t>(sandboxPlateSampleSize_),
        .sampleHeight = static_cast<std::size_t>(sandboxPlateSampleSize_),
        .refractiveIndex = 1.0,
        .extentWidthMetres = extentMetres,
        .extentHeightMetres = extentMetres,
    };
    thinOptions.relativeIntensityReferenceWattsPerSquareMetre
        = static_cast<double>(
              sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
        * 1e3;

    struct PairCandidate final {
        holography::PlateBranchPairSelection selection;
        holography::PlateRecordingGeometry geometry;
    };
    std::vector<PairCandidate> candidates;
    for (const auto& object : fields.branches) {
        if (object.role != holography::RecordingBranchRole::Object) {
            continue;
        }
        for (const auto& reference : fields.branches) {
            if (reference.role
                    != holography::RecordingBranchRole::Reference
                || !bench::canInterfere(object.beam, reference.beam)) {
                continue;
            }
            const auto pair = holography::makePlateRecordingPair(
                fields,
                object.beam.provenance.branchId,
                reference.beam.provenance.branchId);
            candidates.push_back({
                .selection = {
                    .objectBranchId = pair.objectBranchId,
                    .referenceBranchId = pair.referenceBranchId,
                },
                .geometry = pair.geometry,
            });
        }
    }

    SandboxExperimentMode mode = sandboxExperimentMode_;
    std::array<holography::PlateBranchPairSelection, 3> rgbSelections {};
    bool rgbReady = false;
    try {
        rgbSelections = holography::selectRgbThinTransmissionPairs(fields);
        rgbReady = true;
    } catch (const std::exception&) {
        rgbReady = false;
    }
    std::array<holography::PlateBranchPairSelection, 3>
        rgbReflectionSelections {};
    bool rgbReflectionReady = false;
    try {
        rgbReflectionSelections
            = holography::selectRgbReflectionPairs(fields);
        rgbReflectionReady = true;
    } catch (const std::exception&) {
        rgbReflectionReady = false;
    }
    if (mode == SandboxExperimentMode::Auto) {
        if (rgbReady && !rgbReflectionReady) {
            mode = SandboxExperimentMode::RgbFullColour;
        } else if (rgbReflectionReady && !rgbReady) {
            mode = SandboxExperimentMode::RgbReflectionDenisyuk;
        } else if (candidates.size() == 1U) {
            mode = candidates.front().geometry
                    == holography::PlateRecordingGeometry::Transmission
                ? SandboxExperimentMode::ThinTransmission
                : SandboxExperimentMode::ReflectionDenisyuk;
        } else {
            throw std::invalid_argument(
                "auto recording requires one unambiguous pair or exactly one three-channel RGB geometry");
        }
    }

    HologramRecordingRecipe recipe;
    if (mode == SandboxExperimentMode::RgbFullColour) {
        if (!rgbReady) {
            throw std::invalid_argument(
                "RGB recording requires exactly three independent red, green, and blue transmission pairs");
        }
        recipe = makeThinRecordingRecipe(
            "rgb-" + plate->id, fields, rgbSelections, thinOptions);
    } else if (mode == SandboxExperimentMode::RgbReflectionDenisyuk) {
        if (!rgbReflectionReady) {
            throw std::invalid_argument(
                "RGB Denisyuk recording requires exactly three independent red, green, and blue opposite-side pairs");
        }
        const holography::VolumePlateMaterial material {
            .averageRefractiveIndex = static_cast<double>(
                sandboxVolumeAverageRefractiveIndex_),
            .refractiveIndexModulation = static_cast<double>(
                sandboxVolumeIndexModulation_),
            .isotropicLinearShrinkageFraction = static_cast<double>(
                sandboxVolumeShrinkagePercent_) * 0.01,
        };
        recipe = makeVolumeRecordingRecipe(
            "rgb-volume-" + plate->id,
            fields,
            rgbReflectionSelections,
            thinOptions.sampling,
            material);
    } else {
        const auto requiredGeometry
            = mode == SandboxExperimentMode::ThinTransmission
            ? holography::PlateRecordingGeometry::Transmission
            : holography::PlateRecordingGeometry::Reflection;
        std::vector<holography::PlateBranchPairSelection> matchingPairs;
        for (const auto& candidate : candidates) {
            if (candidate.geometry == requiredGeometry) {
                matchingPairs.push_back(candidate.selection);
            }
        }
        if (matchingPairs.size() != 1U) {
            throw std::invalid_argument(
                requiredGeometry
                        == holography::PlateRecordingGeometry::Transmission
                    ? "transmission recording requires exactly one compatible same-side pair"
                    : "reflection/Denisyuk recording requires exactly one compatible opposite-side pair");
        }
        if (mode == SandboxExperimentMode::ThinTransmission) {
            recipe = makeThinRecordingRecipe(
                "thin-" + plate->id,
                fields,
                matchingPairs,
                thinOptions);
        } else {
            const holography::VolumePlateMaterial material {
                .averageRefractiveIndex = static_cast<double>(
                    sandboxVolumeAverageRefractiveIndex_),
                .refractiveIndexModulation = static_cast<double>(
                    sandboxVolumeIndexModulation_),
                .isotropicLinearShrinkageFraction = static_cast<double>(
                    sandboxVolumeShrinkagePercent_) * 0.01,
            };
            recipe = makeVolumeRecordingRecipe(
                "volume-" + plate->id,
                fields,
                matchingPairs.front(),
                thinOptions.sampling,
                material);
        }
    }

    const std::string recipeId = recipe.recipeId;
    recomputeRecordingRecipe(fields, recipe);
    bool responseReferenceAutoScaled = false;
    if (recipe.model == HologramRecordingModel::ThinTransmission) {
        std::size_t minimumClampCount = 0U;
        std::size_t maximumClampCount = 0U;
        double maximumRelativeIntensity = 0.0;
        const auto collectResponseDiagnostics = [&minimumClampCount,
                                                    &maximumClampCount,
                                                    &maximumRelativeIntensity](
                                                    const auto& recording) {
            const auto& diagnostics = recording.hologram.diagnostics;
            minimumClampCount += diagnostics.minimumClampedSampleCount;
            maximumClampCount += diagnostics.maximumClampedSampleCount;
            maximumRelativeIntensity = std::max(
                maximumRelativeIntensity,
                diagnostics.maximumRecordedRelativeIntensity);
        };
        if (recipe.channels.size() == 1U && sandboxPlateRecording_) {
            collectResponseDiagnostics(*sandboxPlateRecording_);
        } else if (recipe.channels.size() == 3U && sandboxRgbRecording_) {
            for (const auto& channel : sandboxRgbRecording_->channels) {
                collectResponseDiagnostics(channel);
            }
        }
        if (minimumClampCount != 0U) {
            throw std::invalid_argument(
                "the thin-plate response clips at its minimum; adjust the response bounds in the Inspector");
        }
        if (maximumClampCount != 0U) {
            const double availableResponse
                = recipe.thinResponse.maximumAmplitudeTransmission
                - recipe.thinResponse.amplitudeBias;
            if (!std::isfinite(maximumRelativeIntensity)
                || maximumRelativeIntensity <= 0.0
                || !std::isfinite(availableResponse)
                || availableResponse <= 0.0
                || !std::isfinite(
                    recipe.thinResponse.intensityToAmplitudeGain)
                || recipe.thinResponse.intensityToAmplitudeGain <= 0.0) {
                throw std::invalid_argument(
                    "the thin-plate response clips and cannot be normalized with its current response parameters");
            }
            constexpr double kResponseHeadroom = 0.9;
            const double maximumUnclippedRelativeIntensity
                = kResponseHeadroom * availableResponse
                / recipe.thinResponse.intensityToAmplitudeGain;
            const double referenceScale = maximumRelativeIntensity
                / maximumUnclippedRelativeIntensity;
            if (!std::isfinite(referenceScale) || referenceScale <= 1.0) {
                throw std::runtime_error(
                    "thin-plate response normalization produced an invalid scale");
            }
            recipe.relativeIntensityReferenceWattsPerSquareMetre
                *= referenceScale;
            if (!std::isfinite(
                    recipe.relativeIntensityReferenceWattsPerSquareMetre)) {
                throw std::overflow_error(
                    "thin-plate relative intensity reference is not representable");
            }
            recomputeRecordingRecipe(fields, recipe);
            responseReferenceAutoScaled = true;
        }
    }
    upsertRecordingRecipe(benchProject_, std::move(recipe));
    if (recordHistory) {
        recordBenchEdit();
    }
    errorMessage_.clear();
    statusMessage_ = "Recorded bench experiment as " + recipeId;
    if (responseReferenceAutoScaled) {
        statusMessage_ += " (relative exposure reference auto-scaled to avoid response clipping)";
    }
}

void Application::reconstructSelectedPlateExperiment() {
    namespace holography = optics::holography;
    namespace bench = optics::scene;

    const auto* plate = benchProject_.scene.find(selectedBenchComponentId_);
    if (plate == nullptr
        || plate->kind != bench::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "select a holographic plate before reconstruction");
    }
    const auto* observation
        = benchProject_.scene.find(sandboxObservationComponentId_);
    if (observation == nullptr
        || (observation->kind
                != bench::BenchComponentKind::ScreenDetector
            && observation->kind
                != bench::BenchComponentKind::FieldProbe
            && observation->kind
                != bench::BenchComponentKind::HolographicPlate)) {
        throw std::invalid_argument(
            "select a placed Screen / Detector, Field Probe, or the recorded plate");
    }
    if (!detectorFftBackend_) {
        throw std::runtime_error("CPU FFT backend is unavailable");
    }
    const auto replayKind = sandboxPlateReplayKindIndex_ == 0
        ? holography::ThinPlateReplayKind::OrdinaryReference
        : holography::ThinPlateReplayKind::ConjugateReference;

    SandboxRecordedExperiment kind = sandboxRecordedExperiment_;
    if (sandboxExperimentMode_
        == SandboxExperimentMode::ThinTransmission) {
        kind = SandboxRecordedExperiment::ThinTransmission;
    } else if (sandboxExperimentMode_
        == SandboxExperimentMode::ReflectionDenisyuk) {
        kind = SandboxRecordedExperiment::ReflectionDenisyuk;
    } else if (sandboxExperimentMode_
        == SandboxExperimentMode::RgbFullColour) {
        kind = SandboxRecordedExperiment::RgbFullColour;
    } else if (sandboxExperimentMode_
        == SandboxExperimentMode::RgbReflectionDenisyuk) {
        kind = SandboxRecordedExperiment::RgbReflectionDenisyuk;
    }

    if (kind == SandboxRecordedExperiment::ThinTransmission) {
        if (!sandboxPlateRecording_
            || sandboxPlateRecording_->plateComponentId != plate->id
            || sandboxPlateRecording_->isStaleFor(benchProject_.scene)) {
            throw std::invalid_argument(
                "record a current thin transmission exposure first");
        }
        auto replay = holography::replayThinTransmissionToObservation(
            benchProject_.scene,
            *sandboxPlateRecording_,
            observation->id,
            replayKind,
            *detectorFftBackend_);
        field::FieldVisualizationOptions viewOptions;
        viewOptions.colormap = field::ColormapKind::Inferno;
        const auto image = field::renderLinearIntensity(
            replay.fullReplayAtObservation, viewOptions);
        if (!sandboxReplayTexture_
            || !sandboxReplayTexture_->uploadImage(image)) {
            throw std::runtime_error(
                "OpenGL rejected the thin reconstruction texture");
        }
        sandboxPlateReplay_ = std::make_unique<
            holography::ThinPlateReplayResult>(std::move(replay));
        sandboxPlateReplayViewIndex_ = 0;
        statusMessage_ = "Reconstructed thin hologram on " + observation->id;
    } else if (kind == SandboxRecordedExperiment::RgbFullColour) {
        if (!sandboxRgbRecording_
            || sandboxRgbRecording_->plateComponentId != plate->id
            || sandboxRgbRecording_->isStaleFor(benchProject_.scene)) {
            throw std::invalid_argument(
                "record a current RGB exposure set first");
        }
        auto replay = holography::replayRgbThinTransmissionToObservation(
            benchProject_.scene,
            *sandboxRgbRecording_,
            observation->id,
            replayKind,
            *detectorFftBackend_);
        const field::RgbIntensityVisualizationOptions displayOptions {
            .channelIntensityGains = {
                static_cast<double>(sandboxRgbDisplayGains_[0]),
                static_cast<double>(sandboxRgbDisplayGains_[1]),
                static_cast<double>(sandboxRgbDisplayGains_[2]),
            },
            .referenceIntensity = 0.0,
            .displayGamma = static_cast<double>(sandboxRgbDisplayGamma_),
        };
        const auto image = field::renderUncalibratedRgbIntensity(
            replay.channels[0].fullReplayAtObservation,
            replay.channels[1].fullReplayAtObservation,
            replay.channels[2].fullReplayAtObservation,
            displayOptions);
        if (!sandboxRgbReplayTexture_
            || !sandboxRgbReplayTexture_->uploadImage(image)) {
            throw std::runtime_error(
                "OpenGL rejected the RGB reconstruction texture");
        }
        sandboxRgbReplay_ = std::make_unique<
            holography::RgbThinPlateReplayResult>(std::move(replay));
        sandboxRgbReplayViewIndex_ = 0;
        statusMessage_ = "Reconstructed three independent RGB channels on "
            + observation->id;
    } else if (kind
        == SandboxRecordedExperiment::RgbReflectionDenisyuk) {
        if (!sandboxRgbVolumeRecording_
            || sandboxRgbVolumeRecording_->plateComponentId != plate->id
            || sandboxRgbVolumeRecording_->isStaleFor(
                benchProject_.scene)) {
            throw std::invalid_argument(
                "record a current RGB reflection/Denisyuk volume set first");
        }
        const auto fields = holography::collectPlateIncidentFields(
            benchProject_.scene, benchTraceGraph_, plate->id);
        holography::PlateFieldSamplingOptions sampling {
            .sampleWidth = static_cast<std::size_t>(sandboxPlateSampleSize_),
            .sampleHeight = static_cast<std::size_t>(sandboxPlateSampleSize_),
            .refractiveIndex = 1.0,
            .extentWidthMetres = static_cast<double>(
                sandboxPlateWindowMillimetres_) * 1e-3,
            .extentHeightMetres = static_cast<double>(
                sandboxPlateWindowMillimetres_) * 1e-3,
        };
        for (const auto& recipe : benchProject_.recordingRecipes) {
            if (recipe.recipeId == sandboxActiveRecordingRecipeId_
                && recipe.plateComponentId == plate->id) {
                sampling = recipe.sampling;
                break;
            }
        }
        auto replay
            = holography::replayRgbReflectionVolumeToObservation(
                benchProject_.scene,
                fields,
                *sandboxRgbVolumeRecording_,
                observation->id,
                sampling,
                *detectorFftBackend_,
                &realLensPrescriptionCatalog_,
                &slmResponseCatalog_,
                293.15);
        const field::RgbIntensityVisualizationOptions displayOptions {
            .channelIntensityGains = {
                static_cast<double>(sandboxRgbDisplayGains_[0]),
                static_cast<double>(sandboxRgbDisplayGains_[1]),
                static_cast<double>(sandboxRgbDisplayGains_[2]),
            },
            .referenceIntensity = 0.0,
            .displayGamma = static_cast<double>(sandboxRgbDisplayGamma_),
        };
        const auto image = field::renderUncalibratedRgbIntensity(
            replay.channels[0].reconstructedAtObservation,
            replay.channels[1].reconstructedAtObservation,
            replay.channels[2].reconstructedAtObservation,
            displayOptions);
        if (!sandboxRgbReplayTexture_
            || !sandboxRgbReplayTexture_->uploadImage(image)) {
            throw std::runtime_error(
                "OpenGL rejected the RGB Denisyuk reconstruction texture");
        }
        sandboxRgbVolumeReplay_ = std::make_unique<
            holography::RgbVolumePlateReplayResult>(std::move(replay));
        sandboxRgbReplayViewIndex_ = 0;
        statusMessage_ = "Reconstructed three independent RGB reflection channels on "
            + observation->id;
    } else if (kind == SandboxRecordedExperiment::ReflectionDenisyuk) {
        if (!sandboxVolumeRecording_
            || sandboxVolumeRecording_->plateComponentId != plate->id
            || sandboxVolumeRecording_->isStaleFor(benchProject_.scene)) {
            throw std::invalid_argument(
                "record a current reflection/Denisyuk volume grating first");
        }
        const auto fields = holography::collectPlateIncidentFields(
            benchProject_.scene, benchTraceGraph_, plate->id);
        holography::PlateFieldSamplingOptions sampling {
            .sampleWidth = static_cast<std::size_t>(sandboxPlateSampleSize_),
            .sampleHeight = static_cast<std::size_t>(sandboxPlateSampleSize_),
            .refractiveIndex = 1.0,
            .extentWidthMetres = static_cast<double>(
                sandboxPlateWindowMillimetres_) * 1e-3,
            .extentHeightMetres = static_cast<double>(
                sandboxPlateWindowMillimetres_) * 1e-3,
        };
        for (const auto& recipe : benchProject_.recordingRecipes) {
            if (recipe.recipeId == sandboxActiveRecordingRecipeId_
                && recipe.plateComponentId == plate->id) {
                sampling = recipe.sampling;
                break;
            }
        }
        auto replay = holography::replayVolumeReflectionToObservation(
            benchProject_.scene,
            fields,
            *sandboxVolumeRecording_,
            sandboxVolumeRecording_->pair.referenceBranchId,
            observation->id,
            sampling,
            *detectorFftBackend_,
            &realLensPrescriptionCatalog_,
            &slmResponseCatalog_,
            293.15);
        field::FieldVisualizationOptions viewOptions;
        viewOptions.colormap = field::ColormapKind::Inferno;
        const auto image = field::renderLinearIntensity(
            replay.reconstructedAtObservation, viewOptions);
        if (!sandboxVolumeReplayTexture_
            || !sandboxVolumeReplayTexture_->uploadImage(image)) {
            throw std::runtime_error(
                "OpenGL rejected the reflection reconstruction texture");
        }
        sandboxVolumeObservationReplay_ = std::make_unique<
            holography::VolumePlateObservationReplayResult>(
                std::move(replay));
        statusMessage_ = "Reconstructed reflection/Denisyuk hologram on "
            + observation->id;
    } else {
        throw std::invalid_argument(
            "record the selected plate before reconstruction");
    }
    errorMessage_.clear();
}

LessonEditState Application::captureLessonEditState() const {
    return {
        .reflectionRefractionConfig = reflectionRefractionConfig_,
        .reflectionProjectProvenance = reflectionProjectProvenance_,
        .reflectionProjectName = reflectionProjectName_,
        .scene = scene_,
        .sceneProvenance = sceneProjectProvenance_,
        .tracerOptions = tracerOptions_,
        .waveDetectorDraft = detectorUiState_.draftConfig(),
        .samplingDebugger = samplingDebuggerConfig_,
        .waveProjectProvenance = waveProjectProvenance_,
        .waveProjectName = waveProjectName_,
        .slmInterferenceDraft = slmInterferenceUiState_.draftConfig(),
        .slmCalibrationSource = slmInterferenceUiState_.draftCalibrationSource(),
        .slmProjectProvenance = slmProjectProvenance_,
        .slmProjectName = slmProjectName_,
    };
}

void Application::recordLessonEdit() {
    if (lessonEditHistoryReady_ && !restoringLessonEdit_) {
        static_cast<void>(lessonEditHistory_.record(captureLessonEditState()));
    }
}

bool Application::restoreLessonEditState(const LessonEditState& state) {
    restoringLessonEdit_ = true;
    try {
        const auto restoredReflectionResult
            = reflection::evaluateReflectionRefraction(
                state.reflectionRefractionConfig);
        slmexperiment::validateSlmInterferenceExperimentConfig(
            state.slmInterferenceDraft);
        project::validateProjectProvenance(
            state.reflectionProjectProvenance);
        project::validateProjectProvenance(state.sceneProvenance);
        project::validateProjectProvenance(state.waveProjectProvenance);
        project::validateProjectProvenance(state.slmProjectProvenance);
        if (!applySceneProject(
                state.scene, state.tracerOptions, state.sceneProvenance)) {
            throw std::runtime_error(
                errorMessage_.empty() ? "scene restoration failed" : errorMessage_);
        }
        detectorUiState_.setDraftConfig(state.waveDetectorDraft);
        samplingDebuggerConfig_ = state.samplingDebugger;
        waveProjectProvenance_ = state.waveProjectProvenance;
        waveProjectName_ = state.waveProjectName;
        slmInterferenceUiState_.replaceDraftProject(
            state.slmInterferenceDraft, state.slmCalibrationSource);
        slmProjectProvenance_ = state.slmProjectProvenance;
        slmProjectName_ = state.slmProjectName;
        reflectionRefractionConfig_ = state.reflectionRefractionConfig;
        reflectionRefractionResult_ = restoredReflectionResult;
        reflectionProjectProvenance_ = state.reflectionProjectProvenance;
        reflectionProjectName_ = state.reflectionProjectName;
        if (learnSession_.activeLessonId() == "reflection_refraction") {
            learnSession_.replaceReflectionConfig(
                state.reflectionRefractionConfig);
        }
        restoringLessonEdit_ = false;

        detectorStatusMessage_
            = "Edit history restored the detector draft; Apply if it differs from the result";
        samplingDebuggerStatusMessage_
            = "Edit history restored debugger inputs; Refresh if they differ from the result";
        slmInterferenceStatusMessage_
            = "Edit history restored the SLM draft; Apply if it differs from the result";
        reflectionStatusMessage_
            = "Edit history restored the reflection/refraction workbench";
        statusMessage_ = "Restored lesson-relevant editable inputs";
        return true;
    } catch (const std::exception& ex) {
        restoringLessonEdit_ = false;
        errorMessage_ = "Edit history restore failed: " + std::string(ex.what());
        statusMessage_.clear();
        return false;
    }
}

bool Application::applyReflectionRefractionConfig(
    const reflection::ReflectionRefractionConfig& config) {
    try {
        const auto result = reflection::evaluateReflectionRefraction(config);
        if (learnSession_.activeLessonId() == "reflection_refraction") {
            learnSession_.setReflectionConfig(config);
        }
        reflectionRefractionConfig_ = config;
        reflectionRefractionResult_ = result;
        reflectionErrorMessage_.clear();
        reflectionStatusMessage_ = "Reflection/refraction result updated";
        recordLessonEdit();
        return true;
    } catch (const std::exception& ex) {
        reflectionErrorMessage_ = ex.what();
        reflectionStatusMessage_.clear();
        return false;
    }
}

bool Application::applySceneProject(
    const optics::scene::OpticalBenchScene& candidateScene,
    const optics::ray::BenchTracerOptions& candidateOptions,
    const project::ProjectProvenance& provenance) {
    try {
        project::validateProjectProvenance(provenance);
    } catch (const std::exception& ex) {
        errorMessage_ = ex.what();
        statusMessage_.clear();
        return false;
    }
    const auto previousProvenance = sceneProjectProvenance_;
    sceneProjectProvenance_ = provenance;
    if (applyScene(candidateScene, candidateOptions)) {
        return true;
    }
    sceneProjectProvenance_ = previousProvenance;
    return false;
}

void Application::undoLessonEdit() {
    if (!lessonEditHistory_.canUndo()) {
        return;
    }
    const LessonEditState state = lessonEditHistory_.undo();
    if (!restoreLessonEditState(state)) {
        static_cast<void>(lessonEditHistory_.redo());
    }
}

void Application::redoLessonEdit() {
    if (!lessonEditHistory_.canRedo()) {
        return;
    }
    const LessonEditState state = lessonEditHistory_.redo();
    if (!restoreLessonEditState(state)) {
        static_cast<void>(lessonEditHistory_.undo());
    }
}

void Application::saveSceneToPath(const char* pathStr) {
    if (pathStr == nullptr || pathStr[0] == '\0') {
        errorMessage_ = "Save failed: file path cannot be empty";
        statusMessage_.clear();
        return;
    }
    std::string pathString(pathStr);
    const auto first = pathString.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        errorMessage_ = "Save failed: file path cannot be empty";
        statusMessage_.clear();
        return;
    }
    const auto last = pathString.find_last_not_of(" \t\n\r");
    const std::filesystem::path path(pathString.substr(first, last - first + 1));

    try {
        optics::scene::saveSceneProject({
            .scene = scene_,
            .provenance = sceneProjectProvenance_,
        }, path);
        statusMessage_ = "Saved scene to " + path.string();
        errorMessage_.clear();
    } catch (const std::exception& ex) {
        errorMessage_ = "Save failed: " + std::string(ex.what());
        statusMessage_.clear();
    }
}

void Application::loadSceneFromPath(const char* pathStr) {
    if (pathStr == nullptr || pathStr[0] == '\0') {
        errorMessage_ = "Load failed: file path cannot be empty";
        statusMessage_.clear();
        return;
    }
    std::string pathString(pathStr);
    const auto first = pathString.find_first_not_of(" \t\n\r");
    if (first == std::string::npos) {
        errorMessage_ = "Load failed: file path cannot be empty";
        statusMessage_.clear();
        return;
    }
    const auto last = pathString.find_last_not_of(" \t\n\r");
    const std::filesystem::path path(pathString.substr(first, last - first + 1));

    try {
        const auto loaded = optics::scene::loadSceneProject(path);
        if (applySceneProject(
                loaded.scene, tracerOptions_, loaded.provenance)) {
            statusMessage_ = "Loaded scene from " + path.string() + " (" + std::to_string(raySegments_.size()) + " segments)";
        }
    } catch (const std::exception& ex) {
        errorMessage_ = "Load failed: " + std::string(ex.what());
        statusMessage_.clear();
    }
}

void Application::loadLessonProgress() {
    try {
        const std::filesystem::path path(lessonProgressPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("lesson progress path cannot be empty");
        }
        auto progress = lessons::loadLessonProgress(
            path, learnSession_.catalog());
        learnSession_.replaceProgress(std::move(progress));
        lessonErrorMessage_.clear();
        lessonStatusMessage_ = "Loaded lesson progress from " + path.string();
    } catch (const std::exception& ex) {
        lessonErrorMessage_ = "Progress load failed: " + std::string(ex.what());
        lessonStatusMessage_.clear();
    }
}

void Application::saveLessonProgress() {
    try {
        const std::filesystem::path path(lessonProgressPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("lesson progress path cannot be empty");
        }
        lessons::saveLessonProgress(
            path, learnSession_.catalog(), learnSession_.progress());
        lessonErrorMessage_.clear();
        lessonStatusMessage_ = "Saved lesson progress to " + path.string();
    } catch (const std::exception& ex) {
        lessonErrorMessage_ = "Progress save failed: " + std::string(ex.what());
        lessonStatusMessage_.clear();
    }
}

bool Application::initialize(const RunOptions& options) {
    if (initialized_) {
        return true;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return false;
    }
    sdlInitialized_ = true;

    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MAJOR_VERSION, 4);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_MINOR_VERSION, 6);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_PROFILE_MASK, SDL_GL_CONTEXT_PROFILE_CORE);
    SDL_GL_SetAttribute(SDL_GL_CONTEXT_FLAGS, SDL_GL_CONTEXT_DEBUG_FLAG);
    SDL_GL_SetAttribute(SDL_GL_DOUBLEBUFFER, 1);
    SDL_GL_SetAttribute(SDL_GL_DEPTH_SIZE, 24);

    glSmokeMode_ = options.glSmoke;
    localizedSmokeTextSubmitted_ = false;
    auto windowFlags = SDL_WINDOW_OPENGL | SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY;
    if (glSmokeMode_) {
        windowFlags |= SDL_WINDOW_HIDDEN;
    }
    isBenchmark_ = options.benchmarkFrames > 0;
    const int requestedWidth = isBenchmark_ ? 1920 : 1440;
    const int requestedHeight = isBenchmark_ ? 1080 : 900;
    window_ = SDL_CreateWindow("HoloBench — Optical Engineering Workbench", requestedWidth, requestedHeight, windowFlags);
    if (window_ == nullptr) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        shutdown();
        return false;
    }

    glContext_ = SDL_GL_CreateContext(window_);
    if (glContext_ == nullptr || !SDL_GL_MakeCurrent(window_, glContext_)) {
        SDL_Log("OpenGL context creation failed: %s", SDL_GetError());
        shutdown();
        return false;
    }

    if (isBenchmark_) {
        SDL_GL_SetSwapInterval(0);
    } else {
        if (!SDL_GL_SetSwapInterval(1)) {
            SDL_GL_SetSwapInterval(-1);
        }
    }
    int interval = isBenchmark_ ? 0 : 1;
    if (SDL_GL_GetSwapInterval(&interval)) {
        vsyncInterval_ = interval;
    } else {
        vsyncInterval_ = isBenchmark_ ? 0 : 1;
    }

    const int loadedVersion = gladLoadGL(loadOpenGlProcedure);
    if (loadedVersion == 0
        || GLAD_VERSION_MAJOR(loadedVersion) < 4
        || (GLAD_VERSION_MAJOR(loadedVersion) == 4 && GLAD_VERSION_MINOR(loadedVersion) < 6)) {
        SDL_Log("Failed to load the required OpenGL 4.6 Core API");
        shutdown();
        return false;
    }

    render::gl::installDebugCallback();
    constexpr std::array<GLenum, 4> glProperties {GL_VENDOR, GL_RENDERER, GL_VERSION, GL_SHADING_LANGUAGE_VERSION};
    constexpr std::array<const char*, 4> glPropertyNames {"vendor", "renderer", "version", "GLSL"};
    for (std::size_t index = 0; index < glProperties.size(); ++index) {
        const auto* value = reinterpret_cast<const char*>(glGetString(glProperties[index]));
        SDL_Log("OpenGL %s: %s", glPropertyNames[index], value != nullptr ? value : "unavailable");
    }

    if (!renderer_ || !renderer_->initialize()) {
        SDL_Log("OpticalBenchRenderer initialization failed");
        shutdown();
        return false;
    }

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    imguiContextCreated_ = true;
    ImGui::GetIO().ConfigFlags |= ImGuiConfigFlags_DockingEnable;
    if (glSmokeMode_) {
        // Automated UI input needs the deterministic first-run layout and must
        // neither consume nor rewrite a developer's local docking preferences.
        ImGui::GetIO().IniFilename = nullptr;
    }
    ImGui::StyleColorsDark();

    try {
        uiFont_ = std::make_unique<UiFontAsset>(
            packagedFontPath(), lessonLocalization_);
        uiFont_->install(ImGui::GetIO());
    } catch (const std::exception& ex) {
        SDL_Log("Packaged UI font initialization failed: %s", ex.what());
        shutdown();
        return false;
    }

    imguiSdlInitialized_ = ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    imguiGlInitialized_ = imguiSdlInitialized_ && ImGui_ImplOpenGL3_Init("#version 460 core");
    if (!imguiGlInitialized_) {
        SDL_Log("Dear ImGui backend initialization failed");
        shutdown();
        return false;
    }

    try {
        static_cast<void>(lessons::loadReflectionRefractionLessonTemplate(
            lessonTemplateRoot(), "lesson_reflection_refraction"));
        static_cast<void>(lessons::loadOpticalBenchLessonTemplate(
            lessonTemplateRoot(), "lesson_thin_lens"));
        static_cast<void>(lessons::loadOpticalBenchLessonTemplate(
            lessonTemplateRoot(), "lesson_real_virtual_images"));
        static_cast<void>(lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_diffraction"));
        static_cast<void>(lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_fourier_plane"));
        static_cast<void>(lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_spatial_filtering"));
        static_cast<void>(lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_na_psf"));
        static_cast<void>(lessons::loadSlmLessonTemplate(
            lessonTemplateRoot(), "lesson_coherence_interference"));
        static_cast<void>(lessons::loadHolographyLessonTemplate(
            lessonTemplateRoot(), "lesson_holography"));
        static_cast<void>(lessons::loadHolographyLessonTemplate(
            lessonTemplateRoot(), "lesson_h1_h2_advanced"));
    } catch (const std::exception& ex) {
        SDL_Log("Packaged lesson template validation failed: %s", ex.what());
        shutdown();
        return false;
    }

    auto initialOptions = tracerOptions_;
    initialOptions.rayCount = static_cast<std::size_t>(options.initialRayCount > 0 ? options.initialRayCount : 64);
    if (!applyScene(scene_, initialOptions)) {
        SDL_Log("Initial scene setup failed: %s", errorMessage_.c_str());
        shutdown();
        return false;
    }
    if (options.chimeraBenchmark) {
        auto compiled = chimera::compileChimeraRecipe(
            chimera::makeCanonicalChimeraRecipe());
        benchProject_ = std::move(compiled.project);
        selectedBenchComponentId_ = "chimera-plate";
    }
    if (!showSandboxViewport()) {
        SDL_Log("Initial dynamic sandbox setup failed: %s", errorMessage_.c_str());
        shutdown();
        return false;
    }
    benchEditHistory_.reset(benchProject_);
    benchEditHistoryReady_ = true;
    refreshRealLensWorkbench();
    lessonEditHistory_.reset(captureLessonEditState());
    lessonEditHistoryReady_ = true;

    initialized_ = true;
    return true;
}

void Application::shutdown() noexcept {
    if (glContext_ != nullptr && window_ != nullptr) {
        SDL_GL_MakeCurrent(window_, glContext_);
    }
    if (renderer_) {
        renderer_->destroy();
    }
    if (detectorTexture_) {
        detectorTexture_->destroy();
    }
    if (samplingSpectrumTexture_) {
        samplingSpectrumTexture_->destroy();
    }
    if (fourFObjectTexture_) {
        fourFObjectTexture_->destroy();
    }
    if (fourFBeforeFilterTexture_) {
        fourFBeforeFilterTexture_->destroy();
    }
    if (fourFAfterFilterTexture_) {
        fourFAfterFilterTexture_->destroy();
    }
    if (fourFImageTexture_) {
        fourFImageTexture_->destroy();
    }
    if (slmInterferenceTexture_) {
        slmInterferenceTexture_->destroy();
    }
    if (holographyTexture_) {
        holographyTexture_->destroy();
    }
    if (sandboxPlateTexture_) {
        sandboxPlateTexture_->destroy();
    }
    if (sandboxReplayTexture_) {
        sandboxReplayTexture_->destroy();
    }
    if (sandboxVolumeReplayTexture_) {
        sandboxVolumeReplayTexture_->destroy();
    }
    if (sandboxRgbReplayTexture_) {
        sandboxRgbReplayTexture_->destroy();
    }
    if (sandboxWaveTexture_) {
        sandboxWaveTexture_->destroy();
    }
    if (chimeraCameraTexture_) {
        chimeraCameraTexture_->destroy();
    }
    detectorResult_.reset();
    samplingDebuggerResult_.reset();
    realLensResult_.reset();
    slmInterferenceResult_.reset();
    holographyResult_.reset();
    sandboxPlateRecording_.reset();
    sandboxPlateReplay_.reset();
    sandboxVolumeRecording_.reset();
    sandboxVolumeReplay_.reset();
    sandboxVolumeObservationReplay_.reset();
    sandboxRgbRecording_.reset();
    sandboxRgbReplay_.reset();
    sandboxRgbVolumeRecording_.reset();
    sandboxRgbVolumeReplay_.reset();
    sandboxWaveObservations_.clear();
    chimeraWorkflow_.reset();
    chimeraBatch_.reset();
    chimeraSweepResult_.reset();
    if (imguiGlInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        imguiGlInitialized_ = false;
    }
    if (imguiSdlInitialized_) {
        ImGui_ImplSDL3_Shutdown();
        imguiSdlInitialized_ = false;
    }
    if (imguiContextCreated_) {
        uiFont_.reset();
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }
    initialized_ = false;
    dockLayoutInitialized_ = false;
    isOrbiting_ = false;
    isPanning_ = false;
    sandboxGizmoDragging_ = false;
    sandboxGizmoChanged_ = false;
    sandboxMechanicalDragging_ = false;
    sandboxMechanicalChanged_ = false;
    sandboxMechanicalControl_ = SandboxMechanicalControl::None;
    benchEditHistoryReady_ = false;
    isGizmoDragging_ = false;
    draggedTarget_ = GizmoTarget::None;
    selectedTarget_ = GizmoTarget::None;
    hasDetectorProbe_ = false;
    detectorProbeLocked_ = false;
    if (glContext_ != nullptr) {
        SDL_GL_DestroyContext(glContext_);
        glContext_ = nullptr;
    }
    if (window_ != nullptr) {
        SDL_DestroyWindow(window_);
        window_ = nullptr;
    }
    if (sdlInitialized_) {
        SDL_Quit();
        sdlInitialized_ = false;
    }
}

BenchWaveObservationResult*
Application::activeSandboxWaveObservation() noexcept {
    if (sandboxWaveChannelIndex_ < 0
        || static_cast<std::size_t>(sandboxWaveChannelIndex_)
            >= sandboxWaveObservations_.size()) {
        return nullptr;
    }
    return &sandboxWaveObservations_[
        static_cast<std::size_t>(sandboxWaveChannelIndex_)];
}

const BenchWaveObservationResult*
Application::activeSandboxWaveObservation() const noexcept {
    if (sandboxWaveChannelIndex_ < 0
        || static_cast<std::size_t>(sandboxWaveChannelIndex_)
            >= sandboxWaveObservations_.size()) {
        return nullptr;
    }
    return &sandboxWaveObservations_[
        static_cast<std::size_t>(sandboxWaveChannelIndex_)];
}

void Application::updateSandboxWaveObservation() {
    namespace bench = optics::scene;
    const auto* selected = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (selected != nullptr
        && (selected->kind == bench::BenchComponentKind::ScreenDetector
            || selected->kind == bench::BenchComponentKind::FieldProbe)) {
        sandboxWaveObservationComponentId_ = selected->id;
    }
    const auto* observation = benchProject_.scene.find(
        sandboxWaveObservationComponentId_);
    if (!sandboxLiveWavePlane_ || observation == nullptr
        || (observation->kind != bench::BenchComponentKind::ScreenDetector
            && observation->kind != bench::BenchComponentKind::FieldProbe)) {
        sandboxWaveObservations_.clear();
        sandboxWaveChannelIndex_ = 0;
        sandboxWaveObservationDiagnostic_.clear();
        sandboxWaveTextureDirty_ = true;
        if (sandboxWaveTexture_) {
            sandboxWaveTexture_->destroy();
        }
        return;
    }

    const auto* activeObservation = activeSandboxWaveObservation();
    const bool current = activeObservation
        && !activeObservation->isStaleFor(benchProject_.scene)
        && activeObservation->observationComponentId
            == observation->id;
    const bool needsFullResolution = current
        && activeObservation->interactivePreview
        && !sandboxGizmoDragging_;

    try {
        const auto renderObservation = [this](
                                           const BenchWaveObservationResult& result) {
            field::FieldVisualizationOptions options;
            field::FieldViewMode viewMode = field::FieldViewMode::Intensity;
            if (sandboxWaveViewModeIndex_ == 1) {
                viewMode = field::FieldViewMode::DecibelIntensity;
                options.colormap = field::ColormapKind::Inferno;
                options.floorDecibels = sandboxWaveDecibelFloor_;
                options.maxDecibels = 0.0;
                options.decibelReferenceIntensity
                    = result.peakIntensityWattsPerSquareMetre > 0.0
                    ? result.peakIntensityWattsPerSquareMetre
                    : 1.0;
            } else if (sandboxWaveViewModeIndex_ == 2) {
                viewMode = field::FieldViewMode::WrappedPhase;
                options.colormap = field::ColormapKind::CyclicPhase;
                options.phaseMinimumIntensity
                    = result.peakIntensityWattsPerSquareMetre
                    * static_cast<double>(
                        sandboxWavePhaseMinimumPeakFraction_);
            } else {
                options.colormap = field::ColormapKind::Grayscale;
            }
            const auto image = field::renderFieldView(
                result.fieldAtObservation, viewMode, options);
            if (!sandboxWaveTexture_
                || !sandboxWaveTexture_->uploadImage(image)) {
                throw std::runtime_error(
                    "OpenGL rejected the live wave-screen texture");
            }
        };
        if (current && !needsFullResolution) {
            if (sandboxWaveTextureDirty_) {
                renderObservation(*activeObservation);
                sandboxWaveTextureDirty_ = false;
            }
            return;
        }
        if (!detectorFftBackend_) {
            throw std::runtime_error("CPU FFT backend is unavailable");
        }
        const bool preview = sandboxGizmoDragging_;
        const auto* previousChannel = activeSandboxWaveObservation();
        const double previousWavelength = previousChannel
            ? previousChannel->fieldAtObservation.vacuumWavelengthMetres()
            : 0.0;
        const std::string previousCoherence = previousChannel
            ? previousChannel->coherenceId
            : std::string {};
        auto results = observeBenchWaveChannels(
            benchProject_.scene,
            benchTraceGraph_,
            observation->id,
            preview
                ? waveSampleLimit(
                    kWavePreviewSampleLimits,
                    sandboxWavePreviewSampleLimitIndex_)
                : waveSampleLimit(
                    kWaveCommittedSampleLimits,
                    sandboxWaveCommittedSampleLimitIndex_),
            preview,
            *detectorFftBackend_,
            &realLensPrescriptionCatalog_,
            &slmResponseCatalog_,
            293.15);
        const auto previous = std::find_if(
            results.begin(), results.end(),
            [&](const BenchWaveObservationResult& candidate) {
                return candidate.fieldAtObservation.vacuumWavelengthMetres()
                        == previousWavelength
                    && candidate.coherenceId == previousCoherence;
            });
        sandboxWaveChannelIndex_ = previous == results.end()
            ? 0
            : static_cast<int>(std::distance(results.begin(), previous));
        sandboxWaveObservations_ = std::move(results);
        activeObservation = activeSandboxWaveObservation();
        if (activeObservation == nullptr) {
            throw std::logic_error(
                "live wave observation produced no selectable channel");
        }
        renderObservation(*activeObservation);
        sandboxWaveTextureDirty_ = false;
        sandboxWaveObservationDiagnostic_.clear();
    } catch (const std::exception& error) {
        sandboxWaveObservations_.clear();
        sandboxWaveChannelIndex_ = 0;
        sandboxWaveObservationDiagnostic_ = error.what();
        sandboxWaveTextureDirty_ = true;
        if (sandboxWaveTexture_) {
            sandboxWaveTexture_->destroy();
        }
    }
}

void Application::updateWaveDetector() {
    if (detectorUiState_.consumePropagationRequest()) {
        try {
            if (!detectorFftBackend_) {
                throw std::runtime_error("CPU FFT backend is unavailable");
            }
            auto result = wave::simulateDetectorField(
                detectorUiState_.appliedConfig(),
                *detectorFftBackend_);
            detectorResult_ = std::make_unique<wave::WaveDetectorResult>(std::move(result));
            detectorUiState_.propagationSucceeded();
            detectorErrorMessage_.clear();
            detectorStatusMessage_ = "Detector field recomputed on the deterministic CPU reference backend";
            hasDetectorProbe_ = false;
            detectorProbeLocked_ = false;
            samplingDebuggerConfig_.probeXIndex = detectorResult_->field.width() / 2U;
            samplingDebuggerConfig_.probeYIndex = detectorResult_->field.height() / 2U;
            refreshSamplingDebugger();
        } catch (const std::exception& ex) {
            detectorErrorMessage_ = "Propagation failed: " + std::string(ex.what());
            detectorStatusMessage_.clear();
            detectorResult_.reset();
            if (detectorTexture_) {
                detectorTexture_->destroy();
            }
            samplingDebuggerResult_.reset();
            if (samplingSpectrumTexture_) {
                samplingSpectrumTexture_->destroy();
            }
            if (fourFObjectTexture_) {
                fourFObjectTexture_->destroy();
            }
            if (fourFBeforeFilterTexture_) {
                fourFBeforeFilterTexture_->destroy();
            }
            if (fourFAfterFilterTexture_) {
                fourFAfterFilterTexture_->destroy();
            }
            if (fourFImageTexture_) {
                fourFImageTexture_->destroy();
            }
            hasDetectorProbe_ = false;
            detectorProbeLocked_ = false;
        }
    }

    if (detectorResult_ && detectorUiState_.consumeVisualizationRequest()) {
        try {
            field::FieldVisualizationOptions options;
            if (detectorResult_->peakIntensity > 0.0) {
                options.phaseMinimumIntensity = detectorResult_->peakIntensity * 1.0e-12;
            }
            auto image = field::renderFieldView(
                detectorResult_->field,
                detectorUiState_.viewMode(),
                options);
            if (!detectorTexture_ || !detectorTexture_->uploadImage(image)) {
                throw std::runtime_error("OpenGL rejected the detector texture upload");
            }
            detectorErrorMessage_.clear();
        } catch (const std::exception& ex) {
            detectorErrorMessage_ = "Visualization failed: " + std::string(ex.what());
            detectorStatusMessage_.clear();
        }
    }
}

void Application::updateSlmInterference() {
    if (slmInterferenceUiState_.consumeSimulationRequest()) {
        try {
            if (!detectorFftBackend_) {
                throw std::runtime_error("CPU FFT backend is unavailable");
            }
            auto result = slmexperiment::runSlmInterferenceExperiment(
                slmInterferenceUiState_.appliedConfig(),
                *detectorFftBackend_);
            slmInterferenceResult_
                = std::make_unique<slmexperiment::SlmInterferenceExperimentResult>(
                    std::move(result));
            slmInterferenceUiState_.simulationSucceeded();
            slmInterferenceErrorMessage_.clear();
            slmInterferenceStatusMessage_
                = "SLM experiment recomputed on the deterministic CPU reference backend";
        } catch (const std::exception& ex) {
            slmInterferenceErrorMessage_ = "SLM experiment failed: " + std::string(ex.what());
            slmInterferenceStatusMessage_.clear();
            slmInterferenceResult_.reset();
            if (slmInterferenceTexture_) {
                slmInterferenceTexture_->destroy();
            }
        }
    }

    if (slmInterferenceResult_
        && slmInterferenceUiState_.consumeVisualizationRequest()) {
        try {
            if (slmInterferenceResult_->wavelengths.empty()) {
                throw std::runtime_error("SLM experiment returned no wavelengths");
            }
            const std::size_t wavelengthIndex = std::min(
                slmInterferenceUiState_.displayedWavelengthIndex(),
                slmInterferenceResult_->wavelengths.size() - 1U);
            const auto& result = slmInterferenceResult_->wavelengths[wavelengthIndex];
            field::FieldVisualizationOptions options;
            options.colormap = field::ColormapKind::Inferno;
            field::RgbaImage image = [&]() {
                switch (slmInterferenceUiState_.displayPlane()) {
                case slmui::DisplayPlane::Interference:
                    return field::renderLinearIntensity(result.interference.intensity, options);
                case slmui::DisplayPlane::AngularIntensity:
                    return field::renderLinearIntensity(result.normalizedAngularIntensity, options);
                case slmui::DisplayPlane::SelectedPixelPsf:
                    return field::renderLinearIntensity(result.normalizedAngularPsf, options);
                }
                throw std::invalid_argument("unsupported SLM display plane");
            }();
            if (!slmInterferenceTexture_ || !slmInterferenceTexture_->uploadImage(image)) {
                throw std::runtime_error("OpenGL rejected the SLM experiment texture upload");
            }
            slmInterferenceErrorMessage_.clear();
        } catch (const std::exception& ex) {
            slmInterferenceErrorMessage_ = "SLM visualization failed: " + std::string(ex.what());
            slmInterferenceStatusMessage_.clear();
        }
    }
}

void Application::updateHolography() {
    if (holographyUiState_.consumeSimulationRequest()) {
        try {
            if (!detectorFftBackend_) {
                throw std::runtime_error("CPU FFT backend is unavailable");
            }
            auto result = holographylab::runHolographyLab(
                holographyUiState_.appliedConfig(), *detectorFftBackend_);
            holographyResult_
                = std::make_unique<holographylab::HolographyLabResult>(
                    std::move(result));
            holographyUiState_.simulationSucceeded();
            holographyErrorMessage_.clear();
            holographyStatusMessage_
                = "RGB H1/H2 holography recomputed on the deterministic CPU reference backend";
        } catch (const std::exception& ex) {
            holographyErrorMessage_ = "Holography simulation failed: "
                + std::string(ex.what());
            holographyStatusMessage_.clear();
            holographyResult_.reset();
            if (holographyTexture_) {
                holographyTexture_->destroy();
            }
        }
    }

    if (holographyResult_ && holographyUiState_.consumeVisualizationRequest()) {
        try {
            const std::size_t channelIndex = std::min(
                holographyUiState_.displayedChannel(), std::size_t {2});
            const auto& channel
                = holographyResult_->rgbTransfer.channels[channelIndex];
            field::FieldVisualizationOptions options;
            options.colormap = field::ColormapKind::Inferno;
            field::RgbaImage image = [&]() {
                switch (holographyUiState_.displayPlane()) {
                case holographyui::DisplayPlane::H1Exposure:
                    return field::renderLinearIntensity(
                        channel.h1.hologram.recordedRelativeIntensity, options);
                case holographyui::DisplayPlane::H1RealImage:
                    return field::renderFieldView(
                        channel.h1.isolatedRealImageOrder,
                        field::FieldViewMode::Intensity,
                        options);
                case holographyui::DisplayPlane::H2Exposure:
                    return field::renderLinearIntensity(
                        channel.h2.recordedRelativeIntensity, options);
                case holographyui::DisplayPlane::H2ReplayImage:
                    return field::renderFieldView(
                        channel.h2IsolatedImageAtH1ImagePlane,
                        field::FieldViewMode::Intensity,
                        options);
                }
                throw std::invalid_argument("unsupported holography display plane");
            }();
            if (!holographyTexture_ || !holographyTexture_->uploadImage(image)) {
                throw std::runtime_error(
                    "OpenGL rejected the holography texture upload");
            }
            holographyErrorMessage_.clear();
        } catch (const std::exception& ex) {
            holographyErrorMessage_ = "Holography visualization failed: "
                + std::string(ex.what());
            holographyStatusMessage_.clear();
        }
    }
}

void Application::loadSlmCalibration() {
    try {
        const std::filesystem::path path(slmCalibrationPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("SLM calibration path cannot be empty");
        }
        auto response = optics::slm::loadSlmResponseJson(path);
        const std::size_t curveCount = response.wavelengths().size();
        slmInterferenceUiState_.setCalibration(std::move(response), path.string());
        recordLessonEdit();
        slmInterferenceErrorMessage_.clear();
        slmInterferenceStatusMessage_ = "Loaded measured LUT with "
            + std::to_string(curveCount)
            + " wavelength curve(s); press Apply to use it";
    } catch (const std::exception& ex) {
        slmInterferenceErrorMessage_ = "SLM calibration load failed: " + std::string(ex.what());
        slmInterferenceStatusMessage_.clear();
    }
}

void Application::saveSlmCalibration() {
    try {
        const std::filesystem::path path(slmCalibrationPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("SLM calibration path cannot be empty");
        }
        const auto& response = slmInterferenceUiState_.draftConfig().calibratedResponse;
        if (!response.has_value()) {
            throw std::invalid_argument("there is no measured SLM LUT to export");
        }
        optics::slm::saveSlmResponseJson(path, response.value());
        slmInterferenceErrorMessage_.clear();
        slmInterferenceStatusMessage_ = "Exported measured LUT to " + path.string();
    } catch (const std::exception& ex) {
        slmInterferenceErrorMessage_ = "SLM calibration export failed: " + std::string(ex.what());
        slmInterferenceStatusMessage_.clear();
    }
}

void Application::loadReflectionRefractionProject() {
    try {
        const std::filesystem::path path(reflectionProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "reflection/refraction project path cannot be empty");
        }
        auto document
            = reflection::loadReflectionRefractionWorkbench(path);
        const auto result
            = reflection::evaluateReflectionRefraction(document.config);
        if (learnSession_.activeLessonId() == "reflection_refraction") {
            learnSession_.replaceReflectionConfig(document.config);
        }
        reflectionRefractionConfig_ = document.config;
        reflectionRefractionResult_ = result;
        reflectionProjectProvenance_ = std::move(document.provenance);
        reflectionProjectName_ = std::move(document.name);
        recordLessonEdit();
        reflectionErrorMessage_.clear();
        reflectionStatusMessage_
            = "Loaded reflection/refraction workbench from " + path.string();
    } catch (const std::exception& ex) {
        reflectionErrorMessage_ = "Load failed: " + std::string(ex.what());
        reflectionStatusMessage_.clear();
    }
}

void Application::saveReflectionRefractionProject() {
    try {
        const std::filesystem::path path(reflectionProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "reflection/refraction project path cannot be empty");
        }
        reflection::ReflectionRefractionWorkbenchDocument document;
        document.name = reflectionProjectName_;
        document.provenance = reflectionProjectProvenance_;
        document.config = reflectionRefractionConfig_;
        reflection::saveReflectionRefractionWorkbench(path, document);
        reflectionErrorMessage_.clear();
        reflectionStatusMessage_
            = "Saved reflection/refraction workbench to " + path.string();
    } catch (const std::exception& ex) {
        reflectionErrorMessage_ = "Save failed: " + std::string(ex.what());
        reflectionStatusMessage_.clear();
    }
}

void Application::loadSlmExperimentProject() {
    try {
        const std::filesystem::path path(slmProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("SLM experiment project path cannot be empty");
        }
        auto document = slmproject::loadSlmInterferenceProject(path);
        slmInterferenceUiState_.replaceDraftProject(
            std::move(document.config),
            std::move(document.calibrationProvenance));
        slmProjectProvenance_ = std::move(document.provenance);
        slmProjectName_ = std::move(document.name);
        recordLessonEdit();
        slmInterferenceErrorMessage_.clear();
        slmInterferenceStatusMessage_ = "Loaded SLM experiment project from "
            + path.string() + "; press Apply to recompute";
    } catch (const std::exception& ex) {
        slmInterferenceErrorMessage_ = "SLM experiment project load failed: "
            + std::string(ex.what());
        slmInterferenceStatusMessage_.clear();
    }
}

void Application::saveSlmExperimentProject() {
    try {
        const std::filesystem::path path(slmProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("SLM experiment project path cannot be empty");
        }
        slmproject::SlmInterferenceProjectDocument document;
        document.name = slmProjectName_;
        document.provenance = slmProjectProvenance_;
        document.config = slmInterferenceUiState_.draftConfig();
        document.calibrationProvenance
            = slmInterferenceUiState_.draftCalibrationSource();
        slmproject::saveSlmInterferenceProject(path, document);
        slmInterferenceErrorMessage_.clear();
        slmInterferenceStatusMessage_ = "Saved draft SLM experiment project to "
            + path.string();
    } catch (const std::exception& ex) {
        slmInterferenceErrorMessage_ = "SLM experiment project save failed: "
            + std::string(ex.what());
        slmInterferenceStatusMessage_.clear();
    }
}

void Application::loadWaveWorkbenchProject() {
    try {
        const std::filesystem::path path(waveProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "wave workbench project path cannot be empty");
        }
        auto document = waveproject::loadWaveWorkbenchProject(path);
        detectorUiState_.setDraftConfig(document.waveDetector);
        samplingDebuggerConfig_ = document.samplingDebugger;
        waveProjectProvenance_ = std::move(document.provenance);
        waveProjectName_ = std::move(document.name);
        samplingDebuggerResult_.reset();
        recordLessonEdit();
        detectorErrorMessage_.clear();
        detectorStatusMessage_ = "Loaded wave workbench project from "
            + path.string() + "; press Apply, then Refresh Sampling Debugger";
    } catch (const std::exception& ex) {
        detectorErrorMessage_ = "Wave workbench project load failed: "
            + std::string(ex.what());
        detectorStatusMessage_.clear();
    }
}

void Application::saveWaveWorkbenchProject() {
    try {
        const std::filesystem::path path(waveProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "wave workbench project path cannot be empty");
        }
        waveproject::WaveWorkbenchProjectDocument document;
        document.name = waveProjectName_;
        document.provenance = waveProjectProvenance_;
        document.waveDetector = detectorUiState_.draftConfig();
        document.samplingDebugger = samplingDebuggerConfig_;
        waveproject::saveWaveWorkbenchProject(path, document);
        detectorErrorMessage_.clear();
        detectorStatusMessage_ = "Saved draft wave workbench project to "
            + path.string();
    } catch (const std::exception& ex) {
        detectorErrorMessage_ = "Wave workbench project save failed: "
            + std::string(ex.what());
        detectorStatusMessage_.clear();
    }
}

void Application::loadHolographyProject() {
    try {
        const std::filesystem::path path(holographyProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "holography experiment project path cannot be empty");
        }
        auto document = holographyproject::loadHolographyProject(path);
        holographyUiState_.replaceDraftProject(std::move(document.config));
        holographyProjectName_ = std::move(document.name);
        holographyProjectProvenance_ = std::move(document.provenance);
        holographyErrorMessage_.clear();
        holographyStatusMessage_ = "Loaded holography experiment from "
            + path.string() + "; press Apply to recompute";
    } catch (const std::exception& ex) {
        holographyErrorMessage_ = "Holography project load failed: "
            + std::string(ex.what());
        holographyStatusMessage_.clear();
    }
}

void Application::saveHolographyProject() {
    try {
        const std::filesystem::path path(holographyProjectPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "holography experiment project path cannot be empty");
        }
        holographyproject::HolographyProjectDocument document;
        document.name = holographyProjectName_;
        document.provenance = holographyProjectProvenance_;
        document.config = holographyUiState_.draftConfig();
        holographyproject::saveHolographyProject(path, document);
        holographyErrorMessage_.clear();
        holographyStatusMessage_ = "Saved draft holography experiment to "
            + path.string();
    } catch (const std::exception& ex) {
        holographyErrorMessage_ = "Holography project save failed: "
            + std::string(ex.what());
        holographyStatusMessage_.clear();
    }
}

void Application::refreshSamplingDebugger() {
    if (!detectorResult_) {
        samplingDebuggerErrorMessage_ = "Sampling Debugger requires a detector field";
        samplingDebuggerStatusMessage_.clear();
        samplingDebuggerResult_.reset();
        return;
    }
    try {
        if (!detectorFftBackend_) {
            throw std::runtime_error("CPU FFT backend is unavailable");
        }
        if (hasDetectorProbe_
            && detectorProbe_.x < detectorResult_->field.width()
            && detectorProbe_.y < detectorResult_->field.height()) {
            samplingDebuggerConfig_.probeXIndex = detectorProbe_.x;
            samplingDebuggerConfig_.probeYIndex = detectorProbe_.y;
        }
        samplingDebuggerConfig_.probeDistancesMetres = {0.0};
        if (samplingDebuggerConfig_.propagationDistanceMetres != 0.0) {
            samplingDebuggerConfig_.probeDistancesMetres.push_back(
                samplingDebuggerConfig_.propagationDistanceMetres);
        }
        auto result = samplingdebug::analyzeSamplingDebugger(
            detectorResult_->field,
            samplingDebuggerConfig_,
            *detectorFftBackend_);
        if (!samplingSpectrumTexture_
            || !samplingSpectrumTexture_->uploadImage(result.angularSpectrumImage)) {
            throw std::runtime_error("OpenGL rejected the angular-spectrum texture upload");
        }
        if (!fourFObjectTexture_ || !fourFObjectTexture_->uploadImage(result.objectPlaneImage)
            || !fourFBeforeFilterTexture_
            || !fourFBeforeFilterTexture_->uploadImage(result.fourierPlaneBeforeFilterImage)
            || !fourFAfterFilterTexture_
            || !fourFAfterFilterTexture_->uploadImage(result.fourierPlaneAfterFilterImage)
            || !fourFImageTexture_ || !fourFImageTexture_->uploadImage(result.imagePlaneImage)) {
            throw std::runtime_error("OpenGL rejected a 4-f plane texture upload");
        }
        samplingDebuggerResult_ = std::make_unique<samplingdebug::SamplingDebuggerResult>(
            std::move(result));
        samplingDebuggerErrorMessage_.clear();
        samplingDebuggerStatusMessage_ =
            "Debugger refreshed from the selected detector plane on the CPU reference backend";
    } catch (const std::exception& ex) {
        samplingDebuggerErrorMessage_ = "Sampling Debugger failed: " + std::string(ex.what());
        samplingDebuggerStatusMessage_.clear();
        samplingDebuggerResult_.reset();
        if (samplingSpectrumTexture_) {
            samplingSpectrumTexture_->destroy();
        }
        if (fourFObjectTexture_) {
            fourFObjectTexture_->destroy();
        }
        if (fourFBeforeFilterTexture_) {
            fourFBeforeFilterTexture_->destroy();
        }
        if (fourFAfterFilterTexture_) {
            fourFAfterFilterTexture_->destroy();
        }
        if (fourFImageTexture_) {
            fourFImageTexture_->destroy();
        }
    }
}

void Application::refreshRealLensWorkbench() {
    try {
        auto result = reallens::runRealLensWorkbench(realLensConfig_);
        const std::size_t accepted = result.spotDiagram.samples.size();
        const std::size_t rejected = result.spotDiagram.rejectedRays.size();
        realLensResult_ = std::make_unique<reallens::RealLensWorkbenchResult>(
            std::move(result));
        realLensErrorMessage_.clear();
        realLensStatusMessage_ = "Real-lens analysis refreshed: "
            + std::to_string(accepted) + " accepted, "
            + std::to_string(rejected) + " rejected rays";
        realLensDirty_ = false;
    } catch (const std::exception& ex) {
        realLensErrorMessage_ = "Real-lens analysis failed: " + std::string(ex.what());
        realLensStatusMessage_.clear();
    }
}

void Application::loadRealLensPrescription(bool csv) {
    try {
        const std::filesystem::path path(realLensPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("prescription path cannot be empty");
        }
        const std::string expectedExtension = csv ? ".csv" : ".json";
        std::string extension = path.extension().string();
        std::transform(
            extension.begin(), extension.end(), extension.begin(),
            [](unsigned char character) {
                return static_cast<char>(std::tolower(character));
            });
        if (extension != expectedExtension) {
            throw std::invalid_argument(
                "selected loader requires a " + expectedExtension
                + " prescription asset");
        }
        auto loaded = loadLensPrescriptionAsset(path);
        realLensPrescriptionCatalog_.registerPrescription(
            loaded.prescription, loaded.provenance);
        realLensConfig_.prescription = loaded.prescription;
        activeLensPrescriptionAsset_ = std::move(loaded);
        selectedRealLensSurface_ = 0;
        realLensDirty_ = true;
        refreshRealLensWorkbench();
        if (realLensErrorMessage_.empty()) {
            realLensStatusMessage_ = "Loaded " + std::string(csv ? "CSV" : "JSON")
                + " prescription from " + path.string();
        }
    } catch (const std::exception& ex) {
        realLensErrorMessage_ = "Prescription load failed: " + std::string(ex.what());
        realLensStatusMessage_.clear();
    }
}

void Application::saveRealLensPrescription(bool csv) {
    try {
        const std::filesystem::path path(realLensPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("prescription path cannot be empty");
        }
        if (csv) {
            optics::io::saveLensPrescriptionCsv(realLensConfig_.prescription, path);
        } else {
            optics::io::saveLensPrescriptionJson(realLensConfig_.prescription, path);
        }
        realLensErrorMessage_.clear();
        realLensStatusMessage_ = "Saved " + std::string(csv ? "CSV" : "JSON")
            + " prescription to " + path.string();
    } catch (const std::exception& ex) {
        realLensErrorMessage_ = "Prescription save failed: " + std::string(ex.what());
        realLensStatusMessage_.clear();
    }
}

void Application::loadDetectorResponseForBench() {
    try {
        const std::filesystem::path path(detectorResponsePathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "detector response path cannot be empty");
        }
        auto loaded = loadDetectorResponseAsset(path);
        detectorResponseCatalog_.registerResponse(
            loaded.response, loaded.provenance);
        const std::string calibrationId
            = loaded.response.calibrationId();
        activeDetectorResponseAsset_ = std::move(loaded);
        errorMessage_.clear();
        statusMessage_ = "Loaded verified detector response "
            + calibrationId + " from " + path.string();
    } catch (const std::exception& error) {
        errorMessage_ = "Detector response load failed: "
            + std::string(error.what());
        statusMessage_.clear();
    }
}

void Application::loadSlmResponseForBench() {
    try {
        const std::filesystem::path path(placedSlmResponsePathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument(
                "SLM response path cannot be empty");
        }
        auto loaded = loadSlmResponseAsset(path);
        loaded.provenance.source = path.generic_string();
        slmResponseCatalog_.registerResponse(loaded);
        const std::string calibrationId = loaded.calibrationId;
        activeSlmResponseAsset_ = std::move(loaded);
        errorMessage_.clear();
        statusMessage_ = "Loaded verified placed-SLM response "
            + calibrationId + " from " + path.string();
    } catch (const std::exception& error) {
        errorMessage_ = "Placed SLM response load failed: "
            + std::string(error.what());
        statusMessage_.clear();
    }
}

void Application::drawReflectionRefractionPanel() {
    if (!ImGui::CollapsingHeader(
            "Reflection / Refraction Workbench",
            ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    if (reflectionProjectProvenance_.originKind
        == project::ProjectOriginKind::LessonTemplate) {
        ImGui::Text(
            "Origin: lesson template %s v%d",
            reflectionProjectProvenance_.sourceId.c_str(),
            reflectionProjectProvenance_.sourceVersion);
    } else {
        ImGui::TextDisabled("Origin: user project");
    }
    ImGui::Text("Project: %s", reflectionProjectName_.c_str());
    ImGui::InputText(
        "Workbench JSON path",
        reflectionProjectPathBuffer_,
        sizeof(reflectionProjectPathBuffer_));
    if (ImGui::Button("Load reflection project")) {
        loadReflectionRefractionProject();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save reflection project")) {
        saveReflectionRefractionProject();
    }

    auto config = reflectionRefractionConfig_;
    float angleDegrees = static_cast<float>(
        config.incidenceAngleRadians * 180.0
        / std::numbers::pi_v<double>);
    bool edited = false;
    if (ImGui::SliderFloat(
            "Workbench incidence angle", &angleDegrees,
            0.0F, 85.0F, "%.1f deg")) {
        config.incidenceAngleRadians = static_cast<double>(angleDegrees)
            * std::numbers::pi_v<double> / 180.0;
        edited = true;
    }
    edited = ImGui::InputDouble(
        "Workbench incident index n1",
        &config.incidentRefractiveIndex,
        0.01, 0.1, "%.4f") || edited;
    edited = ImGui::InputDouble(
        "Workbench transmitted index n2",
        &config.transmittedRefractiveIndex,
        0.01, 0.1, "%.4f") || edited;
    if (edited) {
        static_cast<void>(applyReflectionRefractionConfig(config));
    }

    ImGui::Text(
        "Incidence %.3f deg | reflection %.3f deg | error %.3e rad",
        reflectionRefractionResult_.incidenceAngleRadians * 180.0
            / std::numbers::pi_v<double>,
        reflectionRefractionResult_.reflectionAngleRadians * 180.0
            / std::numbers::pi_v<double>,
        reflectionRefractionResult_.reflectionAngleErrorRadians);
    if (reflectionRefractionResult_.totalInternalReflection) {
        ImGui::TextColored(
            ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
            "Total internal reflection: no transmitted ray.");
    } else {
        ImGui::Text(
            "Transmission %.3f deg | Snell residual %.3e",
            reflectionRefractionResult_.transmissionAngleRadians * 180.0
                / std::numbers::pi_v<double>,
            reflectionRefractionResult_.snellResidual);
    }
    ImGui::TextDisabled(
        "Uses the shared planar mirror/interface ray solvers; results are recomputed and not persisted.");
    if (!reflectionErrorMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s", reflectionErrorMessage_.c_str());
    } else if (!reflectionStatusMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
            "%s", reflectionStatusMessage_.c_str());
    }
}

void Application::drawWaveDetectorPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kWaveDetectorWindowName);

    if (ImGui::CollapsingHeader(
            "Wave Workbench Project", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (waveProjectProvenance_.originKind
            == project::ProjectOriginKind::LessonTemplate) {
            ImGui::Text(
                "Origin: lesson template %s v%d",
                waveProjectProvenance_.sourceId.c_str(),
                waveProjectProvenance_.sourceVersion);
        } else {
            ImGui::TextDisabled("Origin: user project");
        }
        ImGui::Text("Project: %s", waveProjectName_.c_str());
        ImGui::InputText(
            "Path##wave_project",
            waveProjectPathBuffer_,
            sizeof(waveProjectPathBuffer_));
        if (ImGui::Button("Save Wave Project")) {
            saveWaveWorkbenchProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Wave Project")) {
            loadWaveWorkbenchProject();
        }
        ImGui::TextDisabled(
            "Load replaces drafts only; Apply and Refresh remain explicit.");
    }

    auto draft = detectorUiState_.draftConfig();
    bool physicsEdited = false;
    const auto inputNanometres = [&physicsEdited](const char* label, double& metres) {
        double nanometres = metres * 1.0e9;
        if (ImGui::InputDouble(label, &nanometres, 1.0, 10.0, "%.3f nm", ImGuiInputTextFlags_CharsScientific)) {
            metres = nanometres * 1.0e-9;
            physicsEdited = true;
        }
    };
    const auto inputMillimetres = [&physicsEdited](const char* label, double& metres, double step = 0.01) {
        double millimetres = metres * 1.0e3;
        if (ImGui::InputDouble(label, &millimetres, step, step * 10.0, "%.6g mm", ImGuiInputTextFlags_CharsScientific)) {
            metres = millimetres * 1.0e-3;
            physicsEdited = true;
        }
    };

    if (ImGui::CollapsingHeader("Wave Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        int sourceKind = (draft.sourceKind == wave::WaveSourceKind::PlaneWave) ? 0 : 1;
        constexpr std::array<const char*, 2> sourceNames {"Plane wave", "Gaussian beam"};
        if (ImGui::Combo("Source model", &sourceKind, sourceNames.data(), static_cast<int>(sourceNames.size()))) {
            draft.sourceKind = sourceKind == 0
                ? wave::WaveSourceKind::PlaneWave
                : wave::WaveSourceKind::GaussianBeam;
            physicsEdited = true;
        }
        inputNanometres("Vacuum wavelength", draft.wavelengthMetres);
        if (draft.sourceKind == wave::WaveSourceKind::GaussianBeam) {
            inputMillimetres("Waist radius w0", draft.gaussianWaistRadiusMetres);
            ImGui::TextDisabled("Scalar, coherent fundamental Gaussian at its waist plane.");
        } else {
            if (ImGui::InputDouble("Direction cosine X", &draft.planeWaveDirectionCosineX, 0.001, 0.01, "%.6f")) {
                physicsEdited = true;
            }
            if (ImGui::InputDouble("Direction cosine Y", &draft.planeWaveDirectionCosineY, 0.001, 0.01, "%.6f")) {
                physicsEdited = true;
            }
            ImGui::TextDisabled("Direction cosines must satisfy lx^2 + ly^2 <= 1.");
        }
    }

    if (ImGui::CollapsingHeader("Aperture", ImGuiTreeNodeFlags_DefaultOpen)) {
        int apertureKind = static_cast<int>(draft.apertureKind);
        constexpr std::array<const char*, 4> apertureNames {"None", "Circular", "Rectangular", "Double slit"};
        if (ImGui::Combo("Aperture model", &apertureKind, apertureNames.data(), static_cast<int>(apertureNames.size()))) {
            draft.apertureKind = static_cast<wave::WaveApertureKind>(apertureKind);
            physicsEdited = true;
        }
        switch (draft.apertureKind) {
        case wave::WaveApertureKind::None:
            break;
        case wave::WaveApertureKind::Circular:
            inputMillimetres("Radius", draft.circularApertureRadiusMetres);
            break;
        case wave::WaveApertureKind::Rectangular:
            inputMillimetres("Half width", draft.rectangularHalfWidthMetres);
            inputMillimetres("Half height", draft.rectangularHalfHeightMetres);
            break;
        case wave::WaveApertureKind::DoubleSlit:
            inputMillimetres("Slit width", draft.doubleSlitWidthMetres);
            inputMillimetres("Slit height", draft.doubleSlitHeightMetres);
            inputMillimetres("Centre separation", draft.doubleSlitSeparationMetres);
            break;
        }
        if (draft.apertureKind != wave::WaveApertureKind::None) {
            inputMillimetres("Centre X", draft.apertureCenterXMetres);
            inputMillimetres("Centre Y", draft.apertureCenterYMetres);
        }
    }

    if (ImGui::CollapsingHeader("Thin Lens")) {
        if (ImGui::Checkbox("Enable scalar thin-lens phase", &draft.enableThinLens)) {
            physicsEdited = true;
        }
        if (draft.enableThinLens) {
            inputMillimetres("Focal length", draft.thinLensFocalLengthMetres);
            inputMillimetres("Lens centre X", draft.thinLensCenterXMetres);
            inputMillimetres("Lens centre Y", draft.thinLensCenterYMetres);
            ImGui::TextDisabled("Ideal, monochromatic, zero-thickness lens approximation.");
        }
    }

    if (ImGui::CollapsingHeader("Propagation & Detector Grid", ImGuiTreeNodeFlags_DefaultOpen)) {
        int propagator = draft.propagator == wave::WavePropagatorKind::AngularSpectrum ? 0 : 1;
        constexpr std::array<const char*, 2> propagatorNames {"Angular spectrum (ASM)", "Fresnel transfer function"};
        if (ImGui::Combo("Propagation method", &propagator, propagatorNames.data(), static_cast<int>(propagatorNames.size()))) {
            draft.propagator = propagator == 0
                ? wave::WavePropagatorKind::AngularSpectrum
                : wave::WavePropagatorKind::FresnelTransferFunction;
            physicsEdited = true;
        }
        inputMillimetres("Distance z", draft.propagationDistanceMetres, 0.1);
        inputMillimetres("Square grid span", draft.gridPhysicalSpanMetres, 0.1);
        constexpr std::array<int, 4> resolutions {64, 128, 256, 512};
        int resolutionIndex = 1;
        for (std::size_t index = 0; index < resolutions.size(); ++index) {
            if (draft.gridResolution == static_cast<std::size_t>(resolutions[index])) {
                resolutionIndex = static_cast<int>(index);
            }
        }
        constexpr std::array<const char*, 4> resolutionNames {"64 x 64", "128 x 128", "256 x 256", "512 x 512"};
        if (ImGui::Combo("Grid resolution", &resolutionIndex, resolutionNames.data(), static_cast<int>(resolutionNames.size()))) {
            draft.gridResolution = static_cast<std::size_t>(resolutions[static_cast<std::size_t>(resolutionIndex)]);
            physicsEdited = true;
        }
        if (ImGui::InputDouble("Refractive index n", &draft.refractiveIndex, 0.001, 0.01, "%.6f")) {
            physicsEdited = true;
        }
        const double pitchMicrometres = draft.gridPhysicalSpanMetres * 1.0e6 / static_cast<double>(draft.gridResolution);
        ImGui::Text("Sampling pitch: %.4g um", pitchMicrometres);
        ImGui::TextDisabled("Periodic FFT boundary; scalar, monochromatic, coherent propagation.");
    }

    if (physicsEdited) {
        detectorUiState_.setDraftConfig(draft);
        recordLessonEdit();
    }
    if (detectorUiState_.isDirty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.2F, 1.0F), "Parameters edited - press Apply to recompute");
    }
    if (ImGui::Button("Apply & Recompute")) {
        detectorUiState_.apply();
        detectorResult_.reset();
        samplingDebuggerResult_.reset();
        detectorStatusMessage_ = "Detector recompute queued";
        detectorErrorMessage_.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Propagation never runs continuously per frame.");

    ImGui::SeparatorText("Screen Display");
    int viewMode = static_cast<int>(detectorUiState_.viewMode());
    constexpr std::array<const char*, 3> viewNames {"Linear intensity", "Log intensity (dB)", "Wrapped phase"};
    if (ImGui::Combo("Displayed observable", &viewMode, viewNames.data(), static_cast<int>(viewNames.size()))) {
        detectorUiState_.setViewMode(static_cast<field::FieldViewMode>(viewMode));
    }

    if (!detectorErrorMessage_.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.35F, 0.35F, 1.0F), "%s", detectorErrorMessage_.c_str());
    } else if (!detectorStatusMessage_.empty()) {
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.5F, 1.0F), "%s", detectorStatusMessage_.c_str());
    }

    if (detectorResult_ && detectorTexture_ && detectorTexture_->isValid()) {
        ImGui::Text("Peak I: %.6g | Integrated I: %.6g", detectorResult_->peakIntensity, detectorResult_->integratedIntensity);
        ImGui::TextWrapped("%s", detectorResult_->diagnosticSummary.c_str());

        const ImVec2 available = ImGui::GetContentRegionAvail();
        const float imageHeightBudget = std::max(180.0F, available.y - 110.0F);
        const auto layout = waveui::fitDetectorImage(
            available.x,
            imageHeightBudget,
            detectorResult_->field.width(),
            detectorResult_->field.height());
        if (layout.width > 0.0F && layout.height > 0.0F) {
            const float horizontalOffset = std::max(0.0F, (available.x - layout.width) * 0.5F);
            ImGui::SetCursorPosX(ImGui::GetCursorPosX() + horizontalOffset);
            ImGui::Image(
                toImTextureID(detectorTexture_->handle()),
                ImVec2(layout.width, layout.height),
                ImVec2(0.0F, 1.0F),
                ImVec2(1.0F, 0.0F));
            const ImVec2 imageMin = ImGui::GetItemRectMin();
            if (ImGui::IsItemHovered()) {
                const ImVec2 mouse = ImGui::GetMousePos();
                waveui::DetectorPixel hovered;
                if (waveui::mapDisplayPointToDetectorPixel(
                        mouse.x - imageMin.x,
                        mouse.y - imageMin.y,
                        layout.width,
                        layout.height,
                        detectorResult_->field.width(),
                        detectorResult_->field.height(),
                        hovered)) {
                    if (!detectorProbeLocked_) {
                        detectorProbe_ = hovered;
                        hasDetectorProbe_ = true;
                    }
                    if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                        detectorProbe_ = hovered;
                        hasDetectorProbe_ = true;
                        detectorProbeLocked_ = true;
                    }
                }
            }
        }

        ImGui::SeparatorText("Probe");
        if (hasDetectorProbe_
            && detectorProbe_.x < detectorResult_->field.width()
            && detectorProbe_.y < detectorResult_->field.height()) {
            const auto sample = detectorResult_->field.at(detectorProbe_.x, detectorProbe_.y);
            const double intensity = std::norm(sample);
            const double phase = std::arg(sample);
            ImGui::Text("%s pixel (%zu, %zu)", detectorProbeLocked_ ? "Locked" : "Hover", detectorProbe_.x, detectorProbe_.y);
            ImGui::Text("x = %.6g mm, y = %.6g mm",
                detectorResult_->field.xCoordinateMetres(detectorProbe_.x) * 1.0e3,
                detectorResult_->field.yCoordinateMetres(detectorProbe_.y) * 1.0e3);
            ImGui::Text("E = %.9g %+.9gi", sample.real(), sample.imag());
            ImGui::Text("Intensity = %.9g | Wrapped phase = %.9g rad", intensity, phase);
            if (detectorProbeLocked_) {
                ImGui::SameLine();
                if (ImGui::SmallButton("Clear lock")) {
                    detectorProbeLocked_ = false;
                }
            }
        } else {
            ImGui::TextDisabled("Hover the detector to probe; click to lock a sample.");
        }
    } else {
        ImGui::TextDisabled("Detector output is not available yet.");
    }

    ImGui::End();
}

void Application::drawSamplingDebuggerPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kSamplingDebuggerWindowName);
    const auto configBeforeEditing = samplingDebuggerConfig_;
    ImGui::TextDisabled("Selected plane: current Wave Detector output (relative probe z)");
    if (ImGui::CollapsingHeader("How to read this debugger", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::BulletText(
            "The Fourier-plane centre is the field's average/DC content; distance from the centre means finer spatial detail.");
        ImGui::BulletText(
            "A low-pass stop blocks outer fine-detail frequencies, so its image becomes smoother or blurred.");
        ImGui::BulletText(
            "A larger pupil or NA makes the PSF narrower and extends the MTF cutoff, preserving finer detail.");
        ImGui::BulletText(
            "Nyquist, padding, boundary, and wrap warnings mean a plausible-looking image may be a sampling artefact.");
    }

    double angleXDegrees = samplingDebuggerConfig_.requestedHalfAngleXRadians
        * 180.0 / std::numbers::pi;
    double angleYDegrees = samplingDebuggerConfig_.requestedHalfAngleYRadians
        * 180.0 / std::numbers::pi;
    double probeDistanceMillimetres = samplingDebuggerConfig_.propagationDistanceMetres * 1e3;
    double focalLengthMillimetres = samplingDebuggerConfig_.psfFocalLengthMetres * 1e3;
    double pupilRadiusMillimetres = samplingDebuggerConfig_.psfPupilRadiusMetres * 1e3;
    double fourFFirstFocalLengthMillimetres =
        samplingDebuggerConfig_.fourFFirstFocalLengthMetres * 1e3;
    double fourFSecondFocalLengthMillimetres =
        samplingDebuggerConfig_.fourFSecondFocalLengthMetres * 1e3;
    double fourFFilterInnerRadiusMillimetres =
        samplingDebuggerConfig_.fourFFilterInnerRadiusMetres * 1e3;
    double fourFFilterOuterRadiusMillimetres =
        samplingDebuggerConfig_.fourFFilterOuterRadiusMetres * 1e3;
    ImGui::InputDouble("Requested half-angle X", &angleXDegrees, 0.1, 1.0, "%.4g deg");
    ImGui::InputDouble("Requested half-angle Y", &angleYDegrees, 0.1, 1.0, "%.4g deg");
    ImGui::InputDouble("Probe offset z", &probeDistanceMillimetres, 0.1, 1.0, "%.6g mm");
    ImGui::InputDouble("PSF lens focal length", &focalLengthMillimetres, 0.1, 1.0, "%.6g mm");
    ImGui::InputDouble("Circular pupil radius", &pupilRadiusMillimetres, 0.01, 0.1, "%.6g mm");
    ImGui::SeparatorText("4-f spatial filter");
    ImGui::InputDouble(
        "First lens focal length f1",
        &fourFFirstFocalLengthMillimetres,
        0.1,
        1.0,
        "%.6g mm");
    ImGui::InputDouble(
        "Second lens focal length f2",
        &fourFSecondFocalLengthMillimetres,
        0.1,
        1.0,
        "%.6g mm");
    constexpr const char* kFourFFilterNames =
        "Pass all\0Low pass\0High pass\0Band pass\0";
    int fourFFilterIndex = static_cast<int>(samplingDebuggerConfig_.fourFFilterKind);
    if (ImGui::Combo("Circular filter", &fourFFilterIndex, kFourFFilterNames)) {
        samplingDebuggerConfig_.fourFFilterKind =
            static_cast<compute::fourier::CircularFilterKind>(fourFFilterIndex);
    }
    if (samplingDebuggerConfig_.fourFFilterKind
        == compute::fourier::CircularFilterKind::HighPass
        || samplingDebuggerConfig_.fourFFilterKind
            == compute::fourier::CircularFilterKind::BandPass) {
        ImGui::InputDouble(
            "Inner radius",
            &fourFFilterInnerRadiusMillimetres,
            0.01,
            0.1,
            "%.6g mm");
    }
    if (samplingDebuggerConfig_.fourFFilterKind
        == compute::fourier::CircularFilterKind::LowPass
        || samplingDebuggerConfig_.fourFFilterKind
            == compute::fourier::CircularFilterKind::BandPass) {
        ImGui::InputDouble(
            "Outer radius",
            &fourFFilterOuterRadiusMillimetres,
            0.01,
            0.1,
            "%.6g mm");
    }
    samplingDebuggerConfig_.requestedHalfAngleXRadians = angleXDegrees
        * std::numbers::pi / 180.0;
    samplingDebuggerConfig_.requestedHalfAngleYRadians = angleYDegrees
        * std::numbers::pi / 180.0;
    samplingDebuggerConfig_.propagationDistanceMetres = probeDistanceMillimetres * 1e-3;
    samplingDebuggerConfig_.psfFocalLengthMetres = focalLengthMillimetres * 1e-3;
    samplingDebuggerConfig_.psfPupilRadiusMetres = pupilRadiusMillimetres * 1e-3;
    samplingDebuggerConfig_.fourFFirstFocalLengthMetres =
        fourFFirstFocalLengthMillimetres * 1e-3;
    samplingDebuggerConfig_.fourFSecondFocalLengthMetres =
        fourFSecondFocalLengthMillimetres * 1e-3;
    samplingDebuggerConfig_.fourFFilterInnerRadiusMetres =
        fourFFilterInnerRadiusMillimetres * 1e-3;
    samplingDebuggerConfig_.fourFFilterOuterRadiusMetres =
        fourFFilterOuterRadiusMillimetres * 1e-3;
    if (samplingDebuggerConfig_ != configBeforeEditing) {
        recordLessonEdit();
    }
    if (ImGui::Button("Refresh Sampling Debugger")) {
        refreshSamplingDebugger();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("Explicit refresh; no per-frame FFT");

    if (!samplingDebuggerErrorMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s",
            samplingDebuggerErrorMessage_.c_str());
    } else if (!samplingDebuggerStatusMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(0.4F, 0.9F, 0.5F, 1.0F),
            "%s",
            samplingDebuggerStatusMessage_.c_str());
    }
    if (!samplingDebuggerResult_) {
        ImGui::TextDisabled("Apply a Wave Detector configuration to create debugger data.");
        ImGui::End();
        return;
    }

    const auto& result = *samplingDebuggerResult_;
    const auto& diagnostics = result.sampling;
    ImGui::SeparatorText("Sampling validity");
    ImGui::Text(
        "Grid: %.6g x %.6g mm | Nyquist: %.5g deg X, %.5g deg Y",
        diagnostics.physicalWidthMetres * 1e3,
        diagnostics.physicalHeightMetres * 1e3,
        diagnostics.nyquistHalfAngleXRadians * 180.0 / std::numbers::pi,
        diagnostics.nyquistHalfAngleYRadians * 180.0 / std::numbers::pi);
    ImGui::Text(
        "Required padding: %.4gx X, %.4gx Y | periodic boundary: %s",
        diagnostics.requiredPaddingFactorX,
        diagnostics.requiredPaddingFactorY,
        diagnostics.periodicBoundary ? "yes" : "no");
    if (!diagnostics.warning.empty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.72F, 0.2F, 1.0F), "%s", diagnostics.warning.c_str());
    } else {
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.5F, 1.0F), "No active sampling warning.");
    }

    ImGui::SeparatorText("Angular spectrum");
    ImGui::Text(
        "Propagating: %zu bins, %.6g%% energy | Evanescent: %zu bins, %.6g%% energy",
        result.angularSpectrum.propagatingBinCount,
        100.0 * result.angularSpectrum.propagatingSpectralEnergyFraction,
        result.angularSpectrum.evanescentBinCount,
        100.0 * result.angularSpectrum.evanescentSpectralEnergyFraction);
    ImGui::TextDisabled("Turbo = propagating; magenta tint = evanescent; intensity shown on a dB floor.");
    if (samplingSpectrumTexture_ && samplingSpectrumTexture_->isValid()) {
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float imageSize = std::clamp(availableWidth, 160.0F, 360.0F);
        ImGui::Image(
            toImTextureID(samplingSpectrumTexture_->handle()),
            ImVec2(imageSize, imageSize),
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F));
    }

    ImGui::SeparatorText("Arbitrary-plane probe");
    ImGui::Text(
        "Fixed pixel (%zu, %zu), x=%.6g mm, y=%.6g mm",
        result.planeProbe.xIndex,
        result.planeProbe.yIndex,
        result.planeProbe.xCoordinateMetres * 1e3,
        result.planeProbe.yCoordinateMetres * 1e3);
    for (const auto& sample : result.planeProbe.samples) {
        ImGui::BulletText(
            "z=%+.6g mm: E=%.7g%+.7gi, I=%.7g, phase=%+.7g rad (%s)",
            sample.distanceMetres * 1e3,
            sample.fieldValue.real(),
            sample.fieldValue.imag(),
            sample.intensity,
            sample.wrappedPhaseRadians,
            sample.phaseValid ? "valid" : "undefined");
    }

    ImGui::SeparatorText("Circular-pupil PSF / incoherent MTF");
    ImGui::Text(
        "First dark radius: %.6g um | coherent cutoff: %.6g cyc/mm | incoherent cutoff: %.6g cyc/mm",
        result.pupilDiagnostics.firstDarkRadiusMetres * 1e6,
        result.pupilDiagnostics.coherentCutoffCyclesPerMetre * 1e-3,
        result.pupilDiagnostics.incoherentCutoffCyclesPerMetre * 1e-3);
    ImGui::TextDisabled("MTF convention: incoherent intensity; PSF is normalized Airy intensity.");
    std::vector<float> psfProfile;
    const std::size_t psfCenter = result.normalizedPsf.width() / 2U;
    psfProfile.reserve(result.normalizedPsf.width() - psfCenter);
    for (std::size_t x = psfCenter; x < result.normalizedPsf.width(); ++x) {
        psfProfile.push_back(static_cast<float>(result.normalizedPsf.at(x, psfCenter)));
    }
    std::vector<float> mtfProfile;
    mtfProfile.reserve(result.incoherentMtf.size());
    for (const auto& sample : result.incoherentMtf) {
        mtfProfile.push_back(static_cast<float>(sample.normalizedIncoherentMtf));
    }
    if (!psfProfile.empty()) {
        ImGui::PlotLines("Airy PSF radial", psfProfile.data(), static_cast<int>(psfProfile.size()), 0, nullptr, 0.0F, 1.0F, ImVec2(0.0F, 80.0F));
    }
    if (!mtfProfile.empty()) {
        ImGui::PlotLines("Incoherent MTF", mtfProfile.data(), static_cast<int>(mtfProfile.size()), 0, nullptr, 0.0F, 1.0F, ImVec2(0.0F, 80.0F));
    }

    ImGui::SeparatorText("4-f coherent relay");
    const auto& filterDiagnostics = result.fourF.filterDiagnostics;
    const double magnification = -result.fourF.secondTransformDiagnostics.focalLengthMetres
        / result.fourF.firstTransformDiagnostics.focalLengthMetres;
    ImGui::Text(
        "Magnification M=-f2/f1: %.7g | image pitch: %.7g x %.7g um",
        magnification,
        result.fourF.imagePlane.pitchXMetres() * 1e6,
        result.fourF.imagePlane.pitchYMetres() * 1e6);
    ImGui::Text(
        "Filter geometry: %zu / %zu samples transmitted, %zu blocked",
        filterDiagnostics.transmittedSampleCount,
        filterDiagnostics.totalSampleCount,
        filterDiagnostics.blockedSampleCount);
    ImGui::Text(
        "Integrated-intensity transmission: %.9g%%",
        100.0 * filterDiagnostics.integratedIntensityTransmission);
    ImGui::TextDisabled(
        "Each log-intensity plane is peak-normalized independently for shape inspection; use the transmission diagnostic for power.");

    const auto drawFourFPlane = [](const char* label, const render::gl::Texture2D* texture) {
        ImGui::TextUnformatted(label);
        if (texture == nullptr || !texture->isValid()) {
            ImGui::TextDisabled("Texture unavailable");
            return;
        }
        const float availableWidth = ImGui::GetContentRegionAvail().x;
        const float imageWidth = std::max(1.0F, std::min(availableWidth, 320.0F));
        const float aspect = static_cast<float>(texture->height())
            / static_cast<float>(texture->width());
        ImGui::Image(
            toImTextureID(texture->handle()),
            ImVec2(imageWidth, imageWidth * aspect),
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F));
    };
    if (ImGui::BeginTable("four_f_planes", 2, ImGuiTableFlags_SizingStretchSame)) {
        ImGui::TableNextColumn();
        drawFourFPlane("Object plane", fourFObjectTexture_.get());
        ImGui::TableNextColumn();
        drawFourFPlane("Fourier plane before filter", fourFBeforeFilterTexture_.get());
        ImGui::TableNextColumn();
        drawFourFPlane("Fourier plane after filter", fourFAfterFilterTexture_.get());
        ImGui::TableNextColumn();
        drawFourFPlane("Image plane", fourFImageTexture_.get());
        ImGui::EndTable();
    }
    ImGui::End();
}

void Application::drawRealLensPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kRealLensWindowName);

    if (realLensDirty_) {
        ImGui::TextColored(
            ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
            "Edited values are not applied; analysis shows the last successful refresh.");
    } else {
        ImGui::TextDisabled("Applied prescription: %s", realLensConfig_.prescription.id.c_str());
    }
    if (ImGui::Button("Refresh Real-Lens Analysis")) {
        refreshRealLensWorkbench();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset N-BK7 Biconvex Example")) {
        realLensConfig_ = reallens::makeDefaultRealLensWorkbenchConfig();
        activeLensPrescriptionAsset_.reset();
        selectedRealLensSurface_ = 0;
        realLensDirty_ = true;
        refreshRealLensWorkbench();
    }

    ImGui::SeparatorText("Prescription import/export");
    ImGui::SetNextItemWidth(std::max(220.0F, ImGui::GetContentRegionAvail().x * 0.45F));
    ImGui::InputText("Path##real_lens_path", realLensPathBuffer_, sizeof(realLensPathBuffer_));
    if (ImGui::Button("Load JSON")) {
        loadRealLensPrescription(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save JSON")) {
        saveRealLensPrescription(false);
    }
    ImGui::SameLine();
    if (ImGui::Button("Load CSV")) {
        loadRealLensPrescription(true);
    }
    ImGui::SameLine();
    if (ImGui::Button("Save CSV")) {
        saveRealLensPrescription(true);
    }
    if (activeLensPrescriptionAsset_.has_value()) {
        ImGui::TextDisabled(
            "Verified imported asset: %s | SHA-256 %.12s...",
            activeLensPrescriptionAsset_->provenance.source.c_str(),
            activeLensPrescriptionAsset_->provenance.contentSha256.c_str());
    } else {
        ImGui::TextDisabled(
            "No verified imported asset is active; edited prescriptions must be saved with a new ID and reloaded before Bench binding.");
    }

    if (ImGui::BeginTabBar("real_lens_tabs")) {
        if (ImGui::BeginTabItem("Prescription Editor")) {
            std::array<char, 129> prescriptionIdBuffer {};
            const std::size_t prescriptionIdLength = std::min(
                realLensConfig_.prescription.id.size(),
                prescriptionIdBuffer.size() - 1U);
            std::copy_n(
                realLensConfig_.prescription.id.data(),
                prescriptionIdLength,
                prescriptionIdBuffer.data());
            if (ImGui::InputText(
                    "Prescription immutable ID",
                    prescriptionIdBuffer.data(),
                    prescriptionIdBuffer.size())) {
                realLensConfig_.prescription.id
                    = prescriptionIdBuffer.data();
                realLensDirty_ = true;
            }
            ImGui::TextDisabled(
                "After changing optical content, choose a new ID, save, and reload before Bench binding.");
            if (ImGui::CollapsingHeader("Analysis setup", ImGuiTreeNodeFlags_DefaultOpen)) {
                double pupilMm = realLensConfig_.entrancePupilSemiDiameterMetres * 1e3;
                if (ImGui::InputDouble("Entrance pupil semi-diameter (mm)", &pupilMm, 0.1, 1.0, "%.6f")) {
                    realLensConfig_.entrancePupilSemiDiameterMetres = pupilMm * 1e-3;
                    realLensDirty_ = true;
                }
                double launchMm = realLensConfig_.objectSpaceDistanceMetres * 1e3;
                if (ImGui::InputDouble("Launch distance before first surface (mm)", &launchMm, 0.5, 5.0, "%.6f")) {
                    realLensConfig_.objectSpaceDistanceMetres = launchMm * 1e-3;
                    realLensDirty_ = true;
                }
                int rings = static_cast<int>(realLensConfig_.pupilRingCount);
                if (ImGui::InputInt("Pupil ring count", &rings) && rings >= 0) {
                    realLensConfig_.pupilRingCount = static_cast<std::size_t>(rings);
                    realLensDirty_ = true;
                }
                int firstRingSamples = static_cast<int>(realLensConfig_.pupilSamplesPerFirstRing);
                if (ImGui::InputInt("Samples on first ring", &firstRingSamples)
                    && firstRingSamples >= 0) {
                    realLensConfig_.pupilSamplesPerFirstRing = static_cast<std::size_t>(firstRingSamples);
                    realLensDirty_ = true;
                }
                double imagePlaneZMm = realLensConfig_.imagePlaneLocalToWorld.translationMetres.z * 1e3;
                if (ImGui::InputDouble("Image plane world Z (mm)", &imagePlaneZMm, 0.1, 1.0, "%.6f")) {
                    realLensConfig_.imagePlaneLocalToWorld.translationMetres.z = imagePlaneZMm * 1e-3;
                    realLensDirty_ = true;
                }
            }

            if (ImGui::CollapsingHeader("Fields", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (ImGui::BeginTable(
                        "real_lens_fields", 4,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
                    ImGui::TableSetupColumn("Field ID");
                    ImGui::TableSetupColumn("Angle X (deg)");
                    ImGui::TableSetupColumn("Angle Y (deg)");
                    ImGui::TableSetupColumn("Power fraction");
                    ImGui::TableHeadersRow();
                    for (std::size_t index = 0; index < realLensConfig_.fields.size(); ++index) {
                        auto& field = realLensConfig_.fields[index];
                        ImGui::PushID(static_cast<int>(index));
                        ImGui::TableNextRow();
                        ImGui::TableSetColumnIndex(0);
                        ImGui::TextUnformatted(field.id.c_str());
                        ImGui::TableSetColumnIndex(1);
                        double angleX = field.angleXRadians * 180.0 / std::numbers::pi;
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::InputDouble("##field_x", &angleX, 0.1, 1.0, "%.6f")) {
                            field.angleXRadians = angleX * std::numbers::pi / 180.0;
                            realLensDirty_ = true;
                        }
                        ImGui::TableSetColumnIndex(2);
                        double angleY = field.angleYRadians * 180.0 / std::numbers::pi;
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::InputDouble("##field_y", &angleY, 0.1, 1.0, "%.6f")) {
                            field.angleYRadians = angleY * std::numbers::pi / 180.0;
                            realLensDirty_ = true;
                        }
                        ImGui::TableSetColumnIndex(3);
                        ImGui::SetNextItemWidth(-1.0F);
                        if (ImGui::InputDouble("##field_power", &field.powerFraction, 0.01, 0.1, "%.9f")) {
                            realLensDirty_ = true;
                        }
                        ImGui::PopID();
                    }
                    ImGui::EndTable();
                }
                if (ImGui::Button("Add zero-power field")) {
                    realLensConfig_.fields.push_back({
                        .id = "field_" + std::to_string(realLensConfig_.fields.size()),
                        .angleXRadians = 0.0,
                        .angleYRadians = 0.0,
                        .powerFraction = 0.0,
                    });
                    realLensDirty_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove last field") && realLensConfig_.fields.size() > 1) {
                    const std::string removedId = realLensConfig_.fields.back().id;
                    realLensConfig_.fields.pop_back();
                    if (realLensConfig_.chromaticReferenceFieldId == removedId) {
                        realLensConfig_.chromaticReferenceFieldId = realLensConfig_.fields.front().id;
                    }
                    realLensDirty_ = true;
                }
                if (ImGui::BeginCombo(
                        "Chromatic reference field",
                        realLensConfig_.chromaticReferenceFieldId.c_str())) {
                    for (const auto& field : realLensConfig_.fields) {
                        const bool selected = field.id == realLensConfig_.chromaticReferenceFieldId;
                        if (ImGui::Selectable(field.id.c_str(), selected)) {
                            realLensConfig_.chromaticReferenceFieldId = field.id;
                            realLensDirty_ = true;
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::TextDisabled("Field power fractions must sum to 1; angles are limited to |80 deg|.");
            }

            if (ImGui::CollapsingHeader("Sequential surfaces", ImGuiTreeNodeFlags_DefaultOpen)) {
                if (selectedRealLensSurface_ >= realLensConfig_.prescription.surfaces.size()) {
                    selectedRealLensSurface_ = 0;
                }
                if (ImGui::BeginListBox("##surface_list", ImVec2(220.0F, 110.0F))) {
                    for (std::size_t index = 0;
                         index < realLensConfig_.prescription.surfaces.size(); ++index) {
                        const bool selected = index == selectedRealLensSurface_;
                        if (ImGui::Selectable(
                                realLensConfig_.prescription.surfaces[index].id.c_str(), selected)) {
                            selectedRealLensSurface_ = index;
                        }
                    }
                    ImGui::EndListBox();
                }
                if (!realLensConfig_.prescription.surfaces.empty()) {
                    auto& surface = realLensConfig_.prescription.surfaces[selectedRealLensSurface_];
                    ImGui::Text("Editing: %s", surface.id.c_str());
                    if (ImGui::InputDouble(
                            "Curvature (1/m)", &surface.geometry.curvaturePerMetre,
                            0.1, 1.0, "%.12g")) {
                        realLensDirty_ = true;
                    }
                    if (ImGui::InputDouble(
                            "Conic constant", &surface.geometry.conicConstant,
                            0.01, 0.1, "%.12g")) {
                        realLensDirty_ = true;
                    }
                    double apertureMm = surface.geometry.clearSemiDiameterMetres * 1e3;
                    if (ImGui::InputDouble(
                            "Clear semi-diameter (mm)", &apertureMm,
                            0.1, 1.0, "%.9g")) {
                        surface.geometry.clearSemiDiameterMetres = apertureMm * 1e-3;
                        realLensDirty_ = true;
                    }
                    if (ImGui::TreeNode("Even asphere coefficients (SI)")) {
                        for (std::size_t termIndex = 0;
                             termIndex < surface.geometry.evenAsphereTerms.size(); ++termIndex) {
                            auto& term = surface.geometry.evenAsphereTerms[termIndex];
                            ImGui::PushID(static_cast<int>(termIndex));
                            int radialOrder = static_cast<int>(term.radialOrder);
                            if (ImGui::InputInt("Radial order", &radialOrder)
                                && radialOrder >= 0) {
                                term.radialOrder = static_cast<unsigned>(radialOrder);
                                realLensDirty_ = true;
                            }
                            if (ImGui::InputDouble(
                                    "Coefficient", &term.coefficientSi,
                                    0.0, 0.0, "%.12e")) {
                                realLensDirty_ = true;
                            }
                            ImGui::Separator();
                            ImGui::PopID();
                        }
                        if (ImGui::Button("Append even-asphere term")) {
                            const unsigned nextOrder = surface.geometry.evenAsphereTerms.empty()
                                ? 4U
                                : surface.geometry.evenAsphereTerms.back().radialOrder + 2U;
                            surface.geometry.evenAsphereTerms.push_back({
                                .radialOrder = nextOrder,
                                .coefficientSi = 0.0,
                            });
                            realLensDirty_ = true;
                        }
                        ImGui::SameLine();
                        if (ImGui::Button("Remove last asphere term")
                            && !surface.geometry.evenAsphereTerms.empty()) {
                            surface.geometry.evenAsphereTerms.pop_back();
                            realLensDirty_ = true;
                        }
                        ImGui::TextDisabled("Orders must be strictly increasing even integers >= 4.");
                        ImGui::TreePop();
                    }
                    std::array<double*, 3> translations {
                        &surface.localToWorld.translationMetres.x,
                        &surface.localToWorld.translationMetres.y,
                        &surface.localToWorld.translationMetres.z,
                    };
                    constexpr std::array<const char*, 3> translationLabels {
                        "Decenter X (mm)", "Decenter Y (mm)", "Vertex Z (mm)"};
                    for (std::size_t axis = 0; axis < translations.size(); ++axis) {
                        double millimetres = *translations[axis] * 1e3;
                        if (ImGui::InputDouble(
                                translationLabels[axis], &millimetres,
                                0.01, 0.1, "%.9g")) {
                            *translations[axis] = millimetres * 1e-3;
                            realLensDirty_ = true;
                        }
                    }

                    const auto materialCombo = [&](const char* label, std::string& selectedId) {
                        if (ImGui::BeginCombo(label, selectedId.c_str())) {
                            for (const auto& material : realLensConfig_.prescription.materials) {
                                const bool selected = selectedId == material.id;
                                if (ImGui::Selectable(material.id.c_str(), selected)) {
                                    selectedId = material.id;
                                    realLensDirty_ = true;
                                }
                            }
                            ImGui::EndCombo();
                        }
                    };
                    materialCombo("Material before", surface.materialBeforeId);
                    materialCombo("Material after", surface.materialAfterId);

                    constexpr double kTiltStep = 0.1 * std::numbers::pi / 180.0;
                    if (ImGui::Button("Tilt local X +0.1 deg")) {
                        tiltSurfaceFrame(surface, 0, kTiltStep);
                        realLensDirty_ = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Tilt local X -0.1 deg")) {
                        tiltSurfaceFrame(surface, 0, -kTiltStep);
                        realLensDirty_ = true;
                    }
                    if (ImGui::Button("Tilt local Y +0.1 deg")) {
                        tiltSurfaceFrame(surface, 1, kTiltStep);
                        realLensDirty_ = true;
                    }
                    ImGui::SameLine();
                    if (ImGui::Button("Tilt local Y -0.1 deg")) {
                        tiltSurfaceFrame(surface, 1, -kTiltStep);
                        realLensDirty_ = true;
                    }
                    ImGui::TextDisabled(
                        "Tilt buttons rotate the rigid basis; scale, shear, and reflection remain prohibited.");
                }
                if (ImGui::Button("Append planar surface")) {
                    const auto& previous = realLensConfig_.prescription.surfaces.back();
                    realLensConfig_.prescription.surfaces.push_back({
                        .id = "surface_" + std::to_string(realLensConfig_.prescription.surfaces.size()),
                        .geometry = {
                            .curvaturePerMetre = 0.0,
                            .conicConstant = 0.0,
                            .evenAsphereTerms = {},
                            .clearSemiDiameterMetres = previous.geometry.clearSemiDiameterMetres,
                        },
                        .localToWorld = {
                            .translationMetres = {
                                previous.localToWorld.translationMetres.x,
                                previous.localToWorld.translationMetres.y,
                                previous.localToWorld.translationMetres.z + 0.005,
                            },
                        },
                        .materialBeforeId = previous.materialAfterId,
                        .materialAfterId = previous.materialAfterId,
                    });
                    selectedRealLensSurface_ = realLensConfig_.prescription.surfaces.size() - 1;
                    realLensDirty_ = true;
                }
                ImGui::SameLine();
                if (ImGui::Button("Remove selected surface")
                    && realLensConfig_.prescription.surfaces.size() > 1) {
                    realLensConfig_.prescription.surfaces.erase(
                        realLensConfig_.prescription.surfaces.begin()
                        + static_cast<std::ptrdiff_t>(selectedRealLensSurface_));
                    selectedRealLensSurface_ = std::min(
                        selectedRealLensSurface_,
                        realLensConfig_.prescription.surfaces.size() - 1);
                    realLensDirty_ = true;
                }
            }

            if (ImGui::CollapsingHeader("Optical materials", ImGuiTreeNodeFlags_DefaultOpen)) {
                for (std::size_t materialIndex = 0;
                     materialIndex < realLensConfig_.prescription.materials.size(); ++materialIndex) {
                    auto& material = realLensConfig_.prescription.materials[materialIndex];
                    ImGui::PushID(static_cast<int>(materialIndex));
                    if (ImGui::TreeNode(material.displayName.c_str())) {
                        ImGui::TextDisabled("ID: %s", material.id.c_str());
                        double minimumNm = material.wavelengthDomain.minimumMetres * 1e9;
                        double maximumNm = material.wavelengthDomain.maximumMetres * 1e9;
                        if (ImGui::InputDouble("Minimum wavelength (nm)", &minimumNm, 1.0, 10.0, "%.9g")) {
                            material.wavelengthDomain.minimumMetres = minimumNm * 1e-9;
                            realLensDirty_ = true;
                        }
                        if (ImGui::InputDouble("Maximum wavelength (nm)", &maximumNm, 1.0, 10.0, "%.9g")) {
                            material.wavelengthDomain.maximumMetres = maximumNm * 1e-9;
                            realLensDirty_ = true;
                        }
                        std::visit([&](auto& model) {
                            using Model = std::decay_t<decltype(model)>;
                            if constexpr (std::is_same_v<Model, optics::material::ConstantIndexModel>) {
                                ImGui::TextUnformatted("Model: constant index");
                                if (ImGui::InputDouble(
                                        "Refractive index", &model.refractiveIndex,
                                        0.001, 0.01, "%.12g")) {
                                    realLensDirty_ = true;
                                }
                            } else if constexpr (std::is_same_v<Model, optics::material::CauchyModelSi>) {
                                ImGui::TextUnformatted("Model: SI Cauchy");
                                realLensDirty_ = ImGui::InputDouble("A", &model.aDimensionless, 0.001, 0.01, "%.12g") || realLensDirty_;
                                realLensDirty_ = ImGui::InputDouble("B (m^2)", &model.bSquareMetres, 0.0, 0.0, "%.12e") || realLensDirty_;
                                realLensDirty_ = ImGui::InputDouble("C (m^4)", &model.cFourthMetres, 0.0, 0.0, "%.12e") || realLensDirty_;
                            } else {
                                ImGui::TextUnformatted("Model: SI Sellmeier");
                                for (std::size_t termIndex = 0; termIndex < model.terms.size(); ++termIndex) {
                                    ImGui::PushID(static_cast<int>(termIndex));
                                    ImGui::Text("Term %zu", termIndex);
                                    realLensDirty_ = ImGui::InputDouble("B", &model.terms[termIndex].bDimensionless, 0.0, 0.0, "%.12g") || realLensDirty_;
                                    realLensDirty_ = ImGui::InputDouble("C (m^2)", &model.terms[termIndex].cSquareMetres, 0.0, 0.0, "%.12e") || realLensDirty_;
                                    ImGui::PopID();
                                }
                            }
                        }, material.dispersion);
                        ImGui::TreePop();
                    }
                    ImGui::PopID();
                }
                if (ImGui::Button("Add constant-index material")) {
                    const std::string id = "material_"
                        + std::to_string(realLensConfig_.prescription.materials.size());
                    realLensConfig_.prescription.materials.push_back({
                        .id = id,
                        .displayName = id,
                        .wavelengthDomain = {.minimumMetres = 380e-9, .maximumMetres = 780e-9},
                        .dispersion = optics::material::ConstantIndexModel {.refractiveIndex = 1.5},
                    });
                    realLensDirty_ = true;
                }
            }
            ImGui::EndTabItem();
        }

        if (ImGui::BeginTabItem("Ray / Spot Analysis")) {
            if (realLensResult_) {
                const float available = ImGui::GetContentRegionAvail().x;
                if (ImGui::BeginTable("real_lens_plots", 2, ImGuiTableFlags_SizingStretchSame)) {
                    ImGui::TableNextColumn();
                    drawRealLensSystemPlot(
                        realLensConfig_, *realLensResult_, ImVec2(available * 0.60F, 280.0F));
                    ImGui::TableNextColumn();
                    drawRealLensSpotPlot(
                        realLensResult_->spotDiagram, ImVec2(available * 0.38F, 280.0F));
                    ImGui::EndTable();
                }

                const auto& spot = realLensResult_->spotDiagram;
                ImGui::Text(
                    "Accepted: %zu | Rejected: %zu | RMS: %.3f um | Geometric radius: %.3f um",
                    spot.samples.size(), spot.rejectedRays.size(),
                    spot.statistics.rmsRadiusMetres * 1e6,
                    spot.statistics.geometricRadiusMetres * 1e6);
                if (ImGui::BeginTable(
                        "real_lens_field_stats", 5,
                        ImGuiTableFlags_Borders | ImGuiTableFlags_RowBg)) {
                    ImGui::TableSetupColumn("Field");
                    ImGui::TableSetupColumn("Samples");
                    ImGui::TableSetupColumn("Centroid X (mm)");
                    ImGui::TableSetupColumn("Centroid Y (mm)");
                    ImGui::TableSetupColumn("RMS (um)");
                    ImGui::TableHeadersRow();
                    for (const auto& group : spot.fieldGroups) {
                        ImGui::TableNextRow();
                        ImGui::TableNextColumn();
                        ImGui::TextUnformatted(group.fieldId.c_str());
                        ImGui::TableNextColumn();
                        ImGui::Text("%zu", group.sampleIndices.size());
                        ImGui::TableNextColumn();
                        ImGui::Text("%.6f", group.statistics.centroidXMetres * 1e3);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.6f", group.statistics.centroidYMetres * 1e3);
                        ImGui::TableNextColumn();
                        ImGui::Text("%.3f", group.statistics.rmsRadiusMetres * 1e6);
                    }
                    ImGui::EndTable();
                }

                ImGui::SeparatorText("Longitudinal chromatic focus");
                for (const auto& wavelength : realLensResult_->chromaticFocus.wavelengthResults) {
                    ImGui::BulletText(
                        "%.3f nm: local focus offset %.6f mm, RMS %.3f um (%zu rays)",
                        wavelength.vacuumWavelengthMetres * 1e9,
                        wavelength.focus.planeZMetres * 1e3,
                        wavelength.focus.rmsRadiusMetres * 1e6,
                        wavelength.focus.rayCount);
                }
                ImGui::Text(
                    "Total longitudinal focal shift: %.3f um",
                    realLensResult_->chromaticFocus.focalShiftMetres * 1e6);
            } else {
                ImGui::TextDisabled("No successful real-lens analysis is available.");
            }

            ImGui::SeparatorText("Model scope / limitations");
            ImGui::BulletText("Sequential geometric rays; CPU double precision is the correctness path.");
            ImGui::BulletText("Scalar phase index only: no Fresnel power splitting, coatings, polarization, or absorption.");
            ImGui::BulletText("Spot coordinates are physical image-plane metres; colours identify vacuum wavelength.");
            ImGui::BulletText("Field and wavelength failures remain explicit; rejected rays are never dropped silently.");
            ImGui::EndTabItem();
        }
        ImGui::EndTabBar();
    }

    if (activeLensPrescriptionAsset_.has_value()
        && activeLensPrescriptionAsset_->prescription
            != realLensConfig_.prescription) {
        activeLensPrescriptionAsset_.reset();
        realLensStatusMessage_
            = "Prescription edited: save it under a new immutable ID and reload it before binding it to the Bench";
    }

    if (!realLensErrorMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(1.0F, 0.3F, 0.3F, 1.0F), "%s", realLensErrorMessage_.c_str());
    } else if (!realLensStatusMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(
            ImVec4(0.4F, 0.9F, 0.5F, 1.0F), "%s", realLensStatusMessage_.c_str());
    }
    ImGui::End();
}

void Application::drawSlmInterferencePanel() {
    ImGui::Begin(docking::DockLayoutConfig::kSlmInterferenceWindowName);
    ImGui::TextDisabled(
        "Laser -> pixelated SLM -> ideal Fourier lens -> angular probe + reference beam");

    if (ImGui::CollapsingHeader("Experiment project", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (slmProjectProvenance_.originKind
            == project::ProjectOriginKind::LessonTemplate) {
            ImGui::Text(
                "Origin: lesson template %s v%d",
                slmProjectProvenance_.sourceId.c_str(),
                slmProjectProvenance_.sourceVersion);
        } else {
            ImGui::TextDisabled("Origin: user project");
        }
        ImGui::Text("Project: %s", slmProjectName_.c_str());
        ImGui::InputText(
            "Experiment JSON path",
            slmProjectPathBuffer_,
            sizeof(slmProjectPathBuffer_));
        if (ImGui::Button("Load experiment")) {
            loadSlmExperimentProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save draft experiment")) {
            saveSlmExperimentProject();
        }
        ImGui::TextDisabled(
            "Format v2 with provenance; v1 migrates as a user project. Load remains draft-only.");
    }

    if (ImGui::CollapsingHeader("Measured response LUT", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::InputText(
            "LUT JSON path",
            slmCalibrationPathBuffer_,
            sizeof(slmCalibrationPathBuffer_));
        if (ImGui::Button("Load measured LUT")) {
            loadSlmCalibration();
        }
        ImGui::SameLine();
        if (ImGui::Button("Export loaded LUT")) {
            saveSlmCalibration();
        }
        ImGui::SameLine();
        if (ImGui::Button("Clear LUT")) {
            slmInterferenceUiState_.clearCalibration();
            recordLessonEdit();
            slmInterferenceStatusMessage_ = "Measured LUT cleared from the draft configuration";
            slmInterferenceErrorMessage_.clear();
        }
        ImGui::TextWrapped(
            "Draft provenance: %s",
            slmInterferenceUiState_.draftCalibrationSource().c_str());
        const auto& calibration = slmInterferenceUiState_.draftConfig().calibratedResponse;
        if (calibration.has_value()) {
            const auto& curves = calibration->wavelengths();
            ImGui::Text(
                "%zu curve(s), %.3f-%.3f nm; phase is imported explicitly unwrapped",
                curves.size(),
                curves.front().vacuumWavelengthMetres * 1e9,
                curves.back().vacuumWavelengthMetres * 1e9);
        }
    }

    auto draft = slmInterferenceUiState_.draftConfig();
    bool physicsEdited = false;
    const auto inputMicrometres = [&physicsEdited](const char* label, double& metres) {
        double micrometres = metres * 1e6;
        if (ImGui::InputDouble(
                label, &micrometres, 0.1, 1.0, "%.6g um", ImGuiInputTextFlags_CharsScientific)) {
            metres = micrometres * 1e-6;
            physicsEdited = true;
        }
    };
    const auto inputMillimetres = [&physicsEdited](const char* label, double& metres) {
        double millimetres = metres * 1e3;
        if (ImGui::InputDouble(
                label, &millimetres, 0.1, 1.0, "%.6g mm", ImGuiInputTextFlags_CharsScientific)) {
            metres = millimetres * 1e-3;
            physicsEdited = true;
        }
    };

    if (ImGui::CollapsingHeader("SLM and sampling", ImGuiTreeNodeFlags_DefaultOpen)) {
        int responseModel = static_cast<int>(draft.deviceResponseModel);
        constexpr std::array<const char*, 3> responseNames {
            "Ideal scalar", "Measured complex LUT", "LCD polarizer teaching model"};
        if (ImGui::Combo(
                "Device response",
                &responseModel,
                responseNames.data(),
                static_cast<int>(responseNames.size()))) {
            const auto selected = static_cast<slmexperiment::SlmDeviceResponseModel>(responseModel);
            if (selected == slmexperiment::SlmDeviceResponseModel::CalibratedLut
                && !draft.calibratedResponse.has_value()) {
                slmInterferenceErrorMessage_ =
                    "Load a measured response LUT before selecting calibrated mode";
                slmInterferenceStatusMessage_.clear();
            } else {
                draft.deviceResponseModel = selected;
                physicsEdited = true;
            }
        }

        constexpr std::array<int, 3> resolutions {64, 128, 256};
        constexpr std::array<const char*, 3> resolutionNames {"64 x 64", "128 x 128", "256 x 256"};
        int resolutionIndex = 1;
        for (std::size_t index = 0; index < resolutions.size(); ++index) {
            if (draft.fieldWidth == static_cast<std::size_t>(resolutions[index])
                && draft.fieldHeight == static_cast<std::size_t>(resolutions[index])) {
                resolutionIndex = static_cast<int>(index);
            }
        }
        if (ImGui::Combo(
                "Field grid",
                &resolutionIndex,
                resolutionNames.data(),
                static_cast<int>(resolutionNames.size()))) {
            draft.fieldWidth = static_cast<std::size_t>(resolutions[static_cast<std::size_t>(resolutionIndex)]);
            draft.fieldHeight = draft.fieldWidth;
            physicsEdited = true;
        }
        inputMicrometres("Field pitch X", draft.fieldPitchXMetres);
        inputMicrometres("Field pitch Y", draft.fieldPitchYMetres);
        inputMillimetres("Fourier lens focal length", draft.lensFocalLengthMetres);
        inputMicrometres("SLM pixel pitch X", draft.slm.pixelPitchXMetres);
        inputMicrometres("SLM pixel pitch Y", draft.slm.pixelPitchYMetres);
        if (ImGui::InputDouble("Fill factor X", &draft.slm.fillFactorX, 0.01, 0.05, "%.4f")) {
            physicsEdited = true;
        }
        if (ImGui::InputDouble("Fill factor Y", &draft.slm.fillFactorY, 0.01, 0.05, "%.4f")) {
            physicsEdited = true;
        }
        int bitDepth = static_cast<int>(draft.slm.bitDepth);
        if (ImGui::InputInt("Command bit depth (0 = continuous)", &bitDepth)) {
            draft.slm.bitDepth = static_cast<unsigned int>(std::max(bitDepth, 0));
            physicsEdited = true;
        }
        if (draft.deviceResponseModel == slmexperiment::SlmDeviceResponseModel::Ideal) {
            int modulationMode = static_cast<int>(draft.slm.mode);
            constexpr std::array<const char*, 2> modulationNames {"Amplitude", "Phase"};
            if (ImGui::Combo(
                    "Ideal modulation",
                    &modulationMode,
                    modulationNames.data(),
                    static_cast<int>(modulationNames.size()))) {
                draft.slm.mode = static_cast<optics::slm::ModulationMode>(modulationMode);
                physicsEdited = true;
            }
            if (draft.slm.mode == optics::slm::ModulationMode::Phase
                && ImGui::InputDouble(
                    "Phase range (rad)", &draft.slm.phaseRangeRadians, 0.1, 1.0, "%.8g")) {
                physicsEdited = true;
            }
        }

        int selectedColumn = static_cast<int>(draft.selectedPixelColumn);
        int selectedRow = static_cast<int>(draft.selectedPixelRow);
        if (ImGui::InputInt("Selected pixel column", &selectedColumn)) {
            draft.selectedPixelColumn = static_cast<std::size_t>(std::max(selectedColumn, 0));
            physicsEdited = true;
        }
        if (ImGui::InputInt("Selected pixel row", &selectedRow)) {
            draft.selectedPixelRow = static_cast<std::size_t>(std::max(selectedRow, 0));
            physicsEdited = true;
        }
        ImGui::Text(
            "SLM grid: %zu x %zu | command: repeatable X blaze",
            draft.slm.pixelColumns,
            draft.slm.pixelRows);
    }

    if (draft.deviceResponseModel == slmexperiment::SlmDeviceResponseModel::LcdTeaching
        && ImGui::CollapsingHeader("LCD teaching assumptions", ImGuiTreeNodeFlags_DefaultOpen)) {
        const auto inputDegrees = [&physicsEdited](const char* label, double& radians) {
            double degrees = radians * 180.0 / std::numbers::pi;
            if (ImGui::InputDouble(label, &degrees, 1.0, 5.0, "%.4f deg")) {
                radians = degrees * std::numbers::pi / 180.0;
                physicsEdited = true;
            }
        };
        inputDegrees("Input polarizer", draft.lcdTeaching.inputPolarizerAngleRadians);
        inputDegrees("Analyzer", draft.lcdTeaching.analyzerAngleRadians);
        inputDegrees("LC fast axis", draft.lcdTeaching.liquidCrystalFastAxisAngleRadians);
        if (ImGui::InputDouble(
                "Retardance at command 0",
                &draft.lcdTeaching.zeroCommandRetardanceRadians,
                0.1,
                1.0,
                "%.8g rad")) {
            physicsEdited = true;
        }
        if (ImGui::InputDouble(
                "Retardance at command 1",
                &draft.lcdTeaching.fullCommandRetardanceRadians,
                0.1,
                1.0,
                "%.8g rad")) {
            physicsEdited = true;
        }
        int pattern = static_cast<int>(draft.lcdTeaching.colorFilterPattern);
        constexpr std::array<const char*, 4> patternNames {
            "Monochrome", "Vertical RGB stripes", "Horizontal RGB stripes", "RGGB Bayer"};
        if (ImGui::Combo(
                "Color-filter layout",
                &pattern,
                patternNames.data(),
                static_cast<int>(patternNames.size()))) {
            draft.lcdTeaching.colorFilterPattern
                = static_cast<optics::slm::LcdColorFilterPattern>(pattern);
            physicsEdited = true;
        }
        ImGui::TextDisabled(
            "Ideal retarder between ideal polarizers; output keeps only the analyzer-projected scalar field.");
    }

    if (ImGui::CollapsingHeader("Reference beam and coherence", ImGuiTreeNodeFlags_DefaultOpen)) {
        double referenceAmplitude = std::abs(draft.referenceBeam.amplitude);
        if (ImGui::InputDouble("Reference field amplitude", &referenceAmplitude, 0.05, 0.2, "%.6g")) {
            draft.referenceBeam.amplitude = std::polar(
                referenceAmplitude,
                std::arg(draft.referenceBeam.amplitude));
            physicsEdited = true;
        }
        if (ImGui::InputDouble(
                "Reference direction cosine X",
                &draft.referenceBeam.directionCosineX,
                0.001,
                0.01,
                "%.8g")) {
            physicsEdited = true;
        }
        if (ImGui::InputDouble(
                "Reference direction cosine Y",
                &draft.referenceBeam.directionCosineY,
                0.001,
                0.01,
                "%.8g")) {
            physicsEdited = true;
        }
        double coherenceMagnitude = std::abs(draft.mutualCoherence.zeroDelayDegree);
        double coherencePhase = std::arg(draft.mutualCoherence.zeroDelayDegree);
        if (ImGui::InputDouble("|gamma(0)|", &coherenceMagnitude, 0.01, 0.1, "%.6g")) {
            draft.mutualCoherence.zeroDelayDegree
                = std::polar(coherenceMagnitude, coherencePhase);
            physicsEdited = true;
        }
        if (ImGui::InputDouble("arg gamma(0)", &coherencePhase, 0.05, 0.2, "%.6g rad")) {
            draft.mutualCoherence.zeroDelayDegree
                = std::polar(coherenceMagnitude, coherencePhase);
            physicsEdited = true;
        }
        inputMicrometres(
            "Optical path difference", draft.mutualCoherence.opticalPathDifferenceMetres);
        bool finiteCoherence = std::isfinite(draft.mutualCoherence.coherenceLengthMetres);
        if (ImGui::Checkbox("Finite coherence length", &finiteCoherence)) {
            draft.mutualCoherence.coherenceLengthMetres = finiteCoherence
                ? 1e-3
                : std::numeric_limits<double>::infinity();
            physicsEdited = true;
        }
        if (finiteCoherence) {
            inputMillimetres(
                "1/e coherence length", draft.mutualCoherence.coherenceLengthMetres);
        }
        int envelope = static_cast<int>(draft.mutualCoherence.envelope);
        constexpr std::array<const char*, 2> envelopeNames {"Gaussian", "Exponential"};
        if (ImGui::Combo(
                "Coherence envelope",
                &envelope,
                envelopeNames.data(),
                static_cast<int>(envelopeNames.size()))) {
            draft.mutualCoherence.envelope
                = static_cast<optics::wave::CoherenceEnvelope>(envelope);
            physicsEdited = true;
        }
    }

    if (physicsEdited) {
        slmInterferenceUiState_.setDraftConfig(draft);
        recordLessonEdit();
    }
    if (slmInterferenceUiState_.isDirty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
            "Draft differs from the displayed result - press Apply to recompute");
    }
    if (ImGui::Button("Apply SLM Experiment")) {
        slmInterferenceUiState_.apply();
        slmInterferenceResult_.reset();
        slmInterferenceStatusMessage_ = "SLM experiment recompute queued";
        slmInterferenceErrorMessage_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset teaching defaults")) {
        slmInterferenceUiState_.setDraftConfig(
            slmexperiment::makeDefaultSlmInterferenceExperimentConfig());
        slmInterferenceUiState_.clearCalibration();
        recordLessonEdit();
        slmInterferenceStatusMessage_ = "Teaching defaults restored in draft; press Apply";
        slmInterferenceErrorMessage_.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("FFT work runs only on Apply.");

    if (!slmInterferenceErrorMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s",
            slmInterferenceErrorMessage_.c_str());
    } else if (!slmInterferenceStatusMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(0.4F, 0.9F, 0.5F, 1.0F),
            "%s",
            slmInterferenceStatusMessage_.c_str());
    }

    ImGui::SeparatorText("Result");
    if (slmInterferenceResult_ && !slmInterferenceResult_->wavelengths.empty()) {
        const std::size_t selectedIndex = std::min(
            slmInterferenceUiState_.displayedWavelengthIndex(),
            slmInterferenceResult_->wavelengths.size() - 1U);
        const auto& selectedResult = slmInterferenceResult_->wavelengths[selectedIndex];
        char wavelengthLabel[64];
        std::snprintf(
            wavelengthLabel,
            sizeof(wavelengthLabel),
            "%.3f nm",
            selectedResult.vacuumWavelengthMetres * 1e9);
        if (ImGui::BeginCombo("Displayed wavelength", wavelengthLabel)) {
            for (std::size_t index = 0;
                 index < slmInterferenceResult_->wavelengths.size();
                 ++index) {
                char itemLabel[64];
                std::snprintf(
                    itemLabel,
                    sizeof(itemLabel),
                    "%.3f nm",
                    slmInterferenceResult_->wavelengths[index].vacuumWavelengthMetres * 1e9);
                const bool selected = index == selectedIndex;
                if (ImGui::Selectable(itemLabel, selected)) {
                    slmInterferenceUiState_.setDisplayedWavelengthIndex(index);
                }
                if (selected) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
        int displayPlane = static_cast<int>(slmInterferenceUiState_.displayPlane());
        constexpr std::array<const char*, 3> displayNames {
            "SLM/reference interference", "Angular intensity", "Selected-pixel angular PSF"};
        if (ImGui::Combo(
                "Displayed plane",
                &displayPlane,
                displayNames.data(),
                static_cast<int>(displayNames.size()))) {
            slmInterferenceUiState_.setDisplayPlane(
                static_cast<slmui::DisplayPlane>(displayPlane));
        }

        const double intensitySum = selectedResult.interference.maximumIntensity
            + selectedResult.interference.minimumIntensity;
        const double visibility = intensitySum > 0.0
            ? (selectedResult.interference.maximumIntensity
                - selectedResult.interference.minimumIntensity) / intensitySum
            : 0.0;
        ImGui::Text(
            "Interference min/max %.6g / %.6g | visibility %.6g | |gamma| %.6g",
            selectedResult.interference.minimumIntensity,
            selectedResult.interference.maximumIntensity,
            visibility,
            std::abs(selectedResult.interference.degreeOfCoherence));
        const auto& mapping = selectedResult.selectedPixelMapping;
        ImGui::Text(
            "Selected pixel direction cosine predicted (%.6g, %.6g), measured (%.6g, %.6g)",
            mapping.sampledPredictedDirectionCosineX,
            mapping.sampledPredictedDirectionCosineY,
            mapping.measuredDirectionCosineX,
            mapping.measuredDirectionCosineY);
        ImGui::Text(
            "Active/dead/outside/quantized samples: %zu / %zu / %zu / %zu",
            selectedResult.modulationDiagnostics.modulatedSampleCount,
            selectedResult.modulationDiagnostics.deadSpaceSampleCount,
            selectedResult.modulationDiagnostics.outsideActiveAreaSampleCount,
            selectedResult.modulationDiagnostics.quantizedSampleCount);
        ImGui::TextWrapped(
            "Applied calibration provenance: %s",
            slmInterferenceUiState_.appliedCalibrationSource().c_str());

        if (slmInterferenceTexture_ && slmInterferenceTexture_->isValid()) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const auto layout = waveui::fitDetectorImage(
                available.x,
                std::max(220.0F, available.y - 80.0F),
                static_cast<std::size_t>(slmInterferenceTexture_->width()),
                static_cast<std::size_t>(slmInterferenceTexture_->height()));
            if (layout.width > 0.0F && layout.height > 0.0F) {
                ImGui::Image(
                    toImTextureID(slmInterferenceTexture_->handle()),
                    ImVec2(layout.width, layout.height),
                    ImVec2(0.0F, 1.0F),
                    ImVec2(1.0F, 0.0F));
            }
        }
    } else {
        ImGui::TextDisabled("No SLM experiment result is available.");
    }

    ImGui::SeparatorText("Model boundary");
    ImGui::TextWrapped(
        "CPU double-precision scalar reference. The measured LUT is evidence, not a GPU/device workaround. "
        "The LCD path is an analyzer-projected teaching approximation, not a full Jones-field solver.");
    ImGui::End();
}

void Application::drawHolographyPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kHolographyWindowName);
    ImGui::TextDisabled(
        "Complex object -> H1 conjugate replay -> positioned H2 -> RGB image replay");

    if (ImGui::CollapsingHeader(
            "Experiment project", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Project: %s", holographyProjectName_.c_str());
        ImGui::TextDisabled(
            "Origin: %s | source: %s | version: %d",
            std::string(project::projectOriginKindName(
                holographyProjectProvenance_.originKind)).c_str(),
            holographyProjectProvenance_.sourceId.empty()
                ? "(none)"
                : holographyProjectProvenance_.sourceId.c_str(),
            holographyProjectProvenance_.sourceVersion);
        ImGui::InputText(
            "Holography JSON path",
            holographyProjectPathBuffer_,
            sizeof(holographyProjectPathBuffer_));
        if (ImGui::Button("Load holography experiment")) {
            loadHolographyProject();
        }
        ImGui::SameLine();
        if (ImGui::Button("Save holography draft")) {
            saveHolographyProject();
        }
        ImGui::TextDisabled(
            "Strict format-v3 document; v1/v2 migrate to user provenance. "
            "Loading replaces draft state only.");
    }

    auto draft = holographyUiState_.draftConfig();
    bool physicsEdited = false;
    const auto inputMicrometres = [&physicsEdited](
                                       const char* label, double& metres) {
        double micrometres = metres * 1e6;
        if (ImGui::InputDouble(
                label,
                &micrometres,
                0.1,
                1.0,
                "%.7g um",
                ImGuiInputTextFlags_CharsScientific)) {
            metres = micrometres * 1e-6;
            physicsEdited = true;
        }
    };
    const auto inputNanometres = [&physicsEdited](
                                      const char* label, double& metres) {
        double nanometres = metres * 1e9;
        if (ImGui::InputDouble(
                label,
                &nanometres,
                0.1,
                1.0,
                "%.7g nm",
                ImGuiInputTextFlags_CharsScientific)) {
            metres = nanometres * 1e-9;
            physicsEdited = true;
        }
    };
    const auto inputMillimetres = [&physicsEdited](
                                       const char* label, double& metres) {
        double millimetres = metres * 1e3;
        if (ImGui::InputDouble(
                label,
                &millimetres,
                0.05,
                0.5,
                "%.7g mm",
                ImGuiInputTextFlags_CharsScientific)) {
            metres = millimetres * 1e-3;
            physicsEdited = true;
        }
    };
    const auto inputDegrees = [&physicsEdited](
                                  const char* label, double& radians) {
        double degrees = radians * 180.0 / std::numbers::pi_v<double>;
        if (ImGui::InputDouble(
                label,
                &degrees,
                0.05,
                0.5,
                "%.7g deg",
                ImGuiInputTextFlags_CharsScientific)) {
            radians = degrees * std::numbers::pi_v<double> / 180.0;
            physicsEdited = true;
        }
    };

    if (ImGui::CollapsingHeader(
            "Grid and RGB media", ImGuiTreeNodeFlags_DefaultOpen)) {
        constexpr std::array<int, 3> resolutions {32, 64, 128};
        constexpr std::array<const char*, 3> resolutionNames {
            "32 x 32", "64 x 64", "128 x 128"};
        int resolutionIndex = 0;
        for (std::size_t index = 0; index < resolutions.size(); ++index) {
            if (draft.fieldWidth
                    == static_cast<std::size_t>(resolutions[index])
                && draft.fieldHeight
                    == static_cast<std::size_t>(resolutions[index])) {
                resolutionIndex = static_cast<int>(index);
            }
        }
        if (ImGui::Combo(
                "Field grid",
                &resolutionIndex,
                resolutionNames.data(),
                static_cast<int>(resolutionNames.size()))) {
            draft.fieldWidth = static_cast<std::size_t>(
                resolutions[static_cast<std::size_t>(resolutionIndex)]);
            draft.fieldHeight = draft.fieldWidth;
            physicsEdited = true;
        }
        inputMicrometres("Field pitch X", draft.fieldPitchXMetres);
        inputMicrometres("Field pitch Y", draft.fieldPitchYMetres);
        constexpr std::array<const char*, 3> channelNames {"Red", "Green", "Blue"};
        for (std::size_t channel = 0; channel < channelNames.size(); ++channel) {
            ImGui::PushID(static_cast<int>(channel));
            ImGui::SeparatorText(channelNames[channel]);
            inputNanometres("Vacuum wavelength", draft.vacuumWavelengthsMetres[channel]);
            if (ImGui::InputDouble(
                    "Refractive index",
                    &draft.refractiveIndices[channel],
                    0.001,
                    0.01,
                    "%.8g")) {
                physicsEdited = true;
            }
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader("Complex Gaussian object")) {
        for (std::size_t index = 0; index < draft.objectFeatures.size(); ++index) {
            auto& feature = draft.objectFeatures[index];
            ImGui::PushID(static_cast<int>(index));
            const char* featureLabel = index == 0U ? "Feature A" : "Feature B";
            ImGui::SeparatorText(featureLabel);
            if (ImGui::InputDouble(
                    "Amplitude", &feature.amplitude, 0.01, 0.1, "%.7g")) {
                physicsEdited = true;
            }
            if (ImGui::InputDouble(
                    "Phase", &feature.phaseRadians, 0.05, 0.2, "%.7g rad")) {
                physicsEdited = true;
            }
            inputMicrometres("Centre X", feature.centerXMetres);
            inputMicrometres("Centre Y", feature.centerYMetres);
            inputMicrometres("Sigma X", feature.sigmaXMetres);
            inputMicrometres("Sigma Y", feature.sigmaYMetres);
            ImGui::PopID();
        }
    }

    if (ImGui::CollapsingHeader(
            "H1 / H2 recording geometry", ImGuiTreeNodeFlags_DefaultOpen)) {
        inputMillimetres(
            "Object to H1", draft.transfer.h1.objectToPlateDistanceMetres);
        inputMillimetres("H2 axial position", draft.transfer.h2AxialPositionMetres);
        inputMicrometres(
            "Transplane tolerance", draft.transfer.transplaneToleranceMetres);
        ImGui::TextDisabled(
            "The signed image distance is z(image) - z(H2); zero is an explicit transplane case.");

        const auto editReference = [&physicsEdited](
                                       const char* label,
                                       optics::wave::PlaneWaveParameters& reference) {
            if (!ImGui::TreeNode(label)) {
                return;
            }
            double magnitude = std::abs(reference.amplitude);
            double phase = std::arg(reference.amplitude);
            if (ImGui::InputDouble(
                    "Amplitude magnitude", &magnitude, 0.01, 0.1, "%.7g")) {
                reference.amplitude = std::polar(magnitude, phase);
                physicsEdited = true;
            }
            if (ImGui::InputDouble(
                    "Amplitude phase", &phase, 0.05, 0.2, "%.7g rad")) {
                reference.amplitude = std::polar(magnitude, phase);
                physicsEdited = true;
            }
            if (ImGui::InputDouble(
                    "Direction cosine X",
                    &reference.directionCosineX,
                    0.001,
                    0.01,
                    "%.8g")) {
                physicsEdited = true;
            }
            if (ImGui::InputDouble(
                    "Direction cosine Y",
                    &reference.directionCosineY,
                    0.001,
                    0.01,
                    "%.8g")) {
                physicsEdited = true;
            }
            ImGui::TreePop();
        };
        editReference("H1 reference", draft.transfer.h1.recordingReference);
        editReference("H2 reference", draft.transfer.h2RecordingReference);

        const auto editResponse = [&physicsEdited](
                                      const char* label,
                                      optics::holography::ThinHologramResponseParameters& response) {
            if (!ImGui::TreeNode(label)) {
                return;
            }
            if (ImGui::InputDouble(
                    "Amplitude bias", &response.amplitudeBias, 0.01, 0.1, "%.7g")) {
                physicsEdited = true;
            }
            if (ImGui::InputDouble(
                    "Exposure gain",
                    &response.intensityToAmplitudeGain,
                    0.01,
                    0.1,
                    "%.7g")) {
                physicsEdited = true;
            }
            ImGui::TextDisabled(
                "Transmission bounds %.3g .. %.3g (fixed teaching medium)",
                response.minimumAmplitudeTransmission,
                response.maximumAmplitudeTransmission);
            ImGui::TreePop();
        };
        editResponse("H1 thin-plate response", draft.transfer.h1.response);
        editResponse("H2 thin-plate response", draft.transfer.h2Response);
    }

    if (ImGui::CollapsingHeader(
            "Separate volume / Kogelnik model", ImGuiTreeNodeFlags_DefaultOpen)) {
        auto& volume = draft.volume;
        int geometry = static_cast<int>(volume.geometry);
        constexpr std::array<const char*, 2> geometryNames {
            "Transmission", "Reflection"};
        if (ImGui::Combo(
                "Volume geometry",
                &geometry,
                geometryNames.data(),
                static_cast<int>(geometryNames.size()))) {
            volume.geometry
                = static_cast<optics::holography::VolumeHologramGeometry>(geometry);
            if (volume.geometry
                    == optics::holography::VolumeHologramGeometry::Transmission
                && volume.recordingBraggAngleInMediumRadians == 0.0) {
                constexpr double kTeachingAngleRadians
                    = 20.0 * std::numbers::pi_v<double> / 180.0;
                volume.recordingBraggAngleInMediumRadians
                    = kTeachingAngleRadians;
                volume.replayAngleInMediumRadians = kTeachingAngleRadians;
            }
            physicsEdited = true;
        }
        inputMicrometres("Recorded thickness", volume.recordedThicknessMetres);
        if (ImGui::InputDouble(
                "Average refractive index",
                &volume.averageRefractiveIndex,
                0.001,
                0.01,
                "%.8g")) {
            physicsEdited = true;
        }
        if (ImGui::InputDouble(
                "Index modulation",
                &volume.refractiveIndexModulation,
                0.0001,
                0.001,
                "%.8g")) {
            physicsEdited = true;
        }
        inputNanometres(
            "Volume recording wavelength",
            volume.recordingVacuumWavelengthMetres);
        inputNanometres(
            "Volume replay wavelength", volume.replayVacuumWavelengthMetres);
        inputDegrees(
            "Recording Bragg angle in medium",
            volume.recordingBraggAngleInMediumRadians);
        inputDegrees(
            "Replay angle in medium", volume.replayAngleInMediumRadians);
        double shrinkagePercent
            = volume.isotropicLinearShrinkageFraction * 100.0;
        if (ImGui::InputDouble(
                "Isotropic linear shrinkage",
                &shrinkagePercent,
                0.01,
                0.1,
                "%.7g %%")) {
            volume.isotropicLinearShrinkageFraction
                = shrinkagePercent * 0.01;
            physicsEdited = true;
        }
        ImGui::TextWrapped(
            "Independent lossless scalar-TE sinusoidal phase grating. It does not reuse H1/H2 thin masks; absorption, Fresnel loss, polarization, multiplexing, and calibrated processing remain out of scope.");
    }

    if (physicsEdited) {
        holographyUiState_.setDraftConfig(draft);
    }
    if (holographyUiState_.isDirty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
            "Draft differs from the displayed result - press Apply to recompute");
    }
    if (ImGui::Button("Apply Holography Experiment")) {
        try {
            holographyUiState_.apply();
            holographyStatusMessage_ = "Holography recompute queued";
            holographyErrorMessage_.clear();
        } catch (const std::exception& ex) {
            holographyErrorMessage_ = "Holography draft is invalid: "
                + std::string(ex.what());
            holographyStatusMessage_.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset holography defaults")) {
        holographyUiState_.setDraftConfig(
            holographylab::makeDefaultHolographyLabConfig());
        holographyStatusMessage_ = "Holography defaults restored in draft; press Apply";
        holographyErrorMessage_.clear();
    }
    ImGui::SameLine();
    ImGui::TextDisabled("FFT work runs only on Apply.");

    if (!holographyErrorMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.35F, 0.35F, 1.0F),
            "%s",
            holographyErrorMessage_.c_str());
    } else if (!holographyStatusMessage_.empty()) {
        ImGui::TextColored(
            ImVec4(0.4F, 0.9F, 0.5F, 1.0F),
            "%s",
            holographyStatusMessage_.c_str());
    }

    ImGui::SeparatorText("Result and diagnostics");
    if (holographyResult_) {
        int channelIndex = static_cast<int>(holographyUiState_.displayedChannel());
        constexpr std::array<const char*, 3> channelNames {
            "Red", "Green", "Blue"};
        if (ImGui::Combo(
                "Displayed RGB channel",
                &channelIndex,
                channelNames.data(),
                static_cast<int>(channelNames.size()))) {
            holographyUiState_.setDisplayedChannel(
                static_cast<std::size_t>(channelIndex));
        }
        int displayPlane = static_cast<int>(holographyUiState_.displayPlane());
        constexpr std::array<const char*, 4> planeNames {
            "H1 exposure",
            "H1 isolated real image",
            "H2 exposure",
            "H2 isolated replay image"};
        if (ImGui::Combo(
                "Displayed plane",
                &displayPlane,
                planeNames.data(),
                static_cast<int>(planeNames.size()))) {
            holographyUiState_.setDisplayPlane(
                static_cast<holographyui::DisplayPlane>(displayPlane));
        }

        const auto& result = holographyResult_->rgbTransfer.channels[
            std::min(holographyUiState_.displayedChannel(), std::size_t {2})];
        const char* placement = "transplane";
        if (result.imagePlacement == holography::H2ImagePlacement::NegativeSide) {
            placement = "negative side";
        } else if (result.imagePlacement
                   == holography::H2ImagePlacement::PositiveSide) {
            placement = "positive side";
        }
        ImGui::Text(
            "H1 image z %.6g mm | H2 z %.6g mm | signed image distance %.6g mm (%s)",
            result.h1ImageAxialPositionMetres * 1e3,
            holographyUiState_.appliedConfig().transfer.h2AxialPositionMetres * 1e3,
            result.imageDistanceFromH2Metres * 1e3,
            placement);
        ImGui::Text(
            "H1 real-image normalized / peak errors: %.3e / %.3e",
            result.h1.realImageQuality.normalizedComplexL2Error,
            result.h1.realImageQuality.peakNormalizedMaximumComplexError);
        ImGui::Text(
            "H2 replay normalized / peak errors: %.3e / %.3e",
            result.h2ImageQuality.normalizedComplexL2Error,
            result.h2ImageQuality.peakNormalizedMaximumComplexError);
        const auto& h1Order = result.h1.conjugateRealImageOrderPlacement;
        const auto& h2Order = result.h2ReplayOrderPlacement;
        ImGui::Text(
            "H1 zero/twin separation: %.4g / %.4g mm | H2: %.4g / %.4g mm",
            h1Order.desiredToZeroOrderSeparationMetres * 1e3,
            h1Order.desiredToTwinOrderSeparationMetres * 1e3,
            h2Order.desiredToZeroOrderSeparationMetres * 1e3,
            h2Order.desiredToTwinOrderSeparationMetres * 1e3);
        ImGui::Text(
            "H1 zero sampled/propagating/in-window: %s / %s / %s",
            h1Order.zeroOrderCarrierSampled ? "yes" : "no",
            h1Order.zeroOrderCarrierPropagating ? "yes" : "no",
            h1Order.zeroOrderCentreInsidePeriodicWindow ? "yes" : "no");
        ImGui::Text(
            "H1 twin sampled/propagating/in-window: %s / %s / %s",
            h1Order.twinOrderCarrierSampled ? "yes" : "no",
            h1Order.twinOrderCarrierPropagating ? "yes" : "no",
            h1Order.twinOrderCentreInsidePeriodicWindow ? "yes" : "no");
        ImGui::Text(
            "H2 zero sampled/propagating/in-window: %s / %s / %s",
            h2Order.zeroOrderCarrierSampled ? "yes" : "no",
            h2Order.zeroOrderCarrierPropagating ? "yes" : "no",
            h2Order.zeroOrderCentreInsidePeriodicWindow ? "yes" : "no");
        ImGui::Text(
            "H2 twin sampled/propagating/in-window: %s / %s / %s",
            h2Order.twinOrderCarrierSampled ? "yes" : "no",
            h2Order.twinOrderCarrierPropagating ? "yes" : "no",
            h2Order.twinOrderCentreInsidePeriodicWindow ? "yes" : "no");

        const auto& volume = holographyResult_->volume;
        const auto volumeGeometry
            = holographyUiState_.appliedConfig().volume.geometry;
        ImGui::SeparatorText("Volume / Kogelnik result");
        ImGui::Text(
            "%s grating | replay thickness %.6g um | period %.6g -> %.6g um",
            volumeGeometry
                    == optics::holography::VolumeHologramGeometry::Transmission
                ? "Transmission"
                : "Reflection",
            volume.replayThicknessMetres * 1e6,
            volume.recordedGratingPeriodMetres * 1e6,
            volume.replayGratingPeriodMetres * 1e6);
        ImGui::Text(
            "Coupling nu %.7g | detuning xi %.7g | mismatch %.7g rad/m",
            volume.kogelnik.couplingStrength,
            volume.kogelnik.detuningParameter,
            volume.phaseMismatchRadiansPerMetre);
        if (volume.kogelnikEfficiencyEvaluated) {
            ImGui::Text(
                "Diffraction efficiency %.6f | exact-Bragg limit at this coupling %.6f",
                volume.kogelnik.diffractionEfficiency,
                volume.exactBraggEfficiencyAtReplayCoupling);
            ImGui::Text(
                "Diffracted internal angle %.6g deg | order propagating: yes",
                volume.diffractedInternalAngleRadians
                    * 180.0 / std::numbers::pi_v<double>);
        } else {
            ImGui::TextColored(
                ImVec4(1.0F, 0.6F, 0.25F, 1.0F),
                "Selected transmission order is non-propagating; no Kogelnik efficiency is assigned.");
        }

        if (holographyTexture_ && holographyTexture_->isValid()) {
            const ImVec2 available = ImGui::GetContentRegionAvail();
            const auto layout = waveui::fitDetectorImage(
                available.x,
                std::max(220.0F, available.y - 80.0F),
                static_cast<std::size_t>(holographyTexture_->width()),
                static_cast<std::size_t>(holographyTexture_->height()));
            if (layout.width > 0.0F && layout.height > 0.0F) {
                ImGui::Image(
                    toImTextureID(holographyTexture_->handle()),
                    ImVec2(layout.width, layout.height),
                    ImVec2(0.0F, 1.0F),
                    ImVec2(1.0F, 0.0F));
            }
        }
    } else {
        ImGui::TextDisabled("No holography experiment result is available.");
    }

    ImGui::SeparatorText("Model boundary");
    ImGui::TextWrapped(
        "CPU double-precision scalar, coherent, thin-transmission teaching model. "
        "The H2 plate here is not a reflection/Denisyuk volume hologram; volume coupling and Bragg selectivity use the separate Kogelnik model.");
    ImGui::End();
}

void Application::loadLessonTemplate(std::string_view lessonId) {
    if (lessonId == "reflection_refraction") {
        const auto lessonTemplate
            = lessons::loadReflectionRefractionLessonTemplate(
                lessonTemplateRoot(), "lesson_reflection_refraction");
        const auto result = reflection::evaluateReflectionRefraction(
            lessonTemplate.config);
        learnSession_.replaceReflectionConfig(lessonTemplate.config);
        reflectionRefractionConfig_ = lessonTemplate.config;
        reflectionRefractionResult_ = result;
        reflectionProjectProvenance_ = lessonTemplate.provenance;
        reflectionProjectName_ = lessonTemplate.name;
        reflectionErrorMessage_.clear();
        reflectionStatusMessage_ = "Reflection/refraction lesson loaded";
        recordLessonEdit();
        ImGui::SetWindowFocus(
            docking::DockLayoutConfig::kInspectorWindowName);
        return;
    }
    if (lessonId == "thin_lens") {
        const auto lessonTemplate = lessons::loadOpticalBenchLessonTemplate(
            lessonTemplateRoot(), "lesson_thin_lens");
        if (!applySceneProject(
                lessonTemplate.scene,
                tracerOptions_,
                lessonTemplate.provenance)) {
            throw std::runtime_error(
                errorMessage_.empty()
                    ? "application rejected the thin-lens template"
                    : errorMessage_);
        }
        selectedTarget_ = GizmoTarget::Screen;
        camera_.setPresetView(render::CameraPresetView::Perspective);
        return;
    }
    if (lessonId == "real_virtual_images") {
        const auto lessonTemplate = lessons::loadOpticalBenchLessonTemplate(
            lessonTemplateRoot(), "lesson_real_virtual_images");
        if (!applySceneProject(
                lessonTemplate.scene,
                tracerOptions_,
                lessonTemplate.provenance)) {
            throw std::runtime_error(
                errorMessage_.empty()
                    ? "application rejected the real/virtual template"
                    : errorMessage_);
        }
        lessonImageClassification_ = optics::scene::ImageNature::Real;
        camera_.setPresetView(render::CameraPresetView::Perspective);
        return;
    }
    if (lessonId == "diffraction") {
        const auto lessonTemplate = lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_diffraction");
        detectorUiState_.setDraftConfig(lessonTemplate.waveDetector);
        detectorUiState_.apply();
        detectorUiState_.setViewMode(field::FieldViewMode::DecibelIntensity);
        samplingDebuggerConfig_ = lessonTemplate.samplingDebugger;
        waveProjectProvenance_ = lessonTemplate.provenance;
        waveProjectName_ = lessonTemplate.name;
        detectorResult_.reset();
        samplingDebuggerResult_.reset();
        detectorErrorMessage_.clear();
        detectorStatusMessage_ = "Diffraction lesson recompute queued";
        recordLessonEdit();
        ImGui::SetWindowFocus(docking::DockLayoutConfig::kWaveDetectorWindowName);
        return;
    }
    if (lessonId == "fourier_plane"
        || lessonId == "spatial_filtering"
        || lessonId == "na_psf") {
        const auto lessonTemplate = lessons::loadWaveWorkbenchLessonTemplate(
            lessonTemplateRoot(), "lesson_" + std::string(lessonId));
        detectorUiState_.setDraftConfig(lessonTemplate.waveDetector);
        detectorUiState_.apply();
        detectorUiState_.setViewMode(field::FieldViewMode::DecibelIntensity);
        samplingDebuggerConfig_ = lessonTemplate.samplingDebugger;
        waveProjectProvenance_ = lessonTemplate.provenance;
        waveProjectName_ = lessonTemplate.name;
        detectorResult_.reset();
        samplingDebuggerResult_.reset();
        detectorErrorMessage_.clear();
        samplingDebuggerErrorMessage_.clear();
        detectorStatusMessage_ = "Fourier lesson source recompute queued";
        recordLessonEdit();
        if (lessonId == "fourier_plane") {
            lessonFourierPlaneIdentification_
                = lessons::FourierPlaneIdentification::ObjectPlane;
        } else if (lessonId == "spatial_filtering") {
            lessonSpatialFilteringEffect_
                = lessons::SpatialFilteringEffect::Sharper;
        } else {
            lessonPsfWidthChange_ = lessons::PsfWidthChange::Wider;
        }
        ImGui::SetWindowFocus(
            docking::DockLayoutConfig::kSamplingDebuggerWindowName);
        return;
    }
    if (lessonId == "coherence_interference") {
        const auto lessonTemplate = lessons::loadSlmLessonTemplate(
            lessonTemplateRoot(), "lesson_coherence_interference");
        slmInterferenceUiState_.replaceDraftProject(
            lessonTemplate.config, lessonTemplate.calibrationProvenance);
        slmProjectProvenance_ = lessonTemplate.provenance;
        slmProjectName_ = lessonTemplate.name;
        slmInterferenceUiState_.apply();
        slmInterferenceUiState_.setDisplayPlane(slmui::DisplayPlane::Interference);
        slmInterferenceResult_.reset();
        slmInterferenceErrorMessage_.clear();
        slmInterferenceStatusMessage_ = "Coherence lesson recompute queued";
        recordLessonEdit();
        lessonFringeVisibilityChange_
            = lessons::FringeVisibilityChange::Higher;
        ImGui::SetWindowFocus(
            docking::DockLayoutConfig::kSlmInterferenceWindowName);
        return;
    }
    if (lessonId == "holography"
        || lessonId == "h1_h2_advanced") {
        const auto lessonTemplate = lessons::loadHolographyLessonTemplate(
            lessonTemplateRoot(), "lesson_" + std::string(lessonId));
        holographyUiState_.replaceDraftProject(lessonTemplate.config);
        holographyProjectName_ = lessonTemplate.name;
        holographyProjectProvenance_ = lessonTemplate.provenance;
        holographyUiState_.apply();
        holographyUiState_.setDisplayPlane(
            holographyui::DisplayPlane::H1Exposure);
        holographyResult_.reset();
        holographyErrorMessage_.clear();
        holographyStatusMessage_ = "Holography lesson recompute queued";
        lessonHolographyReplayContents_
            = lessons::HolographyReplayContents::DesiredImageOnly;
        lessonH1H2Placement_ = holography::H2ImagePlacement::PositiveSide;
        ImGui::SetWindowFocus(
            docking::DockLayoutConfig::kHolographyWindowName);
    }
}

void Application::drawLearnPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kLearnWindowName);

    constexpr std::array<const char*, 2> localeNames {
        "English (en)",
        "简体中文 (zh-Hans)",
    };
    int localeIndex = lessonLocale_ == lessons::LessonLocale::English ? 0 : 1;
    if (ImGui::Combo(
            "Language", &localeIndex, localeNames.data(),
            static_cast<int>(localeNames.size()))) {
        lessonLocale_ = localeIndex == 0
            ? lessons::LessonLocale::English
            : lessons::LessonLocale::SimplifiedChinese;
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Stable locale: %s",
        std::string(lessons::lessonLocaleCode(lessonLocale_)).c_str());

    ImGui::SeparatorText("Course catalog");
    if (ImGui::BeginChild("LessonCatalog", ImVec2(0.0F, 220.0F), ImGuiChildFlags_Borders)) {
        for (const auto& definition : learnSession_.catalog().lessons()) {
            const auto status = lessons::lessonStatus(
                learnSession_.catalog(), learnSession_.progress(), definition.id);
            const auto& title = lessonLocalization_.text(
                lessonLocale_, definition.titleKey);
            const std::string label = title + "  [" + lessonStatusName(status)
                + "]##" + definition.id;
            if (ImGui::Selectable(
                    label.c_str(), selectedLessonId_ == definition.id)) {
                selectedLessonId_ = definition.id;
            }
        }
    }
    ImGui::EndChild();

    if (ImGui::BeginChild("LessonDetails", ImVec2(0.0F, 0.0F), ImGuiChildFlags_Borders)) {
        const auto& definition = learnSession_.catalog().lesson(selectedLessonId_);
        const auto status = lessons::lessonStatus(
            learnSession_.catalog(), learnSession_.progress(), definition.id);
        ImGui::TextUnformatted(
            lessonLocalization_.text(lessonLocale_, definition.titleKey).c_str());
        ImGui::SameLine();
        ImGui::TextDisabled("[%s]", lessonStatusName(status));
        ImGui::TextWrapped(
            "%s",
            lessonLocalization_.text(
                lessonLocale_, definition.objectiveKey).c_str());
        ImGui::TextDisabled(
            "Template: %s | identity: %s",
            definition.projectTemplateId.c_str(), definition.id.c_str());

        if (status == lessons::LessonStatus::Locked) {
            ImGui::SeparatorText("Prerequisites");
            for (const auto& prerequisiteId : definition.prerequisiteIds) {
                const auto& prerequisite = learnSession_.catalog().lesson(prerequisiteId);
                ImGui::BulletText(
                    "%s [%s]",
                    lessonLocalization_.text(
                        lessonLocale_, prerequisite.titleKey).c_str(),
                    lessonStatusName(lessons::lessonStatus(
                        learnSession_.catalog(), learnSession_.progress(),
                        prerequisiteId)));
            }
        }

        const bool workflowReady = lessons::hasInteractiveLessonWorkflow(definition.id);
        const bool isActive = learnSession_.hasActiveLesson()
            && learnSession_.activeLessonId() == definition.id;
        if (!isActive) {
            ImGui::BeginDisabled(
                status == lessons::LessonStatus::Locked || !workflowReady);
            if (ImGui::Button(
                    status == lessons::LessonStatus::Completed
                        ? "Review lesson"
                        : "Start lesson")) {
                try {
                    learnSession_.beginLesson(definition.id);
                    loadLessonTemplate(definition.id);
                    learnSession_.confirmTemplateLoaded();
                    lessonErrorMessage_.clear();
                    lessonStatusMessage_ = "Started " + definition.id;
                } catch (const std::exception& ex) {
                    learnSession_.endLesson();
                    lessonErrorMessage_ = "Lesson start failed: "
                        + std::string(ex.what());
                    lessonStatusMessage_.clear();
                }
            }
            ImGui::EndDisabled();
            if (!workflowReady) {
                ImGui::SameLine();
                ImGui::TextDisabled("Guided workflow is scheduled in M7.");
            }
        } else {
            try {
                if (definition.id == "thin_lens"
                    || definition.id == "real_virtual_images") {
                    learnSession_.observeOpticalBenchScene(scene_);
                } else if (definition.id == "diffraction"
                    && detectorResult_) {
                    learnSession_.observeWaveDetector(
                        detectorUiState_.appliedConfig(), *detectorResult_);
                } else if ((definition.id == "fourier_plane"
                                || definition.id == "spatial_filtering"
                                || definition.id == "na_psf")
                    && detectorResult_ && samplingDebuggerResult_) {
                    if (samplingDebuggerResult_->sourceConfig
                        == samplingDebuggerConfig_) {
                        learnSession_.observeSamplingDebugger(
                            *detectorResult_, samplingDebuggerConfig_,
                            *samplingDebuggerResult_);
                    }
                } else if (definition.id == "coherence_interference"
                    && slmInterferenceResult_) {
                    learnSession_.observeSlmInterference(
                        slmInterferenceUiState_.appliedConfig(),
                        *slmInterferenceResult_);
                } else if ((definition.id == "holography"
                                || definition.id == "h1_h2_advanced")
                    && holographyResult_) {
                    learnSession_.observeHolographyLab(
                        holographyUiState_.appliedConfig(),
                        *holographyResult_,
                        holographyUiState_.displayPlane()
                            == holographyui::DisplayPlane::H1RealImage);
                }
            } catch (const std::exception& ex) {
                lessonErrorMessage_ = "Lesson observation failed: "
                    + std::string(ex.what());
            }

            const std::size_t completedSteps = lessons::nextLessonStepIndex(
                learnSession_.catalog(), learnSession_.progress(), definition.id);
            ImGui::SeparatorText("Guided steps");
            for (std::size_t index = 0; index < definition.steps.size(); ++index) {
                const auto& lessonStep = definition.steps[index];
                const char* marker = index < completedSteps
                    ? "[done]"
                    : (index == completedSteps ? "[current]" : "[next]");
                ImGui::Text(
                    "%s %s",
                    marker,
                    lessonLocalization_.text(
                        lessonLocale_, lessonStep.titleKey).c_str());
                if (index == completedSteps) {
                    ImGui::Indent();
                    ImGui::TextWrapped(
                        "%s",
                        lessonLocalization_.text(
                            lessonLocale_, lessonStep.instructionKey).c_str());
                    ImGui::TextDisabled(
                        "%s",
                        lessonLocalization_.text(
                            lessonLocale_, lessonStep.contextKey).c_str());
                    ImGui::Unindent();
                }
            }

            if (definition.id == "reflection_refraction") {
                ImGui::SeparatorText("Interactive observation");
                auto config = reflectionRefractionConfig_;
                float angleDegrees = static_cast<float>(
                    config.incidenceAngleRadians * 180.0
                    / std::numbers::pi_v<double>);
                bool edited = false;
                if (ImGui::SliderFloat(
                        "Incidence angle", &angleDegrees,
                        0.0F, 80.0F, "%.1f deg")) {
                    config.incidenceAngleRadians = static_cast<double>(angleDegrees)
                        * std::numbers::pi_v<double> / 180.0;
                    edited = true;
                }
                edited = ImGui::InputDouble(
                    "Incident index n1", &config.incidentRefractiveIndex,
                    0.01, 0.1, "%.4f") || edited;
                edited = ImGui::InputDouble(
                    "Transmitted index n2", &config.transmittedRefractiveIndex,
                    0.01, 0.1, "%.4f") || edited;
                if (edited) {
                    if (applyReflectionRefractionConfig(config)) {
                        lessonErrorMessage_.clear();
                    } else {
                        lessonErrorMessage_ = "Lesson control rejected: "
                            + reflectionErrorMessage_;
                    }
                }

                const auto& result = reflectionRefractionResult_;
                ImGui::Text(
                    "Measured: incidence %.3f deg | reflection %.3f deg",
                    result.incidenceAngleRadians * 180.0
                        / std::numbers::pi_v<double>,
                    result.reflectionAngleRadians * 180.0
                        / std::numbers::pi_v<double>);
                if (result.totalInternalReflection) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Total internal reflection: no transmitted ray.");
                } else {
                    ImGui::Text(
                        "Transmission %.3f deg | Snell residual %.3e",
                        result.transmissionAngleRadians * 180.0
                            / std::numbers::pi_v<double>,
                        result.snellResidual);
                }
                const bool readyToConfirm = completedSteps == 2U
                    && !result.totalInternalReflection;
                ImGui::BeginDisabled(!readyToConfirm);
                if (ImGui::Button("Confirm measured laws")) {
                    if (learnSession_.confirmReflectionObservation()) {
                        lessonStatusMessage_ = "Reflection / Refraction completed";
                        lessonErrorMessage_.clear();
                    } else {
                        lessonErrorMessage_ =
                            "Change incidence by at least 5 degrees and use a refracted state.";
                    }
                }
                ImGui::EndDisabled();
            } else if (definition.id == "thin_lens") {
                ImGui::SeparatorText("Shared Lab observation");
                if (learnSession_.thinLensObservation().has_value()) {
                    const auto& observation =
                        learnSession_.thinLensObservation().value();
                    ImGui::Text(
                        "Predicted image plane: %.3f mm",
                        observation.prediction.imagePlaneZMetres * 1000.0);
                    ImGui::Text(
                        "Screen position: %.3f mm | focus error: %+.3f mm",
                        scene_.screen.planeZMetres * 1000.0,
                        observation.screenFocusErrorMetres * 1000.0);
                    ImGui::TextDisabled(
                        "Use the orange 3D gizmo or Inspector Screen Z. "
                        "Tolerance: +/-1.0 mm.");
                }
            } else if (definition.id == "real_virtual_images") {
                ImGui::SeparatorText("Shared Lab observation");
                if (learnSession_.realVirtualObservation().has_value()) {
                    const auto& observation
                        = learnSession_.realVirtualObservation().value();
                    ImGui::Text(
                        "Object distance u: %.3f mm | focal length f: %.3f mm",
                        observation.prediction.objectDistanceMetres * 1000.0,
                        scene_.lens.focalLengthMetres * 1000.0);
                    ImGui::Text(
                        "Shared solver classification: %s | signed image distance: %+.3f mm",
                        imageNatureName(observation.prediction.nature),
                        observation.prediction.imageDistanceMetres * 1000.0);
                    ImGui::TextDisabled(
                        "Use Inspector > Point Source > Object Distance u. "
                        "Move u below positive f to cross the focal plane.");

                    constexpr std::array<const char*, 3> imageNatureNames {
                        "Real", "Virtual", "At infinity"};
                    int classificationIndex = static_cast<int>(
                        lessonImageClassification_);
                    if (ImGui::Combo(
                            "My classification",
                            &classificationIndex,
                            imageNatureNames.data(),
                            static_cast<int>(imageNatureNames.size()))) {
                        lessonImageClassification_
                            = static_cast<optics::scene::ImageNature>(
                                classificationIndex);
                    }
                    const bool readyToClassify = completedSteps == 2U
                        && observation.crossedFocalPlane;
                    ImGui::BeginDisabled(!readyToClassify);
                    if (ImGui::Button("Confirm image classification")) {
                        if (learnSession_.confirmRealVirtualClassification(
                                lessonImageClassification_)) {
                            lessonStatusMessage_
                                = "Real / Virtual Images completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Use the ray direction and signed image distance to classify the image.";
                        }
                    }
                    ImGui::EndDisabled();
                }
            } else if (definition.id == "diffraction") {
                ImGui::SeparatorText("Shared Wave Detector observation");
                if (detectorUiState_.isDirty()) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Wave Detector draft is pending Apply & Recompute.");
                }
                if (learnSession_.diffractionObservation().has_value()) {
                    const auto& observation
                        = learnSession_.diffractionObservation().value();
                    ImGui::Text(
                        "Applied aperture full width: %.3f mm",
                        observation.apertureFullWidthMetres * 1000.0);
                    ImGui::Text(
                        "Measured horizontal half-maximum width: %.3f mm",
                        observation.horizontalHalfMaximumWidthMetres * 1000.0);
                    ImGui::TextDisabled(
                        "Edit Wave Detector > Aperture > Half width, then Apply. "
                        "The metric is measured from the propagated field.");
                    const bool readyToConfirm = completedSteps == 2U
                        && observation.patternBroadened;
                    ImGui::BeginDisabled(!readyToConfirm);
                    if (ImGui::Button("Confirm diffraction broadening")) {
                        if (learnSession_.confirmDiffractionObservation()) {
                            lessonStatusMessage_ = "Diffraction completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Narrow the aperture by at least 25% and require at least 10% measured broadening.";
                        }
                    }
                    ImGui::EndDisabled();
                } else if (!detectorErrorMessage_.empty()) {
                    ImGui::TextColored(
                        ImVec4(1.0F, 0.35F, 0.30F, 1.0F),
                        "Wave Detector: %s", detectorErrorMessage_.c_str());
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared Wave Detector result.");
                }
            } else if (definition.id == "fourier_plane") {
                ImGui::SeparatorText("Shared Sampling Debugger observation");
                if (samplingDebuggerResult_
                    && samplingDebuggerResult_->sourceConfig
                        != samplingDebuggerConfig_) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Sampling Debugger controls are pending Refresh.");
                }
                if (learnSession_.fourierPlaneObservation().has_value()) {
                    const auto& observation
                        = learnSession_.fourierPlaneObservation().value();
                    ImGui::Text(
                        "Non-DC Fourier energy: %.3f%% | probe planes: %zu",
                        100.0 * observation.nonDcSpectralEnergyFraction,
                        observation.probePlaneCount);
                    ImGui::TextDisabled(
                        "Set Sampling Debugger > Probe offset z to at least 1 mm, "
                        "then Refresh Sampling Debugger.");
                    constexpr std::array<const char*, 3> planeNames {
                        "Object plane", "Fourier plane", "Image plane"};
                    int planeIndex = static_cast<int>(
                        lessonFourierPlaneIdentification_);
                    if (ImGui::Combo(
                            "Plane containing the spatial spectrum",
                            &planeIndex,
                            planeNames.data(),
                            static_cast<int>(planeNames.size()))) {
                        lessonFourierPlaneIdentification_
                            = static_cast<lessons::FourierPlaneIdentification>(
                                planeIndex);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.probeMoved
                        && observation.spectrumResolved;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm Fourier plane")) {
                        if (learnSession_.confirmFourierPlaneIdentification(
                                lessonFourierPlaneIdentification_)) {
                            lessonStatusMessage_ = "Fourier Plane completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Identify the plane where position represents spatial frequency.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared Sampling Debugger result.");
                }
            } else if (definition.id == "spatial_filtering") {
                ImGui::SeparatorText("Shared 4-f filter observation");
                if (samplingDebuggerResult_
                    && samplingDebuggerResult_->sourceConfig
                        != samplingDebuggerConfig_) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Sampling Debugger controls are pending Refresh.");
                }
                if (learnSession_.spatialFilteringObservation().has_value()) {
                    const auto& observation
                        = learnSession_.spatialFilteringObservation().value();
                    ImGui::Text(
                        "Image detail metric: %.6g | power transmission: %.3f%%",
                        observation.imageDetailMetric,
                        100.0 * observation.integratedIntensityTransmission);
                    ImGui::TextDisabled(
                        "Select Low pass in Sampling Debugger, keep the 0.08 mm "
                        "outer radius, then Refresh.");
                    constexpr std::array<const char*, 3> effectNames {
                        "Sharper", "Smoother / blurred", "Only brighter"};
                    int effectIndex = static_cast<int>(
                        lessonSpatialFilteringEffect_);
                    if (ImGui::Combo(
                            "Observed image effect",
                            &effectIndex,
                            effectNames.data(),
                            static_cast<int>(effectNames.size()))) {
                        lessonSpatialFilteringEffect_
                            = static_cast<lessons::SpatialFilteringEffect>(
                                effectIndex);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.imageSmoothed;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm spatial-filter effect")) {
                        if (learnSession_.confirmSpatialFilteringEffect(
                                lessonSpatialFilteringEffect_)) {
                            lessonStatusMessage_ = "Spatial Filtering completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Relate blocked outer frequencies to the measured loss of image detail.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared 4-f result and pass-all baseline.");
                }
            } else if (definition.id == "na_psf") {
                ImGui::SeparatorText("Shared PSF / MTF observation");
                if (samplingDebuggerResult_
                    && samplingDebuggerResult_->sourceConfig
                        != samplingDebuggerConfig_) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Sampling Debugger controls are pending Refresh.");
                }
                if (learnSession_.naPsfObservation().has_value()) {
                    const auto& observation
                        = learnSession_.naPsfObservation().value();
                    ImGui::Text(
                        "Paraxial NA: %.6g | first-dark radius: %.3f um",
                        observation.paraxialNumericalAperture,
                        observation.firstDarkRadiusMetres * 1.0e6);
                    ImGui::TextDisabled(
                        "Increase Sampling Debugger > Circular pupil radius "
                        "from 0.50 mm to at least 0.625 mm, then Refresh.");
                    constexpr std::array<const char*, 3> widthNames {
                        "Wider", "Narrower", "Unchanged"};
                    int widthIndex = static_cast<int>(lessonPsfWidthChange_);
                    if (ImGui::Combo(
                            "PSF width after increasing NA",
                            &widthIndex,
                            widthNames.data(),
                            static_cast<int>(widthNames.size()))) {
                        lessonPsfWidthChange_
                            = static_cast<lessons::PsfWidthChange>(widthIndex);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.psfNarrowed;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm NA / PSF relationship")) {
                        if (learnSession_.confirmPsfWidthChange(
                                lessonPsfWidthChange_)) {
                            lessonStatusMessage_ = "NA / PSF completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Use the measured first-dark radius to classify the PSF change.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared PSF baseline.");
                }
            } else if (definition.id == "coherence_interference") {
                ImGui::SeparatorText("Shared SLM interference observation");
                if (slmInterferenceUiState_.isDirty()) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "SLM draft is pending Apply SLM Experiment.");
                }
                if (learnSession_.coherenceObservation().has_value()) {
                    const auto& observation
                        = learnSession_.coherenceObservation().value();
                    ImGui::Text(
                        "OPD: %+.3f mm | |gamma|: %.6g | visibility: %.6g",
                        observation.opticalPathDifferenceMetres * 1.0e3,
                        observation.coherenceMagnitude,
                        observation.fringeVisibility);
                    ImGui::TextDisabled(
                        "Set SLM > Optical path difference to at least 1.0 mm "
                        "with the 1/e coherence length fixed at 1.0 mm, then Apply.");
                    constexpr std::array<const char*, 3> visibilityNames {
                        "Higher", "Lower", "Unchanged"};
                    int visibilityIndex = static_cast<int>(
                        lessonFringeVisibilityChange_);
                    if (ImGui::Combo(
                            "Fringe visibility change",
                            &visibilityIndex,
                            visibilityNames.data(),
                            static_cast<int>(visibilityNames.size()))) {
                        lessonFringeVisibilityChange_
                            = static_cast<lessons::FringeVisibilityChange>(
                                visibilityIndex);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.visibilityReduced;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm coherence effect")) {
                        if (learnSession_.confirmFringeVisibilityChange(
                                lessonFringeVisibilityChange_)) {
                            lessonStatusMessage_
                                = "Coherence / Interference completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Compare the measured fringe visibility before and after the OPD change.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared SLM interference baseline.");
                }
            } else if (definition.id == "holography") {
                ImGui::SeparatorText("Shared holography observation");
                if (holographyUiState_.isDirty()) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Holography draft is pending Apply.");
                }
                if (learnSession_.holographyObservation().has_value()) {
                    const auto& observation
                        = learnSession_.holographyObservation().value();
                    ImGui::Text(
                        "Worst H1 replay error: %.3e | zero/twin separation: "
                        "%.4g / %.4g mm",
                        observation.worstRealImageNormalizedError,
                        observation.minimumZeroOrderSeparationMetres * 1e3,
                        observation.minimumTwinOrderSeparationMetres * 1e3);
                    ImGui::TextDisabled(
                        "After H1 records, choose 'H1 isolated real image' "
                        "in Holography Lab to replay it.");
                    constexpr std::array<const char*, 3> contentsNames {
                        "Desired image only",
                        "Zero + desired + conjugate/twin orders",
                        "Incoherent noise"};
                    int contentsIndex = static_cast<int>(
                        lessonHolographyReplayContents_);
                    if (ImGui::Combo(
                            "Physical full replay contains",
                            &contentsIndex,
                            contentsNames.data(),
                            static_cast<int>(contentsNames.size()))) {
                        lessonHolographyReplayContents_
                            = static_cast<lessons::HolographyReplayContents>(
                                contentsIndex);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.orderDiagnosticsAvailable;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm replay orders")) {
                        if (learnSession_.confirmHolographyReplayContents(
                                lessonHolographyReplayContents_)) {
                            lessonStatusMessage_ = "Holography completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Distinguish the isolated desired order from the physical full replay.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared Holography Lab result.");
                }
            } else if (definition.id == "h1_h2_advanced") {
                ImGui::SeparatorText("Shared H1/H2 observation");
                if (holographyUiState_.isDirty()) {
                    ImGui::TextColored(
                        ImVec4(0.98F, 0.73F, 0.20F, 1.0F),
                        "Holography draft is pending Apply.");
                }
                if (learnSession_.h1H2AdvancedObservation().has_value()) {
                    const auto& observation
                        = learnSession_.h1H2AdvancedObservation().value();
                    ImGui::Text(
                        "Signed image distance from H2: %+.4f mm | "
                        "worst H2 replay error: %.3e",
                        observation.signedImageDistanceFromH2Metres * 1e3,
                        observation.worstH2ImageNormalizedError);
                    ImGui::TextDisabled(
                        "Move Holography Lab > H2 axial position from 8.0 mm "
                        "to the H1 image at 10.0 mm, then Apply.");
                    constexpr std::array<const char*, 3> placementNames {
                        "Negative side", "Transplane", "Positive side"};
                    int placementIndex = 2;
                    if (lessonH1H2Placement_
                        == holography::H2ImagePlacement::NegativeSide) {
                        placementIndex = 0;
                    } else if (lessonH1H2Placement_
                        == holography::H2ImagePlacement::Transplane) {
                        placementIndex = 1;
                    }
                    if (ImGui::Combo(
                            "H1 image relative to H2",
                            &placementIndex,
                            placementNames.data(),
                            static_cast<int>(placementNames.size()))) {
                        lessonH1H2Placement_ = placementIndex == 0
                            ? holography::H2ImagePlacement::NegativeSide
                            : (placementIndex == 1
                                    ? holography::H2ImagePlacement::Transplane
                                    : holography::H2ImagePlacement::PositiveSide);
                    }
                    const bool ready = completedSteps == 2U
                        && observation.transplaneReached;
                    ImGui::BeginDisabled(!ready);
                    if (ImGui::Button("Confirm transplane placement")) {
                        if (learnSession_.confirmH1H2ImagePlacement(
                                lessonH1H2Placement_)) {
                            lessonStatusMessage_ = "H1/H2 Advanced completed";
                            lessonErrorMessage_.clear();
                        } else {
                            lessonErrorMessage_
                                = "Use the signed H1-image distance relative to H2.";
                        }
                    }
                    ImGui::EndDisabled();
                } else {
                    ImGui::TextDisabled(
                        "Waiting for the shared H1/H2 result.");
                }
            }

            if (lessons::lessonStatus(
                    learnSession_.catalog(), learnSession_.progress(),
                    definition.id) == lessons::LessonStatus::Completed) {
                ImGui::TextColored(
                    ImVec4(0.35F, 0.90F, 0.55F, 1.0F),
                    "Lesson workflow completed. Human concept checks remain part of M7 acceptance.");
            }

            ImGui::Separator();
            if (ImGui::Button("Reset lesson")) {
                try {
                    learnSession_.resetActiveLesson();
                    loadLessonTemplate(definition.id);
                    learnSession_.confirmTemplateLoaded();
                    lessonErrorMessage_.clear();
                    lessonStatusMessage_ = "Reset " + definition.id;
                } catch (const std::exception& ex) {
                    learnSession_.endLesson();
                    lessonErrorMessage_ = "Lesson reset failed: "
                        + std::string(ex.what());
                }
            }
            ImGui::SameLine();
            if (ImGui::Button("End lesson")) {
                learnSession_.endLesson();
                lessonStatusMessage_ = "Ended guided session; progress retained";
            }
        }

        ImGui::SeparatorText("Progress file (separate from physics projects)");
        ImGui::InputText(
            "Progress path", lessonProgressPathBuffer_,
            sizeof(lessonProgressPathBuffer_));
        if (ImGui::Button("Save progress")) {
            saveLessonProgress();
        }
        ImGui::SameLine();
        if (ImGui::Button("Load progress")) {
            loadLessonProgress();
        }
        if (!lessonErrorMessage_.empty()) {
            ImGui::TextColored(
                ImVec4(1.0F, 0.35F, 0.30F, 1.0F),
                "%s", lessonErrorMessage_.c_str());
        } else if (!lessonStatusMessage_.empty()) {
            ImGui::TextColored(
                ImVec4(0.35F, 0.90F, 0.55F, 1.0F),
                "%s", lessonStatusMessage_.c_str());
        }
    }
    ImGui::EndChild();
    ImGui::End();
}

void Application::applySandboxTargetAlignment(
    SandboxAlignmentAction action) {
    try {
        const auto* current = benchProject_.scene.find(
            selectedBenchComponentId_);
        const auto* target = benchProject_.scene.find(
            sandboxAlignmentTargetComponentId_);
        if (current == nullptr || target == nullptr
            || current->id == target->id) {
            throw std::invalid_argument(
                "choose a different alignment target");
        }

        auto edited = *current;
        math::RigidTransform3d desiredTransform = current->transform;
        std::string message;
        switch (action) {
        case SandboxAlignmentAction::AimAtTarget:
            desiredTransform = alignment::aimAt(
                current->transform, target->transform.translationMetres);
            message = "Aimed component at alignment target";
            break;
        case SandboxAlignmentAction::MakeCoaxial:
            desiredTransform = alignment::makeCoaxialWith(
                current->transform, target->transform);
            message = "Aligned component to target optical axis";
            break;
        case SandboxAlignmentAction::MatchHeight:
            desiredTransform = alignment::matchHeight(
                current->transform, target->transform);
            message = "Matched component height to target";
            break;
        case SandboxAlignmentAction::PlaceAlongTargetAxis:
            desiredTransform = alignment::placeAlongTargetAxis(
                current->transform,
                target->transform,
                static_cast<double>(sandboxAlignmentSpacingMillimetres_)
                    * 1e-3);
            message = "Placed component at target-axis spacing";
            break;
        }
        optics::scene::rebaseMechanicalAssembly(edited, desiredTransform);

        auto candidate = benchProject_.scene;
        candidate.replace(edited.id, edited);
        static_cast<void>(applyBenchScene(
            std::move(candidate), std::move(message)));
    } catch (const std::exception& error) {
        errorMessage_ = "Optical alignment failed: "
            + std::string(error.what());
        statusMessage_.clear();
    }
}

void Application::snapSandboxSelectedToNearestBeam() {
    try {
        const auto* current = benchProject_.scene.find(
            selectedBenchComponentId_);
        if (current == nullptr) {
            throw std::invalid_argument(
                "select a component before beam snapping");
        }
        const auto snapped = alignment::snapToNearestBeam(
            current->transform,
            benchTraceGraph_.segments,
            static_cast<double>(sandboxBeamSnapDistanceMillimetres_) * 1e-3);
        auto candidate = benchProject_.scene;
        auto edited = *current;
        optics::scene::rebaseMechanicalAssembly(edited, snapped.transform);
        candidate.replace(edited.id, edited);
        static_cast<void>(applyBenchScene(
            std::move(candidate),
            "Snapped component to beam branch #"
                + std::to_string(snapped.branchId)));
    } catch (const std::exception& error) {
        errorMessage_ = "Beam alignment failed: "
            + std::string(error.what());
        statusMessage_.clear();
    }
}

void Application::focusSandboxSelection() {
    const auto* selected = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (selected == nullptr) {
        errorMessage_ = "Select a Bench component before focusing the camera";
        statusMessage_.clear();
        return;
    }
    const auto& position = selected->transform.translationMetres;
    camera_.focusOn(
        {
            static_cast<float>(position.x),
            static_cast<float>(position.y),
            static_cast<float>(position.z),
        },
        sandboxComponentFocusRadius(*selected));
    errorMessage_.clear();
    statusMessage_ = "Focused camera on " + selected->id;
}

void Application::drawSandboxComponentShelf() {
    namespace bench = optics::scene;
    ImGui::BeginChild(
        "##sandbox_component_shelf",
        ImVec2(0.0F, 164.0F),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::TextUnformatted("Components");
    ImGui::SameLine();
    ImGui::SetNextItemWidth(220.0F);
    ImGui::InputTextWithHint(
        "##sandbox_component_search",
        "Search laser, lens, plate...",
        sandboxComponentSearch_,
        sizeof(sandboxComponentSearch_));
    ImGui::SameLine();
    ImGui::TextDisabled("Click: place at view centre | Drag: place on table");

    if (ImGui::Button("Empty Bench")) {
        BenchProject empty;
        empty.projectId = "untitled-bench";
        empty.name = "Untitled Optical Bench";
        sandboxExperimentMode_ = SandboxExperimentMode::Auto;
        selectedBenchComponentId_.clear();
        static_cast<void>(applyDynamicBenchProject(
            std::move(empty), "Created an empty optical bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.emptyBench);
    ImGui::SameLine();
    if (ImGui::Button("Transmission")) {
        sandboxExperimentMode_ = SandboxExperimentMode::ThinTransmission;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeTransmissionHolographyPreset(),
            "Loaded editable transmission holography bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.transmissionPreset);
    ImGui::SameLine();
    if (ImGui::Button("Reflection / Denisyuk")) {
        sandboxExperimentMode_ = SandboxExperimentMode::ReflectionDenisyuk;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeReflectionHolographyPreset(),
            "Loaded editable reflection / Denisyuk bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.reflectionPreset);
    ImGui::SameLine();
    if (ImGui::Button("RGB Full-colour")) {
        sandboxExperimentMode_ = SandboxExperimentMode::RgbFullColour;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeRgbHolographyPreset(),
            "Loaded editable RGB full-colour holography bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.rgbPreset);
    ImGui::SameLine();
    if (ImGui::Button("RGB Denisyuk")) {
        sandboxExperimentMode_
            = SandboxExperimentMode::RgbReflectionDenisyuk;
        selectedBenchComponentId_ = "plate-h1";
        sandboxObservationComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeRgbDenisyukHolographyPreset(),
            "Loaded editable RGB reflection / Denisyuk bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.rgbDenisyukPreset);
    ImGui::SameLine();
    if (ImGui::Button("CHIMERA")) {
        buildChimeraBench(
            chimera::makeCanonicalChimeraRecipe(), "canonical recipe");
    }
    captureLastItemBounds(sandboxUiEvidence_.chimeraPreset);

    if (ImGui::Button("Double Slit")) {
        selectedBenchComponentId_ = "wave-screen";
        sandboxWaveObservationComponentId_ = "wave-screen";
        static_cast<void>(applyDynamicBenchProject(
            makeDoubleSlitExperimentPreset(),
            "Loaded editable double-slit interference bench"));
    }
    captureLastItemBounds(sandboxUiEvidence_.doubleSlitPreset);
    ImGui::SameLine();
    if (ImGui::Button("Mach-Zehnder")) {
        selectedBenchComponentId_ = "mz-screen";
        sandboxWaveObservationComponentId_ = "mz-screen";
        static_cast<void>(applyDynamicBenchProject(
            makeMachZehnderInterferometerPreset(),
            "Loaded editable Mach-Zehnder interferometer bench"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Single Slit Diffraction")) {
        selectedBenchComponentId_ = "wave-screen";
        sandboxWaveObservationComponentId_ = "wave-screen";
        static_cast<void>(applyDynamicBenchProject(
            makeSingleSlitDiffractionPreset(),
            "Loaded editable single-slit diffraction bench"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Circular Diffraction")) {
        selectedBenchComponentId_ = "wave-screen";
        sandboxWaveObservationComponentId_ = "wave-screen";
        static_cast<void>(applyDynamicBenchProject(
            makeCircularDiffractionPreset(),
            "Loaded editable circular-aperture diffraction bench"));
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "Move a white Screen or non-blocking Field Probe; the field follows its real 3D plane");

    const auto& kinds = bench::requiredBenchComponentKinds();
    constexpr int kShelfColumns = 4;
    if (ImGui::BeginTable(
            "##sandbox_component_shelf_table",
            kShelfColumns,
            ImGuiTableFlags_SizingStretchSame)) {
        int visibleIndex = 0;
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            const auto kind = kinds[index];
            const std::string_view label = bench::benchComponentDisplayName(kind);
            if (!containsAsciiCaseInsensitive(
                    label, sandboxComponentSearch_)) {
                continue;
            }
            ImGui::TableNextColumn();
            ImGui::PushID(static_cast<int>(index));
            const float buttonWidth = std::max(
                80.0F, ImGui::GetContentRegionAvail().x);
            if (ImGui::Button(label.data(), ImVec2(buttonWidth, 0.0F))) {
                const glm::vec3 target = camera_.target();
                static_cast<void>(placeSandboxComponent(
                    kind,
                    {static_cast<double>(target.x), 0.0,
                        static_cast<double>(target.z)},
                    "Placed " + std::string(label) + " from component shelf"));
            }
            if (kind == bench::BenchComponentKind::LaserSource) {
                captureLastItemBounds(sandboxUiEvidence_.laserShelf);
            } else if (kind
                == bench::BenchComponentKind::ObjectWavefrontSource) {
                captureLastItemBounds(sandboxUiEvidence_.objectSourceShelf);
            } else if (kind
                == bench::BenchComponentKind::ScreenDetector) {
                captureLastItemBounds(sandboxUiEvidence_.screenShelf);
            } else if (kind == bench::BenchComponentKind::FieldProbe) {
                captureLastItemBounds(sandboxUiEvidence_.probeShelf);
            } else if (kind == bench::BenchComponentKind::HolographicPlate) {
                captureLastItemBounds(sandboxUiEvidence_.plateShelf);
            }
            const int payloadIndex = static_cast<int>(index);
            if (ImGui::BeginDragDropSource(
                    ImGuiDragDropFlags_SourceAllowNullID)) {
                ImGui::SetDragDropPayload(
                    kBenchComponentDragPayload,
                    &payloadIndex,
                    sizeof(payloadIndex));
                ImGui::Text("Place %s on optical table", label.data());
                ImGui::EndDragDropSource();
            }
            ImGui::PopID();
            ++visibleIndex;
        }
        if (visibleIndex == 0) {
            ImGui::TableNextColumn();
            ImGui::TextDisabled("No matching components");
        }
        ImGui::EndTable();
    }
    ImGui::EndChild();
}

void Application::drawSandboxSourceBar() {
    namespace bench = optics::scene;
    const auto* selected = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (selected == nullptr
        || (selected->kind != bench::BenchComponentKind::LaserSource
            && selected->kind
                != bench::BenchComponentKind::ObjectWavefrontSource)) {
        return;
    }

    const auto applyPreset = [this](BenchSourceSpectrumPreset preset) {
        try {
            const auto* current = benchProject_.scene.find(
                selectedBenchComponentId_);
            if (current == nullptr) {
                throw std::invalid_argument(
                    "select a source before applying a spectral preset");
            }
            auto candidate = benchProject_.scene;
            const auto edited = applySourceSpectrumPreset(*current, preset);
            candidate.replace(edited.id, edited);
            static_cast<void>(applyBenchScene(
                std::move(candidate),
                "Applied source spectrum preset"));
        } catch (const std::exception& error) {
            errorMessage_ = "Source spectrum preset failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    };

    ImGui::BeginChild(
        "##sandbox_source_spectrum_bar",
        ImVec2(0.0F, 48.0F),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Source spectrum: %s", selected->id.c_str());
    ImGui::SameLine();
    if (ImGui::Button("Red 638 nm")) {
        applyPreset(BenchSourceSpectrumPreset::Red);
    }
    captureLastItemBounds(sandboxUiEvidence_.sourceRed);
    ImGui::SameLine();
    if (ImGui::Button("Green 532 nm")) {
        applyPreset(BenchSourceSpectrumPreset::Green);
    }
    captureLastItemBounds(sandboxUiEvidence_.sourceGreen);
    ImGui::SameLine();
    if (ImGui::Button("Blue 450 nm")) {
        applyPreset(BenchSourceSpectrumPreset::Blue);
    }
    captureLastItemBounds(sandboxUiEvidence_.sourceBlue);
    ImGui::SameLine();
    ImGui::BeginDisabled(
        selected->kind != bench::BenchComponentKind::LaserSource);
    if (ImGui::Button("RGB laser")) {
        applyPreset(BenchSourceSpectrumPreset::Rgb);
    }
    captureLastItemBounds(sandboxUiEvidence_.sourceRgb);
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::TextDisabled(
        "presets keep total power; exact channels remain editable in Inspector");
    ImGui::EndChild();
}

void Application::drawSandboxAlignmentBar() {
    namespace bench = optics::scene;
    const auto* selected = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (selected == nullptr || benchProject_.scene.components().size() < 2U) {
        return;
    }

    const auto* target = benchProject_.scene.find(
        sandboxAlignmentTargetComponentId_);
    if (target == nullptr || target->id == selected->id) {
        sandboxAlignmentTargetComponentId_.clear();
        for (const auto& component : benchProject_.scene.components()) {
            if (component.id != selected->id) {
                sandboxAlignmentTargetComponentId_ = component.id;
                target = &component;
                break;
            }
        }
    }

    ImGui::BeginChild(
        "##sandbox_alignment_bar",
        ImVec2(0.0F, 68.0F),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    ImGui::Text("Align %s to", selected->id.c_str());
    ImGui::SameLine();
    ImGui::SetNextItemWidth(230.0F);
    const char* targetPreview = target == nullptr
        ? "No target component" : target->id.c_str();
    if (ImGui::BeginCombo("##bench-alignment-target", targetPreview)) {
        for (const auto& component : benchProject_.scene.components()) {
            if (component.id == selected->id) {
                continue;
            }
            const bool isTarget
                = sandboxAlignmentTargetComponentId_ == component.id;
            const std::string label = component.id + "  ["
                + std::string(bench::benchComponentDisplayName(component.kind))
                + "]";
            if (ImGui::Selectable(label.c_str(), isTarget)) {
                sandboxAlignmentTargetComponentId_ = component.id;
            }
            if (isTarget) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::TextDisabled(
        "ordinary transforms; no hidden optical connection");

    ImGui::BeginDisabled(target == nullptr);
    if (ImGui::Button("Aim +Z")) {
        applySandboxTargetAlignment(SandboxAlignmentAction::AimAtTarget);
    }
    captureLastItemBounds(sandboxUiEvidence_.aimAtTarget);
    ImGui::SameLine();
    if (ImGui::Button("Coaxial")) {
        applySandboxTargetAlignment(SandboxAlignmentAction::MakeCoaxial);
    }
    ImGui::SameLine();
    if (ImGui::Button("Same height")) {
        applySandboxTargetAlignment(SandboxAlignmentAction::MatchHeight);
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(92.0F);
    ImGui::DragFloat(
        "##bench-axis-spacing-mm",
        &sandboxAlignmentSpacingMillimetres_,
        1.0F,
        -5000.0F,
        5000.0F,
        "%.1f mm");
    ImGui::SameLine();
    if (ImGui::Button("Place on target +Z")) {
        applySandboxTargetAlignment(
            SandboxAlignmentAction::PlaceAlongTargetAxis);
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    if (ImGui::Button("Snap to beam")) {
        snapSandboxSelectedToNearestBeam();
    }
    captureLastItemBounds(sandboxUiEvidence_.snapToBeam);
    ImGui::EndChild();
}

void Application::drawSandboxWaveBar() {
    namespace bench = optics::scene;
    const auto* selected = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (selected != nullptr
        && (selected->kind == bench::BenchComponentKind::ScreenDetector
            || selected->kind == bench::BenchComponentKind::FieldProbe)) {
        sandboxWaveObservationComponentId_ = selected->id;
    }
    const auto* observation = benchProject_.scene.find(
        sandboxWaveObservationComponentId_);
    if (observation == nullptr
        || (observation->kind != bench::BenchComponentKind::ScreenDetector
            && observation->kind != bench::BenchComponentKind::FieldProbe)) {
        sandboxWaveObservationComponentId_.clear();
        return;
    }

    auto* waveObservation = activeSandboxWaveObservation();
    bool current = waveObservation
        && !waveObservation->isStaleFor(benchProject_.scene)
        && waveObservation->observationComponentId
            == observation->id;
    ImGui::BeginChild(
        "##sandbox_wave_bar",
        ImVec2(0.0F, 82.0F),
        ImGuiChildFlags_Borders,
        ImGuiWindowFlags_NoScrollbar);
    ImGui::AlignTextToFramePadding();
    const bool virtualProbe
        = observation->kind == bench::BenchComponentKind::FieldProbe;
    ImGui::Text(
        "%s: %s",
        virtualProbe ? "Virtual field plane" : "White screen field",
        observation->id.c_str());
    ImGui::SameLine();
    ImGui::Checkbox("Observe light field", &sandboxLiveWavePlane_);
    if (current && sandboxWaveObservations_.size() > 1U) {
        ImGui::SameLine();
        ImGui::SetNextItemWidth(190.0F);
        const std::string currentChannelLabel
            = waveChannelLabel(*waveObservation);
        if (ImGui::BeginCombo(
                "##bench-wave-channel", currentChannelLabel.c_str())) {
            for (std::size_t index = 0;
                 index < sandboxWaveObservations_.size(); ++index) {
                const auto& candidate = sandboxWaveObservations_[index];
                const std::string label = waveChannelLabel(candidate);
                const bool selectedChannel
                    = index == static_cast<std::size_t>(
                        sandboxWaveChannelIndex_);
                if (ImGui::Selectable(
                        label.c_str(), selectedChannel)) {
                    sandboxWaveChannelIndex_ = static_cast<int>(index);
                    sandboxWaveTextureDirty_ = true;
                    waveObservation = activeSandboxWaveObservation();
                    current = waveObservation != nullptr;
                }
                if (selectedChannel) {
                    ImGui::SetItemDefaultFocus();
                }
            }
            ImGui::EndCombo();
        }
    }
    ImGui::SameLine();
    constexpr std::array<const char*, 3> waveViewNames {
        "Intensity", "dB", "Phase"};
    ImGui::SetNextItemWidth(92.0F);
    if (ImGui::Combo(
            "##bench-wave-view",
            &sandboxWaveViewModeIndex_,
            waveViewNames.data(),
            static_cast<int>(waveViewNames.size()))) {
        sandboxWaveTextureDirty_ = true;
    }
    ImGui::SameLine();
    if (sandboxWaveViewModeIndex_ == 1) {
        ImGui::SetNextItemWidth(95.0F);
        if (ImGui::DragFloat(
                "##bench-wave-db-floor",
                &sandboxWaveDecibelFloor_,
                1.0F,
                -160.0F,
                -1.0F,
                "%.0f dB")) {
            sandboxWaveTextureDirty_ = true;
        }
    } else if (sandboxWaveViewModeIndex_ == 2) {
        ImGui::SetNextItemWidth(105.0F);
        if (ImGui::DragFloat(
                "##bench-wave-phase-floor",
                &sandboxWavePhaseMinimumPeakFraction_,
                1.0e-6F,
                0.0F,
                1.0F,
                "%.3g peak")) {
            sandboxWaveTextureDirty_ = true;
        }
    }
    if (current) {
        const auto& result = *waveObservation;
        const ImVec4 statusColor = result.interactivePreview
                ? ImVec4(1.0F, 0.72F, 0.24F, 1.0F)
                : ImVec4(0.35F, 0.9F, 0.45F, 1.0F);
        if (result.contributions.size() == 1U) {
            const auto& contribution = result.contributions.front();
            ImGui::TextColored(
                statusColor,
                "%s %zux%zu | OPL %.4f m | %zu path interactions%s",
                result.interactivePreview ? "DRAG PREVIEW" : "CURRENT",
                result.fieldAtObservation.width(),
                result.fieldAtObservation.height(),
                contribution.accumulatedOpticalPathMetres,
                contribution.pathComponentIds.size(),
                contribution.pathSampling.usedFoldedPath
                    ? " | folded" : "");
        } else {
            ImGui::TextColored(
                statusColor,
                "%s %zux%zu | %zu coherent branch contributions",
                result.interactivePreview ? "DRAG PREVIEW" : "CURRENT",
                result.fieldAtObservation.width(),
                result.fieldAtObservation.height(),
                result.contributions.size());
        }
    } else if (!sandboxWaveObservationDiagnostic_.empty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.55F, 0.25F, 1.0F),
            "%s", sandboxWaveObservationDiagnostic_.c_str());
    } else {
        ImGui::TextDisabled("Waiting for a supported traced source route");
    }
    ImGui::TextDisabled(
        virtualProbe
            ? "Field Probe is non-blocking; move it through space to scan local intensity without terminating the ray route."
            : "Screen / Detector intercepts the route and displays intensity on its physical white plane.");
    ImGui::EndChild();
}

void Application::drawSandboxExperimentBar() {
    namespace holography = optics::holography;
    namespace bench = optics::scene;

    const auto* selected = benchProject_.scene.find(selectedBenchComponentId_);
    if (selected == nullptr
        || selected->kind != bench::BenchComponentKind::HolographicPlate) {
        return;
    }

    if (const auto* currentObservation
        = benchProject_.scene.find(sandboxObservationComponentId_);
        currentObservation == nullptr
        || (currentObservation->kind
                != bench::BenchComponentKind::ScreenDetector
            && currentObservation->kind
                != bench::BenchComponentKind::FieldProbe
            && currentObservation->kind
                != bench::BenchComponentKind::HolographicPlate)) {
        sandboxObservationComponentId_.clear();
    }
    if (sandboxObservationComponentId_.empty()) {
        if (sandboxExperimentMode_
                == SandboxExperimentMode::RgbReflectionDenisyuk
            || sandboxRecordedExperiment_
                == SandboxRecordedExperiment::RgbReflectionDenisyuk) {
            sandboxObservationComponentId_ = selected->id;
        }
        for (const auto& component : benchProject_.scene.components()) {
            if (!sandboxObservationComponentId_.empty()) {
                break;
            }
            if (component.kind == bench::BenchComponentKind::ScreenDetector
                || component.kind == bench::BenchComponentKind::FieldProbe) {
                sandboxObservationComponentId_ = component.id;
                break;
            }
        }
    }

    std::size_t objectCount = 0U;
    std::size_t referenceCount = 0U;
    std::size_t transmissionPairCount = 0U;
    std::size_t reflectionPairCount = 0U;
    struct IncidentPairSummary final {
        holography::PlateRecordingGeometry geometry;
        double wavelengthMetres = 0.0;
        std::uint64_t objectBranchId = 0U;
        std::uint64_t referenceBranchId = 0U;
        double signedOpticalPathDifferenceMetres = 0.0;
        double crossingAngleRadians = 0.0;
        std::string coherenceId;
    };
    std::vector<IncidentPairSummary> incidentPairs;
    bool rgbReady = false;
    bool rgbReflectionReady = false;
    std::string analysisError;
    try {
        const auto fields = holography::collectPlateIncidentFields(
            benchProject_.scene, benchTraceGraph_, selected->id);
        for (const auto& branch : fields.branches) {
            if (branch.role == holography::RecordingBranchRole::Object) {
                ++objectCount;
            } else {
                ++referenceCount;
            }
        }
        for (const auto& object : fields.branches) {
            if (object.role != holography::RecordingBranchRole::Object) {
                continue;
            }
            for (const auto& reference : fields.branches) {
                if (reference.role
                        != holography::RecordingBranchRole::Reference
                    || !bench::canInterfere(object.beam, reference.beam)) {
                    continue;
                }
                const auto pair = holography::makePlateRecordingPair(
                    fields,
                    object.beam.provenance.branchId,
                    reference.beam.provenance.branchId);
                if (pair.geometry
                    == holography::PlateRecordingGeometry::Transmission) {
                    ++transmissionPairCount;
                } else {
                    ++reflectionPairCount;
                }
                incidentPairs.push_back({
                    .geometry = pair.geometry,
                    .wavelengthMetres = pair.wavelengthMetres,
                    .objectBranchId = pair.objectBranchId,
                    .referenceBranchId = pair.referenceBranchId,
                    .signedOpticalPathDifferenceMetres
                        = pair.signedOpticalPathDifferenceMetres,
                    .crossingAngleRadians = pair.crossingAngleRadians,
                    .coherenceId = object.beam.coherenceId,
                });
            }
        }
        try {
            static_cast<void>(
                holography::selectRgbThinTransmissionPairs(fields));
            rgbReady = true;
        } catch (const std::exception&) {
            rgbReady = false;
        }
        try {
            static_cast<void>(
                holography::selectRgbReflectionPairs(fields));
            rgbReflectionReady = true;
        } catch (const std::exception&) {
            rgbReflectionReady = false;
        }
    } catch (const std::exception& error) {
        analysisError = error.what();
    }

    bool recordingCurrent = false;
    const char* recordingState = "NOT RECORDED";
    if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::ThinTransmission
        && sandboxPlateRecording_
        && sandboxPlateRecording_->plateComponentId == selected->id) {
        recordingCurrent
            = !sandboxPlateRecording_->isStaleFor(benchProject_.scene);
        recordingState = recordingCurrent ? "CURRENT THIN" : "STALE THIN";
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::ReflectionDenisyuk
        && sandboxVolumeRecording_
        && sandboxVolumeRecording_->plateComponentId == selected->id) {
        recordingCurrent
            = !sandboxVolumeRecording_->isStaleFor(benchProject_.scene);
        recordingState = recordingCurrent
            ? "CURRENT REFLECTION" : "STALE REFLECTION";
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::RgbFullColour
        && sandboxRgbRecording_
        && sandboxRgbRecording_->plateComponentId == selected->id) {
        recordingCurrent
            = !sandboxRgbRecording_->isStaleFor(benchProject_.scene);
        recordingState = recordingCurrent ? "CURRENT RGB" : "STALE RGB";
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::RgbReflectionDenisyuk
        && sandboxRgbVolumeRecording_
        && sandboxRgbVolumeRecording_->plateComponentId == selected->id) {
        recordingCurrent = !sandboxRgbVolumeRecording_->isStaleFor(
            benchProject_.scene);
        recordingState = recordingCurrent
            ? "CURRENT RGB DENISYUK" : "STALE RGB DENISYUK";
    }

    ImGui::BeginChild(
        "##sandbox_experiment_bar",
        ImVec2(0.0F, 92.0F),
        ImGuiChildFlags_Borders);
    ImGui::Text("Plate experiment: %s", selected->id.c_str());
    ImGui::SameLine();
    const ImVec4 stateColour = recordingCurrent
        ? ImVec4(0.35F, 0.9F, 0.45F, 1.0F)
        : ImVec4(1.0F, 0.58F, 0.25F, 1.0F);
    ImGui::TextColored(stateColour, "%s", recordingState);
    ImGui::SameLine();
    if (analysisError.empty()) {
        ImGui::TextDisabled(
            "object %zu | reference %zu | transmission %zu | reflection %zu | RGB-T %s | RGB-R %s",
            objectCount,
            referenceCount,
            transmissionPairCount,
            reflectionPairCount,
            rgbReady ? "ready" : "not ready",
            rgbReflectionReady ? "ready" : "not ready");
        ImGui::SameLine();
        if (ImGui::SmallButton("Incident details")) {
            ImGui::OpenPopup("##plate-incident-details");
        }
        if (ImGui::BeginPopup("##plate-incident-details")) {
            if (incidentPairs.empty()) {
                ImGui::TextDisabled(
                    "No compatible same-wavelength/coherence pair reaches the plate.");
            }
            for (const auto& pair : incidentPairs) {
                ImGui::BulletText(
                    "%s | %.3f nm | object #%llu + reference #%llu",
                    pair.geometry
                            == holography::PlateRecordingGeometry::Transmission
                        ? "Transmission"
                        : "Reflection / Denisyuk",
                    pair.wavelengthMetres * 1e9,
                    static_cast<unsigned long long>(pair.objectBranchId),
                    static_cast<unsigned long long>(pair.referenceBranchId));
                ImGui::Indent();
                ImGui::TextDisabled(
                    "coherence %s | OPD %.6g m | crossing %.3f deg",
                    pair.coherenceId.c_str(),
                    pair.signedOpticalPathDifferenceMetres,
                    pair.crossingAngleRadians * 180.0
                        / std::numbers::pi_v<double>);
                ImGui::Unindent();
            }
            ImGui::EndPopup();
        }
    } else {
        ImGui::TextColored(
            ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
            "incident-field error: %s",
            analysisError.c_str());
    }

    constexpr const char* kExperimentModes
        = "Auto (unambiguous)\0Thin transmission\0Reflection / Denisyuk\0RGB full-colour\0RGB reflection / Denisyuk\0";
    int modeIndex = static_cast<int>(sandboxExperimentMode_);
    ImGui::SetNextItemWidth(190.0F);
    if (ImGui::Combo("##experiment-mode", &modeIndex, kExperimentModes)) {
        sandboxExperimentMode_
            = static_cast<SandboxExperimentMode>(modeIndex);
        if (sandboxExperimentMode_
            == SandboxExperimentMode::RgbReflectionDenisyuk) {
            sandboxObservationComponentId_ = selected->id;
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Record", ImVec2(82.0F, 0.0F))) {
        try {
            recordSelectedPlateExperiment();
        } catch (const std::exception& error) {
            errorMessage_ = "Bench recording failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    captureLastItemBounds(sandboxUiEvidence_.record);
    ImGui::SameLine();
    const char* observationPreview = sandboxObservationComponentId_.empty()
        ? "No observation plane"
        : sandboxObservationComponentId_.c_str();
    ImGui::SetNextItemWidth(190.0F);
    if (ImGui::BeginCombo("##experiment-observation", observationPreview)) {
        for (const auto& component : benchProject_.scene.components()) {
            if (component.kind != bench::BenchComponentKind::ScreenDetector
                && component.kind != bench::BenchComponentKind::FieldProbe
                && !(component.kind
                        == bench::BenchComponentKind::HolographicPlate
                    && component.id == selected->id
                    && (sandboxExperimentMode_
                            == SandboxExperimentMode::RgbReflectionDenisyuk
                        || sandboxRecordedExperiment_
                            == SandboxRecordedExperiment::RgbReflectionDenisyuk))) {
                continue;
            }
            const bool isSelected
                = sandboxObservationComponentId_ == component.id;
            if (ImGui::Selectable(component.id.c_str(), isSelected)) {
                sandboxObservationComponentId_ = component.id;
            }
            if (isSelected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(155.0F);
    constexpr const char* kReplayKinds
        = "Ordinary reference\0Conjugate reference\0";
    ImGui::Combo(
        "##experiment-replay-kind",
        &sandboxPlateReplayKindIndex_,
        kReplayKinds);
    if (sandboxExperimentMode_
            == SandboxExperimentMode::ReflectionDenisyuk
        || sandboxRecordedExperiment_
            == SandboxRecordedExperiment::ReflectionDenisyuk
        || sandboxExperimentMode_
            == SandboxExperimentMode::RgbReflectionDenisyuk
        || sandboxRecordedExperiment_
            == SandboxRecordedExperiment::RgbReflectionDenisyuk) {
        ImGui::SameLine();
        ImGui::TextDisabled("volume replay uses the recorded reference branch");
    }
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !recordingCurrent || sandboxObservationComponentId_.empty());
    if (ImGui::Button("Reconstruct", ImVec2(96.0F, 0.0F))) {
        try {
            reconstructSelectedPlateExperiment();
        } catch (const std::exception& error) {
            errorMessage_ = "Bench reconstruction failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    captureLastItemBounds(sandboxUiEvidence_.reconstruct);
    ImGui::EndDisabled();

    const char* reconstructedOn = nullptr;
    if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::ThinTransmission
        && sandboxPlateReplay_
        && !sandboxPlateReplay_->isStaleFor(benchProject_.scene)) {
        reconstructedOn = sandboxPlateReplay_->observationComponentId.c_str();
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::ReflectionDenisyuk
        && sandboxVolumeObservationReplay_
        && !sandboxVolumeObservationReplay_->isStaleFor(
            benchProject_.scene)) {
        reconstructedOn
            = sandboxVolumeObservationReplay_->observationComponentId.c_str();
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::RgbFullColour
        && sandboxRgbReplay_
        && !sandboxRgbReplay_->isStaleFor(benchProject_.scene)) {
        reconstructedOn = sandboxRgbReplay_->observationComponentId.c_str();
    } else if (sandboxRecordedExperiment_
            == SandboxRecordedExperiment::RgbReflectionDenisyuk
        && sandboxRgbVolumeReplay_
        && !sandboxRgbVolumeReplay_->isStaleFor(benchProject_.scene)) {
        reconstructedOn
            = sandboxRgbVolumeReplay_->observationComponentId.c_str();
    }
    if (reconstructedOn != nullptr) {
        ImGui::SameLine();
        ImGui::TextColored(
            ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
            "reconstructed on %s",
            reconstructedOn);
    }
    ImGui::EndChild();
}

void Application::drawChimeraAutomationBar() {
    if (benchProject_.projectId != "chimera-" + chimeraRecipe_.recipeId) {
        return;
    }

    const bool prepared = chimeraWorkflow_ != nullptr;
    const bool current = prepared
        && chimera::isChimeraBenchWorkflowCurrent(
            *chimeraWorkflow_, benchProject_);
    ImGui::BeginChild(
        "##chimera_automation_bar",
        ImVec2(0.0F, 218.0F),
        ImGuiChildFlags_Borders);
    ImGui::TextUnformatted("CHIMERA Automation on this editable Bench");
    ImGui::SameLine();
    if (!prepared) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.68F, 0.28F, 1.0F), "DATA NOT PREPARED");
    } else if (!current) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.45F, 0.25F, 1.0F), "STALE AFTER BENCH EDIT");
    } else {
        ImGui::TextColored(
            ImVec4(0.35F, 0.9F, 0.45F, 1.0F), "CURRENT");
        ImGui::SameLine();
        ImGui::TextDisabled(
            "dataset %s | %zu events | %zu/%zu hogels exposed",
            chimeraWorkflow_->dataset.contentHash.c_str(),
            chimeraWorkflow_->plan.events.size(),
            chimeraWorkflow_->exposures.size(),
            chimeraRecipe_.hogels.countX * chimeraRecipe_.hogels.countY);
    }

    if (ImGui::Button("Generate Dataset + Exposure Plan")) {
        try {
            prepareChimeraAutomation();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA preparation failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    captureLastItemBounds(sandboxUiEvidence_.chimeraPrepare);
    ImGui::SameLine();
    if (ImGui::Button("Load Real View Manifest")) {
        try {
            prepareChimeraAutomationFromManifest();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA view manifest rejected: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0F);
    ImGui::InputInt("Hogel X", &chimeraHogelX_);
    ImGui::SameLine();
    ImGui::SetNextItemWidth(72.0F);
    ImGui::InputInt("Hogel Y", &chimeraHogelY_);
    ImGui::SameLine();
    ImGui::BeginDisabled(!current);
    if (ImGui::Button("Expose selected RGB hogel")) {
        try {
            executeSelectedChimeraHogel();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA exposure failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    captureLastItemBounds(sandboxUiEvidence_.chimeraExpose);
    ImGui::EndDisabled();

    const char* viewPreview = "Prepare data first";
    if (prepared && !chimeraWorkflow_->dataset.sourceViews.empty()) {
        chimeraViewIndex_ = std::clamp(
            chimeraViewIndex_, 0,
            static_cast<int>(
                chimeraWorkflow_->dataset.sourceViews.size() - 1U));
        viewPreview = chimeraWorkflow_->dataset.sourceViews[
            static_cast<std::size_t>(chimeraViewIndex_)].viewId.c_str();
    }
    ImGui::SetNextItemWidth(180.0F);
    if (prepared && ImGui::BeginCombo("View", viewPreview)) {
        for (std::size_t index = 0;
             index < chimeraWorkflow_->dataset.sourceViews.size();
             ++index) {
            const bool selected
                = static_cast<int>(index) == chimeraViewIndex_;
            const auto& view = chimeraWorkflow_->dataset.sourceViews[index];
            if (ImGui::Selectable(view.viewId.c_str(), selected)) {
                chimeraViewIndex_ = static_cast<int>(index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    const char* lensPreview = chimeraCameraLensComponentId_.empty()
        ? "No Real Lens"
        : chimeraCameraLensComponentId_.c_str();
    ImGui::SetNextItemWidth(190.0F);
    if (ImGui::BeginCombo("Camera Lens", lensPreview)) {
        for (const auto& component : benchProject_.scene.components()) {
            if (component.kind
                != optics::scene::BenchComponentKind::RealLensAssembly) {
                continue;
            }
            const bool selected
                = component.id == chimeraCameraLensComponentId_;
            if (ImGui::Selectable(component.id.c_str(), selected)) {
                chimeraCameraLensComponentId_ = component.id;
            }
            if (selected) ImGui::SetItemDefaultFocus();
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const char* observationPreview = sandboxObservationComponentId_.empty()
        ? "No Screen / Probe"
        : sandboxObservationComponentId_.c_str();
    ImGui::SetNextItemWidth(210.0F);
    if (ImGui::BeginCombo("Observation", observationPreview)) {
        for (const auto& component : benchProject_.scene.components()) {
            if (component.kind
                    != optics::scene::BenchComponentKind::ScreenDetector
                && component.kind
                    != optics::scene::BenchComponentKind::FieldProbe) {
                continue;
            }
            const bool selected
                = component.id == sandboxObservationComponentId_;
            if (ImGui::Selectable(component.id.c_str(), selected)) {
                sandboxObservationComponentId_ = component.id;
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    ImGui::SameLine();
    const bool selectedExposed = current
        && std::any_of(
            chimeraWorkflow_->exposures.begin(),
            chimeraWorkflow_->exposures.end(),
            [this](const auto& exposure) {
                return exposure.hogelX
                        == static_cast<std::size_t>(std::max(0, chimeraHogelX_))
                    && exposure.hogelY
                        == static_cast<std::size_t>(std::max(0, chimeraHogelY_));
            });
    ImGui::BeginDisabled(
        !selectedExposed || chimeraCameraLensComponentId_.empty()
        || sandboxObservationComponentId_.empty());
    if (ImGui::Button("Reconstruct Through Placed Camera")) {
        try {
            reconstructSelectedChimeraHogel();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA reconstruction failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    captureLastItemBounds(sandboxUiEvidence_.chimeraReconstruct);
    ImGui::EndDisabled();
    if (current && chimeraWorkflow_->reconstruction) {
        ImGui::SameLine();
        const auto& metrics = chimeraWorkflow_->reconstruction->metrics;
        ImGui::TextDisabled(
            "%zu direction(s) | worst cross-talk %.4f | %s",
            metrics.reconstructedDirectionalSampleCount,
            metrics.maximumNearestViewCrosstalkFraction,
            metrics.allRequestedViewsResolvable ? "resolvable" : "unresolved");
    }
    if (current && chimeraWorkflow_->cameraImage
        && chimeraWorkflow_->cameraImage->usedPlacedSequentialLens) {
        const auto& image = *chimeraWorkflow_->cameraImage;
        ImGui::TextDisabled(
            "camera %s / %s | prescription %s | RGB EFL %.2f / %.2f / %.2f mm | traces %zu ok, %zu rejected",
            image.lensComponentId.c_str(),
            image.observationComponentId.c_str(),
            image.lensPrescriptionId.c_str(),
            image.rgbEffectiveFocalLengthMetres[0] * 1e3,
            image.rgbEffectiveFocalLengthMetres[1] * 1e3,
            image.rgbEffectiveFocalLengthMetres[2] * 1e3,
            image.metrics.prescriptionTraceCompletedCount,
            image.metrics.prescriptionTraceRejectedCount);
        ImGui::TextDisabled(
            "pupil rays %zu/%zu reached the sensor plane | max geometric RMS %.2f um | Airy-convolved spot/defocus",
            image.metrics.pupilRaySensorHitCount,
            image.metrics.pupilRayTraceCount,
            image.metrics.maximumGeometricRmsRadiusMetres * 1e6);
        ImGui::TextDisabled(
            "detector response %s | %s | %.2f K",
            image.cameraCalibrationId.c_str(),
            image.usedPlacedDetectorCalibration
                ? "verified placed calibration"
                : "explicit nominal preview",
            image.detectorResponseTemperatureKelvin);
    }
    ImGui::BeginDisabled(!current);
    if (ImGui::Button("New Print Batch")) {
        try {
            createChimeraBatch();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA batch creation failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::SetNextItemWidth(62.0F);
    ImGui::InputInt("Slice", &chimeraBatchSliceHogels_);
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !current || !chimeraBatch_ || chimeraBatch_->complete());
    if (ImGui::Button("Run + Checkpoint")) {
        try {
            runChimeraBatchSlice();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA batch slice failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Pause at Hogel Boundary")) {
        try {
            pauseChimeraBatch();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA batch pause failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    ImGui::EndDisabled();
    if (chimeraBatch_) {
        ImGui::SameLine();
        const float progress = static_cast<float>(
            chimeraBatch_->progressFraction());
        const std::string label
            = std::to_string(chimeraBatch_->nextLinearHogelIndex) + "/"
            + std::to_string(chimeraBatch_->totalHogelCount())
            + " complete RGB hogels";
        ImGui::ProgressBar(progress, ImVec2(250.0F, 0.0F), label.c_str());
    }
    ImGui::SetNextItemWidth(72.0F);
    ImGui::InputInt(
        "Region hogels", &chimeraReconstructionRegionHogels_);
    ImGui::SameLine();
    ImGui::BeginDisabled(
        !current || !chimeraBatch_
        || chimeraBatch_->completedHogels.empty()
        || sandboxObservationComponentId_.empty());
    if (ImGui::Button("Reconstruct Bounded Batch Region")) {
        try {
            reconstructChimeraBatchRegion();
        } catch (const std::exception& error) {
            errorMessage_ = "CHIMERA batch reconstruction failed: "
                + std::string(error.what());
            statusMessage_.clear();
        }
    }
    ImGui::EndDisabled();
    ImGui::EndChild();
}

void Application::drawSandboxInspector() {
    namespace bench = optics::scene;
    if (!ImGui::CollapsingHeader("3D Optical Sandbox", ImGuiTreeNodeFlags_DefaultOpen)) {
        return;
    }

    try {

    ImGui::TextWrapped(
        "Place and orient physical components. Rays are derived from 3D geometry; "
        "RGB channels remain separate until a coherent observation combines them.");
    if (viewportMode_ == ViewportMode::Sandbox) {
        ImGui::TextColored(ImVec4(0.28F, 0.92F, 0.62F, 1.0F), "Viewport: Free-form Sandbox");
    } else if (ImGui::Button("Return to Sandbox")) {
        static_cast<void>(showSandboxViewport());
    }
    ImGui::SameLine();
    if (ImGui::Button("Open Fixed Reference")) {
        static_cast<void>(showLegacyViewport());
    }

    ImGui::SeparatorText("Scene History");
    ImGui::BeginDisabled(!benchEditHistoryReady_ || !benchEditHistory_.canUndo());
    if (ImGui::Button("Undo Bench (Ctrl+Z)")) {
        undoBenchEdit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!benchEditHistoryReady_ || !benchEditHistory_.canRedo());
    if (ImGui::Button("Redo Bench (Ctrl+Y)")) {
        redoBenchEdit();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "%zu undo / %zu redo; restored scenes receive a fresh revision",
        benchEditHistoryReady_ ? benchEditHistory_.undoDepth() : 0U,
        benchEditHistoryReady_ ? benchEditHistory_.redoDepth() : 0U);

    ImGui::SeparatorText("Component Library");
    const auto& kinds = bench::requiredBenchComponentKinds();
    sandboxLibraryKindIndex_ = std::clamp(
        sandboxLibraryKindIndex_, 0, static_cast<int>(kinds.size()) - 1);
    const char* preview = bench::benchComponentDisplayName(
        kinds[static_cast<std::size_t>(sandboxLibraryKindIndex_)]).data();
    if (ImGui::BeginCombo("Place Component", preview)) {
        for (std::size_t index = 0; index < kinds.size(); ++index) {
            const bool selected = static_cast<int>(index) == sandboxLibraryKindIndex_;
            if (ImGui::Selectable(
                    bench::benchComponentDisplayName(kinds[index]).data(), selected)) {
                sandboxLibraryKindIndex_ = static_cast<int>(index);
            }
            if (selected) {
                ImGui::SetItemDefaultFocus();
            }
        }
        ImGui::EndCombo();
    }
    if (ImGui::Button("Add to Bench")) {
        const auto kind = kinds[static_cast<std::size_t>(sandboxLibraryKindIndex_)];
        const glm::vec3 target = camera_.target();
        static_cast<void>(placeSandboxComponent(
            kind,
            {static_cast<double>(target.x), 0.0,
                static_cast<double>(target.z)},
            "Placed " + std::string(bench::benchComponentDisplayName(kind))));
    }
    ImGui::SameLine();
    if (ImGui::Button("Empty Bench")) {
        BenchProject empty;
        empty.projectId = "untitled-bench";
        empty.name = "Untitled Optical Bench";
        sandboxExperimentMode_ = SandboxExperimentMode::Auto;
        selectedBenchComponentId_.clear();
        static_cast<void>(applyDynamicBenchProject(
            std::move(empty), "Created an empty optical bench"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Ray Branch Demo")) {
        BenchProject preset = makeDefaultSandboxProject();
        selectedBenchComponentId_.clear();
        static_cast<void>(applyDynamicBenchProject(
            std::move(preset), "Loaded ray branch demo"));
    }
    if (ImGui::Button("Transmission Hologram")) {
        sandboxExperimentMode_ = SandboxExperimentMode::ThinTransmission;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeTransmissionHolographyPreset(),
            "Loaded editable transmission holography bench"));
    }
    ImGui::SameLine();
    if (ImGui::Button("Reflection / Denisyuk")) {
        sandboxExperimentMode_ = SandboxExperimentMode::ReflectionDenisyuk;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeReflectionHolographyPreset(),
            "Loaded editable reflection / Denisyuk bench"));
    }
    if (ImGui::Button("RGB Full-colour Hologram")) {
        sandboxExperimentMode_ = SandboxExperimentMode::RgbFullColour;
        selectedBenchComponentId_ = "plate-h1";
        static_cast<void>(applyDynamicBenchProject(
            makeRgbHolographyPreset(),
            "Loaded editable RGB full-colour holography bench"));
    }
    if (ImGui::Button("Canonical CHIMERA-like Bench")) {
        buildChimeraBench(
            chimera::makeCanonicalChimeraRecipe(), "canonical recipe");
    }
    ImGui::InputText(
        "CHIMERA Recipe JSON",
        chimeraRecipePathBuffer_,
        sizeof(chimeraRecipePathBuffer_));
    ImGui::InputText(
        "Perspective View Manifest JSON",
        chimeraViewManifestPathBuffer_,
        sizeof(chimeraViewManifestPathBuffer_));
    ImGui::TextDisabled(
        "Strict manifest: radian view angles, relative P3/P6 paths, and explicit linear/sRGB transfer.");
    if (ImGui::Button("Build CHIMERA Recipe")) {
        try {
            buildChimeraBench(
                chimera::loadChimeraRecipe(chimeraRecipePathBuffer_),
                std::string(chimeraRecipePathBuffer_));
        } catch (const std::exception& error) {
            errorMessage_ = error.what();
            statusMessage_.clear();
        }
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Canonical Recipe")) {
        try {
            chimera::saveChimeraRecipe(
                chimera::makeCanonicalChimeraRecipe(),
                chimeraRecipePathBuffer_);
            errorMessage_.clear();
            statusMessage_ = "Saved canonical CHIMERA recipe: "
                + std::string(chimeraRecipePathBuffer_);
        } catch (const std::exception& error) {
            errorMessage_ = error.what();
            statusMessage_.clear();
        }
    }
    if (!chimeraConstraintReport_.empty()) {
        ImGui::SeparatorText("CHIMERA Constraint Report");
        for (const auto& entry : chimeraConstraintReport_) {
            const char* severity = "FEASIBLE";
            if (entry.severity == chimera::ConstraintSeverity::Warning) {
                severity = "WARNING";
            } else if (entry.severity
                == chimera::ConstraintSeverity::Unsupported) {
                severity = "UNSUPPORTED";
            }
            ImGui::TextWrapped(
                "[%s] %s: %s",
                severity,
                entry.code.c_str(),
                entry.message.c_str());
        }
    }
    if (benchProject_.projectId == "chimera-" + chimeraRecipe_.recipeId) {
        ImGui::SeparatorText("CHIMERA Batch Artifact");
        ImGui::InputText(
            "Batch JSON",
            chimeraBatchPathBuffer_,
            sizeof(chimeraBatchPathBuffer_));
        ImGui::BeginDisabled(!chimeraWorkflow_);
        if (ImGui::Button("Load + Validate Batch")) {
            try {
                auto loaded = chimera::loadChimeraBatchArtifact(
                    chimeraBatchPathBuffer_);
                chimera::validateChimeraBatchForWorkflow(
                    loaded,
                    chimeraWorkflow_->recipe,
                    chimeraWorkflow_->dataset,
                    chimeraWorkflow_->plan,
                    benchProject_);
                chimeraWorkflow_->exposures
                    = chimera::restoreChimeraBatchReconstructionEvidence(
                        loaded);
                chimeraBatch_ = std::make_unique<
                    chimera::ChimeraBatchArtifact>(std::move(loaded));
                errorMessage_.clear();
                statusMessage_ = "Loaded valid CHIMERA batch checkpoint at "
                    + std::to_string(
                        chimeraBatch_->nextLinearHogelIndex)
                    + "/"
                    + std::to_string(chimeraBatch_->totalHogelCount());
            } catch (const std::exception& error) {
                errorMessage_ = "CHIMERA batch load rejected: "
                    + std::string(error.what());
                statusMessage_.clear();
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!chimeraBatch_);
        if (ImGui::Button("Save Batch Checkpoint")) {
            try {
                chimera::saveChimeraBatchArtifact(
                    *chimeraBatch_, chimeraBatchPathBuffer_);
                errorMessage_.clear();
                statusMessage_ = "Saved atomic CHIMERA batch checkpoint";
            } catch (const std::exception& error) {
                errorMessage_ = "CHIMERA batch save failed: "
                    + std::string(error.what());
                statusMessage_.clear();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndDisabled();

        ImGui::SeparatorText("Transparent CHIMERA Parameter Sweep");
        ImGui::InputFloat3(
            "Relay focal candidates (mm)",
            chimeraSweepRelayFocalLengthsMillimetres_,
            "%.2f");
        ImGui::InputFloat(
            "Maximum nearest-view cross-talk",
            &chimeraSweepMaximumCrosstalk_,
            0.01F,
            0.05F,
            "%.4f");
        if (ImGui::Button("Run 3-candidate Relay Sweep")) {
            try {
                runChimeraRelaySweep();
            } catch (const std::exception& error) {
                errorMessage_ = "CHIMERA sweep failed: "
                    + std::string(error.what());
                statusMessage_.clear();
            }
        }
        if (chimeraSweepResult_) {
            const auto* best = chimeraSweepResult_->bestCandidate();
            ImGui::SameLine();
            ImGui::TextDisabled(
                "%zu candidates | best: %s",
                chimeraSweepResult_->candidates.size(),
                best == nullptr ? "none" : best->candidateId.c_str());
            if (best != nullptr) {
                ImGui::TextDisabled(
                    "min RGB efficiency %.6f | cross-talk %.6f | artifact %zu bytes",
                    best->metrics.minimumRgbDiffractionEfficiency,
                    best->metrics.maximumNearestViewCrosstalkFraction,
                    best->metrics.canonicalArtifactBytes);
                if (ImGui::Button("Build Best Candidate as Editable Bench")) {
                    try {
                        applyBestChimeraSweepCandidate();
                    } catch (const std::exception& error) {
                        errorMessage_ = "CHIMERA best-candidate build failed: "
                            + std::string(error.what());
                        statusMessage_.clear();
                    }
                }
            }
        }
    }

    ImGui::SeparatorText("Bench Components");
    ImGui::BeginChild("##sandbox_component_list", ImVec2(0.0F, 150.0F), ImGuiChildFlags_Borders);
    for (const auto& component : benchProject_.scene.components()) {
        const std::string label = component.id + "  ["
            + std::string(bench::benchComponentDisplayName(component.kind)) + "]";
        if (ImGui::Selectable(
                label.c_str(), selectedBenchComponentId_ == component.id)) {
            selectedBenchComponentId_ = component.id;
            static_cast<void>(showSandboxViewport());
        }
    }
    ImGui::EndChild();

    const auto* selected = benchProject_.scene.find(selectedBenchComponentId_);
    if (selected != nullptr) {
        ImGui::Text("Selected: %s", selected->id.c_str());
        ImGui::TextDisabled("Kind: %s", bench::benchComponentDisplayName(selected->kind).data());
        if (ImGui::Button("Duplicate")) {
            auto candidate = benchProject_.scene;
            std::string newId;
            do {
                newId = selected->id + "-copy-" + std::to_string(sandboxNextComponentOrdinal_++);
            } while (candidate.find(newId) != nullptr);
            candidate.duplicate(selected->id, newId);
            selectedBenchComponentId_ = newId;
            static_cast<void>(applyBenchScene(std::move(candidate), "Duplicated component"));
        }
        ImGui::SameLine();
        if (ImGui::Button("Focus (F)")) {
            focusSandboxSelection();
        }
        ImGui::SameLine();
        if (ImGui::Button("Delete")) {
            auto candidate = benchProject_.scene;
            static_cast<void>(candidate.remove(selected->id));
            selectedBenchComponentId_.clear();
            static_cast<void>(applyBenchScene(std::move(candidate), "Deleted component"));
        }

        selected = benchProject_.scene.find(selectedBenchComponentId_);
        if (selected != nullptr) {
            ImGui::SeparatorText("Transform");
            ImGui::DragFloat(
                "Viewport move snap (mm)",
                &sandboxTranslationSnapMillimetres_,
                0.25F,
                0.0F,
                100.0F,
                "%.2f");
            ImGui::TextDisabled(
                "0 disables snapping; X/Y/Z handles use world axes in Move mode");
            float translationMm[3] {
                static_cast<float>(selected->transform.translationMetres.x * 1000.0),
                static_cast<float>(selected->transform.translationMetres.y * 1000.0),
                static_cast<float>(selected->transform.translationMetres.z * 1000.0),
            };
            if (ImGui::DragFloat3(
                    "Position (mm)", translationMm, 0.25F, -5000.0F, 5000.0F, "%.2f")) {
                auto candidate = benchProject_.scene;
                auto edited = *candidate.find(selectedBenchComponentId_);
                auto desiredTransform = edited.transform;
                desiredTransform.translationMetres = {
                    static_cast<double>(translationMm[0]) * 1e-3,
                    static_cast<double>(translationMm[1]) * 1e-3,
                    static_cast<double>(translationMm[2]) * 1e-3,
                };
                optics::scene::rebaseMechanicalAssembly(
                    edited, desiredTransform);
                candidate.replace(edited.id, edited);
                static_cast<void>(applyBenchScene(std::move(candidate), "Moved component"));
            }

            ImGui::DragFloat(
                "Rotation step (deg)", &sandboxRotationStepDegrees_, 0.25F, 0.1F, 90.0F, "%.2f");
            const auto rotateSelected = [this](int axis, double sign) {
                auto candidate = benchProject_.scene;
                auto edited = *candidate.find(selectedBenchComponentId_);
                const double radians = sign * static_cast<double>(sandboxRotationStepDegrees_)
                    * std::numbers::pi_v<double> / 180.0;
                const auto desiredTransform = gizmo::rotateRigidTransformLocally(
                    edited.transform,
                    static_cast<gizmo::LocalRotationAxis>(axis),
                    radians);
                optics::scene::rebaseMechanicalAssembly(
                    edited, desiredTransform);
                candidate.replace(edited.id, edited);
                static_cast<void>(applyBenchScene(std::move(candidate), "Rotated component"));
            };
            if (ImGui::Button("-X##rotate")) rotateSelected(0, -1.0);
            ImGui::SameLine();
            if (ImGui::Button("+X##rotate")) rotateSelected(0, 1.0);
            ImGui::SameLine();
            if (ImGui::Button("-Y##rotate")) rotateSelected(1, -1.0);
            ImGui::SameLine();
            if (ImGui::Button("+Y##rotate")) rotateSelected(1, 1.0);
            ImGui::SameLine();
            if (ImGui::Button("-Z##rotate")) rotateSelected(2, -1.0);
            ImGui::SameLine();
            if (ImGui::Button("+Z##rotate")) rotateSelected(2, 1.0);

            selected = benchProject_.scene.find(selectedBenchComponentId_);
            if (selected != nullptr) {
                ImGui::TextDisabled(
                    "Local +Z optical axis: (%.4f, %.4f, %.4f)",
                    selected->transform.localZAxisInWorld.x,
                    selected->transform.localZAxisInWorld.y,
                    selected->transform.localZAxisInWorld.z);

                ImGui::SeparatorText("Mechanical Assembly");
                if (!selected->mechanicalAssembly.has_value()) {
                    ImGui::TextDisabled(
                        "Free optical frame; no persisted mount constraints");
                    if (ImGui::Button("Add post / XYZ stage / tip-tilt mount")) {
                        try {
                            auto candidate = benchProject_.scene;
                            auto mounted = *candidate.find(
                                selectedBenchComponentId_);
                            bench::applyMechanicalAssembly(
                                mounted,
                                bench::makeDefaultMechanicalAssembly(mounted));
                            candidate.replace(mounted.id, mounted);
                            static_cast<void>(applyBenchScene(
                                std::move(candidate),
                                "Attached constrained mechanical assembly"));
                        } catch (const std::exception& error) {
                            errorMessage_ = "Mechanical assembly failed: "
                                + std::string(error.what());
                            statusMessage_.clear();
                        }
                    }
                } else {
                    auto mechanical = *selected->mechanicalAssembly;
                    float postHeightMm = static_cast<float>(
                        mechanical.postHeightMetres * 1000.0);
                    float stageMm[3] {
                        static_cast<float>(
                            mechanical.stageTranslationMetres.x * 1000.0),
                        static_cast<float>(
                            mechanical.stageTranslationMetres.y * 1000.0),
                        static_cast<float>(
                            mechanical.stageTranslationMetres.z * 1000.0),
                    };
                    float yawDegrees = static_cast<float>(
                        mechanical.mountYawRadians * 180.0
                        / std::numbers::pi_v<double>);
                    float pitchDegrees = static_cast<float>(
                        mechanical.mountPitchRadians * 180.0
                        / std::numbers::pi_v<double>);
                    bool mechanicalChanged = ImGui::DragFloat(
                        "Post height (mm)",
                        &postHeightMm,
                        0.05F,
                        static_cast<float>(
                            mechanical.minimumPostHeightMetres * 1000.0),
                        static_cast<float>(
                            mechanical.maximumPostHeightMetres * 1000.0),
                        "%.3f");
                    mechanicalChanged |= ImGui::DragFloat(
                        "Stage X (mm)",
                        &stageMm[0],
                        0.01F,
                        static_cast<float>(
                            mechanical.minimumStageTranslationMetres.x * 1000.0),
                        static_cast<float>(
                            mechanical.maximumStageTranslationMetres.x * 1000.0),
                        "%.3f");
                    mechanicalChanged |= ImGui::DragFloat(
                        "Stage Y (mm)",
                        &stageMm[1],
                        0.01F,
                        static_cast<float>(
                            mechanical.minimumStageTranslationMetres.y * 1000.0),
                        static_cast<float>(
                            mechanical.maximumStageTranslationMetres.y * 1000.0),
                        "%.3f");
                    mechanicalChanged |= ImGui::DragFloat(
                        "Stage Z (mm)",
                        &stageMm[2],
                        0.01F,
                        static_cast<float>(
                            mechanical.minimumStageTranslationMetres.z * 1000.0),
                        static_cast<float>(
                            mechanical.maximumStageTranslationMetres.z * 1000.0),
                        "%.3f");
                    mechanicalChanged |= ImGui::DragFloat(
                        "Mount yaw (deg)",
                        &yawDegrees,
                        0.02F,
                        static_cast<float>(
                            mechanical.minimumMountYawRadians * 180.0
                            / std::numbers::pi_v<double>),
                        static_cast<float>(
                            mechanical.maximumMountYawRadians * 180.0
                            / std::numbers::pi_v<double>),
                        "%.3f");
                    mechanicalChanged |= ImGui::DragFloat(
                        "Mount pitch (deg)",
                        &pitchDegrees,
                        0.02F,
                        static_cast<float>(
                            mechanical.minimumMountPitchRadians * 180.0
                            / std::numbers::pi_v<double>),
                        static_cast<float>(
                            mechanical.maximumMountPitchRadians * 180.0
                            / std::numbers::pi_v<double>),
                        "%.3f");
                    if (mechanicalChanged) {
                        try {
                            mechanical.postHeightMetres
                                = static_cast<double>(postHeightMm) * 1e-3;
                            mechanical.stageTranslationMetres = {
                                static_cast<double>(stageMm[0]) * 1e-3,
                                static_cast<double>(stageMm[1]) * 1e-3,
                                static_cast<double>(stageMm[2]) * 1e-3,
                            };
                            mechanical.mountYawRadians
                                = static_cast<double>(yawDegrees)
                                * std::numbers::pi_v<double> / 180.0;
                            mechanical.mountPitchRadians
                                = static_cast<double>(pitchDegrees)
                                * std::numbers::pi_v<double> / 180.0;
                            auto candidate = benchProject_.scene;
                            auto mounted = *candidate.find(
                                selectedBenchComponentId_);
                            bench::applyMechanicalAssembly(
                                mounted, mechanical);
                            candidate.replace(mounted.id, mounted);
                            static_cast<void>(applyBenchScene(
                                std::move(candidate),
                                "Adjusted mechanical assembly"));
                        } catch (const std::exception& error) {
                            errorMessage_ = "Mechanical adjustment failed: "
                                + std::string(error.what());
                            statusMessage_.clear();
                        }
                    } else if (ImGui::Button("Detach mechanical assembly")) {
                        auto candidate = benchProject_.scene;
                        auto freeComponent = *candidate.find(
                            selectedBenchComponentId_);
                        bench::removeMechanicalAssembly(freeComponent);
                        candidate.replace(freeComponent.id, freeComponent);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate),
                            "Detached mechanical assembly"));
                    }
                    ImGui::TextDisabled(
                        "Stage and mount values update the solver optical frame; PCG follows the same state.");
                }

                selected = benchProject_.scene.find(
                    selectedBenchComponentId_);
                if (selected == nullptr) {
                    throw std::runtime_error(
                        "selected component disappeared during mechanical editing");
                }

                ImGui::SeparatorText("Digital-twin Identity");
                if (sandboxInstrumentEditorComponentId_
                    != selectedBenchComponentId_) {
                    sandboxInstrumentEditorComponentId_
                        = selectedBenchComponentId_;
                    copyTextToBuffer(
                        selected->instrument.specificationId,
                        sandboxInstrumentSpecificationId_);
                    sandboxInstrumentSpecificationVersion_
                        = selected->instrument.specificationVersion;
                    copyTextToBuffer(
                        selected->instrument.manufacturer.value_or(""),
                        sandboxInstrumentManufacturer_);
                    copyTextToBuffer(
                        selected->instrument.model.value_or(""),
                        sandboxInstrumentModel_);
                    copyTextToBuffer(
                        selected->instrument.serialNumber.value_or(""),
                        sandboxInstrumentSerialNumber_);
                    sandboxInstrumentCalibrationModeIndex_
                        = selected->instrument.calibrationMode
                            == bench::InstrumentCalibrationMode::Calibrated
                        ? 1
                        : 0;
                }

                const auto calibrationState
                    = bench::instrumentCalibrationState(selected->instrument);
                const ImVec4 calibrationStateColor
                    = calibrationState
                            == bench::InstrumentCalibrationState::Calibrated
                    ? ImVec4(0.28F, 0.92F, 0.62F, 1.0F)
                    : calibrationState
                            == bench::InstrumentCalibrationState::Stale
                    ? ImVec4(1.0F, 0.72F, 0.25F, 1.0F)
                    : ImVec4(0.62F, 0.70F, 0.82F, 1.0F);
                ImGui::Text("Instrument class: %s",
                    selected->instrument.instrumentClass.c_str());
                ImGui::TextColored(
                    calibrationStateColor,
                    "State: %s",
                    bench::instrumentCalibrationStateName(
                        calibrationState).data());
                if (calibrationState
                    == bench::InstrumentCalibrationState::Stale) {
                    ImGui::TextWrapped(
                        "Calibrated mode is requested, but no attached asset "
                        "or its specification binding is current. Solvers "
                        "continue to use declared nominal behavior until a "
                        "supported asset is explicitly resolved.");
                }
                ImGui::InputText(
                    "Specification ID",
                    sandboxInstrumentSpecificationId_,
                    sizeof(sandboxInstrumentSpecificationId_));
                ImGui::InputInt(
                    "Specification version",
                    &sandboxInstrumentSpecificationVersion_);
                ImGui::InputText(
                    "Manufacturer (optional)",
                    sandboxInstrumentManufacturer_,
                    sizeof(sandboxInstrumentManufacturer_));
                ImGui::InputText(
                    "Model (optional)",
                    sandboxInstrumentModel_,
                    sizeof(sandboxInstrumentModel_));
                ImGui::InputText(
                    "Serial number (optional)",
                    sandboxInstrumentSerialNumber_,
                    sizeof(sandboxInstrumentSerialNumber_));
                ImGui::RadioButton(
                    "Nominal mode",
                    &sandboxInstrumentCalibrationModeIndex_,
                    0);
                ImGui::SameLine();
                ImGui::RadioButton(
                    "Calibrated mode",
                    &sandboxInstrumentCalibrationModeIndex_,
                    1);
                if (ImGui::Button("Apply instrument identity")) {
                    try {
                        auto candidate = benchProject_.scene;
                        auto edited = *candidate.find(
                            selectedBenchComponentId_);
                        edited.instrument.specificationId
                            = sandboxInstrumentSpecificationId_;
                        edited.instrument.specificationVersion
                            = sandboxInstrumentSpecificationVersion_;
                        edited.instrument.manufacturer = optionalInstrumentText(
                            sandboxInstrumentManufacturer_);
                        edited.instrument.model = optionalInstrumentText(
                            sandboxInstrumentModel_);
                        edited.instrument.serialNumber
                            = optionalInstrumentText(
                                sandboxInstrumentSerialNumber_);
                        edited.instrument.calibrationMode
                            = sandboxInstrumentCalibrationModeIndex_ == 0
                            ? bench::InstrumentCalibrationMode::Nominal
                            : bench::InstrumentCalibrationMode::Calibrated;
                        candidate.replace(edited.id, edited);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate),
                            "Updated instrument identity"));
                    } catch (const std::exception& error) {
                        errorMessage_ = "Instrument identity rejected: "
                            + std::string(error.what());
                        statusMessage_.clear();
                    }
                }
                ImGui::SameLine();
                if (ImGui::Button("Reset generic identity")) {
                    try {
                        auto candidate = benchProject_.scene;
                        auto edited = *candidate.find(
                            selectedBenchComponentId_);
                        edited.instrument
                            = bench::makeDefaultInstrumentIdentity(
                                edited.kind);
                        candidate.replace(edited.id, edited);
                        sandboxInstrumentEditorComponentId_.clear();
                        static_cast<void>(applyBenchScene(
                            std::move(candidate),
                            "Reset generic nominal instrument identity"));
                    } catch (const std::exception& error) {
                        errorMessage_ = "Instrument reset failed: "
                            + std::string(error.what());
                        statusMessage_.clear();
                    }
                }

                selected = benchProject_.scene.find(
                    selectedBenchComponentId_);
                if (selected == nullptr) {
                    throw std::runtime_error(
                        "selected component disappeared during identity editing");
                }
                ImGui::TextDisabled(
                    "%zu attached calibration reference(s); identity metadata never selects a GPU path",
                    selected->instrument.calibrationAssets.size());
                bool calibrationAssetRemoved = false;
                for (std::size_t assetIndex = 0;
                     assetIndex
                        < selected->instrument.calibrationAssets.size();
                     ++assetIndex) {
                    const auto& asset
                        = selected->instrument.calibrationAssets[assetIndex];
                    ImGui::PushID(static_cast<int>(assetIndex));
                    ImGui::TextWrapped(
                        "%s | %s | format v%d",
                        bench::calibrationAssetKindName(asset.kind).data(),
                        asset.calibrationId.c_str(),
                        asset.formatVersion);
                    ImGui::TextDisabled(
                        "%s | %.1f-%.1f nm | %.2f-%.2f K",
                        asset.source.c_str(),
                        asset.validity.minimumVacuumWavelengthMetres * 1e9,
                        asset.validity.maximumVacuumWavelengthMetres * 1e9,
                        asset.validity.minimumTemperatureKelvin,
                        asset.validity.maximumTemperatureKelvin);
                    if (ImGui::SmallButton("Remove reference")) {
                        try {
                            auto candidate = benchProject_.scene;
                            auto edited = *candidate.find(
                                selectedBenchComponentId_);
                            edited.instrument.calibrationAssets.erase(
                                edited.instrument.calibrationAssets.begin()
                                    + static_cast<std::ptrdiff_t>(assetIndex));
                            candidate.replace(edited.id, edited);
                            static_cast<void>(applyBenchScene(
                                std::move(candidate),
                                "Removed calibration asset reference"));
                            calibrationAssetRemoved = true;
                        } catch (const std::exception& error) {
                            errorMessage_ = "Calibration removal failed: "
                                + std::string(error.what());
                            statusMessage_.clear();
                        }
                    }
                    ImGui::PopID();
                    if (calibrationAssetRemoved) break;
                }

                selected = benchProject_.scene.find(
                    selectedBenchComponentId_);
                if (selected == nullptr) {
                    throw std::runtime_error(
                        "selected component disappeared during calibration editing");
                }
                constexpr std::array calibrationAssetKinds {
                    bench::CalibrationAssetKind::OpticalPose,
                    bench::CalibrationAssetKind::ClearAperture,
                    bench::CalibrationAssetKind::CoatingResponse,
                    bench::CalibrationAssetKind::MaterialResponse,
                    bench::CalibrationAssetKind::LensPrescription,
                    bench::CalibrationAssetKind::SlmResponse,
                    bench::CalibrationAssetKind::DetectorResponse,
                    bench::CalibrationAssetKind::StageResponse,
                };
                sandboxCalibrationAssetKindIndex_ = std::clamp(
                    sandboxCalibrationAssetKindIndex_,
                    0,
                    static_cast<int>(calibrationAssetKinds.size()) - 1);
                const auto selectedAssetKind = calibrationAssetKinds[
                    static_cast<std::size_t>(
                        sandboxCalibrationAssetKindIndex_)];
                if (ImGui::BeginCombo(
                        "Calibration kind",
                        bench::calibrationAssetKindName(
                            selectedAssetKind).data())) {
                    for (std::size_t kindIndex = 0;
                         kindIndex < calibrationAssetKinds.size();
                         ++kindIndex) {
                        const bool isSelected = static_cast<int>(kindIndex)
                            == sandboxCalibrationAssetKindIndex_;
                        if (ImGui::Selectable(
                                bench::calibrationAssetKindName(
                                    calibrationAssetKinds[kindIndex]).data(),
                                isSelected)) {
                            sandboxCalibrationAssetKindIndex_
                                = static_cast<int>(kindIndex);
                        }
                        if (isSelected) ImGui::SetItemDefaultFocus();
                    }
                    ImGui::EndCombo();
                }
                ImGui::InputText(
                    "Calibration ID",
                    sandboxCalibrationAssetId_,
                    sizeof(sandboxCalibrationAssetId_));
                ImGui::InputInt(
                    "Asset format version",
                    &sandboxCalibrationAssetFormatVersion_);
                ImGui::InputText(
                    "Asset source / URI",
                    sandboxCalibrationAssetSource_,
                    sizeof(sandboxCalibrationAssetSource_));
                ImGui::InputText(
                    "Content SHA-256",
                    sandboxCalibrationAssetSha256_,
                    sizeof(sandboxCalibrationAssetSha256_));
                ImGui::DragFloat(
                    "Minimum wavelength (nm)",
                    &sandboxCalibrationMinimumWavelengthNanometres_,
                    1.0F,
                    0.001F,
                    1.0e9F,
                    "%.3f");
                ImGui::DragFloat(
                    "Maximum wavelength (nm)",
                    &sandboxCalibrationMaximumWavelengthNanometres_,
                    1.0F,
                    0.001F,
                    1.0e9F,
                    "%.3f");
                ImGui::DragFloat(
                    "Minimum temperature (K)",
                    &sandboxCalibrationMinimumTemperatureKelvin_,
                    0.1F,
                    0.001F,
                    1.0e6F,
                    "%.2f");
                ImGui::DragFloat(
                    "Maximum temperature (K)",
                    &sandboxCalibrationMaximumTemperatureKelvin_,
                    0.1F,
                    0.001F,
                    1.0e6F,
                    "%.2f");
                ImGui::BeginDisabled(
                    selected->instrument.calibrationAssets.size()
                    >= bench::kMaximumInstrumentCalibrationAssets);
                if (ImGui::Button("Attach calibration reference")) {
                    try {
                        auto candidate = benchProject_.scene;
                        auto edited = *candidate.find(
                            selectedBenchComponentId_);
                        edited.instrument.calibrationAssets.push_back({
                            .kind = calibrationAssetKinds[
                                static_cast<std::size_t>(
                                    sandboxCalibrationAssetKindIndex_)],
                            .calibrationId = sandboxCalibrationAssetId_,
                            .formatVersion
                                = sandboxCalibrationAssetFormatVersion_,
                            .source = sandboxCalibrationAssetSource_,
                            .contentSha256
                                = sandboxCalibrationAssetSha256_,
                            .specificationId
                                = edited.instrument.specificationId,
                            .specificationVersion
                                = edited.instrument.specificationVersion,
                            .validity = {
                                .minimumVacuumWavelengthMetres
                                    = static_cast<double>(
                                        sandboxCalibrationMinimumWavelengthNanometres_)
                                        * 1e-9,
                                .maximumVacuumWavelengthMetres
                                    = static_cast<double>(
                                        sandboxCalibrationMaximumWavelengthNanometres_)
                                        * 1e-9,
                                .minimumTemperatureKelvin
                                    = sandboxCalibrationMinimumTemperatureKelvin_,
                                .maximumTemperatureKelvin
                                    = sandboxCalibrationMaximumTemperatureKelvin_,
                            },
                        });
                        candidate.replace(edited.id, edited);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate),
                            "Attached calibration asset reference"));
                        sandboxCalibrationAssetId_[0] = '\0';
                        sandboxCalibrationAssetSource_[0] = '\0';
                        sandboxCalibrationAssetSha256_[0] = '\0';
                    } catch (const std::exception& error) {
                        errorMessage_ = "Calibration reference rejected: "
                            + std::string(error.what());
                        statusMessage_.clear();
                    }
                }
                ImGui::EndDisabled();
                ImGui::TextDisabled(
                    "References are persisted with a SHA-256 and validity domain. Edit by removing and reattaching; unresolved assets do not silently change physics.");

                ImGui::SeparatorText("Optical Alignment");
                const auto* alignmentTarget = benchProject_.scene.find(
                    sandboxAlignmentTargetComponentId_);
                if (alignmentTarget == nullptr
                    || alignmentTarget->id == selectedBenchComponentId_) {
                    sandboxAlignmentTargetComponentId_.clear();
                    alignmentTarget = nullptr;
                }
                const char* targetPreview = alignmentTarget == nullptr
                    ? "Choose target component"
                    : alignmentTarget->id.c_str();
                if (ImGui::BeginCombo("Alignment target", targetPreview)) {
                    for (const auto& component
                         : benchProject_.scene.components()) {
                        if (component.id == selectedBenchComponentId_) {
                            continue;
                        }
                        const bool isTarget
                            = sandboxAlignmentTargetComponentId_
                            == component.id;
                        const std::string label = component.id + "  ["
                            + std::string(
                                bench::benchComponentDisplayName(
                                    component.kind))
                            + "]";
                        if (ImGui::Selectable(label.c_str(), isTarget)) {
                            sandboxAlignmentTargetComponentId_ = component.id;
                        }
                        if (isTarget) {
                            ImGui::SetItemDefaultFocus();
                        }
                    }
                    ImGui::EndCombo();
                }
                ImGui::BeginDisabled(alignmentTarget == nullptr);
                if (ImGui::Button("Aim +Z at target")) {
                    applySandboxTargetAlignment(
                        SandboxAlignmentAction::AimAtTarget);
                }
                ImGui::SameLine();
                if (ImGui::Button("Make coaxial")) {
                    applySandboxTargetAlignment(
                        SandboxAlignmentAction::MakeCoaxial);
                }
                ImGui::SameLine();
                if (ImGui::Button("Match height")) {
                    applySandboxTargetAlignment(
                        SandboxAlignmentAction::MatchHeight);
                }
                ImGui::DragFloat(
                    "Target-axis spacing (mm)",
                    &sandboxAlignmentSpacingMillimetres_,
                    1.0F,
                    -5000.0F,
                    5000.0F,
                    "%.2f");
                if (ImGui::Button("Place at target-axis spacing")) {
                    applySandboxTargetAlignment(
                        SandboxAlignmentAction::PlaceAlongTargetAxis);
                }
                ImGui::EndDisabled();
                ImGui::DragFloat(
                    "Beam snap radius (mm)",
                    &sandboxBeamSnapDistanceMillimetres_,
                    1.0F,
                    0.01F,
                    5000.0F,
                    "%.2f");
                if (ImGui::Button("Snap to nearest visible beam")) {
                    snapSandboxSelectedToNearestBeam();
                }
                ImGui::TextDisabled(
                    "Alignment writes ordinary position/orientation edits; no hidden connection is created.");

                selected = benchProject_.scene.find(
                    selectedBenchComponentId_);
                if (selected == nullptr) {
                    throw std::runtime_error(
                        "selected component disappeared during alignment");
                }

                ImGui::SeparatorText("Physical Parameters");
                const auto commitParameters = [this](bench::BenchComponent edited) {
                    try {
                        auto candidate = benchProject_.scene;
                        candidate.replace(edited.id, edited);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate), "Updated component parameters"));
                    } catch (const std::exception& error) {
                        errorMessage_ = error.what();
                        statusMessage_.clear();
                    }
                };
                auto edited = *selected;
                bool changed = false;
                switch (edited.kind) {
                case bench::BenchComponentKind::LaserSource: {
                    auto value = std::get<bench::LaserSourceParameters>(edited.parameters);
                    float radiusMm = static_cast<float>(value.beamRadiusMetres * 1000.0);
                    changed |= ImGui::DragFloat("Beam radius (mm)", &radiusMm, 0.05F, 0.001F, 500.0F);
                    value.beamRadiusMetres = static_cast<double>(radiusMm) * 1e-3;
                    for (std::size_t index = 0; index < value.channels.size(); ++index) {
                        ImGui::PushID(static_cast<int>(index));
                        float wavelengthNm = static_cast<float>(value.channels[index].wavelengthMetres * 1e9);
                        float powerWatts = static_cast<float>(value.channels[index].powerWatts);
                        changed |= ImGui::DragFloat("Wavelength (nm)", &wavelengthNm, 1.0F, 200.0F, 2000.0F);
                        changed |= ImGui::DragFloat("Power (W)", &powerWatts, 0.01F, 0.0F, 1000.0F);
                        value.channels[index].wavelengthMetres = static_cast<double>(wavelengthNm) * 1e-9;
                        value.channels[index].powerWatts = static_cast<double>(powerWatts);
                        ImGui::TextDisabled("Coherence: %s", value.channels[index].coherenceId.c_str());
                        ImGui::PopID();
                    }
                    if (ImGui::Button("Set RGB channels")) {
                        value.channels = {
                            {.wavelengthMetres = 638e-9, .powerWatts = 0.30, .coherenceId = edited.id + "-red"},
                            {.wavelengthMetres = 532e-9, .powerWatts = 0.30, .coherenceId = edited.id + "-green"},
                            {.wavelengthMetres = 450e-9, .powerWatts = 0.30, .coherenceId = edited.id + "-blue"},
                        };
                        changed = true;
                    }
                    edited.parameters = std::move(value);
                    break;
                }
                case bench::BenchComponentKind::ObjectWavefrontSource: {
                    auto value = std::get<bench::ObjectWavefrontSourceParameters>(edited.parameters);
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    float wavelengthNm = static_cast<float>(value.channel.wavelengthMetres * 1e9);
                    float powerWatts = static_cast<float>(value.channel.powerWatts);
                    changed |= ImGui::DragFloat2("Source size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::DragFloat("Object wavelength (nm)", &wavelengthNm, 1.0F, 200.0F, 2000.0F);
                    changed |= ImGui::DragFloat("Object power (W)", &powerWatts, 0.01F, 0.0F, 1000.0F);
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    value.channel.wavelengthMetres = static_cast<double>(wavelengthNm) * 1e-9;
                    value.channel.powerWatts = static_cast<double>(powerWatts);
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::PlanarMirror: {
                    auto value = std::get<bench::PlanarMirrorParameters>(edited.parameters);
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    float reflectivity = static_cast<float>(value.powerReflectivity);
                    changed |= ImGui::DragFloat2("Mirror size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::SliderFloat("Power reflectivity", &reflectivity, 0.0F, 1.0F);
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    value.powerReflectivity = reflectivity;
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::BeamSplitterCombiner: {
                    auto value = std::get<bench::BeamSplitterParameters>(edited.parameters);
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    float reflectivity = static_cast<float>(value.powerReflectivity);
                    float transmissivity = static_cast<float>(value.powerTransmissivity);
                    changed |= ImGui::DragFloat2("Splitter size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::SliderFloat("Reflected power", &reflectivity, 0.0F, 1.0F);
                    changed |= ImGui::SliderFloat("Transmitted power", &transmissivity, 0.0F, 1.0F);
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    value.powerReflectivity = reflectivity;
                    value.powerTransmissivity = transmissivity;
                    edited.parameters = value;
                    ImGui::TextDisabled("Configured loss: %.3f", 1.0 - reflectivity - transmissivity);
                    break;
                }
                case bench::BenchComponentKind::IdealThinLens: {
                    auto value = std::get<bench::IdealThinLensParameters>(edited.parameters);
                    float focalMm = static_cast<float>(value.focalLengthMetres * 1000.0);
                    float apertureMm = static_cast<float>(value.clearApertureDiameterMetres * 1000.0);
                    changed |= ImGui::DragFloat("Focal length (mm)", &focalMm, 0.25F, -5000.0F, 5000.0F);
                    changed |= ImGui::DragFloat("Clear diameter (mm)", &apertureMm, 0.1F, 0.001F, 5000.0F);
                    value.focalLengthMetres = static_cast<double>(focalMm) * 1e-3;
                    value.clearApertureDiameterMetres = static_cast<double>(apertureMm) * 1e-3;
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::RealLensAssembly: {
                    auto value = std::get<bench::RealLensAssemblyParameters>(edited.parameters);
                    float apertureMm = static_cast<float>(value.clearApertureDiameterMetres * 1000.0);
                    changed |= ImGui::DragFloat("Assembly clear diameter (mm)", &apertureMm, 0.1F, 0.001F, 5000.0F);
                    value.clearApertureDiameterMetres = static_cast<double>(apertureMm) * 1e-3;
                    ImGui::TextDisabled("Prescription ID: %s", value.prescriptionId.c_str());
                    ImGui::BeginDisabled(
                        !activeLensPrescriptionAsset_.has_value());
                    if (ImGui::Button("Use verified imported prescription")) {
                        bindLensPrescriptionAsset(
                            edited, *activeLensPrescriptionAsset_);
                        value = std::get<bench::RealLensAssemblyParameters>(
                            edited.parameters);
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    const auto* resolved
                        = realLensPrescriptionCatalog_.resolve(
                            value.prescriptionId);
                    ImGui::TextDisabled(
                        resolved != nullptr
                            ? "Resolved: exact sequential centre ray + bounded scalar low-NA wave model"
                            : "Unresolved: import a matching JSON/CSV prescription");
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::Aperture: {
                    auto value = std::get<bench::ApertureParameters>(edited.parameters);
                    int shape = value.shape == bench::ApertureShape::Circular
                        ? 0
                        : value.shape == bench::ApertureShape::Rectangular
                            ? 1 : 2;
                    changed |= ImGui::RadioButton("Circular", &shape, 0);
                    ImGui::SameLine();
                    changed |= ImGui::RadioButton("Rectangular", &shape, 1);
                    ImGui::SameLine();
                    changed |= ImGui::RadioButton("Double slit", &shape, 2);
                    value.shape = shape == 0
                        ? bench::ApertureShape::Circular
                        : shape == 1
                            ? bench::ApertureShape::Rectangular
                            : bench::ApertureShape::DoubleSlit;
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    changed |= ImGui::DragFloat2(
                        value.shape == bench::ApertureShape::DoubleSlit
                            ? "Mount size (mm)" : "Opening size (mm)",
                        sizeMm, 0.1F, 0.001F, 5000.0F);
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    if (value.shape == bench::ApertureShape::DoubleSlit) {
                        float slitMicrometres[3] {
                            static_cast<float>(value.slitWidthMetres * 1e6),
                            static_cast<float>(value.slitHeightMetres * 1e6),
                            static_cast<float>(value.slitSeparationMetres * 1e6),
                        };
                        changed |= ImGui::DragFloat3(
                            "Slit width / height / centre spacing (um)",
                            slitMicrometres, 1.0F, 0.1F, 100000.0F);
                        value.slitWidthMetres
                            = static_cast<double>(slitMicrometres[0]) * 1e-6;
                        value.slitHeightMetres
                            = static_cast<double>(slitMicrometres[1]) * 1e-6;
                        value.slitSeparationMetres
                            = static_cast<double>(slitMicrometres[2]) * 1e-6;
                    }
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::SpatialFilter: {
                    auto value = std::get<bench::SpatialFilterParameters>(edited.parameters);
                    float focalMm = static_cast<float>(value.focalLengthMetres * 1000.0);
                    float pinholeUm = static_cast<float>(value.pinholeDiameterMetres * 1e6);
                    float apertureMm = static_cast<float>(value.clearApertureDiameterMetres * 1000.0);
                    changed |= ImGui::DragFloat("Filter focal length (mm)", &focalMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::DragFloat("Pinhole diameter (um)", &pinholeUm, 0.5F, 0.001F, 100000.0F);
                    changed |= ImGui::DragFloat("Filter clear diameter (mm)", &apertureMm, 0.1F, 0.001F, 5000.0F);
                    value.focalLengthMetres = static_cast<double>(focalMm) * 1e-3;
                    value.pinholeDiameterMetres = static_cast<double>(pinholeUm) * 1e-6;
                    value.clearApertureDiameterMetres = static_cast<double>(apertureMm) * 1e-3;
                    edited.parameters = value;
                    break;
                }
                case bench::BenchComponentKind::SpatialLightModulator: {
                    auto value = std::get<bench::SpatialLightModulatorParameters>(edited.parameters);
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    int pixels[2] {static_cast<int>(value.pixelWidth), static_cast<int>(value.pixelHeight)};
                    float fill = static_cast<float>(value.fillFactor);
                    changed |= ImGui::DragFloat2("SLM size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::InputInt2("SLM pixels", pixels);
                    changed |= ImGui::SliderFloat("SLM fill factor", &fill, 0.001F, 1.0F);
                    int modulationMode = value.modulationMode
                            == bench::SlmModulationMode::Amplitude
                        ? 0
                        : 1;
                    constexpr std::array<const char*, 2> modulationNames {
                        "Amplitude", "Phase"};
                    changed |= ImGui::Combo(
                        "SLM modulation",
                        &modulationMode,
                        modulationNames.data(),
                        static_cast<int>(modulationNames.size()));
                    value.modulationMode = modulationMode == 0
                        ? bench::SlmModulationMode::Amplitude
                        : bench::SlmModulationMode::Phase;
                    int commandPattern = static_cast<int>(value.commandPattern);
                    constexpr std::array<const char*, 3> patternNames {
                        "Uniform", "Linear ramp", "Checkerboard"};
                    changed |= ImGui::Combo(
                        "Command pattern",
                        &commandPattern,
                        patternNames.data(),
                        static_cast<int>(patternNames.size()));
                    value.commandPattern
                        = static_cast<bench::SlmCommandPattern>(commandPattern);
                    float primary = static_cast<float>(value.primaryCommand);
                    changed |= ImGui::SliderFloat(
                        value.commandPattern == bench::SlmCommandPattern::LinearRamp
                            ? "Ramp offset command"
                            : "Primary command",
                        &primary,
                        0.0F,
                        1.0F);
                    value.primaryCommand = primary;
                    if (value.commandPattern
                        == bench::SlmCommandPattern::LinearRamp) {
                        changed |= ImGui::InputDouble(
                            "Horizontal cycles",
                            &value.horizontalCycles,
                            0.25,
                            1.0,
                            "%.8g");
                        changed |= ImGui::InputDouble(
                            "Vertical cycles",
                            &value.verticalCycles,
                            0.25,
                            1.0,
                            "%.8g");
                    } else if (value.commandPattern
                        == bench::SlmCommandPattern::Checkerboard) {
                        float secondary
                            = static_cast<float>(value.secondaryCommand);
                        changed |= ImGui::SliderFloat(
                            "Secondary command", &secondary, 0.0F, 1.0F);
                        value.secondaryCommand = secondary;
                        int cells[2] {
                            static_cast<int>(
                                value.checkerboardCellWidthPixels),
                            static_cast<int>(
                                value.checkerboardCellHeightPixels),
                        };
                        changed |= ImGui::InputInt2(
                            "Checker cell pixels", cells);
                        value.checkerboardCellWidthPixels
                            = static_cast<std::size_t>(std::max(cells[0], 0));
                        value.checkerboardCellHeightPixels
                            = static_cast<std::size_t>(std::max(cells[1], 0));
                    }
                    int bitDepth = static_cast<int>(value.bitDepth);
                    changed |= ImGui::InputInt(
                        "Command bit depth (0 = continuous)", &bitDepth);
                    value.bitDepth = static_cast<unsigned int>(
                        std::max(bitDepth, 0));
                    if (value.modulationMode
                        == bench::SlmModulationMode::Phase) {
                        changed |= ImGui::InputDouble(
                            "Phase range (rad)",
                            &value.phaseRangeRadians,
                            0.1,
                            1.0,
                            "%.8g");
                    }
                    ImGui::TextDisabled(
                        "Command %s | origin: %s",
                        value.commandId.c_str(),
                        value.commandOrigin == bench::SlmCommandOrigin::Manual
                            ? "manual"
                            : "automation");
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    value.pixelWidth = static_cast<std::size_t>(std::max(pixels[0], 0));
                    value.pixelHeight = static_cast<std::size_t>(std::max(pixels[1], 0));
                    value.fillFactor = fill;
                    edited.parameters = value;
                    ImGui::InputText(
                        "Placed SLM response JSON",
                        placedSlmResponsePathBuffer_,
                        sizeof(placedSlmResponsePathBuffer_));
                    if (ImGui::Button("Load verified placed SLM response")) {
                        loadSlmResponseForBench();
                    }
                    ImGui::BeginDisabled(
                        !activeSlmResponseAsset_.has_value());
                    if (ImGui::Button("Use verified placed SLM response")) {
                        const auto& response
                            = activeSlmResponseAsset_->response;
                        bindSlmResponseAsset(
                            edited,
                            *activeSlmResponseAsset_,
                            {
                                .minimumVacuumWavelengthMetres
                                    = response.wavelengths().front()
                                        .vacuumWavelengthMetres,
                                .maximumVacuumWavelengthMetres
                                    = response.wavelengths().back()
                                        .vacuumWavelengthMetres,
                                .minimumTemperatureKelvin
                                    = sandboxCalibrationMinimumTemperatureKelvin_,
                                .maximumTemperatureKelvin
                                    = sandboxCalibrationMaximumTemperatureKelvin_,
                            });
                        changed = true;
                    }
                    ImGui::EndDisabled();
                    if (activeSlmResponseAsset_.has_value()) {
                        ImGui::TextDisabled(
                            "Loaded placed response: %.20s... | SHA-256 %.12s...",
                            activeSlmResponseAsset_->calibrationId.c_str(),
                            activeSlmResponseAsset_->provenance
                                .contentSha256.c_str());
                    }
                    break;
                }
                case bench::BenchComponentKind::ScreenDetector:
                case bench::BenchComponentKind::FieldProbe: {
                    double width = 0.0;
                    double height = 0.0;
                    std::size_t samplesX = 0;
                    std::size_t samplesY = 0;
                    if (edited.kind == bench::BenchComponentKind::ScreenDetector) {
                        const auto& value = std::get<bench::ScreenDetectorParameters>(edited.parameters);
                        width = value.widthMetres; height = value.heightMetres;
                        samplesX = value.sampleWidth; samplesY = value.sampleHeight;
                    } else {
                        const auto& value = std::get<bench::FieldProbeParameters>(edited.parameters);
                        width = value.widthMetres; height = value.heightMetres;
                        samplesX = value.sampleWidth; samplesY = value.sampleHeight;
                    }
                    float sizeMm[2] {static_cast<float>(width * 1000.0), static_cast<float>(height * 1000.0)};
                    int samples[2] {static_cast<int>(samplesX), static_cast<int>(samplesY)};
                    changed |= ImGui::DragFloat2("Plane size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::InputInt2("Samples", samples);
                    if (edited.kind == bench::BenchComponentKind::ScreenDetector) {
                        edited.parameters = bench::ScreenDetectorParameters {
                            .widthMetres = static_cast<double>(sizeMm[0]) * 1e-3,
                            .heightMetres = static_cast<double>(sizeMm[1]) * 1e-3,
                            .sampleWidth = static_cast<std::size_t>(std::max(samples[0], 0)),
                            .sampleHeight = static_cast<std::size_t>(std::max(samples[1], 0)),
                        };
                        ImGui::InputText(
                            "Detector response JSON",
                            detectorResponsePathBuffer_,
                            sizeof(detectorResponsePathBuffer_));
                        if (ImGui::Button("Load verified detector response")) {
                            loadDetectorResponseForBench();
                        }
                        ImGui::BeginDisabled(
                            !activeDetectorResponseAsset_.has_value());
                        if (ImGui::Button("Use verified detector response")) {
                            const auto& response
                                = activeDetectorResponseAsset_->response;
                            bindDetectorResponseAsset(
                                edited,
                                *activeDetectorResponseAsset_,
                                {
                                    .minimumVacuumWavelengthMetres
                                        = response.points().front()
                                            .vacuumWavelengthMetres,
                                    .maximumVacuumWavelengthMetres
                                        = response.points().back()
                                            .vacuumWavelengthMetres,
                                    .minimumTemperatureKelvin
                                        = sandboxCalibrationMinimumTemperatureKelvin_,
                                    .maximumTemperatureKelvin
                                        = sandboxCalibrationMaximumTemperatureKelvin_,
                                });
                            changed = true;
                        }
                        ImGui::EndDisabled();
                        if (activeDetectorResponseAsset_.has_value()) {
                            ImGui::TextDisabled(
                                "Loaded response: %s | SHA-256 %.12s...",
                                activeDetectorResponseAsset_->response
                                    .calibrationId().c_str(),
                                activeDetectorResponseAsset_->provenance
                                    .contentSha256.c_str());
                        }
                    } else {
                        edited.parameters = bench::FieldProbeParameters {
                            .widthMetres = static_cast<double>(sizeMm[0]) * 1e-3,
                            .heightMetres = static_cast<double>(sizeMm[1]) * 1e-3,
                            .sampleWidth = static_cast<std::size_t>(std::max(samples[0], 0)),
                            .sampleHeight = static_cast<std::size_t>(std::max(samples[1], 0)),
                        };
                    }
                    ImGui::TextWrapped(
                        edited.kind == bench::BenchComponentKind::FieldProbe
                            ? "Virtual, non-destructive light-field plane: select it and enable Observe light field above the 3D viewport, then move or rotate it to scan space."
                            : "Physical white Screen / Detector: intercepts the routed beam and displays the sampled intensity on the placed plane.");
                    break;
                }
                case bench::BenchComponentKind::HolographicPlate: {
                    auto value = std::get<bench::HolographicPlateParameters>(edited.parameters);
                    float sizeMm[2] {static_cast<float>(value.widthMetres * 1000.0), static_cast<float>(value.heightMetres * 1000.0)};
                    float thicknessUm = static_cast<float>(value.thicknessMetres * 1e6);
                    int role = value.role == bench::HolographicPlateRole::H1 ? 0 : 1;
                    changed |= ImGui::DragFloat2("Plate size (mm)", sizeMm, 0.1F, 0.001F, 5000.0F);
                    changed |= ImGui::DragFloat("Plate thickness (um)", &thicknessUm, 0.1F, 0.001F, 1000000.0F);
                    changed |= ImGui::RadioButton("H1 role", &role, 0);
                    ImGui::SameLine();
                    changed |= ImGui::RadioButton("H2 role", &role, 1);
                    value.widthMetres = static_cast<double>(sizeMm[0]) * 1e-3;
                    value.heightMetres = static_cast<double>(sizeMm[1]) * 1e-3;
                    value.thicknessMetres = static_cast<double>(thicknessUm) * 1e-6;
                    value.role = role == 0 ? bench::HolographicPlateRole::H1 : bench::HolographicPlateRole::H2;
                    edited.parameters = value;
                    break;
                }
                }
                if (changed) {
                    commitParameters(std::move(edited));
                }
            }

            selected = benchProject_.scene.find(selectedBenchComponentId_);
            if (selected != nullptr
                && (selected->kind == bench::BenchComponentKind::ScreenDetector
                    || selected->kind
                        == bench::BenchComponentKind::FieldProbe)) {
                ImGui::SeparatorText("Complex Field Measurement");
                bool samplingLimitChanged = false;
                ImGui::SetNextItemWidth(96.0F);
                samplingLimitChanged |= ImGui::Combo(
                    "Drag max axis",
                    &sandboxWavePreviewSampleLimitIndex_,
                    kWavePreviewSampleLimitLabels.data(),
                    static_cast<int>(
                        kWavePreviewSampleLimitLabels.size()));
                ImGui::SameLine();
                ImGui::SetNextItemWidth(96.0F);
                samplingLimitChanged |= ImGui::Combo(
                    "Settled max axis",
                    &sandboxWaveCommittedSampleLimitIndex_,
                    kWaveCommittedSampleLimitLabels.data(),
                    static_cast<int>(
                        kWaveCommittedSampleLimitLabels.size()));
                if (samplingLimitChanged) {
                    sandboxWaveObservations_.clear();
                    sandboxWaveChannelIndex_ = 0;
                    sandboxWaveObservationDiagnostic_.clear();
                    sandboxWaveTextureDirty_ = true;
                    if (sandboxWaveTexture_) {
                        sandboxWaveTexture_->destroy();
                    }
                }
                ImGui::TextDisabled(
                    "Each branch uses a 2x-padded working grid; lower drag quality keeps a moving probe responsive without changing settled quality.");
                const auto* waveObservation
                    = activeSandboxWaveObservation();
                const bool measurementCurrent = waveObservation
                    && !waveObservation->isStaleFor(
                        benchProject_.scene)
                    && waveObservation->observationComponentId
                        == selected->id;
                if (!measurementCurrent) {
                    ImGui::TextDisabled(
                        "Enable Observe light field and provide fully supported routed branches to this selected plane.");
                } else {
                    const auto& observationResult
                        = *waveObservation;
                    const auto& measuredField
                        = observationResult.fieldAtObservation;
                    if (sandboxWaveMeasurementComponentId_ != selected->id) {
                        sandboxWaveMeasurementComponentId_ = selected->id;
                        sandboxWaveCursorX_ = static_cast<int>(
                            measuredField.width() / 2U);
                        sandboxWaveCursorY_ = static_cast<int>(
                            measuredField.height() / 2U);
                    }
                    sandboxWaveCursorX_ = std::clamp(
                        sandboxWaveCursorX_,
                        0,
                        static_cast<int>(measuredField.width() - 1U));
                    sandboxWaveCursorY_ = std::clamp(
                        sandboxWaveCursorY_,
                        0,
                        static_cast<int>(measuredField.height() - 1U));

                    ImGui::Text(
                        "Channel: %.3f nm | coherence %s | revision %llu",
                        measuredField.vacuumWavelengthMetres() * 1e9,
                        observationResult.coherenceId.c_str(),
                        static_cast<unsigned long long>(
                            observationResult.sourceRevision));
                    ImGui::Text(
                        "Contributing coherent branches: %zu",
                        observationResult.contributions.size());
                    ImGui::Text(
                        "Peak %.7g W/m^2 | integrated %.7g W",
                        observationResult
                            .peakIntensityWattsPerSquareMetre,
                        observationResult.integratedPowerWatts);
                    for (const auto& contribution
                         : observationResult.contributions) {
                        ImGui::PushID(&contribution);
                        const bool routeOpen = ImGui::TreeNode(
                            "Route evidence",
                            "Branch %llu | source %s | OPL %.7g m",
                            static_cast<unsigned long long>(
                                contribution.branchId),
                            contribution.sourceComponentId.c_str(),
                            contribution.accumulatedOpticalPathMetres);
                        if (routeOpen) {
                            std::string completePath
                                = contribution.sourceComponentId;
                            const std::string interactionPath
                                = joinedBenchIdentifiers(
                                    contribution.pathComponentIds);
                            if (!interactionPath.empty()) {
                                completePath += " -> ";
                                completePath += interactionPath;
                            }
                            ImGui::TextWrapped(
                                "Ordered path: %s",
                                completePath.c_str());
                            const auto& diagnostics
                                = contribution.pathSampling;
                            ImGui::Text(
                                "Working grid %zux%zu | %zu ASM segments",
                                diagnostics.workingSampleWidth,
                                diagnostics.workingSampleHeight,
                                diagnostics.propagatedSegmentCount);
                            ImGui::Text(
                                "Folded %s | tilted element %s | tilted target %s | boundary %s",
                                diagnostics.usedFoldedPath ? "yes" : "no",
                                diagnostics.usedTiltedElementProjection
                                    ? "yes" : "no",
                                diagnostics.usedTargetTangentProjection
                                    ? "yes" : "no",
                                diagnostics.supportTouchesBoundary
                                    ? "touched" : "clear");
                            if (!diagnostics.appliedWaveComponentIds.empty()) {
                                const std::string applied
                                    = joinedBenchIdentifiers(
                                        diagnostics.appliedWaveComponentIds);
                                ImGui::TextWrapped(
                                    "Applied wave elements: %s",
                                    applied.c_str());
                            }
                            if (!diagnostics.foldedWaveComponentIds.empty()) {
                                const std::string folds
                                    = joinedBenchIdentifiers(
                                        diagnostics.foldedWaveComponentIds);
                                ImGui::TextWrapped(
                                    "Field-frame folds: %s",
                                    folds.c_str());
                            }
                            if (!diagnostics.appliedSlmCommandIds.empty()) {
                                const std::string commands
                                    = joinedBenchIdentifiers(
                                        diagnostics.appliedSlmCommandIds);
                                ImGui::TextWrapped(
                                    "SLM commands: %s",
                                    commands.c_str());
                            }
                            if (!diagnostics
                                     .appliedRealLensPrescriptionIds.empty()) {
                                const std::string prescriptions
                                    = joinedBenchIdentifiers(
                                        diagnostics
                                            .appliedRealLensPrescriptionIds);
                                ImGui::TextWrapped(
                                    "Real-lens prescriptions: %s",
                                    prescriptions.c_str());
                            }
                            if (!diagnostics
                                     .appliedSlmCalibrationIds.empty()) {
                                const std::string calibrations
                                    = joinedBenchIdentifiers(
                                        diagnostics
                                            .appliedSlmCalibrationIds);
                                ImGui::TextWrapped(
                                    "SLM calibrations: %s",
                                    calibrations.c_str());
                            }
                            for (const auto& warning
                                 : diagnostics.warnings) {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.72F, 0.25F, 1.0F),
                                    "Warning:");
                                ImGui::SameLine();
                                ImGui::TextWrapped(
                                    "%s", warning.c_str());
                            }
                            ImGui::TreePop();
                        }
                        ImGui::PopID();
                    }
                    ImGui::SliderInt(
                        "Sample X",
                        &sandboxWaveCursorX_,
                        0,
                        static_cast<int>(measuredField.width() - 1U));
                    ImGui::SliderInt(
                        "Sample Y",
                        &sandboxWaveCursorY_,
                        0,
                        static_cast<int>(measuredField.height() - 1U));
                    if (ImGui::DragFloat(
                            "dB display floor",
                            &sandboxWaveDecibelFloor_,
                            1.0F,
                            -160.0F,
                            -1.0F,
                            "%.0f dB")) {
                        sandboxWaveTextureDirty_ = true;
                    }
                    if (ImGui::DragFloat(
                            "Phase validity / peak",
                            &sandboxWavePhaseMinimumPeakFraction_,
                            1.0e-6F,
                            0.0F,
                            1.0F,
                            "%.6g")) {
                        sandboxWaveTextureDirty_ = true;
                    }

                    try {
                        const auto sample = measureBenchWaveSample(
                            observationResult,
                            static_cast<std::size_t>(sandboxWaveCursorX_),
                            static_cast<std::size_t>(sandboxWaveCursorY_),
                            observationResult
                                    .peakIntensityWattsPerSquareMetre
                                * static_cast<double>(
                                    sandboxWavePhaseMinimumPeakFraction_),
                            sandboxWaveDecibelFloor_);
                        ImGui::Text(
                            "Position: x %.6f mm | y %.6f mm",
                            sample.xMetres * 1e3,
                            sample.yMetres * 1e3);
                        ImGui::Text(
                            "U = %.7g %+.7gi | |U| %.7g",
                            sample.complexAmplitude.real(),
                            sample.complexAmplitude.imag(),
                            sample.amplitudeMagnitude);
                        ImGui::Text(
                            "I %.7g W/m^2 | %.3f dB re peak",
                            sample.intensityWattsPerSquareMetre,
                            sample.decibelsRelativeToPeak);
                        if (sample.phaseValid) {
                            ImGui::Text(
                                "Wrapped phase %.7g rad",
                                sample.wrappedPhaseRadians);
                        } else {
                            ImGui::TextColored(
                                ImVec4(1.0F, 0.72F, 0.25F, 1.0F),
                                "Phase undefined below the selected intensity threshold");
                        }

                        ImGui::RadioButton(
                            "Horizontal X section",
                            &sandboxWaveCrossSectionAxisIndex_,
                            0);
                        ImGui::SameLine();
                        ImGui::RadioButton(
                            "Vertical Y section",
                            &sandboxWaveCrossSectionAxisIndex_,
                            1);
                        const auto sectionAxis
                            = sandboxWaveCrossSectionAxisIndex_ == 0
                            ? BenchFieldCrossSectionAxis::HorizontalX
                            : BenchFieldCrossSectionAxis::VerticalY;
                        const auto section = measureBenchWaveCrossSection(
                            observationResult,
                            sectionAxis,
                            static_cast<std::size_t>(
                                sandboxWaveCrossSectionAxisIndex_ == 0
                                    ? sandboxWaveCursorY_
                                    : sandboxWaveCursorX_));
                        std::vector<float> normalizedSection;
                        normalizedSection.reserve(
                            section.intensitiesWattsPerSquareMetre.size());
                        for (const double intensity
                             : section.intensitiesWattsPerSquareMetre) {
                            normalizedSection.push_back(static_cast<float>(
                                intensity
                                / observationResult
                                    .peakIntensityWattsPerSquareMetre));
                        }
                        ImGui::PlotLines(
                            "##bench-field-cross-section",
                            normalizedSection.data(),
                            static_cast<int>(normalizedSection.size()),
                            0,
                            "Normalized intensity",
                            0.0F,
                            1.0F,
                            ImVec2(0.0F, 90.0F));
                        ImGui::TextDisabled(
                            "%+.4f to %+.4f mm | %zu physical samples",
                            section.coordinatesMetres.front() * 1e3,
                            section.coordinatesMetres.back() * 1e3,
                            section.coordinatesMetres.size());
                    } catch (const std::exception& error) {
                        ImGui::TextColored(
                            ImVec4(1.0F, 0.45F, 0.35F, 1.0F),
                            "Measurement rejected: %s",
                            error.what());
                    }
                }
            }

            selected = benchProject_.scene.find(selectedBenchComponentId_);
            if (selected != nullptr
                && (selected->kind == bench::BenchComponentKind::ScreenDetector
                    || selected->kind == bench::BenchComponentKind::FieldProbe
                    || selected->kind == bench::BenchComponentKind::HolographicPlate)) {
                ImGui::SeparatorText("Incident Branches");
                std::size_t incidentCount = 0;
                double totalIncidentPowerWatts = 0.0;
                for (const auto& interaction : benchTraceGraph_.interactions) {
                    if (interaction.componentId == selected->id) {
                        ++incidentCount;
                        totalIncidentPowerWatts += interaction.incidentBeam.powerWatts;
                    }
                }
                ImGui::Text(
                    "%zu branch(es), %.6g W total | trace revision %llu",
                    incidentCount,
                    totalIncidentPowerWatts,
                    static_cast<unsigned long long>(benchTraceGraph_.sourceRevision));
                if (incidentCount == 0U) {
                    ImGui::TextDisabled(
                        "No traced centre branch currently reaches this plane.");
                } else {
                    constexpr std::size_t kMaximumDisplayedIncidentBranches = 16U;
                    std::size_t displayedCount = 0;
                    for (const auto& interaction : benchTraceGraph_.interactions) {
                        if (interaction.componentId != selected->id) {
                            continue;
                        }
                        const auto& beam = interaction.incidentBeam;
                        const std::string sourceId
                            = beam.provenance.componentPath.empty()
                            ? std::string("unknown")
                            : beam.provenance.componentPath.front();
                        const auto* source = benchProject_.scene.find(sourceId);
                        const char* sourceRole = source != nullptr
                                && source->kind
                                    == bench::BenchComponentKind::ObjectWavefrontSource
                            ? "object source"
                            : "laser source";
                        ImGui::TextWrapped(
                            "#%llu | %.3f nm | %.6g W | path %.6g m | %s | coherence %s",
                            static_cast<unsigned long long>(beam.provenance.branchId),
                            beam.wavelengthMetres * 1e9,
                            beam.powerWatts,
                            beam.accumulatedOpticalPathMetres,
                            sourceRole,
                            beam.coherenceId.c_str());
                        if (++displayedCount == kMaximumDisplayedIncidentBranches) {
                            if (displayedCount < incidentCount) {
                                ImGui::TextDisabled(
                                    "%zu additional branches hidden",
                                    incidentCount - displayedCount);
                            }
                            break;
                        }
                    }
                    ImGui::TextDisabled(
                        "Centreline routing is immediate; committed plate sampling refines it into a local complex field.");
                    if (selected->kind == bench::BenchComponentKind::HolographicPlate) {
                        ImGui::SeparatorText("Recording Geometry Candidates");
                        ImGui::InputInt(
                            "Samples / axis", &sandboxPlateSampleSize_, 16, 64);
                        ImGui::InputFloat(
                            "Square analysis window (mm)",
                            &sandboxPlateWindowMillimetres_,
                            0.1F,
                            1.0F,
                            "%.4g");
                        ImGui::InputFloat(
                            "Relative I=1 (kW/m^2)",
                            &sandboxPlateRelativeReferenceKilowattsPerSquareMetre_,
                            1.0F,
                            10.0F,
                            "%.4g");
                        ImGui::TextDisabled(
                            "The analysis window is a labelled local patch inside the physical plate.");
                        if (ImGui::TreeNode("Volume recording material")) {
                            ImGui::InputFloat(
                                "Average refractive index",
                                &sandboxVolumeAverageRefractiveIndex_,
                                0.01F,
                                0.1F,
                                "%.5g");
                            ImGui::InputFloat(
                                "Index modulation (dn)",
                                &sandboxVolumeIndexModulation_,
                                0.001F,
                                0.01F,
                                "%.5g");
                            ImGui::InputFloat(
                                "Isotropic shrinkage (%)",
                                &sandboxVolumeShrinkagePercent_,
                                0.1F,
                                1.0F,
                                "%.4g");
                            ImGui::TextDisabled(
                                "Uniform, lossless scalar-TE sinusoidal grating; plate thickness comes from the placed component.");
                            ImGui::TreePop();
                        }
                        const auto fields
                            = optics::holography::collectPlateIncidentFields(
                                benchProject_.scene,
                                benchTraceGraph_,
                                selected->id);
                        std::size_t savedRecipeCount = 0U;
                        for (const auto& recipe
                             : benchProject_.recordingRecipes) {
                            if (recipe.plateComponentId == selected->id) {
                                ++savedRecipeCount;
                            }
                        }
                        if (savedRecipeCount > 0U) {
                            ImGui::SeparatorText("Saved Recording Recipes");
                            ImGui::TextDisabled(
                                "Recipes preserve branch identity and physical parameters; sampled fields are recomputed, not loaded as truth.");
                            for (const auto& recipe
                                 : benchProject_.recordingRecipes) {
                                if (recipe.plateComponentId != selected->id) {
                                    continue;
                                }
                                ImGui::PushID(recipe.recipeId.c_str());
                                const char* modelName
                                    = recipe.model
                                            == HologramRecordingModel::ThinTransmission
                                    ? (recipe.channels.size() == 3U
                                            ? "RGB thin transmission"
                                            : "Thin transmission")
                                    : "Volume grating";
                                ImGui::TextWrapped(
                                    "%s | %s | %zu channel(s) | %zux%zu over %.6g x %.6g mm",
                                    recipe.recipeId.c_str(),
                                    modelName,
                                    recipe.channels.size(),
                                    recipe.sampling.sampleWidth,
                                    recipe.sampling.sampleHeight,
                                    recipe.sampling.extentWidthMetres * 1e3,
                                    recipe.sampling.extentHeightMetres * 1e3);
                                try {
                                    const auto resolved
                                        = resolveRecordingRecipe(fields, recipe);
                                    ImGui::TextColored(
                                        ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                        "Resolvable against %zu current branch pair(s)",
                                        resolved.channels.size());
                                    if (ImGui::Button(
                                            "Recompute from saved recipe")) {
                                        try {
                                            recomputeRecordingRecipe(
                                                fields, recipe);
                                            errorMessage_.clear();
                                        } catch (const std::exception& error) {
                                            errorMessage_
                                                = "Saved recording recipe failed: "
                                                + std::string(error.what());
                                            statusMessage_.clear();
                                        }
                                    }
                                } catch (const std::exception& error) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                        "UNRESOLVED: %s",
                                        error.what());
                                    ImGui::TextDisabled(
                                        "The recipe will not silently switch to a different branch.");
                                }
                                ImGui::PopID();
                            }
                        }
                        std::size_t compatiblePairCount = 0;
                        for (const auto& object : fields.branches) {
                            if (object.role
                                != optics::holography::RecordingBranchRole::Object) {
                                continue;
                            }
                            for (const auto& reference : fields.branches) {
                                if (reference.role
                                        != optics::holography::RecordingBranchRole::Reference
                                    || !bench::canInterfere(
                                        object.beam, reference.beam)) {
                                    continue;
                                }
                                const auto pair
                                    = optics::holography::makePlateRecordingPair(
                                        fields,
                                        object.beam.provenance.branchId,
                                        reference.beam.provenance.branchId);
                                const char* geometry = pair.geometry
                                        == optics::holography::PlateRecordingGeometry::Transmission
                                    ? "Transmission"
                                    : "Reflection / Denisyuk";
                                ImGui::TextWrapped(
                                    "%s | %.3f nm | object #%llu + reference #%llu | OPD %.6g m | crossing %.3f deg",
                                    geometry,
                                    pair.wavelengthMetres * 1e9,
                                    static_cast<unsigned long long>(pair.objectBranchId),
                                    static_cast<unsigned long long>(pair.referenceBranchId),
                                    pair.signedOpticalPathDifferenceMetres,
                                    pair.crossingAngleRadians * 180.0
                                        / std::numbers::pi_v<double>);
                                const std::string pairWidgetId
                                    = "pair-"
                                    + std::to_string(pair.objectBranchId)
                                    + "-" + std::to_string(pair.referenceBranchId);
                                ImGui::PushID(pairWidgetId.c_str());
                                if (pair.geometry
                                        == optics::holography::PlateRecordingGeometry::Transmission
                                    && ImGui::Button("Record thin transmission")) {
                                    try {
                                        if (sandboxPlateSampleSize_ < 2
                                            || sandboxPlateSampleSize_ > 4096) {
                                            throw std::invalid_argument(
                                                "plate sample size must be in [2, 4096]");
                                        }
                                        if (!std::isfinite(
                                                sandboxPlateWindowMillimetres_)
                                            || sandboxPlateWindowMillimetres_ <= 0.0F) {
                                            throw std::invalid_argument(
                                                "plate analysis window must be positive");
                                        }
                                        if (!std::isfinite(
                                                sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
                                            || sandboxPlateRelativeReferenceKilowattsPerSquareMetre_
                                                <= 0.0F) {
                                            throw std::invalid_argument(
                                                "relative intensity reference must be positive");
                                        }
                                        optics::holography::ThinPlateRecordingOptions
                                            recordingOptions;
                                        const double extentMetres
                                            = static_cast<double>(
                                                sandboxPlateWindowMillimetres_)
                                            * 1e-3;
                                        recordingOptions.sampling = {
                                            .sampleWidth = static_cast<std::size_t>(
                                                sandboxPlateSampleSize_),
                                            .sampleHeight = static_cast<std::size_t>(
                                                sandboxPlateSampleSize_),
                                            .refractiveIndex = 1.0,
                                            .extentWidthMetres = extentMetres,
                                            .extentHeightMetres = extentMetres,
                                        };
                                        recordingOptions
                                            .relativeIntensityReferenceWattsPerSquareMetre
                                            = static_cast<double>(
                                                sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
                                            * 1e3;
                                        if (!detectorFftBackend_) {
                                            throw std::runtime_error(
                                                "CPU FFT backend is unavailable");
                                        }
                                        auto recording
                                            = optics::holography::recordThinTransmissionPlate(
                                                benchProject_.scene,
                                                fields,
                                                pair.objectBranchId,
                                                pair.referenceBranchId,
                                                recordingOptions,
                                                *detectorFftBackend_,
                                                &realLensPrescriptionCatalog_,
                                                &slmResponseCatalog_,
                                                293.15);
                                        field::FieldVisualizationOptions viewOptions;
                                        viewOptions.colormap = field::ColormapKind::Inferno;
                                        const auto image = field::renderLinearIntensity(
                                            recording.hologram.recordedRelativeIntensity,
                                            viewOptions);
                                        if (!sandboxPlateTexture_
                                            || !sandboxPlateTexture_->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the sampled plate exposure texture");
                                        }
                                        const std::array recipeChannels {
                                            optics::holography::
                                                PlateBranchPairSelection {
                                                    .objectBranchId
                                                        = pair.objectBranchId,
                                                    .referenceBranchId
                                                        = pair.referenceBranchId,
                                                },
                                        };
                                        upsertRecordingRecipe(
                                            benchProject_,
                                            makeThinRecordingRecipe(
                                                "thin-" + selected->id,
                                                fields,
                                                recipeChannels,
                                                recordingOptions));
                                        recordBenchEdit();
                                        sandboxPlateRecording_ = std::make_unique<
                                            optics::holography::ThinPlateRecordingResult>(
                                                std::move(recording));
                                        sandboxRecordedExperiment_
                                            = SandboxRecordedExperiment::ThinTransmission;
                                        sandboxActiveRecordingRecipeId_
                                            = "thin-" + selected->id;
                                        sandboxPlateReplay_.reset();
                                        if (sandboxReplayTexture_) {
                                            sandboxReplayTexture_->destroy();
                                        }
                                        errorMessage_.clear();
                                        statusMessage_
                                            = "Recorded thin transmission exposure on plate "
                                            + selected->id;
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "Plate recording failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                if (pair.geometry
                                    == optics::holography::PlateRecordingGeometry::Reflection) {
                                    if (ImGui::Button("Record volume reflection")) {
                                        try {
                                            if (!detectorFftBackend_) {
                                                throw std::runtime_error(
                                                    "CPU FFT backend is unavailable");
                                            }
                                            if (sandboxPlateSampleSize_ < 2
                                                || sandboxPlateSampleSize_ > 4096) {
                                                throw std::invalid_argument(
                                                    "plate sample size must be in [2, 4096]");
                                            }
                                            if (!std::isfinite(
                                                    sandboxPlateWindowMillimetres_)
                                                || sandboxPlateWindowMillimetres_
                                                    <= 0.0F) {
                                                throw std::invalid_argument(
                                                    "plate analysis window must be positive");
                                            }
                                            const double extentMetres
                                                = static_cast<double>(
                                                    sandboxPlateWindowMillimetres_)
                                                * 1e-3;
                                            const optics::holography::
                                                PlateFieldSamplingOptions sampling {
                                                    .sampleWidth
                                                        = static_cast<std::size_t>(
                                                            sandboxPlateSampleSize_),
                                                    .sampleHeight
                                                        = static_cast<std::size_t>(
                                                            sandboxPlateSampleSize_),
                                                    .refractiveIndex = 1.0,
                                                    .extentWidthMetres
                                                        = extentMetres,
                                                    .extentHeightMetres
                                                        = extentMetres,
                                                };
                                            const optics::holography::VolumePlateMaterial
                                                material {
                                                    .averageRefractiveIndex
                                                        = static_cast<double>(
                                                            sandboxVolumeAverageRefractiveIndex_),
                                                    .refractiveIndexModulation
                                                        = static_cast<double>(
                                                            sandboxVolumeIndexModulation_),
                                                    .isotropicLinearShrinkageFraction
                                                        = static_cast<double>(
                                                            sandboxVolumeShrinkagePercent_)
                                                        * 0.01,
                                                };
                                            auto recording
                                                = optics::holography::recordVolumePlate(
                                                    benchProject_.scene,
                                                    fields,
                                                    pair.objectBranchId,
                                                    pair.referenceBranchId,
                                                    material,
                                                    sampling,
                                                    *detectorFftBackend_,
                                                    {},
                                                    &realLensPrescriptionCatalog_,
                                                    &slmResponseCatalog_,
                                                    293.15);
                                            upsertRecordingRecipe(
                                                benchProject_,
                                                makeVolumeRecordingRecipe(
                                                    "volume-" + selected->id,
                                                    fields,
                                                    {
                                                        .objectBranchId
                                                            = pair.objectBranchId,
                                                        .referenceBranchId
                                                            = pair.referenceBranchId,
                                                    },
                                                    sampling,
                                                    material));
                                            recordBenchEdit();
                                            sandboxVolumeReplayWavelengthNanometres_
                                                = static_cast<float>(
                                                    recording.pair.wavelengthMetres
                                                    * 1e9);
                                            sandboxVolumeReplayAngleDegrees_
                                                = static_cast<float>(
                                                    recording
                                                        .equivalentSymmetricBraggAngleInMediumRadians
                                                    * 180.0
                                                    / std::numbers::pi_v<double>);
                                            sandboxVolumeRecording_ = std::make_unique<
                                                optics::holography::VolumePlateRecordingResult>(
                                                    std::move(recording));
                                            sandboxRecordedExperiment_
                                                = SandboxRecordedExperiment::ReflectionDenisyuk;
                                            sandboxActiveRecordingRecipeId_
                                                = "volume-" + selected->id;
                                            sandboxVolumeReplay_.reset();
                                            sandboxVolumeObservationReplay_.reset();
                                            if (sandboxVolumeReplayTexture_) {
                                                sandboxVolumeReplayTexture_->destroy();
                                            }
                                            errorMessage_.clear();
                                            statusMessage_
                                                = "Recorded volume reflection grating on plate "
                                                + selected->id;
                                        } catch (const std::exception& error) {
                                            errorMessage_
                                                = "Volume recording failed: "
                                                + std::string(error.what());
                                            statusMessage_.clear();
                                        }
                                    }
                                }
                                ImGui::PopID();
                                ++compatiblePairCount;
                            }
                        }
                        if (compatiblePairCount == 0U) {
                            ImGui::TextDisabled(
                                "No same-wavelength, same-coherence object/reference pair is present.");
                        }
                        ImGui::TextDisabled(
                            "Each wavelength is paired independently; RGB channels never cross-interfere.");
                        std::array<
                            optics::holography::PlateBranchPairSelection,
                            3> rgbSelections {};
                        bool hasRgbSelections = false;
                        try {
                            rgbSelections
                                = optics::holography::
                                    selectRgbThinTransmissionPairs(fields);
                            hasRgbSelections = true;
                        } catch (const std::exception&) {
                            hasRgbSelections = false;
                        }
                        if (hasRgbSelections) {
                            if (ImGui::Button("Record all RGB channels")) {
                                try {
                                    if (sandboxPlateSampleSize_ < 2
                                        || sandboxPlateSampleSize_ > 4096) {
                                        throw std::invalid_argument(
                                            "plate sample size must be in [2, 4096]");
                                    }
                                    if (!std::isfinite(
                                            sandboxPlateWindowMillimetres_)
                                        || sandboxPlateWindowMillimetres_
                                            <= 0.0F) {
                                        throw std::invalid_argument(
                                            "plate analysis window must be positive");
                                    }
                                    if (!std::isfinite(
                                            sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
                                        || sandboxPlateRelativeReferenceKilowattsPerSquareMetre_
                                            <= 0.0F) {
                                        throw std::invalid_argument(
                                            "relative intensity reference must be positive");
                                    }
                                    const double extentMetres
                                        = static_cast<double>(
                                            sandboxPlateWindowMillimetres_)
                                        * 1e-3;
                                    optics::holography::ThinPlateRecordingOptions
                                        options;
                                    options.sampling = {
                                        .sampleWidth = static_cast<std::size_t>(
                                            sandboxPlateSampleSize_),
                                        .sampleHeight = static_cast<std::size_t>(
                                            sandboxPlateSampleSize_),
                                        .refractiveIndex = 1.0,
                                        .extentWidthMetres = extentMetres,
                                        .extentHeightMetres = extentMetres,
                                    };
                                    options.relativeIntensityReferenceWattsPerSquareMetre
                                        = static_cast<double>(
                                            sandboxPlateRelativeReferenceKilowattsPerSquareMetre_)
                                        * 1e3;
                                    if (!detectorFftBackend_) {
                                        throw std::runtime_error(
                                            "CPU FFT backend is unavailable");
                                    }
                                    auto rgbRecording
                                        = optics::holography::
                                            recordRgbThinTransmissionPlate(
                                                benchProject_.scene,
                                                fields,
                                                rgbSelections,
                                                options,
                                                *detectorFftBackend_,
                                                &realLensPrescriptionCatalog_,
                                                &slmResponseCatalog_,
                                                293.15);
                                    upsertRecordingRecipe(
                                        benchProject_,
                                        makeThinRecordingRecipe(
                                            "rgb-" + selected->id,
                                            fields,
                                            rgbSelections,
                                            options));
                                    recordBenchEdit();
                                    sandboxRgbRecording_ = std::make_unique<
                                        optics::holography::
                                            RgbThinPlateRecordingResult>(
                                                std::move(rgbRecording));
                                    sandboxRecordedExperiment_
                                        = SandboxRecordedExperiment::RgbFullColour;
                                    sandboxActiveRecordingRecipeId_
                                        = "rgb-" + selected->id;
                                    sandboxRgbReplay_.reset();
                                    if (sandboxRgbReplayTexture_) {
                                        sandboxRgbReplayTexture_->destroy();
                                    }
                                    errorMessage_.clear();
                                    statusMessage_
                                        = "Recorded three independent RGB channels on plate "
                                        + selected->id;
                                } catch (const std::exception& error) {
                                    errorMessage_
                                        = "RGB plate recording failed: "
                                        + std::string(error.what());
                                    statusMessage_.clear();
                                }
                            }
                            ImGui::TextDisabled(
                                "RGB batch available: exactly three distinct compatible transmission pairs.");
                        }
                        if (sandboxRgbRecording_
                            && sandboxRgbRecording_->plateComponentId
                                == selected->id) {
                            ImGui::SeparatorText("Recorded RGB Exposure Set");
                            const bool stale = sandboxRgbRecording_->isStaleFor(
                                benchProject_.scene);
                            if (stale) {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                    "STALE RGB recording: bench revision changed.");
                            } else {
                                ImGui::TextColored(
                                    ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                    "Three current channels at revision %llu",
                                    static_cast<unsigned long long>(
                                        sandboxRgbRecording_->sourceRevision));
                            }
                            constexpr std::array<const char*, 3> kRgbNames {
                                "Red", "Green", "Blue"};
                            for (std::size_t channel = 0; channel < 3U;
                                 ++channel) {
                                const auto& recorded
                                    = sandboxRgbRecording_->channels[channel];
                                ImGui::TextWrapped(
                                    "%s: %.3f nm | object #%llu + reference #%llu | fringe %.6g cycles/m | exposure %.6g .. %.6g",
                                    kRgbNames[channel],
                                    recorded.pair.wavelengthMetres * 1e9,
                                    static_cast<unsigned long long>(
                                        recorded.pair.objectBranchId),
                                    static_cast<unsigned long long>(
                                        recorded.pair.referenceBranchId),
                                    std::hypot(
                                        recorded.diagnostics
                                            .fringeFrequencyXCyclesPerMetre,
                                        recorded.diagnostics
                                            .fringeFrequencyYCyclesPerMetre),
                                    recorded.hologram.diagnostics
                                        .minimumRecordedRelativeIntensity,
                                    recorded.hologram.diagnostics
                                        .maximumRecordedRelativeIntensity);
                            }

                            ImGui::SeparatorText("RGB Replay to Placed Observation");
                            bool hasRgbObservationComponent = false;
                            ImGui::BeginDisabled(stale);
                            for (const auto& component
                                 : benchProject_.scene.components()) {
                                if (component.kind
                                        != bench::BenchComponentKind::ScreenDetector
                                    && component.kind
                                        != bench::BenchComponentKind::FieldProbe) {
                                    continue;
                                }
                                hasRgbObservationComponent = true;
                                ImGui::PushID((
                                    "rgb-observer-" + component.id).c_str());
                                const std::string label
                                    = "Replay RGB to " + component.id;
                                if (ImGui::Button(label.c_str())) {
                                    try {
                                        if (!detectorFftBackend_) {
                                            throw std::runtime_error(
                                                "CPU FFT backend is unavailable");
                                        }
                                        const auto replayKind
                                            = sandboxPlateReplayKindIndex_ == 0
                                            ? optics::holography::
                                                ThinPlateReplayKind::OrdinaryReference
                                            : optics::holography::
                                                ThinPlateReplayKind::ConjugateReference;
                                        auto rgbReplay
                                            = optics::holography::
                                                replayRgbThinTransmissionToObservation(
                                                    benchProject_.scene,
                                                    *sandboxRgbRecording_,
                                                    component.id,
                                                    replayKind,
                                                    *detectorFftBackend_);
                                        const field::RgbIntensityVisualizationOptions
                                            displayOptions {
                                                .channelIntensityGains = {
                                                    static_cast<double>(
                                                        sandboxRgbDisplayGains_[0]),
                                                    static_cast<double>(
                                                        sandboxRgbDisplayGains_[1]),
                                                    static_cast<double>(
                                                        sandboxRgbDisplayGains_[2]),
                                                },
                                                .referenceIntensity = 0.0,
                                                .displayGamma = static_cast<double>(
                                                    sandboxRgbDisplayGamma_),
                                            };
                                        const auto image
                                            = field::renderUncalibratedRgbIntensity(
                                                rgbReplay.channels[0]
                                                    .fullReplayAtObservation,
                                                rgbReplay.channels[1]
                                                    .fullReplayAtObservation,
                                                rgbReplay.channels[2]
                                                    .fullReplayAtObservation,
                                                displayOptions);
                                        if (!sandboxRgbReplayTexture_
                                            || !sandboxRgbReplayTexture_
                                                ->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the RGB replay texture");
                                        }
                                        sandboxRgbReplay_ = std::make_unique<
                                            optics::holography::
                                                RgbThinPlateReplayResult>(
                                                    std::move(rgbReplay));
                                        sandboxRgbReplayViewIndex_ = 0;
                                        errorMessage_.clear();
                                        statusMessage_
                                            = "Replayed three independent spectral channels to "
                                            + component.id;
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "RGB plate replay failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndDisabled();
                            if (!hasRgbObservationComponent) {
                                ImGui::TextDisabled(
                                    "Place a Screen / Detector or Field Probe to observe RGB reconstruction.");
                            }
                            if (sandboxRgbReplay_
                                && sandboxRgbReplay_->plateComponentId
                                    == selected->id) {
                                if (sandboxRgbReplay_->isStaleFor(
                                        benchProject_.scene)) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                        "STALE RGB replay: bench revision changed.");
                                }
                                bool refreshRgbView = ImGui::InputFloat3(
                                    "RGB display gains",
                                    sandboxRgbDisplayGains_,
                                    "%.3f");
                                refreshRgbView |= ImGui::InputFloat(
                                    "RGB display gamma",
                                    &sandboxRgbDisplayGamma_,
                                    0.1F,
                                    0.5F,
                                    "%.3f");
                                constexpr std::array<const char*, 4> kRgbViews {
                                    "Combined RGB",
                                    "Red intensity",
                                    "Green intensity",
                                    "Blue intensity",
                                };
                                refreshRgbView |= ImGui::Combo(
                                    "RGB replay view",
                                    &sandboxRgbReplayViewIndex_,
                                    kRgbViews.data(),
                                    static_cast<int>(kRgbViews.size()));
                                if (refreshRgbView) {
                                    try {
                                        field::RgbaImage image = [&]() {
                                            if (sandboxRgbReplayViewIndex_ == 0) {
                                                return field::
                                                    renderUncalibratedRgbIntensity(
                                                        sandboxRgbReplay_->channels[0]
                                                            .fullReplayAtObservation,
                                                        sandboxRgbReplay_->channels[1]
                                                            .fullReplayAtObservation,
                                                        sandboxRgbReplay_->channels[2]
                                                            .fullReplayAtObservation,
                                                        {
                                                            .channelIntensityGains = {
                                                                static_cast<double>(
                                                                    sandboxRgbDisplayGains_[0]),
                                                                static_cast<double>(
                                                                    sandboxRgbDisplayGains_[1]),
                                                                static_cast<double>(
                                                                    sandboxRgbDisplayGains_[2]),
                                                            },
                                                            .referenceIntensity = 0.0,
                                                            .displayGamma
                                                                = static_cast<double>(
                                                                    sandboxRgbDisplayGamma_),
                                                        });
                                            }
                                            field::FieldVisualizationOptions
                                                options;
                                            options.colormap
                                                = field::ColormapKind::Inferno;
                                            const std::size_t channel
                                                = static_cast<std::size_t>(
                                                    sandboxRgbReplayViewIndex_ - 1);
                                            return field::renderLinearIntensity(
                                                sandboxRgbReplay_->channels[channel]
                                                    .fullReplayAtObservation,
                                                options);
                                        }();
                                        if (!sandboxRgbReplayTexture_
                                            || !sandboxRgbReplayTexture_
                                                ->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the RGB replay view");
                                        }
                                        errorMessage_.clear();
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "RGB replay visualization failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                ImGui::TextWrapped(
                                    "R/G/B propagated independently to %s; combined colour is an uncalibrated display transform, not coherent field addition.",
                                    sandboxRgbReplay_
                                        ->observationComponentId.c_str());
                                if (sandboxRgbReplayTexture_
                                    && sandboxRgbReplayTexture_->isValid()) {
                                    const float availableWidth
                                        = ImGui::GetContentRegionAvail().x;
                                    const float imageSize = std::clamp(
                                        availableWidth, 160.0F, 360.0F);
                                    ImGui::Image(
                                        toImTextureID(
                                            sandboxRgbReplayTexture_->handle()),
                                        ImVec2(imageSize, imageSize),
                                        ImVec2(0.0F, 1.0F),
                                        ImVec2(1.0F, 0.0F));
                                }
                            }
                        }
                        if (sandboxRgbVolumeRecording_
                            && sandboxRgbVolumeRecording_->plateComponentId
                                == selected->id) {
                            ImGui::SeparatorText(
                                "Recorded RGB Reflection Volume Set");
                            const bool stale
                                = sandboxRgbVolumeRecording_->isStaleFor(
                                    benchProject_.scene);
                            if (stale) {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                    "STALE RGB volume recording: bench revision changed.");
                            } else {
                                ImGui::TextColored(
                                    ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                    "Three current wavelength-separated channels at revision %llu",
                                    static_cast<unsigned long long>(
                                        sandboxRgbVolumeRecording_
                                            ->sourceRevision));
                            }
                            constexpr std::array<const char*, 3>
                                kRgbVolumeNames {"Red", "Green", "Blue"};
                            for (std::size_t channel = 0U; channel < 3U;
                                 ++channel) {
                                const auto& recorded
                                    = sandboxRgbVolumeRecording_
                                          ->channels[channel];
                                ImGui::TextWrapped(
                                    "%s: %.3f nm | object #%llu + reference #%llu | period %.6g nm | nominal efficiency %.3f%%",
                                    kRgbVolumeNames[channel],
                                    recorded.pair.wavelengthMetres * 1e9,
                                    static_cast<unsigned long long>(
                                        recorded.pair.objectBranchId),
                                    static_cast<unsigned long long>(
                                        recorded.pair.referenceBranchId),
                                    recorded.recordedGratingPeriodMetres * 1e9,
                                    recorded.nominalReplay.kogelnik
                                            .diffractionEfficiency
                                        * 100.0);
                            }

                            if (sandboxRgbVolumeReplay_
                                && sandboxRgbVolumeReplay_->plateComponentId
                                    == selected->id) {
                                ImGui::SeparatorText(
                                    "RGB Volume Reconstruction");
                                const auto& replay
                                    = *sandboxRgbVolumeReplay_;
                                if (replay.isStaleFor(benchProject_.scene)) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                        "STALE RGB reconstruction: bench revision changed.");
                                }
                                bool refreshRgbVolumeView = ImGui::InputFloat3(
                                    "RGB volume display gains##rgb-volume",
                                    sandboxRgbDisplayGains_,
                                    "%.3f");
                                refreshRgbVolumeView |= ImGui::InputFloat(
                                    "RGB volume display gamma##rgb-volume",
                                    &sandboxRgbDisplayGamma_,
                                    0.1F,
                                    0.5F,
                                    "%.3f");
                                if (refreshRgbVolumeView) {
                                    try {
                                        const auto image
                                            = field::renderUncalibratedRgbIntensity(
                                                replay.channels[0]
                                                    .reconstructedAtObservation,
                                                replay.channels[1]
                                                    .reconstructedAtObservation,
                                                replay.channels[2]
                                                    .reconstructedAtObservation,
                                                {
                                                    .channelIntensityGains = {
                                                        static_cast<double>(
                                                            sandboxRgbDisplayGains_[0]),
                                                        static_cast<double>(
                                                            sandboxRgbDisplayGains_[1]),
                                                        static_cast<double>(
                                                            sandboxRgbDisplayGains_[2]),
                                                    },
                                                    .referenceIntensity = 0.0,
                                                    .displayGamma
                                                        = static_cast<double>(
                                                            sandboxRgbDisplayGamma_),
                                                });
                                        if (!sandboxRgbReplayTexture_
                                            || !sandboxRgbReplayTexture_
                                                ->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the RGB volume replay view");
                                        }
                                        errorMessage_.clear();
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "RGB volume visualization failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                ImGui::TextWrapped(
                                    "R/G/B propagated independently from %s to %s; colour is composed only from display intensities.",
                                    replay.plateComponentId.c_str(),
                                    replay.observationComponentId.c_str());
                                for (std::size_t channel = 0U; channel < 3U;
                                     ++channel) {
                                    const auto& observation
                                        = replay.channels[channel];
                                    ImGui::TextColored(
                                        observation.usedRoutedWavePath
                                            ? ImVec4(
                                                  0.35F, 0.9F, 0.45F, 1.0F)
                                            : ImVec4(
                                                  0.55F, 0.75F, 1.0F, 1.0F),
                                        "%s %.3f nm: %s | %.6g W reconstructed",
                                        kRgbVolumeNames[channel],
                                        observation.reconstructedAtObservation
                                                .vacuumWavelengthMetres()
                                            * 1e9,
                                        observation.usedRoutedWavePath
                                            ? "placed routed path"
                                            : "direct plate/observation transfer",
                                        observation
                                            .reconstructedPowerOnSampledWindowWatts);
                                    if (!observation.usedRoutedWavePath) {
                                        continue;
                                    }
                                    const auto& routed
                                        = observation.routedWavePath;
                                    ImGui::TextWrapped(
                                        "%zu propagation segments | %zux%zu working grid%s%s",
                                        routed.propagatedSegmentCount,
                                        routed.workingSampleWidth,
                                        routed.workingSampleHeight,
                                        observation
                                                .usedSourcePlaneToBeamFrameRotation
                                            ? " | plate-to-beam rotation"
                                            : "",
                                        routed.usedTargetTangentProjection
                                            ? " | target-tangent projection"
                                            : "");
                                    if (observation
                                            .usedSourcePlaneToBeamFrameRotation) {
                                        const auto& rotation = observation
                                            .sourcePlaneToBeamFrameRotation;
                                        ImGui::TextWrapped(
                                            "Plate tangent -> beam-normal spectrum: %zu propagated, %zu evanescent, %zu outside source band, %zu opposite hemisphere, %zu interpolated bins",
                                            rotation.propagatingOutputBinCount,
                                            rotation.evanescentOutputBinCount,
                                            rotation.sourceBandRejectedBinCount,
                                            rotation.oppositeHemisphereBinCount,
                                            rotation.interpolatedOutputBinCount);
                                    }
                                    if (!routed.appliedWaveComponentIds
                                             .empty()) {
                                        const std::string components
                                            = joinedBenchIdentifiers(
                                                routed
                                                    .appliedWaveComponentIds);
                                        ImGui::TextWrapped(
                                            "Applied placed elements: %s",
                                            components.c_str());
                                    }
                                    if (!routed
                                             .appliedRealLensPrescriptionIds
                                             .empty()) {
                                        const std::string prescriptions
                                            = joinedBenchIdentifiers(
                                                routed
                                                    .appliedRealLensPrescriptionIds);
                                        ImGui::TextWrapped(
                                            "Applied real-lens prescriptions: %s",
                                            prescriptions.c_str());
                                    }
                                    if (routed.supportTouchesBoundary) {
                                        ImGui::TextColored(
                                            ImVec4(
                                                1.0F,
                                                0.65F,
                                                0.25F,
                                                1.0F),
                                            "Sampled support touches the routed boundary.");
                                    }
                                    for (const auto& warning
                                         : routed.warnings) {
                                        ImGui::TextWrapped(
                                            "Model warning: %s",
                                            warning.c_str());
                                    }
                                }
                                if (sandboxRgbReplayTexture_
                                    && sandboxRgbReplayTexture_->isValid()) {
                                    const float imageSize = std::clamp(
                                        ImGui::GetContentRegionAvail().x,
                                        160.0F,
                                        360.0F);
                                    ImGui::Image(
                                        toImTextureID(
                                            sandboxRgbReplayTexture_->handle()),
                                        ImVec2(imageSize, imageSize),
                                        ImVec2(0.0F, 1.0F),
                                        ImVec2(1.0F, 0.0F));
                                }
                            }
                        }
                        if (sandboxVolumeRecording_
                            && sandboxVolumeRecording_->plateComponentId
                                == selected->id) {
                            ImGui::SeparatorText("Recorded Volume Grating");
                            const bool stale = sandboxVolumeRecording_->isStaleFor(
                                benchProject_.scene);
                            if (stale) {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                    "STALE: bench revision changed; record again.");
                            } else {
                                ImGui::TextColored(
                                    ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                    "Current at revision %llu",
                                    static_cast<unsigned long long>(
                                        sandboxVolumeRecording_->sourceRevision));
                            }
                            const auto& recording = *sandboxVolumeRecording_;
                            const auto& grating
                                = recording.recordedGratingVectorLocalRadiansPerMetre;
                            ImGui::TextWrapped(
                                "%s | %.3f nm | period %.6g nm | slant %.4f deg",
                                recording.pair.geometry
                                        == optics::holography::PlateRecordingGeometry::Reflection
                                    ? "Reflection / Denisyuk"
                                    : "Transmission",
                                recording.pair.wavelengthMetres * 1e9,
                                recording.recordedGratingPeriodMetres * 1e9,
                                recording.gratingSlantFromPlateNormalRadians
                                    * 180.0 / std::numbers::pi_v<double>);
                            ImGui::TextWrapped(
                                "K local = (%.6g, %.6g, %.6g) rad/m | equivalent symmetric Bragg angle %.4f deg",
                                grating.x,
                                grating.y,
                                grating.z,
                                recording
                                    .equivalentSymmetricBraggAngleInMediumRadians
                                    * 180.0 / std::numbers::pi_v<double>);
                            ImGui::TextWrapped(
                                "Nominal replay: coupling %.6g | detuning %.6g | efficiency %.3f%%",
                                recording.nominalReplay.kogelnik.couplingStrength,
                                recording.nominalReplay.kogelnik.detuningParameter,
                                recording.nominalReplay.kogelnik.diffractionEfficiency
                                    * 100.0);
                            if (recording.objectIncident.has_value()
                                && recording.referenceIncident.has_value()) {
                                ImGui::TextColored(
                                    ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                    "Recorded sampled wavefront evidence retained");
                                const auto drawRecordedPath = [](
                                    const char* label,
                                    const optics::holography::
                                        SampledPlateIncidentField& incident) {
                                    const auto& path = incident.diagnostics;
                                    ImGui::Text(
                                        "%s field: %.7g W on window | %s",
                                        label,
                                        path.integratedPowerWatts,
                                        path.appliedLocalWavePath
                                            ? "shared sampled path"
                                            : "direct source envelope");
                                    if (!path.appliedWaveComponentIds.empty()) {
                                        const std::string elements
                                            = joinedBenchIdentifiers(
                                                path.appliedWaveComponentIds);
                                        ImGui::TextWrapped(
                                            "Applied %s elements: %s",
                                            label,
                                            elements.c_str());
                                    }
                                    if (!path.appliedRealLensPrescriptionIds
                                             .empty()) {
                                        const std::string prescriptions
                                            = joinedBenchIdentifiers(
                                                path.appliedRealLensPrescriptionIds);
                                        ImGui::TextWrapped(
                                            "%s real-lens prescriptions: %s",
                                            label,
                                            prescriptions.c_str());
                                    }
                                    for (const auto& warning : path.warnings) {
                                        ImGui::TextColored(
                                            ImVec4(1.0F, 0.68F, 0.25F, 1.0F),
                                            "%s: %s",
                                            label,
                                            warning.c_str());
                                    }
                                };
                                drawRecordedPath(
                                    "Object", *recording.objectIncident);
                                drawRecordedPath(
                                    "Reference", *recording.referenceIncident);
                            } else {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.68F, 0.25F, 1.0F),
                                    "Legacy geometric-only volume recording; record again to retain sampled wavefront evidence.");
                            }

                            ImGui::SeparatorText("Volume Replay");
                            ImGui::InputFloat(
                                "Replay wavelength (nm)",
                                &sandboxVolumeReplayWavelengthNanometres_,
                                1.0F,
                                10.0F,
                                "%.5g");
                            ImGui::InputFloat(
                                "Replay internal angle (deg)",
                                &sandboxVolumeReplayAngleDegrees_,
                                0.1F,
                                1.0F,
                                "%.5g");
                            ImGui::BeginDisabled(stale);
                            if (ImGui::Button("Evaluate volume replay")) {
                                try {
                                    auto replay
                                        = optics::holography::replayVolumePlate(
                                            benchProject_.scene,
                                            recording,
                                            static_cast<double>(
                                                sandboxVolumeReplayWavelengthNanometres_)
                                                * 1e-9,
                                            static_cast<double>(
                                                sandboxVolumeReplayAngleDegrees_)
                                                * std::numbers::pi_v<double>
                                                / 180.0);
                                    sandboxVolumeReplay_ = std::make_unique<
                                        optics::holography::VolumePlateReplayResult>(
                                            std::move(replay));
                                    errorMessage_.clear();
                                    statusMessage_
                                        = "Evaluated volume hologram replay";
                                } catch (const std::exception& error) {
                                    errorMessage_
                                        = "Volume replay failed: "
                                        + std::string(error.what());
                                    statusMessage_.clear();
                                }
                            }
                            ImGui::EndDisabled();
                            if (sandboxVolumeReplay_
                                && sandboxVolumeReplay_->plateComponentId
                                    == selected->id) {
                                const auto& volume = sandboxVolumeReplay_->volume;
                                ImGui::TextWrapped(
                                    "Replay period %.6g nm | thickness %.6g um | mismatch %.6g rad/m",
                                    volume.replayGratingPeriodMetres * 1e9,
                                    volume.replayThicknessMetres * 1e6,
                                    volume.phaseMismatchRadiansPerMetre);
                                if (volume.kogelnikEfficiencyEvaluated) {
                                    ImGui::TextWrapped(
                                        "Coupling %.6g | detuning %.6g | diffraction efficiency %.3f%% (exact-Bragg %.3f%%)",
                                        volume.kogelnik.couplingStrength,
                                        volume.kogelnik.detuningParameter,
                                        volume.kogelnik.diffractionEfficiency * 100.0,
                                        volume.exactBraggEfficiencyAtReplayCoupling
                                            * 100.0);
                                } else {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.65F, 0.25F, 1.0F),
                                    "Diffracted order is non-propagating; efficiency was not evaluated.");
                                }
                            }
                            ImGui::SeparatorText(
                                "Reconstruct to Placed Observation");
                            ImGui::TextDisabled(
                                "Uses recorded reference branch #%llu as the physical replay illumination. The reconstructed field follows ordinary placed Bench elements to the selected Screen/Probe; direct decenter and rotated planes retain explicit padded spectrum transforms.",
                                static_cast<unsigned long long>(
                                    recording.pair.referenceBranchId));
                            bool hasVolumeObservationComponent = false;
                            ImGui::BeginDisabled(stale);
                            for (const auto& component
                                 : benchProject_.scene.components()) {
                                if (component.kind
                                        != bench::BenchComponentKind::ScreenDetector
                                    && component.kind
                                        != bench::BenchComponentKind::FieldProbe) {
                                    continue;
                                }
                                hasVolumeObservationComponent = true;
                                ImGui::PushID((
                                    "volume-observer-" + component.id).c_str());
                                const std::string label
                                    = "Reconstruct on " + component.id;
                                if (ImGui::Button(label.c_str())) {
                                    try {
                                        if (!detectorFftBackend_) {
                                            throw std::runtime_error(
                                                "CPU FFT backend is unavailable");
                                        }
                                        if (sandboxPlateSampleSize_ < 2
                                            || sandboxPlateSampleSize_ > 4096) {
                                            throw std::invalid_argument(
                                                "plate sample size must be in [2, 4096]");
                                        }
                                        if (!std::isfinite(
                                                sandboxPlateWindowMillimetres_)
                                            || sandboxPlateWindowMillimetres_
                                                <= 0.0F) {
                                            throw std::invalid_argument(
                                                "plate analysis window must be positive");
                                        }
                                        const double extentMetres
                                            = static_cast<double>(
                                                sandboxPlateWindowMillimetres_)
                                            * 1e-3;
                                        const optics::holography::PlateFieldSamplingOptions
                                            sampling {
                                                .sampleWidth
                                                    = static_cast<std::size_t>(
                                                        sandboxPlateSampleSize_),
                                                .sampleHeight
                                                    = static_cast<std::size_t>(
                                                        sandboxPlateSampleSize_),
                                                .refractiveIndex = 1.0,
                                                .extentWidthMetres = extentMetres,
                                                .extentHeightMetres = extentMetres,
                                            };
                                        auto observationReplay
                                            = optics::holography::
                                                replayVolumeReflectionToObservation(
                                                    benchProject_.scene,
                                                    fields,
                                                    recording,
                                                    recording.pair.referenceBranchId,
                                                    component.id,
                                                    sampling,
                                                    *detectorFftBackend_,
                                                    &realLensPrescriptionCatalog_,
                                                    &slmResponseCatalog_,
                                                    293.15);
                                        field::FieldVisualizationOptions viewOptions;
                                        viewOptions.colormap
                                            = field::ColormapKind::Inferno;
                                        const auto image
                                            = field::renderLinearIntensity(
                                                observationReplay
                                                    .reconstructedAtObservation,
                                                viewOptions);
                                        if (!sandboxVolumeReplayTexture_
                                            || !sandboxVolumeReplayTexture_
                                                ->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the volume reconstruction texture");
                                        }
                                        sandboxVolumeObservationReplay_
                                            = std::make_unique<
                                                optics::holography::
                                                    VolumePlateObservationReplayResult>(
                                                        std::move(
                                                            observationReplay));
                                        errorMessage_.clear();
                                        statusMessage_
                                            = "Reconstructed reflection volume hologram on "
                                            + component.id;
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "Volume observation replay failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                ImGui::PopID();
                            }
                            ImGui::EndDisabled();
                            if (!hasVolumeObservationComponent) {
                                ImGui::TextDisabled(
                                    "Place a Screen / Detector or Field Probe on the reflected side.");
                            }
                            if (sandboxVolumeObservationReplay_
                                && sandboxVolumeObservationReplay_
                                       ->plateComponentId
                                    == selected->id) {
                                const auto& observation
                                    = *sandboxVolumeObservationReplay_;
                                if (observation.isStaleFor(
                                        benchProject_.scene)) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                        "STALE reconstruction: bench revision changed.");
                                }
                                const auto& direction
                                    = observation
                                        .reconstructedDirectionExternalLocal;
                                ImGui::TextWrapped(
                                    "Branch #%llu -> %s | direction local (%.6g, %.6g, %.6g) | distance %.6g m | local offset (%.6g, %.6g) m",
                                    static_cast<unsigned long long>(
                                        observation.replayBranchId),
                                    observation.observationComponentId.c_str(),
                                    direction.x,
                                    direction.y,
                                    direction.z,
                                    observation.signedObservationDistanceMetres,
                                    observation.observationOffsetXMetres,
                                    observation.observationOffsetYMetres);
                                ImGui::TextWrapped(
                                    "Sampled replay %.6g W -> reconstructed %.6g W | efficiency %.3f%%",
                                    observation
                                        .replayPowerOnSampledWindowWatts,
                                    observation
                                        .reconstructedPowerOnSampledWindowWatts,
                                    observation.braggReplay.volume.kogelnik
                                            .diffractionEfficiency
                                        * 100.0);
                                if (observation.usedRoutedWavePath) {
                                    const auto& routed
                                        = observation.routedWavePath;
                                    ImGui::TextColored(
                                        ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                        "Placed routed wave path: %zu segments on %zux%zu working grid",
                                        routed.propagatedSegmentCount,
                                        routed.workingSampleWidth,
                                        routed.workingSampleHeight);
                                    if (observation
                                            .usedSourcePlaneToBeamFrameRotation) {
                                        const auto& rotation = observation
                                            .sourcePlaneToBeamFrameRotation;
                                        ImGui::TextWrapped(
                                            "Plate tangent -> beam-normal spectrum: %zu propagated, %zu evanescent, %zu outside source band, %zu opposite hemisphere, %zu interpolated bins",
                                            rotation.propagatingOutputBinCount,
                                            rotation.evanescentOutputBinCount,
                                            rotation.sourceBandRejectedBinCount,
                                            rotation.oppositeHemisphereBinCount,
                                            rotation.interpolatedOutputBinCount);
                                    }
                                    for (const auto& componentId
                                         : routed.appliedWaveComponentIds) {
                                        ImGui::BulletText(
                                            "Applied placed wave element: %s",
                                            componentId.c_str());
                                    }
                                    for (const auto& prescriptionId
                                         : routed.appliedRealLensPrescriptionIds) {
                                        ImGui::BulletText(
                                            "Applied real-lens prescription: %s",
                                            prescriptionId.c_str());
                                    }
                                    if (routed.usedFoldedPath) {
                                        ImGui::TextDisabled(
                                            "The field frame followed a folded mirror/splitter path.");
                                    }
                                    if (routed.usedTargetTangentProjection) {
                                        ImGui::TextDisabled(
                                            "The routed field was projected onto the selected observation tangent plane.");
                                    }
                                    if (routed.supportTouchesBoundary) {
                                        ImGui::TextColored(
                                            ImVec4(1.0F, 0.65F, 0.25F, 1.0F),
                                            "Routed field support touches the sampled boundary; enlarge the analysis window before quantitative use.");
                                    }
                                    for (const auto& warning : routed.warnings) {
                                        ImGui::TextWrapped(
                                            "Model warning: %s",
                                            warning.c_str());
                                    }
                                } else if (observation.usedTiltedPlanePropagation) {
                                    ImGui::TextWrapped(
                                        "Rotated spectrum: %zu propagated, %zu evanescent, %zu outside source band, %zu interpolated bins",
                                        observation.tiltedPropagation.propagatingOutputBinCount,
                                        observation.tiltedPropagation.evanescentOutputBinCount,
                                        observation.tiltedPropagation.sourceBandRejectedBinCount,
                                        observation.tiltedPropagation.interpolatedOutputBinCount);
                                    ImGui::TextDisabled(
                                        "This reconstruction used rotated angular-spectrum interpolation on a 2x zero-padded grid.");
                                } else {
                                    ImGui::TextWrapped(
                                        "ASM: %zu propagating, %zu evanescent bins",
                                        observation.propagation.propagatingBinCount,
                                        observation.propagation.evanescentBinCount);
                                }
                                if (observation.usedShiftedPaddedPropagation) {
                                    ImGui::TextDisabled(
                                        "This reconstruction used shifted angular-spectrum propagation on a 2x zero-padded grid before cropping to the probe window.");
                                }
                                if (sandboxVolumeReplayTexture_
                                    && sandboxVolumeReplayTexture_->isValid()) {
                                    const float availableWidth
                                        = ImGui::GetContentRegionAvail().x;
                                    const float imageSize = std::clamp(
                                        availableWidth, 160.0F, 360.0F);
                                    ImGui::Image(
                                        toImTextureID(
                                            sandboxVolumeReplayTexture_
                                                ->handle()),
                                        ImVec2(imageSize, imageSize),
                                        ImVec2(0.0F, 1.0F),
                                        ImVec2(1.0F, 0.0F));
                                }
                            }
                            ImGui::TextDisabled(
                                "Efficiency uses an equivalent symmetric Kogelnik model; the full placed slanted K vector above remains authoritative geometry.");
                        }
                        if (sandboxPlateRecording_
                            && sandboxPlateRecording_->plateComponentId
                                == selected->id) {
                            ImGui::SeparatorText("Recorded Thin Exposure");
                            const bool stale = sandboxPlateRecording_->isStaleFor(
                                benchProject_.scene);
                            if (stale) {
                                ImGui::TextColored(
                                    ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                    "STALE: bench revision changed; record again.");
                            } else {
                                ImGui::TextColored(
                                    ImVec4(0.35F, 0.9F, 0.45F, 1.0F),
                                    "Current at revision %llu",
                                    static_cast<unsigned long long>(
                                        sandboxPlateRecording_->sourceRevision));
                            }
                            const auto& diagnostics
                                = sandboxPlateRecording_->diagnostics;
                            ImGui::TextWrapped(
                                "Fringe %.6g cycles/m (X %.6g, Y %.6g) | period %.6g um | sampled: %s",
                                std::hypot(
                                    diagnostics.fringeFrequencyXCyclesPerMetre,
                                    diagnostics.fringeFrequencyYCyclesPerMetre),
                                diagnostics.fringeFrequencyXCyclesPerMetre,
                                diagnostics.fringeFrequencyYCyclesPerMetre,
                                diagnostics.fringePeriodMetres * 1e6,
                                diagnostics.fringeCarrierSampled ? "yes" : "no");
                            ImGui::TextWrapped(
                                "Patch power: object %.6g W, reference %.6g W | exposure %.6g .. %.6g relative intensity",
                                diagnostics.objectPowerOnSampledWindowWatts,
                                diagnostics.referencePowerOnSampledWindowWatts,
                                sandboxPlateRecording_->hologram.diagnostics
                                    .minimumRecordedRelativeIntensity,
                                sandboxPlateRecording_->hologram.diagnostics
                                    .maximumRecordedRelativeIntensity);
                            const auto drawWavePathEvidence = [](
                                const char* label,
                                const optics::holography::
                                    SampledPlateIncidentField& incident) {
                                const auto& path = incident.diagnostics;
                                ImGui::Text(
                                    "%s wave path: %s",
                                    label,
                                    path.appliedLocalWavePath
                                        ? "sampled propagation applied"
                                        : "centreline/source-envelope evidence");
                                if (path.usedFoldedPath) {
                                    ImGui::BulletText(
                                        "Beam-following field transported through %zu fold(s)",
                                        path.foldedWaveComponentIds.size());
                                }
                                if (path.usedTiltedElementProjection) {
                                    ImGui::BulletText(
                                        "Tilted zero-thickness element footprint projected into the transverse field plane");
                                }
                                if (path.usedPlateTangentProjection) {
                                    ImGui::BulletText(
                                        "Carrier restored on the oblique plate tangent plane");
                                }
                                for (const auto& componentId
                                     : path.appliedWaveComponentIds) {
                                    ImGui::BulletText(
                                        "Applied local field transform: %s",
                                        componentId.c_str());
                                }
                                for (const auto& commandId
                                     : path.appliedSlmCommandIds) {
                                    ImGui::BulletText(
                                        "Applied SLM command: %s",
                                        commandId.c_str());
                                }
                                for (const auto& warning : path.warnings) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.68F, 0.25F, 1.0F),
                                        "%s: %s",
                                        label,
                                        warning.c_str());
                                }
                            };
                            drawWavePathEvidence(
                                "Object",
                                sandboxPlateRecording_->objectIncident);
                            drawWavePathEvidence(
                                "Reference",
                                sandboxPlateRecording_->referenceIncident);
                            if (sandboxPlateTexture_
                                && sandboxPlateTexture_->isValid()) {
                                const float availableWidth
                                    = ImGui::GetContentRegionAvail().x;
                                const float imageSize = std::clamp(
                                    availableWidth, 160.0F, 360.0F);
                                ImGui::Image(
                                    toImTextureID(
                                        sandboxPlateTexture_->handle()),
                                    ImVec2(imageSize, imageSize),
                                    ImVec2(0.0F, 1.0F),
                                    ImVec2(1.0F, 0.0F));
                            }

                            ImGui::SeparatorText("Replay to Placed Observation");
                            constexpr const char* kReplayKinds
                                = "Ordinary reference\0Conjugate reference\0";
                            ImGui::Combo(
                                "Replay illumination",
                                &sandboxPlateReplayKindIndex_,
                                kReplayKinds);
                            ImGui::TextDisabled(
                                "Screen/Probe planes may be offset by up to half the sampled window; non-grazing rotations use padded angular-spectrum interpolation.");
                            bool hasObservationComponent = false;
                            ImGui::BeginDisabled(stale);
                            for (const auto& component
                                 : benchProject_.scene.components()) {
                                if (component.kind
                                        != bench::BenchComponentKind::ScreenDetector
                                    && component.kind
                                        != bench::BenchComponentKind::FieldProbe) {
                                    continue;
                                }
                                hasObservationComponent = true;
                                const std::string label
                                    = "Replay to " + component.id;
                                if (ImGui::Button(label.c_str())) {
                                    try {
                                        if (!detectorFftBackend_) {
                                            throw std::runtime_error(
                                                "CPU FFT backend is unavailable");
                                        }
                                        const auto replayKind
                                            = sandboxPlateReplayKindIndex_ == 0
                                            ? optics::holography::ThinPlateReplayKind::OrdinaryReference
                                            : optics::holography::ThinPlateReplayKind::ConjugateReference;
                                        auto replay
                                            = optics::holography::replayThinTransmissionToObservation(
                                                benchProject_.scene,
                                                *sandboxPlateRecording_,
                                                component.id,
                                                replayKind,
                                                *detectorFftBackend_);
                                        field::FieldVisualizationOptions viewOptions;
                                        viewOptions.colormap = field::ColormapKind::Inferno;
                                        const auto image = field::renderLinearIntensity(
                                            replay.fullReplayAtObservation,
                                            viewOptions);
                                        if (!sandboxReplayTexture_
                                            || !sandboxReplayTexture_->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the thin replay texture");
                                        }
                                        sandboxPlateReplay_ = std::make_unique<
                                            optics::holography::ThinPlateReplayResult>(
                                                std::move(replay));
                                        sandboxPlateReplayViewIndex_ = 0;
                                        errorMessage_.clear();
                                        statusMessage_
                                            = "Replayed thin hologram to "
                                            + component.id;
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "Plate replay failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                            }
                            ImGui::EndDisabled();
                            if (!hasObservationComponent) {
                                ImGui::TextDisabled(
                                    "Place a Screen / Detector or Field Probe to observe reconstruction.");
                            }

                            if (sandboxPlateReplay_
                                && sandboxPlateReplay_->plateComponentId
                                    == selected->id) {
                                const bool replayStale
                                    = sandboxPlateReplay_->isStaleFor(
                                        benchProject_.scene);
                                if (replayStale) {
                                    ImGui::TextColored(
                                        ImVec4(1.0F, 0.45F, 0.25F, 1.0F),
                                        "STALE replay: bench revision changed.");
                                }
                                ImGui::TextWrapped(
                                    "%s -> %s | signed distance %.6g m | local offset (%.6g, %.6g) m",
                                    sandboxPlateReplay_->replayKind
                                            == optics::holography::ThinPlateReplayKind::OrdinaryReference
                                        ? "Ordinary"
                                        : "Conjugate",
                                    sandboxPlateReplay_->observationComponentId.c_str(),
                                    sandboxPlateReplay_->signedObservationDistanceMetres,
                                    sandboxPlateReplay_->observationOffsetXMetres,
                                    sandboxPlateReplay_->observationOffsetYMetres);
                                if (sandboxPlateReplay_
                                        ->usedTiltedPlanePropagation) {
                                    ImGui::TextWrapped(
                                        "Rotated spectrum: %zu propagated, %zu evanescent, %zu outside source band, %zu interpolated bins",
                                        sandboxPlateReplay_->tiltedPropagation.propagatingOutputBinCount,
                                        sandboxPlateReplay_->tiltedPropagation.evanescentOutputBinCount,
                                        sandboxPlateReplay_->tiltedPropagation.sourceBandRejectedBinCount,
                                        sandboxPlateReplay_->tiltedPropagation.interpolatedOutputBinCount);
                                    ImGui::TextDisabled(
                                        "This observation used rotated angular-spectrum interpolation on a 2x zero-padded grid.");
                                } else {
                                    ImGui::TextWrapped(
                                        "ASM: %zu propagating, %zu evanescent bins",
                                        sandboxPlateReplay_->propagation.propagatingBinCount,
                                        sandboxPlateReplay_->propagation.evanescentBinCount);
                                }
                                if (sandboxPlateReplay_
                                        ->usedShiftedPaddedPropagation) {
                                    ImGui::TextDisabled(
                                        "This observation used shifted angular-spectrum propagation on a 2x zero-padded grid before cropping to the physical observer window.");
                                }
                                constexpr const char* kReplayViews
                                    = "Physical full replay\0Zero order\0Object-bearing order\0Conjugate order\0";
                                if (ImGui::Combo(
                                        "Replay view",
                                        &sandboxPlateReplayViewIndex_,
                                        kReplayViews)) {
                                    try {
                                        const field::ComplexField2D* replayField
                                            = &sandboxPlateReplay_
                                                ->fullReplayAtObservation;
                                        switch (sandboxPlateReplayViewIndex_) {
                                        case 1:
                                            replayField = &sandboxPlateReplay_
                                                ->zeroOrderAtObservation;
                                            break;
                                        case 2:
                                            replayField = &sandboxPlateReplay_
                                                ->objectBearingOrderAtObservation;
                                            break;
                                        case 3:
                                            replayField = &sandboxPlateReplay_
                                                ->conjugateOrderAtObservation;
                                            break;
                                        default:
                                            break;
                                        }
                                        field::FieldVisualizationOptions viewOptions;
                                        viewOptions.colormap
                                            = field::ColormapKind::Inferno;
                                        const auto image
                                            = field::renderLinearIntensity(
                                                *replayField, viewOptions);
                                        if (!sandboxReplayTexture_
                                            || !sandboxReplayTexture_
                                                ->uploadImage(image)) {
                                            throw std::runtime_error(
                                                "OpenGL rejected the replay-order texture");
                                        }
                                    } catch (const std::exception& error) {
                                        errorMessage_
                                            = "Replay visualization failed: "
                                            + std::string(error.what());
                                        statusMessage_.clear();
                                    }
                                }
                                if (sandboxReplayTexture_
                                    && sandboxReplayTexture_->isValid()) {
                                    const float availableWidth
                                        = ImGui::GetContentRegionAvail().x;
                                    const float imageSize = std::clamp(
                                        availableWidth, 160.0F, 360.0F);
                                    ImGui::Image(
                                        toImTextureID(
                                            sandboxReplayTexture_->handle()),
                                        ImVec2(imageSize, imageSize),
                                        ImVec2(0.0F, 1.0F),
                                        ImVec2(1.0F, 0.0F));
                                }
                            }
                        }
                    }
                }
            }
        }
    } else {
        ImGui::TextDisabled("Select a component in the list or 3D viewport.");
    }

    ImGui::SeparatorText("Unified Bench Project");
    ImGui::InputText("Bench JSON", benchProjectPathBuffer_, sizeof(benchProjectPathBuffer_));
    if (ImGui::Button("Load Bench")) {
        loadBenchProjectFromPath();
    }
    ImGui::SameLine();
    if (ImGui::Button("Save Bench")) {
        saveBenchProjectToPath();
    }
    ImGui::TextDisabled(
        "revision %llu | %zu components | %zu segments | %zu interactions",
        static_cast<unsigned long long>(benchProject_.scene.revision()),
        benchProject_.scene.components().size(),
        benchTraceGraph_.segments.size(),
        benchTraceGraph_.interactions.size());
    } catch (const std::exception& error) {
        errorMessage_ = error.what();
        statusMessage_.clear();
    }
}

void Application::drawWorkspace() {
    sandboxUiEvidence_ = {};
    sandboxReconstructionOverlaySubmitted_ = false;
    sandboxReconstructionOverlayDiagnostic_ = "overlay viewport was not drawn";
    updateWaveDetector();
    updateSlmInterference();
    updateHolography();
    const ImGuiViewport* viewport = ImGui::GetMainViewport();
    const ImGuiID dockspaceId = ImGui::DockSpaceOverViewport(
        ImGui::GetID(docking::DockLayoutConfig::kDockSpaceIdStr),
        viewport);

    // Dear ImGui docking initialization: only configure default layout when the dockspace
    // has not been restored from an existing user imgui.ini configuration.
    // The Dear ImGui internal DockBuilder API (from imgui_internal.h) is used to create
    // the node splits and dock windows into their respective panes.
    if (!dockLayoutInitialized_) {
        ImGuiDockNode* node = ImGui::DockBuilderGetNode(dockspaceId);
        const bool nodeExists = (node != nullptr);
        const bool isSplit = nodeExists && (node->IsSplitNode() || node->ChildNodes[0] != nullptr || node->ChildNodes[1] != nullptr);
        const bool hasWindows = nodeExists && (node->Windows.Size > 0 || node->TabBar != nullptr);
        const bool isEmpty = !nodeExists || (node->IsEmpty() && !hasWindows && !isSplit);

        if (docking::shouldInitializeDefaultDockLayout(nodeExists, isSplit, isEmpty)) {
            ImGui::DockBuilderRemoveNode(dockspaceId);
            ImGui::DockBuilderAddNode(dockspaceId, ImGuiDockNodeFlags_DockSpace);
            ImGui::DockBuilderSetNodeSize(dockspaceId, viewport->WorkSize);
            ImGui::DockBuilderSetNodePos(dockspaceId, viewport->WorkPos);

            ImGuiID dockMainId = dockspaceId;
            ImGuiID dockRightId = 0;
            ImGuiID dockBottomId = 0;

            // Split right side (~25%) for Inspector
            ImGui::DockBuilderSplitNode(
                dockMainId,
                ImGuiDir_Right,
                docking::DockLayoutConfig::kRightInspectorRatio,
                &dockRightId,
                &dockMainId);

            // Split bottom area (~20%) beneath optical bench for Validation
            ImGui::DockBuilderSplitNode(
                dockMainId,
                ImGuiDir_Down,
                docking::DockLayoutConfig::kBottomValidationRatio,
                &dockBottomId,
                &dockMainId);

            // Dock windows into their designated regions:
            // - Center main: Optical Bench (largest region)
            // - Right: Inspector (~25% width)
            // - Bottom: Validation (~20% height under central area)
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kOpticalBenchWindowName, dockMainId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kInspectorWindowName, dockRightId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kValidationWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kWaveDetectorWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kSamplingDebuggerWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kRealLensWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kSlmInterferenceWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kHolographyWindowName, dockBottomId);
            ImGui::DockBuilderDockWindow(docking::DockLayoutConfig::kLearnWindowName, dockRightId);

            ImGui::DockBuilderFinish(dockspaceId);
        }
        dockLayoutInitialized_ = true;
    }

    const ImGuiIO& io = ImGui::GetIO();
    if (!io.WantTextInput && io.KeyCtrl) {
        if (ImGui::IsKeyPressed(ImGuiKey_Z, false)) {
            if (io.KeyShift) {
                if (viewportMode_ == ViewportMode::Sandbox) {
                    redoBenchEdit();
                } else {
                    redoLessonEdit();
                }
            } else {
                if (viewportMode_ == ViewportMode::Sandbox) {
                    undoBenchEdit();
                } else {
                    undoLessonEdit();
                }
            }
        } else if (ImGui::IsKeyPressed(ImGuiKey_Y, false)) {
            if (viewportMode_ == ViewportMode::Sandbox) {
                redoBenchEdit();
            } else {
                redoLessonEdit();
            }
        }
    }

    ImGui::Begin(docking::DockLayoutConfig::kOpticalBenchWindowName);
    if (viewportMode_ == ViewportMode::Sandbox && !isBenchmark_) {
        drawSandboxComponentShelf();
        drawSandboxSourceBar();
        drawSandboxAlignmentBar();
        drawSandboxWaveBar();
        drawSandboxExperimentBar();
        drawChimeraAutomationBar();
    }
    const ImVec2 contentSize = ImGui::GetContentRegionAvail();

    int fboWidth = 0;
    int fboHeight = 0;
    if (isBenchmark_) {
        fboWidth = 1920;
        fboHeight = 1080;
    } else if (contentSize.x >= 1.0F && contentSize.y >= 1.0F) {
        const float scaleX = io.DisplayFramebufferScale.x > 0.0F ? io.DisplayFramebufferScale.x : 1.0F;
        const float scaleY = io.DisplayFramebufferScale.y > 0.0F ? io.DisplayFramebufferScale.y : 1.0F;
        fboWidth = std::max(1, static_cast<int>(std::round(contentSize.x * scaleX)));
        fboHeight = std::max(1, static_cast<int>(std::round(contentSize.y * scaleY)));
    }

    if (fboWidth > 0 && fboHeight > 0) {
        lastViewportWidth_ = fboWidth;
        lastViewportHeight_ = fboHeight;
        camera_.setViewportSize(fboWidth, fboHeight);
        if (renderer_) {
            renderer_->render(fboWidth, fboHeight, camera_);
        }
    }

    if (contentSize.x >= 1.0F && contentSize.y >= 1.0F) {
        const GLuint texId = renderer_ ? renderer_->colorTextureId() : 0;
        if (texId != 0) {
            ImGui::Image(
                toImTextureID(texId),
                contentSize,
                ImVec2(0.0F, 1.0F),
                ImVec2(1.0F, 0.0F));
        }

        const ImVec2 imagePosMin = ImGui::GetItemRectMin();
        const ImVec2 imageSize = ImGui::GetItemRectSize();
        sandboxUiEvidence_.viewport = {
            .minimum = {imagePosMin.x, imagePosMin.y},
            .maximum = {
                imagePosMin.x + imageSize.x,
                imagePosMin.y + imageSize.y},
            .current = imageSize.x > 0.0F && imageSize.y > 0.0F,
        };
        const bool isHovered = ImGui::IsItemHovered();
        const bool noActiveWidget = !ImGui::IsAnyItemActive();
        const glm::vec2 rectMin(imagePosMin.x, imagePosMin.y);
        const glm::vec2 rectSize(imageSize.x, imageSize.y);

        if (viewportMode_ == ViewportMode::Sandbox) {
            const float aspect = (fboHeight > 0)
                ? (static_cast<float>(fboWidth) / static_cast<float>(fboHeight))
                : (16.0F / 9.0F);
            const glm::mat4 viewProj = camera_.projectionMatrix(aspect) * camera_.viewMatrix();
            const glm::vec2 mousePosition(io.MousePos.x, io.MousePos.y);

            if (ImGui::BeginDragDropTarget()) {
                if (const ImGuiPayload* payload = ImGui::AcceptDragDropPayload(
                        kBenchComponentDragPayload)) {
                    if (payload->Data != nullptr
                        && payload->DataSize == sizeof(int)) {
                        const int kindIndex
                            = *static_cast<const int*>(payload->Data);
                        const auto& kinds
                            = optics::scene::requiredBenchComponentKinds();
                        if (kindIndex >= 0
                            && static_cast<std::size_t>(kindIndex)
                                < kinds.size()) {
                            const auto tableHit
                                = gizmo::unprojectScreenToHorizontalPlane(
                                    mousePosition,
                                    0.0F,
                                    viewProj,
                                    rectMin,
                                    rectSize);
                            if (!tableHit.hit) {
                                errorMessage_
                                    = "Cannot place on the horizontal optical table from this view; orbit to a perspective or top view";
                                statusMessage_.clear();
                            } else {
                                const math::Vec3d position {
                                    static_cast<double>(tableHit.worldPosition.x),
                                    0.0,
                                    static_cast<double>(tableHit.worldPosition.z),
                                };
                                const auto kind = kinds[
                                    static_cast<std::size_t>(kindIndex)];
                                static_cast<void>(placeSandboxComponent(
                                    kind,
                                    position,
                                    "Placed " + std::string(
                                        optics::scene::benchComponentDisplayName(kind))
                                        + " on optical table"));
                            }
                        }
                    }
                }
                ImGui::EndDragDropTarget();
            }

            std::string hoveredComponentId;
            float hoveredDepth = std::numeric_limits<float>::max();
            constexpr float kComponentHitRadius = 16.0F;
            for (const auto& component : benchProject_.scene.components()) {
                const glm::vec3 center(
                    static_cast<float>(component.transform.translationMetres.x),
                    static_cast<float>(component.transform.translationMetres.y),
                    static_cast<float>(component.transform.translationMetres.z));
                const auto projected = gizmo::projectWorldToViewport(
                    center, viewProj, rectMin, rectSize);
                if (gizmo::hitTestHandle(mousePosition, projected, kComponentHitRadius)
                    && projected.depth < hoveredDepth) {
                    hoveredComponentId = component.id;
                    hoveredDepth = projected.depth;
                }
            }

            std::array<glm::vec3, 3> gizmoAxisDirections {
                glm::vec3(1.0F, 0.0F, 0.0F),
                glm::vec3(0.0F, 1.0F, 0.0F),
                glm::vec3(0.0F, 0.0F, 1.0F),
            };
            std::array<gizmo::ProjectedPoint, 3> gizmoAxisEndpoints {};
            std::array<gizmo::AxisProjection, 3> gizmoAxisProjections {};
            std::array<gizmo::ProjectedPoint, 6> mechanicalHandleProjections {};
            std::array<gizmo::AxisProjection, 4> mechanicalAxisProjections {};
            gizmo::ProjectedPoint selectedComponentProjection {};
            SandboxGizmoConstraint hoveredGizmoConstraint
                = SandboxGizmoConstraint::None;
            SandboxMechanicalControl hoveredMechanicalControl
                = SandboxMechanicalControl::None;
            if (const auto* selectedComponent
                = benchProject_.scene.find(selectedBenchComponentId_)) {
                const auto& transform = selectedComponent->transform;
                const glm::vec3 centre(
                    static_cast<float>(transform.translationMetres.x),
                    static_cast<float>(transform.translationMetres.y),
                    static_cast<float>(transform.translationMetres.z));
                selectedComponentProjection = gizmo::projectWorldToViewport(
                    centre, viewProj, rectMin, rectSize);
                if (sandboxGizmoMode_ == SandboxGizmoMode::Rotate) {
                    gizmoAxisDirections = {
                        glm::vec3(
                            static_cast<float>(transform.localXAxisInWorld.x),
                            static_cast<float>(transform.localXAxisInWorld.y),
                            static_cast<float>(transform.localXAxisInWorld.z)),
                        glm::vec3(
                            static_cast<float>(transform.localYAxisInWorld.x),
                            static_cast<float>(transform.localYAxisInWorld.y),
                            static_cast<float>(transform.localYAxisInWorld.z)),
                        glm::vec3(
                            static_cast<float>(transform.localZAxisInWorld.x),
                            static_cast<float>(transform.localZAxisInWorld.y),
                            static_cast<float>(transform.localZAxisInWorld.z)),
                    };
                }
                const float axisLength = std::clamp(
                    0.08F * camera_.distance(), 0.025F, 0.25F);
                float nearestHandleDistance
                    = std::numeric_limits<float>::max();
                for (std::size_t axis = 0; axis < gizmoAxisDirections.size();
                     ++axis) {
                    gizmoAxisEndpoints[axis] = gizmo::projectWorldToViewport(
                        centre + gizmoAxisDirections[axis] * axisLength,
                        viewProj,
                        rectMin,
                        rectSize);
                    gizmoAxisProjections[axis] = gizmo::computeAxisProjection(
                        centre,
                        gizmoAxisDirections[axis],
                        axisLength,
                        viewProj,
                        rectMin,
                        rectSize);
                    if (!gizmo::hitTestHandle(
                            mousePosition, gizmoAxisEndpoints[axis], 11.0F)) {
                        continue;
                    }
                    const float distance = std::hypot(
                        mousePosition.x
                            - gizmoAxisEndpoints[axis].screenPos.x,
                        mousePosition.y
                            - gizmoAxisEndpoints[axis].screenPos.y);
                    if (distance < nearestHandleDistance) {
                        nearestHandleDistance = distance;
                        hoveredGizmoConstraint = static_cast<
                            SandboxGizmoConstraint>(
                                static_cast<int>(SandboxGizmoConstraint::AxisX)
                                + static_cast<int>(axis));
                    }
                }

                if (selectedComponent->mechanicalAssembly.has_value()) {
                    const auto& mechanical
                        = *selectedComponent->mechanicalAssembly;
                    const auto toGlm = [](const math::Vec3d& value) {
                        return glm::vec3 {
                            static_cast<float>(value.x),
                            static_cast<float>(value.y),
                            static_cast<float>(value.z),
                        };
                    };
                    const glm::vec3 base = toGlm(
                        mechanical.benchFrame.translationMetres);
                    const glm::vec3 baseX = toGlm(
                        mechanical.benchFrame.localXAxisInWorld);
                    const glm::vec3 baseY = toGlm(
                        mechanical.benchFrame.localYAxisInWorld);
                    const glm::vec3 baseZ = toGlm(
                        mechanical.benchFrame.localZAxisInWorld);
                    const float postHeight = static_cast<float>(
                        mechanical.postHeightMetres);
                    const float controlReach = std::clamp(
                        0.045F * camera_.distance(), 0.018F, 0.065F);
                    const glm::vec3 stageCentre = centre;
                    const std::array<glm::vec3, 6> handlePositions {{
                        base + baseX * 0.014F
                            + baseY * (0.55F * postHeight),
                        stageCentre + baseX * controlReach,
                        stageCentre + baseY * controlReach,
                        stageCentre + baseZ * controlReach,
                        stageCentre - baseX * (0.45F * controlReach)
                            + baseY * (0.28F * controlReach),
                        stageCentre - baseX * (0.45F * controlReach)
                            - baseY * (0.28F * controlReach),
                    }};
                    for (std::size_t index = 0;
                         index < handlePositions.size(); ++index) {
                        mechanicalHandleProjections[index]
                            = gizmo::projectWorldToViewport(
                                handlePositions[index],
                                viewProj,
                                rectMin,
                                rectSize);
                    }
                    if (selectedComponentProjection.visible) {
                        mechanicalHandleProjections[4]
                            = selectedComponentProjection;
                        mechanicalHandleProjections[4].screenPos = {
                            std::clamp(
                                selectedComponentProjection.screenPos.x
                                    - 34.0F,
                                rectMin.x + 12.0F,
                                rectMin.x + rectSize.x - 12.0F),
                            std::clamp(
                                selectedComponentProjection.screenPos.y
                                    - 24.0F,
                                rectMin.y + 12.0F,
                                rectMin.y + rectSize.y - 12.0F),
                        };
                        mechanicalHandleProjections[5]
                            = selectedComponentProjection;
                        mechanicalHandleProjections[5].screenPos = {
                            std::clamp(
                                selectedComponentProjection.screenPos.x
                                    - 34.0F,
                                rectMin.x + 12.0F,
                                rectMin.x + rectSize.x - 12.0F),
                            std::clamp(
                                selectedComponentProjection.screenPos.y
                                    + 24.0F,
                                rectMin.y + 12.0F,
                                rectMin.y + rectSize.y - 12.0F),
                        };
                    }
                    const std::array<glm::vec3, 4> mechanicalAxes {{
                        baseY, baseX, baseY, baseZ,
                    }};
                    for (std::size_t index = 0;
                         index < mechanicalAxes.size(); ++index) {
                        mechanicalAxisProjections[index]
                            = gizmo::computeAxisProjection(
                                handlePositions[index],
                                mechanicalAxes[index],
                                controlReach,
                                viewProj,
                                rectMin,
                                rectSize);
                    }
                    float nearestMechanicalDistance
                        = std::numeric_limits<float>::max();
                    for (std::size_t index = 0;
                         index < mechanicalHandleProjections.size(); ++index) {
                        const auto& projected
                            = mechanicalHandleProjections[index];
                        if (!gizmo::hitTestHandle(
                                mousePosition, projected, 10.0F)) {
                            continue;
                        }
                        if (index < mechanicalAxisProjections.size()
                            && mechanicalAxisProjections[index].isDegenerate) {
                            continue;
                        }
                        const float distance = std::hypot(
                            mousePosition.x - projected.screenPos.x,
                            mousePosition.y - projected.screenPos.y);
                        if (distance < nearestMechanicalDistance) {
                            nearestMechanicalDistance = distance;
                            hoveredMechanicalControl = static_cast<
                                SandboxMechanicalControl>(
                                    static_cast<int>(
                                        SandboxMechanicalControl::PostHeight)
                                    + static_cast<int>(index));
                        }
                    }
                }
            }
            sandboxUiEvidence_.selectedComponent
                = selectedComponentProjection;
            sandboxUiEvidence_.gizmoEndpoints = gizmoAxisEndpoints;
            sandboxUiEvidence_.gizmoProjections = gizmoAxisProjections;
            sandboxUiEvidence_.mechanicalHandles
                = mechanicalHandleProjections;
            sandboxUiEvidence_.mechanicalProjections
                = mechanicalAxisProjections;

            if (sandboxMechanicalDragging_) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    auto candidate = benchProject_.scene;
                    if (const auto* component = candidate.find(
                            selectedBenchComponentId_)) {
                        auto restored = *component;
                        optics::scene::applyMechanicalAssembly(
                            restored, sandboxMechanicalDragInitial_);
                        candidate.replace(restored.id, restored);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate),
                            "Cancelled mechanical adjustment",
                            false));
                    }
                    sandboxMechanicalDragging_ = false;
                    sandboxMechanicalChanged_ = false;
                    sandboxMechanicalControl_
                        = SandboxMechanicalControl::None;
                } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (sandboxMechanicalChanged_) {
                        recordBenchEdit();
                    }
                    sandboxMechanicalDragging_ = false;
                    sandboxMechanicalChanged_ = false;
                    sandboxMechanicalControl_
                        = SandboxMechanicalControl::None;
                } else if (const auto* component = benchProject_.scene.find(
                               selectedBenchComponentId_);
                           component != nullptr
                           && component->mechanicalAssembly.has_value()) {
                    auto adjusted = sandboxMechanicalDragInitial_;
                    const int controlIndex = static_cast<int>(
                        sandboxMechanicalControl_)
                        - static_cast<int>(
                            SandboxMechanicalControl::PostHeight);
                    if (controlIndex >= 0 && controlIndex < 4) {
                        sandboxMechanicalDragAccumulated_
                            += gizmo::computeGizmoDeltaZ(
                                {io.MouseDelta.x, io.MouseDelta.y},
                                mechanicalAxisProjections[
                                    static_cast<std::size_t>(controlIndex)]);
                        const double delta = gizmo::quantizeGizmoDelta(
                            sandboxMechanicalDragAccumulated_, 1e-5);
                        switch (sandboxMechanicalControl_) {
                        case SandboxMechanicalControl::PostHeight:
                            adjusted.postHeightMetres = std::clamp(
                                sandboxMechanicalDragInitial_.postHeightMetres
                                    + delta,
                                adjusted.minimumPostHeightMetres,
                                adjusted.maximumPostHeightMetres);
                            break;
                        case SandboxMechanicalControl::StageX:
                            adjusted.stageTranslationMetres.x = std::clamp(
                                sandboxMechanicalDragInitial_
                                        .stageTranslationMetres.x
                                    + delta,
                                adjusted.minimumStageTranslationMetres.x,
                                adjusted.maximumStageTranslationMetres.x);
                            break;
                        case SandboxMechanicalControl::StageY:
                            adjusted.stageTranslationMetres.y = std::clamp(
                                sandboxMechanicalDragInitial_
                                        .stageTranslationMetres.y
                                    + delta,
                                adjusted.minimumStageTranslationMetres.y,
                                adjusted.maximumStageTranslationMetres.y);
                            break;
                        case SandboxMechanicalControl::StageZ:
                            adjusted.stageTranslationMetres.z = std::clamp(
                                sandboxMechanicalDragInitial_
                                        .stageTranslationMetres.z
                                    + delta,
                                adjusted.minimumStageTranslationMetres.z,
                                adjusted.maximumStageTranslationMetres.z);
                            break;
                        case SandboxMechanicalControl::None:
                        case SandboxMechanicalControl::MountYaw:
                        case SandboxMechanicalControl::MountPitch:
                            break;
                        }
                    } else {
                        sandboxMechanicalDragAccumulated_
                            += static_cast<double>(
                                io.MouseDelta.x - io.MouseDelta.y) * 0.002;
                        const double angleDelta = gizmo::quantizeGizmoDelta(
                            sandboxMechanicalDragAccumulated_,
                            std::numbers::pi_v<double> / 1800.0);
                        if (sandboxMechanicalControl_
                            == SandboxMechanicalControl::MountYaw) {
                            adjusted.mountYawRadians = std::clamp(
                                sandboxMechanicalDragInitial_.mountYawRadians
                                    + angleDelta,
                                adjusted.minimumMountYawRadians,
                                adjusted.maximumMountYawRadians);
                        } else if (sandboxMechanicalControl_
                            == SandboxMechanicalControl::MountPitch) {
                            adjusted.mountPitchRadians = std::clamp(
                                sandboxMechanicalDragInitial_.mountPitchRadians
                                    + angleDelta,
                                adjusted.minimumMountPitchRadians,
                                adjusted.maximumMountPitchRadians);
                        }
                    }
                    auto candidate = benchProject_.scene;
                    auto mounted = *candidate.find(
                        selectedBenchComponentId_);
                    optics::scene::applyMechanicalAssembly(
                        mounted, adjusted);
                    candidate.replace(mounted.id, mounted);
                    sandboxMechanicalChanged_ = applyBenchScene(
                        std::move(candidate),
                        "Adjusted mechanical control in viewport",
                        false)
                        || sandboxMechanicalChanged_;
                }
            }

            if (sandboxGizmoDragging_) {
                if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                    auto candidate = benchProject_.scene;
                    if (const auto* component = candidate.find(selectedBenchComponentId_)) {
                        auto restored = *component;
                        optics::scene::rebaseMechanicalAssembly(
                            restored, sandboxDragInitialTransform_);
                        candidate.replace(restored.id, restored);
                        static_cast<void>(applyBenchScene(
                            std::move(candidate), "Cancelled sandbox gizmo edit", false));
                    }
                    sandboxGizmoDragging_ = false;
                    sandboxGizmoChanged_ = false;
                } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                    if (sandboxGizmoChanged_) {
                        recordBenchEdit();
                    }
                    sandboxGizmoDragging_ = false;
                    sandboxGizmoChanged_ = false;
                } else if (const auto* component = benchProject_.scene.find(selectedBenchComponentId_)) {
                    auto candidate = benchProject_.scene;
                    auto edited = *component;
                    auto desiredTransform = edited.transform;
                    if (sandboxGizmoMode_ == SandboxGizmoMode::Translate) {
                        const int axisIndex = sandboxConstraintAxisIndex(
                            sandboxGizmoConstraint_);
                        math::Vec3d delta;
                        if (axisIndex >= 0) {
                            const double axisDelta
                                = gizmo::computeGizmoDeltaZ(
                                    {io.MouseDelta.x, io.MouseDelta.y},
                                    gizmoAxisProjections[
                                        static_cast<std::size_t>(axisIndex)]);
                            const glm::vec3 axis = gizmoAxisDirections[
                                static_cast<std::size_t>(axisIndex)];
                            delta = {
                                static_cast<double>(axis.x) * axisDelta,
                                static_cast<double>(axis.y) * axisDelta,
                                static_cast<double>(axis.z) * axisDelta,
                            };
                        } else {
                            const double metresPerPixel = 0.00045
                                * static_cast<double>(camera_.distance());
                            const glm::vec3 deltaWorld = camera_.rightVector()
                                    * (io.MouseDelta.x
                                        * static_cast<float>(metresPerPixel))
                                + camera_.upVector()
                                    * (-io.MouseDelta.y
                                        * static_cast<float>(metresPerPixel));
                            delta = {
                                static_cast<double>(deltaWorld.x),
                                static_cast<double>(deltaWorld.y),
                                static_cast<double>(deltaWorld.z),
                            };
                        }
                        sandboxDragAccumulatedTranslationMetres_
                            = sandboxDragAccumulatedTranslationMetres_ + delta;
                        const double snapMetres
                            = static_cast<double>(
                                sandboxTranslationSnapMillimetres_) * 1e-3;
                        const math::Vec3d snapped {
                            gizmo::quantizeGizmoDelta(
                                sandboxDragAccumulatedTranslationMetres_.x,
                                snapMetres),
                            gizmo::quantizeGizmoDelta(
                                sandboxDragAccumulatedTranslationMetres_.y,
                                snapMetres),
                            gizmo::quantizeGizmoDelta(
                                sandboxDragAccumulatedTranslationMetres_.z,
                                snapMetres),
                        };
                        desiredTransform = sandboxDragInitialTransform_;
                        desiredTransform.translationMetres
                            = desiredTransform.translationMetres + snapped;
                    } else {
                        const int axisIndex = sandboxConstraintAxisIndex(
                            sandboxGizmoConstraint_);
                        if (axisIndex >= 0) {
                            sandboxDragAccumulatedAngleRadians_
                                += static_cast<double>(
                                    io.MouseDelta.x - io.MouseDelta.y) * 0.01;
                            const double stepRadians = static_cast<double>(
                                sandboxRotationStepDegrees_)
                                * std::numbers::pi_v<double> / 180.0;
                            const double snappedAngle
                                = gizmo::quantizeGizmoDelta(
                                    sandboxDragAccumulatedAngleRadians_,
                                    stepRadians);
                            desiredTransform = gizmo::rotateRigidTransformLocally(
                                sandboxDragInitialTransform_,
                                localRotationAxisForIndex(axisIndex),
                                snappedAngle);
                        } else {
                            desiredTransform = gizmo::rotateRigidTransformLocally(
                                desiredTransform,
                                gizmo::LocalRotationAxis::Y,
                                static_cast<double>(io.MouseDelta.x) * 0.01);
                            desiredTransform = gizmo::rotateRigidTransformLocally(
                                desiredTransform,
                                gizmo::LocalRotationAxis::X,
                                static_cast<double>(-io.MouseDelta.y) * 0.01);
                        }
                    }
                    optics::scene::rebaseMechanicalAssembly(
                        edited, desiredTransform);
                    candidate.replace(edited.id, edited);
                    sandboxGizmoChanged_ = applyBenchScene(
                        std::move(candidate),
                        sandboxGizmoMode_ == SandboxGizmoMode::Translate
                            ? "Moved component in viewport"
                            : "Rotated component in viewport",
                        false)
                        || sandboxGizmoChanged_;
                }
            }

            if (isHovered && noActiveWidget && !sandboxGizmoDragging_
                && !sandboxMechanicalDragging_) {
                if (std::abs(io.MouseWheel) > 0.0F) {
                    camera_.zoom(io.MouseWheel);
                }
                if (ImGui::IsKeyPressed(ImGuiKey_F, false)) {
                    focusSandboxSelection();
                }
                if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_W, false)) {
                    sandboxGizmoMode_ = SandboxGizmoMode::Translate;
                }
                if (!io.KeyShift && ImGui::IsKeyPressed(ImGuiKey_E, false)) {
                    sandboxGizmoMode_ = SandboxGizmoMode::Rotate;
                }
                if (io.KeyShift && !io.KeyCtrl && !io.KeyAlt
                    && std::isfinite(io.DeltaTime) && io.DeltaTime > 0.0F) {
                    float rightMotion = 0.0F;
                    float forwardMotion = 0.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_A)) rightMotion -= 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_D)) rightMotion += 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_W)) forwardMotion += 1.0F;
                    if (ImGui::IsKeyDown(ImGuiKey_S)) forwardMotion -= 1.0F;
                    const float magnitude = std::hypot(
                        rightMotion, forwardMotion);
                    if (magnitude > 0.0F) {
                        const float speedMetresPerSecond = std::clamp(
                            camera_.distance() * 1.5F, 0.05F, 5.0F);
                        const float scale = speedMetresPerSecond
                            * io.DeltaTime / magnitude;
                        camera_.moveLocal(
                            rightMotion * scale,
                            0.0F,
                            forwardMotion * scale);
                    }
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                    isOrbiting_ = true;
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                    isPanning_ = true;
                }
                if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                    if (hoveredMechanicalControl
                        != SandboxMechanicalControl::None) {
                        const auto* mounted = benchProject_.scene.find(
                            selectedBenchComponentId_);
                        if (mounted != nullptr
                            && mounted->mechanicalAssembly.has_value()) {
                            sandboxMechanicalControl_
                                = hoveredMechanicalControl;
                            sandboxMechanicalDragInitial_
                                = *mounted->mechanicalAssembly;
                            sandboxMechanicalDragAccumulated_ = 0.0;
                            sandboxMechanicalDragging_ = true;
                            sandboxMechanicalChanged_ = false;
                        }
                    } else if (hoveredGizmoConstraint
                        != SandboxGizmoConstraint::None) {
                        sandboxGizmoConstraint_ = hoveredGizmoConstraint;
                        sandboxDragInitialTransform_ = benchProject_.scene
                            .find(selectedBenchComponentId_)->transform;
                        sandboxGizmoDragging_ = true;
                        sandboxGizmoChanged_ = false;
                        sandboxDragAccumulatedTranslationMetres_ = {};
                        sandboxDragAccumulatedAngleRadians_ = 0.0;
                    } else {
                        selectedBenchComponentId_ = hoveredComponentId;
                        if (!selectedBenchComponentId_.empty()) {
                            sandboxGizmoConstraint_
                                = SandboxGizmoConstraint::ViewPlane;
                            sandboxDragInitialTransform_ = benchProject_.scene
                                .find(selectedBenchComponentId_)->transform;
                            sandboxGizmoDragging_ = true;
                            sandboxGizmoChanged_ = false;
                            sandboxDragAccumulatedTranslationMetres_ = {};
                            sandboxDragAccumulatedAngleRadians_ = 0.0;
                        }
                    }
                    static_cast<void>(showSandboxViewport());
                }
            }

            if (isOrbiting_) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                    camera_.orbit(
                        -io.MouseDelta.x * 0.005F,
                        io.MouseDelta.y * 0.005F);
                } else {
                    isOrbiting_ = false;
                }
            }
            if (isPanning_) {
                if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                    const float panFactor = 0.0015F * camera_.distance();
                    camera_.pan(-io.MouseDelta.x * panFactor, io.MouseDelta.y * panFactor);
                } else {
                    isPanning_ = false;
                }
            }

            updateSandboxWaveObservation();
            ImDrawList* drawList = ImGui::GetWindowDrawList();
            if (drawList != nullptr) {
                drawList->PushClipRect(
                    imagePosMin,
                    ImVec2(imagePosMin.x + imageSize.x, imagePosMin.y + imageSize.y),
                    true);
                GLuint reconstructionTexture = 0U;
                std::string_view reconstructionObservationId;
                if (chimeraWorkflow_
                    && chimera::isChimeraBenchWorkflowCurrent(
                        *chimeraWorkflow_, benchProject_)
                    && chimeraWorkflow_->cameraImage
                    && !chimeraWorkflow_->cameraImage->isStaleFor(
                        benchProject_.scene)
                    && !chimeraWorkflow_->observationComponentId.empty()
                    && chimeraCameraTexture_
                    && chimeraCameraTexture_->isValid()) {
                    reconstructionTexture = chimeraCameraTexture_->handle();
                    reconstructionObservationId
                        = chimeraWorkflow_->observationComponentId;
                } else if (sandboxRecordedExperiment_
                        == SandboxRecordedExperiment::ThinTransmission
                    && sandboxPlateReplay_
                    && !sandboxPlateReplay_->isStaleFor(benchProject_.scene)
                    && sandboxReplayTexture_
                    && sandboxReplayTexture_->isValid()) {
                    reconstructionTexture = sandboxReplayTexture_->handle();
                    reconstructionObservationId
                        = sandboxPlateReplay_->observationComponentId;
                } else if (sandboxRecordedExperiment_
                        == SandboxRecordedExperiment::ReflectionDenisyuk
                    && sandboxVolumeObservationReplay_
                    && !sandboxVolumeObservationReplay_->isStaleFor(
                        benchProject_.scene)
                    && sandboxVolumeReplayTexture_
                    && sandboxVolumeReplayTexture_->isValid()) {
                    reconstructionTexture
                        = sandboxVolumeReplayTexture_->handle();
                    reconstructionObservationId
                        = sandboxVolumeObservationReplay_
                              ->observationComponentId;
                } else if (sandboxRecordedExperiment_
                        == SandboxRecordedExperiment::RgbFullColour
                    && sandboxRgbReplay_
                    && !sandboxRgbReplay_->isStaleFor(benchProject_.scene)
                    && sandboxRgbReplayTexture_
                    && sandboxRgbReplayTexture_->isValid()) {
                    reconstructionTexture
                        = sandboxRgbReplayTexture_->handle();
                    reconstructionObservationId
                        = sandboxRgbReplay_->observationComponentId;
                } else if (sandboxRecordedExperiment_
                        == SandboxRecordedExperiment::RgbReflectionDenisyuk
                    && sandboxRgbVolumeReplay_
                    && !sandboxRgbVolumeReplay_->isStaleFor(
                        benchProject_.scene)
                    && sandboxRgbReplayTexture_
                    && sandboxRgbReplayTexture_->isValid()) {
                    reconstructionTexture
                        = sandboxRgbReplayTexture_->handle();
                    reconstructionObservationId
                        = sandboxRgbVolumeReplay_->observationComponentId;
                } else if (const auto* waveObservation
                           = activeSandboxWaveObservation();
                    waveObservation
                    && !waveObservation->isStaleFor(benchProject_.scene)
                    && sandboxWaveTexture_
                    && sandboxWaveTexture_->isValid()) {
                    reconstructionTexture = sandboxWaveTexture_->handle();
                    reconstructionObservationId
                        = waveObservation->observationComponentId;
                }
                if (reconstructionTexture != 0U) {
                    if (const auto* observation = benchProject_.scene.find(
                            reconstructionObservationId)) {
                        try {
                            sandboxReconstructionOverlaySubmitted_
                                = drawPlacedObservationTexture(
                                    *drawList,
                                    reconstructionTexture,
                                    *observation,
                                    viewProj,
                                    rectMin,
                                    rectSize,
                                    reconstructionObservationId,
                                    sandboxReconstructionOverlayDiagnostic_);
                        } catch (const std::exception& error) {
                            errorMessage_
                                = "Placed reconstruction overlay failed: "
                                + std::string(error.what());
                            statusMessage_.clear();
                        }
                    } else {
                        sandboxReconstructionOverlayDiagnostic_
                            = "current reconstruction observation component is missing";
                    }
                } else {
                    sandboxReconstructionOverlayDiagnostic_
                        = "no current reconstruction texture matches the active experiment";
                }
                for (const auto& component : benchProject_.scene.components()) {
                    const glm::vec3 center(
                        static_cast<float>(component.transform.translationMetres.x),
                        static_cast<float>(component.transform.translationMetres.y),
                        static_cast<float>(component.transform.translationMetres.z));
                    const auto projected = gizmo::projectWorldToViewport(
                        center, viewProj, rectMin, rectSize);
                    if (!projected.visible) {
                        continue;
                    }
                    const bool selected = component.id == selectedBenchComponentId_;
                    const bool hovered = component.id == hoveredComponentId;
                    const ImU32 color = selected
                        ? IM_COL32(255, 220, 80, 255)
                        : hovered ? IM_COL32(120, 220, 255, 255)
                                  : IM_COL32(180, 195, 215, 170);
                    drawList->AddCircle(
                        ImVec2(projected.screenPos.x, projected.screenPos.y),
                        selected ? 10.0F : 7.0F, color, 24, selected ? 2.5F : 1.5F);
                    const auto componentCalibrationState
                        = optics::scene::instrumentCalibrationState(
                            component.instrument);
                    const ImU32 calibrationColor
                        = componentCalibrationState
                                == optics::scene::InstrumentCalibrationState::Calibrated
                        ? IM_COL32(72, 235, 158, 255)
                        : componentCalibrationState
                                == optics::scene::InstrumentCalibrationState::Stale
                        ? IM_COL32(255, 184, 64, 255)
                        : IM_COL32(150, 168, 196, 220);
                    drawList->AddCircleFilled(
                        ImVec2(
                            projected.screenPos.x + (selected ? 8.0F : 6.0F),
                            projected.screenPos.y + (selected ? 8.0F : 6.0F)),
                        selected ? 4.0F : 3.0F,
                        calibrationColor,
                        12);
                    if (selected || hovered) {
                        drawList->AddText(
                            ImVec2(projected.screenPos.x + 12.0F, projected.screenPos.y - 8.0F),
                            color,
                            component.id.c_str());
                        const auto stateLabel
                            = optics::scene::instrumentCalibrationStateName(
                                componentCalibrationState);
                        drawList->AddText(
                            ImVec2(
                                projected.screenPos.x + 12.0F,
                                projected.screenPos.y + 8.0F),
                            calibrationColor,
                            stateLabel.data());
                    }
                }
                if (selectedComponentProjection.visible) {
                    constexpr std::array<ImU32, 3> kAxisColors {
                        IM_COL32(238, 82, 83, 255),
                        IM_COL32(78, 201, 112, 255),
                        IM_COL32(74, 144, 245, 255),
                    };
                    constexpr std::array<const char*, 3> kAxisLabels {
                        "X", "Y", "Z"};
                    for (std::size_t axis = 0;
                         axis < gizmoAxisEndpoints.size();
                         ++axis) {
                        if (!gizmoAxisEndpoints[axis].visible) {
                            continue;
                        }
                        const auto constraint = static_cast<
                            SandboxGizmoConstraint>(
                                static_cast<int>(SandboxGizmoConstraint::AxisX)
                                + static_cast<int>(axis));
                        const bool active = sandboxGizmoDragging_
                            && sandboxGizmoConstraint_ == constraint;
                        const bool hovered
                            = hoveredGizmoConstraint == constraint;
                        const ImU32 color = active || hovered
                            ? IM_COL32(255, 226, 96, 255)
                            : kAxisColors[axis];
                        const ImVec2 start(
                            selectedComponentProjection.screenPos.x,
                            selectedComponentProjection.screenPos.y);
                        const ImVec2 end(
                            gizmoAxisEndpoints[axis].screenPos.x,
                            gizmoAxisEndpoints[axis].screenPos.y);
                        drawList->AddLine(
                            start, end, color, active ? 4.0F : 2.5F);
                        drawList->AddCircleFilled(
                            end, active || hovered ? 7.0F : 5.0F,
                            color, 20);
                        drawList->AddText(
                            ImVec2(end.x + 7.0F, end.y - 7.0F),
                            color,
                            kAxisLabels[axis]);
                    }
                }
                constexpr std::array<const char*, 6> kMechanicalLabels {
                    "POST", "STAGE X", "STAGE Y", "STAGE Z", "YAW", "PITCH"};
                for (std::size_t index = 0;
                     index < mechanicalHandleProjections.size(); ++index) {
                    const auto& projected
                        = mechanicalHandleProjections[index];
                    if (!projected.visible) {
                        continue;
                    }
                    const auto control = static_cast<
                        SandboxMechanicalControl>(
                            static_cast<int>(
                                SandboxMechanicalControl::PostHeight)
                            + static_cast<int>(index));
                    const bool active = sandboxMechanicalDragging_
                        && sandboxMechanicalControl_ == control;
                    const bool hovered
                        = hoveredMechanicalControl == control;
                    const ImU32 color = active || hovered
                        ? IM_COL32(255, 238, 132, 255)
                        : IM_COL32(225, 167, 65, 235);
                    const ImVec2 centre(
                        projected.screenPos.x, projected.screenPos.y);
                    drawList->AddCircleFilled(
                        centre, active || hovered ? 7.0F : 5.0F,
                        color, 20);
                    drawList->AddCircle(
                        centre, active || hovered ? 10.0F : 8.0F,
                        IM_COL32(73, 48, 18, 230), 20, 1.5F);
                    drawList->AddText(
                        ImVec2(centre.x + 9.0F, centre.y - 7.0F),
                        color,
                        kMechanicalLabels[index]);
                }
                const char* modeName = sandboxGizmoMode_ == SandboxGizmoMode::Translate
                    ? "Move (world axes)" : "Rotate (local axes)";
                const std::string hud = std::string("SANDBOX | ") + modeName
                    + " [W/E] | brass points: mount controls | F focus | Shift+WASD roam | RMB orbit | MMB pan | wheel zoom";
                drawList->AddRectFilled(
                    ImVec2(imagePosMin.x + 10.0F, imagePosMin.y + 10.0F),
                    ImVec2(imagePosMin.x + 1040.0F, imagePosMin.y + 34.0F),
                    IM_COL32(8, 15, 25, 205), 4.0F);
                drawList->AddText(
                    ImVec2(imagePosMin.x + 17.0F, imagePosMin.y + 15.0F),
                    IM_COL32(210, 230, 245, 255), hud.c_str());
                drawList->PopClipRect();
            }
        } else {

        // Aspect ratio corresponds to actual offscreen FBO size
        const float aspect = (fboHeight > 0)
            ? (static_cast<float>(fboWidth) / static_cast<float>(fboHeight))
            : (16.0F / 9.0F);
        const glm::mat4 viewProj = camera_.projectionMatrix(aspect) * camera_.viewMatrix();

        // Project lens center and +Z axis direction
        const glm::vec3 lensCenterWorld(
            static_cast<float>(scene_.lens.centreXMetres),
            static_cast<float>(scene_.lens.centreYMetres),
            static_cast<float>(scene_.lens.planeZMetres));
        const auto projLensCenter = gizmo::projectWorldToViewport(lensCenterWorld, viewProj, rectMin, rectSize);
        const auto lensAxisProj = gizmo::computeAxisProjection(
            lensCenterWorld,
            glm::vec3(0.0F, 0.0F, 1.0F),
            0.05F,
            viewProj,
            rectMin,
            rectSize);

        // Project screen center and +Z axis direction
        const glm::vec3 screenCenterWorld(
            static_cast<float>(scene_.screen.centreXMetres),
            static_cast<float>(scene_.screen.centreYMetres),
            static_cast<float>(scene_.screen.planeZMetres));
        const auto projScreenCenter = gizmo::projectWorldToViewport(screenCenterWorld, viewProj, rectMin, rectSize);
        const auto screenAxisProj = gizmo::computeAxisProjection(
            screenCenterWorld,
            glm::vec3(0.0F, 0.0F, 1.0F),
            0.05F,
            viewProj,
            rectMin,
            rectSize);

        // Hit testing - only when hovered over viewport, no active ImGui widget, not dragging gizmo, not orbiting, not panning
        constexpr float kHitRadius = 14.0F;
        bool lensHovered = false;
        bool screenHovered = false;

        const glm::vec2 mousePos(io.MousePos.x, io.MousePos.y);
        const glm::vec2 mouseDelta(io.MouseDelta.x, io.MouseDelta.y);

        if (!isGizmoDragging_ && !isOrbiting_ && !isPanning_ && isHovered && noActiveWidget) {
            lensHovered = gizmo::hitTestHandle(mousePos, projLensCenter, kHitRadius);
            screenHovered = gizmo::hitTestHandle(mousePos, projScreenCenter, kHitRadius);
            if (lensHovered && screenHovered) {
                // If both overlap, pick closer one in depth
                if (projLensCenter.depth <= projScreenCenter.depth) {
                    screenHovered = false;
                } else {
                    lensHovered = false;
                }
            }
        }

        // Gizmo Dragging Logic
        if (isGizmoDragging_) {
            if (ImGui::IsKeyPressed(ImGuiKey_Escape)) {
                // ESC full restoration of start position
                if (draggedTarget_ == GizmoTarget::Lens) {
                    auto candidate = scene_;
                    candidate.lens.planeZMetres = dragInitialLensZ_;
                    if (dragApertureWasCoplanar_) {
                        candidate.aperture.planeZMetres = dragInitialApertureZ_;
                    }
                    applyScene(candidate, tracerOptions_);
                } else if (draggedTarget_ == GizmoTarget::Screen) {
                    auto candidate = scene_;
                    candidate.screen.planeZMetres = dragInitialScreenZ_;
                    applyScene(candidate, tracerOptions_);
                }
                isGizmoDragging_ = false;
                draggedTarget_ = GizmoTarget::None;
                gizmoDragChanged_ = false;
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Release drag on left mouse up
                isGizmoDragging_ = false;
                draggedTarget_ = GizmoTarget::None;
                if (gizmoDragChanged_) {
                    recordLessonEdit();
                    gizmoDragChanged_ = false;
                }
            } else {
                // Active dragging along +Z axis
                const auto& activeAxisProj = (draggedTarget_ == GizmoTarget::Lens) ? lensAxisProj : screenAxisProj;
                const double deltaZ = gizmo::computeGizmoDeltaZ(mouseDelta, activeAxisProj);

                if (std::abs(deltaZ) >= 1e-5 && std::isfinite(deltaZ)) {
                    if (draggedTarget_ == GizmoTarget::Lens) {
                        auto candidate = scene_;
                        candidate.lens.planeZMetres += deltaZ;
                        if (dragApertureWasCoplanar_) {
                            candidate.aperture.planeZMetres = candidate.lens.planeZMetres;
                        }
                        gizmoDragChanged_ = applyScene(candidate, tracerOptions_)
                            || gizmoDragChanged_;
                    } else if (draggedTarget_ == GizmoTarget::Screen) {
                        auto candidate = scene_;
                        candidate.screen.planeZMetres += deltaZ;
                        gizmoDragChanged_ = applyScene(candidate, tracerOptions_)
                            || gizmoDragChanged_;
                    }
                }
            }
        }

        // Interaction triggering (when not currently dragging)
        if (isHovered && noActiveWidget && !isGizmoDragging_) {
            if (std::abs(io.MouseWheel) > 0.0F) {
                camera_.zoom(io.MouseWheel);
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Right)) {
                isOrbiting_ = true;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Middle)) {
                isPanning_ = true;
            }
            if (ImGui::IsMouseClicked(ImGuiMouseButton_Left)) {
                if (lensHovered) {
                    isGizmoDragging_ = true;
                    draggedTarget_ = GizmoTarget::Lens;
                    selectedTarget_ = GizmoTarget::Lens;
                    dragInitialLensZ_ = scene_.lens.planeZMetres;
                    dragInitialApertureZ_ = scene_.aperture.planeZMetres;
                    dragInitialScreenZ_ = scene_.screen.planeZMetres;
                    dragApertureWasCoplanar_ = (std::abs(scene_.aperture.planeZMetres - scene_.lens.planeZMetres) < 1e-4);
                    gizmoDragChanged_ = false;
                } else if (screenHovered) {
                    isGizmoDragging_ = true;
                    draggedTarget_ = GizmoTarget::Screen;
                    selectedTarget_ = GizmoTarget::Screen;
                    dragInitialLensZ_ = scene_.lens.planeZMetres;
                    dragInitialApertureZ_ = scene_.aperture.planeZMetres;
                    dragInitialScreenZ_ = scene_.screen.planeZMetres;
                    dragApertureWasCoplanar_ = (std::abs(scene_.aperture.planeZMetres - scene_.lens.planeZMetres) < 1e-4);
                    gizmoDragChanged_ = false;
                } else {
                    selectedTarget_ = GizmoTarget::None;
                }
            }
        }

        if (!noActiveWidget && !isGizmoDragging_) {
            isOrbiting_ = false;
            isPanning_ = false;
        }

        if (isOrbiting_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Right)) {
                constexpr float kOrbitSensitivity = 0.005F;
                camera_.orbit(
                    -io.MouseDelta.x * kOrbitSensitivity,
                    io.MouseDelta.y * kOrbitSensitivity);
            } else {
                isOrbiting_ = false;
            }
        }

        if (isPanning_) {
            if (ImGui::IsMouseDown(ImGuiMouseButton_Middle)) {
                const float panFactor = 0.0015F * camera_.distance();
                camera_.pan(-io.MouseDelta.x * panFactor, io.MouseDelta.y * panFactor);
            } else {
                isPanning_ = false;
            }
        }

        // Draw Gizmos and HUD
        ImDrawList* drawList = ImGui::GetWindowDrawList();
        if (drawList != nullptr) {
            drawList->PushClipRect(imagePosMin, ImVec2(imagePosMin.x + imageSize.x, imagePosMin.y + imageSize.y), true);

            // Lens gizmo
            constexpr ImU32 kLensPrimaryColor = IM_COL32(56, 189, 248, 255);
            constexpr ImU32 kLensHoverColor = IM_COL32(125, 211, 252, 255);
            drawGizmoHandle(
                drawList,
                projLensCenter,
                lensAxisProj,
                lensHovered,
                selectedTarget_ == GizmoTarget::Lens,
                isGizmoDragging_ && draggedTarget_ == GizmoTarget::Lens,
                "Lens",
                scene_.lens.planeZMetres,
                kLensPrimaryColor,
                kLensHoverColor);

            // Screen gizmo
            constexpr ImU32 kScreenPrimaryColor = IM_COL32(251, 146, 60, 255);
            constexpr ImU32 kScreenHoverColor = IM_COL32(253, 186, 116, 255);
            drawGizmoHandle(
                drawList,
                projScreenCenter,
                screenAxisProj,
                screenHovered,
                selectedTarget_ == GizmoTarget::Screen,
                isGizmoDragging_ && draggedTarget_ == GizmoTarget::Screen,
                "Screen",
                scene_.screen.planeZMetres,
                kScreenPrimaryColor,
                kScreenHoverColor);

            // HUD
            drawViewportHud(
                drawList,
                imagePosMin,
                selectedTarget_,
                draggedTarget_,
                isGizmoDragging_);

            drawList->PopClipRect();
        }
        }
    } else {
        const bool shouldCommitGizmoDrag
            = isGizmoDragging_ && gizmoDragChanged_;
        isOrbiting_ = false;
        isPanning_ = false;
        sandboxGizmoDragging_ = false;
        sandboxGizmoChanged_ = false;
        sandboxMechanicalDragging_ = false;
        sandboxMechanicalChanged_ = false;
        sandboxMechanicalControl_ = SandboxMechanicalControl::None;
        isGizmoDragging_ = false;
        draggedTarget_ = GizmoTarget::None;
        if (shouldCommitGizmoDrag) {
            recordLessonEdit();
        }
        gizmoDragChanged_ = false;
    }
    ImGui::End();

    ImGui::Begin(docking::DockLayoutConfig::kInspectorWindowName);

    drawSandboxInspector();
    ImGui::SeparatorText("Legacy Reference Workbenches");

    ImGui::SeparatorText("Legacy Edit History");
    ImGui::BeginDisabled(!lessonEditHistory_.canUndo());
    if (ImGui::Button("Undo (Ctrl+Z)")) {
        undoLessonEdit();
    }
    ImGui::EndDisabled();
    ImGui::SameLine();
    ImGui::BeginDisabled(!lessonEditHistory_.canRedo());
    if (ImGui::Button("Redo (Ctrl+Y)")) {
        redoLessonEdit();
    }
    ImGui::EndDisabled();
    ImGui::TextDisabled(
        "%zu undo / %zu redo; lesson progress is separate",
        lessonEditHistory_.undoDepth(), lessonEditHistory_.redoDepth());

    drawReflectionRefractionPanel();

    if (ImGui::CollapsingHeader("Active Gizmo Selection", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* selName = (selectedTarget_ == GizmoTarget::Lens) ? "Thin Lens"
            : (selectedTarget_ == GizmoTarget::Screen) ? "Screen" : "None (Click 3D handle)";
        ImGui::Text("Selected Component: %s", selName);
        if (isGizmoDragging_) {
            const char* dName = (draggedTarget_ == GizmoTarget::Lens) ? "Thin Lens" : "Screen";
            ImGui::TextColored(ImVec4(0.4F, 0.9F, 1.0F, 1.0F), "Status: Dragging %s (+Z optical axis)", dName);
        } else {
            ImGui::TextDisabled("Status: Ready (LMB drag 3D handle, ESC to cancel)");
        }

        if (ImGui::Button("Select Lens")) {
            selectedTarget_ = GizmoTarget::Lens;
        }
        ImGui::SameLine();
        if (ImGui::Button("Select Screen")) {
            selectedTarget_ = GizmoTarget::Screen;
        }
        ImGui::SameLine();
        if (ImGui::Button("Deselect")) {
            selectedTarget_ = GizmoTarget::None;
        }
    }

    if (ImGui::CollapsingHeader("Presets", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Real Image")) {
            applySceneProject(
                optics::scene::createDefaultRealImageScene(), tracerOptions_, {});
        }
        ImGui::SameLine();
        if (ImGui::Button("Virtual Image")) {
            applySceneProject(
                optics::scene::createDefaultVirtualImageScene(), tracerOptions_, {});
        }
        ImGui::SameLine();
        if (ImGui::Button("Collimated")) {
            applySceneProject(
                optics::scene::createDefaultInfinityScene(), tracerOptions_, {});
        }
    }

    if (ImGui::CollapsingHeader("Analytic Thin-Lens Prediction", ImGuiTreeNodeFlags_DefaultOpen)) {
        const char* natureStr = "Unknown";
        switch (prediction_.nature) {
        case optics::scene::ImageNature::Real:
            natureStr = "Real (Convergent)";
            break;
        case optics::scene::ImageNature::Virtual:
            natureStr = "Virtual (Divergent)";
            break;
        case optics::scene::ImageNature::AtInfinity:
            natureStr = "At Infinity (Collimated)";
            break;
        }
        ImGui::Text("Image Nature: %s", natureStr);
        ImGui::Text("Object Distance u: %.2f mm", prediction_.objectDistanceMetres * 1000.0);

        if (prediction_.nature == optics::scene::ImageNature::AtInfinity) {
            ImGui::Text("Image Distance v: inf");
            ImGui::Text("Image Plane Z: inf");
            ImGui::Text("Transverse Magnification m: inf");
            ImGui::Text("Image Position: inf");
        } else {
            ImGui::Text("Image Distance v: %.2f mm", prediction_.imageDistanceMetres * 1000.0);
            ImGui::Text("Image Plane Z: %.2f mm", prediction_.imagePlaneZMetres * 1000.0);
            ImGui::Text("Transverse Magnification m: %.3f", prediction_.transverseMagnification);
            ImGui::Text("Image Position: (%.2f, %.2f, %.2f) mm",
                prediction_.imagePositionMetres.x * 1000.0,
                prediction_.imagePositionMetres.y * 1000.0,
                prediction_.imagePositionMetres.z * 1000.0);
        }
    }

    if (ImGui::CollapsingHeader("Numerical Aperture (Object-Side NA)", ImGuiTreeNodeFlags_DefaultOpen)) {
        ImGui::Text("Object-Side NA: %.4f", naResult_.numericalAperture);
        ImGui::Text("Acceptance Half-Angle: %.2f deg (%.4f rad)",
            glm::degrees(static_cast<float>(naResult_.halfAngleRadians)),
            naResult_.halfAngleRadians);

        const char* stopKindStr = (naResult_.limitingStop == optics::scene::LimitingStopKind::Lens)
            ? "Thin Lens Clear Aperture"
            : "Independent Aperture";
        ImGui::Text("Limiting Stop: %s (ID: %s)", stopKindStr, naResult_.limitingStopId.c_str());
        ImGui::Text("Stop Axial Distance d: %.2f mm", naResult_.axialDistanceMetres * 1000.0);
        ImGui::Text("Stop Rim Radius R: %.2f mm", naResult_.rimRadiusMetres * 1000.0);
        ImGui::Text("Stop Rim Center: (%.2f, %.2f, %.2f) mm",
            naResult_.rimCenterMetres.x * 1000.0,
            naResult_.rimCenterMetres.y * 1000.0,
            naResult_.rimCenterMetres.z * 1000.0);

        if (naResult_.approximate || naResult_.downstreamStopNotModeled) {
            ImGui::Spacing();
            ImGui::PushStyleColor(ImGuiCol_Text, ImVec4(0.98F, 0.73F, 0.20F, 1.0F));
            ImGui::TextWrapped("Warning: %s",
                naResult_.warningMessage.empty()
                    ? "Aperture is downstream of lens; entrance pupil is approximate."
                    : naResult_.warningMessage.c_str());
            ImGui::PopStyleColor();
        } else {
            ImGui::TextDisabled("Approximation: None (exact entrance pupil in object space)");
        }
    }

    if (ImGui::CollapsingHeader("Point Source", ImGuiTreeNodeFlags_DefaultOpen)) {
        const double currentU = (scene_.lens.planeZMetres - scene_.source.positionMetres.z) * 1000.0;
        float uMm = static_cast<float>(currentU);
        if (ImGui::DragFloat("Object Distance u (mm)", &uMm, 0.5F, 1.0F, 2000.0F, "%.2f mm")) {
            if (uMm > 0.0F && std::isfinite(uMm)) {
                auto candidate = scene_;
                candidate.source.positionMetres.z = candidate.lens.planeZMetres - static_cast<double>(uMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Object distance u must be finite and positive";
                statusMessage_.clear();
            }
        }

        float srcX = static_cast<float>(scene_.source.positionMetres.x * 1000.0);
        if (ImGui::DragFloat("Source X (mm)", &srcX, 0.1F, -200.0F, 200.0F, "%.2f mm")) {
            if (std::isfinite(srcX)) {
                auto candidate = scene_;
                candidate.source.positionMetres.x = static_cast<double>(srcX) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Source X must be finite";
                statusMessage_.clear();
            }
        }

        float srcY = static_cast<float>(scene_.source.positionMetres.y * 1000.0);
        if (ImGui::DragFloat("Source Y (mm)", &srcY, 0.1F, -200.0F, 200.0F, "%.2f mm")) {
            if (std::isfinite(srcY)) {
                auto candidate = scene_;
                candidate.source.positionMetres.y = static_cast<double>(srcY) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Source Y must be finite";
                statusMessage_.clear();
            }
        }

        float wlNm = static_cast<float>(scene_.source.wavelengthMetres * 1e9);
        if (ImGui::DragFloat("Wavelength (nm)", &wlNm, 1.0F, 200.0F, 2000.0F, "%.1f nm")) {
            if (wlNm > 0.0F && std::isfinite(wlNm)) {
                auto candidate = scene_;
                candidate.source.wavelengthMetres = static_cast<double>(wlNm) * 1e-9;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Wavelength must be positive and finite";
                statusMessage_.clear();
            }
        }
    }

    if (ImGui::CollapsingHeader("Thin Lens", ImGuiTreeNodeFlags_DefaultOpen)) {
        float lensZMm = static_cast<float>(scene_.lens.planeZMetres * 1000.0);
        if (ImGui::DragFloat("Lens Z (mm)", &lensZMm, 0.5F, -500.0F, 2000.0F, "%.2f mm")) {
            if (std::isfinite(lensZMm)) {
                auto candidate = scene_;
                const bool coplanar = (std::abs(candidate.aperture.planeZMetres - candidate.lens.planeZMetres) < 1e-4);
                candidate.lens.planeZMetres = static_cast<double>(lensZMm) * 1e-3;
                if (coplanar) {
                    candidate.aperture.planeZMetres = candidate.lens.planeZMetres;
                }
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Lens Z must be finite";
                statusMessage_.clear();
            }
        }

        float fMm = static_cast<float>(scene_.lens.focalLengthMetres * 1000.0);
        if (ImGui::DragFloat("Focal Length f (mm)", &fMm, 0.5F, -1000.0F, 1000.0F, "%.2f mm")) {
            if (std::isfinite(fMm) && std::abs(fMm) >= 0.1F) {
                auto candidate = scene_;
                candidate.lens.focalLengthMetres = static_cast<double>(fMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else if (std::abs(fMm) < 0.1F) {
                errorMessage_ = "Focal length f cannot be zero or near-zero (|f| >= 0.1 mm)";
                statusMessage_.clear();
            } else {
                errorMessage_ = "Focal length f must be finite";
                statusMessage_.clear();
            }
        }

        float lensRMm = static_cast<float>(scene_.lens.clearApertureRadiusMetres * 1000.0);
        if (ImGui::DragFloat("Lens Radius (mm)", &lensRMm, 0.2F, 0.5F, 200.0F, "%.2f mm")) {
            if (lensRMm > 0.0F && std::isfinite(lensRMm)) {
                auto candidate = scene_;
                candidate.lens.clearApertureRadiusMetres = static_cast<double>(lensRMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Lens clear aperture radius must be positive";
                statusMessage_.clear();
            }
        }
    }

    if (ImGui::CollapsingHeader("Independent Aperture", ImGuiTreeNodeFlags_DefaultOpen)) {
        float apZMm = static_cast<float>(scene_.aperture.planeZMetres * 1000.0);
        if (ImGui::DragFloat("Aperture Z (mm)", &apZMm, 0.5F, -500.0F, 1000.0F, "%.2f mm")) {
            if (std::isfinite(apZMm)) {
                auto candidate = scene_;
                candidate.aperture.planeZMetres = static_cast<double>(apZMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Aperture Z must be finite";
                statusMessage_.clear();
            }
        }

        float apRMm = static_cast<float>(scene_.aperture.radiusMetres * 1000.0);
        if (ImGui::DragFloat("Aperture Radius (mm)", &apRMm, 0.2F, 0.5F, 200.0F, "%.2f mm")) {
            if (apRMm > 0.0F && std::isfinite(apRMm)) {
                auto candidate = scene_;
                candidate.aperture.radiusMetres = static_cast<double>(apRMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Aperture radius must be positive";
                statusMessage_.clear();
            }
        }
    }

    if (ImGui::CollapsingHeader("Screen", ImGuiTreeNodeFlags_DefaultOpen)) {
        float scrZMm = static_cast<float>(scene_.screen.planeZMetres * 1000.0);
        if (ImGui::DragFloat("Screen Z (mm)", &scrZMm, 0.5F, -500.0F, 2000.0F, "%.2f mm")) {
            if (std::isfinite(scrZMm)) {
                auto candidate = scene_;
                candidate.screen.planeZMetres = static_cast<double>(scrZMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Screen Z must be finite";
                statusMessage_.clear();
            }
        }

        float scrWMm = static_cast<float>(scene_.screen.widthMetres * 1000.0);
        if (ImGui::DragFloat("Screen Width (mm)", &scrWMm, 0.5F, 1.0F, 500.0F, "%.2f mm")) {
            if (scrWMm > 0.0F && std::isfinite(scrWMm)) {
                auto candidate = scene_;
                candidate.screen.widthMetres = static_cast<double>(scrWMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Screen width must be positive";
                statusMessage_.clear();
            }
        }

        float scrHMm = static_cast<float>(scene_.screen.heightMetres * 1000.0);
        if (ImGui::DragFloat("Screen Height (mm)", &scrHMm, 0.5F, 1.0F, 500.0F, "%.2f mm")) {
            if (scrHMm > 0.0F && std::isfinite(scrHMm)) {
                auto candidate = scene_;
                candidate.screen.heightMetres = static_cast<double>(scrHMm) * 1e-3;
                applyScene(candidate, tracerOptions_);
            } else {
                errorMessage_ = "Screen height must be positive";
                statusMessage_.clear();
            }
        }
    }

    if (ImGui::CollapsingHeader("Ray Tracer", ImGuiTreeNodeFlags_DefaultOpen)) {
        int rayCount = static_cast<int>(tracerOptions_.rayCount);
        if (ImGui::SliderInt("Ray Count", &rayCount, 1, 10000)) {
            if (rayCount >= 1 && rayCount <= 10000) {
                auto candidateOptions = tracerOptions_;
                candidateOptions.rayCount = static_cast<std::size_t>(rayCount);
                applyScene(scene_, candidateOptions);
            }
        }

        constexpr std::array<const char*, 6> patternNames {
            "Fibonacci Disk",
            "Concentric Rings",
            "Meridional Fan (Y)",
            "Sagittal Fan (X)",
            "Cross Fans (XY)",
            "Aperture Boundary"
        };
        int currentPattern = static_cast<int>(tracerOptions_.pattern);
        if (ImGui::Combo("Sampling Pattern", &currentPattern, patternNames.data(), static_cast<int>(patternNames.size()))) {
            auto candidateOptions = tracerOptions_;
            candidateOptions.pattern = static_cast<optics::ray::RaySamplingPattern>(currentPattern);
            applyScene(scene_, candidateOptions);
        }

        bool includeVirt = tracerOptions_.includeVirtualExtensions;
        if (ImGui::Checkbox("Include Virtual Extensions", &includeVirt)) {
            auto candidateOptions = tracerOptions_;
            candidateOptions.includeVirtualExtensions = includeVirt;
            applyScene(scene_, candidateOptions);
        }
    }

    if (ImGui::CollapsingHeader("Project File", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (sceneProjectProvenance_.originKind
            == project::ProjectOriginKind::LessonTemplate) {
            ImGui::Text(
                "Origin: lesson template %s v%d",
                sceneProjectProvenance_.sourceId.c_str(),
                sceneProjectProvenance_.sourceVersion);
        } else {
            ImGui::TextDisabled("Origin: user project");
        }
        ImGui::InputText("Path", projectPathBuffer_, sizeof(projectPathBuffer_));
        if (ImGui::Button("Save Scene")) {
            saveSceneToPath(projectPathBuffer_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Load Scene")) {
            loadSceneFromPath(projectPathBuffer_);
        }
    }

    if (ImGui::CollapsingHeader("Camera Controls", ImGuiTreeNodeFlags_DefaultOpen)) {
        if (ImGui::Button("Reset Camera")) {
            camera_.reset();
        }
        if (viewportMode_ == ViewportMode::Sandbox) {
            ImGui::SameLine();
            ImGui::BeginDisabled(
                benchProject_.scene.find(selectedBenchComponentId_) == nullptr);
            if (ImGui::Button("Focus Selected (F)")) {
                focusSandboxSelection();
            }
            ImGui::EndDisabled();
        }
        ImGui::SameLine();
        if (ImGui::Button("Perspective")) {
            camera_.setPresetView(render::CameraPresetView::Perspective);
        }
        if (ImGui::Button("Top (XZ)")) {
            camera_.setPresetView(render::CameraPresetView::TopXZ);
        }
        ImGui::SameLine();
        if (ImGui::Button("Side (YZ)")) {
            camera_.setPresetView(render::CameraPresetView::SideYZ);
        }
        ImGui::SameLine();
        if (ImGui::Button("Front (XY)")) {
            camera_.setPresetView(render::CameraPresetView::FrontXY);
        }

        ImGui::Separator();
        ImGui::Text("Target: (%.2f, %.2f, %.2f) m",
            static_cast<double>(camera_.target().x),
            static_cast<double>(camera_.target().y),
            static_cast<double>(camera_.target().z));
        ImGui::Text("Distance: %.2f m", static_cast<double>(camera_.distance()));
        ImGui::Text("Yaw: %.1f deg, Pitch: %.1f deg",
            static_cast<double>(glm::degrees(camera_.yaw())),
            static_cast<double>(glm::degrees(camera_.pitch())));
        ImGui::Text("FOV: %.1f deg", static_cast<double>(camera_.fovDegrees()));
    }

    if (!errorMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(1.0F, 0.3F, 0.3F, 1.0F), "Error: %s", errorMessage_.c_str());
    } else if (!statusMessage_.empty()) {
        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.4F, 0.9F, 0.5F, 1.0F), "%s", statusMessage_.c_str());
    }

    ImGui::End();

    ImGui::Begin(docking::DockLayoutConfig::kValidationWindowName);
    ImGui::TextDisabled("Milestone: M1 3D Optical Bench + Geometric Optics");
    ImGui::Separator();
    ImGui::BulletText("Project format: v1 (JSON)");
    ImGui::BulletText("3D Grid & Axes: M1 OpenGL 4.6 bench active");
    ImGui::BulletText("CPU Thin Lens: Paraxial analytic model validated");
    ImGui::BulletText("CPU Camera: Orthonormal basis & projections validated");
    ImGui::BulletText("Traced Rays: %zu (Displayed segments: %zu)", tracerOptions_.rayCount, raySegments_.size());
    if (!errorMessage_.empty()) {
        ImGui::BulletText("Status: Error (%s)", errorMessage_.c_str());
    } else {
        ImGui::BulletText("Status: Nominal");
    }
    ImGui::End();

    drawWaveDetectorPanel();
    drawSamplingDebuggerPanel();
    drawRealLensPanel();
    drawSlmInterferencePanel();
    drawHolographyPanel();
    drawLearnPanel();
}

void Application::runSandboxInteractionSmoke() {
    namespace bench = optics::scene;

    viewportMode_ = ViewportMode::Sandbox;
    sandboxComponentSearch_[0] = '\0';
    camera_.setPresetView(render::CameraPresetView::Perspective);
    camera_.setTarget({0.0F, 0.0F, 0.0F});
    camera_.setDistance(0.8F);

    const auto drawInputFrame = [this](
                                    const glm::vec2& mousePosition,
                                    int leftButtonState,
                                    ImGuiKey key = ImGuiKey_None,
                                    int keyState = -1,
                                    int rightButtonState = -1,
                                    bool shiftDown = false) {
        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGuiIO& io = ImGui::GetIO();
        io.AddMousePosEvent(mousePosition.x, mousePosition.y);
        if (leftButtonState >= 0) {
            io.AddMouseButtonEvent(0, leftButtonState != 0);
        }
        if (rightButtonState >= 0) {
            io.AddMouseButtonEvent(1, rightButtonState != 0);
        }
        io.AddKeyEvent(ImGuiMod_Shift, shiftDown);
        if (key != ImGuiKey_None && keyState >= 0) {
            io.AddKeyEvent(key, keyState != 0);
        }
        ImGui::NewFrame();
        ImGui::SetWindowFocus(
            docking::DockLayoutConfig::kOpticalBenchWindowName);
        drawWorkspace();
        ImGui::Render();

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width <= 0 || height <= 0) {
            throw std::runtime_error(
                "interaction smoke window has no drawable extent");
        }
        glViewport(0, 0, width, height);
        glClearColor(0.035F, 0.045F, 0.060F, 1.0F);
        glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
        ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
    };

    const auto requireBounds = [](const UiItemBounds& bounds,
                                   std::string_view name) {
        if (!bounds.current) {
            throw std::runtime_error(
                std::string(name) + " is not visible in the Bench UI");
        }
    };
    const auto clickPoint = [&](const glm::vec2& point) {
        drawInputFrame(point, 0);
        drawInputFrame(point, 1);
        drawInputFrame(point, 0);
    };
    const auto click = [&](UiItemBounds bounds, std::string_view name) {
        requireBounds(bounds, name);
        clickPoint(bounds.centre());
    };
    const auto dragPoints = [&](const glm::vec2& origin,
                                const glm::vec2& destination,
                                std::string_view name) {
        const glm::vec2 delta = destination - origin;
        const float length = glm::length(delta);
        if (!std::isfinite(length) || length < 20.0F) {
            throw std::runtime_error(
                std::string(name) + " drag path is degenerate");
        }
        const glm::vec2 firstMove = origin + delta * (12.0F / length);
        drawInputFrame(origin, 0);
        drawInputFrame(origin, 1);
        drawInputFrame(firstMove, -1);
        drawInputFrame(destination, -1);
        drawInputFrame(destination, 0);
    };
    const auto drag = [&](UiItemBounds source,
                          const glm::vec2& destination,
                          std::string_view name) {
        requireBounds(source, name);
        dragPoints(source.centre(), destination, name);
    };

    drawInputFrame({-1000.0F, -1000.0F}, 0);
    requireBounds(sandboxUiEvidence_.emptyBench, "Empty Bench action");
    requireBounds(sandboxUiEvidence_.laserShelf, "Laser shelf item");
    requireBounds(sandboxUiEvidence_.plateShelf, "Plate shelf item");
    requireBounds(sandboxUiEvidence_.viewport, "3D Bench viewport");

    const glm::vec2 orbitOrigin = sandboxUiEvidence_.viewport.centre();
    const float yawBeforeOrbit = camera_.yaw();
    const float pitchBeforeOrbit = camera_.pitch();
    drawInputFrame(orbitOrigin, 0, ImGuiKey_None, -1, 0);
    drawInputFrame(orbitOrigin, -1, ImGuiKey_None, -1, 1);
    drawInputFrame(
        orbitOrigin + glm::vec2(30.0F, 20.0F),
        -1,
        ImGuiKey_None,
        -1,
        -1);
    drawInputFrame(
        orbitOrigin + glm::vec2(30.0F, 20.0F),
        -1,
        ImGuiKey_None,
        -1,
        0);
    if (!(camera_.yaw() < yawBeforeOrbit
            && camera_.pitch() > pitchBeforeOrbit)) {
        throw std::runtime_error(
            "RMB viewport orbit did not follow direct drag direction");
    }
    camera_.setPresetView(render::CameraPresetView::Perspective);
    camera_.setTarget({0.0F, 0.0F, 0.0F});
    camera_.setDistance(0.8F);
    drawInputFrame({-1000.0F, -1000.0F}, 0);

    click(sandboxUiEvidence_.emptyBench, "Empty Bench action");
    if (!benchProject_.scene.components().empty()) {
        throw std::runtime_error(
            "Empty Bench click did not clear the ordinary scene");
    }

    const auto tableDropAt = [&](float horizontalFraction) {
        const UiItemBounds viewport = sandboxUiEvidence_.viewport;
        requireBounds(viewport, "3D Bench viewport");
        const glm::vec2 viewportExtent
            = viewport.maximum - viewport.minimum;
        const glm::mat4 viewProjection
            = camera_.projectionMatrix(viewportExtent.x / viewportExtent.y)
            * camera_.viewMatrix();
        for (int row = 4; row <= 18; ++row) {
            const float verticalFraction = static_cast<float>(row) * 0.05F;
            const glm::vec2 point = viewport.minimum
                + glm::vec2(
                    viewportExtent.x * horizontalFraction,
                    viewportExtent.y * verticalFraction);
            if (gizmo::unprojectScreenToHorizontalPlane(
                    point,
                    0.0F,
                    viewProjection,
                    viewport.minimum,
                    viewportExtent)
                    .hit) {
                return point;
            }
        }
        throw std::runtime_error(
            "controlled interaction camera exposes no optical-table drop point");
    };
    const auto projectWorldToCurrentViewport = [&](const math::Vec3d& world) {
        const UiItemBounds currentViewport = sandboxUiEvidence_.viewport;
        requireBounds(currentViewport, "3D Bench viewport");
        const glm::vec2 extent
            = currentViewport.maximum - currentViewport.minimum;
        const glm::mat4 viewProjection
            = camera_.projectionMatrix(extent.x / extent.y)
            * camera_.viewMatrix();
        const auto projected = gizmo::projectWorldToViewport(
            {
                static_cast<float>(world.x),
                static_cast<float>(world.y),
                static_cast<float>(world.z),
            },
            viewProjection,
            currentViewport.minimum,
            extent);
        if (!projected.visible) {
            throw std::runtime_error(
                "controlled world point is outside the current 3D Bench viewport");
        }
        return projected.screenPos;
    };
    const auto dropAtWorld = [&](UiItemBounds shelfItem,
                                 const math::Vec3d& world,
                                 std::string_view name) {
        drag(shelfItem, projectWorldToCurrentViewport(world), name);
        drawInputFrame({-1000.0F, -1000.0F}, 0);
        if (selectedBenchComponentId_.empty()) {
            throw std::runtime_error(
                std::string(name) + " drop created no selected component");
        }
        return selectedBenchComponentId_;
    };
    const auto selectWorldComponent = [&](const math::Vec3d& world) {
        clickPoint(projectWorldToCurrentViewport(world));
        drawInputFrame({-1000.0F, -1000.0F}, 0);
    };
    const glm::vec2 laserDrop = tableDropAt(0.42F);
    const glm::vec2 plateDrop = tableDropAt(0.58F);
    drag(sandboxUiEvidence_.laserShelf, laserDrop, "Laser shelf item");
    if (benchProject_.scene.components().size() != 1U
        || benchProject_.scene.components().front().kind
            != bench::BenchComponentKind::LaserSource) {
        throw std::runtime_error(
            "shelf-to-table laser drag did not create one ordinary laser (count="
            + std::to_string(benchProject_.scene.components().size())
            + ", status=" + statusMessage_ + ", error=" + errorMessage_ + ")");
    }
    drag(sandboxUiEvidence_.plateShelf, plateDrop, "Plate shelf item");
    if (benchProject_.scene.components().size() != 2U
        || std::none_of(
            benchProject_.scene.components().begin(),
            benchProject_.scene.components().end(),
            [](const auto& component) {
                return component.kind
                    == bench::BenchComponentKind::HolographicPlate;
            })) {
        throw std::runtime_error(
            "shelf-to-table plate drag did not add an ordinary plate");
    }

    const auto* navigationSelection = benchProject_.scene.find(
        selectedBenchComponentId_);
    if (navigationSelection == nullptr) {
        throw std::runtime_error(
            "camera navigation smoke has no selected component");
    }
    const auto focusPosition = navigationSelection->transform.translationMetres;
    const glm::vec2 navigationPoint = sandboxUiEvidence_.viewport.centre();
    drawInputFrame(navigationPoint, 0, ImGuiKey_F, 1);
    drawInputFrame(navigationPoint, 0, ImGuiKey_F, 0);
    const glm::vec3 expectedFocus {
        static_cast<float>(focusPosition.x),
        static_cast<float>(focusPosition.y),
        static_cast<float>(focusPosition.z),
    };
    if (glm::length(camera_.target() - expectedFocus) > 1e-6F) {
        throw std::runtime_error(
            "F key did not focus the camera on the selected component");
    }
    const glm::vec3 targetBeforeRoam = camera_.target();
    const glm::vec3 forwardBeforeRoam = camera_.forwardVector();
    drawInputFrame(navigationPoint, 0, ImGuiKey_W, 1, -1, true);
    drawInputFrame(navigationPoint, 0, ImGuiKey_W, 0, -1, false);
    if (glm::dot(
            camera_.target() - targetBeforeRoam,
            forwardBeforeRoam) <= 0.0F) {
        throw std::runtime_error(
            "Shift+W did not roam the camera forward");
    }
    camera_.setPresetView(render::CameraPresetView::Perspective);
    camera_.setTarget({0.0F, 0.0F, 0.0F});
    camera_.setDistance(0.8F);

    // The selected plate adds the contextual experiment bar on the following
    // frame, so sample handle coordinates only after that layout transition.
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    sandboxGizmoMode_ = SandboxGizmoMode::Translate;
    const auto* selectedBeforeGizmo
        = benchProject_.scene.find(selectedBenchComponentId_);
    if (selectedBeforeGizmo == nullptr
        || selectedBeforeGizmo->kind
            != bench::BenchComponentKind::HolographicPlate) {
        throw std::runtime_error(
            "placed plate was not selected for constrained manipulation");
    }
    const std::string gizmoComponentId = selectedBeforeGizmo->id;
    const math::Vec3d translationBeforeGizmo
        = selectedBeforeGizmo->transform.translationMetres;
    std::size_t dragAxis = sandboxUiEvidence_.gizmoEndpoints.size();
    for (std::size_t axis = 0;
         axis < sandboxUiEvidence_.gizmoEndpoints.size(); ++axis) {
        if (sandboxUiEvidence_.gizmoEndpoints[axis].visible
            && !sandboxUiEvidence_.gizmoProjections[axis].isDegenerate) {
            dragAxis = axis;
            break;
        }
    }
    if (dragAxis == sandboxUiEvidence_.gizmoEndpoints.size()) {
        throw std::runtime_error(
            "selected plate exposes no usable constrained transform handle");
    }
    const glm::vec2 handleOrigin
        = sandboxUiEvidence_.gizmoEndpoints[dragAxis].screenPos;
    const glm::vec2 handleDestination = handleOrigin
        + sandboxUiEvidence_.gizmoProjections[dragAxis].screenDir * 36.0F;
    dragPoints(
        handleOrigin,
        handleDestination,
        "selected plate constrained transform handle");
    const auto* selectedAfterGizmo
        = benchProject_.scene.find(gizmoComponentId);
    if (selectedAfterGizmo == nullptr
        || selectedAfterGizmo->transform.translationMetres
            == translationBeforeGizmo) {
        throw std::runtime_error(
            "constrained transform-handle drag did not move the selected plate (axis="
            + std::to_string(dragAxis) + ", metres_per_pixel="
            + std::to_string(
                sandboxUiEvidence_.gizmoProjections[dragAxis].metresPerPixel)
            + ", selected=" + selectedBenchComponentId_ + ", status="
            + statusMessage_ + ", error=" + errorMessage_ + ")");
    }
    if (!selectedAfterGizmo->mechanicalAssembly.has_value()) {
        throw std::runtime_error(
            "newly placed plate has no direct mechanical assembly");
    }

    drawInputFrame({-1000.0F, -1000.0F}, 0);
    std::size_t mechanicalControlIndex
        = sandboxUiEvidence_.mechanicalProjections.size();
    for (std::size_t index = 0;
         index < sandboxUiEvidence_.mechanicalProjections.size(); ++index) {
        if (sandboxUiEvidence_.mechanicalHandles[index].visible
            && !sandboxUiEvidence_.mechanicalProjections[index].isDegenerate) {
            mechanicalControlIndex = index;
            break;
        }
    }
    if (mechanicalControlIndex
        == sandboxUiEvidence_.mechanicalProjections.size()) {
        throw std::runtime_error(
            "mounted plate exposes no usable direct mechanical handle");
    }
    const auto mechanicalValue = [](const bench::MechanicalAssemblyState& state,
                                     std::size_t index) {
        switch (index) {
        case 0U: return state.postHeightMetres;
        case 1U: return state.stageTranslationMetres.x;
        case 2U: return state.stageTranslationMetres.y;
        case 3U: return state.stageTranslationMetres.z;
        default: return std::numeric_limits<double>::quiet_NaN();
        }
    };
    const auto mechanicalBefore
        = *selectedAfterGizmo->mechanicalAssembly;
    const auto opticalBeforeMechanical = selectedAfterGizmo->transform;
    const glm::vec2 mechanicalHandle
        = sandboxUiEvidence_
              .mechanicalHandles[mechanicalControlIndex]
              .screenPos;
    dragPoints(
        mechanicalHandle,
        mechanicalHandle
            + sandboxUiEvidence_
                  .mechanicalProjections[mechanicalControlIndex]
                  .screenDir
                * 32.0F,
        "selected plate mechanical control");
    const auto* selectedAfterMechanical
        = benchProject_.scene.find(gizmoComponentId);
    if (selectedAfterMechanical == nullptr
        || !selectedAfterMechanical->mechanicalAssembly.has_value()
        || mechanicalValue(
               *selectedAfterMechanical->mechanicalAssembly,
               mechanicalControlIndex)
            == mechanicalValue(mechanicalBefore, mechanicalControlIndex)
        || selectedAfterMechanical->transform == opticalBeforeMechanical) {
        throw std::runtime_error(
            "direct mechanical-handle drag did not update mechanical and optical truth");
    }

    focusSandboxSelection();
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    std::size_t angularControlIndex
        = sandboxUiEvidence_.mechanicalHandles.size();
    for (std::size_t index = 4U;
         index < sandboxUiEvidence_.mechanicalHandles.size(); ++index) {
        if (sandboxUiEvidence_.mechanicalHandles[index].visible) {
            angularControlIndex = index;
            break;
        }
    }
    if (angularControlIndex
        == sandboxUiEvidence_.mechanicalHandles.size()) {
        throw std::runtime_error(
            "mounted plate exposes no usable tip-tilt control");
    }
    const auto angularBefore
        = *selectedAfterMechanical->mechanicalAssembly;
    const auto opticalBeforeAngular = selectedAfterMechanical->transform;
    const glm::vec2 angularHandle
        = sandboxUiEvidence_.mechanicalHandles[angularControlIndex].screenPos;
    dragPoints(
        angularHandle,
        angularHandle + glm::vec2(30.0F, -15.0F),
        "selected plate tip-tilt control");
    selectedAfterMechanical = benchProject_.scene.find(gizmoComponentId);
    const auto angularValue = [](const bench::MechanicalAssemblyState& state,
                                 std::size_t index) {
        return index == 4U
            ? state.mountYawRadians : state.mountPitchRadians;
    };
    if (selectedAfterMechanical == nullptr
        || !selectedAfterMechanical->mechanicalAssembly.has_value()
        || angularValue(
               *selectedAfterMechanical->mechanicalAssembly,
               angularControlIndex)
            == angularValue(angularBefore, angularControlIndex)
        || selectedAfterMechanical->transform.localZAxisInWorld
            == opticalBeforeAngular.localZAxisInWorld) {
        throw std::runtime_error(
            "direct tip-tilt drag did not update mount angle and optical axis");
    }

    const auto* alignmentTarget = benchProject_.scene.find(
        sandboxAlignmentTargetComponentId_);
    if (alignmentTarget == nullptr
        || alignmentTarget->id == gizmoComponentId) {
        throw std::runtime_error(
            "Bench alignment bar did not expose a distinct target");
    }
    const math::Vec3d alignedPosition
        = selectedAfterMechanical->transform.translationMetres;
    const math::Vec3d expectedAim = math::normalized(
        alignmentTarget->transform.translationMetres - alignedPosition);
    click(sandboxUiEvidence_.aimAtTarget, "Aim +Z action");
    const auto* selectedAfterAlignment
        = benchProject_.scene.find(gizmoComponentId);
    if (selectedAfterAlignment == nullptr
        || math::length(
               selectedAfterAlignment->transform.translationMetres
               - alignedPosition) > 1e-10
        || math::dot(
            selectedAfterAlignment->transform.localZAxisInWorld,
            expectedAim) < 1.0 - 1e-12) {
        throw std::runtime_error(
            "Aim +Z UI click did not align the ordinary component transform");
    }

    const glm::vec2 viewportKeyPoint
        = sandboxUiEvidence_.viewport.centre();
    drawInputFrame(viewportKeyPoint, 0, ImGuiKey_E, 1);
    drawInputFrame(viewportKeyPoint, 0, ImGuiKey_E, 0);
    if (sandboxGizmoMode_ != SandboxGizmoMode::Rotate) {
        throw std::runtime_error(
            "E key input did not activate local-axis rotation handles");
    }
    std::size_t rotationAxis = sandboxUiEvidence_.gizmoEndpoints.size();
    for (std::size_t axis = 0;
         axis < sandboxUiEvidence_.gizmoEndpoints.size(); ++axis) {
        if (sandboxUiEvidence_.gizmoEndpoints[axis].visible
            && !sandboxUiEvidence_.gizmoProjections[axis].isDegenerate) {
            rotationAxis = axis;
            break;
        }
    }
    if (rotationAxis == sandboxUiEvidence_.gizmoEndpoints.size()) {
        throw std::runtime_error(
            "selected plate exposes no usable local rotation handle");
    }
    const auto transformBeforeRotation
        = selectedAfterAlignment->transform;
    const glm::vec2 rotationHandle
        = sandboxUiEvidence_.gizmoEndpoints[rotationAxis].screenPos;
    dragPoints(
        rotationHandle,
        rotationHandle + glm::vec2(32.0F, -16.0F),
        "selected plate local rotation handle");
    const auto* selectedAfterRotation
        = benchProject_.scene.find(gizmoComponentId);
    if (selectedAfterRotation == nullptr
        || math::length(
               selectedAfterRotation->transform.translationMetres
               - transformBeforeRotation.translationMetres) > 1e-10
        || (selectedAfterRotation->transform.localXAxisInWorld
                == transformBeforeRotation.localXAxisInWorld
            && selectedAfterRotation->transform.localYAxisInWorld
                == transformBeforeRotation.localYAxisInWorld
            && selectedAfterRotation->transform.localZAxisInWorld
                == transformBeforeRotation.localZAxisInWorld)) {
        throw std::runtime_error(
            "local rotation-handle drag did not rotate in place");
    }
    math::validateRigidTransform(selectedAfterRotation->transform);

    sandboxBeamSnapDistanceMillimetres_ = 5000.0F;
    const std::uint64_t revisionBeforeBeamSnap
        = benchProject_.scene.revision();
    click(sandboxUiEvidence_.snapToBeam, "Snap to beam action");
    if (benchProject_.scene.revision() <= revisionBeforeBeamSnap
        || !errorMessage_.empty()
        || statusMessage_.find("Snapped component to beam branch #") != 0U) {
        throw std::runtime_error(
            "Snap to beam UI click did not apply a traced branch transform");
    }
    const glm::vec2 currentViewportKeyPoint
        = sandboxUiEvidence_.viewport.centre();
    drawInputFrame(currentViewportKeyPoint, 0, ImGuiKey_W, 1);
    drawInputFrame(currentViewportKeyPoint, 0, ImGuiKey_W, 0);
    if (sandboxGizmoMode_ != SandboxGizmoMode::Translate) {
        throw std::runtime_error(
            "W key input did not restore world-axis translation handles");
    }

    const auto prepareEmptyTopBench = [&] {
        click(sandboxUiEvidence_.emptyBench, "Empty Bench action");
        camera_.setPresetView(render::CameraPresetView::TopXZ);
        camera_.setTarget({0.0F, 0.0F, 0.0F});
        camera_.setDistance(0.55F);
        drawInputFrame({-1000.0F, -1000.0F}, 0);
        if (!benchProject_.scene.components().empty()
            || sandboxExperimentMode_ != SandboxExperimentMode::Auto) {
            throw std::runtime_error(
                "Empty Bench did not reset scene and experiment auto-selection");
        }
    };
    const auto requireSelectedKind = [&](const std::string& id,
                                         bench::BenchComponentKind kind,
                                         std::string_view context) {
        const auto* component = benchProject_.scene.find(id);
        if (component == nullptr || component->kind != kind) {
            throw std::runtime_error(
                std::string(context) + " created the wrong component kind");
        }
    };

    prepareEmptyTopBench();
    const std::string transmissionPlate = dropAtWorld(
        sandboxUiEvidence_.plateShelf,
        {0.0, 0.0, 0.0},
        "Transmission plate shelf item");
    requireSelectedKind(
        transmissionPlate,
        bench::BenchComponentKind::HolographicPlate,
        "Transmission plate drop");
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.screenShelf,
        {0.0, 0.0, 0.03},
        "Transmission screen shelf item"));
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.laserShelf,
        {0.004, 0.0, -0.20},
        "Transmission reference shelf item"));
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.objectSourceShelf,
        {-0.002, 0.0, -0.20},
        "Transmission object shelf item"));
    selectWorldComponent({0.0, 0.0, 0.0});
    if (selectedBenchComponentId_ != transmissionPlate) {
        throw std::runtime_error(
            "could not select the placed transmission plate in the viewport");
    }
    click(sandboxUiEvidence_.record, "Record action");
    click(sandboxUiEvidence_.reconstruct, "Reconstruct action");
    if (!sandboxPlateRecording_ || !sandboxPlateReplay_
        || sandboxPlateReplay_->isStaleFor(benchProject_.scene)) {
        throw std::runtime_error(
            "empty-Bench transmission assembly did not reconstruct (status="
            + statusMessage_ + ", error=" + errorMessage_ + ")");
    }

    prepareEmptyTopBench();
    const std::string reflectionPlate = dropAtWorld(
        sandboxUiEvidence_.plateShelf,
        {0.0, 0.0, 0.0},
        "Reflection plate shelf item");
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.probeShelf,
        {0.0, 0.0, -0.03},
        "Reflection probe shelf item"));
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.laserShelf,
        {0.003, 0.0, -0.15},
        "Reflection reference shelf item"));
    const std::string reflectionObject = dropAtWorld(
        sandboxUiEvidence_.objectSourceShelf,
        {0.0, 0.0, 0.15},
        "Reflection object shelf item");
    requireSelectedKind(
        reflectionObject,
        bench::BenchComponentKind::ObjectWavefrontSource,
        "Reflection object drop");
    click(sandboxUiEvidence_.aimAtTarget, "Reflection Aim +Z action");
    selectWorldComponent({0.0, 0.0, 0.0});
    if (selectedBenchComponentId_ != reflectionPlate) {
        throw std::runtime_error(
            "could not select the placed reflection plate in the viewport");
    }
    click(sandboxUiEvidence_.record, "Record action");
    click(sandboxUiEvidence_.reconstruct, "Reconstruct action");
    if (!sandboxVolumeRecording_ || !sandboxVolumeObservationReplay_
        || sandboxVolumeObservationReplay_->isStaleFor(
            benchProject_.scene)) {
        throw std::runtime_error(
            "empty-Bench reflection assembly did not reconstruct (status="
            + statusMessage_ + ", error=" + errorMessage_ + ")");
    }

    prepareEmptyTopBench();
    const std::string rgbPlate = dropAtWorld(
        sandboxUiEvidence_.plateShelf,
        {0.0, 0.0, 0.0},
        "RGB plate shelf item");
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.screenShelf,
        {0.0, 0.0, 0.03},
        "RGB screen shelf item"));
    const std::string rgbLaser = dropAtWorld(
        sandboxUiEvidence_.laserShelf,
        {0.003, 0.0, -0.20},
        "RGB reference shelf item");
    click(sandboxUiEvidence_.sourceRgb, "RGB laser spectrum action");
    const auto* rgbLaserComponent = benchProject_.scene.find(rgbLaser);
    if (rgbLaserComponent == nullptr
        || std::get<bench::LaserSourceParameters>(
            rgbLaserComponent->parameters).channels.size() != 3U) {
        throw std::runtime_error(
            "RGB laser spectrum action did not create three channels");
    }
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.objectSourceShelf,
        {-0.006, 0.0, -0.20},
        "Red object shelf item"));
    click(sandboxUiEvidence_.sourceRed, "Red object spectrum action");
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.objectSourceShelf,
        {0.0, 0.0, -0.20},
        "Green object shelf item"));
    click(sandboxUiEvidence_.sourceGreen, "Green object spectrum action");
    static_cast<void>(dropAtWorld(
        sandboxUiEvidence_.objectSourceShelf,
        {0.006, 0.0, -0.20},
        "Blue object shelf item"));
    click(sandboxUiEvidence_.sourceBlue, "Blue object spectrum action");
    selectWorldComponent({0.0, 0.0, 0.0});
    if (selectedBenchComponentId_ != rgbPlate) {
        throw std::runtime_error(
            "could not select the placed RGB plate in the viewport");
    }
    click(sandboxUiEvidence_.record, "Record action");
    click(sandboxUiEvidence_.reconstruct, "Reconstruct action");
    if (!sandboxRgbRecording_ || !sandboxRgbReplay_
        || sandboxRgbReplay_->isStaleFor(benchProject_.scene)) {
        throw std::runtime_error(
            "empty-Bench RGB assembly did not reconstruct (status="
            + statusMessage_ + ", error=" + errorMessage_ + ")");
    }

    const std::uint64_t reconstructionRevision
        = benchProject_.scene.revision();
    drag(
        sandboxUiEvidence_.laserShelf,
        tableDropAt(0.5F),
        "Laser shelf item");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    if (benchProject_.scene.revision() <= reconstructionRevision
        || !sandboxRgbReplay_->isStaleFor(benchProject_.scene)
        || sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "post-reconstruction shelf edit did not stale and hide RGB evidence");
    }

    camera_.setPresetView(render::CameraPresetView::Perspective);
    camera_.setTarget({0.0F, 0.0F, 0.2F});
    camera_.setDistance(0.8F);
    click(
        sandboxUiEvidence_.doubleSlitPreset,
        "Double Slit Bench action");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    const auto* waveObservation = activeSandboxWaveObservation();
    if (benchProject_.projectId != "preset-double-slit"
        || !waveObservation
        || waveObservation->isStaleFor(benchProject_.scene)
        || waveObservation->interactivePreview
        || waveObservation->contributions.size() != 1U
        || waveObservation->contributions.front()
                .pathSampling.workingSampleWidth != 1024U
        || waveObservation->contributions.front()
                .pathSampling.workingSampleHeight != 1024U
        || waveObservation->contributions.front()
                .pathSampling.propagatedSegmentCount != 2U
        || !sandboxWaveTexture_
        || !sandboxWaveTexture_->isValid()
        || !sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "Double Slit action did not submit a current full-resolution field on the placed screen (status="
            + statusMessage_ + ", wave="
            + sandboxWaveObservationDiagnostic_ + ", result="
            + (waveObservation ? "yes" : "no")
            + ", preview="
            + (waveObservation
                    && waveObservation->interactivePreview
                ? "yes" : "no")
            + ", texture="
            + (sandboxWaveTexture_ && sandboxWaveTexture_->isValid()
                ? "yes" : "no")
            + ", overlay="
            + sandboxReconstructionOverlayDiagnostic_ + ")");
    }
    auto rgbWaveScene = benchProject_.scene;
    const auto* waveLaser = rgbWaveScene.find("wave-laser-green");
    if (waveLaser == nullptr) {
        throw std::runtime_error(
            "Double Slit Bench is missing its spectral source");
    }
    auto rgbWaveLaser = applySourceSpectrumPreset(
        *waveLaser, BenchSourceSpectrumPreset::Rgb);
    rgbWaveScene.replace(rgbWaveLaser.id, rgbWaveLaser);
    if (!applyBenchScene(
            std::move(rgbWaveScene),
            "Smoke applied RGB wave channels")) {
        throw std::runtime_error(
            "RGB wave-channel scene edit was rejected");
    }
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    if (sandboxWaveObservations_.size() != 3U) {
        throw std::runtime_error(
            "placed wave observation did not expose three independent RGB channels");
    }
    const std::uint64_t channelSwitchRevision
        = benchProject_.scene.revision();
    double previousChannelWavelength = 0.0;
    for (std::size_t index = 0;
         index < sandboxWaveObservations_.size(); ++index) {
        sandboxWaveChannelIndex_ = static_cast<int>(index);
        sandboxWaveTextureDirty_ = true;
        drawInputFrame({-1000.0F, -1000.0F}, 0);
        waveObservation = activeSandboxWaveObservation();
        if (waveObservation == nullptr
            || waveObservation->fieldAtObservation
                    .vacuumWavelengthMetres()
                <= previousChannelWavelength
            || sandboxWaveTextureDirty_
            || !sandboxWaveTexture_
            || !sandboxWaveTexture_->isValid()
            || benchProject_.scene.revision() != channelSwitchRevision) {
            throw std::runtime_error(
                "RGB placed-field channel switching changed physics or failed to render");
        }
        previousChannelWavelength = waveObservation->fieldAtObservation
            .vacuumWavelengthMetres();
    }
    auto greenWaveScene = benchProject_.scene;
    waveLaser = greenWaveScene.find("wave-laser-green");
    if (waveLaser == nullptr) {
        throw std::runtime_error(
            "RGB channel test lost its wave source");
    }
    auto greenWaveLaser = applySourceSpectrumPreset(
        *waveLaser, BenchSourceSpectrumPreset::Green);
    greenWaveScene.replace(greenWaveLaser.id, greenWaveLaser);
    if (!applyBenchScene(
            std::move(greenWaveScene),
            "Smoke restored one wave channel")) {
        throw std::runtime_error(
            "single-channel wave scene restore was rejected");
    }
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    waveObservation = activeSandboxWaveObservation();
    if (sandboxWaveObservations_.size() != 1U
        || waveObservation == nullptr) {
        throw std::runtime_error(
            "placed wave observation did not return to one physical channel");
    }
    const std::uint64_t waveRevision = benchProject_.scene.revision();
    std::size_t waveDragAxis = 2U;
    if (!sandboxUiEvidence_.gizmoEndpoints[waveDragAxis].visible
        || sandboxUiEvidence_.gizmoProjections[waveDragAxis].isDegenerate) {
        waveDragAxis = sandboxUiEvidence_.gizmoEndpoints.size();
    }
    for (std::size_t axis = 0;
         waveDragAxis == sandboxUiEvidence_.gizmoEndpoints.size()
             && axis < sandboxUiEvidence_.gizmoEndpoints.size(); ++axis) {
        if (sandboxUiEvidence_.gizmoEndpoints[axis].visible
            && !sandboxUiEvidence_.gizmoProjections[axis].isDegenerate) {
            waveDragAxis = axis;
            break;
        }
    }
    if (waveDragAxis == sandboxUiEvidence_.gizmoEndpoints.size()) {
        throw std::runtime_error(
            "Double Slit screen exposes no draggable 3D axis");
    }
    const glm::vec2 waveDragOrigin
        = sandboxUiEvidence_.gizmoEndpoints[waveDragAxis].screenPos;
    const glm::vec2 waveDragDestination = waveDragOrigin
        + sandboxUiEvidence_.gizmoProjections[waveDragAxis].screenDir * 28.0F;
    dragPoints(
        waveDragOrigin,
        waveDragDestination,
        "Double Slit screen axis");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    waveObservation = activeSandboxWaveObservation();
    if (benchProject_.scene.revision() <= waveRevision
        || !waveObservation
        || waveObservation->isStaleFor(benchProject_.scene)
        || waveObservation->interactivePreview
        || waveObservation->fieldAtObservation.width() != 512U
        || !sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "moved Double Slit screen did not settle to a current 512-sample field (revision="
            + std::to_string(benchProject_.scene.revision())
            + ", initial=" + std::to_string(waveRevision)
            + ", result=" + (waveObservation ? "yes" : "no")
            + ", preview="
            + (waveObservation
                    && waveObservation->interactivePreview
                ? "yes" : "no")
            + ", samples="
            + (waveObservation
                ? std::to_string(
                    waveObservation->fieldAtObservation.width())
                : "0")
            + ", wave=" + sandboxWaveObservationDiagnostic_
            + ", overlay=" + sandboxReconstructionOverlayDiagnostic_ + ")");
    }

    const std::string virtualProbeId = dropAtWorld(
        sandboxUiEvidence_.probeShelf,
        {0.001, 0.0, 0.30},
        "Virtual Field Probe shelf item");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    const auto* virtualProbe = benchProject_.scene.find(virtualProbeId);
    waveObservation = activeSandboxWaveObservation();
    if (virtualProbe == nullptr
        || virtualProbe->kind != bench::BenchComponentKind::FieldProbe
        || !waveObservation
        || waveObservation->isStaleFor(benchProject_.scene)
        || waveObservation->observationComponentId != virtualProbeId
        || waveObservation->fieldAtObservation.width() != 256U
        || !sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "virtual Field Probe did not display a current non-destructive light-field plane (wave="
            + sandboxWaveObservationDiagnostic_ + ", overlay="
            + sandboxReconstructionOverlayDiagnostic_ + ")");
    }
    const auto probeSample = measureBenchWaveSample(
        *waveObservation,
        waveObservation->fieldAtObservation.width() / 2U,
        waveObservation->fieldAtObservation.height() / 2U,
        waveObservation->peakIntensityWattsPerSquareMetre * 1e-6,
        -60.0);
    const auto probeSection = measureBenchWaveCrossSection(
        *waveObservation,
        BenchFieldCrossSectionAxis::HorizontalX,
        waveObservation->fieldAtObservation.height() / 2U);
    if (waveObservation->peakIntensityWattsPerSquareMetre <= 0.0
        || waveObservation->integratedPowerWatts <= 0.0
        || !std::isfinite(probeSample.intensityWattsPerSquareMetre)
        || probeSection.intensitiesWattsPerSquareMetre.size()
            != waveObservation->fieldAtObservation.width()) {
        throw std::runtime_error(
            "virtual Field Probe did not expose finite cursor and cross-section measurements");
    }
    sandboxWaveViewModeIndex_ = 1;
    sandboxWaveTextureDirty_ = true;
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    if (sandboxWaveTextureDirty_ || !sandboxWaveTexture_
        || !sandboxWaveTexture_->isValid()) {
        throw std::runtime_error(
            "virtual Field Probe did not render the dB observable");
    }
    sandboxWaveViewModeIndex_ = 2;
    sandboxWaveTextureDirty_ = true;
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    if (sandboxWaveTextureDirty_ || !sandboxWaveTexture_
        || !sandboxWaveTexture_->isValid()) {
        throw std::runtime_error(
            "virtual Field Probe did not render the wrapped-phase observable");
    }
    sandboxWaveViewModeIndex_ = 0;
    sandboxWaveTextureDirty_ = true;

    click(
        sandboxUiEvidence_.rgbDenisyukPreset,
        "RGB Denisyuk Bench action");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    click(sandboxUiEvidence_.record, "RGB Denisyuk Record action");
    click(
        sandboxUiEvidence_.reconstruct,
        "RGB Denisyuk Reconstruct action");
    if (!sandboxRgbVolumeRecording_
        || !sandboxRgbVolumeReplay_
        || sandboxRgbVolumeReplay_->isStaleFor(benchProject_.scene)
        || sandboxRgbVolumeReplay_->observationComponentId != "plate-h1"
        || !sandboxRgbReplayTexture_
        || !sandboxRgbReplayTexture_->isValid()
        || !sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "RGB Denisyuk action did not reconstruct three volume channels directly on the plate (status="
            + statusMessage_ + ", error=" + errorMessage_ + ")");
    }

    click(sandboxUiEvidence_.chimeraPreset, "CHIMERA Bench action");
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    if (benchProject_.projectId != "chimera-canonical-chimera"
        || benchProject_.scene.components().size() != 24U
        || selectedBenchComponentId_ != "chimera-plate") {
        throw std::runtime_error(
            "CHIMERA action did not build the ordinary editable 24-component Bench");
    }
    click(
        sandboxUiEvidence_.chimeraPrepare,
        "Generate CHIMERA dataset and exposure plan action");
    if (!chimeraWorkflow_
        || !chimera::isChimeraBenchWorkflowCurrent(
            *chimeraWorkflow_, benchProject_)
        || chimeraWorkflow_->dataset.sourceViews.size() != 15U
        || chimeraWorkflow_->plan.events.size() != 624U) {
        throw std::runtime_error(
            "CHIMERA preparation action did not bind deterministic artifacts to the current Bench");
    }
    click(
        sandboxUiEvidence_.chimeraExpose,
        "Expose selected CHIMERA RGB hogel action");
    if (chimeraWorkflow_->exposures.size() != 1U
        || chimeraWorkflow_->exposures.front().channels.size() != 3U) {
        throw std::runtime_error(
            "CHIMERA exposure action did not produce three RGB M8 recordings");
    }
    camera_.setPresetView(render::CameraPresetView::FrontXY);
    camera_.setTarget({0.0F, 0.0F, 0.0F});
    camera_.setDistance(0.25F);
    drawInputFrame({-1000.0F, -1000.0F}, 0);
    click(
        sandboxUiEvidence_.chimeraReconstruct,
        "Reconstruct CHIMERA view to placed Probe action");
    if (!chimeraWorkflow_->reconstruction
        || !chimeraWorkflow_->cameraImage
        || !chimeraWorkflow_->cameraImage->usedPlacedSequentialLens
        || chimeraWorkflow_->cameraImage->lensComponentId
            != "chimera-camera-lens"
        || chimeraWorkflow_->cameraImage->lensPrescriptionId
            != "default_n_bk7_biconvex"
        || chimeraWorkflow_->cameraImage->sourceSceneRevision
            != benchProject_.scene.revision()
        || chimeraWorkflow_->cameraImage->metrics
                .prescriptionTraceCompletedCount
            != 3U
        || chimeraWorkflow_->cameraImage->metrics.pupilRayTraceCount
            != 3U * chimera::kPlacedCameraPupilRayCount
        || chimeraWorkflow_->cameraImage->metrics.pupilRaySensorHitCount <= 3U
        || chimeraWorkflow_->cameraImage->metrics
                .maximumGeometricRmsRadiusMetres
            <= 0.0
        || chimeraWorkflow_->cameraImage->usedPlacedDetectorCalibration
        || !chimeraWorkflow_->cameraImage
                ->detectorResponseContentSha256.empty()
        || chimeraWorkflow_->cameraImage
                ->detectorResponseTemperatureKelvin
            != 293.15
        || chimeraWorkflow_->observationComponentId
            != "chimera-reconstruction-probe"
        || !chimeraCameraTexture_
        || !chimeraCameraTexture_->isValid()
        || !sandboxReconstructionOverlaySubmitted_) {
        throw std::runtime_error(
            "CHIMERA reconstruction action did not submit prescription-routed camera evidence on the placed Probe (status="
            + statusMessage_ + ", error=" + errorMessage_ + ", overlay="
            + sandboxReconstructionOverlayDiagnostic_ + ")");
    }

    glFinish();
    if (glGetError() != GL_NO_ERROR) {
        throw std::runtime_error(
            "Bench interaction smoke produced an OpenGL error");
    }
}

int Application::run(const RunOptions& options) {
    if (!initialize(options)) {
        return 1;
    }

    const bool isBenchmark = options.benchmarkFrames > 0;
    constexpr int kWarmupFrames = 60;
    const int measuredTarget = options.benchmarkFrames;

    std::vector<double> frameDurationsMs;
    if (isBenchmark) {
        frameDurationsMs.reserve(static_cast<std::size_t>(measuredTarget));
    }

    bool running = true;
    int totalFrames = 0;
    int lastWindowPixelWidth = 0;
    int lastWindowPixelHeight = 0;

    while (running) {
        const auto frameStart = std::chrono::steady_clock::now();

        SDL_Event event;
        while (SDL_PollEvent(&event)) {
            ImGui_ImplSDL3_ProcessEvent(&event);
            if (event.type == SDL_EVENT_QUIT) {
                running = false;
            }
            if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == SDL_GetWindowID(window_)) {
                running = false;
            }
        }

        ImGui_ImplOpenGL3_NewFrame();
        ImGui_ImplSDL3_NewFrame();
        ImGui::NewFrame();

        drawWorkspace();

        if (glSmokeMode_) {
            ImDrawList* foreground = ImGui::GetForegroundDrawList();
            const int vertexCountBefore = foreground->VtxBuffer.Size;
            foreground->AddText(
                ImVec2(8.0F, 8.0F), IM_COL32_WHITE,
                "中文光学渲染检查");
            localizedSmokeTextSubmitted_
                = localizedSmokeTextSubmitted_
                || foreground->VtxBuffer.Size > vertexCountBefore;
        }

        ImGui::Render();

        int width = 0;
        int height = 0;
        SDL_GetWindowSizeInPixels(window_, &width, &height);
        if (width > 0 && height > 0) {
            lastWindowPixelWidth = width;
            lastWindowPixelHeight = height;
            glViewport(0, 0, width, height);
            glClearColor(0.035F, 0.045F, 0.060F, 1.0F);
            glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());
            SDL_GL_SwapWindow(window_);
        }

        if (isBenchmark) {
            glFinish();
        }

        const auto frameEnd = std::chrono::steady_clock::now();
        const std::chrono::duration<double, std::milli> frameDuration = frameEnd - frameStart;

        ++totalFrames;

        if (isBenchmark) {
            if (totalFrames > kWarmupFrames) {
                frameDurationsMs.push_back(frameDuration.count());
                if (static_cast<int>(frameDurationsMs.size()) >= measuredTarget) {
                    running = false;
                }
            }
        } else if (options.smokeFrameLimit > 0) {
            if (totalFrames >= options.smokeFrameLimit) {
                running = false;
            }
        }
    }

    bool benchmarkTargetMissed = false;
    if (isBenchmark && !frameDurationsMs.empty()) {
        const std::size_t measuredCount = frameDurationsMs.size();
        const double totalTimeMs = std::accumulate(frameDurationsMs.begin(), frameDurationsMs.end(), 0.0);
        const double avgFps = totalTimeMs > 0.0 ? (static_cast<double>(measuredCount) * 1000.0 / totalTimeMs) : 0.0;

        std::vector<double> sortedDurations = frameDurationsMs;
        std::sort(sortedDurations.begin(), sortedDurations.end());

        const std::size_t p50Index = static_cast<std::size_t>(std::round(0.50 * static_cast<double>(measuredCount - 1)));
        const std::size_t p95Index = static_cast<std::size_t>(std::round(0.95 * static_cast<double>(measuredCount - 1)));

        const double p50_ms = sortedDurations[p50Index];
        const double p95_ms = sortedDurations[p95Index];
        const double max_ms = sortedDurations.back();
        constexpr double kChimeraRendererP95TargetMilliseconds = 33.333;
        const double targetP95 = options.chimeraBenchmark
            ? kChimeraRendererP95TargetMilliseconds : 0.0;
        const bool targetMet = !options.chimeraBenchmark
            || p95_ms < targetP95;
        benchmarkTargetMissed = !targetMet;

        if (window_ != nullptr && (lastWindowPixelWidth <= 0 || lastWindowPixelHeight <= 0)) {
            SDL_GetWindowSizeInPixels(window_, &lastWindowPixelWidth, &lastWindowPixelHeight);
        }

        const int viewportWidth = (lastViewportWidth_ > 0) ? lastViewportWidth_ : (isBenchmark ? 1920 : lastWindowPixelWidth);
        const int viewportHeight = (lastViewportHeight_ > 0) ? lastViewportHeight_ : (isBenchmark ? 1080 : lastWindowPixelHeight);

        SDL_Log(
            "[Benchmark] benchmark=%s avg_fps=%.2f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f target_p95_ms=%.3f target_met=%s viewport_width=%d viewport_height=%d window_width=%d window_height=%d ray_count=%zu displayed_segments=%zu dynamic_components=%zu warmup_frames=%d measured_frames=%zu vsync=%d gpu_sync=%s",
            options.chimeraBenchmark
                ? "chimera/editable_24_component_bench_renderer"
                : "application/default_bench_renderer",
            avgFps,
            p50_ms,
            p95_ms,
            max_ms,
            targetP95,
            targetMet ? "true" : "false",
            viewportWidth,
            viewportHeight,
            lastWindowPixelWidth,
            lastWindowPixelHeight,
            tracerOptions_.rayCount,
            raySegments_.size(),
            benchProject_.scene.components().size(),
            kWarmupFrames,
            measuredCount,
            vsyncInterval_,
            isBenchmark ? "true" : "false");
    }

    glFinish();
    bool rawGlError = false;
    for (GLenum error = glGetError(); error != GL_NO_ERROR; error = glGetError()) {
        rawGlError = true;
        SDL_Log("OpenGL smoke check observed error 0x%04X", static_cast<unsigned int>(error));
    }
    const bool debugCallbackClean = render::gl::errorCount() == 0;
    if (glSmokeMode_ && !detectorTexture_->isValid()) {
        SDL_Log("OpenGL smoke check failed: detector texture was never uploaded");
        rawGlError = true;
    }
    if (glSmokeMode_) {
        try {
            if (!uiFont_) {
                throw std::runtime_error("packaged UI font state is missing");
            }
            uiFont_->validateBakedCoverage();
            if (!localizedSmokeTextSubmitted_) {
                throw std::runtime_error(
                    "localized smoke text produced no draw geometry");
            }
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: packaged UI font: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            const auto& requiredKinds = optics::scene::requiredBenchComponentKinds();
            const bool hasEveryKind = std::all_of(
                requiredKinds.begin(), requiredKinds.end(), [this](const auto kind) {
                    return std::any_of(
                        benchProject_.scene.components().begin(),
                        benchProject_.scene.components().end(),
                        [kind](const auto& component) { return component.kind == kind; });
                });
            const std::string serialized = serializeBenchProject(benchProject_);
            const auto restored = parseBenchProject(serialized);
            bool redEvidence = false;
            bool greenEvidence = false;
            bool blueEvidence = false;
            if (renderer_ && renderer_->framebuffer().isValid()) {
                const int width = renderer_->framebuffer().width();
                const int height = renderer_->framebuffer().height();
                if (width <= 0 || height <= 0
                    || width > 4096 || height > 4096) {
                    throw std::runtime_error("dynamic sandbox framebuffer dimensions are invalid");
                }
                std::vector<std::uint8_t> pixels(
                    static_cast<std::size_t>(width)
                        * static_cast<std::size_t>(height) * 4);
                GLint previousReadFramebuffer = 0;
                GLint previousPackAlignment = 0;
                glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previousReadFramebuffer);
                glGetIntegerv(GL_PACK_ALIGNMENT, &previousPackAlignment);
                glBindFramebuffer(
                    GL_READ_FRAMEBUFFER, renderer_->framebuffer().handle());
                glPixelStorei(GL_PACK_ALIGNMENT, 1);
                glReadPixels(
                    0, 0, width, height, GL_RGBA, GL_UNSIGNED_BYTE, pixels.data());
                glPixelStorei(GL_PACK_ALIGNMENT, previousPackAlignment);
                glBindFramebuffer(
                    GL_READ_FRAMEBUFFER,
                    static_cast<GLuint>(previousReadFramebuffer));
                for (std::size_t offset = 0; offset < pixels.size(); offset += 4) {
                    const auto red = pixels[offset];
                    const auto green = pixels[offset + 1];
                    const auto blue = pixels[offset + 2];
                    redEvidence = redEvidence
                        || (red > 150 && green < 130 && blue < 130);
                    greenEvidence = greenEvidence
                        || (green > 150 && red < 130 && blue < 150);
                    blueEvidence = blueEvidence
                        || (blue > 150 && red < 150 && green < 170);
                }
            }
            if (viewportMode_ != ViewportMode::Sandbox
                || !hasEveryKind
                || benchTraceGraph_.sourceRevision != benchProject_.scene.revision()
                || benchTraceGraph_.segments.empty()
                || benchTraceGraph_.interactions.empty()
                || !renderer_
                || renderer_->sceneVertexCount() <= 0
                || renderer_->solidVertexCount() <= 0
                || !redEvidence || !greenEvidence || !blueEvidence
                || serializeBenchProject(restored) != serialized) {
                throw std::runtime_error(
                    "dynamic sandbox did not retain all component, trace, render, and persistence evidence");
            }
        } catch (const std::exception& ex) {
            SDL_Log("OpenGL smoke check failed: dynamic sandbox: %s", ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            const auto exerciseExperiment = [this](
                                                BenchProject project,
                                                SandboxExperimentMode mode,
                                                std::string observationId) {
                selectedBenchComponentId_ = "plate-h1";
                if (!applyDynamicBenchProject(
                        std::move(project),
                        "OpenGL experiment-action smoke",
                        false)) {
                    throw std::runtime_error(
                        "could not activate holography preset");
                }
                sandboxExperimentMode_ = mode;
                sandboxObservationComponentId_ = std::move(observationId);
                recordSelectedPlateExperiment(false);
                reconstructSelectedPlateExperiment();
            };

            exerciseExperiment(
                makeTransmissionHolographyPreset(),
                SandboxExperimentMode::ThinTransmission,
                "reconstruction-screen");
            if (!sandboxPlateRecording_
                || !sandboxPlateReplay_
                || sandboxPlateRecording_->isStaleFor(benchProject_.scene)
                || sandboxPlateReplay_->isStaleFor(benchProject_.scene)
                || !sandboxPlateTexture_
                || !sandboxPlateTexture_->isValid()
                || !sandboxReplayTexture_
                || !sandboxReplayTexture_->isValid()) {
                throw std::runtime_error(
                    "thin transmission Record/Reconstruct did not produce current placed evidence");
            }

            exerciseExperiment(
                makeReflectionHolographyPreset(),
                SandboxExperimentMode::ReflectionDenisyuk,
                "reflection-reconstruction-probe");
            if (!sandboxVolumeRecording_
                || !sandboxVolumeObservationReplay_
                || sandboxVolumeRecording_->isStaleFor(benchProject_.scene)
                || sandboxVolumeObservationReplay_->isStaleFor(
                    benchProject_.scene)
                || !sandboxVolumeReplayTexture_
                || !sandboxVolumeReplayTexture_->isValid()) {
                throw std::runtime_error(
                    "reflection/Denisyuk Record/Reconstruct did not produce current placed evidence");
            }

            exerciseExperiment(
                makeRgbHolographyPreset(),
                SandboxExperimentMode::RgbFullColour,
                "reconstruction-screen");
            if (!sandboxRgbRecording_
                || !sandboxRgbReplay_
                || sandboxRgbRecording_->isStaleFor(benchProject_.scene)
                || sandboxRgbReplay_->isStaleFor(benchProject_.scene)
                || !sandboxRgbReplayTexture_
                || !sandboxRgbReplayTexture_->isValid()) {
                throw std::runtime_error(
                    "RGB Record/Reconstruct did not produce current placed evidence");
            }

            camera_.setPresetView(render::CameraPresetView::FrontXY);
            camera_.setTarget(glm::vec3(0.0F, 0.0F, 0.03F));
            camera_.setDistance(0.3F);
            ImGui_ImplOpenGL3_NewFrame();
            ImGui_ImplSDL3_NewFrame();
            ImGui::NewFrame();
            ImGui::SetNextWindowPos(ImVec2(20.0F, 20.0F));
            ImGui::SetNextWindowSize(ImVec2(680.0F, 540.0F));
            ImGui::Begin(
                "##placed-reconstruction-smoke",
                nullptr,
                ImGuiWindowFlags_NoDecoration
                    | ImGuiWindowFlags_NoSavedSettings);
            const ImVec2 smokeMinimum = ImGui::GetCursorScreenPos();
            const ImVec2 smokeSize(640.0F, 480.0F);
            ImGui::InvisibleButton(
                "##placed-reconstruction-smoke-canvas", smokeSize);
            const auto* smokeObservation = benchProject_.scene.find(
                sandboxRgbReplay_->observationComponentId);
            if (smokeObservation == nullptr) {
                throw std::runtime_error(
                    "RGB smoke observation component disappeared");
            }
            ImDrawList* smokeDrawList = ImGui::GetWindowDrawList();
            if (smokeDrawList == nullptr) {
                throw std::runtime_error(
                    "RGB smoke overlay draw list is unavailable");
            }
            const glm::vec2 smokeRectMinimum(
                smokeMinimum.x, smokeMinimum.y);
            const glm::vec2 smokeRectSize(smokeSize.x, smokeSize.y);
            const glm::mat4 smokeViewProjection
                = camera_.projectionMatrix(smokeSize.x / smokeSize.y)
                * camera_.viewMatrix();
            smokeDrawList->PushClipRect(
                smokeMinimum,
                ImVec2(
                    smokeMinimum.x + smokeSize.x,
                    smokeMinimum.y + smokeSize.y),
                true);
            sandboxReconstructionOverlaySubmitted_
                = drawPlacedObservationTexture(
                    *smokeDrawList,
                    sandboxRgbReplayTexture_->handle(),
                    *smokeObservation,
                    smokeViewProjection,
                    smokeRectMinimum,
                    smokeRectSize,
                    sandboxRgbReplay_->observationComponentId,
                    sandboxReconstructionOverlayDiagnostic_);
            smokeDrawList->PopClipRect();
            ImGui::End();
            ImGui::Render();
            const ImDrawData* drawData = ImGui::GetDrawData();
            const ImTextureID expectedTexture
                = toImTextureID(sandboxRgbReplayTexture_->handle());
            bool replayTextureSubmitted = false;
            if (drawData != nullptr) {
                for (int listIndex = 0;
                     listIndex < drawData->CmdListsCount; ++listIndex) {
                    const ImDrawList* list = drawData->CmdLists[listIndex];
                    for (const auto& command : list->CmdBuffer) {
                        replayTextureSubmitted = replayTextureSubmitted
                            || (command.ElemCount >= 6U
                                && command.TexRef._TexData == nullptr
                                && command.TexRef._TexID
                                    == expectedTexture);
                    }
                }
            }
            if (!sandboxReconstructionOverlaySubmitted_
                || !replayTextureSubmitted) {
                throw std::runtime_error(
                    std::string(
                        "current reconstruction was not submitted on its placed Screen/Probe quad (overlay=")
                    + (sandboxReconstructionOverlaySubmitted_ ? "yes" : "no")
                    + ", texture="
                    + (replayTextureSubmitted ? "yes" : "no") + "; "
                    + sandboxReconstructionOverlayDiagnostic_ + ")");
            }
            ImGui_ImplOpenGL3_RenderDrawData(ImGui::GetDrawData());

            auto rgbVolumeProject = makeRgbDenisyukHolographyPreset();
            auto rgbVolumeProbe
                = optics::scene::makeDefaultBenchComponent(
                    optics::scene::BenchComponentKind::FieldProbe,
                    "rgb-volume-reconstruction-probe");
            rgbVolumeProbe.transform = {
                .translationMetres = {0.0, 0.0, -0.03},
                .localXAxisInWorld = {-1.0, 0.0, 0.0},
                .localYAxisInWorld = {0.0, 1.0, 0.0},
                .localZAxisInWorld = {0.0, 0.0, -1.0},
            };
            rgbVolumeProject.scene.add(std::move(rgbVolumeProbe));
            exerciseExperiment(
                std::move(rgbVolumeProject),
                SandboxExperimentMode::RgbReflectionDenisyuk,
                "rgb-volume-reconstruction-probe");
            if (!sandboxRgbVolumeRecording_
                || !sandboxRgbVolumeReplay_
                || sandboxRgbVolumeRecording_->isStaleFor(
                    benchProject_.scene)
                || sandboxRgbVolumeReplay_->isStaleFor(
                    benchProject_.scene)
                || sandboxRgbVolumeReplay_->observationComponentId
                    != "rgb-volume-reconstruction-probe"
                || !sandboxRgbReplayTexture_
                || !sandboxRgbReplayTexture_->isValid()) {
                throw std::runtime_error(
                    "RGB Denisyuk Record/Reconstruct did not target the placed Probe");
            }
            for (const auto& channel : sandboxRgbVolumeReplay_->channels) {
                if (field::computeIntegratedIntensity(
                        channel.reconstructedAtObservation)
                    <= 0.0) {
                    throw std::runtime_error(
                        "RGB Denisyuk placed Probe received an empty channel");
                }
            }
            sandboxRecordedExperiment_ = SandboxRecordedExperiment::None;
            sandboxObservationComponentId_.clear();
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: Bench experiment actions: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            const auto compiled = chimera::compileChimeraRecipe(
                chimera::makeCanonicalChimeraRecipe());
            const auto trace = optics::ray::traceDynamicBench(
                compiled.project.scene, benchTraceBudget_);
            const auto fields = optics::holography::collectPlateIncidentFields(
                compiled.project.scene, trace, "chimera-plate");
            const std::string bytes = serializeBenchProject(compiled.project);
            if (!compiled.feasible()
                || compiled.generatedComponents.size() != 24U
                || fields.branches.size() != 6U
                || !renderer_
                || !renderer_->updateDynamicScene(
                    compiled.project.scene, trace, "chimera-plate")
                || renderer_->sceneVertexCount() <= 0
                || renderer_->solidVertexCount() <= 0
                || serializeBenchProject(parseBenchProject(bytes)) != bytes) {
                throw std::runtime_error(
                    "canonical recipe did not produce renderable ordinary bench evidence");
            }
            if (glGetError() != GL_NO_ERROR) {
                throw std::runtime_error(
                    "canonical recipe renderer produced an OpenGL error");
            }
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: CHIMERA recipe-to-bench: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_ && (!samplingSpectrumTexture_ || !samplingSpectrumTexture_->isValid())) {
        SDL_Log("OpenGL smoke check failed: angular-spectrum texture was never uploaded");
        rawGlError = true;
    }
    if (glSmokeMode_
        && (!fourFObjectTexture_ || !fourFObjectTexture_->isValid()
            || !fourFBeforeFilterTexture_ || !fourFBeforeFilterTexture_->isValid()
            || !fourFAfterFilterTexture_ || !fourFAfterFilterTexture_->isValid()
            || !fourFImageTexture_ || !fourFImageTexture_->isValid())) {
        SDL_Log("OpenGL smoke check failed: one or more 4-f plane textures were never uploaded");
        rawGlError = true;
    }
    if (glSmokeMode_
        && (!realLensResult_ || realLensResult_->spotDiagram.samples.empty()
            || realLensResult_->tracePolylines.empty()
            || !realLensErrorMessage_.empty())) {
        SDL_Log("OpenGL smoke check failed: Real Lens Workbench did not produce drawable analysis");
        rawGlError = true;
    }
    if (glSmokeMode_
        && (!slmInterferenceResult_
            || slmInterferenceResult_->wavelengths.empty()
            || !slmInterferenceTexture_
            || !slmInterferenceTexture_->isValid()
            || !slmInterferenceErrorMessage_.empty())) {
        SDL_Log("OpenGL smoke check failed: SLM Interference Lab did not produce drawable analysis");
        rawGlError = true;
    }
    if (glSmokeMode_
        && (!holographyResult_
            || !holographyTexture_
            || !holographyTexture_->isValid()
            || !holographyErrorMessage_.empty())) {
        SDL_Log("OpenGL smoke check failed: Holography Lab did not produce drawable analysis");
        rawGlError = true;
    }
    if (glSmokeMode_) {
        try {
            reflection::ReflectionRefractionWorkbenchDocument document;
            document.name = reflectionProjectName_;
            document.provenance = reflectionProjectProvenance_;
            document.config = reflectionRefractionConfig_;
            const auto restored
                = reflection::deserializeReflectionRefractionWorkbenchJson(
                    reflection::serializeReflectionRefractionWorkbenchJson(
                        document));
            if (restored != document) {
                SDL_Log("OpenGL smoke check failed: reflection/refraction project semantic round trip changed state");
                rawGlError = true;
            }
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: reflection/refraction project round trip: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            slmproject::SlmInterferenceProjectDocument document;
            document.name = slmProjectName_;
            document.provenance = slmProjectProvenance_;
            document.config = slmInterferenceUiState_.appliedConfig();
            document.calibrationProvenance
                = slmInterferenceUiState_.appliedCalibrationSource();
            const auto restored = slmproject::deserializeSlmInterferenceProjectJson(
                slmproject::serializeSlmInterferenceProjectJson(document));
            if (!slmui::sameExperimentPhysicsConfig(document.config, restored.config)
                || document.calibrationProvenance != restored.calibrationProvenance
                || document.provenance != restored.provenance
                || document.name != restored.name) {
                SDL_Log("OpenGL smoke check failed: SLM project semantic round trip changed state");
                rawGlError = true;
            }
        } catch (const std::exception& ex) {
            SDL_Log("OpenGL smoke check failed: SLM project round trip: %s", ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            holographyproject::HolographyProjectDocument document;
            document.name = holographyProjectName_;
            document.provenance = holographyProjectProvenance_;
            document.config = holographyUiState_.appliedConfig();
            const auto restored
                = holographyproject::deserializeHolographyProjectJson(
                    holographyproject::serializeHolographyProjectJson(document));
            if (!holographyui::sameHolographyLabConfig(
                    document.config, restored.config)
                || document.name != restored.name
                || document.provenance != restored.provenance) {
                SDL_Log("OpenGL smoke check failed: Holography project semantic round trip changed state");
                rawGlError = true;
            }
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: Holography project round trip: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            const auto serialized = lessons::serializeLessonProgressJson(
                learnSession_.catalog(), learnSession_.progress());
            const auto restored = lessons::deserializeLessonProgressJson(
                learnSession_.catalog(), serialized);
            const auto reserialized = lessons::serializeLessonProgressJson(
                learnSession_.catalog(), restored);
            if (serialized != reserialized) {
                SDL_Log(
                    "OpenGL smoke check failed: lesson progress round trip changed bytes");
                rawGlError = true;
            }
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: lesson progress round trip: %s",
                ex.what());
            rawGlError = true;
        }
    }
    if (glSmokeMode_) {
        try {
            runSandboxInteractionSmoke();
        } catch (const std::exception& ex) {
            SDL_Log(
                "OpenGL smoke check failed: Bench user interactions: %s",
                ex.what());
            rawGlError = true;
        }
    }

    shutdown();

    return (!rawGlError && debugCallbackClean && !benchmarkTargetMissed)
        ? 0 : 2;
}

int Application::run(int smokeFrameLimit, int initialRayCount) {
    RunOptions options;
    options.smokeFrameLimit = smokeFrameLimit;
    options.initialRayCount = initialRayCount;
    return run(options);
}

} // namespace holobench::app
