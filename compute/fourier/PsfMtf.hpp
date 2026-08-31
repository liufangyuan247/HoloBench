#pragma once

#include <cstddef>
#include <vector>

#include "core/field/ScalarField2D.hpp"

namespace holobench::compute::fourier {

struct CircularPupilTransferDiagnostics final {
    double vacuumWavelengthMetres = 0.0;
    double mediumWavelengthMetres = 0.0;
    double refractiveIndex = 1.0;
    double focalLengthMetres = 0.0;
    double pupilRadiusMetres = 0.0;
    double pupilRadiusToFocalLength = 0.0;
    double paraxialNumericalAperture = 0.0;
    double coherentCutoffCyclesPerMetre = 0.0;
    double incoherentCutoffCyclesPerMetre = 0.0;
    double firstDarkRadiusMetres = 0.0;
    bool scalarParaxialModel = true;
    bool paraxialValiditySatisfied = false;
};

struct RadialMtfSample final {
    double spatialFrequencyCyclesPerMetre = 0.0;
    double normalizedIncoherentMtf = 0.0;
};

/**
 * Analytic diffraction-limited response of an ideal hard-edged circular pupil.
 *
 * The coherent amplitude PSF is 2*J1(v)/v with
 * v = 2*pi*f_coherent*r. The normalized intensity PSF is its squared
 * magnitude. The exposed MTF is explicitly the incoherent intensity MTF,
 * obtained from the normalized circular-pupil autocorrelation; it is not the
 * coherent amplitude transfer function.
 */
class CircularPupilPsfMtf final {
public:
    CircularPupilPsfMtf(
        double vacuumWavelengthMetres,
        double refractiveIndex,
        double focalLengthMetres,
        double pupilRadiusMetres);

    [[nodiscard]] const CircularPupilTransferDiagnostics& diagnostics() const noexcept {
        return diagnostics_;
    }

    [[nodiscard]] double normalizedCoherentAmplitudePsf(double radiusMetres) const;
    [[nodiscard]] double normalizedIntensityPsf(double radiusMetres) const;
    [[nodiscard]] double normalizedIncoherentMtf(
        double spatialFrequencyCyclesPerMetre) const;

    [[nodiscard]] field::ScalarField2D sampleNormalizedIntensityPsf(
        std::size_t width,
        std::size_t height,
        double pitchXMetres,
        double pitchYMetres) const;

    [[nodiscard]] std::vector<RadialMtfSample> sampleNormalizedIncoherentMtf(
        std::size_t sampleCount,
        double maximumSpatialFrequencyCyclesPerMetre) const;

private:
    CircularPupilTransferDiagnostics diagnostics_;
};

} // namespace holobench::compute::fourier
