#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <vector>

#include "compute/fft/CpuFftBackend.hpp"
#include "compute/propagation/FraunhoferPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/wave/FieldElements.hpp"

namespace field = holobench::field;
namespace fft = holobench::compute::fft;
namespace propagation = holobench::compute::propagation;
namespace wave = holobench::optics::wave;

namespace {

constexpr double vacuumWavelength = 532e-9; // 532 nm

// Independent analytic unnormalized sinc: sinc(u) = sin(u) / u
double analyticSinc(double u) noexcept {
    if (std::abs(u) < 1e-8) {
        const double u2 = u * u;
        return 1.0 - u2 / 6.0 + (u2 * u2) / 120.0;
    }
    return std::sin(u) / u;
}

// Independent evaluation of Bessel function J1(v) using power series
double analyticJ1(double v) noexcept {
    if (std::abs(v) < 1e-15) {
        return 0.0;
    }
    const double halfV = 0.5 * v;
    double term = halfV; // m = 0 term: (v/2) / (0! * 1!)
    double sum = term;
    const double halfV2 = halfV * halfV;
    for (int m = 1; m < 50; ++m) {
        term *= -halfV2 / (static_cast<double>(m) * static_cast<double>(m + 1));
        sum += term;
        if (std::abs(term) < 1e-16 * std::abs(sum)) {
            break;
        }
    }
    return sum;
}

// Independent Airy somb factor: 2 * J1(v) / v
double analyticAiryFactor(double v) noexcept {
    if (std::abs(v) < 1e-7) {
        const double v2 = v * v;
        return 1.0 - v2 / 8.0 + (v2 * v2) / 192.0 - (v2 * v2 * v2) / 9216.0;
    }
    return 2.0 * analyticJ1(v) / v;
}

// Independent analytic single slit intensity oracle
double singleSlitAnalyticIntensity(
    double x,
    double y,
    double slitWidth,
    double slitHeight,
    double lambda,
    double distance) noexcept {
    const double uX = (std::numbers::pi * slitWidth * x) / (lambda * distance);
    const double uY = (std::numbers::pi * slitHeight * y) / (lambda * distance);
    const double sincX = analyticSinc(uX);
    const double sincY = analyticSinc(uY);
    const double peakAmplitude = (slitWidth * slitHeight) / (lambda * distance);
    const double peakIntensity = peakAmplitude * peakAmplitude;
    return peakIntensity * (sincX * sincX) * (sincY * sincY);
}

// Independent analytic double slit intensity oracle
double doubleSlitAnalyticIntensity(
    double x,
    double y,
    double slitWidth,
    double slitHeight,
    double separation,
    double lambda,
    double distance) noexcept {
    const double uX = (std::numbers::pi * slitWidth * x) / (lambda * distance);
    const double uY = (std::numbers::pi * slitHeight * y) / (lambda * distance);
    const double cosTerm = std::cos((std::numbers::pi * separation * x) / (lambda * distance));
    const double sincX = analyticSinc(uX);
    const double sincY = analyticSinc(uY);
    const double singleSlitPeakAmp = (slitWidth * slitHeight) / (lambda * distance);
    const double peakIntensity = 4.0 * singleSlitPeakAmp * singleSlitPeakAmp;
    return peakIntensity * (sincX * sincX) * (cosTerm * cosTerm) * (sincY * sincY);
}

// Independent analytic circular aperture Airy intensity oracle
double circularApertureAnalyticIntensity(
    double radius,
    double diameter,
    double lambda,
    double distance) noexcept {
    const double v = (std::numbers::pi * diameter * radius) / (lambda * distance);
    const double airy = analyticAiryFactor(v);
    const double area = std::numbers::pi * 0.25 * diameter * diameter;
    const double peakAmplitude = area / (lambda * distance);
    const double peakIntensity = peakAmplitude * peakAmplitude;
    return peakIntensity * airy * airy;
}

} // namespace

