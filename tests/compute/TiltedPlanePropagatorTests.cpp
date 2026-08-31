#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <numbers>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"
#include "core/field/ComplexField2D.hpp"

namespace fft = holobench::compute::fft;
namespace propagation = holobench::compute::propagation;
namespace field = holobench::field;
namespace math = holobench::math;

TEST_CASE("tilted-plane rotation maps an exact spectral bin between physical axes") {
    field::ComplexField2D value(32U, 32U, 20e-6, 20e-6, 532e-9);
    constexpr std::size_t sourceXBin = 2U;
    constexpr std::size_t sourceYBin = 1U;
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double phase = 2.0 * std::numbers::pi
                * (static_cast<double>(sourceXBin * x) / 32.0
                    + static_cast<double>(sourceYBin * y) / 32.0);
            value.at(x, y) = std::polar(1.0, phase);
        }
    }
    const math::RigidTransform3d input {};
    const math::RigidTransform3d output {
        .translationMetres = {0.0, 0.0, 0.01},
        .localXAxisInWorld = {0.0, 1.0, 0.0},
        .localYAxisInWorld = {-1.0, 0.0, 0.0},
        .localZAxisInWorld = {0.0, 0.0, 1.0},
    };
    const double fx = static_cast<double>(sourceXBin)
        / (32.0 * value.pitchXMetres());
    const double fy = static_cast<double>(sourceYBin)
        / (32.0 * value.pitchYMetres());
    const double cutoff = 1.0 / value.vacuumWavelengthMetres();
    const double fz = std::sqrt(cutoff * cutoff - fx * fx - fy * fy);
    const auto axialPhase = std::polar(
        1.0, std::remainder(
            2.0 * std::numbers::pi * fz * 0.01,
            2.0 * std::numbers::pi));

    fft::CpuFftBackend backend;
    propagation::TiltedPlanePropagator propagator(backend);
    const auto diagnostics = propagator.propagateInPlace(
        value, input, output, {0.0, 0.0, 1.0});

    CHECK(diagnostics.propagatingOutputBinCount
            + diagnostics.sourceBandRejectedBinCount
        == value.sampleCount());
    CHECK(diagnostics.sourceBandRejectedBinCount == value.height());
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const double expectedPhase = 2.0 * std::numbers::pi
                * (static_cast<double>(sourceYBin * x) / 32.0
                    - static_cast<double>(sourceXBin * y) / 32.0);
            CHECK(std::abs(
                value.at(x, y) - std::polar(1.0, expectedPhase) * axialPhase)
                < 3e-10);
        }
    }
}

TEST_CASE("tilted-plane propagation rejects a grazing preferred direction") {
    field::ComplexField2D value(8U, 8U, 10e-6, 10e-6, 532e-9);
    value.fill({1.0, 0.0});
    fft::CpuFftBackend backend;
    propagation::TiltedPlanePropagator propagator(backend);
    const math::RigidTransform3d plane {};
    CHECK_THROWS_AS(
        propagator.propagateInPlace(
            value, plane, plane, {1.0, 0.0, 0.0}),
        std::invalid_argument);
}

TEST_CASE("tilted-plane propagation rejects an unresolved output carrier") {
    field::ComplexField2D value(16U, 16U, 10e-6, 10e-6, 532e-9);
    value.fill({1.0, 0.0});
    constexpr double sine = 0.1;
    const double cosine = std::sqrt(1.0 - sine * sine);
    const math::RigidTransform3d input {};
    const math::RigidTransform3d output {
        .translationMetres = {0.0, 0.0, 0.01},
        .localXAxisInWorld = {cosine, 0.0, -sine},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {sine, 0.0, cosine},
    };
    fft::CpuFftBackend backend;
    propagation::TiltedPlanePropagator propagator(backend);
    CHECK_THROWS_AS(
        propagator.propagateInPlace(
            value, input, output, {0.0, 0.0, 1.0}),
        std::invalid_argument);
}

TEST_CASE("tilted-plane propagation matches an oblique plane-wave oracle") {
    constexpr std::size_t samples = 64U;
    constexpr double pitch = 10e-6;
    constexpr double wavelength = 100e-6;
    constexpr double sine = 0.3125;
    const double cosine = std::sqrt(1.0 - sine * sine);
    field::ComplexField2D value(
        samples, samples, pitch, pitch, wavelength);
    value.fill({1.0, 0.0});
    const math::RigidTransform3d input {};
    const math::RigidTransform3d output {
        .translationMetres = {0.0, 0.0, 0.01},
        .localXAxisInWorld = {cosine, 0.0, -sine},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {sine, 0.0, cosine},
    };

    fft::CpuFftBackend backend;
    propagation::TiltedPlanePropagator propagator(backend);
    const auto diagnostics = propagator.propagateInPlace(
        value, input, output, {0.0, 0.0, 1.0});

    REQUIRE(diagnostics.propagatingOutputBinCount > 0U);
    const double outputCarrier = -sine / wavelength;
    for (std::size_t y = 0; y < value.height(); ++y) {
        for (std::size_t x = 0; x < value.width(); ++x) {
            const auto expected = std::polar(
                1.0,
                2.0 * std::numbers::pi
                    * outputCarrier * value.xCoordinateMetres(x));
            CHECK(std::abs(value.at(x, y) - expected) < 3e-10);
        }
    }
}
