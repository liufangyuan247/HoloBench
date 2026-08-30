#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"
#include "core/field/ScalarField2D.hpp"

namespace field = holobench::field;

TEST_CASE("ScalarField2D validates metadata, geometry and bounds") {
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

TEST_CASE("computeNaturalLogIntensity applies explicit floor and computes natural logarithm") {
    field::ComplexField2D field(3, 1, 1e-4, 1e-4, 532e-9, 1.0);
    field.at(0, 0) = {std::numbers::e, 0.0}; // |U|^2 = e^2, ln(I) = 2.0
    field.at(1, 0) = {1.0, 0.0};            // |U|^2 = 1.0, ln(I) = 0.0
    field.at(2, 0) = {0.0, 0.0};            // |U|^2 = 0.0 -> clamped to floor 1e-4

    constexpr double floorValue = 1e-4;
    const auto logI = field::computeNaturalLogIntensity(field, floorValue);

    CHECK(logI.at(0, 0) == doctest::Approx(2.0));
    CHECK(logI.at(1, 0) == doctest::Approx(0.0));
    CHECK(logI.at(2, 0) == doctest::Approx(std::log(floorValue)));

    // Test value smaller than floor is clamped
    field::ComplexField2D weakField(1, 1, 1e-4, 1e-4, 532e-9, 1.0);
    weakField.at(0, 0) = {1e-5, 0.0}; // |U|^2 = 1e-10 < floorValue
    const auto weakLogI = field::computeNaturalLogIntensity(weakField, floorValue);
    CHECK(weakLogI.at(0, 0) == doctest::Approx(std::log(floorValue)));
}

TEST_CASE("computeDecibelIntensity applies floor and reference intensity accurately") {
    field::ComplexField2D field(4, 1, 1e-4, 1e-4, 532e-9, 1.0);
    field.at(0, 0) = {10.0, 0.0}; // I = 100, reference = 1.0 -> 20 dB
    field.at(1, 0) = {1.0, 0.0};  // I = 1, reference = 1.0 -> 0 dB
    field.at(2, 0) = {std::sqrt(0.1), 0.0}; // I = 0.1 -> -10 dB
    field.at(3, 0) = {0.0, 0.0};  // I = 0 -> floor = 1e-6 -> -60 dB

    constexpr double floorValue = 1e-6;
    const auto db = field::computeDecibelIntensity(field, floorValue, 1.0);

    CHECK(db.at(0, 0) == doctest::Approx(20.0));
    CHECK(db.at(1, 0) == doctest::Approx(0.0));
    CHECK(db.at(2, 0) == doctest::Approx(-10.0));
    CHECK(db.at(3, 0) == doctest::Approx(-60.0));

    // Custom reference intensity test: I = 100, reference = 50 -> 10*log10(2) ~= +3.0103 dB
    const auto dbRel = field::computeDecibelIntensity(field, floorValue, 50.0);
    CHECK(dbRel.at(0, 0) == doctest::Approx(10.0 * std::log10(2.0)));
}

TEST_CASE("computeWrappedPhase evaluates principal angle across all quadrants and branch cuts") {
    field::ComplexField2D field(3, 3, 1e-4, 1e-4, 532e-9, 1.0);
    constexpr double pi = std::numbers::pi;

    field.at(0, 0) = {1.0, 0.0};              // 0 rad
    field.at(1, 0) = {1.0, 1.0};              // +pi/4
    field.at(2, 0) = {0.0, 1.0};              // +pi/2
    field.at(0, 1) = {-1.0, 1.0};             // +3pi/4
    field.at(1, 1) = {-1.0, 0.0};             // +pi (standard branch cut positive side)
    field.at(2, 1) = {-1.0, -0.0};            // -pi (standard branch cut negative side)
    field.at(0, 2) = {0.0, -1.0};             // -pi/2
    field.at(1, 2) = {1.0, -1.0};             // -pi/4
    field.at(2, 2) = {0.0, 0.0};              // exact zero -> 0.0 rad

    const auto phase = field::computeWrappedPhase(field);

    CHECK(phase.at(0, 0) == doctest::Approx(0.0));
    CHECK(phase.at(1, 0) == doctest::Approx(pi / 4.0));
    CHECK(phase.at(2, 0) == doctest::Approx(pi / 2.0));
    CHECK(phase.at(0, 1) == doctest::Approx(3.0 * pi / 4.0));
    CHECK(phase.at(1, 1) == doctest::Approx(pi));
    CHECK(phase.at(2, 1) == doctest::Approx(-pi));
    CHECK(phase.at(0, 2) == doctest::Approx(-pi / 2.0));
    CHECK(phase.at(1, 2) == doctest::Approx(-pi / 4.0));
    CHECK(phase.at(2, 2) == 0.0);

    // Verify all samples lie in [-pi, +pi]
    for (const auto val : phase.samples()) {
        CHECK(val >= -pi);
        CHECK(val <= pi);
    }
}

TEST_CASE("computeIntegratedPower scales accurately with transverse grid pitch") {
    // 4x5 grid with constant amplitude 3 + 4i (|U|^2 = 25)
    // dx = 0.5 m, dy = 0.25 m
    // Total power = 25 * (4 * 5) * (0.5 * 0.25) = 25 * 20 * 0.125 = 62.5
    field::ComplexField2D field(4, 5, 0.5, 0.25, 532e-9, 1.0);
    field.fill({3.0, 4.0});

    const double powerFromComplex = field::computeIntegratedPower(field);
    CHECK(powerFromComplex == doctest::Approx(62.5));

    const auto intensity = field::computeIntensity(field);
    const double powerFromScalar = field::computeIntegratedPower(intensity);
    CHECK(powerFromScalar == doctest::Approx(62.5));

    // Zero field gives 0.0
    field.fill({0.0, 0.0});
    CHECK(field::computeIntegratedPower(field) == 0.0);
}

TEST_CASE("observables strictly reject non-finite inputs and invalid parameters") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double inf = std::numeric_limits<double>::infinity();

    field::ComplexField2D field(2, 2, 1e-4, 1e-4, 532e-9, 1.0);
    field.fill({1.0, 0.0});

    // Valid parameters test baseline
    CHECK_NOTHROW(static_cast<void>(field::computeIntensity(field)));
    CHECK_NOTHROW(static_cast<void>(field::computeNaturalLogIntensity(field, 1e-6)));
    CHECK_NOTHROW(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, 1.0)));
    CHECK_NOTHROW(static_cast<void>(field::computeWrappedPhase(field)));
    CHECK_NOTHROW(static_cast<void>(field::computeIntegratedPower(field)));

    // Rejection of invalid floor / reference in log intensity
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, 0.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, -1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, nan)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, inf)), std::invalid_argument);

    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 0.0, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, 0.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, -1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, nan)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, inf)), std::invalid_argument);

    // Rejection of NaN / Inf in ComplexField2D samples
    field.at(1, 1) = {nan, 0.0};
    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, 1e-6)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(field)), std::invalid_argument);

    field.at(1, 1) = {0.0, inf};
    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(field, 1e-6)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(field, 1e-6, 1.0)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeWrappedPhase(field)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(field)), std::invalid_argument);

    // Rejection of negative or non-finite values in computeIntegratedPower(ScalarField2D)
    auto scalar = field::ScalarField2D(2, 2, 1e-4, 1e-4, 532e-9, 1.0);
    scalar.fill(1.0);
    CHECK_NOTHROW(static_cast<void>(field::computeIntegratedPower(scalar)));

    scalar.at(0, 0) = -0.5;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(scalar)), std::invalid_argument);

    scalar.at(0, 0) = nan;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(scalar)), std::invalid_argument);

    scalar.at(0, 0) = inf;
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(scalar)), std::invalid_argument);
}

TEST_CASE("observables detect and reject floating point overflow") {
    field::ComplexField2D hugeField(1, 1, 1.0, 1.0, 532e-9, 1.0);
    // 1e200 squared is 1e400 which exceeds max double (~1.79e308)
    hugeField.at(0, 0) = {1e200, 0.0};

    CHECK_THROWS_AS(static_cast<void>(field::computeIntensity(hugeField)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeNaturalLogIntensity(hugeField, 1e-6)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeDecibelIntensity(hugeField, 1e-6, 1.0)), std::overflow_error);
    CHECK_THROWS_AS(static_cast<void>(field::computeIntegratedPower(hugeField)), std::overflow_error);
}
