#include "optics/wave/FieldElements.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "core/field/ComplexField2D.hpp"

namespace holobench::optics::wave {
namespace {

void requireFinite(double value, const char* message) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(message);
    }
}

void validateFiniteField(const field::ComplexField2D& value) {
    for (const auto& sample : value.samples()) {
        if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
            throw std::invalid_argument("wave element input samples must be finite");
        }
    }
}

[[nodiscard]] double finiteCoordinateOffset(double coordinate, double center) {
    const double offset = coordinate - center;
    if (!std::isfinite(offset)) {
        throw std::overflow_error("wave element coordinate offset is not representable");
    }
    return offset;
}

template <typename Predicate>
BinaryMaskDiagnostics applyBinaryMask(field::ComplexField2D& field, Predicate&& isTransmitted) {
    validateFiniteField(field);
    auto masked = field;
    BinaryMaskDiagnostics diagnostics;
    for (std::size_t y = 0; y < masked.height(); ++y) {
        for (std::size_t x = 0; x < masked.width(); ++x) {
            if (isTransmitted(masked.xCoordinateMetres(x), masked.yCoordinateMetres(y))) {
                ++diagnostics.transmittedSampleCount;
            } else {
                masked.at(x, y) = {0.0, 0.0};
                ++diagnostics.blockedSampleCount;
            }
        }
    }
    field = std::move(masked);
    return diagnostics;
}

} // namespace

BinaryMaskDiagnostics applyCircularAperture(
    field::ComplexField2D& field,
    const CircularApertureParameters& parameters) {
    requireFinite(parameters.radiusMetres, "circular aperture radius must be finite");
    requireFinite(parameters.centerXMetres, "circular aperture center X must be finite");
    requireFinite(parameters.centerYMetres, "circular aperture center Y must be finite");
    if (parameters.radiusMetres <= 0.0) {
        throw std::invalid_argument("circular aperture radius must be positive");
    }

    return applyBinaryMask(field, [&](double xMetres, double yMetres) {
        const double x = finiteCoordinateOffset(xMetres, parameters.centerXMetres);
        const double y = finiteCoordinateOffset(yMetres, parameters.centerYMetres);
        return std::hypot(x, y) <= parameters.radiusMetres;
    });
}

BinaryMaskDiagnostics applyEllipticalAperture(
    field::ComplexField2D& field,
    const EllipticalApertureParameters& parameters) {
    requireFinite(
        parameters.halfWidthMetres,
        "elliptical aperture half-width must be finite");
    requireFinite(
        parameters.halfHeightMetres,
        "elliptical aperture half-height must be finite");
    requireFinite(
        parameters.centerXMetres,
        "elliptical aperture center X must be finite");
    requireFinite(
        parameters.centerYMetres,
        "elliptical aperture center Y must be finite");
    if (parameters.halfWidthMetres <= 0.0
        || parameters.halfHeightMetres <= 0.0) {
        throw std::invalid_argument(
            "elliptical aperture half-sizes must be positive");
    }
    return applyBinaryMask(field, [&](double xMetres, double yMetres) {
        const double x = finiteCoordinateOffset(
            xMetres, parameters.centerXMetres)
            / parameters.halfWidthMetres;
        const double y = finiteCoordinateOffset(
            yMetres, parameters.centerYMetres)
            / parameters.halfHeightMetres;
        return std::hypot(x, y) <= 1.0;
    });
}

BinaryMaskDiagnostics applyRectangularAperture(
    field::ComplexField2D& field,
    const RectangularApertureParameters& parameters) {
    requireFinite(parameters.halfWidthMetres, "rectangular aperture half-width must be finite");
    requireFinite(parameters.halfHeightMetres, "rectangular aperture half-height must be finite");
    requireFinite(parameters.centerXMetres, "rectangular aperture center X must be finite");
    requireFinite(parameters.centerYMetres, "rectangular aperture center Y must be finite");
    if (parameters.halfWidthMetres <= 0.0 || parameters.halfHeightMetres <= 0.0) {
        throw std::invalid_argument("rectangular aperture half-sizes must be positive");
    }

    return applyBinaryMask(field, [&](double xMetres, double yMetres) {
        const double x = finiteCoordinateOffset(xMetres, parameters.centerXMetres);
        const double y = finiteCoordinateOffset(yMetres, parameters.centerYMetres);
        return std::abs(x) <= parameters.halfWidthMetres
            && std::abs(y) <= parameters.halfHeightMetres;
    });
}

