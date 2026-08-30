#include "render/OpticalBenchRenderer.hpp"

#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "optics/scene/NumericalAperture.hpp"

namespace holobench::render {

namespace {

constexpr std::string_view kVertexShaderSource = R"(#version 460 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec4 aColor;

uniform mat4 uViewProjection;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kFragmentShaderSource = R"(#version 460 core
in vec4 vColor;
out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

struct GlRenderStateGuard {
    GLint prevDrawFbo = 0;
    GLint prevReadFbo = 0;
    GLint prevViewport[4] = {0, 0, 0, 0};
    GLfloat prevClearColor[4] = {0.0F, 0.0F, 0.0F, 0.0F};
    GLboolean prevDepthTest = GL_FALSE;
    GLint prevDepthFunc = 0;
    GLboolean prevDepthMask = GL_TRUE;
    GLboolean prevColorMask[4] = {GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE};
    GLboolean prevBlend = GL_FALSE;
    GLint prevBlendEqRgb = 0;
    GLint prevBlendEqAlpha = 0;
    GLint prevBlendSrcRgb = 0;
    GLint prevBlendDstRgb = 0;
    GLint prevBlendSrcAlpha = 0;
    GLint prevBlendDstAlpha = 0;
    GLboolean prevCullFace = GL_FALSE;
    GLboolean prevScissorTest = GL_FALSE;
    GLboolean prevStencilTest = GL_FALSE;
    GLint prevPolygonMode[2] = {GL_FILL, GL_FILL};
    GLint prevActiveTexture = GL_TEXTURE0;
    GLint prevTexture2D = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevProgram = 0;

    GlRenderStateGuard() noexcept {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevDrawFbo);
        glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &prevReadFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);

        prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        glGetBooleanv(GL_DEPTH_WRITEMASK, &prevDepthMask);
        glGetBooleanv(GL_COLOR_WRITEMASK, prevColorMask);

        prevBlend = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_EQUATION_RGB, &prevBlendEqRgb);
        glGetIntegerv(GL_BLEND_EQUATION_ALPHA, &prevBlendEqAlpha);
        glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);

        prevCullFace = glIsEnabled(GL_CULL_FACE);
        prevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
        prevStencilTest = glIsEnabled(GL_STENCIL_TEST);
        glGetIntegerv(GL_POLYGON_MODE, prevPolygonMode);

        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTexture2D);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
    }

    ~GlRenderStateGuard() noexcept {
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        glUseProgram(static_cast<GLuint>(prevProgram));

        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTexture2D));

        glBindFramebuffer(GL_DRAW_FRAMEBUFFER, static_cast<GLuint>(prevDrawFbo));
        glBindFramebuffer(GL_READ_FRAMEBUFFER, static_cast<GLuint>(prevReadFbo));
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);

        glColorMask(prevColorMask[0], prevColorMask[1], prevColorMask[2], prevColorMask[3]);
        glDepthMask(prevDepthMask);

        if (prevDepthTest == GL_TRUE) {
            glEnable(GL_DEPTH_TEST);
        } else {
            glDisable(GL_DEPTH_TEST);
        }
        glDepthFunc(static_cast<GLenum>(prevDepthFunc));

        if (prevBlend == GL_TRUE) {
            glEnable(GL_BLEND);
        } else {
            glDisable(GL_BLEND);
        }
        glBlendEquationSeparate(
            static_cast<GLenum>(prevBlendEqRgb),
            static_cast<GLenum>(prevBlendEqAlpha));
        glBlendFuncSeparate(
            static_cast<GLenum>(prevBlendSrcRgb),
            static_cast<GLenum>(prevBlendDstRgb),
            static_cast<GLenum>(prevBlendSrcAlpha),
            static_cast<GLenum>(prevBlendDstAlpha));

        if (prevCullFace == GL_TRUE) {
            glEnable(GL_CULL_FACE);
        } else {
            glDisable(GL_CULL_FACE);
        }

        if (prevScissorTest == GL_TRUE) {
            glEnable(GL_SCISSOR_TEST);
        } else {
            glDisable(GL_SCISSOR_TEST);
        }

        if (prevStencilTest == GL_TRUE) {
            glEnable(GL_STENCIL_TEST);
        } else {
            glDisable(GL_STENCIL_TEST);
        }

        if (prevPolygonMode[0] == prevPolygonMode[1]) {
            glPolygonMode(GL_FRONT_AND_BACK, static_cast<GLenum>(prevPolygonMode[0]));
        } else {
            glPolygonMode(GL_FRONT, static_cast<GLenum>(prevPolygonMode[0]));
            glPolygonMode(GL_BACK, static_cast<GLenum>(prevPolygonMode[1]));
        }
    }

    GlRenderStateGuard(const GlRenderStateGuard&) = delete;
    GlRenderStateGuard& operator=(const GlRenderStateGuard&) = delete;
    GlRenderStateGuard(GlRenderStateGuard&&) = delete;
    GlRenderStateGuard& operator=(GlRenderStateGuard&&) = delete;
};

struct GlBufferBindingGuard {
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;

    GlBufferBindingGuard() noexcept {
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
    }

    ~GlBufferBindingGuard() noexcept {
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
    }

    GlBufferBindingGuard(const GlBufferBindingGuard&) = delete;
    GlBufferBindingGuard& operator=(const GlBufferBindingGuard&) = delete;
    GlBufferBindingGuard(GlBufferBindingGuard&&) = delete;
    GlBufferBindingGuard& operator=(GlBufferBindingGuard&&) = delete;
};

[[nodiscard]] inline bool toFiniteFloat(
    double val,
    float& out,
    double minVal = -1e4,
    double maxVal = 1e4) noexcept {
    if (!std::isfinite(val) || val < minVal || val > maxVal) {
        return false;
    }
    const auto converted = static_cast<float>(val);
    if (!std::isfinite(converted)) {
        return false;
    }
    out = converted;
    return true;
}

