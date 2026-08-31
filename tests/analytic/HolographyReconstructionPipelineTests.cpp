#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "app/HolographyReconstructionPipeline.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace appholography = holobench::app::holography;
namespace fft = holobench::compute::fft;
namespace field = holobench::field;

namespace {

[[nodiscard]] field::ComplexField2D makeObjectField(std::size_t size = 16) {
    field::ComplexField2D result(size, size, 8e-6, 8e-6, 532e-9);
    for (std::size_t y = 0; y < size; ++y) {
        const double py = result.yCoordinateMetres(y) / 40e-6;
        for (std::size_t x = 0; x < size; ++x) {
            const double px = result.xCoordinateMetres(x) / 40e-6;
            const double first = 0.35 * std::exp(
                -((px - 0.6) * (px - 0.6) + (py + 0.3) * (py + 0.3)));
            const double second = 0.2 * std::exp(
                -((px + 0.8) * (px + 0.8) + (py - 0.5) * (py - 0.5)) / 0.7);
            result.at(x, y) = {
                first + second,
                0.15 * first - 0.1 * second,
            };
        }
    }
    return result;
}

[[nodiscard]] double maximumDifference(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) {
    double result = 0.0;
    for (std::size_t index = 0; index < first.sampleCount(); ++index) {
        result = std::max(result, std::abs(
            first.samples()[index] - second.samples()[index]));
    }
    return result;
}

} // namespace

TEST_SUITE("HolographyReconstructionPipeline") {

TEST_CASE("ordinary and conjugate isolated orders reconstruct virtual and real images") {
    const auto object = makeObjectField();
    appholography::ThinHologramReconstructionConfig config;
    config.objectToPlateDistanceMetres = 0.012;
    config.recordingReference.amplitude = {0.5, -0.1};
    config.recordingReference.directionCosineX = 0.025;
    config.recordingReference.directionCosineY = -0.015;
    config.recordingReference.phaseAtOriginRadians = 0.31;
    fft::CpuFftBackend backend;

    const auto result = appholography::runThinHologramReconstruction(
        object, config, backend);

    CHECK(result.expectedImageAmplitudeScale
        == doctest::Approx(config.response.intensityToAmplitudeGain
            * std::norm(config.recordingReference.amplitude)).epsilon(1e-15));
    CHECK(result.virtualImageQuality.normalizedComplexL2Error < 2e-12);
    CHECK(result.virtualImageQuality.peakNormalizedMaximumComplexError < 2e-12);
    CHECK(result.realImageQuality.normalizedComplexL2Error < 2e-12);
    CHECK(result.realImageQuality.peakNormalizedMaximumComplexError < 2e-12);
    CHECK(result.recordingPropagation.evanescentBinCount == 0);
    CHECK(result.virtualImagePropagation.evanescentBinCount == 0);
    CHECK(result.realImagePropagation.evanescentBinCount == 0);
    CHECK(result.hologram.diagnostics.minimumClampedSampleCount == 0);
    CHECK(result.hologram.diagnostics.maximumClampedSampleCount == 0);
}

TEST_CASE("recording propagation matches an independent Helmholtz spectral-bin phase") {
    constexpr std::size_t size = 16;
    constexpr std::size_t xBin = 2;
    constexpr std::size_t yBin = 1;
    constexpr double pitch = 8e-6;
    constexpr double wavelength = 532e-9;
    constexpr double distance = 0.009;
    field::ComplexField2D object(size, size, pitch, pitch, wavelength);
    for (std::size_t y = 0; y < size; ++y) {
        for (std::size_t x = 0; x < size; ++x) {
            const double phase = 2.0 * std::numbers::pi
                * (static_cast<double>(xBin * x + yBin * y)
                    / static_cast<double>(size));
            object.at(x, y) = 0.2 * std::polar(1.0, phase);
        }
    }
    appholography::ThinHologramReconstructionConfig config;
    config.objectToPlateDistanceMetres = distance;
    fft::CpuFftBackend backend;

    const auto result = appholography::runThinHologramReconstruction(
        object, config, backend);

    const double fx = static_cast<double>(xBin)
        / (static_cast<double>(size) * pitch);
    const double fy = static_cast<double>(yBin)
        / (static_cast<double>(size) * pitch);
    const double inverseWavelength = 1.0 / wavelength;
    const double longitudinalCycles = std::sqrt(
        inverseWavelength * inverseWavelength - fx * fx - fy * fy);
    const auto expectedTransfer = std::polar(
        1.0, 2.0 * std::numbers::pi * longitudinalCycles * distance);
    for (std::size_t index = 0; index < object.sampleCount(); ++index) {
        CHECK(std::abs(
            result.objectAtRecordingPlate.samples()[index]
                - object.samples()[index] * expectedTransfer) < 2e-12);
    }
}

TEST_CASE("physical full replay remains distinct from explicitly isolated image orders") {
    const auto object = makeObjectField();
    fft::CpuFftBackend backend;
    const auto result = appholography::runThinHologramReconstruction(
        object, {}, backend);

    CHECK(maximumDifference(
        result.ordinaryFullReplayAtVirtualPlane,
        result.isolatedVirtualImageOrder) > 1e-3);
    CHECK(maximumDifference(
        result.conjugateFullReplayAtRealPlane,
        result.isolatedRealImageOrder) > 1e-3);
}

TEST_CASE("thin hologram reconstruction is deterministic") {
    const auto object = makeObjectField();
    fft::CpuFftBackend backend;
    const auto first = appholography::runThinHologramReconstruction(
        object, {}, backend);
    const auto second = appholography::runThinHologramReconstruction(
        object, {}, backend);

    CHECK(first.virtualImageQuality.normalizedComplexL2Error
        == second.virtualImageQuality.normalizedComplexL2Error);
    CHECK(first.realImageQuality.normalizedComplexL2Error
        == second.realImageQuality.normalizedComplexL2Error);
    CHECK(maximumDifference(
        first.isolatedVirtualImageOrder,
        second.isolatedVirtualImageOrder) == 0.0);
    CHECK(maximumDifference(
        first.isolatedRealImageOrder,
        second.isolatedRealImageOrder) == 0.0);
}

TEST_CASE("reconstruction rejects clipping zero signal and invalid geometry") {
    const auto object = makeObjectField();
    fft::CpuFftBackend backend;
    appholography::ThinHologramReconstructionConfig config;

    config.objectToPlateDistanceMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runThinHologramReconstruction(
            object, config, backend)),
        std::invalid_argument);

    config = {};
    config.response.intensityToAmplitudeGain = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runThinHologramReconstruction(
            object, config, backend)),
        std::invalid_argument);

    config = {};
    config.response.maximumAmplitudeTransmission = 0.01;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runThinHologramReconstruction(
            object, config, backend)),
        std::invalid_argument);

    auto zero = object;
    zero.fill({0.0, 0.0});
    config = {};
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runThinHologramReconstruction(
            zero, config, backend)),
        std::invalid_argument);

    const auto unsupported = makeObjectField(15);
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runThinHologramReconstruction(
            unsupported, config, backend)),
        std::invalid_argument);
}

} // TEST_SUITE("HolographyReconstructionPipeline")
