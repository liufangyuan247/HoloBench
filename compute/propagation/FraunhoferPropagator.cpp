#include "compute/propagation/FraunhoferPropagator.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <cstdint>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::propagation {
namespace {

void validateFiniteSamples(const field::ComplexField2D& value, const char* message) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error(message);
        }
    }
}

/// Evaluates products and quotients without transient overflow/underflow. A mathematically non-zero
/// result outside the representable double domain is reported rather than silently rounded to zero.
[[nodiscard]] double stableProduct(
    std::initializer_list<double> numerators,
    std::initializer_list<double> denominators = {}) {
    for (const double numerator : numerators) {
        if (!std::isfinite(numerator)) {
            throw std::overflow_error("Fraunhofer product contains a non-finite numerator");
        }
    }
    for (const double denominator : denominators) {
        if (!std::isfinite(denominator) || denominator == 0.0) {
            throw std::overflow_error("Fraunhofer product contains a non-finite or zero denominator");
        }
    }
    for (const double numerator : numerators) {
        if (numerator == 0.0) {
            return 0.0;
        }
    }

    double mantissa = 1.0;
    int exponent = 0;
    for (const double numerator : numerators) {
        int factorExponent = 0;
        const double factorMantissa = std::frexp(numerator, &factorExponent);
        mantissa *= factorMantissa;
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += factorExponent + normalizationExponent;
    }
    for (const double denominator : denominators) {
        int factorExponent = 0;
        const double factorMantissa = std::frexp(denominator, &factorExponent);
        mantissa /= factorMantissa;
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += normalizationExponent - factorExponent;
    }

    if (mantissa == 0.0) {
        throw std::underflow_error("Fraunhofer product underflows the double domain");
    }
    if (exponent > std::numeric_limits<double>::max_exponent) {
        throw std::overflow_error("Fraunhofer product exceeds the finite double domain");
    }

    constexpr int minimumDenormalExponent =
        std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits + 1;
    if (exponent < minimumDenormalExponent
        || (exponent == minimumDenormalExponent && std::abs(mantissa) < 0.5)) {
        throw std::underflow_error("Fraunhofer product underflows the double domain");
    }

    const double result = std::ldexp(mantissa, exponent);
    if (!std::isfinite(result)) {
        throw std::overflow_error("Fraunhofer product exceeds the finite double domain");
    }
    if (result == 0.0) {
        throw std::underflow_error("Fraunhofer product underflows the double domain");
    }
    return result;
}

[[nodiscard]] bool supportCoordinateContained(double coordinate, double halfExtent) noexcept {
    const double scale = std::max(std::abs(coordinate), halfExtent);
    const double roundoffAllowance = 16.0 * std::numeric_limits<double>::epsilon() * scale;
    return std::abs(coordinate) <= halfExtent + roundoffAllowance;
}

void validateClaimedSupport(
    const field::ComplexField2D& value,
    const FraunhoferOptions& options) {
    const bool hasDiameter = options.illuminatedDiameterMetres.has_value();
    const bool hasExtents = options.illuminatedExtentXMetres.has_value()
        && options.illuminatedExtentYMetres.has_value();
    if (!hasDiameter && !hasExtents) {
        return;
    }

    const double halfDiameter = hasDiameter ? 0.5 * *options.illuminatedDiameterMetres : 0.0;
    const double halfExtentX = hasExtents ? 0.5 * *options.illuminatedExtentXMetres : 0.0;
    const double halfExtentY = hasExtents ? 0.5 * *options.illuminatedExtentYMetres : 0.0;

    for (std::size_t yIndex = 0; yIndex < value.height(); ++yIndex) {
        const double y = value.yCoordinateMetres(yIndex);
        for (std::size_t xIndex = 0; xIndex < value.width(); ++xIndex) {
            const auto& sample = value.at(xIndex, yIndex);
            if (sample.real() == 0.0 && sample.imag() == 0.0) {
                continue;
            }

            const double x = value.xCoordinateMetres(xIndex);
            if (hasDiameter) {
                const double radius = std::hypot(x, y);
                if (!supportCoordinateContained(radius, halfDiameter)) {
                    throw std::invalid_argument(
                        "Fraunhofer illuminated diameter excludes a non-zero input sample");
                }
            } else if (!supportCoordinateContained(x, halfExtentX)
                || !supportCoordinateContained(y, halfExtentY)) {
                throw std::invalid_argument(
                    "Fraunhofer illuminated extents exclude a non-zero input sample");
            }
        }
    }
}