BinaryMaskDiagnostics applyDoubleSlit(
    field::ComplexField2D& field,
    const DoubleSlitParameters& parameters) {
    requireFinite(parameters.slitWidthMetres, "double-slit width must be finite");
    requireFinite(parameters.slitHeightMetres, "double-slit height must be finite");
    requireFinite(parameters.centerSeparationMetres, "double-slit separation must be finite");
    requireFinite(parameters.centerXMetres, "double-slit center X must be finite");
    requireFinite(parameters.centerYMetres, "double-slit center Y must be finite");
    if (parameters.slitWidthMetres <= 0.0 || parameters.slitHeightMetres <= 0.0) {
        throw std::invalid_argument("double-slit sizes must be positive");
    }
    if (parameters.centerSeparationMetres <= parameters.slitWidthMetres) {
        throw std::invalid_argument("double-slit centers must be separated by more than one slit width");
    }

    const double halfWidth = parameters.slitWidthMetres / 2.0;
    const double halfHeight = parameters.slitHeightMetres / 2.0;
    const double halfSeparation = parameters.centerSeparationMetres / 2.0;
    return applyBinaryMask(field, [&](double xMetres, double yMetres) {
        const double x = finiteCoordinateOffset(xMetres, parameters.centerXMetres);
        const double y = finiteCoordinateOffset(yMetres, parameters.centerYMetres);
        const bool insideEitherSlit = std::abs(std::abs(x) - halfSeparation) <= halfWidth;
        return insideEitherSlit && std::abs(y) <= halfHeight;
    });
}

ThinLensPhaseDiagnostics applyIdealThinLensPhase(
    field::ComplexField2D& field,
    const ThinLensPhaseParameters& parameters) {
    requireFinite(parameters.focalLengthMetres, "thin-lens focal length must be finite");
    requireFinite(parameters.centerXMetres, "thin-lens center X must be finite");
    requireFinite(parameters.centerYMetres, "thin-lens center Y must be finite");
    if (parameters.focalLengthMetres == 0.0) {
        throw std::invalid_argument("thin-lens focal length must be nonzero");
    }
    validateFiniteField(field);

    const double wavenumber = field.mediumWavenumberRadiansPerMetre();
    if (!std::isfinite(wavenumber) || wavenumber <= 0.0) {
        throw std::invalid_argument("thin-lens medium wavenumber must be positive and finite");
    }
    const double phaseCoefficient = (-0.5 * wavenumber) / parameters.focalLengthMetres;
    if (!std::isfinite(phaseCoefficient)) {
        throw std::overflow_error("thin-lens phase coefficient is not representable");
    }

    auto phased = field;
    ThinLensPhaseDiagnostics diagnostics;
    for (std::size_t y = 0; y < phased.height(); ++y) {
        const double yOffset = finiteCoordinateOffset(
            phased.yCoordinateMetres(y), parameters.centerYMetres);
        for (std::size_t x = 0; x < phased.width(); ++x) {
            const double xOffset = finiteCoordinateOffset(
                phased.xCoordinateMetres(x), parameters.centerXMetres);
            const double radius = std::hypot(xOffset, yOffset);
            if (!std::isfinite(radius)) {
                throw std::overflow_error("thin-lens radial coordinate is not representable");
            }
            const double phase = (phaseCoefficient * radius) * radius;
            if (!std::isfinite(phase)) {
                throw std::overflow_error("thin-lens phase is not representable");
            }
            const auto transfer = std::polar(
                1.0, std::remainder(phase, 2.0 * std::numbers::pi));
            const auto sample = phased.at(x, y) * transfer;
            if (!std::isfinite(sample.real()) || !std::isfinite(sample.imag())) {
                throw std::overflow_error("thin-lens phase produced a non-finite sample");
            }
            phased.at(x, y) = sample;
            diagnostics.maximumAbsoluteUnwrappedPhaseRadians = std::max(
                diagnostics.maximumAbsoluteUnwrappedPhaseRadians, std::abs(phase));
            ++diagnostics.modifiedSampleCount;
        }
    }

    field = std::move(phased);
    return diagnostics;
}

} // namespace holobench::optics::wave
