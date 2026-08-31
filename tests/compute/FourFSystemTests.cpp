#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/fourier/FourFSystem.hpp"
#include "core/field/FieldObservables.hpp"

namespace {

namespace fft = holobench::compute::fft;
namespace fourier = holobench::compute::fourier;
namespace field = holobench::field;

constexpr std::size_t gridSize = 8U;
constexpr std::size_t harmonicIndex = 2U;
constexpr double pitchMetres = 10e-6;
constexpr double wavelengthMetres = 500e-9;
constexpr double firstFocalLengthMetres = 0.10;
constexpr double secondFocalLengthMetres = 0.15;

field::ComplexField2D makeDcPlusHarmonicField() {
    field::ComplexField2D value(
        gridSize,
        gridSize,
        pitchMetres,
        pitchMetres,
        wavelengthMetres);
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double phase = 2.0 * std::numbers::pi
                * static_cast<double>(harmonicIndex * x) / static_cast<double>(gridSize);
            value.at(x, y) = {1.0 + 0.75 * std::cos(phase), 0.0};
        }
    }
    return value;
}

double amplitudeContrast(const field::ComplexField2D& value) {
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = 0.0;
    for (const auto& sample : value.samples()) {
        const double amplitude = std::abs(sample);
        minimum = std::min(minimum, amplitude);
        maximum = std::max(maximum, amplitude);
    }
    const double sum = maximum + minimum;
    return sum == 0.0 ? 0.0 : (maximum - minimum) / sum;
}

double fourierPlanePitchMetres() {
    return wavelengthMetres * firstFocalLengthMetres
        / (static_cast<double>(gridSize) * pitchMetres);
}

} // namespace

TEST_SUITE("4-f Fourier filtering") {

TEST_CASE("pass-all 4-f relay preserves energy inversion and analytic magnification") {
    const auto input = makeDcPlusHarmonicField();
    const auto original = input;
    const double inputEnergy = field::computeIntegratedIntensity(input);
    fft::CpuFftBackend backend;
    fourier::FourFSystem system(backend);
    const auto result = system.run(
        input,
        firstFocalLengthMetres,
        secondFocalLengthMetres);

    CHECK(result.filterDiagnostics.kind == fourier::CircularFilterKind::PassAll);
    CHECK(result.filterDiagnostics.transmittedSampleCount == input.sampleCount());
    CHECK(result.filterDiagnostics.blockedSampleCount == 0U);
    CHECK(result.filterDiagnostics.integratedIntensityTransmission
        == doctest::Approx(1.0).epsilon(2e-15));
    CHECK(field::computeIntegratedIntensity(result.imagePlane)
        == doctest::Approx(inputEnergy).epsilon(5e-12));
    CHECK(result.imagePlane.pitchXMetres()
        == doctest::Approx(pitchMetres * secondFocalLengthMetres
            / firstFocalLengthMetres).epsilon(2e-15));

    const auto globalFactor = result.imagePlane.at(0, 0) / input.at(0, 0);
    CHECK(std::abs(globalFactor)
        == doctest::Approx(firstFocalLengthMetres / secondFocalLengthMetres).epsilon(4e-12));
    for (std::size_t y = 0; y < input.height(); ++y) {
        const std::size_t sourceY = (input.height() - y) % input.height();
        for (std::size_t x = 0; x < input.width(); ++x) {
            const std::size_t sourceX = (input.width() - x) % input.width();
            const auto expected = globalFactor * input.at(sourceX, sourceY);
            CHECK(std::abs(result.imagePlane.at(x, y) - expected)
                <= 4e-12 * std::max(1.0, std::abs(expected)));
        }
    }
    CHECK(std::equal(input.samples().begin(), input.samples().end(), original.samples().begin()));
}

TEST_CASE("closing a circular low-pass aperture removes high-frequency image contrast") {
    const auto input = makeDcPlusHarmonicField();
    fft::CpuFftBackend backend;
    fourier::FourFSystem system(backend);
    const double planePitch = fourierPlanePitchMetres();
    const auto wide = system.run(
        input,
        firstFocalLengthMetres,
        secondFocalLengthMetres,
        fourier::CircularFourierFilter::lowPass(
            static_cast<double>(harmonicIndex) * planePitch));
    const auto narrow = system.run(
        input,
        firstFocalLengthMetres,
        secondFocalLengthMetres,
        fourier::CircularFourierFilter::lowPass(1.5 * planePitch));

    CHECK(amplitudeContrast(wide.imagePlane) == doctest::Approx(0.75).epsilon(2e-12));
    CHECK(amplitudeContrast(narrow.imagePlane) < 2e-12);
    CHECK(narrow.filterDiagnostics.outputIntegratedIntensity
        < wide.filterDiagnostics.outputIntegratedIntensity);
    CHECK(narrow.filterDiagnostics.integratedIntensityTransmission < 1.0);
}

TEST_CASE("high-pass central stop blocks a uniform field") {
    field::ComplexField2D input(
        gridSize,
        gridSize,
        pitchMetres,
        pitchMetres,
        wavelengthMetres);
    input.fill({1.0, 0.0});
    fft::CpuFftBackend backend;
    fourier::FourFSystem system(backend);
    const auto result = system.run(
        input,
        firstFocalLengthMetres,
        secondFocalLengthMetres,
        fourier::CircularFourierFilter::highPass(0.5 * fourierPlanePitchMetres()));

    CHECK(result.filterDiagnostics.blockedSampleCount == 1U);
    CHECK(result.filterDiagnostics.outputIntegratedIntensity == 0.0);
    CHECK(result.filterDiagnostics.integratedIntensityTransmission == 0.0);
    for (const auto& sample : result.imagePlane.samples()) {
        CHECK(sample == std::complex<double> {0.0, 0.0});
    }
}

TEST_CASE("band-pass annulus selects the known harmonic and filter factories reject invalid radii") {
    const auto input = makeDcPlusHarmonicField();
    fft::CpuFftBackend backend;
    fourier::FourFSystem system(backend);
    const double planePitch = fourierPlanePitchMetres();
    const auto filter = fourier::CircularFourierFilter::bandPass(
        1.5 * planePitch,
        static_cast<double>(harmonicIndex) * planePitch);
    const auto result = system.run(
        input,
        firstFocalLengthMetres,
        secondFocalLengthMetres,
        filter);

    const std::size_t center = gridSize / 2U;
    CHECK(result.fourierPlaneAfterFilter.at(center, center)
        == std::complex<double> {0.0, 0.0});
    CHECK(std::abs(result.fourierPlaneAfterFilter.at(center - harmonicIndex, center)) > 0.0);
    CHECK(std::abs(result.fourierPlaneAfterFilter.at(center + harmonicIndex, center)) > 0.0);
    CHECK(amplitudeContrast(result.imagePlane) > 0.999999999999);
    CHECK(filter.transmitsRadiusMetres(1.5 * planePitch));
    CHECK(filter.transmitsRadiusMetres(static_cast<double>(harmonicIndex) * planePitch));
    CHECK_FALSE(filter.transmitsRadiusMetres(0.0));

    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularFourierFilter::lowPass(0.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularFourierFilter::highPass(
            std::numeric_limits<double>::infinity())),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularFourierFilter::bandPass(2.0, 1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(fourier::CircularFourierFilter::bandPass(-1.0, 1.0)),
        std::invalid_argument);
}

} // TEST_SUITE("4-f Fourier filtering")