void validateInput(
    const field::ComplexField2D& value,
    double distanceMetres,
    const fft::IFftBackend& backend,
    const FraunhoferOptions& options) {
    if (!std::isfinite(distanceMetres) || distanceMetres <= 0.0) {
        throw std::invalid_argument("Fraunhofer propagation distance must be positive and finite");
    }
    if (!backend.supportsDimensions(value.width(), value.height())) {
        throw std::invalid_argument("FFT backend does not support the field dimensions");
    }
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("Fraunhofer input samples must be finite");
        }
    }

    const bool hasDiameter = options.illuminatedDiameterMetres.has_value();
    const bool hasExtentX = options.illuminatedExtentXMetres.has_value();
    const bool hasExtentY = options.illuminatedExtentYMetres.has_value();

    if (hasDiameter && (hasExtentX || hasExtentY)) {
        throw std::invalid_argument(
            "Fraunhofer options cannot specify both illuminated diameter and extents");
    }

    if (hasDiameter) {
        const double d = *options.illuminatedDiameterMetres;
        if (!std::isfinite(d) || d <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated diameter must be positive and finite");
        }
    } else if (hasExtentX || hasExtentY) {
        if (!hasExtentX || !hasExtentY) {
            throw std::invalid_argument(
                "Fraunhofer options must specify both X and Y extents when using extents mode");
        }
        const double ex = *options.illuminatedExtentXMetres;
        if (!std::isfinite(ex) || ex <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated X extent must be positive and finite");
        }
        const double ey = *options.illuminatedExtentYMetres;
        if (!std::isfinite(ey) || ey <= 0.0) {
            throw std::invalid_argument("Fraunhofer illuminated Y extent must be positive and finite");
        }
    }

    validateClaimedSupport(value, options);

    const double lambda = value.vacuumWavelengthMetres() / value.refractiveIndex();
    if (!std::isfinite(lambda) || lambda <= 0.0) {
        throw std::invalid_argument("Fraunhofer medium wavelength must be positive and finite");
    }

    const double wavenumber = value.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("Fraunhofer medium wavenumber must be positive and finite");
    }

    static_cast<void>(stableProduct({wavenumber, distanceMetres}));

    const auto width = value.width();
    const auto height = value.height();
    const double pitchXIn = value.pitchXMetres();
    const double pitchYIn = value.pitchYMetres();

    const double denomX = static_cast<double>(width) * pitchXIn;
    const double denomY = static_cast<double>(height) * pitchYIn;
    if (denomX <= 0.0 || denomY <= 0.0 || !std::isfinite(denomX) || !std::isfinite(denomY)) {
        throw std::invalid_argument("Fraunhofer input grid dimensions and pitches must be positive and finite");
    }

    const double lambdaZ = stableProduct({lambda, distanceMetres});

    const double pitchXOut = stableProduct({lambdaZ}, {denomX});
    const double pitchYOut = stableProduct({lambdaZ}, {denomY});
    if (!std::isfinite(pitchXOut) || pitchXOut <= 0.0 || !std::isfinite(pitchYOut) || pitchYOut <= 0.0) {
        throw std::overflow_error("Fraunhofer output pitch computation overflowed or resulted in a non-positive value");
    }

    const double areaScale = stableProduct({pitchXIn, pitchYIn}, {lambdaZ});
    if (!std::isfinite(areaScale) || areaScale <= 0.0) {
        throw std::overflow_error("Fraunhofer amplitude scale computation overflowed or resulted in a non-positive value");
    }

    double effectiveD = 0.0;
    if (hasDiameter) {
        effectiveD = *options.illuminatedDiameterMetres;
    } else if (hasExtentX && hasExtentY) {
        effectiveD = std::hypot(*options.illuminatedExtentXMetres, *options.illuminatedExtentYMetres);
    } else {
        effectiveD = std::hypot(denomX, denomY);
    }

    if (!std::isfinite(effectiveD) || effectiveD <= 0.0) {
        throw std::overflow_error("Fraunhofer effective support diameter computation overflowed or is non-positive");
    }

    const double fresnelNumber = stableProduct({effectiveD, effectiveD}, {lambda, distanceMetres});
    if (!std::isfinite(fresnelNumber) || fresnelNumber < 0.0) {
        throw std::overflow_error("Fraunhofer Fresnel number computation overflowed");
    }

    const std::size_t mMaxX = std::max(width / 2, (width > 0) ? (width - 1 - width / 2) : 0);
    const std::size_t mMaxY = std::max(height / 2, (height > 0) ? (height - 1 - height / 2) : 0);
    const double maxX = static_cast<double>(mMaxX) * pitchXOut;
    const double maxY = static_cast<double>(mMaxY) * pitchYOut;
    const double maxR = std::hypot(maxX, maxY);
    const double maxParaxialParam = stableProduct({maxR}, {distanceMetres});
    if (!std::isfinite(maxParaxialParam) || maxParaxialParam < 0.0) {
        throw std::overflow_error("Fraunhofer maximum paraxial parameter computation overflowed");
    }

    const double factorX = (mMaxX > 0) ? static_cast<double>(2 * mMaxX - 1) : 0.0;
    const double factorY = (mMaxY > 0) ? static_cast<double>(2 * mMaxY - 1) : 0.0;
    const double maxStepX = (factorX == 0.0)
        ? 0.0
        : stableProduct({std::numbers::pi, factorX, pitchXOut}, {denomX});
    const double maxStepY = (factorY == 0.0)
        ? 0.0
        : stableProduct({std::numbers::pi, factorY, pitchYOut}, {denomY});
    const double maxAdjacentPhaseStep = std::max(maxStepX, maxStepY);
    if (!std::isfinite(maxAdjacentPhaseStep) || maxAdjacentPhaseStep < 0.0) {
        throw std::overflow_error("Fraunhofer maximum adjacent phase step computation overflowed");
    }
}

} // namespace

