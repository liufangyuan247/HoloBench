#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/slm/SpatialLightModulator.hpp"

namespace field = holobench::field;
namespace slm = holobench::optics::slm;

namespace {

field::ComplexField2D makeField(
    std::size_t width = 4,
    std::size_t height = 2,
    double pitch = 1.0) {
    field::ComplexField2D result(width, height, pitch, pitch, 532e-9);
    result.fill({2.0, -1.0});
    return result;
}

std::vector<field::ComplexField2D::Sample> copySamples(const field::ComplexField2D& value) {
    return {value.samples().begin(), value.samples().end()};
}

void checkExactly(
    const field::ComplexField2D& actual,
    const std::vector<field::ComplexField2D::Sample>& expected) {
    REQUIRE(actual.sampleCount() == expected.size());
    CHECK(std::equal(actual.samples().begin(), actual.samples().end(), expected.begin()));
}

} // namespace

TEST_CASE("ideal amplitude SLM has identity and extinction endpoints") {
    auto value = makeField();
    const auto original = copySamples(value);
    std::vector<double> commands(value.sampleCount(), 1.0);

    const auto identity = slm::applyIdealAmplitudeSlm(value, commands);
    CHECK(identity.modulatedSampleCount == value.sampleCount());
    checkExactly(value, original);

    std::fill(commands.begin(), commands.end(), 0.0);
    slm::applyIdealAmplitudeSlm(value, commands);
    for (const auto sample : value.samples()) {
        CHECK(sample == std::complex<double>(0.0, 0.0));
    }
}

TEST_CASE("ideal phase SLM applies global phase and preserves intensity") {
    auto value = makeField();
    const auto original = copySamples(value);
    std::vector<double> phases(value.sampleCount(), 0.5 * std::numbers::pi);

    slm::applyIdealPhaseSlm(value, phases);

    for (std::size_t index = 0; index < value.sampleCount(); ++index) {
        const auto expected = original[index] * std::complex<double>(0.0, 1.0);
        CHECK(std::abs(value.samples()[index] - expected) < 5e-16);
        CHECK(std::norm(value.samples()[index])
            == doctest::Approx(std::norm(original[index])).epsilon(2e-15));
    }
}

TEST_CASE("pixelated amplitude SLM maps centered active areas and opaque dead space") {
    auto value = makeField(9, 3, 0.5);
    slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 2;
    parameters.pixelRows = 1;
    parameters.pixelPitchXMetres = 2.0;
    parameters.pixelPitchYMetres = 2.0;
    parameters.fillFactorX = 0.5;
    parameters.fillFactorY = 1.0;
    parameters.mode = slm::ModulationMode::Amplitude;

    const std::vector<double> commands{0.25, 0.75};
    const auto diagnostics = slm::applyPixelatedSlm(value, parameters, commands);

    CHECK(diagnostics.modulatedSampleCount == 12);
    CHECK(diagnostics.deadSpaceSampleCount == 12);
    CHECK(diagnostics.outsideActiveAreaSampleCount == 3);
    for (std::size_t y = 0; y < value.height(); ++y) {
        CHECK(value.at(1, y) == std::complex<double>(0.5, -0.25));
        CHECK(value.at(2, y) == std::complex<double>(0.5, -0.25));
        CHECK(value.at(5, y) == std::complex<double>(1.5, -0.75));
        CHECK(value.at(6, y) == std::complex<double>(1.5, -0.75));
        CHECK(value.at(0, y) == std::complex<double>(0.0, 0.0));
        CHECK(value.at(8, y) == std::complex<double>(0.0, 0.0));
    }
}

TEST_CASE("pixelated phase SLM quantizes normalized commands to bit-depth endpoints") {
    auto value = makeField(2, 1, 1.0);
    value.fill({1.0, 0.0});
    slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 2;
    parameters.pixelRows = 1;
    parameters.pixelPitchXMetres = 1.0;
    parameters.pixelPitchYMetres = 1.0;
    parameters.centerXMetres = -0.5;
    parameters.bitDepth = 1;
    parameters.phaseRangeRadians = std::numbers::pi;

    const std::vector<double> commands{0.49, 0.51};
    const auto diagnostics = slm::applyPixelatedSlm(value, parameters, commands);

    CHECK(diagnostics.modulatedSampleCount == 2);
    CHECK(diagnostics.quantizedSampleCount == 2);
    CHECK(value.at(0, 0) == std::complex<double>(1.0, 0.0));
    CHECK(std::abs(value.at(1, 0) - std::complex<double>(-1.0, 0.0)) < 2e-16);
}

TEST_CASE("micrometre pixel boundaries retain the locked half-open classification") {
    field::ComplexField2D value(128, 64, 1e-6, 1e-6, 532e-9);
    value.fill({1.0, 0.0});
    slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 16;
    parameters.pixelRows = 8;
    parameters.pixelPitchXMetres = 8e-6;
    parameters.pixelPitchYMetres = 8e-6;
    parameters.fillFactorX = 0.75;
    parameters.fillFactorY = 0.75;
    parameters.centerXMetres = 1e-6;
    parameters.mode = slm::ModulationMode::Amplitude;
    const std::vector<double> commands(16U * 8U, 1.0);

    const auto diagnostics = slm::applyPixelatedSlm(value, parameters, commands);

    CHECK(diagnostics.modulatedSampleCount == 48U * 96U);
    CHECK(diagnostics.deadSpaceSampleCount == 64U * 127U - 48U * 96U);
    CHECK(diagnostics.outsideActiveAreaSampleCount == 64U);
    CHECK(value.at(2, 4) == std::complex<double>(1.0, 0.0));
    CHECK(value.at(1, 4) == std::complex<double>(0.0, 0.0));
    CHECK(value.at(8, 4) == std::complex<double>(0.0, 0.0));
}

TEST_CASE("SLM operations are deterministic and reject bad input without mutation") {
    auto first = makeField(7, 5, 2e-6);
    auto second = first;
    slm::PixelatedSlmParameters parameters;
    parameters.pixelColumns = 3;
    parameters.pixelRows = 2;
    parameters.pixelPitchXMetres = 4e-6;
    parameters.pixelPitchYMetres = 6e-6;
    parameters.fillFactorX = 0.8;
    parameters.fillFactorY = 0.7;
    parameters.bitDepth = 8;
    const std::vector<double> commands{0.0, 0.2, 0.4, 0.6, 0.8, 1.0};
    slm::applyPixelatedSlm(first, parameters, commands);
    slm::applyPixelatedSlm(second, parameters, commands);
    checkExactly(first, copySamples(second));

    auto invalid = makeField();
    const auto before = copySamples(invalid);
    std::vector<double> bad(invalid.sampleCount(), 1.0);
    bad[2] = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(slm::applyIdealAmplitudeSlm(invalid, bad), std::invalid_argument);
    checkExactly(invalid, before);

    parameters.fillFactorX = 1.1;
    CHECK_THROWS_AS(slm::applyPixelatedSlm(invalid, parameters, commands), std::invalid_argument);
    checkExactly(invalid, before);

    invalid.at(0, 0) = {std::numeric_limits<double>::infinity(), 0.0};
    const auto nonFinite = copySamples(invalid);
    std::vector<double> phases(invalid.sampleCount(), 0.0);
    CHECK_THROWS_AS(slm::applyIdealPhaseSlm(invalid, phases), std::invalid_argument);
    checkExactly(invalid, nonFinite);
}
