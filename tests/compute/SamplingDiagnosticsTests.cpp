#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>

#include "compute/sampling/SamplingDiagnostics.hpp"
#include "core/field/ComplexField2D.hpp"

namespace {

namespace sampling = holobench::compute::sampling;
namespace field = holobench::field;

constexpr double degreesToRadians(double value) noexcept {
    return value * std::numbers::pi / 180.0;
}

} // namespace

TEST_SUITE("Sampling diagnostics") {

TEST_CASE("1024-grid teaching example reports physical width Nyquist angle and aliasing") {
    field::ComplexField2D value(1024, 1024, 4e-6, 4e-6, 532e-9);
    sampling::SamplingAnalysisOptions options;
    options.requestedHalfAngleXRadians = degreesToRadians(12.0);
    const auto result = sampling::analyzeSampling(value, options);

    CHECK(result.physicalWidthMetres == doctest::Approx(4.096e-3).epsilon(1e-15));
    CHECK(result.physicalHeightMetres == doctest::Approx(4.096e-3).epsilon(1e-15));
    CHECK(result.nyquistHalfAngleXRadians
        == doctest::Approx(std::asin(532e-9 / (2.0 * 4e-6))).epsilon(1e-15));
    CHECK(result.nyquistHalfAngleXRadians * 180.0 / std::numbers::pi
        == doctest::Approx(3.81298).epsilon(2e-6));
    CHECK(result.spatialAliasingRisk);
    CHECK(result.angularBandwidthInsufficient);
    CHECK(result.warning.find("Nyquist") != std::string::npos);
}

TEST_CASE("support travel distinguishes safe padding from periodic wrap-around") {
    field::ComplexField2D value(256, 128, 8e-6, 10e-6, 633e-9);
    value.fill({0.0, 0.0});
    sampling::SamplingAnalysisOptions safe;
    safe.propagationDistanceMetres = 0.010;
    safe.requestedHalfAngleXRadians = degreesToRadians(2.0);
    safe.requestedHalfAngleYRadians = degreesToRadians(1.0);
    safe.illuminatedExtentXMetres = 1.0e-3;
    safe.illuminatedExtentYMetres = 0.6e-3;
    const auto safeResult = sampling::analyzeSampling(value, safe);
    CHECK_FALSE(safeResult.spatialAliasingRisk);
    CHECK_FALSE(safeResult.wrapAroundRisk);
    CHECK_FALSE(safeResult.insufficientPadding);
    CHECK(safeResult.requiredPaddingFactorX == doctest::Approx(1.0));
    CHECK(safeResult.requiredPaddingFactorY == doctest::Approx(1.0));

    auto unsafe = safe;
    unsafe.propagationDistanceMetres = 0.10;
    const auto unsafeResult = sampling::analyzeSampling(value, unsafe);
    CHECK(unsafeResult.wrapAroundRisk);
    CHECK(unsafeResult.insufficientPadding);
    CHECK(unsafeResult.requiredPaddingFactorX > 1.0);
    CHECK(unsafeResult.requiredPaddingFactorY > 1.0);
    CHECK(unsafeResult.warning.find("wrap") != std::string::npos);
    CHECK(unsafeResult.warning.find("padding") != std::string::npos);
}

TEST_CASE("boundary guard and evanescent bandwidth are reported independently") {
    constexpr double wavelength = 500e-9;
    field::ComplexField2D value(64, 64, wavelength / 4.0, wavelength / 4.0, wavelength);
    value.fill({0.0, 0.0});
    sampling::SamplingAnalysisOptions options;
    options.illuminatedExtentXMetres = static_cast<double>(value.width())
        * value.pitchXMetres() - 2.0 * value.pitchXMetres();
    options.illuminatedExtentYMetres = static_cast<double>(value.height())
        * value.pitchYMetres() - 2.0 * value.pitchYMetres();
    options.minimumBoundaryGuardSamples = 4;
    const auto result = sampling::analyzeSampling(value, options);
    CHECK(result.apertureTooCloseToBoundary);
    CHECK(result.containsEvanescentBins);
    CHECK_FALSE(result.spatialAliasingRisk);
    CHECK(result.warning.find("boundary") != std::string::npos);
    CHECK(result.warning.find("evanescent") != std::string::npos);
}

TEST_CASE("sampled radial bandwidth uses the exact odd-grid FFT bins") {
    constexpr double wavelength = 0.65;
    field::ComplexField2D value(3, 5, 0.4, 0.4, wavelength);
    value.fill({0.0, 0.0});
    sampling::SamplingAnalysisOptions options;
    options.illuminatedExtentXMetres = 0.4;
    options.illuminatedExtentYMetres = 0.4;
    options.minimumBoundaryGuardSamples = 0U;
    const auto result = sampling::analyzeSampling(value, options);

    const double expectedMaximum = std::hypot(1.0 / 1.2, 2.0 / 2.0);
    CHECK(result.maximumSampledRadialFrequencyCyclesPerMetre
        == doctest::Approx(expectedMaximum).epsilon(1e-15));
    CHECK(result.maximumSampledRadialFrequencyCyclesPerMetre
        < std::hypot(1.0 / (2.0 * value.pitchXMetres()),
            1.0 / (2.0 * value.pitchYMetres())));
    CHECK_FALSE(result.containsEvanescentBins);
}

TEST_CASE("invalid options and understated support fail explicitly") {
    field::ComplexField2D value(16, 8, 5e-6, 7e-6, 532e-9);
    value.fill({0.0, 0.0});
    value.at(0, 0) = {1.0, 0.0};

    sampling::SamplingAnalysisOptions incomplete;
    incomplete.illuminatedExtentXMetres = 20e-6;
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeSampling(value, incomplete)),
        std::invalid_argument);

    sampling::SamplingAnalysisOptions understated;
    understated.illuminatedExtentXMetres = 40e-6;
    understated.illuminatedExtentYMetres = 40e-6;
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeSampling(value, understated)),
        std::invalid_argument);

    sampling::SamplingAnalysisOptions invalidAngle;
    invalidAngle.requestedHalfAngleXRadians = std::numbers::pi / 2.0;
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeSampling(value, invalidAngle)),
        std::invalid_argument);
    invalidAngle.requestedHalfAngleXRadians = 0.0;
    invalidAngle.propagationDistanceMetres = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeSampling(value, invalidAngle)),
        std::invalid_argument);

    value.at(0, 0) = {std::numeric_limits<double>::quiet_NaN(), 0.0};
    sampling::SamplingAnalysisOptions finiteSamples;
    finiteSamples.illuminatedExtentXMetres = 80e-6;
    finiteSamples.illuminatedExtentYMetres = 56e-6;
    CHECK_THROWS_AS(
        static_cast<void>(sampling::analyzeSampling(value, finiteSamples)),
        std::invalid_argument);
}

} // TEST_SUITE("Sampling diagnostics")
