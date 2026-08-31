#include "optics/holography/BenchHologramReplay.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"

namespace holobench::optics::holography {
namespace {

constexpr double kParallelTolerance = 2e-12;

const scene::BenchComponent& requireObservation(
    const scene::BenchScene& bench,
    std::string_view componentId) {
    const auto* observer = bench.find(componentId);
    if (observer == nullptr
        || (observer->kind != scene::BenchComponentKind::ScreenDetector
            && observer->kind != scene::BenchComponentKind::FieldProbe)) {
        throw std::invalid_argument(
            "thin-plate replay observation must be a placed screen or field probe");
    }
    return *observer;
}

std::pair<double, double> observerExtent(
    const scene::BenchComponent& observer) {
    if (observer.kind == scene::BenchComponentKind::ScreenDetector) {
        const auto& value = std::get<scene::ScreenDetectorParameters>(
            observer.parameters);
        return {value.widthMetres, value.heightMetres};
    }
    const auto& value = std::get<scene::FieldProbeParameters>(observer.parameters);
    return {value.widthMetres, value.heightMetres};
}

} // namespace

bool ThinPlateReplayResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* observer = bench.find(observationComponentId);
    return sourceRevision != bench.revision()
        || observer == nullptr
        || (observer->kind != scene::BenchComponentKind::ScreenDetector
            && observer->kind != scene::BenchComponentKind::FieldProbe);
}

