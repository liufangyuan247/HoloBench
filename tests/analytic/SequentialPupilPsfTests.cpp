#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <stdexcept>

#include "optics/material/OpticalMaterial.hpp"
#include "optics/wave/SequentialPupilPsf.hpp"

namespace material = holobench::optics::material;
namespace math = holobench::math;
namespace ray = holobench::optics::ray;
namespace wave = holobench::optics::wave;

namespace {

ray::SequentialLensPrescription makeParallelPlate() {
    const auto vacuum = material::makeVacuumMaterial();
    const material::OpticalMaterial glass {
        .id = "glass",
        .displayName = "Glass",
        .wavelengthDomain = {
            .minimumMetres = 400e-9,
            .maximumMetres = 700e-9,
        },
        .dispersion = material::ConstantIndexModel {.refractiveIndex = 1.5},
    };
    const ray::RotationalSurface plane {
        .curvaturePerMetre = 0.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.02,
    };
    return {
        .id = "pupil_parallel_plate",
        .materials = {vacuum, glass},
        .surfaces = {
            {
                .id = "front",
                .geometry = plane,
                .localToWorld = {.translationMetres = {0.0, 0.0, 0.0}},
                .materialBeforeId = "vacuum",
                .materialAfterId = "glass",
            },
            {
                .id = "back",
                .geometry = plane,
                .localToWorld = {.translationMetres = {0.0, 0.0, 0.01}},
                .materialBeforeId = "glass",
                .materialAfterId = "vacuum",
            },
        },
    };
}

ray::SurfaceIntersectionOptions traceOptions() {
    ray::SurfaceIntersectionOptions result;
    result.maximumDistanceMetres = 0.2;
    return result;
}

} // namespace

TEST_CASE("sequential pupil wavefront retains geometric and optical-path truth") {
    const std::array rays {
        ray::makeRay({-1e-3, 0.0, -0.02}, {0.0, 0.0, 1.0}, 532e-9, 0.5),
        ray::makeRay({1e-3, 0.0, -0.02}, {0.0, 0.0, 1.0}, 532e-9, 0.5),
    };
    math::RigidTransform3d sensor;
    sensor.translationMetres = {0.0, 0.0, 0.10};
    const auto result = wave::traceSequentialPupilWavefront(
        rays,
        makeParallelPlate(),
        sensor,
        0.0,
        0.0,
        traceOptions());

    CHECK(result.inputRayCount == 2U);
    CHECK(result.rejectedRayCount == 0U);
    REQUIRE(result.samples.size() == 2U);
    CHECK(result.acceptedPower == doctest::Approx(1.0));
    CHECK(std::abs(result.geometricCentroidXMetres) < 1e-15);
    CHECK(result.geometricRmsRadiusMetres == doctest::Approx(1e-3));
    CHECK(result.geometricRadiusMetres == doctest::Approx(1e-3));
    CHECK(result.referenceOpticalPathMetres > 0.12);
    CHECK(result.rmsOpticalPathDifferenceMetres < 1e-15);
    CHECK(result.peakToValleyOpticalPathDifferenceMetres < 1e-15);

    const std::array offsets {0.0, 0.25 * 532e-9};
    const auto phaseShifted = wave::traceSequentialPupilWavefront(
        rays,
        makeParallelPlate(),
        sensor,
        0.0,
        0.0,
        traceOptions(),
        offsets);
    CHECK(phaseShifted.peakToValleyOpticalPathDifferenceMetres
        == doctest::Approx(0.25 * 532e-9).epsilon(1e-9));
    CHECK(phaseShifted.rmsOpticalPathDifferenceMetres
        == doctest::Approx(0.125 * 532e-9).epsilon(1e-9));
    const std::array wrongOffsets {0.0};
    CHECK_THROWS_AS(
        static_cast<void>(wave::traceSequentialPupilWavefront(
            rays,
            makeParallelPlate(),
            sensor,
            0.0,
            0.0,
            traceOptions(),
            wrongOffsets)),
        std::invalid_argument);
}

TEST_CASE("coherent pupil phase produces constructive and destructive interference") {
    constexpr double wavelength = 500e-9;
    wave::SequentialPupilWavefront pupil;
    pupil.inputRayCount = 2U;
    pupil.acceptedPower = 1.0;
    pupil.referenceOpticalPathMetres = 0.0;
    pupil.samples = {
        {
            .sourceRayIndex = 0U,
            .exitRay = ray::makeRay(
                {-1e-3, 0.0, 0.0}, {0.0, 0.0, 1.0}, wavelength, 0.5),
            .opticalPathToExitMetres = 0.0,
        },
        {
            .sourceRayIndex = 1U,
            .exitRay = ray::makeRay(
                {1e-3, 0.0, 0.0}, {0.0, 0.0, 1.0}, wavelength, 0.5),
            .opticalPathToExitMetres = 0.0,
        },
    };
    const std::array sensorPoint {math::Vec3d {0.0, 0.0, 0.10}};
    const auto constructive = wave::evaluateCoherentPupil(
        pupil, sensorPoint);
    REQUIRE(constructive.relativeIntensities.size() == 1U);
    CHECK(constructive.relativeIntensities.front() > 100.0);
    CHECK(constructive.complexTermCount == 2U);

    pupil.samples[1].opticalPathToExitMetres = 0.5 * wavelength;
    const auto destructive = wave::evaluateCoherentPupil(
        pupil, sensorPoint);
    REQUIRE(destructive.relativeIntensities.size() == 1U);
    CHECK(destructive.relativeIntensities.front()
        < constructive.relativeIntensities.front() * 1e-20);
}
