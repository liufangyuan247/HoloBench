#pragma once

#include <cstddef>
#include <vector>

#include <glm/glm.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::render {

struct InstrumentVertex final {
    glm::vec3 position {};
    glm::vec3 normal {0.0F, 0.0F, 1.0F};
    glm::vec4 color {1.0F};
};

struct OpticalProxyVisual final {
    glm::vec3 centre {};
    glm::vec3 normal {0.0F, 0.0F, 1.0F};
    glm::vec3 xAxis {1.0F, 0.0F, 0.0F};
    glm::vec3 yAxis {0.0F, 1.0F, 0.0F};
    float widthMetres = 0.0F;
    float heightMetres = 0.0F;
};

struct ProceduralInstrumentMesh final {
    std::vector<InstrumentVertex> triangles;
    OpticalProxyVisual opticalProxy;
    glm::vec3 boundsMinimum {};
    glm::vec3 boundsMaximum {};

    [[nodiscard]] std::size_t triangleCount() const noexcept {
        return triangles.size() / 3U;
    }
};

struct InstrumentGenerationOptions final {
    std::size_t radialSegments = 24U;
    bool selected = false;
};

// Generates disposable visual geometry from the validated instrument state.
// The returned opticalProxy is diagnostic presentation evidence only; optics/
// remains the sole owner of physical interaction truth.
[[nodiscard]] ProceduralInstrumentMesh generateProceduralInstrumentMesh(
    const optics::scene::BenchComponent& component,
    const InstrumentGenerationOptions& options = {});

} // namespace holobench::render
