#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"
#include "core/field/FieldVisualization.hpp"
#include "core/field/ScalarField2D.hpp"

using namespace holobench::field;

TEST_SUITE("FieldVisualization") {

TEST_CASE("zero field visualization produces deterministic expected images") {
    constexpr std::size_t width = 16;
    constexpr std::size_t height = 16;
    constexpr double pitch = 1e-5;
    constexpr double wavelength = 532e-9;

    ComplexField2D field(width, height, pitch, pitch, wavelength, 1.0);
    field.fill(std::complex<double>(0.0, 0.0));

    // Linear intensity
    FieldVisualizationOptions options;
    options.colormap = ColormapKind::Grayscale;
    const auto linearImg = renderLinearIntensity(field, options);
    CHECK(linearImg.width() == width);
    CHECK(linearImg.height() == height);
    CHECK(linearImg.pixelCount() == width * height);
    CHECK(linearImg.byteCount() == width * height * 4);

    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto px = linearImg.pixel(x, y);
            CHECK(px.r == 0);
            CHECK(px.g == 0);
            CHECK(px.b == 0);
            CHECK(px.a == 255);
        }
    }

    // Decibel log intensity: all zeros should evaluate to floorDecibels
    options.floorDecibels = -80.0;
    options.maxDecibels = 0.0;
    options.decibelReferenceIntensity = 1.0;
    const auto dbImg = renderDecibelIntensity(field, options);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto px = dbImg.pixel(x, y);
            CHECK(px.r == 0);
            CHECK(px.g == 0);
            CHECK(px.b == 0);
            CHECK(px.a == 255);
        }
    }

    // Wrapped phase: zero field has undefined phase (validity = 0), so rendered with invalidColor
    options.invalidColor = RgbaColor{10, 20, 30, 255};
    const auto phaseImg = renderWrappedPhase(field, options);
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto px = phaseImg.pixel(x, y);
            CHECK(px == options.invalidColor);
        }
    }
}

TEST_CASE("dynamic range and decibel floor normalization mapping") {
    constexpr std::size_t width = 4;
    constexpr std::size_t height = 1;
    ComplexField2D field(width, height, 1e-5, 1e-5, 633e-9, 1.0);

    // Amplitudes: 1.0 (0 dB), 0.1 (-20 dB), 0.01 (-40 dB), 0.001 (-60 dB)
    field.at(0, 0) = std::complex<double>(1.0, 0.0);
    field.at(1, 0) = std::complex<double>(0.1, 0.0);
    field.at(2, 0) = std::complex<double>(0.01, 0.0);
    field.at(3, 0) = std::complex<double>(0.001, 0.0);

    FieldVisualizationOptions options;
    options.colormap = ColormapKind::Grayscale;
    options.floorDecibels = -60.0;
    options.maxDecibels = 0.0;
    options.decibelReferenceIntensity = 1.0;

    const auto img = renderDecibelIntensity(field, options);

    // 0 dB -> normalized 1.0 -> 255
    CHECK(img.pixel(0, 0).r == 255);
    // -20 dB -> normalized (-20 - (-60)) / 60 = 40/60 = 2/3 -> round(255 * 2 / 3) = 170
    CHECK(img.pixel(1, 0).r == 170);
    // -40 dB -> normalized (-40 - (-60)) / 60 = 20/60 = 1/3 -> round(255 * 1 / 3) = 85
    CHECK(img.pixel(2, 0).r == 85);
    // -60 dB -> normalized (-60 - (-60)) / 60 = 0 -> 0
    CHECK(img.pixel(3, 0).r == 0);
}

