#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "core/field/ComplexField2D.hpp"
#include "optics/holography/PhaseOnlyHologram.hpp"

namespace field = holobench::field;
namespace holography = holobench::optics::holography;

TEST_SUITE("PhaseOnlyHologram") {

TEST_CASE("continuous phase-only encoding wraps analytic target phase with offset") {
    field::ComplexField2D target(5, 1, 1e-6, 1e-6, 532e-9);
    target.samples()[0] = std::polar(0.2, -2.8);
    target.samples()[1] = std::polar(0.4, -0.1);
    target.samples()[2] = std::polar(0.6, 0.0);
    target.samples()[3] = std::polar(0.8, 2.9);
    target.samples()[4] = std::polar(1.0, std::numbers::pi);
    constexpr double offset = 0.37;

    const auto encoded = holography::encodePhaseOnlyHologram(
        target, {.phaseOffsetRadians = offset});

    for (std::size_t index = 0; index < target.sampleCount(); ++index) {
        double expected = std::fmod(std::arg(target.samples()[index]) + offset,
            2.0 * std::numbers::pi);
        if (expected < 0.0) {
            expected += 2.0 * std::numbers::pi;
        }
        CHECK(encoded.encodedPhaseRadians.samples()[index]
            == doctest::Approx(expected).epsilon(1e-15));
        CHECK(encoded.validPhaseMask[index] == 1U);
    }
    CHECK(encoded.diagnostics.rmsCircularPhaseErrorRadians == 0.0);
    CHECK(encoded.diagnostics.maximumCircularPhaseErrorRadians == 0.0);
}

TEST_CASE("circular phase quantization uses nearest code and wraps the top boundary") {
    field::ComplexField2D target(4, 1, 1e-6, 1e-6, 532e-9);
    const double step = 0.5 * std::numbers::pi;
    target.samples()[0] = std::polar(1.0, 0.24 * step);
    target.samples()[1] = std::polar(1.0, 0.76 * step);
    target.samples()[2] = std::polar(1.0, 2.49 * step);
    target.samples()[3] = std::polar(1.0, 3.76 * step);

    const auto encoded = holography::encodePhaseOnlyHologram(
        target, {.bitDepth = 2});

    CHECK(encoded.encodedPhaseRadians.samples()[0] == 0.0);
    CHECK(encoded.encodedPhaseRadians.samples()[1] == doctest::Approx(step));
    CHECK(encoded.encodedPhaseRadians.samples()[2]
        == doctest::Approx(2.0 * step));
    CHECK(encoded.encodedPhaseRadians.samples()[3] == 0.0);
    CHECK(encoded.diagnostics.quantizedPhaseSampleCount == 4);
    CHECK(encoded.diagnostics.maximumCircularPhaseErrorRadians <= 0.5 * step);
}

TEST_CASE("phase-only replay preserves illumination intensity and applies encoded phase") {
    field::ComplexField2D target(3, 2, 2e-6, 3e-6, 638e-9, 1.1);
    field::ComplexField2D illumination(3, 2, 2e-6, 3e-6, 638e-9, 1.1);
    for (std::size_t index = 0; index < target.sampleCount(); ++index) {
        target.samples()[index] = std::polar(
            0.1 + 0.1 * static_cast<double>(index),
            -1.2 + 0.4 * static_cast<double>(index));
        illumination.samples()[index] = {
            0.3 + 0.02 * static_cast<double>(index),
            -0.2 + 0.01 * static_cast<double>(index),
        };
    }
    const auto hologram = holography::encodePhaseOnlyHologram(target);

    const auto replay = holography::replayPhaseOnlyHologram(
        hologram, illumination);

    for (std::size_t index = 0; index < target.sampleCount(); ++index) {
        const auto expected = illumination.samples()[index]
            * std::polar(1.0, std::arg(target.samples()[index]));
        CHECK(std::abs(replay.field.samples()[index] - expected) < 4e-16);
        CHECK(std::norm(replay.field.samples()[index])
            == doctest::Approx(std::norm(illumination.samples()[index])).epsilon(1e-15));
    }
}

TEST_CASE("zero and thresholded targets have explicit invalid phase state") {
    field::ComplexField2D target(4, 1, 1e-6, 1e-6, 532e-9);
    target.samples()[0] = {0.0, 0.0};
    target.samples()[1] = {0.1, 0.0};
    target.samples()[2] = {0.2, 0.0};
    target.samples()[3] = {0.3, 0.0};
    const double threshold = std::norm(target.samples()[2]);
    const auto hologram = holography::encodePhaseOnlyHologram(
        target,
        {
            .phaseOffsetRadians = -0.2,
            .minimumTargetRelativeIntensity = threshold,
        });

    CHECK(hologram.validPhaseMask[0] == 0U);
    CHECK(hologram.validPhaseMask[1] == 0U);
    CHECK(hologram.validPhaseMask[2] == 0U);
    CHECK(hologram.validPhaseMask[3] == 1U);
    CHECK(hologram.diagnostics.invalidPhaseSampleCount == 3);
    CHECK(hologram.diagnostics.validPhaseSampleCount == 1);
    CHECK(hologram.encodedPhaseRadians.samples()[0]
        == doctest::Approx(2.0 * std::numbers::pi - 0.2));
}

TEST_CASE("phase-only encoding and replay reject invalid and incompatible state") {
    field::ComplexField2D target(2, 2, 1e-6, 1e-6, 532e-9);
    target.fill({1.0, 0.0});
    CHECK_THROWS_AS(
        static_cast<void>(holography::encodePhaseOnlyHologram(
            target, {.bitDepth = 53})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::encodePhaseOnlyHologram(
            target,
            {.minimumTargetRelativeIntensity
                = std::numeric_limits<double>::quiet_NaN()})),
        std::invalid_argument);
    auto nonFinite = target;
    nonFinite.samples()[0] = {std::numeric_limits<double>::infinity(), 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(holography::encodePhaseOnlyHologram(nonFinite)),
        std::invalid_argument);

    auto hologram = holography::encodePhaseOnlyHologram(target);
    field::ComplexField2D wrongWavelength(2, 2, 1e-6, 1e-6, 638e-9);
    wrongWavelength.fill({1.0, 0.0});
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayPhaseOnlyHologram(
            hologram, wrongWavelength)),
        std::invalid_argument);
    hologram.validPhaseMask.pop_back();
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayPhaseOnlyHologram(
            hologram, target)),
        std::invalid_argument);
}

} // TEST_SUITE("PhaseOnlyHologram")