[[nodiscard]] inline bool toFiniteVec3(
    const holobench::math::Vec3d& vec,
    glm::vec3& out,
    double minVal = -1e4,
    double maxVal = 1e4) noexcept {
    return toFiniteFloat(vec.x, out.x, minVal, maxVal)
        && toFiniteFloat(vec.y, out.y, minVal, maxVal)
        && toFiniteFloat(vec.z, out.z, minVal, maxVal);
}

} // namespace

OpticalBenchRenderer::~OpticalBenchRenderer() {
    destroy();
}

OpticalBenchRenderer::OpticalBenchRenderer(OpticalBenchRenderer&& other) noexcept
    : shader_(std::move(other.shader_))
    , framebuffer_(std::move(other.framebuffer_))
    , gridVao_(std::exchange(other.gridVao_, 0))
    , gridVbo_(std::exchange(other.gridVbo_, 0))
    , gridVertexCount_(std::exchange(other.gridVertexCount_, 0))
    , sceneVao_(std::exchange(other.sceneVao_, 0))
    , sceneVbo_(std::exchange(other.sceneVbo_, 0))
    , sceneVertexCount_(std::exchange(other.sceneVertexCount_, 0))
    , sceneVboCapacityBytes_(std::exchange(other.sceneVboCapacityBytes_, 0))
    , cpuSceneVertices_(std::move(other.cpuSceneVertices_))
    , stagingVertices_(std::move(other.stagingVertices_))
    , sceneDirty_(std::exchange(other.sceneDirty_, false))
    , initialized_(std::exchange(other.initialized_, false)) {}

OpticalBenchRenderer& OpticalBenchRenderer::operator=(OpticalBenchRenderer&& other) noexcept {
    if (this != &other) {
        destroy();
        shader_ = std::move(other.shader_);
        framebuffer_ = std::move(other.framebuffer_);
        gridVao_ = std::exchange(other.gridVao_, 0);
        gridVbo_ = std::exchange(other.gridVbo_, 0);
        gridVertexCount_ = std::exchange(other.gridVertexCount_, 0);
        sceneVao_ = std::exchange(other.sceneVao_, 0);
        sceneVbo_ = std::exchange(other.sceneVbo_, 0);
        sceneVertexCount_ = std::exchange(other.sceneVertexCount_, 0);
        sceneVboCapacityBytes_ = std::exchange(other.sceneVboCapacityBytes_, 0);
        cpuSceneVertices_ = std::move(other.cpuSceneVertices_);
        stagingVertices_ = std::move(other.stagingVertices_);
        sceneDirty_ = std::exchange(other.sceneDirty_, false);
        initialized_ = std::exchange(other.initialized_, false);
    }
    return *this;
}

void OpticalBenchRenderer::destroy() noexcept {
    if (sceneVbo_ != 0) {
        glDeleteBuffers(1, &sceneVbo_);
        sceneVbo_ = 0;
    }
    if (sceneVao_ != 0) {
        glDeleteVertexArrays(1, &sceneVao_);
        sceneVao_ = 0;
    }
    if (gridVbo_ != 0) {
        glDeleteBuffers(1, &gridVbo_);
        gridVbo_ = 0;
    }
    if (gridVao_ != 0) {
        glDeleteVertexArrays(1, &gridVao_);
        gridVao_ = 0;
    }
    shader_.destroy();
    framebuffer_.destroy();
    gridVertexCount_ = 0;
    sceneVertexCount_ = 0;
    sceneVboCapacityBytes_ = 0;
    cpuSceneVertices_.clear();
    stagingVertices_.clear();
    sceneDirty_ = false;
    initialized_ = false;
}

bool OpticalBenchRenderer::initialize() {
    if (initialized_) {
        return true;
    }

    std::string errorMessage;
    if (!shader_.compileAndLink(kVertexShaderSource, kFragmentShaderSource, &errorMessage)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "OpticalBenchRenderer failed to compile shaders: %s",
            errorMessage.c_str());
        return false;
    }

    glGenVertexArrays(1, &gridVao_);
    glGenBuffers(1, &gridVbo_);
    if (gridVao_ == 0 || gridVbo_ == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpticalBenchRenderer failed to create grid VAO/VBO");
        destroy();
        return false;
    }

    glGenVertexArrays(1, &sceneVao_);
    glGenBuffers(1, &sceneVbo_);
    if (sceneVao_ == 0 || sceneVbo_ == 0) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpticalBenchRenderer failed to create scene VAO/VBO");
        destroy();
        return false;
    }

    // Configure scene VAO attributes
    {
        const GlBufferBindingGuard bindingGuard;
        glBindVertexArray(sceneVao_);
        glBindBuffer(GL_ARRAY_BUFFER, sceneVbo_);

        glEnableVertexAttribArray(0);
        glVertexAttribPointer(
            0,
            3,
            GL_FLOAT,
            GL_FALSE,
            sizeof(BenchVertex),
            reinterpret_cast<const void*>(offsetof(BenchVertex, position)));

        glEnableVertexAttribArray(1);
        glVertexAttribPointer(
            1,
            4,
            GL_FLOAT,
            GL_FALSE,
            sizeof(BenchVertex),
            reinterpret_cast<const void*>(offsetof(BenchVertex, color)));
    }

    if (!generateGridAndAxes()) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "OpticalBenchRenderer failed to generate grid and axes geometry");
        destroy();
        return false;
    }

    initialized_ = true;
    return true;
}