TEST_CASE("wrapped phase boundary continuity and cyclic color wheel") {
    constexpr double pi = std::numbers::pi;

    // Boundary at -pi
    const auto colMinusPi = evaluateCyclicPhaseColormap(-pi);
    // Boundary approaching +pi (e.g. pi - 1e-7)
    const auto colNearPlusPi = evaluateCyclicPhaseColormap(pi - 1e-7);

    // Both should evaluate to the exact same continuous hue (Pure Red: 255, 0, 0)
    CHECK(colMinusPi.r == 255);
    CHECK(colMinusPi.g == 0);
    CHECK(colMinusPi.b == 0);
    CHECK(colMinusPi.a == 255);

    CHECK(colNearPlusPi.r == 255);
    CHECK(colNearPlusPi.g == 0);
    CHECK(colNearPlusPi.b == 0);
    CHECK(colNearPlusPi.a == 255);

    // Phase at -pi/3 (-60 deg -> Hue 120 deg -> Green)
    const auto colGreen = evaluateCyclicPhaseColormap(-pi / 3.0);
    CHECK(colGreen.r == 0);
    CHECK(colGreen.g == 255);
    CHECK(colGreen.b == 0);

    // Phase at +pi/3 (+60 deg -> Hue 240 deg -> Blue)
    const auto colBlue = evaluateCyclicPhaseColormap(pi / 3.0);
    CHECK(colBlue.r == 0);
    CHECK(colBlue.g == 0);
    CHECK(colBlue.b == 255);
}

TEST_CASE("validity mask handling masks invalid pixels cleanly") {
    constexpr std::size_t width = 4;
    constexpr std::size_t height = 2;
    ComplexField2D field(width, height, 1e-5, 1e-5, 532e-9, 1.0);

    // Set uniform non-zero amplitude
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            field.at(x, y) = std::complex<double>(1.0, 0.0);
        }
    }
    // Set (1, 0) and (2, 1) below threshold
    field.at(1, 0) = std::complex<double>(1e-4, 0.0); // intensity 1e-8
    field.at(2, 1) = std::complex<double>(0.0, 0.0);  // zero

    FieldVisualizationOptions options;
    options.phaseMinimumIntensity = 1e-4; // threshold > 1e-8
    options.invalidColor = RgbaColor{77, 88, 99, 255};

    const auto phaseImg = renderWrappedPhase(field, options);

    // Valid sample at (0, 0): phase = 0 (Hue = 180 deg -> Cyan)
    CHECK(phaseImg.pixel(0, 0) != options.invalidColor);
    // Invalid sample at (1, 0)
    CHECK(phaseImg.pixel(1, 0) == options.invalidColor);
    // Invalid sample at (2, 1)
    CHECK(phaseImg.pixel(2, 1) == options.invalidColor);
    // Valid sample at (3, 1)
    CHECK(phaseImg.pixel(3, 1) != options.invalidColor);
}

TEST_CASE("rectangular grid orientation and row-major layout are preserved without flipping or transposing") {
    constexpr std::size_t width = 12;
    constexpr std::size_t height = 5;
    ComplexField2D field(width, height, 1e-5, 2e-5, 532e-9, 1.0);
    field.fill(std::complex<double>(0.0, 0.0));

    // Place asymmetric bright spot at (x=9, y=2)
    field.at(9, 2) = std::complex<double>(5.0, 0.0);

    FieldVisualizationOptions options;
    options.colormap = ColormapKind::Grayscale;
    const auto img = renderLinearIntensity(field, options);

    CHECK(img.width() == width);
    CHECK(img.height() == height);

    // Verify hotspot is at (9, 2) and nowhere else
    for (std::size_t y = 0; y < height; ++y) {
        for (std::size_t x = 0; x < width; ++x) {
            const auto px = img.pixel(x, y);
            if (x == 9 && y == 2) {
                CHECK(px.r == 255);
                CHECK(px.g == 255);
                CHECK(px.b == 255);
            } else {
                CHECK(px.r == 0);
                CHECK(px.g == 0);
                CHECK(px.b == 0);
            }
        }
    }

    // Verify exact byte buffer layout: index = (y * width + x) * 4
    const auto bytes = img.rgbaBytes();
    const std::size_t hotspotByteIdx = (2 * width + 9) * 4;
    CHECK(bytes[hotspotByteIdx + 0] == 255);
    CHECK(bytes[hotspotByteIdx + 1] == 255);
    CHECK(bytes[hotspotByteIdx + 2] == 255);
    CHECK(bytes[hotspotByteIdx + 3] == 255);
}

