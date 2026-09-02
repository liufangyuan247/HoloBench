#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/wave/DiffuseObjectWavefront.hpp"

namespace fft = holobench::compute::fft;
namespace field = holobench::field;
namespace scene = holobench::optics::scene;
namespace wave = holobench::optics::wave;

namespace {

scene::ObjectWavefrontSourceParameters primitiveParameters(
    scene::ObjectSourceGeometry geometry) {
    scene::ObjectWavefrontSourceParameters result;
    result.geometry = geometry;
    result.widthMetres = 0.012;
    result.heightMetres = 0.010;
    result.depthMetres = 0.008;
    result.primitiveYawRadians = 0.0;
    result.primitivePitchRadians = 0.0;
    result.roughnessSeed = 42U;
    return result;
}

double integratedPower(const field::ComplexField2D& value) {
    double result = 0.0;
    for (const auto sample : value.samples()) result += std::norm(sample);
    return result * value.pitchXMetres() * value.pitchYMetres();
}

} // namespace

TEST_CASE("analytic diffuse primitives expose visible depth and Lambert normals") {
    auto sphere = primitiveParameters(scene::ObjectSourceGeometry::Sphere);
    const auto sphereCentre = wave::sampleDiffuseObjectSurface(
        sphere, 0.0, 0.0);
    REQUIRE(sphereCentre.has_value());
    CHECK(sphereCentre->positionInSourceMetres.z
        == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(sphereCentre->outwardNormalInSource.z
        == doctest::Approx(1.0).epsilon(1e-12));
    CHECK(sphereCentre->lambertianAmplitude
        == doctest::Approx(1.0).epsilon(1e-12));

    const auto sphereOffAxis = wave::sampleDiffuseObjectSurface(
        sphere, 0.003, 0.0);
    REQUIRE(sphereOffAxis.has_value());
    CHECK(sphereOffAxis->positionInSourceMetres.z < -0.0004);
    CHECK(sphereOffAxis->positionInSourceMetres.z > -0.0010);
    CHECK(sphereOffAxis->outwardNormalInSource.x > 0.0);
    CHECK(sphereOffAxis->outwardNormalInSource.z < 1.0);
    CHECK_FALSE(wave::sampleDiffuseObjectSurface(
        sphere, 0.007, 0.0).has_value());

    auto cube = primitiveParameters(scene::ObjectSourceGeometry::Cube);
    cube.primitiveYawRadians = 0.45;
    cube.primitivePitchRadians = -0.25;
    const auto cubeHit = wave::sampleDiffuseObjectSurface(cube, 0.0, 0.0);
    REQUIRE(cubeHit.has_value());
    CHECK(cubeHit->positionInSourceMetres.z <= 1e-12);
    CHECK(cubeHit->outwardNormalInSource.z > 0.8);
    CHECK(cubeHit->lambertianAmplitude > 0.8);

    auto tetrahedron = primitiveParameters(
        scene::ObjectSourceGeometry::Tetrahedron);
    tetrahedron.primitiveYawRadians = -0.3;
    tetrahedron.primitivePitchRadians = 0.2;
    const auto tetrahedronHit = wave::sampleDiffuseObjectSurface(
        tetrahedron, 0.0, 0.0);
    REQUIRE(tetrahedronHit.has_value());
    CHECK(tetrahedronHit->positionInSourceMetres.z <= 1e-12);
    CHECK(tetrahedronHit->outwardNormalInSource.z > 0.0);
}

TEST_CASE("coherent diffuse rough phase is stable in physical space") {
    const holobench::math::Vec3d point {0.0012, -0.0023, 0.0004};
    const double first = wave::diffuseObjectRoughPhaseRadians(123U, point);
    const double repeated = wave::diffuseObjectRoughPhaseRadians(123U, point);
    const double anotherSeed = wave::diffuseObjectRoughPhaseRadians(124U, point);
    const double anotherCell = wave::diffuseObjectRoughPhaseRadians(
        123U, {point.x + 50e-6, point.y, point.z});

    CHECK(first == repeated);
    CHECK(first >= 0.0);
    CHECK(first < 2.0 * std::numbers::pi);
    CHECK(first != anotherSeed);
    CHECK(first != anotherCell);
}

TEST_CASE("layered diffuse object wavefront is deterministic and power normalized") {
    fft::CpuFftBackend backend;
    auto parameters = primitiveParameters(scene::ObjectSourceGeometry::Sphere);
    parameters.primitiveYawRadians = 0.35;
    parameters.primitivePitchRadians = -0.2;
    field::ComplexField2D first(64U, 64U, 0.00025, 0.00025, 532e-9);
    field::ComplexField2D repeated(64U, 64U, 0.00025, 0.00025, 532e-9);
    const auto diagnostics
        = wave::synthesizeDiffuseObjectWavefrontAtReferencePlane(
            first, parameters, 0.2, std::polar(1.0, 0.3), backend);
    const auto repeatedDiagnostics
        = wave::synthesizeDiffuseObjectWavefrontAtReferencePlane(
            repeated, parameters, 0.2, std::polar(1.0, 0.3), backend);

    CHECK(diagnostics.visibleSurfaceSampleCount > 500U);
    CHECK(diagnostics.populatedDepthLayerCount > 1U);
    CHECK(diagnostics.populatedDepthLayerCount <= 6U);
    CHECK(diagnostics.nearestSurfaceDepthMetres
        > diagnostics.farthestSurfaceDepthMetres);
    CHECK(diagnostics.normalizedPowerWatts == doctest::Approx(0.2));
    CHECK(repeatedDiagnostics.visibleSurfaceSampleCount
        == diagnostics.visibleSurfaceSampleCount);
    CHECK(integratedPower(first)
        == doctest::Approx(0.2).epsilon(2e-12));
    REQUIRE(first.samples().size() == repeated.samples().size());
    for (std::size_t index = 0U; index < first.sampleCount(); ++index) {
        CHECK(first.samples()[index] == repeated.samples()[index]);
    }

    parameters.roughnessSeed += 1U;
    field::ComplexField2D changedSeed(
        64U, 64U, 0.00025, 0.00025, 532e-9);
    static_cast<void>(
        wave::synthesizeDiffuseObjectWavefrontAtReferencePlane(
            changedSeed, parameters, 0.2, std::polar(1.0, 0.3), backend));
    const bool differs = std::mismatch(
        first.samples().begin(), first.samples().end(),
        changedSeed.samples().begin()).first != first.samples().end();
    CHECK(differs);
    CHECK(integratedPower(changedSeed)
        == doctest::Approx(0.2).epsilon(2e-12));
}

TEST_CASE("all supported solid samples synthesize nonzero object waves") {
    fft::CpuFftBackend backend;
    constexpr std::array geometries {
        scene::ObjectSourceGeometry::Cube,
        scene::ObjectSourceGeometry::Sphere,
        scene::ObjectSourceGeometry::Tetrahedron,
    };
    for (const auto geometry : geometries) {
        auto parameters = primitiveParameters(geometry);
        parameters.primitiveYawRadians = 0.4;
        parameters.primitivePitchRadians = -0.2;
        field::ComplexField2D value(
            32U, 32U, 0.0004, 0.0004, 532e-9);
        const auto diagnostics
            = wave::synthesizeDiffuseObjectWavefrontAtReferencePlane(
                value, parameters, 0.1, {1.0, 0.0}, backend);
        CAPTURE(static_cast<int>(geometry));
        CHECK(diagnostics.visibleSurfaceSampleCount > 0U);
        CHECK(diagnostics.populatedDepthLayerCount > 0U);
        CHECK(integratedPower(value)
            == doctest::Approx(0.1).epsilon(2e-12));
    }
}

TEST_CASE("diffuse primitive APIs reject invalid geometry coordinates and phase") {
    auto parameters = primitiveParameters(scene::ObjectSourceGeometry::Cube);
    parameters.depthMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(wave::sampleDiffuseObjectSurface(
            parameters, 0.0, 0.0)),
        std::invalid_argument);
    parameters = primitiveParameters(scene::ObjectSourceGeometry::Cube);
    CHECK_THROWS_AS(
        static_cast<void>(wave::sampleDiffuseObjectSurface(
            parameters,
            std::numeric_limits<double>::quiet_NaN(),
            0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(wave::diffuseObjectRoughPhaseRadians(
            1U,
            {0.0, std::numeric_limits<double>::infinity(), 0.0})),
        std::invalid_argument);

    fft::CpuFftBackend backend;
    field::ComplexField2D value(
        16U, 16U, 0.0005, 0.0005, 532e-9);
    CHECK_THROWS_AS(
        static_cast<void>(
            wave::synthesizeDiffuseObjectWavefrontAtReferencePlane(
                value, parameters, 0.1, {2.0, 0.0}, backend)),
        std::invalid_argument);
}
