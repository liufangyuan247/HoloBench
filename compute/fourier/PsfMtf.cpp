#include "compute/fourier/PsfMtf.hpp"

#include <algorithm>
#include <cmath>
#include <initializer_list>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace holobench::compute::fourier {
namespace {

constexpr double firstPositiveJ1Zero = 3.8317059702075125;
constexpr double paraxialRatioLimit = 0.1;

[[nodiscard]] double checkedPositiveProductRatio(
    std::initializer_list<double> numerators,
    std::initializer_list<double> denominators,
    const char* operation) {
    double mantissa = 1.0;
    int exponent = 0;
    for (const double value : numerators) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::invalid_argument(std::string(operation) + " requires positive finite inputs");
        }
        int valueExponent = 0;
        mantissa *= std::frexp(value, &valueExponent);
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += valueExponent + normalizationExponent;
    }
    for (const double value : denominators) {
        if (!std::isfinite(value) || value <= 0.0) {
            throw std::invalid_argument(std::string(operation) + " requires positive finite inputs");
        }
        int valueExponent = 0;
        mantissa /= std::frexp(value, &valueExponent);
        int normalizationExponent = 0;
        mantissa = std::frexp(mantissa, &normalizationExponent);
        exponent += normalizationExponent - valueExponent;
    }

    constexpr int minimumDenormalExponent =
        std::numeric_limits<double>::min_exponent - std::numeric_limits<double>::digits + 1;
    if (exponent > std::numeric_limits<double>::max_exponent) {
        throw std::overflow_error(std::string(operation) + " exceeds the finite double domain");
    }
    if (exponent < minimumDenormalExponent
        || (exponent == minimumDenormalExponent && std::abs(mantissa) < 0.5)) {
        throw std::underflow_error(std::string(operation) + " underflows the double domain");
    }
    const double result = std::ldexp(mantissa, exponent);
    if (!std::isfinite(result)) {
        throw std::overflow_error(std::string(operation) + " exceeds the finite double domain");
    }
    if (result == 0.0) {
        throw std::underflow_error(std::string(operation) + " underflows the double domain");
    }
    return result;
}

void requireNonNegativeFinite(double value, const char* name) {
    if (!std::isfinite(value) || value < 0.0) {
        throw std::invalid_argument(std::string(name) + " must be non-negative and finite");
    }
}

[[nodiscard]] double besselJ1(double argument) {
    // The power series is well conditioned at small/moderate arguments when
    // accumulated in long double. Beyond 12 rad, use the optimally truncated
    // Hankel asymptotic expansion. This keeps the implementation deterministic
    // on standard libraries that do not provide C++ special functions.
    if (argument < 12.0) {
        const long double halfArgument = 0.5L * static_cast<long double>(argument);
        const long double halfArgumentSquared = halfArgument * halfArgument;
        long double term = halfArgument;
        long double sum = term;
        for (int order = 1; order < 200; ++order) {
            term *= -halfArgumentSquared
                / (static_cast<long double>(order) * static_cast<long double>(order + 1));
            sum += term;
            if (std::abs(term) <= std::numeric_limits<long double>::epsilon()
                    * std::max(1.0L, std::abs(sum))) {
                break;
            }
        }
        return static_cast<double>(sum);
    }

    const long double x = static_cast<long double>(argument);
    long double coefficientOverPower = 1.0L;
    long double evenSeries = 1.0L;
    long double oddSeries = 0.0L;
    long double previousMagnitude = std::numeric_limits<long double>::infinity();
    for (int order = 1; order < 200; ++order) {
        const long double oddInteger = static_cast<long double>(2 * order - 1);
        coefficientOverPower *= (4.0L - oddInteger * oddInteger)
            / (8.0L * static_cast<long double>(order) * x);
        const long double magnitude = std::abs(coefficientOverPower);
        if (magnitude > previousMagnitude) {
            break;
        }
        previousMagnitude = magnitude;
        if ((order % 2) == 0) {
            const int pairIndex = order / 2;
            evenSeries += (pairIndex % 2) == 0
                ? coefficientOverPower
                : -coefficientOverPower;
        } else {
            const int pairIndex = (order - 1) / 2;
            oddSeries += (pairIndex % 2) == 0
                ? coefficientOverPower
                : -coefficientOverPower;
        }
        if (magnitude <= std::numeric_limits<long double>::epsilon()) {
            break;
        }
    }
    const long double phase = static_cast<long double>(std::remainder(
        argument,
        2.0 * std::numbers::pi)) - 3.0L * std::numbers::pi_v<long double> / 4.0L;
    const long double result = std::sqrt(2.0L / (std::numbers::pi_v<long double> * x))
        * (std::cos(phase) * evenSeries - std::sin(phase) * oddSeries);
    return static_cast<double>(result);
}

} // namespace