bool OpticalBenchRenderer::generateGridAndAxes() {
    std::vector<BenchVertex> vertices;
    vertices.reserve(1200);

    // Bench dimensions in meters (SI canonical length unit)
    constexpr float xHalfWidth = 0.30F; // 30 cm half-width (total width = 60 cm)
    constexpr float zMin = -0.20F;      // 20 cm behind coordinate origin
    constexpr float zMax = 1.80F;       // 1.80 m along +Z forward optical axis
    constexpr float minorStep = 0.05F;  // 5 cm minor grid spacing
    constexpr float majorStep = 0.10F;  // 10 cm major grid spacing

    const glm::vec4 minorColor(0.20F, 0.24F, 0.30F, 0.40F);
    const glm::vec4 majorColor(0.32F, 0.38F, 0.48F, 0.70F);
    const glm::vec4 railColor(0.40F, 0.60F, 0.85F, 0.90F);
    const glm::vec4 borderColor(0.45F, 0.55F, 0.68F, 0.85F);

    auto addLine = [&vertices](const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
        vertices.push_back(BenchVertex {start, color});
        vertices.push_back(BenchVertex {end, color});
    };

    // Lines parallel to Z (constant X) on Y=0 plane
    const int numXLines = static_cast<int>(std::round((2.0F * xHalfWidth) / minorStep));
    for (int i = 0; i <= numXLines; ++i) {
        const float x = -xHalfWidth + static_cast<float>(i) * minorStep;
        const bool isMajor = (std::abs(std::remainder(x, majorStep)) < 0.005F);
        const bool isCenter = (std::abs(x) < 0.005F);

        const glm::vec4 col = isCenter ? railColor : (isMajor ? majorColor : minorColor);
        addLine(glm::vec3(x, 0.0F, zMin), glm::vec3(x, 0.0F, zMax), col);
    }

    // Lines parallel to X (constant Z) on Y=0 plane
    const int numZLines = static_cast<int>(std::round((zMax - zMin) / minorStep));
    for (int i = 0; i <= numZLines; ++i) {
        const float z = zMin + static_cast<float>(i) * minorStep;
        const bool isMajor = (std::abs(std::remainder(z, majorStep)) < 0.005F);
        const bool isOrigin = (std::abs(z) < 0.005F);

        const glm::vec4 col = isOrigin ? majorColor : (isMajor ? majorColor : minorColor);
        addLine(glm::vec3(-xHalfWidth, 0.0F, z), glm::vec3(xHalfWidth, 0.0F, z), col);
    }

    // Outer bench boundary border
    addLine(glm::vec3(-xHalfWidth, 0.0F, zMin), glm::vec3(xHalfWidth, 0.0F, zMin), borderColor);
    addLine(glm::vec3(xHalfWidth, 0.0F, zMin), glm::vec3(xHalfWidth, 0.0F, zMax), borderColor);
    addLine(glm::vec3(xHalfWidth, 0.0F, zMax), glm::vec3(-xHalfWidth, 0.0F, zMax), borderColor);
    addLine(glm::vec3(-xHalfWidth, 0.0F, zMax), glm::vec3(-xHalfWidth, 0.0F, zMin), borderColor);

    // RGB Coordinate Axes at Origin (0,0,0)
    // +X Axis (Red)
    constexpr float xLength = 0.20F;
    const glm::vec4 xAxisColor(0.95F, 0.20F, 0.20F, 1.0F);
    addLine(glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(xLength, 0.0F, 0.0F), xAxisColor);
    addLine(glm::vec3(xLength, 0.0F, 0.0F), glm::vec3(xLength - 0.03F, 0.0F, 0.015F), xAxisColor);
    addLine(glm::vec3(xLength, 0.0F, 0.0F), glm::vec3(xLength - 0.03F, 0.0F, -0.015F), xAxisColor);

    // +Y Axis (Green) - Upward
    constexpr float yLength = 0.20F;
    const glm::vec4 yAxisColor(0.20F, 0.90F, 0.30F, 1.0F);
    addLine(glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, yLength, 0.0F), yAxisColor);
    addLine(glm::vec3(0.0F, yLength, 0.0F), glm::vec3(0.015F, yLength - 0.03F, 0.0F), yAxisColor);
    addLine(glm::vec3(0.0F, yLength, 0.0F), glm::vec3(-0.015F, yLength - 0.03F, 0.0F), yAxisColor);

    // +Z Axis (Blue) - Optical propagation axis (Longest axis!)
    constexpr float zLength = 1.80F;
    const glm::vec4 zAxisColor(0.25F, 0.60F, 1.0F, 1.0F);
    addLine(glm::vec3(0.0F, 0.0F, 0.0F), glm::vec3(0.0F, 0.0F, zLength), zAxisColor);
    addLine(glm::vec3(0.0F, 0.0F, zLength), glm::vec3(0.02F, 0.0F, zLength - 0.04F), zAxisColor);
    addLine(glm::vec3(0.0F, 0.0F, zLength), glm::vec3(-0.02F, 0.0F, zLength - 0.04F), zAxisColor);

    // Ticks along +Z optical axis every 0.10 m
    for (float tickZ = 0.10F; tickZ < zLength - 0.05F; tickZ += 0.10F) {
        const float tickSize = (std::abs(std::remainder(tickZ, 0.50F)) < 0.01F) ? 0.015F : 0.008F;
        addLine(glm::vec3(-tickSize, 0.0F, tickZ), glm::vec3(tickSize, 0.0F, tickZ), zAxisColor);
    }

    const std::size_t vertexCount = vertices.size();
    if (vertexCount > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "OpticalBenchRenderer::generateGridAndAxes failed: vertex count (%llu) exceeds GLsizei max (%lld)",
            static_cast<unsigned long long>(vertexCount),
            static_cast<long long>(std::numeric_limits<GLsizei>::max()));
        return false;
    }

    if (vertexCount > std::numeric_limits<std::size_t>::max() / sizeof(BenchVertex)) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "OpticalBenchRenderer::generateGridAndAxes failed: buffer byte size calculation overflows size_t for %llu vertices",
            static_cast<unsigned long long>(vertexCount));
        return false;
    }

    const std::size_t byteSize = vertexCount * sizeof(BenchVertex);
    if (byteSize > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
        SDL_LogError(
            SDL_LOG_CATEGORY_RENDER,
            "OpticalBenchRenderer::generateGridAndAxes failed: buffer byte size (%llu) exceeds GLsizeiptr max (%lld)",
            static_cast<unsigned long long>(byteSize),
            static_cast<long long>(std::numeric_limits<GLsizeiptr>::max()));
        return false;
    }

    // Preserve caller's GL bindings so we do not pollute external OpenGL state.
    const GlBufferBindingGuard bindingGuard;

    // Upload to VBO
    gridVertexCount_ = static_cast<GLsizei>(vertexCount);
    glBindVertexArray(gridVao_);
    glBindBuffer(GL_ARRAY_BUFFER, gridVbo_);
    glBufferData(
        GL_ARRAY_BUFFER,
        static_cast<GLsizeiptr>(byteSize),
        vertices.data(),
        GL_STATIC_DRAW);

    glEnableVertexAttribArray(0);
    glVertexAttribPointer(
        0,
        3,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BenchVertex),
        reinterpret_cast<const void*>(offsetof(BenchVertex, position)));

    glEnableVertexAttribArray(1);
    glVertexAttribPointer(
        1,
        4,
        GL_FLOAT,
        GL_FALSE,
        sizeof(BenchVertex),
        reinterpret_cast<const void*>(offsetof(BenchVertex, color)));

    return true;
}

