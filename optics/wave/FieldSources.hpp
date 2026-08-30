#pragma once

#include <complex>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::optics::wave {

struct PlaneWaveParameters final {
    // amplitude is the complex value before phaseAtOriginRadians is applied.
    std::complex<double> amplitude{1.0, 0.0};
    // The positive-Z direction is derived from these transverse direction cosines.
    double directionCosineX = 0.0;
    double directionCosineY = 0.0;
    double phaseAtOriginRadians = 0.0;
    double planeZMetres = 0.0;
};

struct PlaneWaveDiagnostics final {
    double directionCosineZ = 1.0;
};

struct GaussianBeamParameters final {
    // Fundamental paraxial Gaussian beam. waistRadiusMetres is the 1/e field-amplitude radius.
    std::complex<double> waistAmplitude{1.0, 0.0};
    double waistRadiusMetres = 1e-3;
    double waistZMetres = 0.0;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
    double planeZMetres = 0.0;
};

struct GaussianBeamDiagnostics final {
    double rayleighRangeMetres = 0.0;
    double beamRadiusMetres = 0.0;
    double inverseWavefrontRadiusPerMetre = 0.0;
    double gouyPhaseRadians = 0.0;
    double onAxisAmplitudeScale = 0.0;
};

PlaneWaveDiagnostics fillPlaneWave(
    field::ComplexField2D& destination,
    const PlaneWaveParameters& parameters);

GaussianBeamDiagnostics fillFundamentalGaussianBeam(
    field::ComplexField2D& destination,
    const GaussianBeamParameters& parameters);

} // namespace holobench::optics::wave