CircularPupilPsfMtf::CircularPupilPsfMtf(
    double vacuumWavelengthMetres,
    double refractiveIndex,
    double focalLengthMetres,
    double pupilRadiusMetres) {
    const double mediumWavelength = checkedPositiveProductRatio(
        {vacuumWavelengthMetres},
        {refractiveIndex},
        "Circular-pupil medium wavelength");
    const double radiusToFocalLength = checkedPositiveProductRatio(
        {pupilRadiusMetres},
        {focalLengthMetres},
        "Circular-pupil radius-to-focal-length ratio");
    const double coherentCutoff = checkedPositiveProductRatio(
        {pupilRadiusMetres},
        {mediumWavelength, focalLengthMetres},
        "Circular-pupil coherent cutoff");
    const double incoherentCutoff = checkedPositiveProductRatio(
        {2.0, coherentCutoff},
        {},
        "Circular-pupil incoherent cutoff");
    const double firstDarkRadius = checkedPositiveProductRatio(
        {firstPositiveJ1Zero},
        {2.0, std::numbers::pi, coherentCutoff},
        "Circular-pupil first dark radius");
    const double paraxialNumericalAperture = checkedPositiveProductRatio(
        {refractiveIndex, pupilRadiusMetres},
        {focalLengthMetres},
        "Circular-pupil paraxial numerical aperture");

    diagnostics_.vacuumWavelengthMetres = vacuumWavelengthMetres;
    diagnostics_.mediumWavelengthMetres = mediumWavelength;
    diagnostics_.refractiveIndex = refractiveIndex;
    diagnostics_.focalLengthMetres = focalLengthMetres;
    diagnostics_.pupilRadiusMetres = pupilRadiusMetres;
    diagnostics_.pupilRadiusToFocalLength = radiusToFocalLength;
    diagnostics_.paraxialNumericalAperture = paraxialNumericalAperture;
    diagnostics_.coherentCutoffCyclesPerMetre = coherentCutoff;
    diagnostics_.incoherentCutoffCyclesPerMetre = incoherentCutoff;
    diagnostics_.firstDarkRadiusMetres = firstDarkRadius;
    diagnostics_.paraxialValiditySatisfied = radiusToFocalLength < paraxialRatioLimit;
}

double CircularPupilPsfMtf::normalizedCoherentAmplitudePsf(double radiusMetres) const {
    requireNonNegativeFinite(radiusMetres, "PSF radius");
    if (radiusMetres == 0.0) {
        return 1.0;
    }
    const double argument = checkedPositiveProductRatio(
        {2.0, std::numbers::pi, diagnostics_.coherentCutoffCyclesPerMetre, radiusMetres},
        {},
        "Circular-pupil PSF argument");
    if (argument < 1e-4) {
        const double argumentSquared = argument * argument;
        return 1.0 - argumentSquared / 8.0
            + argumentSquared * argumentSquared / 192.0;
    }
    const double amplitude = 2.0 * besselJ1(argument) / argument;
    if (!std::isfinite(amplitude)) {
        throw std::overflow_error("Circular-pupil Bessel evaluation is non-finite");
    }
    return amplitude;
}

double CircularPupilPsfMtf::normalizedIntensityPsf(double radiusMetres) const {
    const double amplitude = normalizedCoherentAmplitudePsf(radiusMetres);
    const long double intensity = static_cast<long double>(amplitude)
        * static_cast<long double>(amplitude);
    if (intensity != 0.0L
        && intensity < static_cast<long double>(std::numeric_limits<double>::denorm_min())) {
        throw std::underflow_error("Circular-pupil normalized PSF intensity underflows double");
    }
    return static_cast<double>(intensity);
}

double CircularPupilPsfMtf::normalizedIncoherentMtf(
    double spatialFrequencyCyclesPerMetre) const {
    requireNonNegativeFinite(spatialFrequencyCyclesPerMetre, "MTF spatial frequency");
    const double normalizedFrequency = spatialFrequencyCyclesPerMetre
        / diagnostics_.incoherentCutoffCyclesPerMetre;
    if (normalizedFrequency >= 1.0) {
        return 0.0;
    }
    const double angle = std::acos(normalizedFrequency);
    if (angle < 1e-3) {
        const double angleSquared = angle * angle;
        return (4.0 / (3.0 * std::numbers::pi)) * angle * angleSquared
            * (1.0 - angleSquared / 5.0 + 2.0 * angleSquared * angleSquared / 105.0);
    }
    return (2.0 / std::numbers::pi)
        * (angle - 0.5 * std::sin(2.0 * angle));
}

field::ScalarField2D CircularPupilPsfMtf::sampleNormalizedIntensityPsf(
    std::size_t width,
    std::size_t height,
    double pitchXMetres,
    double pitchYMetres) const {
    field::ScalarField2D result(
        width,
        height,
        pitchXMetres,
        pitchYMetres,
        diagnostics_.vacuumWavelengthMetres,
        diagnostics_.refractiveIndex);
    for (std::size_t y = 0; y < result.height(); ++y) {
        const double yMetres = result.yCoordinateMetres(y);
        for (std::size_t x = 0; x < result.width(); ++x) {
            result.at(x, y) = normalizedIntensityPsf(
                std::hypot(result.xCoordinateMetres(x), yMetres));
        }
    }
    return result;
}

std::vector<RadialMtfSample> CircularPupilPsfMtf::sampleNormalizedIncoherentMtf(
    std::size_t sampleCount,
    double maximumSpatialFrequencyCyclesPerMetre) const {
    if (sampleCount < 2U) {
        throw std::invalid_argument("MTF curve requires at least two samples");
    }
    requireNonNegativeFinite(maximumSpatialFrequencyCyclesPerMetre, "Maximum MTF frequency");
    std::vector<RadialMtfSample> result;
    result.reserve(sampleCount);
    for (std::size_t index = 0; index < sampleCount; ++index) {
        const double frequency = maximumSpatialFrequencyCyclesPerMetre
            * static_cast<double>(index) / static_cast<double>(sampleCount - 1U);
        result.push_back({frequency, normalizedIncoherentMtf(frequency)});
    }
    return result;
}

} // namespace holobench::compute::fourier
