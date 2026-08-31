#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/FourFSystem.hpp"
#include "compute/fourier/PsfMtf.hpp"

namespace {

namespace fft = holobench::compute::fft;
namespace fourier = holobench::compute::fourier;
namespace field = holobench::field;

double independentJ1Series(double argument) noexcept {
    const double halfArgument = 0.5 * argument;
    double term = halfArgument;
    double sum = term;
    const double halfArgumentSquared = halfArgument * halfArgument;
    for (int order = 1; order < 80; ++order) {
        term *= -halfArgumentSquared
            / (static_cast<double>(order) * static_cast<double>(order + 1));
        sum += term;
        if (std::abs(term) < 1e-16 * std::max(1.0, std::abs(sum))) {
            break;
        }
    }
    return sum;
}

double independentAiryAmplitude(double argument) noexcept {
    if (argument == 0.0) {
        return 1.0;
    }
    return 2.0 * independentJ1Series(argument) / argument;
}

double discreteUnitPupilOverlap(double normalizedFrequency) noexcept {
    constexpr int sampleCount = 801;
    std::size_t pupilSamples = 0U;
    std::size_t overlapSamples = 0U;
    const double separation = 2.0 * normalizedFrequency;
    for (int yIndex = 0; yIndex < sampleCount; ++yIndex) {
        const double y = -1.0
            + 2.0 * (static_cast<double>(yIndex) + 0.5) / static_cast<double>(sampleCount);
        for (int xIndex = 0; xIndex < sampleCount; ++xIndex) {
            const double x = -1.0
                + 2.0 * (static_cast<double>(xIndex) + 0.5) / static_cast<double>(sampleCount);
            if (x * x + y * y <= 1.0) {
                ++pupilSamples;
                const double displacedX = x - separation;
                if (displacedX * displacedX + y * y <= 1.0) {
                    ++overlapSamples;
                }
            }
        }
    }
    return static_cast<double>(overlapSamples) / static_cast<double>(pupilSamples);
}

} // namespace

