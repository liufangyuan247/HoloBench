#include "optics/ray/DynamicBenchTracer.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <deque>
#include <limits>
#include <optional>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include "optics/ray/GeometricElements.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/ray/Ray.hpp"

namespace holobench::optics::ray {
namespace {

struct PendingBranch final {
    scene::BeamState beam;
    std::size_t hopCount = 0;
};

struct CandidateHit final {
    const scene::BenchComponent* component = nullptr;
    double distanceMetres = 0.0;
    math::Vec3d pointMetres {};
    math::Vec3d localPointMetres {};
};

math::RigidTransform3d makeBeamFrame(math::Vec3d originMetres, math::Vec3d direction) {
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

scene::BeamState continueBranch(
    const scene::BeamState& incidentAtHit,
    math::Vec3d outgoingDirection,
    double powerScale) {
    scene::BeamState result = incidentAtHit;
    result.direction = math::normalized(outgoingDirection);
    result.powerWatts *= powerScale;
    result.localFrame = makeBeamFrame(result.originMetres, result.direction);
    scene::validateBeamState(result);
    return result;
}

scene::BeamState incidentAtHit(
    const scene::BeamState& incoming,
    const CandidateHit& hit) {
    scene::BeamState result = incoming;
    result.originMetres = hit.pointMetres;
    result.accumulatedOpticalPathMetres += hit.distanceMetres;
    result.localFrame = makeBeamFrame(result.originMetres, result.direction);
    result.provenance.componentPath.push_back(hit.component->id);
    scene::validateBeamState(result);
    return result;
}

bool isTraceablePlaneKind(scene::BenchComponentKind kind) noexcept {
    switch (kind) {
    case scene::BenchComponentKind::PlanarMirror:
    case scene::BenchComponentKind::BeamSplitterCombiner:
    case scene::BenchComponentKind::IdealThinLens:
    case scene::BenchComponentKind::Aperture:
    case scene::BenchComponentKind::RealLensAssembly:
    case scene::BenchComponentKind::SpatialFilter:
    case scene::BenchComponentKind::SpatialLightModulator:
    case scene::BenchComponentKind::ScreenDetector:
    case scene::BenchComponentKind::FieldProbe:
    case scene::BenchComponentKind::HolographicPlate:
        return true;
    case scene::BenchComponentKind::LaserSource:
    case scene::BenchComponentKind::ObjectWavefrontSource:
        return false;
    }
    return false;
}

bool isWithinFootprint(const scene::BenchComponent& component, math::Vec3d localPoint) {
    switch (component.kind) {
    case scene::BenchComponentKind::PlanarMirror: {
        const auto& value = std::get<scene::PlanarMirrorParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::BeamSplitterCombiner: {
        const auto& value = std::get<scene::BeamSplitterParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::IdealThinLens: {
        const auto radius = std::get<scene::IdealThinLensParameters>(component.parameters)
            .clearApertureDiameterMetres * 0.5;
        return localPoint.x * localPoint.x + localPoint.y * localPoint.y <= radius * radius;
    }
    case scene::BenchComponentKind::Aperture: {
        const auto& value = std::get<scene::ApertureParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::RealLensAssembly: {
        const auto radius = std::get<scene::RealLensAssemblyParameters>(component.parameters)
            .clearApertureDiameterMetres * 0.5;
        return localPoint.x * localPoint.x + localPoint.y * localPoint.y <= radius * radius;
    }
    case scene::BenchComponentKind::SpatialFilter: {
        const auto radius = std::get<scene::SpatialFilterParameters>(component.parameters)
            .clearApertureDiameterMetres * 0.5;
        return localPoint.x * localPoint.x + localPoint.y * localPoint.y <= radius * radius;
    }
    case scene::BenchComponentKind::SpatialLightModulator: {
        const auto& value = std::get<scene::SpatialLightModulatorParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::ScreenDetector: {
        const auto& value = std::get<scene::ScreenDetectorParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::FieldProbe: {
        const auto& value = std::get<scene::FieldProbeParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::HolographicPlate: {
        const auto& value = std::get<scene::HolographicPlateParameters>(component.parameters);
        return std::abs(localPoint.x) <= value.widthMetres * 0.5
            && std::abs(localPoint.y) <= value.heightMetres * 0.5;
    }
    case scene::BenchComponentKind::LaserSource:
    case scene::BenchComponentKind::ObjectWavefrontSource:
        return false;
    }
    return false;
}

std::optional<CandidateHit> findNextHit(
    const scene::BenchScene& bench,
    const scene::BeamState& beam,
    const ILensPrescriptionResolver* lensPrescriptions) {
    const Ray ray = makeRay(
        beam.originMetres, beam.direction, beam.wavelengthMetres, beam.powerWatts);
    std::optional<CandidateHit> nearest;
    for (const auto& component : bench.components()) {
        if (!isTraceablePlaneKind(component.kind)) {
            continue;
        }
        const auto intersection = intersectPlaneForward(
            ray,
            component.transform.translationMetres,
            component.transform.localZAxisInWorld,
            kGeometricIntersectionEpsilon);
        if (!intersection.hit) {
            continue;
        }
        double candidateDistance = intersection.signedDistanceMetres;
        math::Vec3d candidatePoint = intersection.pointMetres;
        if (component.kind == scene::BenchComponentKind::RealLensAssembly
            && lensPrescriptions != nullptr) {
            const auto& parameters
                = std::get<scene::RealLensAssemblyParameters>(
                    component.parameters);
            const auto* prescription = lensPrescriptions->resolve(
                parameters.prescriptionId);
            if (prescription != nullptr) {
                const auto placed = placeLensPrescription(
                    *prescription, component.transform);
                const auto& firstSurface = placed.surfaces.front();
                const Ray localRay = makeRay(
                    math::transformPointWorldToLocal(
                        firstSurface.localToWorld, ray.originMetres),
                    math::transformDirectionWorldToLocal(
                        firstSurface.localToWorld, ray.direction),
                    ray.wavelengthMetres,
                    ray.power);
                SurfaceIntersectionOptions options;
                options.maximumDistanceMetres = std::max(
                    options.maximumDistanceMetres,
                    intersection.signedDistanceMetres
                        + parameters.clearApertureDiameterMetres);
                const auto surfaceHit = intersectRotationalSurfaceForward(
                    localRay, firstSurface.geometry, options);
                if (surfaceHit.status == SurfaceIntersectionStatus::Miss
                    || surfaceHit.status
                        == SurfaceIntersectionStatus::Clipped) {
                    continue;
                }
                if (surfaceHit.status == SurfaceIntersectionStatus::Hit) {
                    candidateDistance = surfaceHit.distanceMetres;
                    candidatePoint = math::transformPointLocalToWorld(
                        firstSurface.localToWorld,
                        surfaceHit.pointMetres);
                }
            }
        }
        const auto localPoint = math::transformPointWorldToLocal(
            component.transform, candidatePoint);
        if (!isWithinFootprint(component, localPoint)) {
            continue;
        }
        const bool isCloser = !nearest.has_value()
            || candidateDistance < nearest->distanceMetres;
        const bool isStableTieBreak = nearest.has_value()
            && candidateDistance == nearest->distanceMetres
            && component.id < nearest->component->id;
        if (isCloser || isStableTieBreak) {
            nearest = CandidateHit {
                .component = &component,
                .distanceMetres = candidateDistance,
                .pointMetres = candidatePoint,
                .localPointMetres = localPoint,
            };
        }
    }
    return nearest;
}

bool apertureTransmits(const scene::ApertureParameters& aperture, math::Vec3d localPoint) {
    if (aperture.shape == scene::ApertureShape::DoubleSlit) {
        // The centre carrier is a routing aid only. The opaque gap between the
        // slits must not terminate the branch before the local sampled wave
        // model applies both openings and diffracts the field.
        return true;
    }
    if (aperture.shape == scene::ApertureShape::Rectangular) {
        return true;
    }
    const double normalizedX = localPoint.x / (aperture.widthMetres * 0.5);
    const double normalizedY = localPoint.y / (aperture.heightMetres * 0.5);
    return normalizedX * normalizedX + normalizedY * normalizedY <= 1.0;
}

bool spatialFilterTransmits(
    const scene::SpatialFilterParameters& filter,
    math::Vec3d localPoint) {
    const double pinholeRadius = filter.pinholeDiameterMetres * 0.5;
    return localPoint.x * localPoint.x + localPoint.y * localPoint.y
        <= pinholeRadius * pinholeRadius;
}

scene::OpticalInteraction singleOutputInteraction(
    const scene::BeamState& incoming,
    const CandidateHit& hit,
    math::Vec3d outgoingDirection,
    double powerScale,
    scene::BranchInteractionKind interactionKind,
    std::string diagnostic = {}) {
    const scene::BeamState incident = incidentAtHit(incoming, hit);
    scene::OpticalInteraction result {
        .componentId = hit.component->id,
        .hitPointMetres = hit.pointMetres,
        .distanceMetres = hit.distanceMetres,
        .incidentBeam = incident,
        .outgoing = {{
            .interaction = interactionKind,
            .beam = continueBranch(
                incident,
                outgoingDirection,
                powerScale),
        }},
        .diagnostics = {},
    };
    if (!diagnostic.empty()) {
        result.diagnostics.push_back(std::move(diagnostic));
    }
    return result;
}

void appendSegment(
    scene::BenchTraceGraph& graph,
    const scene::BeamState& beam,
    math::Vec3d endMetres) {
    graph.segments.push_back({
        .branchId = beam.provenance.branchId,
        .startMetres = beam.originMetres,
        .endMetres = endMetres,
        .wavelengthMetres = beam.wavelengthMetres,
        .powerWatts = beam.powerWatts,
    });
}

void appendTermination(
    scene::BenchTraceGraph& graph,
    const scene::BeamState& beam,
    scene::TraceTerminationReason reason,
    std::string detail) {
    graph.terminations.push_back({
        .branchId = beam.provenance.branchId,
        .reason = reason,
        .detail = std::move(detail),
    });
}

scene::OpticalInteraction interactMirror(
    const scene::BeamState& incoming,
    const CandidateHit& hit) {
    const auto& value = std::get<scene::PlanarMirrorParameters>(hit.component->parameters);
    const math::Vec3d direction = math::normalized(incoming.direction);
    const math::Vec3d normal = hit.component->transform.localZAxisInWorld;
    const math::Vec3d reflected = direction - normal * (2.0 * math::dot(direction, normal));
    return singleOutputInteraction(
        incoming,
        hit,
        reflected,
        value.powerReflectivity,
        scene::BranchInteractionKind::Reflected,
        value.powerReflectivity < 1.0 ? "mirror absorbs configured residual power" : "");
}

scene::OpticalInteraction interactThinLens(
    const scene::BeamState& incoming,
    const CandidateHit& hit) {
    const auto& value = std::get<scene::IdealThinLensParameters>(hit.component->parameters);
    const math::Vec3d localDirection = math::transformDirectionWorldToLocal(
        hit.component->transform, math::normalized(incoming.direction));
    const double propagationSign = localDirection.z > 0.0 ? 1.0 : -1.0;
    const double slopeX = localDirection.x / localDirection.z;
    const double slopeY = localDirection.y / localDirection.z;
    const double outgoingSlopeX = slopeX - propagationSign * hit.localPointMetres.x / value.focalLengthMetres;
    const double outgoingSlopeY = slopeY - propagationSign * hit.localPointMetres.y / value.focalLengthMetres;
    const math::Vec3d outgoingLocal {
        outgoingSlopeX * propagationSign,
        outgoingSlopeY * propagationSign,
        propagationSign,
    };
    const math::Vec3d outgoingWorld = math::transformDirectionLocalToWorld(
        hit.component->transform, outgoingLocal);
    return singleOutputInteraction(
        incoming,
        hit,
        outgoingWorld,
        1.0,
        scene::BranchInteractionKind::Transmitted);
}

const material::OpticalMaterial& requirePrescriptionMaterial(
    const SequentialLensPrescription& prescription,
    const std::string& materialId) {
    const auto found = std::find_if(
        prescription.materials.begin(), prescription.materials.end(),
        [&](const auto& material) { return material.id == materialId; });
    if (found == prescription.materials.end()) {
        throw std::logic_error(
            "validated prescription lost a referenced material");
    }
    return *found;
}

std::string_view sequentialTraceStatusName(
    SequentialTraceStatus status) noexcept {
    switch (status) {
    case SequentialTraceStatus::Completed: return "completed";
    case SequentialTraceStatus::Miss: return "missed a surface";
    case SequentialTraceStatus::Clipped: return "was clipped";
    case SequentialTraceStatus::OutsideSurfaceDomain:
        return "left the surface domain";
    case SequentialTraceStatus::SurfaceNonConvergence:
        return "did not converge at a surface";
    case SequentialTraceStatus::TotalInternalReflection:
        return "encountered total internal reflection";
    }
    return "has an unknown status";
}

struct RealLensInteractionResult final {
    scene::OpticalInteraction interaction;
    SequentialTraceResult trace;
};

RealLensInteractionResult interactRealLens(
    const scene::BeamState& incoming,
    const CandidateHit& hit,
    const ILensPrescriptionResolver* lensPrescriptions) {
    const auto& parameters
        = std::get<scene::RealLensAssemblyParameters>(
            hit.component->parameters);
    const auto unresolved = [&](std::string diagnostic) {
        return RealLensInteractionResult {
            .interaction = {
                .componentId = hit.component->id,
                .hitPointMetres = hit.pointMetres,
                .distanceMetres = hit.distanceMetres,
                .incidentBeam = incidentAtHit(incoming, hit),
                .outgoing = {},
                .diagnostics = {std::move(diagnostic)},
            },
            .trace = {},
        };
    };
    if (lensPrescriptions == nullptr) {
        return unresolved(
            "real-lens prescription resolver is unavailable for this dynamic scene");
    }
    const auto* prescription = lensPrescriptions->resolve(
        parameters.prescriptionId);
    if (prescription == nullptr) {
        return unresolved(
            "real-lens prescription '" + parameters.prescriptionId
            + "' is not present in the runtime catalog");
    }
    const auto placed = placeLensPrescription(
        *prescription, hit.component->transform);
    const auto& entryMaterial = requirePrescriptionMaterial(
        placed, placed.surfaces.front().materialBeforeId);
    const auto& exitMaterial = requirePrescriptionMaterial(
        placed, placed.surfaces.back().materialAfterId);
    const double entryIndex = material::refractiveIndexAtVacuumWavelength(
        entryMaterial, incoming.wavelengthMetres);
    const double exitIndex = material::refractiveIndexAtVacuumWavelength(
        exitMaterial, incoming.wavelengthMetres);
    constexpr double kBenchAmbientTolerance = 2e-12;
    if (std::abs(entryIndex - 1.0) > kBenchAmbientTolerance
        || std::abs(exitIndex - 1.0) > kBenchAmbientTolerance) {
        return unresolved(
            "real-lens Bench routing currently requires vacuum/air-equivalent entry and exit media");
    }
    SurfaceIntersectionOptions options;
    options.maximumDistanceMetres = std::max(
        options.maximumDistanceMetres,
        hit.distanceMetres + parameters.clearApertureDiameterMetres);
    auto trace = traceSequentialLens(
        makeRay(
            incoming.originMetres,
            incoming.direction,
            incoming.wavelengthMetres,
            incoming.powerWatts),
        placed,
        options);
    if (trace.status != SequentialTraceStatus::Completed
        || !trace.finalRay.has_value()
        || trace.records.empty()) {
        return unresolved(
            "real-lens prescription '" + parameters.prescriptionId + "' "
            + std::string(sequentialTraceStatusName(trace.status)));
    }

    scene::BeamState incident = incoming;
    incident.originMetres = trace.records.front().worldPointMetres;
    incident.accumulatedOpticalPathMetres +=
        trace.records.front().segmentOpticalPathMetres;
    incident.localFrame = makeBeamFrame(
        incident.originMetres, incident.direction);
    incident.provenance.componentPath.push_back(hit.component->id);
    scene::validateBeamState(incident);

    scene::BeamState outgoing = incident;
    outgoing.originMetres = trace.finalRay->originMetres;
    outgoing.direction = trace.finalRay->direction;
    outgoing.accumulatedOpticalPathMetres
        = incoming.accumulatedOpticalPathMetres
        + trace.totalOpticalPathMetres;
    outgoing.localFrame = makeBeamFrame(
        outgoing.originMetres, outgoing.direction);
    scene::validateBeamState(outgoing);

    return {
        .interaction = {
            .componentId = hit.component->id,
            .hitPointMetres = incident.originMetres,
            .distanceMetres
                = trace.records.front().geometricDistanceMetres,
            .incidentBeam = incident,
            .outgoing = {{
                .interaction = scene::BranchInteractionKind::Transmitted,
                .beam = outgoing,
            }},
            .diagnostics = {
                "resolved sequential prescription '"
                    + parameters.prescriptionId + "' across "
                    + std::to_string(trace.records.size())
                    + " refracting surfaces",
            },
        },
        .trace = std::move(trace),
    };
}

scene::BenchTraceGraph traceDynamicBenchImpl(
    const scene::BenchScene& bench,
    const scene::TraceBudget& budget,
    const ILensPrescriptionResolver* lensPrescriptions,
    const scene::BeamState* derivedSeed) {
    scene::validateTraceBudget(budget);
    static_cast<void>(scene::BenchScene(bench.components(), bench.revision()));

    scene::BenchTraceGraph graph {
        .sourceRevision = bench.revision(),
        .segments = {},
        .interactions = {},
        .terminations = {},
    };
    std::vector<const scene::BenchComponent*> sources;
    for (const auto& component : bench.components()) {
        if (component.kind == scene::BenchComponentKind::LaserSource
            || component.kind == scene::BenchComponentKind::ObjectWavefrontSource) {
            sources.push_back(&component);
        }
    }
    std::sort(sources.begin(), sources.end(), [](const auto* first, const auto* second) {
        return first->id < second->id;
    });

    std::deque<PendingBranch> pending;
    std::uint64_t nextBranchId = 1;
    std::size_t createdBranchCount = 0;
    if (derivedSeed != nullptr) {
        scene::validateBeamState(*derivedSeed);
        const auto* source = derivedSeed->provenance.componentPath.size() == 1U
            ? bench.find(derivedSeed->provenance.componentPath.front())
            : nullptr;
        if (derivedSeed->provenance.branchId == 0U
            || derivedSeed->provenance.parentBranchId != 0U
            || source == nullptr) {
            throw std::invalid_argument(
                "derived Bench beam must start at one current placed component with root provenance");
        }
        if (derivedSeed->provenance.branchId
            == std::numeric_limits<std::uint64_t>::max()) {
            throw std::invalid_argument(
                "derived Bench beam branch ID leaves no child-branch identity space");
        }
        pending.push_back({.beam = *derivedSeed, .hopCount = 0U});
        nextBranchId = derivedSeed->provenance.branchId + 1U;
        createdBranchCount = 1U;
    }
    for (const auto* source : sources) {
        if (derivedSeed != nullptr) {
            break;
        }
        std::vector<scene::SpectralChannel> channels;
        if (source->kind == scene::BenchComponentKind::LaserSource) {
            channels = std::get<scene::LaserSourceParameters>(source->parameters).channels;
        } else {
            channels.push_back(
                std::get<scene::ObjectWavefrontSourceParameters>(source->parameters).channel);
        }
        for (const auto& channel : channels) {
            if (createdBranchCount >= budget.maximumBranches) {
                graph.terminations.push_back({
                    .branchId = 0,
                    .reason = scene::TraceTerminationReason::BranchLimit,
                    .detail = "source branches exceed the configured branch budget",
                });
                continue;
            }
            scene::BeamState beam {
                .wavelengthMetres = channel.wavelengthMetres,
                .powerWatts = channel.powerWatts,
                .phaseRadians = 0.0,
                .coherenceId = channel.coherenceId,
                .accumulatedOpticalPathMetres = 0.0,
                .originMetres = source->transform.translationMetres,
                .direction = source->transform.localZAxisInWorld,
                .localFrame = source->transform,
                .provenance = {
                    .branchId = nextBranchId,
                    .parentBranchId = 0,
                    .componentPath = {source->id},
                },
            };
            scene::validateBeamState(beam);
            pending.push_back({.beam = std::move(beam), .hopCount = 0});
            ++nextBranchId;
            ++createdBranchCount;
        }
    }

    while (!pending.empty()) {
        PendingBranch current = std::move(pending.front());
        pending.pop_front();

        if (current.beam.powerWatts < budget.minimumPowerWatts) {
            appendTermination(graph, current.beam,
                scene::TraceTerminationReason::MinimumPower,
                "branch power is below the configured cutoff");
            continue;
        }
        if (current.hopCount >= budget.maximumHopsPerBranch) {
            appendTermination(graph, current.beam,
                scene::TraceTerminationReason::HopLimit,
                "branch reached the configured hop limit");
            continue;
        }

        const auto hit = findNextHit(
            bench, current.beam, lensPrescriptions);
        if (!hit.has_value()) {
            const auto end = current.beam.originMetres
                + math::normalized(current.beam.direction) * budget.escapeDistanceMetres;
            appendSegment(graph, current.beam, end);
            appendTermination(graph, current.beam,
                scene::TraceTerminationReason::EscapedScene,
                "branch found no supported component within the scene");
            continue;
        }

        appendSegment(graph, current.beam, hit->pointMetres);
        switch (hit->component->kind) {
        case scene::BenchComponentKind::PlanarMirror: {
            auto interaction = interactMirror(current.beam, *hit);
            if (interaction.outgoing.front().beam.powerWatts == 0.0) {
                graph.interactions.push_back(std::move(interaction));
                appendTermination(graph, current.beam,
                    scene::TraceTerminationReason::Absorbed,
                    "mirror reflectivity is zero");
            } else {
                pending.push_back({
                    .beam = interaction.outgoing.front().beam,
                    .hopCount = current.hopCount + 1,
                });
                graph.interactions.push_back(std::move(interaction));
            }
            break;
        }
        case scene::BenchComponentKind::BeamSplitterCombiner: {
            const auto& parameters = std::get<scene::BeamSplitterParameters>(hit->component->parameters);
            const std::size_t outputCount = static_cast<std::size_t>(parameters.powerReflectivity > 0.0)
                + static_cast<std::size_t>(parameters.powerTransmissivity > 0.0);
            if (outputCount > budget.maximumBranches - createdBranchCount) {
                graph.interactions.push_back({
                    .componentId = hit->component->id,
                    .hitPointMetres = hit->pointMetres,
                    .distanceMetres = hit->distanceMetres,
                    .incidentBeam = incidentAtHit(current.beam, *hit),
                    .outgoing = {},
                    .diagnostics = {"splitter outputs were suppressed by the branch budget"},
                });
                appendTermination(graph, current.beam,
                    scene::TraceTerminationReason::BranchLimit,
                    "splitter outputs exceed the configured branch budget");
                break;
            }
            auto interaction = scene::interactIdealBeamSplitter(
                current.beam,
                *hit->component,
                hit->pointMetres,
                nextBranchId,
                nextBranchId + 1);
            if (interaction.outgoing.empty()) {
                graph.interactions.push_back(std::move(interaction));
                appendTermination(graph, current.beam,
                    scene::TraceTerminationReason::Absorbed,
                    "splitter has zero reflected and transmitted power");
                break;
            }
            for (auto& outgoing : interaction.outgoing) {
                pending.push_back({
                    .beam = outgoing.beam,
                    .hopCount = current.hopCount + 1,
                });
            }
            createdBranchCount += interaction.outgoing.size();
            nextBranchId += 2;
            graph.interactions.push_back(std::move(interaction));
            break;
        }
        case scene::BenchComponentKind::IdealThinLens: {
            auto interaction = interactThinLens(current.beam, *hit);
            pending.push_back({
                .beam = interaction.outgoing.front().beam,
                .hopCount = current.hopCount + 1,
            });
            graph.interactions.push_back(std::move(interaction));
            break;
        }
        case scene::BenchComponentKind::Aperture: {
            const auto& parameters = std::get<scene::ApertureParameters>(hit->component->parameters);
            if (!apertureTransmits(parameters, hit->localPointMetres)) {
                graph.interactions.push_back({
                    .componentId = hit->component->id,
                    .hitPointMetres = hit->pointMetres,
                    .distanceMetres = hit->distanceMetres,
                    .incidentBeam = incidentAtHit(current.beam, *hit),
                    .outgoing = {},
                    .diagnostics = {"branch was clipped by the aperture"},
                });
                appendTermination(graph, current.beam,
                    scene::TraceTerminationReason::Absorbed,
                    "branch was clipped by the aperture");
            } else {
                auto interaction = singleOutputInteraction(
                    current.beam,
                    *hit,
                    current.beam.direction,
                    1.0,
                    scene::BranchInteractionKind::Transmitted);
                pending.push_back({
                    .beam = interaction.outgoing.front().beam,
                    .hopCount = current.hopCount + 1,
                });
                graph.interactions.push_back(std::move(interaction));
            }
            break;
        }
        case scene::BenchComponentKind::SpatialFilter: {
            const auto& parameters
                = std::get<scene::SpatialFilterParameters>(hit->component->parameters);
            if (!spatialFilterTransmits(parameters, hit->localPointMetres)) {
                graph.interactions.push_back({
                    .componentId = hit->component->id,
                    .hitPointMetres = hit->pointMetres,
                    .distanceMetres = hit->distanceMetres,
                    .incidentBeam = incidentAtHit(current.beam, *hit),
                    .outgoing = {},
                    .diagnostics = {"centre ray was clipped by the spatial-filter pinhole"},
                });
                appendTermination(graph, current.beam,
                    scene::TraceTerminationReason::Absorbed,
                    "centre ray was clipped by the spatial-filter pinhole");
            } else {
                auto interaction = singleOutputInteraction(
                    current.beam,
                    *hit,
                    current.beam.direction,
                    1.0,
                    scene::BranchInteractionKind::Transmitted,
                    "centreline pass-through only; pinhole diffraction belongs to local wave refinement");
                pending.push_back({
                    .beam = interaction.outgoing.front().beam,
                    .hopCount = current.hopCount + 1,
                });
                graph.interactions.push_back(std::move(interaction));
            }
            break;
        }
        case scene::BenchComponentKind::SpatialLightModulator: {
            auto interaction = singleOutputInteraction(
                current.beam,
                *hit,
                current.beam.direction,
                1.0,
                scene::BranchInteractionKind::Transmitted,
                "centreline pass-through only; SLM amplitude and phase require local wave refinement");
            pending.push_back({
                .beam = interaction.outgoing.front().beam,
                .hopCount = current.hopCount + 1,
            });
            graph.interactions.push_back(std::move(interaction));
            break;
        }
        case scene::BenchComponentKind::FieldProbe: {
            auto interaction = singleOutputInteraction(
                current.beam,
                *hit,
                current.beam.direction,
                1.0,
                scene::BranchInteractionKind::Transmitted,
                "field probe observed the branch without absorbing it");
            pending.push_back({
                .beam = interaction.outgoing.front().beam,
                .hopCount = current.hopCount + 1,
            });
            graph.interactions.push_back(std::move(interaction));
            break;
        }
        case scene::BenchComponentKind::ScreenDetector:
            graph.interactions.push_back({
                .componentId = hit->component->id,
                .hitPointMetres = hit->pointMetres,
                .distanceMetres = hit->distanceMetres,
                .incidentBeam = incidentAtHit(current.beam, *hit),
                .outgoing = {},
                .diagnostics = {"screen detector intercepted the branch"},
            });
            appendTermination(graph, current.beam,
                scene::TraceTerminationReason::Absorbed,
                "screen detector intercepted the branch");
            break;
        case scene::BenchComponentKind::HolographicPlate:
            graph.interactions.push_back({
                .componentId = hit->component->id,
                .hitPointMetres = hit->pointMetres,
                .distanceMetres = hit->distanceMetres,
                .incidentBeam = incidentAtHit(current.beam, *hit),
                .outgoing = {},
                .diagnostics = {
                    "holographic plate collected the incident branch; recording is an explicit M8 operation"},
            });
            appendTermination(graph, current.beam,
                scene::TraceTerminationReason::Absorbed,
                "holographic plate intercepted the branch for recording input");
            break;
        case scene::BenchComponentKind::RealLensAssembly: {
            auto resolved = interactRealLens(
                current.beam, *hit, lensPrescriptions);
            if (resolved.interaction.outgoing.empty()) {
                const std::string detail
                    = resolved.interaction.diagnostics.empty()
                    ? "real-lens prescription interaction failed"
                    : resolved.interaction.diagnostics.front();
                graph.interactions.push_back(
                    std::move(resolved.interaction));
                appendTermination(
                    graph,
                    current.beam,
                    scene::TraceTerminationReason::InvalidInteraction,
                    detail);
                break;
            }
            for (std::size_t index = 1;
                 index < resolved.trace.records.size(); ++index) {
                graph.segments.push_back({
                    .branchId = current.beam.provenance.branchId,
                    .startMetres = resolved.trace.records[index - 1U]
                        .worldPointMetres,
                    .endMetres = resolved.trace.records[index]
                        .worldPointMetres,
                    .wavelengthMetres = current.beam.wavelengthMetres,
                    .powerWatts = current.beam.powerWatts,
                });
            }
            pending.push_back({
                .beam = resolved.interaction.outgoing.front().beam,
                .hopCount = current.hopCount + 1,
            });
            graph.interactions.push_back(
                std::move(resolved.interaction));
            break;
        }
        case scene::BenchComponentKind::LaserSource:
        case scene::BenchComponentKind::ObjectWavefrontSource:
            throw std::logic_error("unsupported component escaped traceable-plane filtering");
        }
    }

    return graph;
}

} // namespace

scene::BenchTraceGraph traceDynamicBench(
    const scene::BenchScene& bench,
    const scene::TraceBudget& budget,
    const ILensPrescriptionResolver* lensPrescriptions) {
    return traceDynamicBenchImpl(
        bench, budget, lensPrescriptions, nullptr);
}

scene::BenchTraceGraph traceDerivedBenchBeam(
    const scene::BenchScene& bench,
    const scene::BeamState& seed,
    const scene::TraceBudget& budget,
    const ILensPrescriptionResolver* lensPrescriptions) {
    return traceDynamicBenchImpl(
        bench, budget, lensPrescriptions, &seed);
}

} // namespace holobench::optics::ray