FraunhoferPropagator::FraunhoferPropagator(fft::IFftBackend& fftBackend) noexcept
    : fftBackend_(fftBackend) {
}

FraunhoferResult FraunhoferPropagator::propagate(
    const field::ComplexField2D& field,
    double distanceMetres,
    const FraunhoferOptions& options) const {
    validateInput(field, distanceMetres, fftBackend_, options);

    const auto width = field.width();
    const auto height = field.height();
    const double lambda0 = field.vacuumWavelengthMetres();
    const double n = field.refractiveIndex();
    const double lambda = lambda0 / n;
    const double wavenumber = field.mediumWavenumberRadiansPerMetre();

    const double pitchXIn = field.pitchXMetres();
    const double pitchYIn = field.pitchYMetres();
    const double denomX = static_cast<double>(width) * pitchXIn;
    const double denomY = static_cast<double>(height) * pitchYIn;
    const double lambdaZ = stableProduct({lambda, distanceMetres});
    const double pitchXOut = stableProduct({lambdaZ}, {denomX});
    const double pitchYOut = stableProduct({lambdaZ}, {denomY});

    // Forward FFT on a copy of the input field to guarantee strong exception safety.
    auto transformed = field;
    fftBackend_.forward2D(transformed);
    validateFiniteSamples(transformed, "Fraunhofer forward FFT produced a non-finite spectrum");

    field::ComplexField2D output(width, height, pitchXOut, pitchYOut, lambda0, n);

    const double amplitudeScale = stableProduct({pitchXIn, pitchYIn}, {lambdaZ});
    const double quadraticPhaseFactor = stableProduct({wavenumber}, {2.0, distanceMetres});
    if (!std::isfinite(quadraticPhaseFactor) || quadraticPhaseFactor <= 0.0) {
        throw std::overflow_error("Fraunhofer quadratic phase factor computation overflowed");
    }

    const double axialPhase = stableProduct({wavenumber, distanceMetres}) - std::numbers::pi / 2.0;
    if (!std::isfinite(axialPhase)) {
        throw std::overflow_error("Fraunhofer axial phase computation overflowed");
    }

    const std::size_t mMaxX = std::max(width / 2, (width > 0) ? (width - 1 - width / 2) : 0);
    const std::size_t mMaxY = std::max(height / 2, (height > 0) ? (height - 1 - height / 2) : 0);
    const double maxX = static_cast<double>(mMaxX) * pitchXOut;
    const double maxY = static_cast<double>(mMaxY) * pitchYOut;
    const double maxR2 = maxX * maxX + maxY * maxY;
    if (!std::isfinite(maxR2) || !std::isfinite(quadraticPhaseFactor * maxR2)) {
        throw std::overflow_error("Fraunhofer quadratic phase computation overflowed");
    }

    const auto centerX = width / 2;
    const auto centerY = height / 2;

    for (std::size_t q = 0; q < height; ++q) {
        const double yOut = output.yCoordinateMetres(q);
        const auto v = (q >= centerY) ? (q - centerY) : (q + height - centerY);
        const double shiftPhaseY = 2.0 * std::numbers::pi
            * (static_cast<double>(static_cast<std::int64_t>(q) - static_cast<std::int64_t>(centerY))
                * static_cast<double>(centerY) / static_cast<double>(height));

        for (std::size_t p = 0; p < width; ++p) {
            const double xOut = output.xCoordinateMetres(p);
            const auto u = (p >= centerX) ? (p - centerX) : (p + width - centerX);
            const double shiftPhaseX = 2.0 * std::numbers::pi
                * (static_cast<double>(static_cast<std::int64_t>(p) - static_cast<std::int64_t>(centerX))
                    * static_cast<double>(centerX) / static_cast<double>(width));

            const double quadPhaseX = (xOut == 0.0)
                ? 0.0
                : stableProduct({quadraticPhaseFactor, xOut, xOut});
            const double quadPhaseY = (yOut == 0.0)
                ? 0.0
                : stableProduct({quadraticPhaseFactor, yOut, yOut});
            const double quadPhase = quadPhaseX + quadPhaseY;
            if (!std::isfinite(quadPhase)) {
                throw std::overflow_error("Fraunhofer quadratic phase sample overflowed");
            }

            const double totalPhase = axialPhase + quadPhase + shiftPhaseX + shiftPhaseY;
            if (!std::isfinite(totalPhase)) {
                throw std::overflow_error("Fraunhofer total phase sample overflowed");
            }

            const double reducedPhase = std::remainder(totalPhase, 2.0 * std::numbers::pi);
            const auto& fftSample = transformed.at(u, v);
            if (fftSample.real() != 0.0 || fftSample.imag() != 0.0) {
                static_cast<void>(stableProduct({amplitudeScale, std::abs(fftSample)}));
            }
            output.at(p, q) = (amplitudeScale * fftSample) * std::polar(1.0, reducedPhase);
            if ((fftSample.real() != 0.0 || fftSample.imag() != 0.0)
                && output.at(p, q).real() == 0.0 && output.at(p, q).imag() == 0.0) {
                throw std::underflow_error("Fraunhofer output sample underflows the complex double domain");
            }
        }
    }

    validateFiniteSamples(output, "Fraunhofer propagation produced non-finite output samples");

    const double maxR = std::hypot(maxX, maxY);
    const double maxParaxial = stableProduct({maxR}, {distanceMetres});
    if (!std::isfinite(maxParaxial)) {
        throw std::overflow_error("Fraunhofer maximum paraxial parameter exceeds the finite double range");
    }

    const double factorX = (mMaxX > 0) ? static_cast<double>(2 * mMaxX - 1) : 0.0;
    const double factorY = (mMaxY > 0) ? static_cast<double>(2 * mMaxY - 1) : 0.0;
    const double maxPhaseStepX = (factorX == 0.0)
        ? 0.0
        : stableProduct({std::numbers::pi, factorX, pitchXOut}, {denomX});
    const double maxPhaseStepY = (factorY == 0.0)
        ? 0.0
        : stableProduct({std::numbers::pi, factorY, pitchYOut}, {denomY});
    const double maxAdjacentPhaseStep = std::max(maxPhaseStepX, maxPhaseStepY);
    if (!std::isfinite(maxAdjacentPhaseStep)) {
        throw std::overflow_error("Fraunhofer maximum adjacent phase step exceeds the finite double range");
    }

    FraunhoferDiagnostics diagnostics;
    diagnostics.mediumWavelengthMetres = lambda;
    diagnostics.outputPitchXMetres = pitchXOut;
    diagnostics.outputPitchYMetres = pitchYOut;
    diagnostics.periodicBoundary = true;
    diagnostics.automaticPadding = false;
    diagnostics.isExact = false;

    diagnostics.maximumParaxialParameter = maxParaxial;
    diagnostics.paraxialParameterBelowThreshold = (maxParaxial < 0.1);
    diagnostics.maxAdjacentPhaseStepRadians = maxAdjacentPhaseStep;
    diagnostics.quadraticPhaseUndersampled = (maxAdjacentPhaseStep > std::numbers::pi);

    if (options.illuminatedDiameterMetres.has_value()) {
        diagnostics.effectiveSupportDiameterMetres = *options.illuminatedDiameterMetres;
        diagnostics.supportSource = FraunhoferSupportSource::CallerProvidedDiameter;
    } else if (options.illuminatedExtentXMetres.has_value() && options.illuminatedExtentYMetres.has_value()) {
        const double extX = *options.illuminatedExtentXMetres;
        const double extY = *options.illuminatedExtentYMetres;
        diagnostics.effectiveSupportDiameterMetres = std::hypot(extX, extY);
        diagnostics.supportSource = FraunhoferSupportSource::CallerProvidedExtents;
    } else {
        diagnostics.effectiveSupportDiameterMetres = std::hypot(denomX, denomY);
        diagnostics.supportSource = FraunhoferSupportSource::FullGridExtentConservative;
    }

    diagnostics.fresnelNumber = stableProduct(
        {diagnostics.effectiveSupportDiameterMetres, diagnostics.effectiveSupportDiameterMetres},
        {lambda, distanceMetres});
    diagnostics.fresnelNumberBelowThreshold = (diagnostics.fresnelNumber < 0.1);
    diagnostics.farFieldConditionSatisfied = diagnostics.fresnelNumberBelowThreshold
        && diagnostics.paraxialParameterBelowThreshold;

    std::vector<std::string> warningParts;
    if (!diagnostics.fresnelNumberBelowThreshold) {
        warningParts.push_back(
            "Fresnel number D^2/(lambda*z) is " + std::to_string(diagnostics.fresnelNumber)
            + " (>= 0.1); far-field condition N_F << 1 is not satisfied, leading to significant near-field phase and amplitude discrepancies.");
    }
    if (!diagnostics.paraxialParameterBelowThreshold) {
        warningParts.push_back(
            "Maximum paraxial parameter lambda*hypot(fx_max, fy_max) is " + std::to_string(diagnostics.maximumParaxialParameter)
            + " (>= 0.1); small-angle paraxial approximation is violated at grid boundaries.");
    }
    if (diagnostics.quadraticPhaseUndersampled) {
        warningParts.push_back(
            "Maximum adjacent quadratic phase step is " + std::to_string(diagnostics.maxAdjacentPhaseStepRadians)
            + " rad (> pi); output spherical quadratic phase factor is undersampled/aliased on the discrete grid.");
    }

    if (warningParts.empty()) {
        diagnostics.warning.clear();
    } else {
        diagnostics.warning = warningParts[0];
        for (std::size_t i = 1; i < warningParts.size(); ++i) {
            diagnostics.warning += "\n" + warningParts[i];
        }
    }

    return FraunhoferResult{std::move(output), std::move(diagnostics)};
}

} // namespace holobench::compute::propagation
