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

} // namespace

bool VolumePlateRecordingResult::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate;
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
    };
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
