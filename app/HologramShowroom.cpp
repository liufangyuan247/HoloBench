#include "app/HologramShowroom.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <iostream>
#include <imgui.h>
#include <numbers>
#include <string_view>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>
#include <glm/gtc/matrix_transform.hpp>
#include <SDL3/SDL_log.h>

namespace holobench::app {
namespace h = optics::holography;

namespace {
constexpr std::string_view kShowroomTextureVertexShader = R"(#version 460 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec2 aUv;

uniform mat4 uViewProjection;

out vec3 vWorldPos;
out vec2 vUv;

void main() {
    vWorldPos = aPosition;
    vUv = aUv;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kShowroomTextureFragmentShader = R"(#version 460 core
in vec3 vWorldPos;
in vec2 vUv;

uniform vec3 uEyePos;
uniform vec3 uLampPos;
uniform vec3 uPlateCenter;
uniform vec3 uPlateX;
uniform vec3 uPlateY;
uniform vec3 uPlateZ;

uniform float uPlateWidth;
uniform float uPlateHeight;

uniform float uRefractiveIndex;
uniform float uIndexModulation;
uniform float uThickness;
uniform float uExposure;
uniform float uLampLit;
uniform int uIsRgb;
uniform int uShowFringes;
uniform int uGeometry; // 0: Transmission, 1: Reflection

uniform float uWavelength[3];
uniform vec3 uChannelColor[3];

uniform sampler2D uReconTex;
uniform sampler2D uFringeTex0;
uniform sampler2D uFringeTex1;
uniform sampler2D uFringeTex2;

out vec4 fragColor;

const float PI = 3.14159265358979323846;
const float TWO_PI = 6.28318530717958647692;

void main() {
    if (uLampLit <= 0.0) {
        fragColor = vec4(0.0, 0.0, 0.0, 0.0);
        return;
    }

    // 1. Interference / Microscopic Diffraction Fringes Mode
    if (uShowFringes == 1) {
        vec2 posMetres = (vUv - vec2(0.5)) * vec2(uPlateWidth, uPlateHeight);
        if (uIsRgb == 1) {
            vec3 col = vec3(0.0);
            for (int c = 0; c < 3; ++c) {
                vec4 gData = (c == 0) ? texture(uFringeTex0, vUv) :
                             (c == 1) ? texture(uFringeTex1, vUv) :
                                        texture(uFringeTex2, vUv);
                float carrier = (gData.g * posMetres.x + gData.b * posMetres.y) * 0.02;
                float fringe = 0.5 + 0.5 * cos(carrier);
                col += uChannelColor[c] * gData.r * fringe * 1.5;
            }
            fragColor = vec4(col * uExposure, 0.95);
        } else {
            vec4 gData = texture(uFringeTex0, vUv);
            float carrier = (gData.g * posMetres.x + gData.b * posMetres.y) * 0.02;
            float fringe = 0.5 + 0.5 * cos(carrier);
            vec3 col = mix(vec3(0.02, 0.04, 0.02), uChannelColor[0] * 1.5, gData.r * fringe);
            fragColor = vec4(col * uExposure, 0.95);
        }
        return;
    }
    // 2. Physical Holographic Wavefront Reconstruction
    // Samples the reconstructed sensor wavefront computed via angular spectrum propagation
    // and physical pupil aperture filtering.
    vec4 recon = texture(uReconTex, vUv);
    vec3 col = recon.rgb;
    float maxC = max(max(col.r, col.g), col.b);
    vec3 baseTint = vec3(0.015, 0.022, 0.025);
    fragColor = vec4(col + baseTint * (1.0 - clamp(maxC * 2.0, 0.0, 1.0)), 1.0);
}
)";

constexpr std::string_view kShowroomColorVertexShader = R"(#version 460 core
layout (location = 0) in vec3 aPosition;
layout (location = 1) in vec4 aColor;

uniform mat4 uViewProjection;

out vec4 vColor;

void main() {
    vColor = aColor;
    gl_Position = uViewProjection * vec4(aPosition, 1.0);
}
)";

constexpr std::string_view kShowroomColorFragmentShader = R"(#version 460 core
in vec4 vColor;

out vec4 fragColor;

void main() {
    fragColor = vColor;
}
)";

struct ShowroomTextureVertex {
    glm::vec3 pos;
    glm::vec2 uv;
};

struct ShowroomColorVertex {
    glm::vec3 pos;
    glm::vec4 col;
};

struct GlStateGuard {
    GLint prevFbo = 0;
    GLint prevViewport[4]{};
    GLfloat prevClearColor[4]{};
    GLboolean prevDepthTest = GL_FALSE;
    GLint prevDepthFunc = 0;
    GLboolean prevBlend = GL_FALSE;
    GLint prevBlendSrcRgb = 0, prevBlendDstRgb = 0, prevBlendSrcAlpha = 0, prevBlendDstAlpha = 0;
    GLboolean prevCullFace = GL_FALSE;
    GLboolean prevScissorTest = GL_FALSE;
    GLint prevProgram = 0;
    GLint prevVao = 0;
    GLint prevArrayBuffer = 0;
    GLint prevActiveTexture = 0;
    GLint prevTextureBinding = 0;

    GlStateGuard() {
        glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &prevFbo);
        glGetIntegerv(GL_VIEWPORT, prevViewport);
        glGetFloatv(GL_COLOR_CLEAR_VALUE, prevClearColor);
        prevDepthTest = glIsEnabled(GL_DEPTH_TEST);
        glGetIntegerv(GL_DEPTH_FUNC, &prevDepthFunc);
        prevBlend = glIsEnabled(GL_BLEND);
        glGetIntegerv(GL_BLEND_SRC_RGB, &prevBlendSrcRgb);
        glGetIntegerv(GL_BLEND_DST_RGB, &prevBlendDstRgb);
        glGetIntegerv(GL_BLEND_SRC_ALPHA, &prevBlendSrcAlpha);
        glGetIntegerv(GL_BLEND_DST_ALPHA, &prevBlendDstAlpha);
        prevCullFace = glIsEnabled(GL_CULL_FACE);
        prevScissorTest = glIsEnabled(GL_SCISSOR_TEST);
        glGetIntegerv(GL_CURRENT_PROGRAM, &prevProgram);
        glGetIntegerv(GL_VERTEX_ARRAY_BINDING, &prevVao);
        glGetIntegerv(GL_ARRAY_BUFFER_BINDING, &prevArrayBuffer);
        glGetIntegerv(GL_ACTIVE_TEXTURE, &prevActiveTexture);
        glGetIntegerv(GL_TEXTURE_BINDING_2D, &prevTextureBinding);
    }

    ~GlStateGuard() {
        glBindFramebuffer(GL_FRAMEBUFFER, static_cast<GLuint>(prevFbo));
        glViewport(prevViewport[0], prevViewport[1], prevViewport[2], prevViewport[3]);
        glClearColor(prevClearColor[0], prevClearColor[1], prevClearColor[2], prevClearColor[3]);
        if (prevDepthTest) glEnable(GL_DEPTH_TEST); else glDisable(GL_DEPTH_TEST);
        glDepthFunc(static_cast<GLenum>(prevDepthFunc));
        if (prevBlend) glEnable(GL_BLEND); else glDisable(GL_BLEND);
        glBlendFuncSeparate(static_cast<GLenum>(prevBlendSrcRgb),
                            static_cast<GLenum>(prevBlendDstRgb),
                            static_cast<GLenum>(prevBlendSrcAlpha),
                            static_cast<GLenum>(prevBlendDstAlpha));
        if (prevCullFace) glEnable(GL_CULL_FACE); else glDisable(GL_CULL_FACE);
        if (prevScissorTest) glEnable(GL_SCISSOR_TEST); else glDisable(GL_SCISSOR_TEST);
        glUseProgram(static_cast<GLuint>(prevProgram));
        glBindVertexArray(static_cast<GLuint>(prevVao));
        glBindBuffer(GL_ARRAY_BUFFER, static_cast<GLuint>(prevArrayBuffer));
        glActiveTexture(static_cast<GLenum>(prevActiveTexture));
        glBindTexture(GL_TEXTURE_2D, static_cast<GLuint>(prevTextureBinding));
    }
};

