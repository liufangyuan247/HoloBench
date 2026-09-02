#pragma once

#include <array>
#include <cstddef>
#include <limits>
#include <span>
#include <string_view>
#include <vector>

#include <glad/gl.h>
#include <glm/glm.hpp>

#include "optics/ray/BenchTracer.hpp"
#include "optics/scene/BenchInteraction.hpp"
#include "optics/scene/BenchScene.hpp"
#include "optics/scene/NumericalAperture.hpp"
#include "optics/scene/OpticalBenchScene.hpp"
#include "render/Camera.hpp"
#include "render/ProceduralInstrumentGeometry.hpp"
#include "render/gl/Framebuffer.hpp"
#include "render/gl/Shader.hpp"

namespace holobench::optics::scene {
using NumericalApertureResult = ObjectSideNumericalApertureResult;
}

namespace holobench::render {

class OpticalBenchRenderer final {
public:
    OpticalBenchRenderer() noexcept = default;
    ~OpticalBenchRenderer();

    OpticalBenchRenderer(const OpticalBenchRenderer&) = delete;
    OpticalBenchRenderer& operator=(const OpticalBenchRenderer&) = delete;

    OpticalBenchRenderer(OpticalBenchRenderer&& other) noexcept;
    OpticalBenchRenderer& operator=(OpticalBenchRenderer&& other) noexcept;

    [[nodiscard]] bool initialize();
    void destroy() noexcept;

    [[nodiscard]] bool updateScene(
        const optics::scene::OpticalBenchScene& scene,
        const optics::scene::ThinLensImagePrediction& prediction,
        std::span<const optics::ray::RaySegment> raySegments);

    [[nodiscard]] bool updateDynamicScene(
        const optics::scene::BenchScene& scene,
        const optics::scene::BenchTraceGraph& traceGraph,
        std::string_view selectedComponentId = {},
        const optics::scene::BenchScene* opticalProxyScene = nullptr);

    void render(int width, int height, const OrbitCamera& camera);

    /**
     * Draw an observation image on a physical world-space plane in the
     * existing bench framebuffer. The world-space vertex shader preserves
     * clip-space W, so UVs are perspective-correct while the camera moves.
     */
    [[nodiscard]] bool renderObservationTexture(
        GLuint texture,
        const std::array<math::Vec3d, 4>& worldCorners,
        const glm::mat4& viewProjection,
        float opacity = 0.92F);

    [[nodiscard]] GLuint colorTextureId() const noexcept {
        return framebuffer_.colorTextureId();
    }

    [[nodiscard]] const gl::Framebuffer& framebuffer() const noexcept {
        return framebuffer_;
    }

    [[nodiscard]] bool isInitialized() const noexcept {
        return initialized_;
    }

    [[nodiscard]] GLsizei gridVertexCount() const noexcept {
        return gridVertexCount_;
    }

    [[nodiscard]] std::size_t vertexCount() const noexcept {
        const auto g = static_cast<std::size_t>(gridVertexCount_ > 0 ? gridVertexCount_ : 0);
        const auto s = static_cast<std::size_t>(sceneVertexCount_ > 0 ? sceneVertexCount_ : 0);
        const auto t = static_cast<std::size_t>(solidVertexCount_ > 0 ? solidVertexCount_ : 0);
        if (g > std::numeric_limits<std::size_t>::max() - s
            || g + s > std::numeric_limits<std::size_t>::max() - t) {
            return std::numeric_limits<std::size_t>::max();
        }
        return g + s + t;
    }

    [[nodiscard]] GLsizei sceneVertexCount() const noexcept {
        return sceneVertexCount_;
    }

    [[nodiscard]] GLsizei solidVertexCount() const noexcept {
        return solidVertexCount_;
    }

private:
    struct BenchVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    struct ObservationVertex {
        glm::vec3 position;
        glm::vec2 uv;
    };

    [[nodiscard]] bool generateGridAndAxes();
    void uploadSceneBufferIfNeeded();

    gl::ShaderProgram shader_;
    gl::ShaderProgram solidShader_;
    gl::ShaderProgram observationShader_;
    gl::Framebuffer framebuffer_;

    GLuint gridVao_ = 0;
    GLuint gridVbo_ = 0;
    GLsizei gridVertexCount_ = 0;

    GLuint sceneVao_ = 0;
    GLuint sceneVbo_ = 0;
    GLsizei sceneVertexCount_ = 0;
    std::size_t sceneVboCapacityBytes_ = 0;

    GLuint solidVao_ = 0;
    GLuint solidVbo_ = 0;
    GLsizei solidVertexCount_ = 0;
    std::size_t solidVboCapacityBytes_ = 0;

    GLuint observationVao_ = 0;
    GLuint observationVbo_ = 0;

    std::vector<BenchVertex> cpuSceneVertices_;
    std::vector<BenchVertex> stagingVertices_;
    std::vector<InstrumentVertex> cpuSolidVertices_;
    std::vector<InstrumentVertex> stagingSolidVertices_;
    bool sceneDirty_ = false;

    bool initialized_ = false;
};

} // namespace holobench::render
