#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <filesystem>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/slm/SlmResponse.hpp"
#include "optics/slm/SlmResponseIO.hpp"

namespace field = holobench::field;
namespace slm = holobench::optics::slm;

namespace {

slm::CalibratedSlmResponse makeResponse() {
    return slm::CalibratedSlmResponse({
        {
            .vacuumWavelengthMetres = 500e-9,
            .commandResponse = {
                {0.0, 0.2, 0.0},
                {1.0, 1.0, std::numbers::pi},
            },
        },
        {
            .vacuumWavelengthMetres = 600e-9,
            .commandResponse = {
                {0.0, 0.4, 0.5 * std::numbers::pi},
                {1.0, 0.8, 1.5 * std::numbers::pi},
            },
        },
    });
}

field::ComplexField2D makeTwoPixelField(double wavelength = 550e-9) {
    field::ComplexField2D value(2, 1, 1.0, 1.0, wavelength);
    value.fill({1.0, 0.0});
    return value;
}

std::vector<std::complex<double>> copySamples(const field::ComplexField2D& value) {
    return {value.samples().begin(), value.samples().end()};
}

void checkExactly(
    const field::ComplexField2D& actual,
    const std::vector<std::complex<double>>& expected) {
    CHECK(std::equal(actual.samples().begin(), actual.samples().end(), expected.begin()));
}

} // namespace

TEST_CASE("calibrated SLM response interpolates command amplitude phase and wavelength") {
    const auto response = makeResponse();

    const auto evaluated = response.evaluate(550e-9, 0.5);

    CHECK(evaluated.amplitudeTransmission == doctest::Approx(0.6).epsilon(2e-15));
    CHECK(evaluated.unwrappedPhaseDelayRadians
        == doctest::Approx(0.75 * std::numbers::pi).epsilon(2e-15));
    const auto transfer = evaluated.complexTransfer();
    CHECK(std::abs(transfer) == doctest::Approx(0.6).epsilon(2e-15));
    CHECK(std::arg(transfer) == doctest::Approx(0.75 * std::numbers::pi).epsilon(2e-15));
}

TEST_CASE("calibrated pixelated SLM applies wavelength response after bit-depth quantization") {
    auto value = makeTwoPixelField();
    slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 2;
    parameters.pixelRows = 1;
    parameters.pixelPitchXMetres = 1.0;
    parameters.pixelPitchYMetres = 1.0;
    parameters.centerXMetres = -0.5;
    parameters.bitDepth = 1;
    const std::vector<double> commands{0.49, 0.51};

    const auto diagnostics = slm::applyCalibratedPixelatedSlm(
        value, parameters, commands, makeResponse());

    CHECK(diagnostics.modulatedSampleCount == 2);
    CHECK(diagnostics.quantizedSampleCount == 2);
    const auto expectedFirst = 0.3 * std::polar(1.0, 0.25 * std::numbers::pi);
    const auto expectedSecond = 0.9 * std::polar(1.0, 1.25 * std::numbers::pi);
    CHECK(std::abs(value.at(0, 0) - expectedFirst) < 2e-15);
    CHECK(std::abs(value.at(1, 0) - expectedSecond) < 2e-15);
}

TEST_CASE("LCD Jones teaching model reproduces crossed-polarizer retardance endpoints") {
    slm::LcdTeachingParameters lcd;
    lcd.colorFilterPattern = slm::LcdColorFilterPattern::Monochrome;
    lcd.inputPolarizerAngleRadians = 0.0;
    lcd.analyzerAngleRadians = 0.5 * std::numbers::pi;
    lcd.liquidCrystalFastAxisAngleRadians = 0.25 * std::numbers::pi;
    lcd.zeroCommandRetardanceRadians = std::numbers::pi;
    lcd.fullCommandRetardanceRadians = 0.0;

    const auto bright = slm::evaluateLcdTeachingTransfer(
        lcd, slm::LcdColorChannel::Green, 532e-9, 0.0);
    const auto dark = slm::evaluateLcdTeachingTransfer(
        lcd, slm::LcdColorChannel::Green, 532e-9, 1.0);

    CHECK(std::abs(bright) == doctest::Approx(1.0).epsilon(2e-15));
    CHECK(std::abs(dark) < 1e-15);
}

