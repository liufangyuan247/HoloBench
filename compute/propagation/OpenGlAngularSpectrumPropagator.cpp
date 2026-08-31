#include "compute/propagation/OpenGlAngularSpectrumPropagator.hpp"

#include <cmath>
#include <stdexcept>

#include "compute/fft/OpenGlComputeFftBackend.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::compute::propagation {

OpenGlAngularSpectrumPropagator::OpenGlAngularSpectrumPropagator(
    fft::OpenGlComputeFftBackend& backend) noexcept
    : backend_(backend) {
}

AngularSpectrumDiagnostics OpenGlAngularSpectrumPropagator::propagateInPlace(
    field::ComplexField2D& field,
    double distanceMetres) {
    if (!std::isfinite(distanceMetres)) {
        throw std::invalid_argument("OpenGL ASM propagation distance must be finite");
    }
    if (!backend_.supportsDimensions(field.width(), field.height())) {
        throw std::invalid_argument("OpenGL ASM backend does not support the field dimensions");
    }

    const TransferFunctionKey requestedKey {
        field.width(),
        field.height(),
        field.pitchXMetres(),
        field.pitchYMetres(),
        field.vacuumWavelengthMetres(),
        field.refractiveIndex(),
        distanceMetres};
    if (!hasCachedTransferFunction_ || requestedKey != cachedKey_) {
        auto prepared = makeAngularSpectrumTransferFunction(field, distanceMetres);
        transferFunction_.swap(prepared.samples);
        cachedDiagnostics_ = prepared.diagnostics;
        cachedKey_ = requestedKey;
        hasCachedTransferFunction_ = true;
    }

    backend_.applySpectralTransfer2D(field, transferFunction_);
    return cachedDiagnostics_;
}

} // namespace holobench::compute::propagation
