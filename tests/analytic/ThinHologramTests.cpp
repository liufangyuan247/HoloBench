#include <doctest/doctest.h>

#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>

#include "core/field/ComplexField2D.hpp"
#include "optics/holography/ThinHologram.hpp"
#include "optics/wave/FieldSources.hpp"

namespace field = holobench::field;
namespace holography = holobench::optics::holography;
namespace wave = holobench::optics::wave;

TEST_SUITE("ThinHologram") {

TEST_CASE("constant coherent fields record exact relative intensity and amplitude response") {
    field::ComplexField2D object(4, 3, 2e-6, 3e-6, 532e-9);
    field::ComplexField2D reference(4, 3, 2e-6, 3e-6, 532e-9);
    object.fill({1.0, 0.0});
    reference.fill({0.0, 1.0});

    const auto result = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.1,
            .intensityToAmplitudeGain = 0.2,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 1.0,
        });

    CHECK(result.diagnostics.minimumRecordedRelativeIntensity == doctest::Approx(2.0));
    CHECK(result.diagnostics.maximumRecordedRelativeIntensity == doctest::Approx(2.0));
    CHECK(result.diagnostics.minimumAmplitudeTransmission == doctest::Approx(0.5));
    CHECK(result.diagnostics.maximumAmplitudeTransmission == doctest::Approx(0.5));
    CHECK(result.diagnostics.minimumClampedSampleCount == 0);
    CHECK(result.diagnostics.maximumClampedSampleCount == 0);
    for (std::size_t index = 0; index < result.recordedRelativeIntensity.sampleCount(); ++index) {
        CHECK(result.recordedRelativeIntensity.samples()[index] == doctest::Approx(2.0));
        CHECK(result.amplitudeTransmission.samples()[index] == doctest::Approx(0.5));
    }
}

TEST_CASE("tilted reference records the independent analytic carrier fringe") {
    constexpr double wavelength = 500e-9;
    constexpr double pitch = 1e-6;
    constexpr double directionX = wavelength / (8.0 * pitch);
    constexpr double originPhase = 0.37;
    field::ComplexField2D object(17, 3, pitch, 2e-6, wavelength);
    field::ComplexField2D reference(17, 3, pitch, 2e-6, wavelength);
    wave::fillPlaneWave(object, {});
    wave::fillPlaneWave(reference, {
        .directionCosineX = directionX,
        .phaseAtOriginRadians = originPhase,
    });

    const auto hologram = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.0,
            .intensityToAmplitudeGain = 0.25,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 1.0,
        });

    for (std::size_t y = 0; y < object.height(); ++y) {
        for (std::size_t x = 0; x < object.width(); ++x) {
            const double phase = 2.0 * std::numbers::pi / wavelength
                * directionX * object.xCoordinateMetres(x) + originPhase;
            const double expectedIntensity = 2.0 + 2.0 * std::cos(phase);
            CHECK(hologram.recordedRelativeIntensity.at(x, y)
                == doctest::Approx(expectedIntensity).epsilon(2e-12));
            CHECK(hologram.amplitudeTransmission.at(x, y)
                == doctest::Approx(0.25 * expectedIntensity).epsilon(2e-12));
        }
    }
}

TEST_CASE("response clamps both signed-response extremes and reports them") {
    field::ComplexField2D object(3, 1, 1e-6, 1e-6, 532e-9);
    field::ComplexField2D reference(3, 1, 1e-6, 1e-6, 532e-9);
    object.samples()[0] = {0.0, 0.0};
    object.samples()[1] = {1.0, 0.0};
    object.samples()[2] = {3.0, 0.0};
    reference.fill({0.0, 0.0});

    const auto increasing = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = -0.1,
            .intensityToAmplitudeGain = 0.2,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 0.8,
        });
    CHECK(increasing.amplitudeTransmission.samples()[0] == 0.0);
    CHECK(increasing.amplitudeTransmission.samples()[1] == doctest::Approx(0.1));
    CHECK(increasing.amplitudeTransmission.samples()[2] == 0.8);
    CHECK(increasing.diagnostics.minimumClampedSampleCount == 1);
    CHECK(increasing.diagnostics.maximumClampedSampleCount == 1);

    const auto decreasing = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.8,
            .intensityToAmplitudeGain = -0.1,
            .minimumAmplitudeTransmission = 0.1,
            .maximumAmplitudeTransmission = 0.8,
        });
    CHECK(decreasing.amplitudeTransmission.samples()[0] == 0.8);
    CHECK(decreasing.amplitudeTransmission.samples()[1] == doctest::Approx(0.7));
    CHECK(decreasing.amplitudeTransmission.samples()[2] == 0.1);
    CHECK(decreasing.diagnostics.minimumClampedSampleCount == 1);
}