TEST_SUITE("circular-pupil PSF and MTF") {

TEST_CASE("Airy amplitude and sampled intensity match an independent Bessel-series oracle") {
    constexpr double wavelength = 532e-9;
    constexpr double refractiveIndex = 1.33;
    constexpr double focalLength = 0.080;
    constexpr double pupilRadius = 0.001;
    fourier::CircularPupilPsfMtf response(
        wavelength,
        refractiveIndex,
        focalLength,
        pupilRadius);
    const auto& diagnostics = response.diagnostics();
    const double mediumWavelength = wavelength / refractiveIndex;
    const double coherentCutoff = pupilRadius / (mediumWavelength * focalLength);

    CHECK(diagnostics.mediumWavelengthMetres
        == doctest::Approx(mediumWavelength).epsilon(1e-15));
    CHECK(diagnostics.coherentCutoffCyclesPerMetre
        == doctest::Approx(coherentCutoff).epsilon(1e-15));
    CHECK(diagnostics.incoherentCutoffCyclesPerMetre
        == doctest::Approx(2.0 * coherentCutoff).epsilon(1e-15));
    CHECK(diagnostics.firstDarkRadiusMetres
        == doctest::Approx(3.8317059702075125
            / (2.0 * std::numbers::pi * coherentCutoff)).epsilon(1e-15));
    CHECK(diagnostics.paraxialNumericalAperture
        == doctest::Approx(refractiveIndex * pupilRadius / focalLength).epsilon(1e-15));
    CHECK(diagnostics.paraxialValiditySatisfied);

    constexpr std::array<double, 6> arguments {0.0, 1e-5, 0.5, 2.0, 3.5, 7.0};
    for (const double argument : arguments) {
        const double radius = argument / (2.0 * std::numbers::pi * coherentCutoff);
        const double expectedAmplitude = independentAiryAmplitude(argument);
        CHECK(response.normalizedCoherentAmplitudePsf(radius)
            == doctest::Approx(expectedAmplitude).epsilon(2e-12));
        CHECK(response.normalizedIntensityPsf(radius)
            == doctest::Approx(expectedAmplitude * expectedAmplitude).epsilon(4e-12));
    }

    // Independent high-argument J1 reference values exercise the Hankel
    // branch used when the standard library has no special functions.
    constexpr std::array<std::array<double, 2>, 4> highArgumentJ1 {{
        {12.0, -0.22344710449062768},
        {14.0, 0.13337515469879327},
        {20.0, 0.06683312417585004},
        {50.0, -0.09751182812517517},
    }};
    for (const auto& reference : highArgumentJ1) {
        const double radius = reference[0] / (2.0 * std::numbers::pi * coherentCutoff);
        CHECK(response.normalizedCoherentAmplitudePsf(radius)
            == doctest::Approx(2.0 * reference[1] / reference[0]).epsilon(3e-12));
    }

    const auto sampled = response.sampleNormalizedIntensityPsf(7U, 5U, 2e-6, 3e-6);
    CHECK(sampled.at(3U, 2U) == doctest::Approx(1.0));
    CHECK(sampled.at(2U, 2U) == doctest::Approx(sampled.at(4U, 2U)).epsilon(1e-15));
    CHECK(sampled.at(3U, 1U) == doctest::Approx(sampled.at(3U, 3U)).epsilon(1e-15));
}

TEST_CASE("incoherent circular-pupil MTF matches independent discrete pupil overlap") {
    fourier::CircularPupilPsfMtf response(550e-9, 1.0, 0.10, 1.5e-3);
    const double cutoff = response.diagnostics().incoherentCutoffCyclesPerMetre;
    CHECK(response.normalizedIncoherentMtf(0.0) == doctest::Approx(1.0));
    CHECK(response.normalizedIncoherentMtf(cutoff) == 0.0);
    CHECK(response.normalizedIncoherentMtf(1.1 * cutoff) == 0.0);

    constexpr std::array<double, 3> normalizedFrequencies {0.25, 0.50, 0.75};
    for (const double normalizedFrequency : normalizedFrequencies) {
        const double independent = discreteUnitPupilOverlap(normalizedFrequency);
        CHECK(response.normalizedIncoherentMtf(normalizedFrequency * cutoff)
            == doctest::Approx(independent).epsilon(3e-3));
    }
    const double nearCutoff = response.normalizedIncoherentMtf((1.0 - 1e-12) * cutoff);
    CHECK(std::isfinite(nearCutoff));
    CHECK(nearCutoff > 0.0);

    const auto curve = response.sampleNormalizedIncoherentMtf(5U, cutoff);
    REQUIRE(curve.size() == 5U);
    CHECK(curve.front().spatialFrequencyCyclesPerMetre == 0.0);
    CHECK(curve.front().normalizedIncoherentMtf == doctest::Approx(1.0));
    CHECK(curve.back().spatialFrequencyCyclesPerMetre == doctest::Approx(cutoff));
    CHECK(curve.back().normalizedIncoherentMtf == 0.0);
}

TEST_CASE("4-f point response through a circular stop agrees with the continuous Airy oracle") {
    constexpr std::size_t size = 256U;
    constexpr std::size_t pupilRadiusBins = 24U;
    constexpr double pitch = 8e-6;
    constexpr double wavelength = 532e-9;
    constexpr double focalLength = 0.10;
    field::ComplexField2D point(size, size, pitch, pitch, wavelength);
    point.fill({0.0, 0.0});
    point.at(size / 2U, size / 2U) = {1.0, 0.0};

    const double fourierPitch = wavelength * focalLength
        / (static_cast<double>(size) * pitch);
    const double pupilRadius = static_cast<double>(pupilRadiusBins) * fourierPitch;
    fft::CpuFftBackend backend;
    fourier::FourFSystem system(backend);
    const auto result = system.run(
        point,
        focalLength,
        focalLength,
        fourier::CircularFourierFilter::lowPass(pupilRadius));
    fourier::CircularPupilPsfMtf oracle(wavelength, 1.0, focalLength, pupilRadius);

    const std::size_t center = size / 2U;
    const double peak = std::norm(result.imagePlane.at(center, center));
    REQUIRE(peak > 0.0);
    double maximumPeakRelativeError = 0.0;
    for (std::size_t offset = 0U; offset <= 24U; ++offset) {
        const double numerical = std::norm(result.imagePlane.at(center + offset, center)) / peak;
        const double expected = oracle.normalizedIntensityPsf(static_cast<double>(offset) * pitch);
        maximumPeakRelativeError = std::max(
            maximumPeakRelativeError,
            std::abs(numerical - expected));
    }
    CHECK(maximumPeakRelativeError < 0.02);
    CHECK(result.imagePlane.pitchXMetres() == doctest::Approx(pitch).epsilon(2e-15));
}

TEST_CASE("PSF and MTF domains reject ambiguous or non-finite parameters") {
    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularPupilPsfMtf(0.0, 1.0, 0.1, 1e-3)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularPupilPsfMtf(532e-9, -1.0, 0.1, 1e-3)),
        std::invalid_argument);
    fourier::CircularPupilPsfMtf response(532e-9, 1.0, 0.1, 1e-3);
    CHECK_THROWS_AS(
        static_cast<void>(response.normalizedIntensityPsf(-1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(response.normalizedIncoherentMtf(
            std::numeric_limits<double>::infinity())),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(response.sampleNormalizedIncoherentMtf(1U, 1.0)),
        std::invalid_argument);
}

} // TEST_SUITE("circular-pupil PSF and MTF")