template <typename T = ImTextureID>
constexpr T toImTextureID(GLuint textureId) noexcept {
    if constexpr (std::is_pointer_v<T>) {
        return reinterpret_cast<T>(static_cast<std::uintptr_t>(textureId));
    } else {
        return static_cast<T>(textureId);
    }
}

glm::vec3 toGlm(const math::Vec3d &v) noexcept {
    return {static_cast<float>(v.x), static_cast<float>(v.y), static_cast<float>(v.z)};
}

math::Vec3d rotate(math::Vec3d v, double yaw, double pitch) {
    const math::Vec3d p{v.x, std::cos(pitch) * v.y - std::sin(pitch) * v.z,
                        std::sin(pitch) * v.y + std::cos(pitch) * v.z};
    return {std::cos(yaw) * p.x + std::sin(yaw) * p.z, p.y,
            -std::sin(yaw) * p.x + std::cos(yaw) * p.z};
}
} // namespace

HologramShowroom::HologramShowroom() = default;

HologramShowroom::~HologramShowroom() {
    destroyGl();
}

void HologramShowroom::initGl() {
    if (glInitialized_) return;
    std::string err;
    if (!textureShader_.compileAndLink(kShowroomTextureVertexShader, kShowroomTextureFragmentShader, &err)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "HologramShowroom texture shader error: %s", err.c_str());
        return;
    }
    if (!colorShader_.compileAndLink(kShowroomColorVertexShader, kShowroomColorFragmentShader, &err)) {
        SDL_LogError(SDL_LOG_CATEGORY_RENDER, "HologramShowroom color shader error: %s", err.c_str());
        return;
    }

    glGenVertexArrays(1, &textureVao_);
    glGenBuffers(1, &textureVbo_);
    glBindVertexArray(textureVao_);
    glBindBuffer(GL_ARRAY_BUFFER, textureVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShowroomTextureVertex),
                          reinterpret_cast<void *>(offsetof(ShowroomTextureVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 2, GL_FLOAT, GL_FALSE, sizeof(ShowroomTextureVertex),
                          reinterpret_cast<void *>(offsetof(ShowroomTextureVertex, uv)));

    glGenVertexArrays(1, &colorVao_);
    glGenBuffers(1, &colorVbo_);
    glBindVertexArray(colorVao_);
    glBindBuffer(GL_ARRAY_BUFFER, colorVbo_);
    glEnableVertexAttribArray(0);
    glVertexAttribPointer(0, 3, GL_FLOAT, GL_FALSE, sizeof(ShowroomColorVertex),
                          reinterpret_cast<void *>(offsetof(ShowroomColorVertex, pos)));
    glEnableVertexAttribArray(1);
    glVertexAttribPointer(1, 4, GL_FLOAT, GL_FALSE, sizeof(ShowroomColorVertex),
                          reinterpret_cast<void *>(offsetof(ShowroomColorVertex, col)));

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    glInitialized_ = true;
}

void HologramShowroom::destroyGl() noexcept {
    if (textureVao_ != 0) { glDeleteVertexArrays(1, &textureVao_); textureVao_ = 0; }
    if (textureVbo_ != 0) { glDeleteBuffers(1, &textureVbo_); textureVbo_ = 0; }
    if (colorVao_ != 0) { glDeleteVertexArrays(1, &colorVao_); colorVao_ = 0; }
    if (colorVbo_ != 0) { glDeleteBuffers(1, &colorVbo_); colorVbo_ = 0; }
    textureShader_.destroy();
    colorShader_.destroy();
    fbo_.destroy();
    for (auto &tex : fringeTextures_) {
        tex.destroy();
    }
    fringeTexturesReady_ = false;
    glInitialized_ = false;
}

