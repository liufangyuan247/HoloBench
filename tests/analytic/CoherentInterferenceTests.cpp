#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "core/field/ComplexField2D.hpp"
#include "optics/wave/CoherentInterference.hpp"
#include "optics/wave/FieldSources.hpp"

namespace field = holobench::field;
namespace wave = holobench::optics::wave;

namespace {

field::ComplexField2D makeField(std::size_t width = 3, double pitch = 1.0) {
    field::ComplexField2D result(width, 1, pitch, 1.0, 532e-9);
    result.fill({1.0, 0.0});
    return result;
}

} // namespace

TEST_CASE("fully coherent field combination performs exact complex superposition") {
    auto first = makeField();
    auto second = makeField();
    first.fill({1.0, 2.0});
    second.fill({-0.5, 3.0});

    const auto combined = wave::combineFullyCoherentFields(first, second);

    for (const auto sample : combined.samples()) {
        CHECK(sample == std::complex<double>(0.5, 5.0));
    }
}

TEST_CASE("equal-amplitude two-beam visibility equals degree of coherence") {
    auto first = makeField(2);
    auto second = makeField(2);
    second.at(1, 0) = {-1.0, 0.0};
    wave::MutualCoherenceParameters coherence;
    coherence.zeroDelayDegree = {0.25, 0.0};

    const auto result = wave::evaluateTwoBeamInterference(first, second, coherence);

    CHECK(result.intensity.at(0, 0) == doctest::Approx(2.5).epsilon(1e-15));
    CHECK(result.intensity.at(1, 0) == doctest::Approx(1.5).epsilon(1e-15));
    const double visibility = (result.maximumIntensity - result.minimumIntensity)
        / (result.maximumIntensity + result.minimumIntensity);
    CHECK(visibility == doctest::Approx(0.25).epsilon(1e-15));
}

TEST_CASE("coherence length is the one-over-e path-difference point") {
    wave::MutualCoherenceParameters parameters;
    parameters.zeroDelayDegree = std::polar(0.8, 0.3);
    parameters.opticalPathDifferenceMetres = 0.012;
    parameters.coherenceLengthMetres = 0.012;

    parameters.envelope = wave::CoherenceEnvelope::Gaussian;
    const auto gaussian = wave::mutualDegreeOfCoherence(parameters);
    parameters.envelope = wave::CoherenceEnvelope::Exponential;
    const auto exponential = wave::mutualDegreeOfCoherence(parameters);

    CHECK(std::abs(gaussian) == doctest::Approx(0.8 / std::numbers::e).epsilon(2e-15));
    CHECK(std::abs(exponential) == doctest::Approx(0.8 / std::numbers::e).epsilon(2e-15));
    CHECK(std::arg(gaussian) == doctest::Approx(0.3).epsilon(2e-15));
}

TEST_CASE("symmetric plane waves produce the analytic two-beam fringe period") {
    constexpr double wavelength = 532e-9;
    constexpr double crossingAngle = 0.1;
    const double directionCosine = std::sin(0.5 * crossingAngle);
    const double expectedPeriod = wavelength / (2.0 * directionCosine);
    const double pitch = expectedPeriod / 64.0;
    field::ComplexField2D first(257, 1, pitch, pitch, wavelength);
    field::ComplexField2D second = first;
    wave::PlaneWaveParameters plane;
    plane.directionCosineX = directionCosine;
    wave::fillPlaneWave(first, plane);
    plane.directionCosineX = -directionCosine;
    wave::fillPlaneWave(second, plane);

    const auto result = wave::evaluateTwoBeamInterference(first, second);
    constexpr std::size_t center = 128;

    CHECK(result.intensity.at(center, 0) == doctest::Approx(4.0).epsilon(2e-14));
    CHECK(result.intensity.at(center + 64, 0) == doctest::Approx(4.0).epsilon(2e-13));
    CHECK(result.intensity.at(center + 32, 0) < 2e-28);
    const double measuredPeriod = 64.0 * result.intensity.pitchXMetres();
    CHECK(measuredPeriod == doctest::Approx(expectedPeriod).epsilon(2e-15));

    plane.phaseAtOriginRadians = std::numbers::pi;
    wave::fillPlaneWave(second, plane);
    const auto shifted = wave::evaluateTwoBeamInterference(first, second);
    CHECK(shifted.intensity.at(center, 0) < 2e-30);
    CHECK(shifted.intensity.at(center + 32, 0) == doctest::Approx(4.0).epsilon(2e-13));
}

TEST_CASE("interference rejects incompatible and non-physical inputs") {
    auto first = makeField();
    auto differentPitch = makeField(3, 2.0);
    CHECK_THROWS_AS(
        static_cast<void>(wave::evaluateTwoBeamInterference(first, differentPitch)),
        std::invalid_argument);

    auto nonFinite = first;
    nonFinite.at(0, 0) = {std::numeric_limits<double>::infinity(), 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(wave::combineFullyCoherentFields(first, nonFinite)),
        std::invalid_argument);

    wave::MutualCoherenceParameters coherence;
    coherence.zeroDelayDegree = {1.01, 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(wave::evaluateTwoBeamInterference(first, first, coherence)),
        std::invalid_argument);
    coherence.zeroDelayDegree = {1.0, 0.0};
    coherence.coherenceLengthMetres = 0.0;
    CHECK_THROWS_AS(
        static_cast<void>(wave::mutualDegreeOfCoherence(coherence)),
        std::invalid_argument);
}