ThinPlateReplayResult replayThinTransmissionToObservation(
    const scene::BenchScene& bench,
    const ThinPlateRecordingResult& recording,
    std::string observationComponentId,
    ThinPlateReplayKind replayKind,
    compute::fft::IFftBackend& fftBackend) {
    if (recording.isStaleFor(bench)) {
        throw std::invalid_argument(
            "thin-plate replay requires a current recording");
    }
    const auto* plate = bench.find(recording.plateComponentId);
    if (plate == nullptr) {
        throw std::invalid_argument("thin-plate replay plate was not found");
    }
    const auto& observer = requireObservation(bench, observationComponentId);
    const double xAlignment = math::dot(
        plate->transform.localXAxisInWorld,
        observer.transform.localXAxisInWorld);
    const double yAlignment = math::dot(
        plate->transform.localYAxisInWorld,
        observer.transform.localYAxisInWorld);
    const double normalAlignment = math::dot(
        plate->transform.localZAxisInWorld,
        observer.transform.localZAxisInWorld);
    const bool parallelAxisAligned = xAlignment >= 1.0 - kParallelTolerance
        && yAlignment >= 1.0 - kParallelTolerance
        && normalAlignment >= 1.0 - kParallelTolerance;
    if (!parallelAxisAligned
        && std::abs(math::dot(
            observer.transform.localZAxisInWorld,
            plate->transform.localZAxisInWorld)) <= 1e-8) {
        throw std::invalid_argument(
            "thin-plate replay observation plane is grazing the transmitted direction");
    }
    const auto observerCentre = math::transformPointWorldToLocal(
        plate->transform, observer.transform.translationMetres);
    if (observerCentre.z == 0.0) {
        throw std::invalid_argument(
            "thin-plate replay observation plane must be separated from the plate");
    }
    const double forwardSign = recording.referenceIncident.side
            == PlateIncidenceSide::NegativeLocalZ
        ? 1.0
        : -1.0;
    if (observerCentre.z * forwardSign <= 0.0) {
        throw std::invalid_argument(
            "thin-plate replay screen/probe must lie on the transmitted output side");
    }
    const double extentWidth
        = recording.objectIncident.diagnostics.sampledExtentWidthMetres;
    const double extentHeight
        = recording.objectIncident.diagnostics.sampledExtentHeightMetres;
    const double samplingOffsetX = observerCentre.x
        - recording.objectIncident.diagnostics.sampledCentreXMetres;
    const double samplingOffsetY = observerCentre.y
        - recording.objectIncident.diagnostics.sampledCentreYMetres;
    if (std::abs(samplingOffsetX) > 0.5 * extentWidth
        || std::abs(samplingOffsetY) > 0.5 * extentHeight) {
        throw std::invalid_argument(
            "thin-plate replay observation centre exceeds the padded sampling support");
    }
    const auto [observerWidth, observerHeight] = observerExtent(observer);
    if (observerWidth < recording.objectIncident.diagnostics.sampledExtentWidthMetres
        || observerHeight
            < recording.objectIncident.diagnostics.sampledExtentHeightMetres) {
        throw std::invalid_argument(
            "thin-plate replay observation plane is smaller than the sampled recording window");
    }
    if (!fftBackend.supportsDimensions(
            recording.relativeObjectField.width(),
            recording.relativeObjectField.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the thin-plate replay grid");
    }

    auto replayAtPlate = replayKind == ThinPlateReplayKind::OrdinaryReference
        ? recording.relativeReferenceField
        : makeConjugateReplayField(recording.relativeReferenceField);
    auto full = replayThinAmplitudeHologram(
        recording.hologram, replayAtPlate).field;
    auto orders = decomposeUnclampedLinearReplayOrders(
        recording.hologram,
        recording.relativeObjectField,
        recording.relativeReferenceField,
        replayAtPlate);
    auto zero = std::move(orders.zeroOrderField);
    auto objectOrder = std::move(orders.objectBearingOrderField);
    auto conjugateOrder = std::move(orders.conjugateOrderField);

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    const bool tilted = !parallelAxisAligned;
    const bool shifted = !tilted
        && (samplingOffsetX != 0.0 || samplingOffsetY != 0.0);
    compute::propagation::AngularSpectrumDiagnostics diagnostics;
    compute::propagation::TiltedPlaneDiagnostics tiltedDiagnostics;
    if (tilted) {
        auto inputPlane = plate->transform;
        inputPlane.translationMetres = math::transformPointLocalToWorld(
            plate->transform,
            {
                recording.objectIncident.diagnostics.sampledCentreXMetres,
                recording.objectIncident.diagnostics.sampledCentreYMetres,
                0.0,
            });
        const math::Vec3d preferredDirection
            = plate->transform.localZAxisInWorld * forwardSign;
        compute::propagation::TiltedPlanePropagator tiltedPropagator(
            fftBackend);
        tiltedDiagnostics = tiltedPropagator.propagatePaddedInPlace(
            full, inputPlane, observer.transform, preferredDirection);
        static_cast<void>(tiltedPropagator.propagatePaddedInPlace(
            zero, inputPlane, observer.transform, preferredDirection));
        static_cast<void>(tiltedPropagator.propagatePaddedInPlace(
            objectOrder, inputPlane, observer.transform, preferredDirection));
        static_cast<void>(tiltedPropagator.propagatePaddedInPlace(
            conjugateOrder, inputPlane, observer.transform, preferredDirection));
    } else if (shifted) {
        diagnostics = propagator.propagateShiftedPaddedInPlace(
            full,
            observerCentre.z,
            samplingOffsetX,
            samplingOffsetY);
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            zero,
            observerCentre.z,
            samplingOffsetX,
            samplingOffsetY));
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            objectOrder,
            observerCentre.z,
            samplingOffsetX,
            samplingOffsetY));
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            conjugateOrder,
            observerCentre.z,
            samplingOffsetX,
            samplingOffsetY));
    } else {
        diagnostics = propagator.propagateInPlace(full, observerCentre.z);
        static_cast<void>(propagator.propagateInPlace(
            zero, observerCentre.z));
        static_cast<void>(propagator.propagateInPlace(
            objectOrder, observerCentre.z));
        static_cast<void>(propagator.propagateInPlace(
            conjugateOrder, observerCentre.z));
    }
    return {
        .plateComponentId = recording.plateComponentId,
        .observationComponentId = std::move(observationComponentId),
        .sourceRevision = recording.sourceRevision,
        .replayKind = replayKind,
        .signedObservationDistanceMetres = observerCentre.z,
        .observationOffsetXMetres = observerCentre.x,
        .observationOffsetYMetres = observerCentre.y,
        .usedShiftedPaddedPropagation = shifted,
        .usedTiltedPlanePropagation = tilted,
        .replayAtPlate = std::move(replayAtPlate),
        .fullReplayAtObservation = std::move(full),
        .zeroOrderAtObservation = std::move(zero),
        .objectBearingOrderAtObservation = std::move(objectOrder),
        .conjugateOrderAtObservation = std::move(conjugateOrder),
        .propagation = diagnostics,
        .tiltedPropagation = tiltedDiagnostics,
    };
}

} // namespace holobench::optics::holography
