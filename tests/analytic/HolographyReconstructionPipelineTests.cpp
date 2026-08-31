#include <doctest/doctest.h>

#include <algorithm>
#include <array>
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

TEST_CASE("phase-only spectral mode reconstructs with independent Helmholtz phase") {
    constexpr std::size_t size = 16;
    constexpr std::size_t xBin = 2;
    constexpr std::size_t yBin = 1;
    constexpr double pitch = 8e-6;
    constexpr double wavelength = 532e-9;
    constexpr double distance = 0.011;
    constexpr double offset = 0.31;
    field::ComplexField2D target(size, size, pitch, pitch, wavelength);
    const double fx = static_cast<double>(xBin)
        / (static_cast<double>(size) * pitch);
    const double fy = static_cast<double>(yBin)
        / (static_cast<double>(size) * pitch);
    const double inverseWavelength = 1.0 / wavelength;
    const double longitudinalCycles = std::sqrt(
        inverseWavelength * inverseWavelength - fx * fx - fy * fy);
    const auto axialTransfer = std::polar(
        1.0, 2.0 * std::numbers::pi * longitudinalCycles * distance);
    for (std::size_t y = 0; y < size; ++y) {
        for (std::size_t x = 0; x < size; ++x) {
            const double transversePhase = 2.0 * std::numbers::pi
                * static_cast<double>(xBin * x + yBin * y)
                / static_cast<double>(size);
            target.at(x, y) = std::polar(1.0, transversePhase) * axialTransfer;
        }
    }
    appholography::PhaseOnlyReconstructionConfig config;
    config.hologramToTargetDistanceMetres = distance;
    config.uniformReplayAmplitude = {0.7, -0.2};
    config.encoding.phaseOffsetRadians = offset;
    fft::CpuFftBackend backend;

    const auto result = appholography::runPhaseOnlyReconstruction(
        target, config, backend);

    const auto expectedScale = config.uniformReplayAmplitude
        * std::polar(1.0, offset);
    CHECK(std::abs(
        result.quality.bestFitTargetComplexScale - expectedScale) < 2e-12);
    CHECK(result.quality.matchedModePowerFraction
        == doctest::Approx(1.0).epsilon(2e-14));
    CHECK(result.quality.replayNormalizedComplexResidual < 2e-12);
    CHECK(result.quality.replayPeakNormalizedMaximumComplexResidual < 2e-12);
    CHECK(result.quality.replayNormalizedIntensityResidual < 2e-12);
    CHECK(result.quality.replayPeakNormalizedMaximumIntensityResidual < 2e-12);
    CHECK(result.hologram.diagnostics.invalidPhaseSampleCount == 0);
    CHECK(result.synthesisPropagation.evanescentBinCount == 0);
    CHECK(result.replayPropagation.evanescentBinCount == 0);
}

TEST_CASE("phase-only reconstruction quality exposes discarded target amplitude") {
    constexpr std::size_t size = 8;
    field::ComplexField2D plate(size, size, 8e-6, 8e-6, 532e-9);
    for (std::size_t index = 0; index < plate.sampleCount(); ++index) {
        plate.samples()[index] = {
            index % 2U == 0U ? 0.5 : 1.0,
            0.0,
        };
    }
    constexpr double distance = 0.007;
    fft::CpuFftBackend backend;
    holobench::compute::propagation::AngularSpectrumPropagator propagator(backend);
    auto target = plate;
    static_cast<void>(propagator.propagateInPlace(target, distance));
    appholography::PhaseOnlyReconstructionConfig config;
    config.hologramToTargetDistanceMetres = distance;

    const auto result = appholography::runPhaseOnlyReconstruction(
        target, config, backend);

    // Parseval invariance makes this independent mode-overlap oracle equivalent
    // at the plate: mean(a)^2 / mean(a^2) = 0.75^2 / 0.625 = 0.9.
    CHECK(result.quality.matchedModePowerFraction
        == doctest::Approx(0.9).epsilon(2e-13));
    CHECK(result.quality.replayNormalizedComplexResidual
        == doctest::Approx(std::sqrt(0.1)).epsilon(2e-12));
    CHECK(result.quality.replayNormalizedIntensityResidual > 0.0);
    CHECK(result.quality.replayPeakNormalizedMaximumIntensityResidual > 0.0);
    CHECK(result.hologram.diagnostics.minimumTargetAmplitude
        == doctest::Approx(0.5).epsilon(2e-12));
    CHECK(result.hologram.diagnostics.maximumTargetAmplitude
        == doctest::Approx(1.0).epsilon(2e-12));
}