TEST_CASE("LCD RGB patterns select independently interpolated filter amplitudes") {
    slm::LcdTeachingParameters lcd;
    lcd.inputPolarizerAngleRadians = 0.0;
    lcd.analyzerAngleRadians = 0.0;
    lcd.liquidCrystalFastAxisAngleRadians = 0.0;
    lcd.zeroCommandRetardanceRadians = 0.0;
    lcd.fullCommandRetardanceRadians = 0.0;
    lcd.colorFilterPattern = slm::LcdColorFilterPattern::VerticalRgbStripes;
    lcd.spectralTransmission = {
        {500e-9, 1.0, 0.2, 0.1},
        {600e-9, 0.5, 0.8, 0.2},
    };
    field::ComplexField2D value(3, 1, 1.0, 1.0, 550e-9);
    value.fill({1.0, 0.0});
    slm::PixelatedSlmParameters pixels;
    pixels.pixelColumns = 3;
    pixels.pixelRows = 1;
    pixels.pixelPitchXMetres = 1.0;
    pixels.pixelPitchYMetres = 1.0;
    const std::vector<double> commands(3, 0.5);

    slm::applyLcdTeachingSlm(value, pixels, commands, lcd);

    CHECK(std::abs(value.at(0, 0) - std::complex<double>(0.75, 0.0)) < 1e-15);
    CHECK(std::abs(value.at(1, 0) - std::complex<double>(0.5, 0.0)) < 1e-15);
    CHECK(std::abs(value.at(2, 0) - std::complex<double>(0.15, 0.0)) < 1e-15);
    CHECK(slm::lcdColorChannelAt(0, 0, slm::LcdColorFilterPattern::BayerRggb)
        == slm::LcdColorChannel::Red);
    CHECK(slm::lcdColorChannelAt(1, 1, slm::LcdColorFilterPattern::BayerRggb)
        == slm::LcdColorChannel::Blue);
}

TEST_CASE("SLM calibration JSON is semantic and byte stable") {
    const auto response = makeResponse();
    const std::string first = slm::serializeSlmResponseJson(response);
    const auto restored = slm::deserializeSlmResponseJson(first);
    const std::string second = slm::serializeSlmResponseJson(restored);

    CHECK(second == first);
    CHECK(restored.evaluate(550e-9, 0.5).amplitudeTransmission
        == doctest::Approx(0.6).epsilon(2e-15));

    const auto path = std::filesystem::temp_directory_path()
        / ("holobench-slm-response-roundtrip-"
            + std::to_string(reinterpret_cast<std::uintptr_t>(&response)) + ".json");
    slm::saveSlmResponseJson(path, response);
    const auto loaded = slm::loadSlmResponseJson(path);
    CHECK(slm::serializeSlmResponseJson(loaded) == first);
    std::error_code removeError;
    std::filesystem::remove(path, removeError);
    CHECK_FALSE(removeError);
}

TEST_CASE("SLM response models reject invalid calibration and preserve fields on error") {
    CHECK_THROWS_AS(
        slm::CalibratedSlmResponse({{
            .vacuumWavelengthMetres = 532e-9,
            .commandResponse = {{0.1, 1.0, 0.0}, {1.0, 1.0, 0.0}},
        }}),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(makeResponse().evaluate(700e-9, 0.5)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(slm::deserializeSlmResponseJson(
            R"({"format_version":1,"model":"scalar_complex_response_lut","wavelength_curves":[],"unknown":1})")),
        std::invalid_argument);

    auto value = makeTwoPixelField(700e-9);
    const auto before = copySamples(value);
    slm::PixelatedSlmParameters pixels;
    pixels.pixelColumns = 2;
    pixels.pixelRows = 1;
    pixels.pixelPitchXMetres = 1.0;
    pixels.pixelPitchYMetres = 1.0;
    pixels.centerXMetres = -0.5;
    const std::vector<double> commands{0.0, 1.0};
    CHECK_THROWS_AS(
        slm::applyCalibratedPixelatedSlm(value, pixels, commands, makeResponse()),
        std::out_of_range);
    checkExactly(value, before);

    slm::LcdTeachingParameters invalidLcd;
    invalidLcd.colorFilterPattern = slm::LcdColorFilterPattern::VerticalRgbStripes;
    CHECK_THROWS_AS(
        slm::applyLcdTeachingSlm(value, pixels, commands, invalidLcd),
        std::invalid_argument);
    checkExactly(value, before);
}