bool OpticalBenchRenderer::updateScene(
    const optics::scene::OpticalBenchScene& scene,
    const optics::scene::ThinLensImagePrediction& prediction,
    std::span<const optics::ray::RaySegment> raySegments) {
    // 1. Double-to-float finite and range validation.
    // If any parameter is invalid, preserve old scene and return without touching cpuSceneVertices_.
    glm::vec3 srcPos {};
    if (!toFiniteVec3(scene.source.positionMetres, srcPos)) {
        return false;
    }

    float lensZ = 0.0F;
    float lensX = 0.0F;
    float lensY = 0.0F;
    float lensR = 0.0F;
    float lensF = 0.0F;
    if (!toFiniteFloat(scene.lens.planeZMetres, lensZ)
        || !toFiniteFloat(scene.lens.centreXMetres, lensX)
        || !toFiniteFloat(scene.lens.centreYMetres, lensY)
        || !toFiniteFloat(scene.lens.clearApertureRadiusMetres, lensR, 1e-6, 10.0)
        || !toFiniteFloat(scene.lens.focalLengthMetres, lensF)) {
        return false;
    }

    float apZ = 0.0F;
    float apX = 0.0F;
    float apY = 0.0F;
    float apR = 0.0F;
    if (!toFiniteFloat(scene.aperture.planeZMetres, apZ)
        || !toFiniteFloat(scene.aperture.centreXMetres, apX)
        || !toFiniteFloat(scene.aperture.centreYMetres, apY)
        || !toFiniteFloat(scene.aperture.radiusMetres, apR, 1e-6, 10.0)) {
        return false;
    }

    float scrZ = 0.0F;
    float scrX = 0.0F;
    float scrY = 0.0F;
    float scrW = 0.0F;
    float scrH = 0.0F;
    if (!toFiniteFloat(scene.screen.planeZMetres, scrZ)
        || !toFiniteFloat(scene.screen.centreXMetres, scrX)
        || !toFiniteFloat(scene.screen.centreYMetres, scrY)
        || !toFiniteFloat(scene.screen.widthMetres, scrW, 1e-6, 10.0)
        || !toFiniteFloat(scene.screen.heightMetres, scrH, 1e-6, 10.0)) {
        return false;
    }

    glm::vec3 predPos {};
    float predPlaneZ = 0.0F;
    if (prediction.nature == optics::scene::ImageNature::Real
        || prediction.nature == optics::scene::ImageNature::Virtual) {
        if (!toFiniteVec3(prediction.imagePositionMetres, predPos, -1e4, 1e4)
            || !toFiniteFloat(prediction.imagePlaneZMetres, predPlaneZ, -1e4, 1e4)) {
            return false;
        }
    }

    // Object-side NA validation: reject update and preserve old scene if invalid or non-finite
    optics::scene::ObjectSideNumericalApertureResult naResult {};
    try {
        naResult = optics::scene::computeObjectSideNumericalAperture(scene);
    } catch (const std::exception&) {
        return false;
    }

    glm::vec3 naRimCenter {};
    float naRimRadius = 0.0F;
    float naHalfAngle = 0.0F;
    float naValue = 0.0F;
    if (!toFiniteVec3(naResult.rimCenterMetres, naRimCenter)
        || !toFiniteFloat(naResult.rimRadiusMetres, naRimRadius, 1e-6, 10.0)
        || !toFiniteFloat(naResult.halfAngleRadians, naHalfAngle, 1e-6, std::numbers::pi)
        || !toFiniteFloat(naResult.numericalAperture, naValue, 1e-6, 10.0)) {
        return false;
    }

    // Pre-validate all ray segments
    for (const auto& segment : raySegments) {
        glm::vec3 start {};
        glm::vec3 end {};
        if (!toFiniteVec3(segment.startMetres, start) || !toFiniteVec3(segment.endMetres, end)) {
            return false;
        }
    }

    // 2. Vertex budget and checked limits
    constexpr std::size_t kMaxDashesPerSegment = 256;
    constexpr std::size_t kMaxVerticesPerSegment = kMaxDashesPerSegment * 2; // 512
    constexpr std::size_t kBaseEstimatedVertices = 768;
    constexpr std::size_t kNominalVerticesPerSegment = 16;

    constexpr std::size_t kMaxGlVertices = static_cast<std::size_t>(std::numeric_limits<GLsizei>::max());
    constexpr std::size_t kMaxGlByteSize = static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max());
    constexpr std::size_t kMaxAllowedVertices = std::min({
        kMaxGlVertices,
        kMaxGlByteSize / sizeof(BenchVertex),
        std::numeric_limits<std::size_t>::max() / sizeof(BenchVertex)
    });

    if constexpr (kMaxAllowedVertices <= kBaseEstimatedVertices) {
        return false;
    }

    const std::size_t maxRaySegments = (kMaxAllowedVertices - kBaseEstimatedVertices) / kMaxVerticesPerSegment;
    if (raySegments.size() > maxRaySegments) {
        return false;
    }

    // Checked nominal estimate for reserve
    std::size_t estimatedVertices = kBaseEstimatedVertices;
    if (raySegments.size() > (kMaxAllowedVertices - kBaseEstimatedVertices) / kNominalVerticesPerSegment) {
        estimatedVertices = kMaxAllowedVertices;
    } else {
        estimatedVertices += raySegments.size() * kNominalVerticesPerSegment;
    }

    // 3. CPU geometry generation into stagingVertices_ (reusing member capacity)
    stagingVertices_.clear();
    if (stagingVertices_.capacity() < estimatedVertices) {
        stagingVertices_.reserve(estimatedVertices);
    }

    auto addLine = [this](const glm::vec3& start, const glm::vec3& end, const glm::vec4& color) {
        stagingVertices_.push_back(BenchVertex {start, color});
        stagingVertices_.push_back(BenchVertex {end, color});
    };

    auto addCircle = [&addLine](
        const glm::vec3& center,
        float radius,
        int segmentCount,
        const glm::vec4& color) {
        constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
        const float step = twoPi / static_cast<float>(segmentCount);
        for (int i = 0; i < segmentCount; ++i) {
            const float theta0 = static_cast<float>(i) * step;
            const float theta1 = static_cast<float>(i + 1) * step;
            const glm::vec3 p0(center.x + radius * std::cos(theta0), center.y + radius * std::sin(theta0), center.z);
            const glm::vec3 p1(center.x + radius * std::cos(theta1), center.y + radius * std::sin(theta1), center.z);
            addLine(p0, p1, color);
        }
    };

    const glm::vec4 postColor(0.50F, 0.55F, 0.62F, 0.85F);
    const glm::vec4 baseColor(0.40F, 0.45F, 0.52F, 0.80F);

    auto addPostAndBase = [&addLine, &postColor, &baseColor](float x, float yBottom, float z) {
        addLine(glm::vec3(x, yBottom, z), glm::vec3(x, 0.0F, z), postColor);
        // Base clamp on bench rail (Y=0)
        constexpr float baseRadius = 0.020F;
        addLine(glm::vec3(x - baseRadius, 0.0F, z), glm::vec3(x + baseRadius, 0.0F, z), baseColor);
        addLine(glm::vec3(x, 0.0F, z - baseRadius), glm::vec3(x, 0.0F, z + baseRadius), baseColor);
    };

    // A. Point Source Display
    {
        const glm::vec4 sourceColor(1.0F, 0.82F, 0.15F, 1.0F);
        constexpr float s = 0.012F;
        // 3D Octahedron / Crosshair at point source
        addLine(glm::vec3(srcPos.x - s, srcPos.y, srcPos.z), glm::vec3(srcPos.x + s, srcPos.y, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x, srcPos.y - s, srcPos.z), glm::vec3(srcPos.x, srcPos.y + s, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x, srcPos.y, srcPos.z - s), glm::vec3(srcPos.x, srcPos.y, srcPos.z + s), sourceColor);

        // Diamond wire edges
        addLine(glm::vec3(srcPos.x + s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y + s, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x, srcPos.y + s, srcPos.z), glm::vec3(srcPos.x - s, srcPos.y, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x - s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y - s, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x, srcPos.y - s, srcPos.z), glm::vec3(srcPos.x + s, srcPos.y, srcPos.z), sourceColor);

        addLine(glm::vec3(srcPos.x + s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y, srcPos.z + s), sourceColor);
        addLine(glm::vec3(srcPos.x - s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y + s, srcPos.z), sourceColor);
        addLine(glm::vec3(srcPos.x + s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y, srcPos.z - s), sourceColor);
        addLine(glm::vec3(srcPos.x - s, srcPos.y, srcPos.z), glm::vec3(srcPos.x, srcPos.y, srcPos.z - s), sourceColor);

        // Mounting post
        addPostAndBase(srcPos.x, srcPos.y - s, srcPos.z);
    }

    // B. Thin Lens (Circle + Bracket/Mount) Display
    {
        const glm::vec4 lensGlassColor(0.35F, 0.85F, 0.95F, 0.95F);
        const glm::vec4 lensMountColor(0.48F, 0.55F, 0.65F, 0.90F);
        const glm::vec3 lensCenter(lensX, lensY, lensZ);

        // Clear aperture circle
        addCircle(lensCenter, lensR, 48, lensGlassColor);

        // Lens center optical axis cross mark
        const float crossR = lensR * 0.20F;
        addLine(glm::vec3(lensX - crossR, lensY, lensZ), glm::vec3(lensX + crossR, lensY, lensZ), lensGlassColor);
        addLine(glm::vec3(lensX, lensY - crossR, lensZ), glm::vec3(lensX, lensY + crossR, lensZ), lensGlassColor);

        // Outer mounting bracket ring
        const float outerR = lensR * 1.22F + 0.004F;
        addCircle(lensCenter, outerR, 48, lensMountColor);

        // 4 radial mounting struts
        addLine(glm::vec3(lensX + lensR, lensY, lensZ), glm::vec3(lensX + outerR, lensY, lensZ), lensMountColor);
        addLine(glm::vec3(lensX - lensR, lensY, lensZ), glm::vec3(lensX - outerR, lensY, lensZ), lensMountColor);
        addLine(glm::vec3(lensX, lensY + lensR, lensZ), glm::vec3(lensX, lensY + outerR, lensZ), lensMountColor);
        addLine(glm::vec3(lensX, lensY - lensR, lensZ), glm::vec3(lensX, lensY - outerR, lensZ), lensMountColor);

        // Collar block at bottom of bracket
        constexpr float collarHalfW = 0.015F;
        constexpr float collarH = 0.008F;
        const float collarTopY = lensY - outerR;
        const float collarBotY = collarTopY - collarH;
        addLine(glm::vec3(lensX - collarHalfW, collarTopY, lensZ), glm::vec3(lensX + collarHalfW, collarTopY, lensZ), lensMountColor);
        addLine(glm::vec3(lensX - collarHalfW, collarBotY, lensZ), glm::vec3(lensX + collarHalfW, collarBotY, lensZ), lensMountColor);
        addLine(glm::vec3(lensX - collarHalfW, collarTopY, lensZ), glm::vec3(lensX - collarHalfW, collarBotY, lensZ), lensMountColor);
        addLine(glm::vec3(lensX + collarHalfW, collarTopY, lensZ), glm::vec3(lensX + collarHalfW, collarBotY, lensZ), lensMountColor);

        // Mounting post
        addPostAndBase(lensX, collarBotY, lensZ);
    }

    // C. Independent Aperture Display
    {
        const glm::vec4 apertureColor(0.95F, 0.60F, 0.18F, 0.95F);
        const glm::vec4 plateColor(0.60F, 0.50F, 0.35F, 0.85F);
        const glm::vec3 apCenter(apX, apY, apZ);

        // Circular aperture opening
        addCircle(apCenter, apR, 48, apertureColor);

        // Outer diaphragm square plate
        const float halfSide = apR * 1.35F + 0.004F;
        const glm::vec3 c0(apX - halfSide, apY - halfSide, apZ);
        const glm::vec3 c1(apX + halfSide, apY - halfSide, apZ);
        const glm::vec3 c2(apX + halfSide, apY + halfSide, apZ);
        const glm::vec3 c3(apX - halfSide, apY + halfSide, apZ);
        addLine(c0, c1, plateColor);
        addLine(c1, c2, plateColor);
        addLine(c2, c3, plateColor);
        addLine(c3, c0, plateColor);

        // Diagonal spokes to iris rim
        constexpr float sqrt2Inv = 0.70710678F;
        addLine(c0, glm::vec3(apX - apR * sqrt2Inv, apY - apR * sqrt2Inv, apZ), plateColor);
        addLine(c1, glm::vec3(apX + apR * sqrt2Inv, apY - apR * sqrt2Inv, apZ), plateColor);
        addLine(c2, glm::vec3(apX + apR * sqrt2Inv, apY + apR * sqrt2Inv, apZ), plateColor);
        addLine(c3, glm::vec3(apX - apR * sqrt2Inv, apY + apR * sqrt2Inv, apZ), plateColor);

        // Mounting post
        addPostAndBase(apX, apY - halfSide, apZ);
    }

    // D. Screen Rectangle Display
    {
        const glm::vec4 screenBorderColor(0.88F, 0.92F, 0.98F, 0.95F);
        const glm::vec4 screenGridColor(0.40F, 0.55F, 0.70F, 0.45F);
        const float halfW = scrW * 0.50F;
        const float halfH = scrH * 0.50F;

        // Screen boundary rectangle
        const glm::vec3 s0(scrX - halfW, scrY - halfH, scrZ);
        const glm::vec3 s1(scrX + halfW, scrY - halfH, scrZ);
        const glm::vec3 s2(scrX + halfW, scrY + halfH, scrZ);
        const glm::vec3 s3(scrX - halfW, scrY + halfH, scrZ);
        addLine(s0, s1, screenBorderColor);
        addLine(s1, s2, screenBorderColor);
        addLine(s2, s3, screenBorderColor);
        addLine(s3, s0, screenBorderColor);

        // Crosshairs on screen
        addLine(glm::vec3(scrX - halfW, scrY, scrZ), glm::vec3(scrX + halfW, scrY, scrZ), screenGridColor);
        addLine(glm::vec3(scrX, scrY - halfH, scrZ), glm::vec3(scrX, scrY + halfH, scrZ), screenGridColor);

        // Internal grid lines
        addLine(glm::vec3(scrX - halfW, scrY - halfH * 0.5F, scrZ), glm::vec3(scrX + halfW, scrY - halfH * 0.5F, scrZ), screenGridColor);
        addLine(glm::vec3(scrX - halfW, scrY + halfH * 0.5F, scrZ), glm::vec3(scrX + halfW, scrY + halfH * 0.5F, scrZ), screenGridColor);
        addLine(glm::vec3(scrX - halfW * 0.5F, scrY - halfH, scrZ), glm::vec3(scrX - halfW * 0.5F, scrY + halfH, scrZ), screenGridColor);
        addLine(glm::vec3(scrX + halfW * 0.5F, scrY - halfH, scrZ), glm::vec3(scrX + halfW * 0.5F, scrY + halfH, scrZ), screenGridColor);

        // Mounting post
        addPostAndBase(scrX, scrY - halfH, scrZ);
    }

    // E. ThinLensImagePrediction Markers (Real, Virtual, At-Infinity)
    {
        switch (prediction.nature) {
        case optics::scene::ImageNature::Real: {
            const glm::vec4 realColor(0.20F, 0.95F, 0.40F, 1.0F);
            constexpr float d = 0.012F;
            // Conjugate focused image diamond marker
            addLine(glm::vec3(predPos.x - d, predPos.y, predPos.z), glm::vec3(predPos.x, predPos.y + d, predPos.z), realColor);
            addLine(glm::vec3(predPos.x, predPos.y + d, predPos.z), glm::vec3(predPos.x + d, predPos.y, predPos.z), realColor);
            addLine(glm::vec3(predPos.x + d, predPos.y, predPos.z), glm::vec3(predPos.x, predPos.y - d, predPos.z), realColor);
            addLine(glm::vec3(predPos.x, predPos.y - d, predPos.z), glm::vec3(predPos.x - d, predPos.y, predPos.z), realColor);

            // 3D Crosshair
            addLine(glm::vec3(predPos.x - d * 1.5F, predPos.y, predPos.z), glm::vec3(predPos.x + d * 1.5F, predPos.y, predPos.z), realColor);
            addLine(glm::vec3(predPos.x, predPos.y - d * 1.5F, predPos.z), glm::vec3(predPos.x, predPos.y + d * 1.5F, predPos.z), realColor);
            addLine(glm::vec3(predPos.x, predPos.y, predPos.z - d * 1.5F), glm::vec3(predPos.x, predPos.y, predPos.z + d * 1.5F), realColor);

            // Conjugate focal plane tick marks
            constexpr float planeTick = 0.025F;
            addLine(glm::vec3(predPos.x - planeTick, predPos.y, predPlaneZ), glm::vec3(predPos.x + planeTick, predPos.y, predPlaneZ), realColor);
            addLine(glm::vec3(predPos.x, predPos.y - planeTick, predPlaneZ), glm::vec3(predPos.x, predPos.y + planeTick, predPlaneZ), realColor);
            break;
        }
        case optics::scene::ImageNature::Virtual: {
            const glm::vec4 virtColor(0.95F, 0.25F, 0.85F, 1.0F);
            constexpr float d = 0.012F;
            // Virtual image dashed cross / star
            addLine(glm::vec3(predPos.x - d, predPos.y - d, predPos.z), glm::vec3(predPos.x + d, predPos.y + d, predPos.z), virtColor);
            addLine(glm::vec3(predPos.x - d, predPos.y + d, predPos.z), glm::vec3(predPos.x + d, predPos.y - d, predPos.z), virtColor);
            addLine(glm::vec3(predPos.x - d * 1.5F, predPos.y, predPos.z), glm::vec3(predPos.x + d * 1.5F, predPos.y, predPos.z), virtColor);
            addLine(glm::vec3(predPos.x, predPos.y - d * 1.5F, predPos.z), glm::vec3(predPos.x, predPos.y + d * 1.5F, predPos.z), virtColor);

            // Virtual conjugate plane indicator (4 corners bracket)
            constexpr float planeTick = 0.020F;
            addLine(glm::vec3(predPos.x - planeTick, predPos.y - planeTick, predPlaneZ), glm::vec3(predPos.x - planeTick * 0.5F, predPos.y - planeTick, predPlaneZ), virtColor);
            addLine(glm::vec3(predPos.x + planeTick, predPos.y - planeTick, predPlaneZ), glm::vec3(predPos.x + planeTick * 0.5F, predPos.y - planeTick, predPlaneZ), virtColor);
            addLine(glm::vec3(predPos.x - planeTick, predPos.y + planeTick, predPlaneZ), glm::vec3(predPos.x - planeTick * 0.5F, predPos.y + planeTick, predPlaneZ), virtColor);
            addLine(glm::vec3(predPos.x + planeTick, predPos.y + planeTick, predPlaneZ), glm::vec3(predPos.x + planeTick * 0.5F, predPos.y + planeTick, predPlaneZ), virtColor);
            break;
        }
        case optics::scene::ImageNature::AtInfinity: {
            const glm::vec4 infColor(0.30F, 0.80F, 1.0F, 0.95F);
            // Collimated infinity indicator: infinity symbol loops and parallel arrows
            const float indicatorZ = lensZ + std::abs(lensF);
            constexpr float rLoop = 0.008F;
            const glm::vec3 leftLoopCenter(lensX - rLoop, lensY, indicatorZ);
            const glm::vec3 rightLoopCenter(lensX + rLoop, lensY, indicatorZ);
            addCircle(leftLoopCenter, rLoop, 24, infColor);
            addCircle(rightLoopCenter, rLoop, 24, infColor);

            // Parallel collimation direction arrows along +Z
            constexpr float arrowLen = 0.040F;
            const float arrowZ0 = indicatorZ + 0.020F;
            const float arrowZ1 = arrowZ0 + arrowLen;
            for (float offset : {-0.015F, 0.0F, 0.015F}) {
                addLine(glm::vec3(lensX + offset, lensY, arrowZ0), glm::vec3(lensX + offset, lensY, arrowZ1), infColor);
                addLine(glm::vec3(lensX + offset, lensY, arrowZ1), glm::vec3(lensX + offset - 0.003F, lensY, arrowZ1 - 0.006F), infColor);
                addLine(glm::vec3(lensX + offset, lensY, arrowZ1), glm::vec3(lensX + offset + 0.003F, lensY, arrowZ1 - 0.006F), infColor);
            }
            break;
        }
        }
    }

    // F. Ray Segments Display (Incident, Transmitted, Clipped, VirtualExtension)
    const glm::vec4 incidentColor(1.0F, 0.85F, 0.15F, 0.85F);      // Amber laser
    const glm::vec4 transmittedColor(0.15F, 0.90F, 1.0F, 0.85F);   // Cyan
    const glm::vec4 clippedColor(0.95F, 0.20F, 0.20F, 0.70F);       // Crimson
    const glm::vec4 virtualExtColor(0.95F, 0.30F, 0.90F, 0.80F);    // Magenta dashed

    for (const auto& segment : raySegments) {
        glm::vec3 p0 {};
        glm::vec3 p1 {};
        if (!toFiniteVec3(segment.startMetres, p0) || !toFiniteVec3(segment.endMetres, p1)) {
            continue;
        }

        switch (segment.kind) {
        case optics::ray::RaySegmentKind::Incident:
            addLine(p0, p1, incidentColor);
            break;
        case optics::ray::RaySegmentKind::Transmitted:
            addLine(p0, p1, transmittedColor);
            break;
        case optics::ray::RaySegmentKind::Clipped:
            addLine(p0, p1, clippedColor);
            break;
        case optics::ray::RaySegmentKind::VirtualExtension: {
            // VirtualExtension MUST be rendered as a dashed line with bounded dash count
            const float segLen = glm::length(p1 - p0);
            if (segLen > 1e-6F && std::isfinite(segLen)) {
                const glm::vec3 dir = (p1 - p0) / segLen;
                constexpr float kNominalDashLen = 0.008F; // 8 mm dash
                constexpr float kNominalGapLen = 0.006F;  // 6 mm gap
                constexpr float kNominalPeriod = kNominalDashLen + kNominalGapLen; // 14 mm

                float dashLen = kNominalDashLen;
                float period = kNominalPeriod;

                const float nominalCount = std::ceil(segLen / kNominalPeriod);
                if (nominalCount > static_cast<float>(kMaxDashesPerSegment)) {
                    period = segLen / static_cast<float>(kMaxDashesPerSegment);
                    constexpr float ratio = kNominalDashLen / kNominalPeriod;
                    dashLen = period * ratio;
                }

                const std::size_t dashCount = std::min(
                    static_cast<std::size_t>(std::max(1.0F, std::ceil(segLen / period))),
                    kMaxDashesPerSegment);

                for (std::size_t d = 0; d < dashCount; ++d) {
                    const float dStart = static_cast<float>(d) * period;
                    if (dStart >= segLen) {
                        break;
                    }
                    const float dEnd = std::min(dStart + dashLen, segLen);
                    if (dEnd > dStart) {
                        addLine(p0 + dir * dStart, p0 + dir * dEnd, virtualExtColor);
                    }
                }
            }
            break;
        }
        }
    }

    // G. Object-side Numerical Aperture (NA) Cone Display
    {
        // Translucent Emerald / Mint Green boundary lines for exact physical pupil cone.
        // For approximate results (downstream stop unmodeled), use distinct warning amber color
        // and dashed generator lines so the visual display remains informative without misleading.
        const glm::vec4 naConeColor = naResult.approximate
            ? glm::vec4(0.95F, 0.72F, 0.18F, 0.50F)
            : glm::vec4(0.20F, 0.88F, 0.70F, 0.45F);

        // Limiting rim circle
        addCircle(naRimCenter, naRimRadius, 48, naConeColor);

        // 16 generator boundary lines from point source to limiting rim
        constexpr int kNaConeLineCount = 16;
        constexpr float twoPi = 2.0F * std::numbers::pi_v<float>;
        const float step = twoPi / static_cast<float>(kNaConeLineCount);
        for (int i = 0; i < kNaConeLineCount; ++i) {
            const float theta = static_cast<float>(i) * step;
            const glm::vec3 rimPoint(
                naRimCenter.x + naRimRadius * std::cos(theta),
                naRimCenter.y + naRimRadius * std::sin(theta),
                naRimCenter.z);
            if (naResult.approximate) {
                // Dashed generator lines for approximate / unvalidated cone
                constexpr int kDashesPerApproxGenerator = 4;
                for (int d = 0; d < kDashesPerApproxGenerator; ++d) {
                    const float t0 = static_cast<float>(d) / static_cast<float>(kDashesPerApproxGenerator);
                    const float t1 = (static_cast<float>(d) + 0.60F) / static_cast<float>(kDashesPerApproxGenerator);
                    addLine(glm::mix(srcPos, rimPoint, t0), glm::mix(srcPos, rimPoint, t1), naConeColor);
                }
            } else {
                addLine(srcPos, rimPoint, naConeColor);
            }
        }
    }

    // 4. Overflow validation of generated geometry
    const std::size_t vertexCount = stagingVertices_.size();
    if (vertexCount > kMaxAllowedVertices) {
        stagingVertices_.clear();
        return false;
    }

    // 5. Update CPU buffer via swap (guaranteeing buffer capacity reuse) and mark dirty
    cpuSceneVertices_.swap(stagingVertices_);
    sceneDirty_ = true;
    return true;
}

