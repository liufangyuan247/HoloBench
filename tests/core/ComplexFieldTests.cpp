#include <doctest/doctest.h>

#include <complex>
#include <limits>
#include <stdexcept>

#include "core/field/ComplexField2D.hpp"

namespace field = holobench::field;

TEST_CASE("complex field validates metadata and uses row-major centered coordinates") {
    field::ComplexField2D value(4, 3, 0.5, 0.25, 500e-9, 1.5);

    CHECK(value.width() == 4);
    CHECK(value.height() == 3);
    CHECK(value.sampleCount() == 12);
    CHECK(value.pitchXMetres() == 0.5);
    CHECK(value.pitchYMetres() == 0.25);
    CHECK(value.vacuumWavelengthMetres() == 500e-9);
    CHECK(value.refractiveIndex() == 1.5);
    CHECK(value.mediumWavenumberRadiansPerMetre() == doctest::Approx(2.0 * 3.14159265358979323846 * 1.5 / 500e-9));

    CHECK(value.xCoordinateMetres(0) == doctest::Approx(-1.0));
    CHECK(value.xCoordinateMetres(2) == doctest::Approx(0.0));
    CHECK(value.xCoordinateMetres(3) == doctest::Approx(0.5));
    CHECK(value.yCoordinateMetres(0) == doctest::Approx(-0.25));
    CHECK(value.yCoordinateMetres(1) == doctest::Approx(0.0));
    CHECK(value.yCoordinateMetres(2) == doctest::Approx(0.25));

    value.at(3, 2) = {4.0, -2.0};
    CHECK(value.samples()[11] == std::complex<double>(4.0, -2.0));
}

TEST_CASE("complex field rejects illegal dimensions and physical metadata") {
    constexpr double nan = std::numeric_limits<double>::quiet_NaN();
    constexpr double infinity = std::numeric_limits<double>::infinity();

    CHECK_THROWS_AS(field::ComplexField2D(0, 1, 1.0, 1.0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(field::ComplexField2D(1, 0, 1.0, 1.0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(field::ComplexField2D(1, 1, 0.0, 1.0, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(field::ComplexField2D(1, 1, 1.0, nan, 1.0), std::invalid_argument);
    CHECK_THROWS_AS(field::ComplexField2D(1, 1, 1.0, 1.0, infinity), std::invalid_argument);
    CHECK_THROWS_AS(field::ComplexField2D(1, 1, 1.0, 1.0, 1.0, -1.0), std::invalid_argument);
    CHECK_THROWS_AS(
        field::ComplexField2D(std::numeric_limits<std::size_t>::max(), 2, 1.0, 1.0, 1.0),
        std::overflow_error);
}

TEST_CASE("complex field bounds checks and fill are deterministic") {
    field::ComplexField2D value(2, 2, 1.0, 1.0, 1.0);
    value.fill({3.0, 4.0});
    for (const auto& sample : value.samples()) {
        CHECK(sample == std::complex<double>(3.0, 4.0));
    }

    CHECK_THROWS_AS(static_cast<void>(value.at(2, 0)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(value.at(0, 2)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(value.xCoordinateMetres(2)), std::out_of_range);
    CHECK_THROWS_AS(static_cast<void>(value.yCoordinateMetres(2)), std::out_of_range);
}
