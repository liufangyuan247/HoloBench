#include "optics/holography/PlateIncidentFields.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>
#include <utility>

namespace holobench::optics::holography {
namespace {

const scene::BenchComponent& requirePlate(
    const scene::BenchScene& bench,
    std::string_view plateComponentId) {
    const auto* plate = bench.find(plateComponentId);
    if (plate == nullptr) {
        throw std::invalid_argument("holographic plate component was not found");
    }
    if (plate->kind != scene::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "plate incident fields require a holographic plate component");
    }
    return *plate;
}

RecordingBranchRole sourceRole(
    const scene::BenchScene& bench,
    const scene::BeamState& beam) {
    if (beam.provenance.componentPath.empty()) {
        throw std::invalid_argument("incident branch has no source provenance");
    }
    const auto* source = bench.find(beam.provenance.componentPath.front());
    if (source == nullptr) {
        throw std::invalid_argument(
            "incident branch source provenance is missing from the bench");
    }
    if (source->kind == scene::BenchComponentKind::ObjectWavefrontSource) {
        return RecordingBranchRole::Object;
    }
    if (source->kind == scene::BenchComponentKind::LaserSource) {
        for (std::size_t i = 1; i < beam.provenance.componentPath.size(); ++i) {
            const auto* comp = bench.find(beam.provenance.componentPath[i]);
            if (comp != nullptr
                && comp->kind == scene::BenchComponentKind::SpatialLightModulator) {
                return RecordingBranchRole::Object;
            }
        }
        return RecordingBranchRole::Reference;
    }
    throw std::invalid_argument(
        "incident branch provenance does not begin at a supported source");
}

const PlateIncidentBranch& requireBranch(
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    RecordingBranchRole requiredRole) {
    const auto found = std::find_if(
        fields.branches.begin(), fields.branches.end(),
        [branchId](const auto& branch) {
            return branch.beam.provenance.branchId == branchId;
        });
    if (found == fields.branches.end()) {
        throw std::invalid_argument(
            "recording branch was not found at the holographic plate");
    }
    if (found->role != requiredRole) {
        throw std::invalid_argument(
            "recording branch has the wrong object/reference role");
    }
    return *found;
}

} // namespace

bool PlateIncidentFieldSet::isStaleFor(
    const scene::BenchScene& bench) const noexcept {
    const auto* plate = bench.find(plateComponentId);
    return sourceRevision != bench.revision()
        || plate == nullptr
        || plate->kind != scene::BenchComponentKind::HolographicPlate;
}

PlateIncidentFieldSet collectPlateIncidentFields(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string plateComponentId) {
    if (traceGraph.sourceRevision != bench.revision()) {
        throw std::invalid_argument(
            "plate incident fields require a current trace graph");
    }
    const auto& plate = requirePlate(bench, plateComponentId);
    PlateIncidentFieldSet result {
        .plateComponentId = std::move(plateComponentId),
        .sourceRevision = traceGraph.sourceRevision,
        .branches = {},
    };

    for (const auto& interaction : traceGraph.interactions) {
        if (interaction.componentId != result.plateComponentId) {
            continue;
        }
        scene::validateBeamState(interaction.incidentBeam);
        const math::Vec3d localDirection = math::normalized(
            math::transformDirectionWorldToLocal(
                plate.transform, interaction.incidentBeam.direction));
        if (localDirection.z == 0.0) {
            throw std::invalid_argument("plate incident branch is exactly grazing");
        }
        result.branches.push_back({
            .role = sourceRole(bench, interaction.incidentBeam),
            .side = localDirection.z > 0.0
                ? PlateIncidenceSide::NegativeLocalZ
                : PlateIncidenceSide::PositiveLocalZ,
            .localHitPointMetres = math::transformPointWorldToLocal(
                plate.transform, interaction.hitPointMetres),
            .localDirection = localDirection,
            .incidenceAngleRadians = std::acos(std::clamp(
                std::abs(localDirection.z), 0.0, 1.0)),
            .beam = interaction.incidentBeam,
            .pathInteractions = scene::collectBenchPathInteractions(
                traceGraph, interaction),
        });
    }

    std::sort(result.branches.begin(), result.branches.end(),
        [](const auto& first, const auto& second) {
            if (first.beam.wavelengthMetres != second.beam.wavelengthMetres) {
                return first.beam.wavelengthMetres < second.beam.wavelengthMetres;
            }
            if (first.role != second.role) {
                return first.role < second.role;
            }
            return first.beam.provenance.branchId
                < second.beam.provenance.branchId;
        });
    return result;
}

PlateRecordingPair makePlateRecordingPair(
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId) {
    const auto& object = requireBranch(
        fields, objectBranchId, RecordingBranchRole::Object);
    const auto& reference = requireBranch(
        fields, referenceBranchId, RecordingBranchRole::Reference);
    if (!scene::canInterfere(object.beam, reference.beam)) {
        throw std::invalid_argument(
            "object and reference branches must share wavelength and coherence identity");
    }
    const double directionDot = std::clamp(
        math::dot(object.localDirection, reference.localDirection), -1.0, 1.0);
    return {
        .objectBranchId = objectBranchId,
        .referenceBranchId = referenceBranchId,
        .geometry = object.side == reference.side
            ? PlateRecordingGeometry::Transmission
            : PlateRecordingGeometry::Reflection,
        .wavelengthMetres = object.beam.wavelengthMetres,
        .signedOpticalPathDifferenceMetres
            = object.beam.accumulatedOpticalPathMetres
            - reference.beam.accumulatedOpticalPathMetres,
        .crossingAngleRadians = std::acos(directionDot),
    };
}

} // namespace holobench::optics::holography
