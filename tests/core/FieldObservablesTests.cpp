#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"
#include "core/field/ScalarField2D.hpp"

namespace field = holobench::field;

TEST_CASE("ScalarField2D validates metadata geometry and bounds") {
    field::ScalarField2D scalar(4, 3, 0.5, 0.25, 500e-9, 1.5);

    CHECK(scalar.width() == 4);
    CHECK(scalar.height() == 3);
    CHECK(scalar.sampleCount() == 12);
    CHECK(scalar.pitchXMetres() == 0.5);
    CHECK(scalar.pitchYMetres() == 0.25);
    CHECK(scalar.vacuumWavelengthMetres() == 500e-9);
    CHECK(scalar.refractiveIndex() == 1.5);

    CHECK(scalar.xCoordinateMetres(0) == doctest::Approx(-1.0));
    CHECK(scalar.xCoordinateMetres(2) == doctest::Approx(0.0));
    CHECK(scalar.xCoordinateMetres(3) == doctest::Approx(0.5));
    CHECK(scalar.yCoordinateMetres(0) == doctest::Approx(-0.25));
    CHECK(scalar.yCoordinateMetres(1) == doctest::Approx(0.0));
    CHECK(scalar.yCoordinateMetres(2) == doctest::Approx(0.25));

    scalar.at(3, 2) = 42.5;
    CHECK(scalar.samples()[11] == 42.5);

    scalar.fill(3.14);
    for (const auto& sample : scalar.samples()) {
        CHECK(sample == 3.14);
    }

    CHECK_THROWS_AS(static_cast<void>(scalar.at(4, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(scalar.at(0, 3)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(scalar.xCoordinateMetres(4)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(scalar.yCoordinateMetres(3)), std::out_of_range);

    field::ComplexField2D complexField(2, 2, 0.1, 0.2, 633e-9, 1.33);
    auto matching = field::ScalarField2D::createMatching(complexField);
    CHECK(matching.width() == 2);
    CHECK(matching.height() == 2);
    CHECK(matching.pitchXMetres() == 0.1);
    CHECK(matching.pitchYMetres() == 0.2);
    CHECK(matching.vacuumWavelengthMetres() == 633e-9);
    CHECK(matching.refractiveIndex() == 1.33);
}

TEST_CASE("ScalarField2D constructor rejects illegal dimensions and parameters") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();

    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(0, 1, 1.0, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 0, 1.0, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, 0.0, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, -1.0, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, nan, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, 1.0, inf, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, 1.0, 1.0, -1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::ScalarField2D(1, 1, 1.0, 1.0, 1.0, 0.0)), std::invalid_argument);
}

TEST_CASE("PhaseResult enforces invariant between wrappedPhase and validityMask") {
    field::ScalarField2D phaseField(3, 2, 1e-4, 1e-4, 532e-9, 1.0);
    REQUIRE(phaseField.sampleCount() == 6);

    // Matching size succeeds
    std::vector<std::uint8_t> validMask(6, 1);
    validMask[2] = 0;
    REQUIRE_NOTHROW({
        field::PhaseResult result(phaseField, validMask);
        CHECK(result.wrappedPhaseRadians().sampleCount() == 6);
        CHECK(result.validityMask().size() == 6);
        CHECK(result.isValid(0, 0) == true);
        CHECK(result.isValid(2, 0) == false);
        CHECK(result.isValid(2) == false);
        CHECK(result.isValid(5) == true);
    });

    // Mismatched size (smaller) throws invalid_argument
    std::vector<std::uint8_t> tooSmallMask(5, 1);
    CHECK_THROWS_AS(static_cast<void>(field::PhaseResult(phaseField, tooSmallMask)), std::invalid_argument);

    // Mismatched size (larger) throws invalid_argument
    std::vector<std::uint8_t> tooLargeMask(7, 1);
    CHECK_THROWS_AS(static_cast<void>(field::PhaseResult(phaseField, tooLargeMask)), std::invalid_argument);

    // Empty mask throws invalid_argument when field is non-empty
    std::vector<std::uint8_t> emptyMask;
    CHECK_THROWS_AS(static_cast<void>(field::PhaseResult(phaseField, emptyMask)), std::invalid_argument);

    // Bounds checking on isValid
    field::PhaseResult result(phaseField, validMask);
    CHECK_THROWS_AS(static_cast<void>(result.isValid(3, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(result.isValid(0, 2)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(result.isValid(4, 5)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(result.isValid(6)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(result.isValid(100)), std::out_of_range);
}

TEST_CASE("computeIntensity computes accurate pointwise intensity and preserves metadata") {
    field::ComplexField2D field(3, 2, 1e-4, 2e-4, 532e-9, 1.0);
    field.at(0, 0) = {1.0, 0.0};
    field.at(1, 0) = {0.0, 1.0};
    field.at(2, 0) = {3.0, 4.0};
    field.at(0, 1) = {-2.0, 5.0};
    field.at(1, 1) = {0.0, 0.0};
    field.at(2, 1) = {-0.0, -0.0};

    const auto intensity = field::computeIntensity(field);

    CHECK(intensity.width() == field.width());
    CHECK(intensity.height() == field.height());
    CHECK(intensity.pitchXMetres() == field.pitchXMetres());
    CHECK(intensity.pitchYMetres() == field.pitchYMetres());
    CHECK(intensity.vacuumWavelengthMetres() == field.vacuumWavelengthMetres());
    CHECK(intensity.refractiveIndex() == field.refractiveIndex());

    CHECK(intensity.at(0, 0) == doctest::Approx(1.0));
    CHECK(intensity.at(1, 0) == doctest::Approx(1.0));
    CHECK(intensity.at(2, 0) == doctest::Approx(25.0));
    CHECK(intensity.at(0, 1) == doctest::Approx(29.0));
    CHECK(intensity.at(1, 1) == doctest::Approx(0.0));
    CHECK(intensity.at(2, 1) == doctest::Approx(0.0));

    // Check input field immutability
    CHECK(field.at(2, 0) == std::complex<double>(3.0, 4.0));
    CHECK(field.at(0, 1) == std::complex<double>(-2.0, 5.0));

    // Determinism test: repeat call gives bit-identical results
    const auto intensity2 = field::computeIntensity(field);
    for (std::size_t i = 0; i < intensity.sampleCount(); ++i) {
        CHECK(intensity.samples()[i] == intensity2.samples()[i]);
    }
}

TEST_CASE("computeDecibelIntensity evaluates relative dB with stable log differences and floor clamping") {
    field::ComplexField2D field(5, 1, 1e-4, 1e-4, 532e-9, 1.0);
    field.at(0, 0) = {10.0, 0.0};            // I = 100, reference = 1.0 -> +20 dB
    field.at(1, 0) = {1.0, 0.0};             // I = 1.0, reference = 1.0 -> 0 dB
    field.at(2, 0) = {std::sqrt(0.1), 0.0};  // I = 0.1, reference = 1.0 -> -10 dB
    field.at(3, 0) = {1e-5, 0.0};            // I = 1e-10 -> -100 dB, clamped to floorDecibels = -60 dB
    field.at(4, 0) = {0.0, 0.0};             // exact zero -> clamped directly to floorDecibels = -60 dB

    constexpr double floorDecibels = -60.0;
    const auto db = field::computeDecibelIntensity(field, floorDecibels, 1.0);

    CHECK(db.at(0, 0) == doctest::Approx(20.0));
    CHECK(db.at(1, 0) == doctest::Approx(0.0));
    CHECK(db.at(2, 0) == doctest::Approx(-10.0));
    CHECK(db.at(3, 0) == doctest::Approx(-60.0));
    CHECK(db.at(4, 0) == doctest::Approx(-60.0));

    // Custom reference intensity test: I = 100, reference = 50 -> 10*log10(2) ~= +3.0103 dB
    const auto dbRel = field::computeDecibelIntensity(field, floorDecibels, 50.0);
    CHECK(dbRel.at(0, 0) == doctest::Approx(10.0 * std::log10(2.0)));

    // Determinism test
    const auto db2 = field::computeDecibelIntensity(field, floorDecibels, 1.0);
    for (std::size_t i = 0; i < db.sampleCount(); ++i) {
        CHECK(db.samples()[i] == db2.samples()[i]);
    }
}

TEST_CASE("computeDecibelIntensity handles extreme finite reference and intensity without division overflow/underflow") {
    // Huge intensity with tiny reference: I = 10^200, reference = 10^-200
    // Quotient I/reference = 10^400 would overflow double if divided directly.
    // Using log difference: 10 * (200 - (-200)) = +4000 dB.
    field::ComplexField2D extremeHighField(1, 1, 1e-4, 1e-4, 532e-9, 1.0);
    extremeHighField.at(0, 0) = {1e100, 0.0}; // I = (1e100)^2 = 1e200

    constexpr double tinyReference = 1e-200;
    const auto dbHigh = field::computeDecibelIntensity(extremeHighField, -10000.0, tinyReference);
    CHECK(dbHigh.at(0, 0) == doctest::Approx(4000.0));

    // Tiny intensity with huge reference: I = 10^-200, reference = 10^200
    // Quotient I/reference = 10^-400 would underflow double to 0.0.
    // Clamps cleanly to floorDecibels.
    field::ComplexField2D extremeLowField(1, 1, 1e-4, 1e-4, 532e-9, 1.0);
    extremeLowField.at(0, 0) = {1e-100, 0.0}; // I = 1e-200

    constexpr double hugeReference = 1e200;
    const auto dbLow = field::computeDecibelIntensity(extremeLowField, -120.0, hugeReference);
    CHECK(dbLow.at(0, 0) == doctest::Approx(-120.0));
}

TEST_CASE("computeWrappedPhase normalizes to minus pi to plus pi interval and produces validity mask") {
    field::ComplexField2D field(4, 3, 1e-4, 1e-4, 532e-9, 1.0);
    constexpr double pi = std::numbers::pi;

    // Row 0: Positive angles and axes
    field.at(0, 0) = {1.0, 0.0};              // 0 rad
    field.at(1, 0) = {1.0, 1.0};              // +pi/4
    field.at(2, 0) = {0.0, 1.0};              // +pi/2
    field.at(3, 0) = {-1.0, 1.0};             // +3pi/4

    // Row 1: Negative real axis normalization (+0.0 vs -0.0 imag) -> must both be -pi
    field.at(0, 1) = {-1.0, 0.0};             // negative real -> -pi
    field.at(1, 1) = {-1.0, -0.0};            // negative real with -0.0 imag -> -pi
    field.at(2, 1) = {-1.0, +0.0};            // negative real with +0.0 imag -> -pi
    field.at(3, 1) = {0.0, -1.0};             // -pi/2

    // Row 2: Negative angles and exact zero
    field.at(0, 2) = {1.0, -1.0};             // -pi/4
    field.at(1, 2) = {-1.0, -1.0};            // -3pi/4
    field.at(2, 2) = {0.0, 0.0};              // exact zero -> invalid (mask 0, phase 0.0)
    field.at(3, 2) = {-0.0, -0.0};            // exact zero -> invalid (mask 0, phase 0.0)

    const auto phaseResult = field::computeWrappedPhase(field);
    const auto& phase = phaseResult.wrappedPhaseRadians();
    const auto& mask = phaseResult.validityMask();

    REQUIRE(mask.size() == 12);
    REQUIRE(phase.sampleCount() == 12);

    // Row 0 checks
    CHECK(phase.at(0, 0) == doctest::Approx(0.0));
    CHECK(phaseResult.isValid(0, 0) == true);

    CHECK(phase.at(1, 0) == doctest::Approx(pi / 4.0));
    CHECK(phaseResult.isValid(1, 0) == true);

    CHECK(phase.at(2, 0) == doctest::Approx(pi / 2.0));
    CHECK(phaseResult.isValid(2, 0) == true);

    CHECK(phase.at(3, 0) == doctest::Approx(3.0 * pi / 4.0));
    CHECK(phaseResult.isValid(3, 0) == true);

    // Row 1 checks: negative real axis normalized uniformly to -pi
    CHECK(phase.at(0, 1) == doctest::Approx(-pi));
    CHECK(phaseResult.isValid(0, 1) == true);

    CHECK(phase.at(1, 1) == doctest::Approx(-pi));
    CHECK(phaseResult.isValid(1, 1) == true);

    CHECK(phase.at(2, 1) == doctest::Approx(-pi));
    CHECK(phaseResult.isValid(2, 1) == true);

    CHECK(phase.at(3, 1) == doctest::Approx(-pi / 2.0));
    CHECK(phaseResult.isValid(3, 1) == true);

    // Row 2 checks
    CHECK(phase.at(0, 2) == doctest::Approx(-pi / 4.0));
    CHECK(phaseResult.isValid(0, 2) == true);

    CHECK(phase.at(1, 2) == doctest::Approx(-3.0 * pi / 4.0));
    CHECK(phaseResult.isValid(1, 2) == true);

    // Exact zero samples must have validity 0 and deterministic 0.0 phase
    CHECK(phase.at(2, 2) == 0.0);
    CHECK(phaseResult.isValid(2, 2) == false);
    CHECK(mask[2 + 2 * 4] == 0);

    CHECK(phase.at(3, 2) == 0.0);
    CHECK(phaseResult.isValid(3, 2) == false);
    CHECK(mask[3 + 2 * 4] == 0);

    // Strict half-open interval [-pi, +pi) verification on all valid samples
    for (std::size_t i = 0; i < phase.sampleCount(); ++i) {
        if (phaseResult.isValid(i)) {
            CHECK(phase.samples()[i] >= -pi);
            CHECK(phase.samples()[i] < pi);
        } else {
            CHECK(phase.samples()[i] == 0.0);
        }
    }

    // Bounds checking on isValid
    CHECK_THROWS_AS(static_cast<void>(phaseResult.isValid(4, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(phaseResult.isValid(0, 3)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(phaseResult.isValid(12)), std::out_of_range);
}

TEST_CASE("computeWrappedPhase handles all-zero field sub-threshold intensity and determinism") {
    // All-zero field test
    field::ComplexField2D zeroField(3, 3, 1e-4, 1e-4, 532e-9, 1.0);
    zeroField.fill({0.0, 0.0});

    const auto zeroPhase = field::computeWrappedPhase(zeroField);
    for (std::size_t i = 0; i < zeroPhase.wrappedPhaseRadians().sampleCount(); ++i) {
        CHECK(zeroPhase.wrappedPhaseRadians().samples()[i] == 0.0);
        CHECK(zeroPhase.validityMask()[i] == 0);
        CHECK(zeroPhase.isValid(i) == false);
    }

    // Sub-threshold intensity test with explicit minimumIntensity
    field::ComplexField2D field(3, 1, 1e-4, 1e-4, 532e-9, 1.0);
    field.at(0, 0) = {1.0, 0.0};  // I = 1.0 >= 1e-4 -> valid, phase 0.0
    field.at(1, 0) = {1e-3, 0.0}; // I = 1e-6 < 1e-4 -> invalid (sub-threshold), phase 0.0
    field.at(2, 0) = {0.0, 0.0};  // I = 0.0 -> invalid, phase 0.0

    constexpr double minimumIntensity = 1e-4;
    const auto threshPhase = field::computeWrappedPhase(field, minimumIntensity);

    CHECK(threshPhase.isValid(0, 0) == true);
    CHECK(threshPhase.wrappedPhaseRadians().at(0, 0) == doctest::Approx(0.0));

    CHECK(threshPhase.isValid(1, 0) == false);
    CHECK(threshPhase.wrappedPhaseRadians().at(1, 0) == 0.0);

    CHECK(threshPhase.isValid(2, 0) == false);
    CHECK(threshPhase.wrappedPhaseRadians().at(2, 0) == 0.0);

    // Determinism test: repeated calls produce bit-identical phase and mask
    const auto threshPhase2 = field::computeWrappedPhase(field, minimumIntensity);
    for (std::size_t i = 0; i < threshPhase.wrappedPhaseRadians().sampleCount(); ++i) {
        CHECK(threshPhase.wrappedPhaseRadians().samples()[i] == threshPhase2.wrappedPhaseRadians().samples()[i]);
        CHECK(threshPhase.validityMask()[i] == threshPhase2.validityMask()[i]);
    }
}

TEST_CASE("computeIntegratedIntensity scales accurately with transverse grid pitch") {
    // 4x5 grid with constant amplitude 3 + 4i (|U|^2 = 25)
    // dx = 0.5 m, dy = 0.25 m
    // Total integrated relative intensity = 25 * (4 * 5) * (0.5 * 0.25) = 25 * 20 * 0.125 = 62.5
    field::ComplexField2D field(4, 5, 0.5, 0.25, 532e-9, 1.0);
    field.fill({3.0, 4.0});

    const double intensityFromComplex = field::computeIntegratedIntensity(field);
    CHECK(intensityFromComplex == doctest::Approx(62.5));

    const auto intensity = field::computeIntensity(field);
    const double intensityFromScalar = field::computeIntegratedIntensity(intensity);
    CHECK(intensityFromScalar == doctest::Approx(62.5));

    // Zero field gives 0.0
    field.fill({0.0, 0.0});
    CHECK(field::computeIntegratedIntensity(field) == 0.0);

    // Determinism test
    field.fill({1.5, -2.5});
    const double integrated1 = field::computeIntegratedIntensity(field);
    const double integrated2 = field::computeIntegratedIntensity(field);
    CHECK(integrated1 == integrated2);
}

TEST_CASE("observables strictly reject non-finite inputs and invalid parameters") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();

    field::ComplexField2D field(2, 2, 1e-4, 1e-4, 532e-9, 1.0);
    field.fill({1.0, 0.0});

    // Valid parameters baseline
    CHECK_NOTHROW(static_cast<void>(field::computeIntensity(field)));
    CHECK_NOTHROW(static_cast<void>(field::computeDecibelIntensity(field, -120.0, 1.0)));
    CHECK_NOTHROW(static_cast<void>(field::computeWrappedPhase(field, 0.0)));
    CHECK_NOTHROW(static_cast<void>(field::computeIntegratedIntensity(field)));

    // Rejection of invalid floorDecibels (> 0.0 or non-finite) in computeDecibelIntensity
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, nan, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, inf, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -inf, 1.0)), std::invalid_argument);

    // Rejection of invalid referenceIntensity (<= 0.0 or non-finite) in computeDecibelIntensity
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, 0.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, -1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, nan)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, inf)), std::invalid_argument);

    // Rejection of negative or non-finite minimumIntensity in computeWrappedPhase
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field, -1e-6)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field, nan)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field, inf)), std::invalid_argument);

    // Rejection of NaN / Inf in ComplexField2D samples
    field.at(1, 1) = {nan, 0.0};
    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(field)), std::invalid_argument);

    field.at(1, 1) = {0.0, inf};
    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, -120.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(field)), std::invalid_argument);

    // Rejection of negative or non-finite values in computeIntegratedIntensity(ScalarField2D)
    auto scalar = field::ScalarField2D(2, 2, 1e-4, 1e-4, 532e-9, 1.0);
    scalar.fill(1.0);
    CHECK_NOTHROW(static_cast<void>(field::computeIntegratedIntensity(scalar)));

    scalar.at(0, 0) = -0.5;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(scalar)), std::invalid_argument);

    scalar.at(0, 0) = nan;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(scalar)), std::invalid_argument);

    scalar.at(0, 0) = inf;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(scalar)), std::invalid_argument);
}

TEST_CASE("observables detect and reject floating point overflow") {
    field::ComplexField2D hugeField(1, 1, 1.0, 1.0, 532e-9, 1.0);
    // 1e200 squared is 1e400 which exceeds max double (~1.79e308)
    hugeField.at(0, 0) = {1e200, 0.0};

    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(hugeField)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(hugeField, -120.0, 1.0)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(hugeField)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedIntensity(hugeField)), std::overflow_error);
}