TEST_CASE("phase quantization degrades a non-code spectral ramp deterministically") {
    constexpr std::size_t size = 16;
    field::ComplexField2D plate(size, size, 8e-6, 8e-6, 532e-9);
    for (std::size_t y = 0; y < size; ++y) {
        for (std::size_t x = 0; x < size; ++x) {
            const double phase = 2.0 * std::numbers::pi
                * static_cast<double>(x + 2U * y)
                / static_cast<double>(size);
            plate.at(x, y) = std::polar(1.0, phase);
        }
    }
    constexpr double distance = 0.009;
    fft::CpuFftBackend backend;
    holobench::compute::propagation::AngularSpectrumPropagator propagator(backend);
    auto target = plate;
    static_cast<void>(propagator.propagateInPlace(target, distance));
    appholography::PhaseOnlyReconstructionConfig continuousConfig;
    continuousConfig.hologramToTargetDistanceMetres = distance;
    auto quantizedConfig = continuousConfig;
    quantizedConfig.encoding.bitDepth = 2;

    const auto continuous = appholography::runPhaseOnlyReconstruction(
        target, continuousConfig, backend);
    const auto quantized = appholography::runPhaseOnlyReconstruction(
        target, quantizedConfig, backend);
    const auto repeated = appholography::runPhaseOnlyReconstruction(
        target, quantizedConfig, backend);

    CHECK(continuous.quality.replayNormalizedComplexResidual < 2e-12);
    CHECK(quantized.quality.matchedModePowerFraction
        < continuous.quality.matchedModePowerFraction);
    CHECK(quantized.quality.replayNormalizedComplexResidual
        > continuous.quality.replayNormalizedComplexResidual);
    CHECK(quantized.hologram.diagnostics.quantizedPhaseSampleCount > 0);
    CHECK(quantized.quality.replayNormalizedComplexResidual
        == repeated.quality.replayNormalizedComplexResidual);
    CHECK(maximumDifference(
        quantized.reconstructedAtTarget,
        repeated.reconstructedAtTarget) == 0.0);
}

TEST_CASE("phase-only reconstruction rejects invalid geometry signal and backend") {
    const auto target = makeObjectField();
    fft::CpuFftBackend backend;
    appholography::PhaseOnlyReconstructionConfig config;

    config.hologramToTargetDistanceMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runPhaseOnlyReconstruction(
            target, config, backend)),
        std::invalid_argument);
    config = {};
    config.uniformReplayAmplitude = {0.0, 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runPhaseOnlyReconstruction(
            target, config, backend)),
        std::invalid_argument);
    config = {};
    auto zero = target;
    zero.fill({0.0, 0.0});
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runPhaseOnlyReconstruction(
            zero, config, backend)),
        std::invalid_argument);
    const auto unsupported = makeObjectField(15);
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runPhaseOnlyReconstruction(
            unsupported, config, backend)),
        std::invalid_argument);
}

TEST_CASE("H2 placement crosses the H1 real-image plane with signed geometry") {
    const auto object = makeObjectField();
    appholography::H1H2TransferConfig config;
    config.h1.objectToPlateDistanceMetres = 0.012;
    fft::CpuFftBackend backend;

    config.h2AxialPositionMetres = 0.008;
    const auto positiveSide = appholography::runH1H2Transfer(
        object, config, backend);
    config.h2AxialPositionMetres = 0.012;
    const auto transplane = appholography::runH1H2Transfer(
        object, config, backend);
    config.h2AxialPositionMetres = 0.016;
    const auto negativeSide = appholography::runH1H2Transfer(
        object, config, backend);

    CHECK(positiveSide.imagePlacement
        == appholography::H2ImagePlacement::PositiveSide);
    CHECK(positiveSide.imageDistanceFromH2Metres
        == doctest::Approx(0.004).epsilon(1e-15));
    CHECK(transplane.imagePlacement
        == appholography::H2ImagePlacement::Transplane);
    CHECK(transplane.imageDistanceFromH2Metres == 0.0);
    CHECK(negativeSide.imagePlacement
        == appholography::H2ImagePlacement::NegativeSide);
    CHECK(negativeSide.imageDistanceFromH2Metres
        == doctest::Approx(-0.004).epsilon(1e-15));
    for (const auto* result : {&positiveSide, &transplane, &negativeSide}) {
        CHECK(result->h2ImageQuality.normalizedComplexL2Error < 3e-11);
        CHECK(result->h2ImageQuality.peakNormalizedMaximumComplexError < 3e-11);
        CHECK(result->h1ToH2Propagation.evanescentBinCount == 0);
        CHECK(result->h2ToImagePropagation.evanescentBinCount == 0);
        CHECK(result->expectedH2ImageAmplitudeScale
            == doctest::Approx(config.h2Response.intensityToAmplitudeGain
                * std::norm(config.h2RecordingReference.amplitude)).epsilon(1e-15));
        CHECK(maximumDifference(
            result->h2FullReplayAtH1ImagePlane,
            result->h2IsolatedImageAtH1ImagePlane) > 1e-3);
    }
}

