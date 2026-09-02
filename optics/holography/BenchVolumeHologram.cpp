#include "optics/holography/BenchVolumeHologram.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

namespace holobench::optics::holography {
namespace {

const PlateIncidentBranch& branchById(
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId) {
    const auto found = std::find_if(
        fields.branches.begin(), fields.branches.end(),
        [branchId](const auto& branch) {
            return branch.beam.provenance.branchId == branchId;
        });
    if (found == fields.branches.end()) {
        throw std::invalid_argument(
            "volume-plate recording branch was not found");
    }
    return *found;
}

void validateMaterial(const VolumePlateMaterial& material) {
    if (!std::isfinite(material.averageRefractiveIndex)
        || material.averageRefractiveIndex <= 0.0
        || !std::isfinite(material.refractiveIndexModulation)
        || material.refractiveIndexModulation < 0.0
        || material.refractiveIndexModulation
            >= material.averageRefractiveIndex
        || !std::isfinite(material.isotropicLinearShrinkageFraction)
        || material.isotropicLinearShrinkageFraction < 0.0
        || material.isotropicLinearShrinkageFraction >= 1.0) {
        throw std::invalid_argument(
            "volume-plate material parameters must be finite and physical");
    }
}

math::Vec3d refractIntoMaterial(
    math::Vec3d externalDirection,
    double refractiveIndex) {
    const double internalX = externalDirection.x / refractiveIndex;
    const double internalY = externalDirection.y / refractiveIndex;
    const double transverseSquared = internalX * internalX + internalY * internalY;
    if (!std::isfinite(transverseSquared) || transverseSquared >= 1.0) {
        throw std::invalid_argument(
            "incident branch cannot propagate inside the configured plate material");
    }
    const double internalZ = std::copysign(
        std::sqrt(1.0 - transverseSquared), externalDirection.z);
    return {internalX, internalY, internalZ};
}

VolumePlateRecordingResult attachSampledFields(
    VolumePlateRecordingResult recording,
    SampledPlateIncidentField objectIncident,
    SampledPlateIncidentField referenceIncident,
    const scene::BenchScene& bench) {
    const auto sameSampling = [&] {
        return objectIncident.field.width() == referenceIncident.field.width()
            && objectIncident.field.height() == referenceIncident.field.height()
            && objectIncident.field.pitchXMetres()
                == referenceIncident.field.pitchXMetres()
            && objectIncident.field.pitchYMetres()
                == referenceIncident.field.pitchYMetres()
            && objectIncident.field.refractiveIndex() == 1.0
            && referenceIncident.field.refractiveIndex() == 1.0
            && objectIncident.diagnostics.sampledExtentWidthMetres
                == referenceIncident.diagnostics.sampledExtentWidthMetres
            && objectIncident.diagnostics.sampledExtentHeightMetres
                == referenceIncident.diagnostics.sampledExtentHeightMetres
            && objectIncident.diagnostics.sampledCentreXMetres
                == referenceIncident.diagnostics.sampledCentreXMetres
            && objectIncident.diagnostics.sampledCentreYMetres
                == referenceIncident.diagnostics.sampledCentreYMetres;
    };
    if (objectIncident.isStaleFor(bench)
        || referenceIncident.isStaleFor(bench)
        || objectIncident.plateComponentId != recording.plateComponentId
        || referenceIncident.plateComponentId != recording.plateComponentId
        || objectIncident.branchId != recording.pair.objectBranchId
        || referenceIncident.branchId != recording.pair.referenceBranchId
        || objectIncident.role != RecordingBranchRole::Object
        || referenceIncident.role != RecordingBranchRole::Reference
        || objectIncident.field.vacuumWavelengthMetres()
            != recording.pair.wavelengthMetres
        || referenceIncident.field.vacuumWavelengthMetres()
            != recording.pair.wavelengthMetres
        || !sameSampling()) {
        throw std::invalid_argument(
            "volume recording sampled fields do not match the current branch pair");
    }
    if (!std::isfinite(objectIncident.diagnostics.integratedPowerWatts)
        || objectIncident.diagnostics.integratedPowerWatts <= 0.0
        || !std::isfinite(referenceIncident.diagnostics.integratedPowerWatts)
        || referenceIncident.diagnostics.integratedPowerWatts <= 0.0) {
        throw std::invalid_argument(
            "volume recording requires non-zero finite sampled object and reference power");
    }
    recording.objectIncident = std::move(objectIncident);
    recording.referenceIncident = std::move(referenceIncident);
    return recording;
}

} // namespace

bool VolumePlateRecordingResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate
        || objectIncident.has_value() != referenceIncident.has_value()
        || (objectIncident.has_value()
            && (objectIncident->isStaleFor(bench)
                || referenceIncident->isStaleFor(bench)));
}

bool VolumePlateReplayResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate;
}

