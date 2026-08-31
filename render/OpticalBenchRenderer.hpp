#pragma once

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
        std::string_view selectedComponentId = {});

    void render(int width, int height, const OrbitCamera& camera);

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
        if (g > std::numeric_limits<std::size_t>::max() - s) {
            return std::numeric_limits<std::size_t>::max();
        }
        return g + s;
    }

    [[nodiscard]] GLsizei sceneVertexCount() const noexcept {
        return sceneVertexCount_;
    }

private:
    struct BenchVertex {
        glm::vec3 position;
        glm::vec4 color;
    };

    [[nodiscard]] bool generateGridAndAxes();
    void uploadSceneBufferIfNeeded();

    gl::ShaderProgram shader_;
    gl::Framebuffer framebuffer_;

    GLuint gridVao_ = 0;
    GLuint gridVbo_ = 0;
    GLsizei gridVertexCount_ = 0;

    GLuint sceneVao_ = 0;
    GLuint sceneVbo_ = 0;
    GLsizei sceneVertexCount_ = 0;
    std::size_t sceneVboCapacityBytes_ = 0;

    std::vector<BenchVertex> cpuSceneVertices_;
    std::vector<BenchVertex> stagingVertices_;
    bool sceneDirty_ = false;

    bool initialized_ = false;
};

} // namespace holobench::render