TEST_CASE("RGB H1 H2 channels preserve wavelength-specific Helmholtz scaling") {
    constexpr std::size_t size = 16;
    constexpr std::size_t xBin = 2;
    constexpr std::size_t yBin = 1;
    constexpr double pitch = 8e-6;
    const auto makeSpectralField = [](double wavelength) {
        field::ComplexField2D result(size, size, pitch, pitch, wavelength);
        for (std::size_t y = 0; y < size; ++y) {
            for (std::size_t x = 0; x < size; ++x) {
                const double phase = 2.0 * std::numbers::pi
                    * static_cast<double>(xBin * x + yBin * y)
                    / static_cast<double>(size);
                result.at(x, y) = 0.2 * std::polar(1.0, phase);
            }
        }
        return result;
    };
    const std::array<field::ComplexField2D, 3> inputs {
        makeSpectralField(638e-9),
        makeSpectralField(532e-9),
        makeSpectralField(450e-9),
    };
    appholography::H1H2TransferConfig config;
    config.h1.objectToPlateDistanceMetres = 0.01;
    config.h2AxialPositionMetres = 0.007;
    fft::CpuFftBackend backend;

    const auto result = appholography::runRgbH1H2Transfer(
        inputs, config, backend);

    const double fx = static_cast<double>(xBin)
        / (static_cast<double>(size) * pitch);
    const double fy = static_cast<double>(yBin)
        / (static_cast<double>(size) * pitch);
    for (std::size_t channel = 0; channel < inputs.size(); ++channel) {
        const auto& input = inputs[channel];
        const auto& output = result.channels[channel];
        const double cutoff = input.refractiveIndex()
            / input.vacuumWavelengthMetres();
        const double longitudinalCycles = std::sqrt(
            cutoff * cutoff - fx * fx - fy * fy);
        const auto expectedTransfer = std::polar(
            1.0,
            2.0 * std::numbers::pi * longitudinalCycles
                * config.h1.objectToPlateDistanceMetres);
        CHECK(output.h1.objectAtRecordingPlate.vacuumWavelengthMetres()
            == input.vacuumWavelengthMetres());
        CHECK(output.h2IsolatedImageAtH1ImagePlane.vacuumWavelengthMetres()
            == input.vacuumWavelengthMetres());
        CHECK(output.h2ImageQuality.normalizedComplexL2Error < 3e-11);
        for (std::size_t index = 0; index < input.sampleCount(); ++index) {
            CHECK(std::abs(
                output.h1.objectAtRecordingPlate.samples()[index]
                    - input.samples()[index] * expectedTransfer) < 2e-12);
        }
    }
}

TEST_CASE("H1 H2 and RGB transfer reject invalid placement clipping and identity") {
    const auto object = makeObjectField();
    fft::CpuFftBackend backend;
    appholography::H1H2TransferConfig config;

    config.h2AxialPositionMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runH1H2Transfer(
            object, config, backend)),
        std::invalid_argument);
    config = {};
    config.transplaneToleranceMetres = -1.0;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runH1H2Transfer(
            object, config, backend)),
        std::invalid_argument);
    config = {};
    config.h2Response.maximumAmplitudeTransmission = 0.01;
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runH1H2Transfer(
            object, config, backend)),
        std::invalid_argument);

    const std::array<field::ComplexField2D, 3> wrongOrder {
        field::ComplexField2D(16, 16, 8e-6, 8e-6, 450e-9),
        field::ComplexField2D(16, 16, 8e-6, 8e-6, 532e-9),
        field::ComplexField2D(16, 16, 8e-6, 8e-6, 638e-9),
    };
    config = {};
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runRgbH1H2Transfer(
            wrongOrder, config, backend)),
        std::invalid_argument);
    const std::array<field::ComplexField2D, 3> wrongGrid {
        field::ComplexField2D(16, 16, 8e-6, 8e-6, 638e-9),
        field::ComplexField2D(16, 16, 9e-6, 8e-6, 532e-9),
        field::ComplexField2D(16, 16, 8e-6, 8e-6, 450e-9),
    };
    CHECK_THROWS_AS(
        static_cast<void>(appholography::runRgbH1H2Transfer(
            wrongGrid, config, backend)),
        std::invalid_argument);
}

} // TEST_SUITE("HolographyReconstructionPipeline")
