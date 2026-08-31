#include "optics/wave/CoherentInterference.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holobench::optics::wave {
namespace {

void requireCompatible(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) {
    if (first.width() != second.width() || first.height() != second.height()
        || first.pitchXMetres() != second.pitchXMetres()
        || first.pitchYMetres() != second.pitchYMetres()
        || first.vacuumWavelengthMetres() != second.vacuumWavelengthMetres()
        || first.refractiveIndex() != second.refractiveIndex()) {
        throw std::invalid_argument(
            "interfering fields must have identical dimensions, sampling, wavelength, and medium");
    }
}

void requireFiniteFields(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) {
    for (std::size_t index = 0; index < first.sampleCount(); ++index) {
        const auto firstSample = first.samples()[index];
        const auto secondSample = second.samples()[index];
        if (!std::isfinite(firstSample.real()) || !std::isfinite(firstSample.imag())
            || !std::isfinite(secondSample.real()) || !std::isfinite(secondSample.imag())) {
            throw std::invalid_argument("interfering field samples must be finite");
        }
    }
}

[[nodiscard]] double finiteIntensity(
    std::complex<double> first,
    std::complex<double> second,
    std::complex<double> gamma) {
    const long double ar = first.real();
    const long double ai = first.imag();
    const long double br = second.real();
    const long double bi = second.imag();
    const long double gr = gamma.real();
    const long double gi = gamma.imag();
    const long double firstIntensity = ar * ar + ai * ai;
    const long double secondIntensity = br * br + bi * bi;
    const long double crossReal = ar * br + ai * bi;
    const long double crossImaginary = ai * br - ar * bi;
    const long double coherentReal = gr * crossReal - gi * crossImaginary;
    long double result = firstIntensity + secondIntensity + 2.0L * coherentReal;
    const long double scale = firstIntensity + secondIntensity;
    const long double tolerance = 64.0L
        * std::numeric_limits<long double>::epsilon() * std::max(1.0L, scale);
    if (result < 0.0L && result >= -tolerance) {
        result = 0.0L;
    }
    if (result < 0.0L) {
        throw std::runtime_error("mutual-coherence evaluation produced negative intensity");
    }
    if (!std::isfinite(result)
        || result > static_cast<long double>(std::numeric_limits<double>::max())) {
        throw std::overflow_error("interference intensity is not representable");
    }
    return static_cast<double>(result);
}

} // namespace

std::complex<double> mutualDegreeOfCoherence(
    const MutualCoherenceParameters& parameters) {
    if (!std::isfinite(parameters.zeroDelayDegree.real())
        || !std::isfinite(parameters.zeroDelayDegree.imag())) {
        throw std::invalid_argument("zero-delay degree of coherence must be finite");
    }
    const double zeroDelayMagnitude = std::abs(parameters.zeroDelayDegree);
    if (!std::isfinite(zeroDelayMagnitude) || zeroDelayMagnitude > 1.0) {
        throw std::invalid_argument("zero-delay degree of coherence magnitude must not exceed one");
    }
    if (!std::isfinite(parameters.opticalPathDifferenceMetres)) {
        throw std::invalid_argument("coherence optical-path difference must be finite");
    }
    if (!(parameters.coherenceLengthMetres > 0.0)
        || (std::isnan(parameters.coherenceLengthMetres))) {
        throw std::invalid_argument("coherence length must be positive or positive infinity");
    }
    if (std::isinf(parameters.coherenceLengthMetres)) {
        return parameters.zeroDelayDegree;
    }

    const double normalizedDelay = std::abs(parameters.opticalPathDifferenceMetres)
        / parameters.coherenceLengthMetres;
    if (!std::isfinite(normalizedDelay)) {
        return {0.0, 0.0};
    }
    const double exponent = parameters.envelope == CoherenceEnvelope::Gaussian
        ? -(normalizedDelay * normalizedDelay)
        : -normalizedDelay;
    const double visibilityEnvelope = std::exp(exponent);
    return parameters.zeroDelayDegree * visibilityEnvelope;
}

field::ComplexField2D combineFullyCoherentFields(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second) {
    requireCompatible(first, second);
    requireFiniteFields(first, second);
    auto combined = first;
    for (std::size_t index = 0; index < combined.sampleCount(); ++index) {
        combined.samples()[index] += second.samples()[index];
        const auto sample = combined.samples()[index];
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::overflow_error("coherent field sum is not representable");
        }
    }
    return combined;
}

InterferenceResult evaluateTwoBeamInterference(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second,
    const MutualCoherenceParameters& coherence) {
    requireCompatible(first, second);
    requireFiniteFields(first, second);
    const auto gamma = mutualDegreeOfCoherence(coherence);
    auto intensity = field::ScalarField2D::createMatching(first);
    double minimum = std::numeric_limits<double>::infinity();
    double maximum = 0.0;
    for (std::size_t index = 0; index < intensity.sampleCount(); ++index) {
        const double value = finiteIntensity(
            first.samples()[index], second.samples()[index], gamma);
        intensity.samples()[index] = value;
        minimum = std::min(minimum, value);
        maximum = std::max(maximum, value);
    }
    return {
        .intensity = std::move(intensity),
        .degreeOfCoherence = gamma,
        .minimumIntensity = minimum,
        .maximumIntensity = maximum,
    };
}

} // namespace holobench::optics::wave
