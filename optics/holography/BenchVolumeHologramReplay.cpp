#include "optics/holography/BenchVolumeHologramReplay.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/scene/BenchPathEvidence.hpp"

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
            && observer->kind != scene::BenchComponentKind::FieldProbe
            && observer->kind != scene::BenchComponentKind::HolographicPlate)) {
        throw std::invalid_argument(
            "volume replay observation must be a placed screen, field probe, or the recorded plate");
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
    if (observer.kind == scene::BenchComponentKind::HolographicPlate) {
        const auto& value = std::get<scene::HolographicPlateParameters>(
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

void requireMatchingRecordedSampling(
    const VolumePlateRecordingResult& recording,
    const scene::HolographicPlateParameters& plate,
    const PlateFieldSamplingOptions& sampling) {
    if (!recording.objectIncident.has_value()
        || !recording.referenceIncident.has_value()) {
        return;
    }
    const auto& object = *recording.objectIncident;
    const auto& reference = *recording.referenceIncident;
    const double extentWidth = sampling.extentWidthMetres == 0.0
        ? plate.widthMetres : sampling.extentWidthMetres;
    const double extentHeight = sampling.extentHeightMetres == 0.0
        ? plate.heightMetres : sampling.extentHeightMetres;
    const auto matches = [&](const SampledPlateIncidentField& incident) {
        return incident.field.width() == sampling.sampleWidth
            && incident.field.height() == sampling.sampleHeight
            && incident.field.refractiveIndex() == sampling.refractiveIndex
            && incident.diagnostics.sampledExtentWidthMetres == extentWidth
            && incident.diagnostics.sampledExtentHeightMetres == extentHeight
            && incident.diagnostics.sampledCentreXMetres
                == sampling.centreXMetres
            && incident.diagnostics.sampledCentreYMetres
                == sampling.centreYMetres;
    };
    if (!matches(object) || !matches(reference)
        || object.field.width() != reference.field.width()
        || object.field.height() != reference.field.height()
        || object.field.pitchXMetres() != reference.field.pitchXMetres()
        || object.field.pitchYMetres() != reference.field.pitchYMetres()) {
        throw std::invalid_argument(
            "volume replay sampling must match the recorded sampled wave evidence");
    }
}

math::RigidTransform3d makeBeamNormalFrame(
    const math::RigidTransform3d& sourcePlane,
    math::Vec3d originMetres,
    math::Vec3d direction) {
    const math::Vec3d zAxis = math::normalized(direction);
    math::Vec3d xCandidate = sourcePlane.localXAxisInWorld
        - zAxis * math::dot(sourcePlane.localXAxisInWorld, zAxis);
    if (math::lengthSquared(xCandidate) <= 1e-12) {
        xCandidate = sourcePlane.localYAxisInWorld
            - zAxis * math::dot(sourcePlane.localYAxisInWorld, zAxis);
    }
    const math::Vec3d xAxis = math::normalized(xCandidate);
    const math::Vec3d yAxis = math::cross(zAxis, xAxis);
    math::RigidTransform3d result {
        .translationMetres = originMetres,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = yAxis,
        .localZAxisInWorld = zAxis,
    };
    math::validateRigidTransform(result);
    return result;
}

struct DerivedObservationPath final {
    scene::BeamState terminalBeam;
    std::vector<scene::BenchPathInteraction> interactions;
};

DerivedObservationPath traceDerivedObservationPath(
    const scene::BenchScene& bench,
    const scene::BenchComponent& plate,
    const PlateIncidentBranch& replayBranch,
    math::Vec3d sourcePoint,
    math::Vec3d reconstructedDirectionWorld,
    double reconstructedPowerWatts,
    std::string_view observationComponentId,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    const auto sourceFrame = makeBeamNormalFrame(
        plate.transform, sourcePoint, reconstructedDirectionWorld);
    const scene::BeamState seed {
        .wavelengthMetres = replayBranch.beam.wavelengthMetres,
        .powerWatts = reconstructedPowerWatts,
        .phaseRadians = 0.0,
        .coherenceId = replayBranch.beam.coherenceId,
        .accumulatedOpticalPathMetres = 0.0,
        .originMetres = sourcePoint,
        .direction = reconstructedDirectionWorld,
        .localFrame = sourceFrame,
        .provenance = {
            .branchId = 1U,
            .parentBranchId = 0U,
            .componentPath = {plate.id},
        },
    };
    const auto trace = ray::traceDerivedBenchBeam(
        bench, seed, {}, lensPrescriptions);
    const scene::OpticalInteraction* terminal = nullptr;
    for (const auto& interaction : trace.interactions) {
        if (interaction.componentId != observationComponentId
            || interaction.incidentBeam.provenance.componentPath.empty()
            || interaction.incidentBeam.provenance.componentPath.front()
                != plate.id) {
            continue;
        }
        if (terminal != nullptr) {
            throw std::invalid_argument(
                "derived volume reconstruction reaches the selected observation through more than one branch");
        }
        terminal = &interaction;
    }
    if (terminal == nullptr) {
        const auto failure = std::find_if(
            trace.terminations.begin(),
            trace.terminations.end(),
            [](const auto& termination) {
                return termination.reason
                    == scene::TraceTerminationReason::InvalidInteraction;
            });
        std::string reason
            = "derived volume reconstruction centre ray does not reach the selected observation";
        if (failure != trace.terminations.end()) {
            reason = "derived volume reconstruction path failed: "
                + failure->detail;
        }
        throw std::invalid_argument(reason);
    }
    return {
        .terminalBeam = terminal->incidentBeam,
        .interactions = scene::collectBenchPathInteractions(
            trace, *terminal),
    };
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
            && observer->kind != scene::BenchComponentKind::FieldProbe
            && observer->kind != scene::BenchComponentKind::HolographicPlate);
}

VolumePlateObservationReplayResult replayVolumeReflectionToObservation(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    const VolumePlateRecordingResult& recording,
    std::uint64_t replayBranchId,
    std::string observationComponentId,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    const ray::ILensPrescriptionResolver* lensPrescriptions,
    const slm::ISlmResponseResolver* slmResponses,
    double environmentTemperatureKelvin) {
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
    const auto& plateParameters
        = std::get<scene::HolographicPlateParameters>(plate->parameters);
    requireMatchingRecordedSampling(recording, plateParameters, sampling);
    const auto& observer = requireObservation(bench, observationComponentId);
    const bool observeAtRecordedPlate
        = observer.id == recording.plateComponentId;
    if (observer.kind == scene::BenchComponentKind::HolographicPlate
        && !observeAtRecordedPlate) {
        throw std::invalid_argument(
            "volume replay can only use its own recorded plate as an in-plane observation");
    }
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
    const bool parallelAxisAligned = xAlignment >= 1.0 - kParallelTolerance
        && yAlignment >= 1.0 - kParallelTolerance
        && normalAlignment >= 1.0 - kParallelTolerance;
    const auto observerCentre = math::transformPointWorldToLocal(
        plate->transform, observer.transform.translationMetres);
    const math::Vec3d reconstructedDirectionWorld
        = math::transformDirectionLocalToWorld(
            plate->transform, reconstructedExternal);
    const math::Vec3d plateToObserver
        = observer.transform.translationMetres
        - plate->transform.translationMetres;
    if (!observeAtRecordedPlate
        && math::dot(plateToObserver, reconstructedDirectionWorld) <= 0.0) {
        throw std::invalid_argument(
            "volume replay observation is not on the reconstructed reflection side");
    }
    if (!observeAtRecordedPlate && !parallelAxisAligned
        && std::abs(math::dot(
            observer.transform.localZAxisInWorld,
            reconstructedDirectionWorld)) <= 1e-8) {
        throw std::invalid_argument(
            "volume replay observation plane is grazing the reconstructed direction");
    }

    auto object = recording.objectIncident.has_value()
        ? *recording.objectIncident
        : samplePlateIncidentField(
            bench,
            fields,
            recording.pair.objectBranchId,
            sampling,
            fftBackend,
            {},
            lensPrescriptions,
            slmResponses,
            environmentTemperatureKelvin);
    auto reference = recording.referenceIncident.has_value()
        ? *recording.referenceIncident
        : samplePlateIncidentField(
            bench,
            fields,
            recording.pair.referenceBranchId,
            sampling,
            fftBackend,
            {},
            lensPrescriptions,
            slmResponses,
            environmentTemperatureKelvin);
    auto replay = replayBranchId == recording.pair.referenceBranchId
        ? reference
        : samplePlateIncidentField(
            bench,
            fields,
            replayBranchId,
            sampling,
            fftBackend,
            {},
            lensPrescriptions,
            slmResponses,
            environmentTemperatureKelvin);
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
    const double samplingOffsetX
        = observerCentre.x - sampling.centreXMetres;
    const double samplingOffsetY
        = observerCentre.y - sampling.centreYMetres;
    if (!observeAtRecordedPlate && (std::abs(samplingOffsetX)
            > 0.5 * object.diagnostics.sampledExtentWidthMetres
        || std::abs(samplingOffsetY)
            > 0.5 * object.diagnostics.sampledExtentHeightMetres)) {
        throw std::invalid_argument(
            "volume replay observation centre exceeds the padded sampling support");
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

    auto inputPlane = plate->transform;
    inputPlane.translationMetres = math::transformPointLocalToWorld(
        plate->transform,
        {sampling.centreXMetres, sampling.centreYMetres, 0.0});
    DerivedObservationPath derivedPath;
    bool routed = false;
    if (!observeAtRecordedPlate) {
        derivedPath = traceDerivedObservationPath(
            bench,
            *plate,
            replayBranch,
            inputPlane.translationMetres,
            reconstructedDirectionWorld,
            targetPower,
            observationComponentId,
            lensPrescriptions);
        routed = std::any_of(
            derivedPath.interactions.begin(),
            std::prev(derivedPath.interactions.end()),
            [&](const auto& interaction) {
                const auto* component = bench.find(interaction.componentId);
                return component != nullptr
                    && wave::requiresBeamFollowingWaveTransform(
                        component->kind);
            });
    }

    auto reconstructedAtObservation = reconstructedAtPlate;
    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    const bool tilted = !routed
        && !observeAtRecordedPlate && !parallelAxisAligned;
    const bool shifted = !routed && !tilted
        && !observeAtRecordedPlate
        && (samplingOffsetX != 0.0 || samplingOffsetY != 0.0);
    compute::propagation::AngularSpectrumDiagnostics propagation;
    compute::propagation::TiltedPlaneDiagnostics tiltedPropagation;
    compute::propagation::TiltedPlaneDiagnostics sourceRotation;
    wave::BeamFollowingFieldDiagnostics routedDiagnostics;
    bool rotatedToBeamFrame = false;
    if (observeAtRecordedPlate) {
        // This is the reconstructed exit field at the emulsion plane itself;
        // no fictitious zero-distance propagation or separate probe is used.
    } else if (routed) {
        const auto sourceFrame = makeBeamNormalFrame(
            plate->transform,
            inputPlane.translationMetres,
            reconstructedDirectionWorld);
        const bool sourceFramesMatch
            = math::dot(
                sourceFrame.localXAxisInWorld,
                inputPlane.localXAxisInWorld) >= 1.0 - kParallelTolerance
            && math::dot(
                sourceFrame.localYAxisInWorld,
                inputPlane.localYAxisInWorld) >= 1.0 - kParallelTolerance
            && math::dot(
                sourceFrame.localZAxisInWorld,
                inputPlane.localZAxisInWorld) >= 1.0 - kParallelTolerance;
        if (!sourceFramesMatch) {
            compute::propagation::TiltedPlanePropagator tiltedPropagator(
                fftBackend);
            sourceRotation = tiltedPropagator.propagatePaddedInPlace(
                reconstructedAtObservation,
                inputPlane,
                sourceFrame,
                reconstructedDirectionWorld);
            rotatedToBeamFrame = true;
        }
        const wave::BeamFollowingFieldOptions routedOptions {
            .sampleWidth = reconstructedAtObservation.width(),
            .sampleHeight = reconstructedAtObservation.height(),
            .extentWidthMetres
                = object.diagnostics.sampledExtentWidthMetres,
            .extentHeightMetres
                = object.diagnostics.sampledExtentHeightMetres,
            .centreXMetres = 0.0,
            .centreYMetres = 0.0,
            .refractiveIndex = 1.0,
            .slmResponses = slmResponses,
            .environmentTemperatureKelvin
                = environmentTemperatureKelvin,
        };
        auto routedField = wave::sampleDerivedBeamFollowingField(
            bench,
            reconstructedAtObservation,
            sourceFrame,
            derivedPath.terminalBeam,
            derivedPath.interactions,
            routedOptions,
            fftBackend,
            {},
            lensPrescriptions);
        reconstructedAtObservation = std::move(routedField.fieldAtTarget);
        routedDiagnostics = std::move(routedField.diagnostics);
    } else if (tilted) {
        compute::propagation::TiltedPlanePropagator tiltedPropagator(
            fftBackend);
        tiltedPropagation = tiltedPropagator.propagatePaddedInPlace(
            reconstructedAtObservation,
            inputPlane,
            observer.transform,
            reconstructedDirectionWorld);
    } else if (shifted) {
        propagation = propagator.propagateShiftedPaddedInPlace(
            reconstructedAtObservation,
            observerCentre.z,
            samplingOffsetX,
            samplingOffsetY);
    } else {
        propagation = propagator.propagateInPlace(
            reconstructedAtObservation, observerCentre.z);
    }
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
        .observationOffsetXMetres = observerCentre.x,
        .observationOffsetYMetres = observerCentre.y,
        .usedShiftedPaddedPropagation = shifted,
        .usedTiltedPlanePropagation = tilted,
        .usedRoutedWavePath = routed,
        .usedSourcePlaneToBeamFrameRotation = rotatedToBeamFrame,
        .replayPowerOnSampledWindowWatts
            = replay.diagnostics.integratedPowerWatts,
        .reconstructedPowerOnSampledWindowWatts = targetPower,
        .reconstructedAtPlate = std::move(reconstructedAtPlate),
        .reconstructedAtObservation = std::move(reconstructedAtObservation),
        .propagation = propagation,
        .tiltedPropagation = tiltedPropagation,
        .sourcePlaneToBeamFrameRotation = sourceRotation,
        .routedWavePath = std::move(routedDiagnostics),
    };
}

} // namespace holobench::optics::holography
