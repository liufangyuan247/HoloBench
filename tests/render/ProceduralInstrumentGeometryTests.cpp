#include "render/ProceduralInstrumentGeometry.hpp"

#include <cmath>
#include <limits>

#include <doctest/doctest.h>
#include <glm/geometric.hpp>

#include "optics/scene/BenchScene.hpp"

namespace holobench::render {
namespace {

bool finite(const glm::vec3& value) {
    return std::isfinite(value.x) && std::isfinite(value.y)
        && std::isfinite(value.z);
}

} // namespace

TEST_CASE("every Bench component produces bounded finite PCG solid geometry") {
    namespace scene = optics::scene;
    std::size_t componentIndex = 0U;
    for (const auto kind : scene::requiredBenchComponentKinds()) {
        auto component = scene::makeDefaultBenchComponent(
            kind, "pcg-component-" + std::to_string(componentIndex++));
        const auto mesh = generateProceduralInstrumentMesh(component);

        CAPTURE(scene::benchComponentDisplayName(kind));
        CHECK(mesh.triangleCount() > 0U);
        CHECK(mesh.triangles.size() % 3U == 0U);
        CHECK(mesh.triangles.size() <= 50'000U);
        CHECK(finite(mesh.boundsMinimum));
        CHECK(finite(mesh.boundsMaximum));
        CHECK(glm::all(glm::greaterThan(
            mesh.boundsMaximum - mesh.boundsMinimum,
            glm::vec3(0.0F))));
        for (const auto& vertex : mesh.triangles) {
            CHECK(finite(vertex.position));
            CHECK(finite(vertex.normal));
            CHECK(glm::length(vertex.normal)
                == doctest::Approx(1.0F).epsilon(1e-5));
        }
        for (std::size_t index = 0U; index < mesh.triangles.size(); index += 3U) {
            const auto& a = mesh.triangles[index].position;
            const auto& b = mesh.triangles[index + 1U].position;
            const auto& c = mesh.triangles[index + 2U].position;
            CHECK(glm::length(glm::cross(b - a, c - a)) > 1e-10F);
        }
    }
}

TEST_CASE("PCG instrument dimensions follow editable optical parameters") {
    namespace scene = optics::scene;
    auto mirror = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror, "scalable-mirror");
    auto parameters = std::get<scene::PlanarMirrorParameters>(mirror.parameters);
    parameters.widthMetres = 0.04;
    parameters.heightMetres = 0.03;
    mirror.parameters = parameters;
    const auto small = generateProceduralInstrumentMesh(mirror);

    parameters.widthMetres = 0.12;
    parameters.heightMetres = 0.09;
    mirror.parameters = parameters;
    const auto large = generateProceduralInstrumentMesh(mirror);

    CHECK(small.opticalProxy.widthMetres == doctest::Approx(0.04F));
    CHECK(small.opticalProxy.heightMetres == doctest::Approx(0.03F));
    CHECK(large.opticalProxy.widthMetres == doctest::Approx(0.12F));
    CHECK(large.opticalProxy.heightMetres == doctest::Approx(0.09F));
    CHECK((large.boundsMaximum.x - large.boundsMinimum.x)
        > (small.boundsMaximum.x - small.boundsMinimum.x) * 2.0F);
    CHECK((large.boundsMaximum.y - large.boundsMinimum.y)
        > (small.boundsMaximum.y - small.boundsMinimum.y) * 2.0F);
}

TEST_CASE("PCG solids and diagnostic proxy follow the exact rigid instrument pose") {
    namespace scene = optics::scene;
    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "posed-lens");
    lens.transform = {
        .translationMetres = {0.25, 0.10, -0.40},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    const auto mesh = generateProceduralInstrumentMesh(
        lens, {.radialSegments = 32U, .selected = true});

    CHECK(mesh.opticalProxy.centre.x == doctest::Approx(0.25F));
    CHECK(mesh.opticalProxy.centre.y == doctest::Approx(0.10F));
    CHECK(mesh.opticalProxy.centre.z == doctest::Approx(-0.40F));
    CHECK(mesh.opticalProxy.normal.x == doctest::Approx(1.0F));
    CHECK(mesh.opticalProxy.normal.y == doctest::Approx(0.0F));
    CHECK(mesh.opticalProxy.normal.z == doctest::Approx(0.0F));
    CHECK(mesh.boundsMaximum.x > mesh.opticalProxy.centre.x);
    CHECK(mesh.boundsMinimum.x < mesh.opticalProxy.centre.x);
}

TEST_CASE("PCG tessellation is deterministic and bounded by the public options") {
    namespace scene = optics::scene;
    const auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "deterministic-lens");
    const auto first = generateProceduralInstrumentMesh(
        lens, {.radialSegments = 24U, .selected = false});
    const auto second = generateProceduralInstrumentMesh(
        lens, {.radialSegments = 24U, .selected = false});
    const auto clampedLow = generateProceduralInstrumentMesh(
        lens, {.radialSegments = 1U, .selected = false});
    const auto explicitLow = generateProceduralInstrumentMesh(
        lens, {.radialSegments = 8U, .selected = false});

    REQUIRE(first.triangles.size() == second.triangles.size());
    for (std::size_t index = 0U; index < first.triangles.size(); ++index) {
        CHECK(first.triangles[index].position == second.triangles[index].position);
        CHECK(first.triangles[index].normal == second.triangles[index].normal);
        CHECK(first.triangles[index].color == second.triangles[index].color);
    }
    CHECK(clampedLow.triangles.size() == explicitLow.triangles.size());
}

} // namespace holobench::render

