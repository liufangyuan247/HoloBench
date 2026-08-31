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
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <numeric>
#include <numbers>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldVisualization.hpp"
#include "optics/io/LensPrescriptionIO.hpp"
#include "optics/slm/SlmResponseIO.hpp"
#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/NumericalAperture.hpp"
#include "optics/scene/OpticalBenchScene.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"
#include "render/OpticalBenchRenderer.hpp"
#include "render/gl/GlDebug.hpp"
#include "render/gl/Texture2D.hpp"

namespace holobench::app {
namespace {

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
    , realLensConfig_(reallens::makeDefaultRealLensWorkbenchConfig())
    , scene_(optics::scene::createDefaultRealImageScene())
    , naResult_(optics::scene::computeObjectSideNumericalAperture(scene_)) {
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

        if (!renderer_ || !renderer_->updateScene(sceneCopy, pred, stagingRaySegments_)) {
            throw std::runtime_error("Renderer rejected scene geometry update");
        }

        scene_ = std::move(sceneCopy);
        prediction_ = pred;
        naResult_ = na;
        tracerOptions_ = candidateOptions;
        raySegments_.swap(stagingRaySegments_);
        errorMessage_.clear();
        statusMessage_ = std::move(newStatus);
        return true;
    } catch (const std::exception& ex) {
        errorMessage_ = ex.what();
        statusMessage_.clear();
        return false;
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
        optics::scene::saveScene(scene_, path);
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
        const auto loaded = optics::scene::loadScene(path);
        if (applyScene(loaded, tracerOptions_)) {
            statusMessage_ = "Loaded scene from " + path.string() + " (" + std::to_string(raySegments_.size()) + " segments)";
        }
    } catch (const std::exception& ex) {
        errorMessage_ = "Load failed: " + std::string(ex.what());
        statusMessage_.clear();
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
    ImGui::StyleColorsDark();

    imguiSdlInitialized_ = ImGui_ImplSDL3_InitForOpenGL(window_, glContext_);
    imguiGlInitialized_ = imguiSdlInitialized_ && ImGui_ImplOpenGL3_Init("#version 460 core");
    if (!imguiGlInitialized_) {
        SDL_Log("Dear ImGui backend initialization failed");
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
    refreshRealLensWorkbench();

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
    detectorResult_.reset();
    samplingDebuggerResult_.reset();
    realLensResult_.reset();
    slmInterferenceResult_.reset();
    if (imguiGlInitialized_) {
        ImGui_ImplOpenGL3_Shutdown();
        imguiGlInitialized_ = false;
    }
    if (imguiSdlInitialized_) {
        ImGui_ImplSDL3_Shutdown();
        imguiSdlInitialized_ = false;
    }
    if (imguiContextCreated_) {
        ImGui::DestroyContext();
        imguiContextCreated_ = false;
    }
    initialized_ = false;
    dockLayoutInitialized_ = false;
    isOrbiting_ = false;
    isPanning_ = false;
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

void Application::loadSlmCalibration() {
    try {
        const std::filesystem::path path(slmCalibrationPathBuffer_);
        if (path.empty()) {
            throw std::invalid_argument("SLM calibration path cannot be empty");
        }
        auto response = optics::slm::loadSlmResponseJson(path);
        const std::size_t curveCount = response.wavelengths().size();
        slmInterferenceUiState_.setCalibration(std::move(response), path.string());
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
        realLensConfig_.prescription = csv
            ? optics::io::loadLensPrescriptionCsv(path)
            : optics::io::loadLensPrescriptionJson(path);
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

void Application::drawWaveDetectorPanel() {
    ImGui::Begin(docking::DockLayoutConfig::kWaveDetectorWindowName);

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
    }
    if (detectorUiState_.isDirty()) {
        ImGui::TextColored(ImVec4(1.0F, 0.75F, 0.2F, 1.0F), "Parameters edited - press Apply to recompute");
    }
    if (ImGui::Button("Apply & Recompute")) {
        detectorUiState_.apply();
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

    if (ImGui::BeginTabBar("real_lens_tabs")) {
        if (ImGui::BeginTabItem("Prescription Editor")) {
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
    }
    if (slmInterferenceUiState_.isDirty()) {
        ImGui::TextColored(
            ImVec4(1.0F, 0.75F, 0.2F, 1.0F),
            "Draft differs from the displayed result - press Apply to recompute");
    }
    if (ImGui::Button("Apply SLM Experiment")) {
        slmInterferenceUiState_.apply();
        slmInterferenceStatusMessage_ = "SLM experiment recompute queued";
        slmInterferenceErrorMessage_.clear();
    }
    ImGui::SameLine();
    if (ImGui::Button("Reset teaching defaults")) {
        slmInterferenceUiState_.setDraftConfig(
            slmexperiment::makeDefaultSlmInterferenceExperimentConfig());
        slmInterferenceUiState_.clearCalibration();
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

void Application::drawWorkspace() {
    updateWaveDetector();
    updateSlmInterference();
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

            ImGui::DockBuilderFinish(dockspaceId);
        }
        dockLayoutInitialized_ = true;
    }

    const ImGuiIO& io = ImGui::GetIO();

    ImGui::Begin(docking::DockLayoutConfig::kOpticalBenchWindowName);
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
        const bool isHovered = ImGui::IsItemHovered();
        const bool noActiveWidget = !ImGui::IsAnyItemActive();
        const glm::vec2 rectMin(imagePosMin.x, imagePosMin.y);
        const glm::vec2 rectSize(imageSize.x, imageSize.y);

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
            } else if (!ImGui::IsMouseDown(ImGuiMouseButton_Left)) {
                // Release drag on left mouse up
                isGizmoDragging_ = false;
                draggedTarget_ = GizmoTarget::None;
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
                        applyScene(candidate, tracerOptions_);
                    } else if (draggedTarget_ == GizmoTarget::Screen) {
                        auto candidate = scene_;
                        candidate.screen.planeZMetres += deltaZ;
                        applyScene(candidate, tracerOptions_);
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
                } else if (screenHovered) {
                    isGizmoDragging_ = true;
                    draggedTarget_ = GizmoTarget::Screen;
                    selectedTarget_ = GizmoTarget::Screen;
                    dragInitialLensZ_ = scene_.lens.planeZMetres;
                    dragInitialApertureZ_ = scene_.aperture.planeZMetres;
                    dragInitialScreenZ_ = scene_.screen.planeZMetres;
                    dragApertureWasCoplanar_ = (std::abs(scene_.aperture.planeZMetres - scene_.lens.planeZMetres) < 1e-4);
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
                camera_.orbit(io.MouseDelta.x * kOrbitSensitivity, -io.MouseDelta.y * kOrbitSensitivity);
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
    } else {
        isOrbiting_ = false;
        isPanning_ = false;
        isGizmoDragging_ = false;
        draggedTarget_ = GizmoTarget::None;
    }
    ImGui::End();

    ImGui::Begin(docking::DockLayoutConfig::kInspectorWindowName);

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
            applyScene(optics::scene::createDefaultRealImageScene(), tracerOptions_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Virtual Image")) {
            applyScene(optics::scene::createDefaultVirtualImageScene(), tracerOptions_);
        }
        ImGui::SameLine();
        if (ImGui::Button("Collimated")) {
            applyScene(optics::scene::createDefaultInfinityScene(), tracerOptions_);
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

        if (window_ != nullptr && (lastWindowPixelWidth <= 0 || lastWindowPixelHeight <= 0)) {
            SDL_GetWindowSizeInPixels(window_, &lastWindowPixelWidth, &lastWindowPixelHeight);
        }

        const int viewportWidth = (lastViewportWidth_ > 0) ? lastViewportWidth_ : (isBenchmark ? 1920 : lastWindowPixelWidth);
        const int viewportHeight = (lastViewportHeight_ > 0) ? lastViewportHeight_ : (isBenchmark ? 1080 : lastWindowPixelHeight);

        SDL_Log(
            "[Benchmark] avg_fps=%.2f p50_ms=%.3f p95_ms=%.3f max_ms=%.3f viewport_width=%d viewport_height=%d window_width=%d window_height=%d ray_count=%zu displayed_segments=%zu warmup_frames=%d measured_frames=%zu vsync=%d gpu_sync=%s",
            avgFps,
            p50_ms,
            p95_ms,
            max_ms,
            viewportWidth,
            viewportHeight,
            lastWindowPixelWidth,
            lastWindowPixelHeight,
            tracerOptions_.rayCount,
            raySegments_.size(),
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

    shutdown();

    return (!rawGlError && debugCallbackClean) ? 0 : 2;
}

int Application::run(int smokeFrameLimit, int initialRayCount) {
    RunOptions options;
    options.smokeFrameLimit = smokeFrameLimit;
    options.initialRayCount = initialRayCount;
    return run(options);
}

} // namespace holobench::app
