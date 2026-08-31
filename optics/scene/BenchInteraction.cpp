#include "optics/scene/BenchInteraction.hpp"

#include <cmath>
#include <stdexcept>
#include <utility>

namespace holobench::optics::scene {
namespace {

constexpr double kFrameTolerance = 2e-12;

math::RigidTransform3d beamFrame(math::Vec3d originMetres, math::Vec3d direction) {
    const math::Vec3d zAxis = math::normalized(direction);
    const math::Vec3d referenceAxis = std::abs(math::dot(zAxis, {0.0, 1.0, 0.0})) < 0.999
        ? math::Vec3d {0.0, 1.0, 0.0}
        : math::Vec3d {1.0, 0.0, 0.0};
    const math::Vec3d xAxis = math::normalized(math::cross(referenceAxis, zAxis));
    const math::Vec3d yAxis = math::cross(zAxis, xAxis);
    return {
        .translationMetres = originMetres,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = yAxis,
        .localZAxisInWorld = zAxis,
    };
}

BranchProvenance childProvenance(
    const BeamState& incidentAtHit,
    std::uint64_t branchId) {
    BranchProvenance result {
        .branchId = branchId,
        .parentBranchId = incidentAtHit.provenance.branchId,
        .componentPath = incidentAtHit.provenance.componentPath,
    };
    return result;
}

BeamState makeOutgoing(
    const BeamState& incidentAtHit,
    math::Vec3d direction,
    double powerScale,
    std::uint64_t branchId) {
    BeamState result = incidentAtHit;
    result.powerWatts *= powerScale;
    result.direction = math::normalized(direction);
    result.originMetres = incidentAtHit.originMetres;
    result.localFrame = beamFrame(result.originMetres, result.direction);
    result.provenance = childProvenance(incidentAtHit, branchId);
    return result;
}

} // namespace

void validateBeamState(const BeamState& beam) {
    if (!std::isfinite(beam.wavelengthMetres) || beam.wavelengthMetres <= 0.0) {
        throw std::invalid_argument("beam wavelength_m must be finite and positive");
    }
    if (!std::isfinite(beam.powerWatts) || beam.powerWatts < 0.0) {
        throw std::invalid_argument("beam power_w must be finite and non-negative");
    }
    if (!std::isfinite(beam.phaseRadians)) {
        throw std::invalid_argument("beam phase_rad must be finite");
    }
    if (!isStableBenchId(beam.coherenceId)) {
        throw std::invalid_argument("beam coherence ID is invalid");
    }
    if (!std::isfinite(beam.accumulatedOpticalPathMetres)
        || beam.accumulatedOpticalPathMetres < 0.0) {
        throw std::invalid_argument("beam optical path_m must be finite and non-negative");
    }
    if (!math::isFinite(beam.originMetres)) {
        throw std::invalid_argument("beam origin_m must be finite");
    }
    if (!math::isFinite(beam.direction) || math::lengthSquared(beam.direction) <= 0.0) {
        throw std::invalid_argument("beam direction must be finite and non-zero");
    }
    math::validateRigidTransform(beam.localFrame);
    const math::Vec3d normalizedDirection = math::normalized(beam.direction);
    if (math::length(beam.localFrame.translationMetres - beam.originMetres) > kFrameTolerance
        || math::length(beam.localFrame.localZAxisInWorld - normalizedDirection) > kFrameTolerance) {
        throw std::invalid_argument("beam local frame must originate at the beam origin and follow its direction");
    }
    for (const auto& id : beam.provenance.componentPath) {
        if (!isStableBenchId(id)) {
            throw std::invalid_argument("beam branch provenance contains an invalid component ID");
        }
    }
}

void validateTraceBudget(const TraceBudget& budget) {
    if (budget.maximumHopsPerBranch == 0 || budget.maximumHopsPerBranch > 100'000) {
        throw std::invalid_argument("trace hop budget must be in [1, 100000]");
    }
    if (budget.maximumBranches == 0 || budget.maximumBranches > 1'000'000) {
        throw std::invalid_argument("trace branch budget must be in [1, 1000000]");
    }
    if (!std::isfinite(budget.minimumPowerWatts) || budget.minimumPowerWatts < 0.0) {
        throw std::invalid_argument("trace minimum power_w must be finite and non-negative");
    }
    if (!std::isfinite(budget.escapeDistanceMetres) || budget.escapeDistanceMetres <= 0.0) {
        throw std::invalid_argument("trace escape distance_m must be finite and positive");
    }
}

OpticalInteraction interactIdealBeamSplitter(
    const BeamState& incoming,
    const BenchComponent& splitter,
    math::Vec3d hitPointMetres,
    std::uint64_t reflectedBranchId,
    std::uint64_t transmittedBranchId) {
    validateBeamState(incoming);
    validateBenchComponent(splitter);
    if (splitter.kind != BenchComponentKind::BeamSplitterCombiner) {
        throw std::invalid_argument("ideal beam-splitter interaction requires a splitter component");
    }
    if (!math::isFinite(hitPointMetres)) {
        throw std::invalid_argument("splitter hit point_m must be finite");
    }
    if (reflectedBranchId == transmittedBranchId) {
        throw std::invalid_argument("splitter output branch IDs must be distinct");
    }
    if (reflectedBranchId == incoming.provenance.branchId
        || transmittedBranchId == incoming.provenance.branchId) {
        throw std::invalid_argument("splitter output branch IDs must differ from their parent");
    }

    const auto& parameters = std::get<BeamSplitterParameters>(splitter.parameters);
    const math::Vec3d incomingDirection = math::normalized(incoming.direction);
    const math::Vec3d hitDisplacement = hitPointMetres - incoming.originMetres;
    const double hitDistance = math::dot(hitDisplacement, incomingDirection);
    const math::Vec3d transverseError = hitDisplacement - incomingDirection * hitDistance;
    if (hitDistance < 0.0
        || math::length(transverseError) > 1e-9 * (1.0 + hitDistance)) {
        throw std::invalid_argument("splitter hit point must lie on the forward incoming ray");
    }
    const math::Vec3d normal = splitter.transform.localZAxisInWorld;
    const math::Vec3d reflectedDirection = incomingDirection
        - normal * (2.0 * math::dot(incomingDirection, normal));
    BeamState incidentAtHit = incoming;
    incidentAtHit.originMetres = hitPointMetres;
    incidentAtHit.accumulatedOpticalPathMetres += hitDistance;
    incidentAtHit.localFrame = beamFrame(hitPointMetres, incomingDirection);
    incidentAtHit.provenance.componentPath.push_back(splitter.id);
    validateBeamState(incidentAtHit);

    OpticalInteraction result {
        .componentId = splitter.id,
        .hitPointMetres = hitPointMetres,
        .distanceMetres = hitDistance,
        .incidentBeam = incidentAtHit,
        .outgoing = {},
        .diagnostics = {},
    };
    if (parameters.powerReflectivity > 0.0) {
        result.outgoing.push_back({
            .interaction = BranchInteractionKind::Reflected,
            .beam = makeOutgoing(
                incidentAtHit,
                reflectedDirection,
                parameters.powerReflectivity,
                reflectedBranchId),
        });
    }
    if (parameters.powerTransmissivity > 0.0) {
        result.outgoing.push_back({
            .interaction = BranchInteractionKind::Transmitted,
            .beam = makeOutgoing(
                incidentAtHit,
                incomingDirection,
                parameters.powerTransmissivity,
                transmittedBranchId),
        });
    }
    const double loss = 1.0 - parameters.powerReflectivity - parameters.powerTransmissivity;
    if (loss > 1e-12) {
        result.diagnostics.push_back("splitter absorbs configured residual power");
    }
    return result;
}

bool canInterfere(const BeamState& first, const BeamState& second) noexcept {
    return first.wavelengthMetres == second.wavelengthMetres
        && first.coherenceId == second.coherenceId;
}

} // namespace holobench::optics::scene
