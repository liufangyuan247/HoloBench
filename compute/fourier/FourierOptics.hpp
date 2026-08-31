#pragma once

#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::compute::fourier {

struct FourierPlaneDiagnostics final {
    double focalLengthMetres = 0.0;
    double mediumWavelengthMetres = 0.0;
    double spatialFrequencyPitchXCyclesPerMetre = 0.0;
    double spatialFrequencyPitchYCyclesPerMetre = 0.0;
    double outputPitchXMetres = 0.0;
    double outputPitchYMetres = 0.0;
    bool periodicBoundary = true;
    bool automaticPadding = false;
    bool scalarParaxialModel = true;
};

struct FourierPlaneResult final {
    field::ComplexField2D field;
    FourierPlaneDiagnostics diagnostics;
};

/**
 * Maps the front focal plane of an ideal positive thin lens to its back focal
 * plane under the scalar paraxial convention in ADR 0005. The transform is a
 * centred, unnormalised Fourier integral with physical output sampling
 * dx_out = lambda*f/(Nx*dx_in). The two free-space focal lengths and lens are
 * represented by the corresponding ABCD system; no far-field claim is made.
 */
class FourierLensTransform final {
public:
    explicit FourierLensTransform(fft::IFftBackend& fftBackend) noexcept;

    [[nodiscard]] FourierPlaneResult transformFrontToBackFocalPlane(
        const field::ComplexField2D& input,
        double focalLengthMetres) const;

private:
    fft::IFftBackend& fftBackend_;
};

} // namespace holobench::compute::fourier
