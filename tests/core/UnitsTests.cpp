#include <doctest/doctest.h>

#include "core/units/Units.hpp"

using holobench::units::Length;

TEST_CASE("length conversions preserve SI values") {
    constexpr auto wavelength = Length::fromNanometres(532.0);
    CHECK(wavelength.metres() == doctest::Approx(532e-9));
    CHECK(wavelength.micrometres() == doctest::Approx(0.532));
    CHECK(wavelength.nanometres() == doctest::Approx(532.0));

    constexpr auto focalLength = Length::fromMillimetres(50.0);
    CHECK(focalLength.metres() == doctest::Approx(0.05));
    CHECK(focalLength.millimetres() == doctest::Approx(50.0));
}

TEST_CASE("length algebra stays strongly typed") {
    constexpr auto first = Length::fromMillimetres(10.0);
    constexpr auto second = Length::fromMillimetres(5.0);
    CHECK((first + second).millimetres() == doctest::Approx(15.0));
    CHECK((first - second).millimetres() == doctest::Approx(5.0));
    CHECK((second * 3.0).millimetres() == doctest::Approx(15.0));
    CHECK((first / 2.0).millimetres() == doctest::Approx(5.0));
    CHECK((first / second) == doctest::Approx(2.0));
}
