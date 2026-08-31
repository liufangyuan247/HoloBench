#include "optics/holography/BenchHologramReplay.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

#include "compute/fft/IFftBackend.hpp"

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
    if (xAlignment < 1.0 - kParallelTolerance
        || yAlignment < 1.0 - kParallelTolerance
        || normalAlignment < 1.0 - kParallelTolerance) {
        throw std::invalid_argument(
            "thin-plate replay currently requires a parallel axis-aligned observation plane");
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
    const bool shifted = observerCentre.x != 0.0 || observerCentre.y != 0.0;
    compute::propagation::AngularSpectrumDiagnostics diagnostics;
    if (shifted) {
        diagnostics = propagator.propagateShiftedPaddedInPlace(
            full,
            observerCentre.z,
            observerCentre.x,
            observerCentre.y);
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            zero,
            observerCentre.z,
            observerCentre.x,
            observerCentre.y));
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            objectOrder,
            observerCentre.z,
            observerCentre.x,
            observerCentre.y));
        static_cast<void>(propagator.propagateShiftedPaddedInPlace(
            conjugateOrder,
            observerCentre.z,
            observerCentre.x,
            observerCentre.y));
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
        .replayAtPlate = std::move(replayAtPlate),
        .fullReplayAtObservation = std::move(full),
        .zeroOrderAtObservation = std::move(zero),
        .objectBearingOrderAtObservation = std::move(objectOrder),
        .conjugateOrderAtObservation = std::move(conjugateOrder),
        .propagation = diagnostics,
    };
}

} // namespace holobench::optics::holography
