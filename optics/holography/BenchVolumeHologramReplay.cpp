#include "optics/holography/BenchVolumeHologramReplay.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>

#include "compute/fft/IFftBackend.hpp"
#include "core/field/FieldObservables.hpp"

namespace holobench::optics::holography {
namespace {

constexpr double kParallelTolerance = 2e-12;

const PlateIncidentBranch& requireBranch(
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId) {
    const auto found = std::find_if(
        fields.branches.begin(), fields.branches.end(),
        [branchId](const auto& branch) {
            return branch.beam.provenance.branchId == branchId;
        });
    if (found == fields.branches.end()) {
        throw std::invalid_argument(
            "volume replay branch was not found at the recorded plate");
    }
    return *found;
}

const scene::BenchComponent& requireObservation(
    const scene::BenchScene& bench,
    std::string_view componentId) {
    const auto* observer = bench.find(componentId);
    if (observer == nullptr
        || (observer->kind != scene::BenchComponentKind::ScreenDetector
            && observer->kind != scene::BenchComponentKind::FieldProbe)) {
        throw std::invalid_argument(
            "volume replay observation must be a placed screen or field probe");
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
    const auto& value = std::get<scene::FieldProbeParameters>(
        observer.parameters);
    return {value.widthMetres, value.heightMetres};
}

math::Vec3d refractIntoMaterial(
    math::Vec3d externalDirection,
    double refractiveIndex) {
    const double internalX = externalDirection.x / refractiveIndex;
    const double internalY = externalDirection.y / refractiveIndex;
    const double transverseSquared
        = internalX * internalX + internalY * internalY;
    if (!std::isfinite(transverseSquared) || transverseSquared >= 1.0) {
        throw std::invalid_argument(
            "volume replay branch cannot propagate inside the plate material");
    }
    return {
        internalX,
        internalY,
        std::copysign(std::sqrt(1.0 - transverseSquared), externalDirection.z),
    };
}

math::Vec3d refractOutOfMaterial(
    math::Vec3d internalDirection,
    double refractiveIndex) {
    const double externalX = internalDirection.x * refractiveIndex;
    const double externalY = internalDirection.y * refractiveIndex;
    const double transverseSquared
        = externalX * externalX + externalY * externalY;
    if (!std::isfinite(transverseSquared) || transverseSquared >= 1.0) {
        throw std::invalid_argument(
            "volume reconstructed order is totally internally reflected at the plate boundary");
    }
    return {
        externalX,
        externalY,
        std::copysign(std::sqrt(1.0 - transverseSquared), internalDirection.z),
    };
}

math::Vec3d reflectedCoupledDirection(
    math::Vec3d replayDirection,
    math::Vec3d gratingDirection) {
    const double longitudinal = math::dot(replayDirection, gratingDirection);
    if (longitudinal >= 0.0) {
        throw std::invalid_argument(
            "volume replay branch approaches the reflection grating from the wrong coupled-wave side");
    }
    const math::Vec3d transverse
        = replayDirection - longitudinal * gratingDirection;
    const double transverseSquared = math::lengthSquared(transverse);
    if (!std::isfinite(transverseSquared) || transverseSquared >= 1.0) {
        throw std::invalid_argument(
            "volume reconstructed order is non-propagating in the plate material");
    }
    return transverse
        + std::sqrt(1.0 - transverseSquared) * gratingDirection;
}

void requireResolvedCarrier(
    const SampledPlateIncidentField& sampled,
    std::string_view label) {
    if (!sampled.diagnostics.carrierSampled) {
        throw std::invalid_argument(
            std::string(label)
            + " transverse carrier is not resolved by the replay sampling grid");
    }
}

} // namespace

bool VolumePlateObservationReplayResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    const auto* observer = bench.find(observationComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate
        || observer == nullptr
        || (observer->kind != scene::BenchComponentKind::ScreenDetector
            && observer->kind != scene::BenchComponentKind::FieldProbe);
}

VolumePlateObservationReplayResult replayVolumeReflectionToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const VolumePlateRecordingResult& recording,
    std::uint64_t replayBranchId,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend) {
    if (sampling.refractiveIndex != 1.0) {
        throw std::invalid_argument(
            "volume observation replay sampling is defined on the external air-side plate plane and requires refractive index 1");
    }
    if (recording.isStaleFor(bench) || fields.isStaleFor(bench)
        || fields.plateComponentId != recording.plateComponentId
        || fields.sourceRevision != recording.sourceRevision) {
        throw std::invalid_argument(
            "volume observation replay requires current matching plate evidence");
    }
    if (recording.pair.geometry != PlateRecordingGeometry::Reflection) {
        throw std::invalid_argument(
            "placed volume observation replay currently requires a reflection recording");
    }
    const auto* plate = bench.find(recording.plateComponentId);
    if (plate == nullptr) {
        throw std::invalid_argument("volume replay plate was not found");
    }
    const auto& observer = requireObservation(bench, observationComponentId);
    const auto& replayBranch = requireBranch(fields, replayBranchId);
    if (replayBranch.role != RecordingBranchRole::Reference) {
        throw std::invalid_argument(
            "volume replay illumination must be a reference-role laser branch");
    }

    const math::Vec3d replayInternal = refractIntoMaterial(
        replayBranch.localDirection,
        recording.material.averageRefractiveIndex);
    const math::Vec3d gratingDirection = math::normalized(
        recording.recordedGratingVectorLocalRadiansPerMetre);
    const auto reconstructedInternal = reflectedCoupledDirection(
        replayInternal, gratingDirection);
    const double replayAngle = std::acos(std::clamp(
        -math::dot(replayInternal, gratingDirection), 0.0, 1.0));
    auto braggReplay = replayVolumePlate(
        bench,
        recording,
        replayBranch.beam.wavelengthMetres,
        replayAngle);
    if (!braggReplay.volume.kogelnikEfficiencyEvaluated
        || !braggReplay.volume.diffractedOrderPropagating) {
        throw std::invalid_argument(
            "volume reflection replay does not produce a propagating coupled order");
    }
    const auto reconstructedExternal = refractOutOfMaterial(
        reconstructedInternal,
        recording.material.averageRefractiveIndex);

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
            "volume replay currently requires a parallel axis-aligned observation plane");
    }
    const auto observerCentre = math::transformPointWorldToLocal(
        plate->transform, observer.transform.translationMetres);
    if (observerCentre.z == 0.0
        || observerCentre.z * reconstructedExternal.z <= 0.0) {
        throw std::invalid_argument(
            "volume replay observation is not on the reconstructed reflection side");
    }

    auto object = samplePlateIncidentField(
        bench, fields, recording.pair.objectBranchId, sampling);
    auto reference = samplePlateIncidentField(
        bench, fields, recording.pair.referenceBranchId, sampling);
    auto replay = replayBranchId == recording.pair.referenceBranchId
        ? reference
        : samplePlateIncidentField(bench, fields, replayBranchId, sampling);
    requireResolvedCarrier(object, "recorded object");
    requireResolvedCarrier(reference, "recorded reference");
    requireResolvedCarrier(replay, "replay illumination");
    if (!fftBackend.supportsDimensions(
            object.field.width(), object.field.height())) {
        throw std::invalid_argument(
            "FFT backend does not support the volume replay sampling grid");
    }
    const auto [observerWidth, observerHeight] = observerExtent(observer);
    if (observerWidth < object.diagnostics.sampledExtentWidthMetres
        || observerHeight < object.diagnostics.sampledExtentHeightMetres) {
        throw std::invalid_argument(
            "volume replay observation plane is smaller than the sampled recording window");
    }
    const double coaxialTolerance = 0.5 * std::min(
        object.field.pitchXMetres(), object.field.pitchYMetres());
    if (std::hypot(observerCentre.x, observerCentre.y) > coaxialTolerance) {
        throw std::invalid_argument(
            "volume replay currently requires a coaxial observation plane");
    }
    const double lateralShift = std::hypot(
        observerCentre.z * reconstructedExternal.x
            / reconstructedExternal.z,
        observerCentre.z * reconstructedExternal.y
            / reconstructedExternal.z);
    if (lateralShift > coaxialTolerance) {
        throw std::invalid_argument(
            "volume replay currently requires an axis-aligned reconstructed order; off-axis observation resampling is not yet supported");
    }

    const double shrinkScale
        = 1.0 - recording.material.isotropicLinearShrinkageFraction;
    const math::Vec3d gratingCorrection
        = recording.recordedGratingVectorLocalRadiansPerMetre
        * (1.0 / shrinkScale - 1.0);
    field::ComplexField2D reconstructedAtPlate(
        object.field.width(),
        object.field.height(),
        object.field.pitchXMetres(),
        object.field.pitchYMetres(),
        replayBranch.beam.wavelengthMetres,
        1.0);
    for (std::size_t y = 0; y < reconstructedAtPlate.height(); ++y) {
        const double yMetres = reconstructedAtPlate.yCoordinateMetres(y);
        for (std::size_t x = 0; x < reconstructedAtPlate.width(); ++x) {
            const double correctionPhase = std::fma(
                gratingCorrection.x,
                reconstructedAtPlate.xCoordinateMetres(x),
                gratingCorrection.y * yMetres);
            reconstructedAtPlate.at(x, y)
                = replay.field.at(x, y)
                * object.field.at(x, y)
                * std::conj(reference.field.at(x, y))
                * std::polar(1.0, std::remainder(
                    correctionPhase, 2.0 * std::numbers::pi));
        }
    }
    const double rawNormalFlux = field::computeIntegratedIntensity(
        reconstructedAtPlate) * std::abs(reconstructedExternal.z);
    const double targetPower = replay.diagnostics.integratedPowerWatts
        * braggReplay.volume.kogelnik.diffractionEfficiency;
    if (!std::isfinite(rawNormalFlux) || rawNormalFlux <= 0.0
        || !std::isfinite(targetPower) || targetPower <= 0.0) {
        throw std::invalid_argument(
            "volume replay has no finite non-zero reconstructed sampled power");
    }
    const double amplitudeScale = std::sqrt(targetPower / rawNormalFlux);
    if (!std::isfinite(amplitudeScale)) {
        throw std::overflow_error(
            "volume reconstructed field normalization is not representable");
    }
    for (auto& sample : reconstructedAtPlate.samples()) {
        sample *= amplitudeScale;
    }

    auto reconstructedAtObservation = reconstructedAtPlate;
    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    const auto propagation = propagator.propagateInPlace(
        reconstructedAtObservation, observerCentre.z);
    return {
        .plateComponentId = recording.plateComponentId,
        .observationComponentId = std::move(observationComponentId),
        .sourceRevision = recording.sourceRevision,
        .replayBranchId = replayBranchId,
        .braggReplay = std::move(braggReplay),
        .replayDirectionInMediumLocal = replayInternal,
        .reconstructedDirectionInMediumLocal = reconstructedInternal,
        .reconstructedDirectionExternalLocal = reconstructedExternal,
        .signedObservationDistanceMetres = observerCentre.z,
        .replayPowerOnSampledWindowWatts
            = replay.diagnostics.integratedPowerWatts,
        .reconstructedPowerOnSampledWindowWatts = targetPower,
        .reconstructedAtPlate = std::move(reconstructedAtPlate),
        .reconstructedAtObservation = std::move(reconstructedAtObservation),
        .propagation = propagation,
    };
}

} // namespace holobench::optics::holography
