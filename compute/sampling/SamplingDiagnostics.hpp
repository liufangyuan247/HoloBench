#pragma once

#include <cstddef>
#include <optional>
#include <string>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::sampling {

struct SamplingAnalysisOptions final {
    double propagationDistanceMetres = 0.0;
    double requestedHalfAngleXRadians = 0.0;
    double requestedHalfAngleYRadians = 0.0;
    std::optional<double> illuminatedExtentXMetres;
    std::optional<double> illuminatedExtentYMetres;
    std::size_t minimumBoundaryGuardSamples = 4U;
    bool periodicBoundary = true;
};

struct SamplingDiagnostics final {
    double mediumWavelengthMetres = 0.0;
    double physicalWidthMetres = 0.0;
    double physicalHeightMetres = 0.0;
    double nyquistHalfAngleXRadians = 0.0;
    double nyquistHalfAngleYRadians = 0.0;
    double requestedHalfAngleXRadians = 0.0;
    double requestedHalfAngleYRadians = 0.0;
    double maximumSampledRadialFrequencyCyclesPerMetre = 0.0;
    double propagatingCutoffFrequencyCyclesPerMetre = 0.0;
    double boundaryClearanceXMetres = 0.0;
    double boundaryClearanceYMetres = 0.0;
    double requiredPaddingFactorX = 1.0;
    double requiredPaddingFactorY = 1.0;
    bool spatialAliasingRisk = false;
    bool angularBandwidthInsufficient = false;
    bool wrapAroundRisk = false;
    bool insufficientPadding = false;
    bool apertureTooCloseToBoundary = false;
    bool containsEvanescentBins = false;
    bool periodicBoundary = true;
    std::string warning;
};

[[nodiscard]] SamplingDiagnostics analyzeSampling(
    const field::ComplexField2D& field,
    const SamplingAnalysisOptions& options = {});

} // namespace holobench::compute::sampling
