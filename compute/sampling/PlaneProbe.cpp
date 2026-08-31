#include "compute/sampling/PlaneProbe.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"

namespace holobench::compute::sampling {

PlaneProbeResult probeAngularSpectrumPlanes(
    const field::ComplexField2D& sourcePlane,
    std::size_t xIndex,
    std::size_t yIndex,
    std::span<const double> distancesMetres,
    fft::IFftBackend& fftBackend) {
    if (xIndex >= sourcePlane.width() || yIndex >= sourcePlane.height()) {
        throw std::out_of_range("Plane probe index is outside the source grid");
    }
    if (distancesMetres.empty()) {
        throw std::invalid_argument("Plane probe requires at least one z distance");
    }
    for (const auto& value : sourcePlane.samples()) {
        if (!std::isfinite(value.real()) || !std::isfinite(value.imag())) {
            throw std::invalid_argument("Plane probe source samples must be finite");
        }
    }
    for (const double distance : distancesMetres) {
        if (!std::isfinite(distance)) {
            throw std::invalid_argument("Plane probe distances must be finite");
        }
    }
    const bool requiresFft = std::any_of(
        distancesMetres.begin(), distancesMetres.end(), [](double distance) { return distance != 0.0; });
    if (requiresFft && !fftBackend.supportsDimensions(sourcePlane.width(), sourcePlane.height())) {
        throw std::invalid_argument("FFT backend does not support plane-probe dimensions");
    }

    PlaneProbeResult result;
    result.xIndex = xIndex;
    result.yIndex = yIndex;
    result.xCoordinateMetres = sourcePlane.xCoordinateMetres(xIndex);
    result.yCoordinateMetres = sourcePlane.yCoordinateMetres(yIndex);
    result.samples.reserve(distancesMetres.size());
    propagation::AngularSpectrumPropagator propagator(fftBackend);
    for (const double distance : distancesMetres) {
        auto plane = sourcePlane;
        propagation::AngularSpectrumDiagnostics diagnostics;
        if (distance == 0.0) {
            diagnostics = propagation::makeAngularSpectrumTransferFunction(
                sourcePlane, 0.0).diagnostics;
        } else {
            diagnostics = propagator.propagateInPlace(plane, distance);
        }
        const auto intensity = field::computeIntensity(plane);
        const auto phase = field::computeWrappedPhase(plane);
        PlaneProbeSample sample;
        sample.distanceMetres = distance;
        sample.fieldValue = plane.at(xIndex, yIndex);
        sample.intensity = intensity.at(xIndex, yIndex);
        sample.wrappedPhaseRadians = phase.wrappedPhaseRadians().at(xIndex, yIndex);
        sample.phaseValid = phase.isValid(xIndex, yIndex);
        sample.propagatingBinCount = diagnostics.propagatingBinCount;
        sample.evanescentBinCount = diagnostics.evanescentBinCount;
        result.samples.push_back(sample);
    }
    return result;
}

} // namespace holobench::compute::sampling
