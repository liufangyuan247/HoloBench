#include <doctest/doctest.h>

#include <algorithm>
#include <complex>
#include <stdexcept>

#include "core/field/RgbFieldVisualization.hpp"

namespace field = holobench::field;

TEST_CASE("RGB visualization combines only independent channel intensities") {
    field::ComplexField2D red(2, 1, 1e-6, 1e-6, 638e-9);
    field::ComplexField2D green(2, 1, 1e-6, 1e-6, 532e-9);
    field::ComplexField2D blue(2, 1, 1e-6, 1e-6, 450e-9);
    red.at(0, 0) = {1.0, 0.0};
    green.at(0, 0) = {0.0, 1.0};
    blue.at(0, 0) = {0.0, 0.0};
    red.at(1, 0) = {0.0, 0.0};
    green.at(1, 0) = {0.0, -1.0};
    blue.at(1, 0) = {1.0, 0.0};

    const auto image = field::renderUncalibratedRgbIntensity(
        red, green, blue, {.displayGamma = 1.0});
    CHECK(image.pixel(0, 0) == field::RgbaColor {255, 255, 0, 255});
    CHECK(image.pixel(1, 0) == field::RgbaColor {0, 255, 255, 255});

    green.at(0, 0) = {-1.0, 0.0};
    green.at(1, 0) = {1.0, 0.0};
    const auto phaseChanged = field::renderUncalibratedRgbIntensity(
        red, green, blue, {.displayGamma = 1.0});
    CHECK(std::equal(
        phaseChanged.rgbaBytes().begin(),
        phaseChanged.rgbaBytes().end(),
        image.rgbaBytes().begin(),
        image.rgbaBytes().end()));
}

TEST_CASE("RGB visualization validates grid gains reference and gamma") {
    field::ComplexField2D red(2, 2, 1e-6, 1e-6, 638e-9);
    field::ComplexField2D green(2, 2, 1e-6, 1e-6, 532e-9);
    field::ComplexField2D blue(4, 2, 1e-6, 1e-6, 450e-9);
    CHECK_THROWS_AS(
        static_cast<void>(
            field::renderUncalibratedRgbIntensity(red, green, blue)),
        std::invalid_argument);

    blue = field::ComplexField2D(2, 2, 1e-6, 1e-6, 450e-9);
    CHECK_THROWS_AS(
        static_cast<void>(field::renderUncalibratedRgbIntensity(
            red,
            green,
            blue,
            {.channelIntensityGains = {0.0, 0.0, 0.0}})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(field::renderUncalibratedRgbIntensity(
            red, green, blue, {.displayGamma = 0.0})),
        std::invalid_argument);
}
