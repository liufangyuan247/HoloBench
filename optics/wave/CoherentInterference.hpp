#pragma once

#include <complex>
#include <limits>

#include "core/field/ComplexField2D.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::optics::wave {

enum class CoherenceEnvelope {
    Gaussian,
    Exponential,
};

struct MutualCoherenceParameters final {
    std::complex<double> zeroDelayDegree{1.0, 0.0};
    double opticalPathDifferenceMetres = 0.0;
    // Positive infinity represents the monochromatic teaching limit.
    double coherenceLengthMetres = std::numeric_limits<double>::infinity();
    CoherenceEnvelope envelope = CoherenceEnvelope::Gaussian;
};

struct InterferenceResult final {
    field::ScalarField2D intensity;
    std::complex<double> degreeOfCoherence{1.0, 0.0};
    double minimumIntensity = 0.0;
    double maximumIntensity = 0.0;
};

// The coherence length is the optical-path difference at which |gamma| has
// fallen to 1/e for either supported teaching envelope.
[[nodiscard]] std::complex<double> mutualDegreeOfCoherence(
    const MutualCoherenceParameters& parameters);

[[nodiscard]] field::ComplexField2D combineFullyCoherentFields(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second);

// I = |U1|^2 + |U2|^2 + 2 Re(gamma U1 conj(U2)). This is a scalar,
// time-averaged mutual-coherence model and does not model polarization.
[[nodiscard]] InterferenceResult evaluateTwoBeamInterference(
    const field::ComplexField2D& first,
    const field::ComplexField2D& second,
    const MutualCoherenceParameters& coherence = {});

} // namespace holobench::optics::wave