void HologramShowroom::renderScene3D(int width, int height) {
    if (!glInitialized_) {
        initGl();
    }
    if (!glInitialized_) {
        return;
    }
    if (!fbo_.isValid() || fbo_.width() != width || fbo_.height() != height) {
        if (!fbo_.resize(width, height)) {
            return;
        }
    }

    GlStateGuard guard;

    fbo_.bind();
    glViewport(0, 0, width, height);
    glClearColor(0.032F, 0.045F, 0.068F, 1.0F);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glEnable(GL_DEPTH_TEST);
    glDepthFunc(GL_LEQUAL);
    glEnable(GL_BLEND);
    glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    glDisable(GL_CULL_FACE);

    const float fovY = glm::radians(60.0F);
    const float aspect = static_cast<float>(width) / static_cast<float>(height);
    const glm::mat4 projection = glm::perspective(fovY, aspect, 0.01F, 10.0F);
    const glm::vec3 eyePos{static_cast<float>(panX_), static_cast<float>(panY_), static_cast<float>(cameraDistance_)};
    const glm::vec3 lookTarget{static_cast<float>(panX_), static_cast<float>(panY_), 0.0F};
    const glm::vec3 upVec{0.0F, 1.0F, 0.0F};
    const glm::mat4 view = glm::lookAt(eyePos, lookTarget, upVec);
    const glm::mat4 viewProj = projection * view;

    if (!asset_) {
        fbo_.unbind();
        return;
    }

    const double rawW = static_cast<double>(asset_->coupling.width()) * asset_->coupling.pitchXMetres();
    const double rawH = static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres();
    const double plateW = (rawW >= 0.01) ? rawW : 0.040;
    const double plateH = (rawH >= 0.01) ? rawH : 0.040;
    const double glassW = plateW + 0.010;
    const double glassH = plateH + 0.010;
    const double glassThick = 0.002;

    const math::Vec3d gFrontLoc[4] = {
        {-glassW * 0.5, -glassH * 0.5, 0.0},
        {glassW * 0.5, -glassH * 0.5, 0.0},
        {glassW * 0.5, glassH * 0.5, 0.0},
        {-glassW * 0.5, glassH * 0.5, 0.0},
    };
    const math::Vec3d gBackLoc[4] = {
        {-glassW * 0.5, -glassH * 0.5, glassThick},
        {glassW * 0.5, -glassH * 0.5, glassThick},
        {glassW * 0.5, glassH * 0.5, glassThick},
        {-glassW * 0.5, glassH * 0.5, glassThick},
    };
    const math::Vec3d emuLoc[4] = {
        {-plateW * 0.5, -plateH * 0.5, 0.0},
        {plateW * 0.5, -plateH * 0.5, 0.0},
        {plateW * 0.5, plateH * 0.5, 0.0},
        {-plateW * 0.5, plateH * 0.5, 0.0},
    };

    glm::vec3 F[4], B[4], E[4];
    for (int i = 0; i < 4; ++i) {
        F[i] = toGlm(math::transformPointLocalToWorld(view_.platePose, gFrontLoc[i]));
        B[i] = toGlm(math::transformPointLocalToWorld(view_.platePose, gBackLoc[i]));
        E[i] = toGlm(math::transformPointLocalToWorld(view_.platePose, emuLoc[i]));
    }

    // Reference lamp 3D position
    const bool isReflection = asset_->material.geometry == optics::holography::VolumeHologramGeometry::Reflection;
    const double lampZ = isReflection ? 0.14 : -0.14;
    math::Vec3d lampWorld{};
    if (mode_ == ShowroomMode::OrbitParallax) {
        const math::Vec3d lampLoc{0.0, glassH * 0.5 + 0.08, lampZ};
        lampWorld = math::transformPointLocalToWorld(view_.platePose, lampLoc);
    } else {
        const double radPitch = static_cast<double>(lightPitchDeg_) * (std::numbers::pi / 180.0);
        const double radYaw = static_cast<double>(lightYawDeg_) * (std::numbers::pi / 180.0);
        lampWorld = {-std::sin(radYaw) * 0.15, glassH * 0.5 + 0.08, lampZ * std::sin(radPitch) / 0.7071};
    }
    const glm::vec3 lampPos = toGlm(lampWorld);

    // 1. Solid / Mesh Geometry
    std::vector<ShowroomColorVertex> solidTris;
    solidTris.reserve(512);

    auto addQuad = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec3 &p2, const glm::vec3 &p3, const glm::vec4 &c) {
        solidTris.push_back({p0, c});
        solidTris.push_back({p1, c});
        solidTris.push_back({p2, c});
        solidTris.push_back({p0, c});
        solidTris.push_back({p2, c});
        solidTris.push_back({p3, c});
    };

    // Glass back face
    const glm::vec4 glassBackCol{0.08F, 0.16F, 0.22F, 0.40F};
    addQuad(B[0], B[1], B[2], B[3], glassBackCol);

    // Glass side facets
    const glm::vec4 glassEdgeCol{0.12F, 0.38F, 0.48F, 0.70F};
    addQuad(F[3], F[0], B[0], B[3], glassEdgeCol); // Left
    addQuad(F[1], F[2], B[2], B[1], glassEdgeCol); // Right
    addQuad(F[2], F[3], B[3], B[2], glassEdgeCol); // Top
    addQuad(F[0], F[1], B[1], B[0], glassEdgeCol); // Bottom

    // Glass front face (subtle sheen)
    const glm::vec4 glassFrontCol{0.18F, 0.38F, 0.48F, 0.22F};
    addQuad(F[0], F[1], F[2], F[3], glassFrontCol);

    // Emulsion quad is rendered via texVerts and textureShader_ below

    // Bottom mounting clips
    const glm::vec4 clipCol{0.22F, 0.28F, 0.36F, 0.95F};
    const glm::vec3 clipOffset1 = (F[1] - F[0]) * 0.25F;
    const glm::vec3 clipOffset2 = (F[1] - F[0]) * 0.75F;
    const glm::vec3 clipW = (F[1] - F[0]) * 0.06F;
    const glm::vec3 clipH = (F[3] - F[0]) * 0.08F;
    addQuad(F[0] + clipOffset1 - clipW, F[0] + clipOffset1 + clipW,
            F[0] + clipOffset1 + clipW + clipH, F[0] + clipOffset1 - clipW + clipH, clipCol);
    addQuad(F[0] + clipOffset2 - clipW, F[0] + clipOffset2 + clipW,
            F[0] + clipOffset2 + clipW + clipH, F[0] + clipOffset2 - clipW + clipH, clipCol);

    // 3D Reference Spotlight Fixture
    const glm::vec3 plateCenter = toGlm(view_.platePose.translationMetres);
    glm::vec3 beamDir = glm::normalize(plateCenter - lampPos);
    glm::vec3 beamRight = glm::cross(beamDir, glm::vec3(0.0F, 1.0F, 0.0F));
    if (glm::length(beamRight) < 0.001F) {
        beamRight = glm::vec3(1.0F, 0.0F, 0.0F);
    } else {
        beamRight = glm::normalize(beamRight);
    }
    const glm::vec3 beamUp = glm::cross(beamRight, beamDir);

    const float spotRadius = 0.012F;
    const float spotLength = 0.022F;
    const glm::vec3 spotFront = lampPos;
    const glm::vec3 spotBack = lampPos - beamDir * spotLength;
    constexpr int kSegments = 16;
    constexpr float kTwoPi = 2.0F * std::numbers::pi_v<float>;

    const glm::vec4 housingCol{0.18F, 0.24F, 0.32F, 0.95F};
    const glm::vec4 backCapCol{0.12F, 0.16F, 0.22F, 0.95F};
    const glm::vec4 rimCol{0.75F, 0.70F, 0.50F, 0.95F};
    const glm::vec4 emitterCol{1.00F, 0.97F, 0.86F, 1.00F};

    for (int k = 0; k < kSegments; ++k) {
        const float a0 = static_cast<float>(k) * kTwoPi / static_cast<float>(kSegments);
        const float a1 = static_cast<float>(k + 1) * kTwoPi / static_cast<float>(kSegments);
        const glm::vec3 r0 = (beamRight * std::cos(a0) + beamUp * std::sin(a0)) * spotRadius;
        const glm::vec3 r1 = (beamRight * std::cos(a1) + beamUp * std::sin(a1)) * spotRadius;

        // Housing cylinder
        solidTris.push_back({spotFront + r0, housingCol});
        solidTris.push_back({spotFront + r1, housingCol});
        solidTris.push_back({spotBack + r1, housingCol});
        solidTris.push_back({spotFront + r0, housingCol});
        solidTris.push_back({spotBack + r1, housingCol});
        solidTris.push_back({spotBack + r0, housingCol});

        // Back cap
        solidTris.push_back({spotBack, backCapCol});
        solidTris.push_back({spotBack + r1, backCapCol});
        solidTris.push_back({spotBack + r0, backCapCol});

        // Front lens rim
        const glm::vec3 inner0 = (beamRight * std::cos(a0) + beamUp * std::sin(a0)) * (spotRadius * 0.75F);
        const glm::vec3 inner1 = (beamRight * std::cos(a1) + beamUp * std::sin(a1)) * (spotRadius * 0.75F);
        solidTris.push_back({spotFront + inner0, rimCol});
        solidTris.push_back({spotFront + inner1, rimCol});
        solidTris.push_back({spotFront + r1, rimCol});
        solidTris.push_back({spotFront + inner0, rimCol});
        solidTris.push_back({spotFront + r1, rimCol});
        solidTris.push_back({spotFront + r0, rimCol});

        // Front emitter disk
        solidTris.push_back({spotFront, emitterCol});
        solidTris.push_back({spotFront + inner0, emitterCol});
        solidTris.push_back({spotFront + inner1, emitterCol});
    }

    // Draw solid triangles
    colorShader_.use();
    colorShader_.setMat4("uViewProjection", viewProj);
    glBindVertexArray(colorVao_);
    glBindBuffer(GL_ARRAY_BUFFER, colorVbo_);
    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(solidTris.size() * sizeof(ShowroomColorVertex)),
                 solidTris.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(solidTris.size()));

    // 2. Wireframe Edge Lines & Highlights
    std::vector<ShowroomColorVertex> solidLines;
    solidLines.reserve(128);

    auto addLine = [&](const glm::vec3 &p0, const glm::vec3 &p1, const glm::vec4 &c) {
        solidLines.push_back({p0, c});
        solidLines.push_back({p1, c});
    };

    const glm::vec4 edgeLineCol{0.35F, 0.85F, 0.95F, 0.85F};
    for (int i = 0; i < 4; ++i) {
        addLine(F[i], F[(i + 1) % 4], edgeLineCol);
        addLine(B[i], B[(i + 1) % 4], edgeLineCol * 0.6F);
        addLine(F[i], B[i], edgeLineCol * 0.8F);
    }
    // Top bevel highlight from reference light
    const glm::vec4 topHighlight{1.0F, 0.96F, 0.85F, 0.95F};
    addLine(F[3], F[2], topHighlight);

    // Emulsion active boundary
    const glm::vec4 emuBorderCol{0.35F, 0.90F, 0.55F, 0.90F};
    for (int i = 0; i < 4; ++i) {
        addLine(E[i], E[(i + 1) % 4], emuBorderCol);
    }

    // Plate Normal Indicator (+Z)
    const glm::vec3 normalDir = toGlm(-view_.platePose.localZAxisInWorld);
    const glm::vec3 normalTip = plateCenter + normalDir * 0.025F;
    addLine(plateCenter, normalTip, glm::vec4(0.0F, 0.95F, 1.0F, 0.95F));

    // Corner Coordinate Axes
    const glm::vec3 axO = F[0];
    const glm::vec3 axX = axO + toGlm(view_.platePose.localXAxisInWorld) * 0.015F;
    const glm::vec3 axY = axO + toGlm(view_.platePose.localYAxisInWorld) * 0.015F;
    const glm::vec3 axZ = axO + normalDir * 0.015F;
    addLine(axO, axX, glm::vec4(1.0F, 0.35F, 0.35F, 0.95F)); // X (Red)
    addLine(axO, axY, glm::vec4(0.35F, 1.0F, 0.45F, 0.95F)); // Y (Green)
    addLine(axO, axZ, glm::vec4(0.25F, 0.70F, 1.0F, 0.95F)); // Z (Blue)

    glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(solidLines.size() * sizeof(ShowroomColorVertex)),
                 solidLines.data(), GL_DYNAMIC_DRAW);
    glDrawArrays(GL_LINES, 0, static_cast<GLsizei>(solidLines.size()));

    // 3. Reconstructed Hologram Textured Quad (Local Bragg Diffraction Reflection on Plate)
    if (!fringeTexturesReady_) {
        uploadFringeTextures();
    }

    if (fringeTexturesReady_ || (textureCurrent_ && texture_.isValid())) {
        const std::array<ShowroomTextureVertex, 6> texVerts{{
            {E[0], {0.0F, 0.0F}},
            {E[1], {1.0F, 0.0F}},
            {E[2], {1.0F, 1.0F}},
            {E[0], {0.0F, 0.0F}},
            {E[2], {1.0F, 1.0F}},
            {E[3], {0.0F, 1.0F}},
        }};

        textureShader_.use();
        textureShader_.setMat4("uViewProjection", viewProj);
        textureShader_.setVec3("uEyePos", eyePos);
        textureShader_.setVec3("uLampPos", lampPos);
        textureShader_.setVec3("uPlateCenter", toGlm(view_.platePose.translationMetres));
        textureShader_.setVec3("uPlateX", toGlm(view_.platePose.localXAxisInWorld));
        textureShader_.setVec3("uPlateY", toGlm(view_.platePose.localYAxisInWorld));
        textureShader_.setVec3("uPlateZ", toGlm(view_.platePose.localZAxisInWorld));

        const double wMetres = static_cast<double>(asset_->coupling.width()) * asset_->coupling.pitchXMetres();
        const double hMetres = static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres();

        textureShader_.setFloat("uPlateWidth", static_cast<float>(wMetres));
        textureShader_.setFloat("uPlateHeight", static_cast<float>(hMetres));
        textureShader_.setInt("uShowFringes", showFringes_ ? 1 : 0);

        const auto &mat = asset_->material;
        textureShader_.setInt("uGeometry", isReflection ? 1 : 0);
        textureShader_.setFloat("uRefractiveIndex", static_cast<float>(mat.averageRefractiveIndex));
        textureShader_.setFloat("uIndexModulation", static_cast<float>(mat.refractiveIndexModulation));
        textureShader_.setFloat("uThickness", static_cast<float>(mat.recordedThicknessMetres));
        textureShader_.setFloat("uExposure", displayExposure_);
        textureShader_.setFloat("uLampLit", view_.irradianceWattsPerSquareMetre > 0.0 ? 1.0F : 0.0F);

        const bool isRgb = (channelIndex_ == -1 && channels_.size() == 3U);
        textureShader_.setInt("uIsRgb", isRgb ? 1 : 0);

        if (isRgb) {
            const glm::vec3 channelColors[3] = {
                {1.0F, 0.06F, 0.02F},  // Red (638 nm)
                {0.05F, 1.0F, 0.12F},  // Green (532 nm)
                {0.04F, 0.25F, 1.0F}   // Blue (450 nm)
            };
            for (int c = 0; c < 3; ++c) {
                const std::string idxStr = std::to_string(c);
                textureShader_.setFloat(("uWavelength[" + idxStr + "]").c_str(),
                    static_cast<float>(channels_[static_cast<std::size_t>(c)].material.recordingVacuumWavelengthMetres));
                textureShader_.setVec3(("uGratingK0[" + idxStr + "]").c_str(),
                    toGlm(channels_[static_cast<std::size_t>(c)].gratingVector));
                textureShader_.setVec3(("uChannelColor[" + idxStr + "]").c_str(),
                    channelColors[c]);
            }
        } else {
            textureShader_.setFloat("uWavelength[0]", static_cast<float>(view_.wavelengthMetres));
            textureShader_.setVec3("uGratingK0[0]", toGlm(asset_->gratingVector));

            const double nm = view_.wavelengthMetres * 1e9;
            glm::vec3 col{0.0F, 1.0F, 0.2F};
            if (nm > 610.0) col = {1.0F, 0.06F, 0.02F};
            else if (nm < 490.0) col = {0.04F, 0.25F, 1.0F};
            textureShader_.setVec3("uChannelColor[0]", col);
        }


        if (textureCurrent_ && texture_.isValid()) {
            texture_.bind(0);
            textureShader_.setInt("uReconTex", 0);
        }

        if (fringeTexturesReady_) {
            for (std::size_t c = 0; c < 3U; ++c) {
                fringeTextures_[c].bind(static_cast<GLuint>(1 + c));
                textureShader_.setInt(("uFringeTex" + std::to_string(c)).c_str(), static_cast<int>(1 + c));
            }
        }

        glDisable(GL_DEPTH_TEST);
        glBindVertexArray(textureVao_);
        glBindBuffer(GL_ARRAY_BUFFER, textureVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(texVerts.size() * sizeof(ShowroomTextureVertex)),
                     texVerts.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, 6);
        glEnable(GL_DEPTH_TEST);
        SDL_Log("GL error after draw: %u", glGetError());
    }

    // 4. Volumetric Conical Light Beam (Additive Blend)
    {
        glBlendFunc(GL_SRC_ALPHA, GL_ONE);
        glDepthMask(GL_FALSE);

        std::vector<ShowroomColorVertex> coneTris;
        coneTris.reserve(24);
        const glm::vec4 beamCol{1.0F, 0.94F, 0.75F, 0.09F};

        coneTris.push_back({spotFront, beamCol});
        coneTris.push_back({F[3], beamCol});
        coneTris.push_back({F[2], beamCol});

        coneTris.push_back({spotFront, beamCol});
        coneTris.push_back({F[0], beamCol});
        coneTris.push_back({F[1], beamCol});

        coneTris.push_back({spotFront, beamCol});
        coneTris.push_back({F[3], beamCol});
        coneTris.push_back({F[0], beamCol});

        coneTris.push_back({spotFront, beamCol});
        coneTris.push_back({F[2], beamCol});
        coneTris.push_back({F[1], beamCol});

        colorShader_.use();
        colorShader_.setMat4("uViewProjection", viewProj);
        glBindVertexArray(colorVao_);
        glBindBuffer(GL_ARRAY_BUFFER, colorVbo_);
        glBufferData(GL_ARRAY_BUFFER, static_cast<GLsizeiptr>(coneTris.size() * sizeof(ShowroomColorVertex)),
                     coneTris.data(), GL_DYNAMIC_DRAW);
        glDrawArrays(GL_TRIANGLES, 0, static_cast<GLsizei>(coneTris.size()));

        glDepthMask(GL_TRUE);
        glBlendFunc(GL_SRC_ALPHA, GL_ONE_MINUS_SRC_ALPHA);
    }

    glBindVertexArray(0);
    glBindBuffer(GL_ARRAY_BUFFER, 0);
    fbo_.unbind();
}