void OpticalBenchRenderer::uploadSceneBufferIfNeeded() {
    if (!sceneDirty_ || sceneVao_ == 0 || sceneVbo_ == 0) {
        return;
    }

    const std::size_t vertexCount = cpuSceneVertices_.size();
    if (vertexCount > static_cast<std::size_t>(std::numeric_limits<GLsizei>::max())
        || vertexCount > std::numeric_limits<std::size_t>::max() / sizeof(BenchVertex)) {
        return;
    }

    const std::size_t byteSize = vertexCount * sizeof(BenchVertex);
    if (byteSize > static_cast<std::size_t>(std::numeric_limits<GLsizeiptr>::max())) {
        return;
    }

    const GlBufferBindingGuard bindingGuard;
    glBindVertexArray(sceneVao_);
    glBindBuffer(GL_ARRAY_BUFFER, sceneVbo_);

    if (byteSize > sceneVboCapacityBytes_) {
        glBufferData(
            GL_ARRAY_BUFFER,
            static_cast<GLsizeiptr>(byteSize),
            cpuSceneVertices_.data(),
            GL_DYNAMIC_DRAW);
        sceneVboCapacityBytes_ = byteSize;
    } else if (byteSize > 0) {
        glBufferSubData(
            GL_ARRAY_BUFFER,
            0,
            static_cast<GLsizeiptr>(byteSize),
            cpuSceneVertices_.data());
    }

    sceneVertexCount_ = static_cast<GLsizei>(vertexCount);
    sceneDirty_ = false;
}

