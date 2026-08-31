#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/sampling/PlaneProbe.hpp"
#include "core/field/ComplexField2D.hpp"

namespace {

namespace fft = holobench::compute::fft;
namespace sampling = holobench::compute::sampling;
namespace field = holobench::field;

} // namespace

TEST_SUITE("arbitrary-plane probe") {

TEST_CASE("fixed transverse probe follows analytic plane-wave phase across positive and negative z") {
    field::ComplexField2D source(8U, 4U, 10e-6, 12e-6, 532e-9, 1.33);
    source.fill({1.0, 0.0});
    const auto original = source;
    const std::vector<double> distances {0.0, 1e-3, -1e-3};
    fft::CpuFftBackend backend;
    const auto result = sampling::probeAngularSpectrumPlanes(
        source, 3U, 1U, distances, backend);

    REQUIRE(result.samples.size() == distances.size());
    CHECK(result.xCoordinateMetres == doctest::Approx(source.xCoordinateMetres(3U)));
    CHECK(result.yCoordinateMetres == doctest::Approx(source.yCoordinateMetres(1U)));
    for (std::size_t index = 0; index < distances.size(); ++index) {
        const auto expected = std::polar(
            1.0,
            source.mediumWavenumberRadiansPerMetre() * distances[index]);
        CHECK(std::abs(result.samples[index].fieldValue - expected) <= 2e-12);
        CHECK(result.samples[index].intensity == doctest::Approx(1.0).epsilon(3e-12));
        CHECK(result.samples[index].phaseValid);
    }
    CHECK(result.samples.front().fieldValue == std::complex<double> {1.0, 0.0});
    CHECK(result.samples.front().propagatingBinCount == source.sampleCount());
    CHECK(std::equal(source.samples().begin(), source.samples().end(), original.samples().begin()));
}

TEST_CASE("zero field has deterministic invalid phase at every requested plane") {
    constexpr double wavelength = 633e-9;
    field::ComplexField2D source(
        4U, 4U, wavelength / 4.0, wavelength / 4.0, wavelength);
    source.fill({0.0, 0.0});
    const std::vector<double> distances {0.0, 0.01};
    fft::CpuFftBackend backend;
    const auto result = sampling::probeAngularSpectrumPlanes(
        source, 2U, 2U, distances, backend);
    for (const auto& sample : result.samples) {
        CHECK(sample.fieldValue == std::complex<double> {0.0, 0.0});
        CHECK(sample.intensity == 0.0);
        CHECK(sample.wrappedPhaseRadians == 0.0);
        CHECK_FALSE(sample.phaseValid);
        CHECK(sample.evanescentBinCount > 0U);
        CHECK(sample.propagatingBinCount + sample.evanescentBinCount == source.sampleCount());
    }
}

TEST_CASE("plane probe rejects empty non-finite unsupported and out-of-range requests") {
    field::ComplexField2D source(4U, 4U, 5e-6, 5e-6, 633e-9);
    source.fill({1.0, 0.0});
    fft::CpuFftBackend backend;
    const std::vector<double> empty;
    CHECK_THROWS_AS(
        static_cast<void>(sampling::probeAngularSpectrumPlanes(
            source, 0U, 0U, empty, backend)),
        std::invalid_argument);
    const std::vector<double> zero {0.0};
    CHECK_THROWS_AS(
        static_cast<void>(sampling::probeAngularSpectrumPlanes(
            source, 4U, 0U, zero, backend)),
        std::out_of_range);
    const std::vector<double> nonFinite {std::numeric_limits<double>::infinity()};
    CHECK_THROWS_AS(
        static_cast<void>(sampling::probeAngularSpectrumPlanes(
            source, 0U, 0U, nonFinite, backend)),
        std::invalid_argument);

    field::ComplexField2D unsupported(3U, 2U, 5e-6, 5e-6, 633e-9);
    unsupported.fill({1.0, 0.0});
    const std::vector<double> propagated {0.01};
    CHECK_THROWS_AS(
        static_cast<void>(sampling::probeAngularSpectrumPlanes(
            unsupported, 0U, 0U, propagated, backend)),
        std::invalid_argument);
}

} // TEST_SUITE("arbitrary-plane probe")