TEST_CASE("parameter validation and NaN/Inf rejection") {
    ComplexField2D field(4, 4, 1e-5, 1e-5, 532e-9, 1.0);
    field.fill(std::complex<double>(1.0, 0.0));

    FieldVisualizationOptions badOptions;
    // Invalid dB floor >= maxDecibels
    badOptions.floorDecibels = 0.0;
    badOptions.maxDecibels = -10.0;
    CHECK_THROWS_AS(static_cast<void>(renderDecibelIntensity(field, badOptions)), std::invalid_argument);

    badOptions.floorDecibels = -60.0;
    badOptions.maxDecibels = 0.0;
    badOptions.decibelReferenceIntensity = -1.0; // non-positive reference
    CHECK_THROWS_AS(static_cast<void>(renderDecibelIntensity(field, badOptions)), std::invalid_argument);

    // Non-finite field sample rejection
    ComplexField2D nanField(4, 4, 1e-5, 1e-5, 532e-9, 1.0);
    nanField.fill(std::complex<double>(1.0, 0.0));
    nanField.at(1, 1) = std::complex<double>(std::numeric_limits<double>::quiet_NaN(), 0.0);

    CHECK_THROWS_AS(static_cast<void>(renderLinearIntensity(nanField)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(renderDecibelIntensity(nanField)), std::invalid_argument);
    CHECK_THROWS_AS(static_cast<void>(renderWrappedPhase(nanField)), std::invalid_argument);

    // Verify input fields are const and unmodified
    CHECK(field.at(0, 0) == std::complex<double>(1.0, 0.0));

    FieldVisualizationOptions invalidLinearOptions;
    invalidLinearOptions.maxIntensityReference = -1.0;
    CHECK_THROWS_AS(
        static_cast<void>(renderLinearIntensity(field, invalidLinearOptions)),
        std::invalid_argument);
    invalidLinearOptions.maxIntensityReference = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(
        static_cast<void>(renderLinearIntensity(field, invalidLinearOptions)),
        std::invalid_argument);

    FieldVisualizationOptions invalidPhaseOptions;
    invalidPhaseOptions.phaseMinimumIntensity = -1.0;
    CHECK_THROWS_AS(
        static_cast<void>(renderWrappedPhase(field, invalidPhaseOptions)),
        std::invalid_argument);
    invalidPhaseOptions.phaseMinimumIntensity = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS(
        static_cast<void>(renderWrappedPhase(field, invalidPhaseOptions)),
        std::invalid_argument);
}

TEST_CASE("RGBA image rejects byte-count overflow before validating the supplied buffer") {
    const auto overflowingWidth = std::numeric_limits<std::size_t>::max() / 4U + 1U;
    CHECK_THROWS_AS(
        static_cast<void>(RgbaImage(overflowingWidth, 1, {})),
        std::overflow_error);
}

TEST_CASE("unified renderFieldView dispatches accurately across all modes") {
    ComplexField2D field(8, 8, 1e-5, 1e-5, 532e-9, 1.0);
    field.fill(std::complex<double>(1.0, 0.0));

    FieldVisualizationOptions options;
    options.colormap = ColormapKind::Grayscale;

    const auto linear = renderFieldView(field, FieldViewMode::Intensity, options);
    const auto db = renderFieldView(field, FieldViewMode::DecibelIntensity, options);
    const auto phase = renderFieldView(field, FieldViewMode::WrappedPhase, options);

    CHECK(linear.width() == 8);
    CHECK(db.width() == 8);
    CHECK(phase.width() == 8);
}

} // TEST_SUITE("FieldVisualization")