void OpticalBenchRenderer::render(int width, int height, const OrbitCamera& camera) {
    if (width <= 0 || height <= 0) {
        return;
    }

    if (!initialized_ && !initialize()) {
        return;
    }

    if (!framebuffer_.resize(width, height)) {
        return;
    }

    // Upload scene dynamic buffer if dirty
    uploadSceneBufferIfNeeded();

    // Setup rendering state for optical bench
    framebuffer_.bind();
    glViewport(0, 0, width, height);

    glDisable(GL_SCISSOR_TEST);
    glDisable(GL_CULL_FACE);
    glDisable(GL_STENCIL_TEST);
    glPolygonMode(GL_FRONT_AND_BACK, GL_FILL);

    glColorMask(GL_TRUE, GL_TRUE, GL_TRUE, GL_TRUE);
    glDepthMask(GL_TRUE);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);

    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);

    glClearColor(0.035F, 0.045F, 0.060F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    // Calculate projection matrix with current viewport aspect ratio
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 projection = camera.projectionMatrix(aspect);
    const glm::mat4 viewProj = projection * camera.viewMatrix();

    shader_.use();
    shader_.setMat4("uViewProjection", viewProj);

    if (gridVertexCount_ > 0) {
        glBindVertexArray(gridVao_);
        glDrawArrays(GL_LINES, 0, gridVertexCount_);
    }

    if (sceneVertexCount_ > 0) {
        glBindVertexArray(sceneVao_);
        glDrawArrays(GL_LINES, 0, sceneVertexCount_);
    }

    glBindVertexArray(0);
    framebuffer_.unbind();
}

} // namespace holobench::render