void HologramShowroom::uploadFringeTextures() {
    if (!asset_) {
        return;
    }

    std::vector<const optics::holography::RecordedHologram *> channelList;
    if (channels_.size() == 3U && channelIndex_ == -1) {
        channelList = {&channels_[0], &channels_[1], &channels_[2]};
    } else {
        channelList = {asset_.get()};
    }

    for (std::size_t c = 0; c < channelList.size() && c < 3U; ++c) {
        const auto &h = *channelList[c];
        const auto &f = h.coupling;
        const int w = static_cast<int>(f.width());
        const int hgt = static_cast<int>(f.height());
        if (w <= 0 || hgt <= 0) {
            continue;
        }

        const auto gratingField = optics::holography::computeLocalVolumeGratingField(h);
        const auto totalSamples = static_cast<std::size_t>(w) * static_cast<std::size_t>(hgt);
        std::vector<float> data(totalSamples * 4U);

        for (std::size_t i = 0; i < totalSamples; ++i) {
            const auto &sample = gratingField[i];
            data[i * 4U + 0] = sample.amplitude;
            data[i * 4U + 1] = sample.Kx;
            data[i * 4U + 2] = sample.Ky;
            data[i * 4U + 3] = sample.Kz;
        }

        static_cast<void>(fringeTextures_[c].uploadRgbaF32(w, hgt, data));
    }
    fringeTexturesReady_ = true;
}