VolumePlateRecordingResult recordVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material) {
    validateMaterial(material);
    if (fields.isStaleFor(bench)) {
        throw std::invalid_argument(
            "volume-plate recording requires current incident branch evidence");
    }
    const auto* plate = bench.find(fields.plateComponentId);
    if (plate == nullptr) {
        throw std::invalid_argument("volume-plate component was not found");
    }
    const auto& plateParameters = std::get<scene::HolographicPlateParameters>(
        plate->parameters);
    const auto pair = makePlateRecordingPair(
        fields, objectBranchId, referenceBranchId);
    const auto& object = branchById(fields, objectBranchId);
    const auto& reference = branchById(fields, referenceBranchId);
    const auto objectInternal = refractIntoMaterial(
        object.localDirection, material.averageRefractiveIndex);
    const auto referenceInternal = refractIntoMaterial(
        reference.localDirection, material.averageRefractiveIndex);
    const double recordingWavenumber = 2.0 * std::numbers::pi
        * material.averageRefractiveIndex / pair.wavelengthMetres;
    const math::Vec3d gratingVector
        = (objectInternal - referenceInternal) * recordingWavenumber;
    const double gratingMagnitude = math::length(gratingVector);
    if (!std::isfinite(gratingMagnitude) || gratingMagnitude <= 0.0) {
        throw std::invalid_argument(
            "volume-plate branches do not form a non-zero grating vector");
    }
    const double normalizedHalfGrating = std::clamp(
        gratingMagnitude / (2.0 * recordingWavenumber), 0.0, 1.0);
    const auto volumeGeometry = pair.geometry == PlateRecordingGeometry::Reflection
        ? VolumeHologramGeometry::Reflection
        : VolumeHologramGeometry::Transmission;
    const double equivalentAngle
        = volumeGeometry == VolumeHologramGeometry::Reflection
        ? std::acos(normalizedHalfGrating)
        : std::asin(normalizedHalfGrating);
    const double slant = std::acos(std::clamp(
        std::abs(gratingVector.z) / gratingMagnitude, 0.0, 1.0));
    VolumeHologramParameters parameters {
        .geometry = volumeGeometry,
        .recordedThicknessMetres = plateParameters.thicknessMetres,
        .averageRefractiveIndex = material.averageRefractiveIndex,
        .refractiveIndexModulation = material.refractiveIndexModulation,
        .recordingVacuumWavelengthMetres = pair.wavelengthMetres,
        .replayVacuumWavelengthMetres = pair.wavelengthMetres,
        .recordingBraggAngleInMediumRadians = equivalentAngle,
        .replayAngleInMediumRadians = equivalentAngle,
        .isotropicLinearShrinkageFraction
            = material.isotropicLinearShrinkageFraction,
    };
    const auto nominalReplay = evaluateVolumeHologram(parameters);
    return {
        .plateComponentId = fields.plateComponentId,
        .sourceRevision = fields.sourceRevision,
        .pair = pair,
        .objectDirectionInMediumLocal = objectInternal,
        .referenceDirectionInMediumLocal = referenceInternal,
        .recordedGratingVectorLocalRadiansPerMetre = gratingVector,
        .recordedGratingPeriodMetres
            = 2.0 * std::numbers::pi / gratingMagnitude,
        .gratingSlantFromPlateNormalRadians = slant,
        .equivalentSymmetricBraggAngleInMediumRadians = equivalentAngle,
        .material = material,
        .nominalReplayParameters = parameters,
        .nominalReplay = nominalReplay,
        .objectIncident = std::nullopt,
        .referenceIncident = std::nullopt,
    };
}

VolumePlateRecordingResult recordVolumePlate(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material,
    const PlateFieldSamplingOptions& sampling,
    compute::fft::IFftBackend& fftBackend,
    std::span<const PlacedSlmSparseCommand> slmCommands,
    const ray::ILensPrescriptionResolver* lensPrescriptions) {
    if (sampling.refractiveIndex != 1.0) {
        throw std::invalid_argument(
            "volume recording samples the external plate plane and requires refractive index 1");
    }
    auto recording = recordVolumePlate(
        bench,
        fields,
        objectBranchId,
        referenceBranchId,
        material);
    auto objectIncident = samplePlateIncidentField(
        bench,
        fields,
        objectBranchId,
        sampling,
        fftBackend,
        slmCommands,
        lensPrescriptions);
    auto referenceIncident = samplePlateIncidentField(
        bench,
        fields,
        referenceBranchId,
        sampling,
        fftBackend,
        slmCommands,
        lensPrescriptions);
    if (!objectIncident.diagnostics.carrierSampled
        || !referenceIncident.diagnostics.carrierSampled) {
        throw std::invalid_argument(
            "volume recording sampling does not resolve both transverse carriers");
    }
    return attachSampledFields(
        std::move(recording),
        std::move(objectIncident),
        std::move(referenceIncident),
        bench);
}

VolumePlateRecordingResult recordVolumePlateFromSampledFields(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId,
    const VolumePlateMaterial& material,
    SampledPlateIncidentField objectIncident,
    SampledPlateIncidentField referenceIncident) {
    return attachSampledFields(
        recordVolumePlate(
            bench,
            fields,
            objectBranchId,
            referenceBranchId,
            material),
        std::move(objectIncident),
        std::move(referenceIncident),
        bench);
}

VolumePlateReplayResult replayVolumePlate(
    const scene::BenchScene& bench,
    const VolumePlateRecordingResult& recording,
    double replayVacuumWavelengthMetres,
    double replayAngleInMediumRadians) {
    if (recording.isStaleFor(bench)) {
        throw std::invalid_argument(
            "volume-plate replay requires a current recording");
    }
    auto parameters = recording.nominalReplayParameters;
    parameters.replayVacuumWavelengthMetres = replayVacuumWavelengthMetres;
    parameters.replayAngleInMediumRadians = replayAngleInMediumRadians;
    return {
        .plateComponentId = recording.plateComponentId,
        .sourceRevision = recording.sourceRevision,
        .replayVacuumWavelengthMetres = replayVacuumWavelengthMetres,
        .replayAngleInMediumRadians = replayAngleInMediumRadians,
        .volume = evaluateVolumeHologram(parameters),
    };
}

} // namespace holobench::optics::holography
