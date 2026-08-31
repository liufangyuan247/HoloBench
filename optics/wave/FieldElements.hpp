#pragma once

#include <cstddef>

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::optics::wave {

struct BinaryMaskDiagnostics final {
    std::size_t transmittedSampleCount = 0;
    std::size_t blockedSampleCount = 0;
};

struct CircularApertureParameters final {
    double radiusMetres = 1e-3;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
};

struct EllipticalApertureParameters final {
    double halfWidthMetres = 1e-3;
    double halfHeightMetres = 1e-3;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
};

struct RectangularApertureParameters final {
    double halfWidthMetres = 1e-3;
    double halfHeightMetres = 1e-3;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
};

struct DoubleSlitParameters final {
    double slitWidthMetres = 0.1e-3;
    double slitHeightMetres = 1e-3;
    double centerSeparationMetres = 0.3e-3;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
};

struct ThinLensPhaseParameters final {
    double focalLengthMetres = 0.1;
    double centerXMetres = 0.0;
    double centerYMetres = 0.0;
};

struct ThinLensPhaseDiagnostics final {
    std::size_t modifiedSampleCount = 0;
    double maximumAbsoluteUnwrappedPhaseRadians = 0.0;
};

BinaryMaskDiagnostics applyCircularAperture(
    field::ComplexField2D& field,
    const CircularApertureParameters& parameters);

BinaryMaskDiagnostics applyEllipticalAperture(
    field::ComplexField2D& field,
    const EllipticalApertureParameters& parameters);

BinaryMaskDiagnostics applyRectangularAperture(
    field::ComplexField2D& field,
    const RectangularApertureParameters& parameters);

BinaryMaskDiagnostics applyDoubleSlit(
    field::ComplexField2D& field,
    const DoubleSlitParameters& parameters);

ThinLensPhaseDiagnostics applyIdealThinLensPhase(
    field::ComplexField2D& field,
    const ThinLensPhaseParameters& parameters);

} // namespace holobench::optics::wave