void HologramShowroom::showEmpty() {
    worker_.cancel();
    ++requestId_;
    asset_.reset(); channels_.clear(); result_.reset();
    textureCurrent_ = false;
    fringeTexturesReady_ = false;
    status_ = "Record the selected plate or open a saved recording";
    active_ = true;
}
void HologramShowroom::open(h::RecordedHologram asset) {
    channels_.clear();
    auto candidate = std::make_shared<const h::RecordedHologram>(std::move(asset));
    auto initial = h::defaultHologramView(*candidate);
    cameraDistance_ = 0.12; // 12 cm default observation distance for clear 4 cm plate view
    initial.sensorDistanceMetres = cameraDistance_;
    initial.focusDistanceMetres = cameraDistance_ + candidate->suggestedFocusDepthMetres;
    asset_ = std::move(candidate);
    view_ = initial_ = initial;
    referenceIntensity_ = 0.0;
    displayExposure_ = 3.0F;
    yaw_ = pitch_ = 0.0F;
    panX_ = panY_ = 0.0;
    mode_ = ShowroomMode::OrbitParallax;
    lightPitchDeg_ = 45.0F;
    lightYawDeg_ = 0.0F;
    active_ = true;
    fringeTexturesReady_ = false;
    request();
}
void HologramShowroom::open(std::vector<h::RecordedHologram> channels) {
    if (channels.empty()) throw std::invalid_argument("No recorded wavelength channels");
    if (channels.size() > 3U) throw std::invalid_argument("At most three recorded wavelength channels are supported");
    for (const auto& channel : channels) h::validateRecordedHologram(channel);
    open(channels.front());
    channels_ = std::move(channels);
    if (channels_.size() == 3U) {
        channelIndex_ = -1; // Default to White light (RGB) reconstruction
        fringeTexturesReady_ = false;
        request();
    } else {
        channelIndex_ = 0;
    }
}
void HologramShowroom::request() {
    if (!asset_)
        return;
    const auto rawZ = rotate(initial_.platePose.localZAxisInWorld, yaw_, pitch_);
    const auto rawY = rotate(initial_.platePose.localYAxisInWorld, yaw_, pitch_);
    const auto z = math::normalized(rawZ);
    const auto x = math::normalized(math::cross(rawY, z));
    const auto y = math::cross(z, x);
    view_.platePose.localXAxisInWorld = x;
    view_.platePose.localYAxisInWorld = y;
    view_.platePose.localZAxisInWorld = z;

    const bool isReflection = asset_->material.geometry == optics::holography::VolumeHologramGeometry::Reflection;
    const double lampZ = isReflection ? 0.14 : -0.14;
    const double glassH = static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres();

    if (mode_ == ShowroomMode::OrbitParallax) {
        // Orbit & Parallax mode: reference lamp rotates with plate as a rigid body.
        const math::Vec3d lampLoc{0.0, glassH * 0.5 + 0.08, lampZ};
        const math::Vec3d lampDirLocal = math::normalized(-lampLoc);
        view_.illuminationDirection = math::transformDirectionLocalToWorld(
            view_.platePose, lampDirLocal);
    } else {
        // Bragg Tuning mode: light direction is defined in world coordinates from overhead.
        const double radPitch = static_cast<double>(lightPitchDeg_) * (std::numbers::pi / 180.0);
        const double radYaw = static_cast<double>(lightYawDeg_) * (std::numbers::pi / 180.0);
        view_.illuminationDirection = math::normalized(math::Vec3d{
            std::sin(radYaw),
            -std::cos(radPitch),
            (isReflection ? 1.0 : -1.0) * std::sin(radPitch) * std::cos(radYaw)
        });
    }

    view_.eyePosition.x = 0.0;
    view_.eyePosition.y = 0.0;
    view_.eyePosition.z = cameraDistance_;
    view_.sensorDistanceMetres = cameraDistance_;
    view_.focusDistanceMetres = cameraDistance_ + asset_->suggestedFocusDepthMetres;

    status_ = "Computing current view...";

    if (channelIndex_ == -1 && channels_.size() == 3U) {
        worker_.submitMultiChannel(++requestId_, channels_, view_);
    } else {
        worker_.submit(++requestId_, asset_, view_);
    }
}
void HologramShowroom::upload() {
    if (channelIndex_ == -1 && channelResults_.size() == 3U) {
        const auto &fR = channelResults_[0].sensorField;
        const auto &fG = channelResults_[1].sensorField;
        const auto &fB = channelResults_[2].sensorField;
        const std::size_t count = fR.sampleCount();
        if (referenceIntensity_ <= 0.0) {
            for (std::size_t i = 0; i < count; ++i) {
                referenceIntensity_ = std::max(referenceIntensity_, std::norm(fR.samples()[i]));
                referenceIntensity_ = std::max(referenceIntensity_, std::norm(fG.samples()[i]));
                referenceIntensity_ = std::max(referenceIntensity_, std::norm(fB.samples()[i]));
            }
        }
        const double reference = std::max(referenceIntensity_, 1e-20);
        SDL_Log("[ShowroomUpload] RGB count=%zu, referenceIntensity_=%e, exposure=%f", count, referenceIntensity_, displayExposure_);
        std::vector<std::uint8_t> pixels(count * 4U, 255U);
        for (std::size_t i = 0; i < count; ++i) {
            const double nR = std::norm(fR.samples()[i]);
            const double nG = std::norm(fG.samples()[i]);
            const double nB = std::norm(fB.samples()[i]);

            const double valR = (std::isfinite(nR) && nR > 0.0)
                ? std::pow(std::clamp(nR * static_cast<double>(displayExposure_) / reference, 0.0, 1.0), 1.0 / 2.2)
                : 0.0;
            const double valG = (std::isfinite(nG) && nG > 0.0)
                ? std::pow(std::clamp(nG * static_cast<double>(displayExposure_) / reference, 0.0, 1.0), 1.0 / 2.2)
                : 0.0;
            const double valB = (std::isfinite(nB) && nB > 0.0)
                ? std::pow(std::clamp(nB * static_cast<double>(displayExposure_) / reference, 0.0, 1.0), 1.0 / 2.2)
                : 0.0;

            pixels[i * 4U]      = static_cast<std::uint8_t>(std::clamp(std::lround(255.0 * valR), 0L, 255L));
            pixels[i * 4U + 1U] = static_cast<std::uint8_t>(std::clamp(std::lround(255.0 * valG), 0L, 255L));
            pixels[i * 4U + 2U] = static_cast<std::uint8_t>(std::clamp(std::lround(255.0 * valB), 0L, 255L));
        }
        textureCurrent_ =
            texture_.uploadRgba8(static_cast<int>(fR.width()), static_cast<int>(fR.height()), pixels);
        if (!textureCurrent_)
            status_ = "Could not upload observer RGB image";
        return;
    }
    if (!result_)
        return;
    const auto &f = result_->sensorField;
    if (referenceIntensity_ <= 0.0) {
        for (const auto &s : f.samples()) {
            const double norm = std::norm(s);
            if (std::isfinite(norm))
                referenceIntensity_ = std::max(referenceIntensity_, norm);
        }
    }
    const double reference = std::max(referenceIntensity_, 1e-20);
    const double nm = view_.wavelengthMetres * 1e9;
    double rFactor = 1.0, gFactor = 1.0, bFactor = 1.0;
    if (nm >= 600.0) {
        rFactor = 1.0; gFactor = 0.22; bFactor = 0.22;
    } else if (nm >= 500.0) {
        rFactor = 0.22; gFactor = 1.0; bFactor = 0.22;
    } else if (nm > 0.0) {
        rFactor = 0.22; gFactor = 0.32; bFactor = 1.0;
    }
    std::vector<std::uint8_t> pixels(f.sampleCount() * 4U, 255U);
    for (std::size_t i = 0; i < f.sampleCount(); ++i) {
        const double norm = std::norm(f.samples()[i]);
        const double normalized = (std::isfinite(norm) && norm > 0.0)
            ? (norm * static_cast<double>(displayExposure_) / reference)
            : 0.0;
        const double clamped = std::clamp(normalized, 0.0, 1.0);
        const double value = std::isfinite(clamped) ? std::pow(clamped, 1.0 / 2.2) : 0.0;
        const auto byte = static_cast<std::uint8_t>(std::clamp(std::lround(255.0 * value), 0L, 255L));
        pixels[i * 4U]      = static_cast<std::uint8_t>(std::clamp(std::lround(byte * rFactor), 0L, 255L));
        pixels[i * 4U + 1U] = static_cast<std::uint8_t>(std::clamp(std::lround(byte * gFactor), 0L, 255L));
        pixels[i * 4U + 2U] = static_cast<std::uint8_t>(std::clamp(std::lround(byte * bFactor), 0L, 255L));
    }
    textureCurrent_ =
        texture_.uploadRgba8(static_cast<int>(f.width()), static_cast<int>(f.height()), pixels);
    if (!textureCurrent_)
        status_ = "Could not upload observer image";
}
bool HologramShowroom::draw() {
    if (!active_)
        return false;
    const auto *viewport = ImGui::GetMainViewport();
    ImGui::SetNextWindowPos(viewport->WorkPos);
    ImGui::SetNextWindowSize(viewport->WorkSize);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 1));
    ImGui::Begin("Hologram showroom", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoDocking |
                     ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoSavedSettings);
    if (ImGui::Button("Back to Bench")) {
        active_ = false;
        worker_.cancel();
    }
    backButtonCentre = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                        (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
    ImGui::SameLine();
    ImGui::TextUnformatted("HOLOGRAM SHOWROOM  |  Monochromatic scalar reconstruction");
    if (ImGui::Button("Recorded plate file"))
        ImGui::OpenPopup("Showroom file");
    ImGui::SameLine();
    if (ImGui::Button("Two-depth reference")) {
        open(h::makeTwoPointReflectionReference());
        view_.focusDistanceMetres = 0.14;
        initial_ = view_;
        request();
    }
    referenceButtonCentre = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                             (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
    if (ImGui::BeginPopup("Showroom file")) {
        ImGui::InputText("Path", path_, sizeof(path_));
        if (ImGui::Button("Open")) {
            try {
                open(h::loadRecordedHolograms(path_));
            } catch (const std::exception &e) {
                status_ = e.what();
            }
        }
        ImGui::SameLine();
        ImGui::BeginDisabled(!asset_);
        if (ImGui::Button("Save recording")) {
            try {
                if (channels_.empty()) h::saveRecordedHologram(*asset_, path_);
                else h::saveRecordedHolograms(channels_, path_);
                status_ = "Detached recording saved";
            } catch (const std::exception &e) {
                status_ = e.what();
            }
        }
        ImGui::EndDisabled();
        ImGui::EndPopup();
    }
    bool changed = false;
    if (channels_.size() > 1U) {
        ImGui::TextUnformatted("Reconstruction:");
        if (channels_.size() == 3U) {
            ImGui::SameLine();
            if (ImGui::RadioButton("White light (RGB)", channelIndex_ == -1)) {
                channelIndex_ = -1;
                asset_ = std::make_shared<h::RecordedHologram>(channels_[0]);
                fringeTexturesReady_ = false;
                changed = true;
            }
            rgbButtonCentre = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                               (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
        }
        for (std::size_t i = 0; i < channels_.size(); ++i) {
            ImGui::SameLine();
            const auto label = std::to_string(static_cast<int>(std::lround(
                channels_[i].material.recordingVacuumWavelengthMetres * 1e9))) + " nm";
            if (ImGui::RadioButton(label.c_str(), channelIndex_ == static_cast<int>(i))) {
                channelIndex_ = static_cast<int>(i);
                asset_ = std::make_shared<h::RecordedHologram>(channels_[i]);
                view_.wavelengthMetres = channels_[i].material.recordingVacuumWavelengthMetres;
                fringeTexturesReady_ = false;
                changed = true;
            }
            channelButtonCentres[i] = {(ImGui::GetItemRectMin().x + ImGui::GetItemRectMax().x) * 0.5F,
                (ImGui::GetItemRectMin().y + ImGui::GetItemRectMax().y) * 0.5F};
        }
    }
    if (asset_) {
        ImGui::SameLine();
        ImGui::TextUnformatted("| View:");
        ImGui::SameLine();
        if (ImGui::RadioButton("3D Hologram", !showFringes_)) {
            showFringes_ = false;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Diffraction Fringes", showFringes_)) {
            showFringes_ = true;
        }
        ImGui::SameLine();
        ImGui::TextUnformatted("| Mode:");
        ImGui::SameLine();
        if (ImGui::RadioButton("3D Orbit & Parallax", mode_ == ShowroomMode::OrbitParallax)) {
            mode_ = ShowroomMode::OrbitParallax;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::RadioButton("Bragg Tuning", mode_ == ShowroomMode::BraggTuning)) {
            mode_ = ShowroomMode::BraggTuning;
            changed = true;
        }
        if (mode_ == ShowroomMode::BraggTuning) {
            ImGui::SameLine();
            ImGui::SetNextItemWidth(110.0F);
            if (ImGui::SliderFloat("Light Pitch", &lightPitchDeg_, 10.0F, 85.0F, "%.1f deg")) {
                changed = true;
            }
            ImGui::SameLine();
            if (ImGui::Button("Auto Align")) {
                yaw_ = pitch_ = 0.0F;
                lightPitchDeg_ = 45.0F;
                lightYawDeg_ = 0.0F;
                changed = true;
            }
        }
        ImGui::SameLine();
        bool lit = view_.irradianceWattsPerSquareMetre > 0.0;
        if (ImGui::Checkbox("Light", &lit)) {
            view_.irradianceWattsPerSquareMetre = lit ? 1.0 : 0.0;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Reset pose / light")) {
            view_ = initial_;
            yaw_ = pitch_ = 0.0F;
            panX_ = panY_ = 0.0;
            cameraDistance_ = 0.12;
            lightPitchDeg_ = 45.0F;
            lightYawDeg_ = 0.0F;
            changed = true;
        }
        ImGui::SameLine();
        if (ImGui::Button("Observation settings"))
            ImGui::OpenPopup("Observation settings");
        if (ImGui::BeginPopup("Observation settings")) {
            changed |= ImGui::SliderFloat("Plate yaw (rad)", &yaw_, -0.25F, 0.25F, "%.4f");
            changed |= ImGui::SliderFloat("Plate pitch (rad)", &pitch_, -0.25F, 0.25F, "%.4f");
            const double small = 0.00001;
            changed |=
                ImGui::InputDouble("Eye X (m)", &view_.eyePosition.x, small, small * 10, "%.6f");
            changed |=
                ImGui::InputDouble("Eye Y (m)", &view_.eyePosition.y, small, small * 10, "%.6f");
            changed |=
                ImGui::InputDouble("Eye distance (m)", &view_.eyePosition.z, 0.005, 0.01, "%.4f");
            changed |= ImGui::InputDouble("Focus distance (m)", &view_.focusDistanceMetres, 0.005,
                                          0.01, "%.4f");
            changed |= ImGui::InputDouble("Pupil radius (m)", &view_.pupilRadiusMetres, small,
                                          small * 10, "%.6f");
            double nm = view_.wavelengthMetres * 1e9;
            if (ImGui::InputDouble("Wavelength (nm)", &nm, 0.1, 1.0, "%.2f")) {
                view_.wavelengthMetres = nm * 1e-9;
                changed = true;
            }
            if (ImGui::SliderFloat("Display exposure (locked scale)", &displayExposure_, 0.01F,
                                   100.0F, "%.2f", ImGuiSliderFlags_Logarithmic))
                upload();
            ImGui::TextWrapped(
                "Finite sampled window; equivalent-symmetric TE grating; paraxial focused camera. "
                "Supports White Light (RGB) synthesis and monochromatic laser reconstruction.");
            ImGui::EndPopup();
        }
    }
    if (asset_)
        ImGui::TextDisabled("%s | %s | recorded window %.3f x %.3f mm | %zu x %zu samples",
            asset_->sourcePlateId.c_str(),
            channelIndex_ == -1 ? "White light (RGB)" : (std::to_string(static_cast<int>(std::lround(view_.wavelengthMetres * 1e9))) + " nm").c_str(),
            static_cast<double>(asset_->coupling.width()) * asset_->coupling.pitchXMetres() * 1e3,
            static_cast<double>(asset_->coupling.height()) * asset_->coupling.pitchYMetres() * 1e3,
            asset_->coupling.width(), asset_->coupling.height());
    const auto available = ImGui::GetContentRegionAvail();
    const ImVec2 origin = ImGui::GetCursorScreenPos();
    const ImVec2 size{std::max(1.0F, available.x), std::max(1.0F, available.y - 78.0F)};
    canvasCentre = {origin.x + size.x * 0.5F, origin.y + size.y * 0.5F};

    // 1. Render genuine 3D OpenGL scene into off-screen FBO
    const int vpW = std::max(1, static_cast<int>(size.x));
    const int vpH = std::max(1, static_cast<int>(size.y));
    renderScene3D(vpW, vpH);

    // 2. Display FBO texture in ImGui
    ImGui::SetCursorScreenPos(origin);
    if (fbo_.isValid()) {
        ImGui::Image(
            toImTextureID(fbo_.colorTextureId()),
            size,
            ImVec2(0.0F, 1.0F),
            ImVec2(1.0F, 0.0F));
    }

    // 3. Invisible interactive button overlaid on top to capture mouse input
    ImGui::SetCursorScreenPos(origin);
    ImGui::InvisibleButton("Showroom view", size,
                           ImGuiButtonFlags_MouseButtonLeft | ImGuiButtonFlags_MouseButtonRight);
    if (asset_ && ImGui::IsItemActive()) {
        const auto delta = ImGui::GetIO().MouseDelta;
        if (ImGui::IsMouseDown(ImGuiMouseButton_Left) && (delta.x != 0 || delta.y != 0)) {
            yaw_ += delta.x * 0.005F;
            pitch_ += delta.y * 0.005F;
            constexpr float tau = 2.0F * std::numbers::pi_v<float>;
            while (yaw_ > std::numbers::pi_v<float>) yaw_ -= tau;
            while (yaw_ < -std::numbers::pi_v<float>) yaw_ -= tau;
            while (pitch_ > std::numbers::pi_v<float>) pitch_ -= tau;
            while (pitch_ < -std::numbers::pi_v<float>) pitch_ -= tau;
        } else if (ImGui::IsMouseDown(ImGuiMouseButton_Right) && (delta.x != 0 || delta.y != 0)) {
            panX_ -= static_cast<double>(delta.x) * 0.0003;
            panY_ += static_cast<double>(delta.y) * 0.0003;
        }
    }
    if (asset_ && ImGui::IsItemDeactivated()) {
        changed = true;
    }
    if (asset_ && ImGui::IsItemHovered() && ImGui::GetIO().MouseWheel != 0.0F) {
        cameraDistance_ = std::clamp(
            cameraDistance_ * std::exp(-0.08 * ImGui::GetIO().MouseWheel), 0.04, 0.80);
    }
    if (asset_ && ImGui::IsItemHovered() && ImGui::IsMouseDoubleClicked(ImGuiMouseButton_Left)) {
        yaw_ = 0.0F;
        pitch_ = 0.0F;
        panX_ = 0.0;
        panY_ = 0.0;
        cameraDistance_ = 0.12;
        lightPitchDeg_ = 45.0F;
        lightYawDeg_ = 0.0F;
        changed = true;
    }
    if (changed)
        request();
    if (auto update = worker_.poll(); update && update->requestId == requestId_) {
        if (!update->error.empty()) {
            status_ = update->error;
        } else {
            result_ = std::move(update->result);
            channelResults_ = std::move(update->channelResults);
            status_ = update->finalStage ? (channelIndex_ == -1 ? "Current / White-light RGB reconstruction" : "Current / finite-window scalar result")
                                         : "Preview / refining pupil window";
            if (result_ && result_->boundaryPowerFraction > 0.01)
                status_ += " | boundary energy: enlarge recording window for convergence";
            upload();
        }
    }
    auto *draw = ImGui::GetWindowDrawList();
    const ImVec2 end{origin.x + size.x, origin.y + size.y},
        centre{origin.x + size.x / 2, origin.y + size.y / 2};
    draw->PushClipRect(origin, end, true);

    // Viewport bounds with padding
    const float pad = 8.0F;
    const ImVec2 vMin = {origin.x + pad, origin.y + pad};
    const ImVec2 vMax = {origin.x + size.x - pad, origin.y + size.y - pad};




    if (asset_) {
        // Plate Attitude HUD Card with Mini 3D Attitude Dial
        const float cardW = 210.0F;
        const float cardH = 118.0F;
        const ImVec2 cardPos = {vMax.x - cardW - 12.0F, vMin.y + 12.0F};
        draw->AddRectFilled(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                            IM_COL32(14, 20, 28, 230), 6.0F);
        draw->AddRect(cardPos, {cardPos.x + cardW, cardPos.y + cardH},
                      IM_COL32(55, 78, 105, 210), 6.0F);

        const float yawDeg = yaw_ * (180.0F / std::numbers::pi_v<float>);
        const float pitchDeg = pitch_ * (180.0F / std::numbers::pi_v<float>);

        draw->AddText({cardPos.x + 10.0F, cardPos.y + 8.0F}, IM_COL32(175, 210, 240, 255),
                      "PLATE ATTITUDE");

        // Mini 3D Attitude Dial
        const ImVec2 dialCenter = {cardPos.x + 34.0F, cardPos.y + 56.0F};
        draw->AddCircleFilled(dialCenter, 22.0F, IM_COL32(10, 15, 22, 255));
        draw->AddCircle(dialCenter, 22.0F, IM_COL32(60, 85, 115, 220), 32, 1.5F);
        draw->AddCircle(dialCenter, 5.0F, IM_COL32(60, 240, 120, 140), 16, 1.0F);
        draw->AddLine({dialCenter.x - 20.0F, dialCenter.y}, {dialCenter.x + 20.0F, dialCenter.y},
                      IM_COL32(50, 70, 95, 160), 1.0F);
        draw->AddLine({dialCenter.x, dialCenter.y - 20.0F}, {dialCenter.x, dialCenter.y + 20.0F},
                      IM_COL32(50, 70, 95, 160), 1.0F);

        const float markX = dialCenter.x + std::sin(yaw_) * 18.0F;
        const float markY = dialCenter.y - std::sin(pitch_) * 18.0F;
        draw->AddLine(dialCenter, {markX, markY}, IM_COL32(0, 230, 255, 200), 1.5F);
        const float dev = std::hypot(yawDeg, pitchDeg);
        const ImU32 dotCol = (dev < 0.5F) ? IM_COL32(60, 240, 120, 255) : IM_COL32(255, 180, 50, 255);
        draw->AddCircleFilled({markX, markY}, 3.5F, dotCol);

        // Numeric Readouts
        char buf[64];
        std::snprintf(buf, sizeof(buf), "Yaw:   %+.2f deg", yawDeg);
        draw->AddText({cardPos.x + 68.0F, cardPos.y + 26.0F}, IM_COL32(210, 225, 240, 230), buf);

        std::snprintf(buf, sizeof(buf), "Pitch: %+.2f deg", pitchDeg);
        draw->AddText({cardPos.x + 68.0F, cardPos.y + 44.0F}, IM_COL32(210, 225, 240, 230), buf);

        if (result_) {
            const double eff = result_->efficiency * 100.0;
            const ImU32 effCol = (eff >= 10.0) ? IM_COL32(60, 240, 120, 255)
                                 : (eff >= 1.0) ? IM_COL32(245, 190, 50, 255)
                                                : IM_COL32(140, 150, 160, 200);
            std::snprintf(buf, sizeof(buf), "Eff:   %.1f%%", eff);
            draw->AddText({cardPos.x + 68.0F, cardPos.y + 62.0F}, effCol, buf);

            const char *badge = (eff >= 10.0) ? "[ ALIGNED ]"
                                : (eff >= 1.0) ? "[ NEAR ]"
                                               : "[ OFF-BRAGG ]";
            draw->AddText({cardPos.x + 68.0F, cardPos.y + 80.0F}, effCol, badge);
        }

        draw->AddText({cardPos.x + 10.0F, cardPos.y + 98.0F}, IM_COL32(130, 160, 190, 200),
                      "Double-click: Reset 0 deg");
    } else
        draw->AddText({vMin.x + 20.0F, vMin.y + 20.0F}, IM_COL32_WHITE,
                      "Record a reflection plate on the Bench, or open a saved .holo.json file.");

    // 8. Top-Layer Viewport Chrome (Drawn on top of canvas and textures)
    draw->AddRect(vMin, vMax, IM_COL32(95, 130, 175, 255), 6.0F, 0, 2.5F);
    draw->AddRect({vMin.x + 3.0F, vMin.y + 3.0F}, {vMax.x - 3.0F, vMax.y - 3.0F},
                  IM_COL32(35, 55, 80, 160), 4.0F, 0, 1.0F);

    // Optical viewfinder corner brackets (bright neon cyan)
    const float brLen = 22.0F;
    const ImU32 brCol = IM_COL32(0, 225, 255, 240);
    draw->AddLine({vMin.x + 6.0F, vMin.y + 6.0F}, {vMin.x + 6.0F + brLen, vMin.y + 6.0F}, brCol, 2.5F);
    draw->AddLine({vMin.x + 6.0F, vMin.y + 6.0F}, {vMin.x + 6.0F, vMin.y + 6.0F + brLen}, brCol, 2.5F);
    draw->AddLine({vMax.x - 6.0F, vMin.y + 6.0F}, {vMax.x - 6.0F - brLen, vMin.y + 6.0F}, brCol, 2.5F);
    draw->AddLine({vMax.x - 6.0F, vMin.y + 6.0F}, {vMax.x - 6.0F, vMin.y + 6.0F + brLen}, brCol, 2.5F);
    draw->AddLine({vMin.x + 6.0F, vMax.y - 6.0F}, {vMin.x + 6.0F + brLen, vMax.y - 6.0F}, brCol, 2.5F);
    draw->AddLine({vMin.x + 6.0F, vMax.y - 6.0F}, {vMin.x + 6.0F, vMax.y - 6.0F - brLen}, brCol, 2.5F);
    draw->AddLine({vMax.x - 6.0F, vMax.y - 6.0F}, {vMax.x - 6.0F - brLen, vMax.y - 6.0F}, brCol, 2.5F);
    draw->AddLine({vMax.x - 6.0F, vMax.y - 6.0F}, {vMax.x - 6.0F, vMax.y - 6.0F - brLen}, brCol, 2.5F);

    // Viewport header badge
    const float badgeW = 460.0F;
    draw->AddRectFilled({vMin.x + 12.0F, vMin.y + 10.0F}, {vMin.x + 12.0F + badgeW, vMin.y + 32.0F},
                        IM_COL32(14, 22, 32, 230), 4.0F);
    draw->AddRect({vMin.x + 12.0F, vMin.y + 10.0F}, {vMin.x + 12.0F + badgeW, vMin.y + 32.0F},
                  IM_COL32(60, 90, 120, 200), 4.0F);
    char badgeBuf[128];
    std::snprintf(badgeBuf, sizeof(badgeBuf),
                  "3D OPENGL SCENE | FOV 60° | Eye: %.1f cm | %s",
                  cameraDistance_ * 100.0,
                  mode_ == ShowroomMode::OrbitParallax ? "Orbit (Lamp Locked)" : "Bragg Tuning (Fixed Lamp)");
    draw->AddText({vMin.x + 20.0F, vMin.y + 13.0F}, IM_COL32(170, 220, 250, 255),
                  badgeBuf);

    // Floating In-Viewport Progress HUD
    const auto prog = worker_.progress();
    if (prog.busy) {
        const float hudW = 420.0F;
        const float hudH = 30.0F;
        const float hx = (vMin.x + vMax.x - hudW) * 0.5F;
        const float hy = vMax.y - 42.0F;
        draw->AddRectFilled({hx, hy}, {hx + hudW, hy + hudH}, IM_COL32(12, 20, 32, 235), 5.0F);
        draw->AddRect({hx, hy}, {hx + hudW, hy + hudH}, IM_COL32(0, 210, 255, 220), 5.0F, 0, 1.5F);
        const float fillW = std::clamp(prog.fraction, 0.0F, 1.0F) * (hudW - 8.0F);
        draw->AddRectFilled({hx + 4.0F, hy + hudH - 5.0F}, {hx + 4.0F + fillW, hy + hudH - 2.0F},
                            IM_COL32(0, 240, 255, 255), 2.0F);
        char progText[128];
        std::snprintf(progText, sizeof(progText), "RECONSTRUCTING: %.0f%% | %s",
                      prog.fraction * 100.0F, prog.stageText.c_str());
        draw->AddText({hx + 12.0F, hy + 6.0F}, IM_COL32(210, 245, 255, 255), progText);
    } else if (!prog.error.empty()) {
        const float hudW = 440.0F;
        const float hudH = 30.0F;
        const float hx = (vMin.x + vMax.x - hudW) * 0.5F;
        const float hy = vMax.y - 42.0F;
        draw->AddRectFilled({hx, hy}, {hx + hudW, hy + hudH}, IM_COL32(42, 12, 16, 235), 5.0F);
        draw->AddRect({hx, hy}, {hx + hudW, hy + hudH}, IM_COL32(255, 75, 75, 240), 5.0F, 0, 1.5F);
        char errText[128];
        std::snprintf(errText, sizeof(errText), "RECONSTRUCTION ISSUE: %s", prog.error.c_str());
        draw->AddText({hx + 12.0F, hy + 6.0F}, IM_COL32(255, 210, 210, 255), errText);
    }

    draw->PopClipRect();

    // Bottom Bar Progress & Diagnostics
    char progressOverlay[256];
    ImVec4 barColor;
    if (prog.busy) {
        std::snprintf(progressOverlay, sizeof(progressOverlay), "Reconstructing: %.0f%% - %s",
                      prog.fraction * 100.0F, prog.stageText.c_str());
        barColor = ImVec4(0.0F, 0.75F, 0.95F, 1.0F);
    } else if (!prog.error.empty()) {
        std::snprintf(progressOverlay, sizeof(progressOverlay), "Reconstruction Issue: %s",
                      prog.error.c_str());
        barColor = ImVec4(0.95F, 0.25F, 0.25F, 1.0F);
    } else {
        std::snprintf(progressOverlay, sizeof(progressOverlay), "Reconstruction Ready (100%%) - %s",
                      (channelIndex_ == -1 ? "White light RGB" : "Single wavelength"));
        barColor = ImVec4(0.20F, 0.65F, 0.35F, 1.0F);
    }
    ImGui::PushStyleColor(ImGuiCol_PlotHistogram, barColor);
    ImGui::ProgressBar(prog.fraction, ImVec2(-1.0F, 14.0F), progressOverlay);
    ImGui::PopStyleColor();

    if (mode_ == ShowroomMode::OrbitParallax) {
        ImGui::TextUnformatted("Mode: 3D Orbit & Parallax | Left drag: orbit 360° (lamp locked with plate) | Right drag: pan | Wheel: zoom | Double-click: reset");
    } else {
        ImGui::TextUnformatted("Mode: Bragg Tuning | Left drag: tilt plate (overhead lamp fixed) | Light Pitch: adjust lamp angle | Right drag: pan | Wheel: zoom");
    }
    ImGui::TextWrapped("%s", status_.c_str());
    ImGui::End();
    ImGui::PopStyleColor();
    return true;
}
} // namespace holobench::app