TEST_CASE("replay matches pointwise thin-mask algebra and permits a new wavelength") {
    field::ComplexField2D object(2, 2, 4e-6, 5e-6, 532e-9);
    field::ComplexField2D reference(2, 2, 4e-6, 5e-6, 532e-9);
    object.samples()[0] = {0.2, -0.1};
    object.samples()[1] = {0.3, 0.4};
    object.samples()[2] = {-0.5, 0.2};
    object.samples()[3] = {0.1, 0.7};
    reference.fill({0.4, -0.2});
    const auto hologram = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.05,
            .intensityToAmplitudeGain = 0.2,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 1.0,
        });
    field::ComplexField2D replay(2, 2, 4e-6, 5e-6, 638e-9, 1.2);
    replay.samples()[0] = {0.7, 0.2};
    replay.samples()[1] = {-0.1, 0.8};
    replay.samples()[2] = {0.5, -0.6};
    replay.samples()[3] = {-0.3, -0.2};

    const auto result = holography::replayThinAmplitudeHologram(hologram, replay);

    CHECK(result.field.vacuumWavelengthMetres() == 638e-9);
    CHECK(result.field.refractiveIndex() == 1.2);
    for (std::size_t index = 0; index < replay.sampleCount(); ++index) {
        const double independentIntensity = std::norm(
            object.samples()[index] + reference.samples()[index]);
        const double transmission = 0.05 + 0.2 * independentIntensity;
        CHECK(result.field.samples()[index].real()
            == doctest::Approx((replay.samples()[index] * transmission).real()).epsilon(1e-12));
        CHECK(result.field.samples()[index].imag()
            == doctest::Approx((replay.samples()[index] * transmission).imag()).epsilon(1e-12));
    }
}

TEST_CASE("explicit unclamped order decomposition sums to full replay") {
    field::ComplexField2D object(3, 2, 4e-6, 5e-6, 532e-9);
    field::ComplexField2D reference(3, 2, 4e-6, 5e-6, 532e-9);
    for (std::size_t index = 0; index < object.sampleCount(); ++index) {
        object.samples()[index] = {
            0.1 + 0.03 * static_cast<double>(index),
            -0.2 + 0.02 * static_cast<double>(index)};
        reference.samples()[index] = {
            0.4 - 0.01 * static_cast<double>(index),
            0.15 + 0.02 * static_cast<double>(index)};
    }
    const auto hologram = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.2,
            .intensityToAmplitudeGain = 0.3,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 1.0,
        });
    const auto replay = holography::makeConjugateReplayField(reference);
    const auto full = holography::replayThinAmplitudeHologram(hologram, replay);
    const auto orders = holography::decomposeUnclampedLinearReplayOrders(
        hologram, object, reference, replay);

    for (std::size_t index = 0; index < full.field.sampleCount(); ++index) {
        const auto sum = orders.zeroOrderField.samples()[index]
            + orders.objectBearingOrderField.samples()[index]
            + orders.conjugateOrderField.samples()[index];
        CHECK(std::abs(sum - full.field.samples()[index]) <= 2e-16);
    }

    auto corrupted = hologram;
    corrupted.recordedRelativeIntensity.samples()[0] += 1e-3;
    CHECK_THROWS_AS(
        static_cast<void>(holography::decomposeUnclampedLinearReplayOrders(
            corrupted, object, reference, replay)),
        std::invalid_argument);

    const auto clipped = holography::recordThinAmplitudeHologram(
        object,
        reference,
        {
            .amplitudeBias = 0.0,
            .intensityToAmplitudeGain = 10.0,
            .minimumAmplitudeTransmission = 0.0,
            .maximumAmplitudeTransmission = 0.1,
        });
    CHECK_THROWS_AS(
        static_cast<void>(holography::decomposeUnclampedLinearReplayOrders(
            clipped, object, reference, replay)),
        std::invalid_argument);
}

TEST_CASE("conjugate replay reverses sample phase and is exactly involutive") {
    field::ComplexField2D reference(3, 2, 2e-6, 2e-6, 450e-9);
    reference.samples()[0] = {1.0, 2.0};
    reference.samples()[1] = {-3.0, 4.0};
    reference.samples()[2] = {0.5, -0.75};
    reference.samples()[3] = {-0.2, -0.4};
    reference.samples()[4] = {0.0, 1.0};
    reference.samples()[5] = {-1.0, 0.0};

    const auto conjugate = holography::makeConjugateReplayField(reference);
    const auto restored = holography::makeConjugateReplayField(conjugate);

    for (std::size_t index = 0; index < reference.sampleCount(); ++index) {
        CHECK(conjugate.samples()[index] == std::conj(reference.samples()[index]));
        CHECK(restored.samples()[index] == reference.samples()[index]);
    }
}

TEST_CASE("recording and replay reject incompatible non-finite and non-physical state") {
    field::ComplexField2D first(2, 2, 1e-6, 1e-6, 532e-9);
    field::ComplexField2D differentPitch(2, 2, 2e-6, 1e-6, 532e-9);
    first.fill({1.0, 0.0});
    differentPitch.fill({1.0, 0.0});
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordThinAmplitudeHologram(first, differentPitch)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordThinAmplitudeHologram(
            first,
            first,
            {.maximumAmplitudeTransmission = 1.1})),
        std::invalid_argument);

    auto nonFinite = first;
    nonFinite.samples()[0] = {std::numeric_limits<double>::infinity(), 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordThinAmplitudeHologram(nonFinite, first)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::makeConjugateReplayField(nonFinite)),
        std::invalid_argument);

    auto hologram = holography::recordThinAmplitudeHologram(first, first);
    hologram.amplitudeTransmission.samples()[0]
        = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS(
        static_cast<void>(holography::replayThinAmplitudeHologram(hologram, first)),
        std::invalid_argument);
    CHECK(first.samples()[0] == std::complex<double>(1.0, 0.0));
}

} // TEST_SUITE("ThinHologram")