TEST_CASE("Single slit Fraunhofer diffraction matches the independent sinc^2 analytic oracle") {
    constexpr std::size_t sampleCount = 512;
    constexpr double pitchIn = 4e-6; // 4 um pitch keeps the full discrete angular grid paraxial
    constexpr double halfWidth = 32e-6;  // 17 transmitted samples -> effective width = 68 um
    constexpr double halfHeight = 64e-6; // 33 transmitted samples -> effective height = 132 um
    constexpr double effectiveWidth = 17.0 * pitchIn; // 68 um
    constexpr double effectiveHeight = 33.0 * pitchIn; // 132 um
    constexpr double distance = 3.0; // NF ~ 0.0138 and max paraxial parameter ~ 0.094

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::RectangularApertureParameters aperture;
    aperture.halfWidthMetres = halfWidth;
    aperture.halfHeightMetres = halfHeight;
    const auto diagnostics = wave::applyRectangularAperture(field, aperture);
    CHECK(diagnostics.transmittedSampleCount == 17 * 33);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    propagation::FraunhoferOptions options;
    options.illuminatedExtentXMetres = effectiveWidth;
    options.illuminatedExtentYMetres = effectiveHeight;
    const auto result = propagator.propagate(field, distance, options);
    const auto& output = result.field;

    CHECK(result.diagnostics.supportSource == propagation::FraunhoferSupportSource::CallerProvidedExtents);
    CHECK(result.diagnostics.isExact == false);
    REQUIRE(result.diagnostics.fresnelNumberBelowThreshold);
    REQUIRE(result.diagnostics.fresnelNumber <= 0.02);
    REQUIRE(result.diagnostics.paraxialParameterBelowThreshold);
    REQUIRE(result.diagnostics.farFieldConditionSatisfied);
    CHECK(result.diagnostics.warning.find("Fresnel number") == std::string::npos);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = singleSlitAnalyticIntensity(
        0.0, 0.0, effectiveWidth, effectiveHeight, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    // Peak intensity normalized relative error is within floating-point integration precision (< 1e-12)
    const double peakRelError = std::abs(numericalPeak - expectedPeak) / expectedPeak;
    CHECK(peakRelError < 1e-12);

    // First null position on x axis: x_null = lambda * z / effectiveWidth
    const double expectedFirstNullX = (vacuumWavelength * distance) / effectiveWidth;
    const double pitchXOut = output.pitchXMetres();
    const auto nullIndexOffset = static_cast<std::size_t>(std::round(expectedFirstNullX / pitchXOut));
    const auto nullXIndex = centerX + nullIndexOffset;

    REQUIRE(nullXIndex < output.width());
    const double intensityAtNull = std::norm(output.at(nullXIndex, centerY));
    // Discrete sample nearest to continuous null is suppressed below 0.05% of peak
    CHECK(intensityAtNull / numericalPeak < 0.0005);

    // Profile check along central horizontal slice across main lobe and multiple sidelobes
    for (std::size_t p = centerX - 40; p <= centerX + 40; ++p) {
        const double x = output.xCoordinateMetres(p);
        const double numerical = std::norm(output.at(p, centerY));
        const double analytic = singleSlitAnalyticIntensity(
            x, 0.0, effectiveWidth, effectiveHeight, vacuumWavelength, distance);

        // Near zero / across full slice: absolute difference relative to peak is < 0.002 (0.2%)
        const double diffRelToPeak = std::abs(numerical - analytic) / expectedPeak;
        CHECK(diffRelToPeak < 0.002);

        // On significant lobes (> 2% peak), normalized relative error is < 0.025 (2.5%)
        if (analytic > 0.02 * expectedPeak) {
            const double relError = std::abs(numerical - analytic) / analytic;
            CHECK(relError < 0.025);
        }
    }
}

TEST_CASE("Double slit Fraunhofer diffraction matches the independent sinc^2*cos^2 analytic oracle") {
    constexpr std::size_t sampleCount = 512;
    constexpr double pitchIn = 4e-6; // 4 um pitch keeps the full discrete angular grid paraxial
    constexpr double slitWidth = 64e-6; // 17 transmitted samples = 68 um effective width
    constexpr double slitHeight = 128e-6; // 33 transmitted samples = 132 um effective height
    constexpr double effectiveSlitWidth = 17.0 * pitchIn;  // 68 um
    constexpr double effectiveSlitHeight = 33.0 * pitchIn; // 132 um
    constexpr double separation = 256e-6; // 64-sample slit-centre separation
    constexpr double distance = 12.0; // NF ~ 0.0192 and max paraxial parameter ~ 0.094

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::DoubleSlitParameters doubleSlit;
    doubleSlit.slitWidthMetres = slitWidth;
    doubleSlit.slitHeightMetres = slitHeight;
    doubleSlit.centerSeparationMetres = separation;
    const auto diagnostics = wave::applyDoubleSlit(field, doubleSlit);
    CHECK(diagnostics.transmittedSampleCount == 2 * 17 * 33);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    propagation::FraunhoferOptions options;
    options.illuminatedExtentXMetres = separation + effectiveSlitWidth;
    options.illuminatedExtentYMetres = effectiveSlitHeight;
    const auto result = propagator.propagate(field, distance, options);
    const auto& output = result.field;

    CHECK(result.diagnostics.supportSource == propagation::FraunhoferSupportSource::CallerProvidedExtents);
    REQUIRE(result.diagnostics.fresnelNumberBelowThreshold);
    REQUIRE(result.diagnostics.fresnelNumber <= 0.02);
    REQUIRE(result.diagnostics.paraxialParameterBelowThreshold);
    REQUIRE(result.diagnostics.farFieldConditionSatisfied);
    CHECK(result.diagnostics.warning.find("Fresnel number") == std::string::npos);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = doubleSlitAnalyticIntensity(
        0.0, 0.0, effectiveSlitWidth, effectiveSlitHeight, separation, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    // Peak intensity normalized relative error is within floating-point precision (< 1e-12)
    const double peakRelError = std::abs(numericalPeak - expectedPeak) / expectedPeak;
    CHECK(peakRelError < 1e-12);

    // Independently scan the numerical profile along the central horizontal slice to detect bright fringe peaks
    struct DetectedFringePeak {
        std::size_t index;
        double xMetres;
        double intensity;
    };
    std::vector<DetectedFringePeak> detectedPeaks;
    const double minPeakThreshold = 0.05 * numericalPeak;

    for (std::size_t p = centerX - 20; p <= centerX + 20; ++p) {
        const double prev = std::norm(output.at(p - 1, centerY));
        const double curr = std::norm(output.at(p, centerY));
        const double next = std::norm(output.at(p + 1, centerY));
        if (curr > minPeakThreshold && curr >= prev && curr >= next) {
            detectedPeaks.push_back({p, output.xCoordinateMetres(p), curr});
        }
    }

    // Must have found at least 5 bright interference fringes across the central diffraction envelope
    REQUIRE(detectedPeaks.size() >= 5);

    // Verify measured peak-to-peak spacing for adjacent central fringes against theoretical lambda * z / d
    const double expectedFringeSpacing = (vacuumWavelength * distance) / separation;
    for (std::size_t i = 1; i < detectedPeaks.size(); ++i) {
        const double measuredSpacing = detectedPeaks[i].xMetres - detectedPeaks[i - 1].xMetres;
        const double spacingRelError = std::abs(measuredSpacing - expectedFringeSpacing) / expectedFringeSpacing;
        CHECK(spacingRelError < 1e-12);
    }

    // Profile verification across the central interference-diffraction pattern
    for (std::size_t p = centerX - 25; p <= centerX + 25; ++p) {
        const double x = output.xCoordinateMetres(p);
        const double numerical = std::norm(output.at(p, centerY));
        const double analytic = doubleSlitAnalyticIntensity(
            x, 0.0, effectiveSlitWidth, effectiveSlitHeight, separation, vacuumWavelength, distance);

        const double diffRelToPeak = std::abs(numerical - analytic) / expectedPeak;
        CHECK(diffRelToPeak < 0.005);

        if (analytic > 0.05 * expectedPeak) {
            const double relError = std::abs(numerical - analytic) / analytic;
            CHECK(relError < 0.05);
        }
    }

    // Envelope first null: x_null = lambda * z / effectiveSlitWidth
    const double pitchXOut = output.pitchXMetres();
    const double envelopeNullX = (vacuumWavelength * distance) / effectiveSlitWidth;
    const auto envelopeNullIndex = centerX + static_cast<std::size_t>(std::round(envelopeNullX / pitchXOut));
    REQUIRE(envelopeNullIndex < output.width());
    const double intensityAtEnvelopeNull = std::norm(output.at(envelopeNullIndex, centerY));
    CHECK(intensityAtEnvelopeNull / numericalPeak < 0.005);
}

TEST_CASE("Circular aperture Fraunhofer diffraction matches the Airy pattern and 1.22 lambda*z/D first dark ring") {
    constexpr std::size_t sampleCount = 1024;
    constexpr double pitchIn = 4e-6; // 4 um pitch keeps the full discrete angular grid paraxial
    constexpr double diameter = 256e-6; // 32-sample radius
    constexpr double distance = 6.5; // NF ~ 0.0190 and max paraxial parameter ~ 0.094

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, 1.0);
    field.fill({1.0, 0.0});

    wave::CircularApertureParameters circular;
    circular.radiusMetres = 0.5 * diameter;
    const auto maskDiag = wave::applyCircularAperture(field, circular);
    // Number of discrete lattice points with x^2 + y^2 <= 32^2 on integer grid is 3209
    CHECK(maskDiag.transmittedSampleCount == 3209);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    propagation::FraunhoferOptions options;
    options.illuminatedDiameterMetres = diameter;
    const auto result = propagator.propagate(field, distance, options);
    const auto& output = result.field;

    CHECK(result.diagnostics.supportSource == propagation::FraunhoferSupportSource::CallerProvidedDiameter);
    CHECK(std::abs(result.diagnostics.effectiveSupportDiameterMetres - diameter) / diameter < 1e-12);
    const double expectedNf = (diameter * diameter) / (vacuumWavelength * distance);
    CHECK(std::abs(result.diagnostics.fresnelNumber - expectedNf) / expectedNf < 1e-12);
    REQUIRE(result.diagnostics.fresnelNumberBelowThreshold);
    REQUIRE(result.diagnostics.fresnelNumber <= 0.02);
    REQUIRE(result.diagnostics.paraxialParameterBelowThreshold);
    REQUIRE(result.diagnostics.farFieldConditionSatisfied);
    CHECK(result.diagnostics.warning.find("Fresnel number") == std::string::npos);

    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;

    const double expectedPeak = circularApertureAnalyticIntensity(
        0.0, diameter, vacuumWavelength, distance);
    const double numericalPeak = std::norm(output.at(centerX, centerY));

    // Discrete staircase circle area (3209 pixels) vs continuous pi*R^2 (3216.99...) results in
    // peak intensity normalized relative error = 0.496% (< 0.5%)
    const double peakRelError = std::abs(numericalPeak - expectedPeak) / expectedPeak;
    CHECK(peakRelError < 0.005);

    // Analytic first dark ring radius: r1 = 1.21966989 * lambda * z / D
    constexpr double firstZeroCoeff = 3.8317059702075123156 / std::numbers::pi; // ~1.21966989
    const double expectedFirstDarkRingRadius = firstZeroCoeff * (vacuumWavelength * distance) / diameter;
    const double pitchXOut = output.pitchXMetres();

    // Find the first local minimum along the positive X axis
    std::size_t firstMinIndex = 0;
    for (std::size_t p = centerX + 4; p < centerX + 35; ++p) {
        const double prev = std::norm(output.at(p - 1, centerY));
        const double curr = std::norm(output.at(p, centerY));
        const double next = std::norm(output.at(p + 1, centerY));
        if (curr <= prev && curr <= next) {
            firstMinIndex = p;
            break;
        }
    }

    REQUIRE(firstMinIndex != 0);
    const double previousIntensity = std::norm(output.at(firstMinIndex - 1, centerY));
    const double minIntensity = std::norm(output.at(firstMinIndex, centerY));
    const double nextIntensity = std::norm(output.at(firstMinIndex + 1, centerY));
    const double curvature = previousIntensity - 2.0 * minIntensity + nextIntensity;
    REQUIRE(curvature > 0.0);
    const double subPixelOffset = 0.5 * (previousIntensity - nextIntensity) / curvature;
    REQUIRE(std::abs(subPixelOffset) <= 1.0);
    const double numericalFirstDarkRingRadius =
        (static_cast<double>(firstMinIndex - centerX) + subPixelOffset) * pitchXOut;

    // Quadratic sub-pixel localization of the measured numerical minimum makes the first-zero
    // comparison a dimensionless physical gate rather than a grid-resolution-sized allowance.
    const double firstDarkRingRelativeError =
        std::abs(numericalFirstDarkRingRadius - expectedFirstDarkRingRadius)
        / expectedFirstDarkRingRadius;
    CHECK(firstDarkRingRelativeError < 0.005);
    CHECK(minIntensity / numericalPeak < 0.001);

    // Verify radial profile across main lobe and secondary ring
    for (std::size_t p = centerX - 35; p <= centerX + 35; ++p) {
        const double x = output.xCoordinateMetres(p);
        const double numerical = std::norm(output.at(p, centerY));
        const double analytic = circularApertureAnalyticIntensity(
            x, diameter, vacuumWavelength, distance);

        const double diffRelToPeak = std::abs(numerical - analytic) / expectedPeak;
        CHECK(diffRelToPeak < 0.005);

        if (analytic > 0.005 * expectedPeak) {
            const double relError = std::abs(numerical - analytic) / analytic;
            CHECK(relError < 0.09);
        }
    }

    // Verify secondary ring maximum: occurs at v ~ 5.13562 -> r ~ 1.6347 lambda*z/D with I/I0 ~ 0.0175
    const double secondaryRingRadius = (5.1356223 / std::numbers::pi) * (vacuumWavelength * distance) / diameter;
    const auto secondaryIndex = centerX + static_cast<std::size_t>(std::round(secondaryRingRadius / pitchXOut));
    REQUIRE(secondaryIndex < output.width());
    const double secondaryIntensity = std::norm(output.at(secondaryIndex, centerY));
    const double expectedSecondary = circularApertureAnalyticIntensity(
        output.xCoordinateMetres(secondaryIndex), diameter, vacuumWavelength, distance);
    CHECK(std::abs(secondaryIntensity - expectedSecondary) / expectedSecondary < 0.05);
    CHECK(std::abs((secondaryIntensity / numericalPeak) - 0.0175) / 0.0175 < 0.05);
}

TEST_CASE("Fraunhofer propagation scales correctly with medium refractive index") {
    constexpr std::size_t sampleCount = 256;
    constexpr double pitchIn = 4e-6;
    constexpr double halfWidth = 16e-6; // 9 transmitted samples -> effective width 36 um
    constexpr double effectiveWidth = 9.0 * pitchIn;
    constexpr double distance = 0.6;
    constexpr double refractiveIndex = 1.5;

    field::ComplexField2D field(
        sampleCount, sampleCount, pitchIn, pitchIn, vacuumWavelength, refractiveIndex);
    field.fill({1.0, 0.0});

    wave::RectangularApertureParameters aperture;
    aperture.halfWidthMetres = halfWidth;
    aperture.halfHeightMetres = halfWidth;
    wave::applyRectangularAperture(field, aperture);

    fft::CpuFftBackend backend;
    propagation::FraunhoferPropagator propagator(backend);
    propagation::FraunhoferOptions options;
    options.illuminatedExtentXMetres = effectiveWidth;
    options.illuminatedExtentYMetres = effectiveWidth;
    const auto result = propagator.propagate(field, distance, options);
    const auto& output = result.field;

    const double lambdaMedium = vacuumWavelength / refractiveIndex;
    const double expectedFirstNullX = (lambdaMedium * distance) / effectiveWidth;
    const double pitchXOut = output.pitchXMetres();
    const auto centerX = output.width() / 2;
    const auto centerY = output.height() / 2;
    const auto nullOffset = static_cast<std::size_t>(std::round(expectedFirstNullX / pitchXOut));
    const auto nullIndex = centerX + nullOffset;

    const double peakIntensity = std::norm(output.at(centerX, centerY));
    const double nullIntensity = std::norm(output.at(nullIndex, centerY));

    REQUIRE(result.diagnostics.fresnelNumberBelowThreshold);
    REQUIRE(result.diagnostics.fresnelNumber <= 0.02);
    REQUIRE(result.diagnostics.paraxialParameterBelowThreshold);
    REQUIRE(result.diagnostics.farFieldConditionSatisfied);
    CHECK(output.refractiveIndex() == refractiveIndex);
    CHECK(nullIntensity / peakIntensity < 0.0005);
}
