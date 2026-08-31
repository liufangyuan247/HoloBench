#include "optics/ray/SequentialLens.hpp"

#include <cmath>
#include <stdexcept>
#include <unordered_set>

#include "optics/ray/Interface.hpp"

namespace holobench::optics::ray {

namespace {

[[nodiscard]] const material::OpticalMaterial& findMaterial(
    const SequentialLensPrescription& prescription,
    const std::string& id) {
    for (const auto& candidate : prescription.materials) {
        if (candidate.id == id) {
            return candidate;
        }
    }
    throw std::invalid_argument("prescription surface references an unknown material");
}

[[nodiscard]] SequentialTraceStatus mapFailure(SurfaceIntersectionStatus status) {
    switch (status) {
    case SurfaceIntersectionStatus::Clipped: return SequentialTraceStatus::Clipped;
    case SurfaceIntersectionStatus::OutsideDomain: return SequentialTraceStatus::OutsideSurfaceDomain;
    case SurfaceIntersectionStatus::DidNotConverge: return SequentialTraceStatus::SurfaceNonConvergence;
    case SurfaceIntersectionStatus::Miss: return SequentialTraceStatus::Miss;
    case SurfaceIntersectionStatus::Hit: break;
    }
    throw std::logic_error("hit is not a failure status");
}

} // namespace

void validateSequentialLensPrescription(const SequentialLensPrescription& prescription) {
    if (prescription.id.empty() || prescription.materials.empty() || prescription.surfaces.empty()) {
        throw std::invalid_argument("prescription id, materials, and surfaces must be non-empty");
    }
    std::unordered_set<std::string> materialIds;
    for (const auto& materialValue : prescription.materials) {
        material::validateOpticalMaterial(materialValue);
        if (!materialIds.insert(materialValue.id).second) {
            throw std::invalid_argument("prescription material ids must be unique");
        }
    }
    std::unordered_set<std::string> surfaceIds;
    for (std::size_t index = 0; index < prescription.surfaces.size(); ++index) {
        const auto& surface = prescription.surfaces[index];
        if (surface.id.empty() || !surfaceIds.insert(surface.id).second) {
            throw std::invalid_argument("prescription surface ids must be non-empty and unique");
        }
        validateRotationalSurface(surface.geometry);
        math::validateRigidTransform(surface.localToWorld);
        static_cast<void>(findMaterial(prescription, surface.materialBeforeId));
        static_cast<void>(findMaterial(prescription, surface.materialAfterId));
        if (index > 0 && prescription.surfaces[index - 1].materialAfterId != surface.materialBeforeId) {
            throw std::invalid_argument("adjacent prescription surface media must be continuous");
        }
    }
}

SequentialTraceResult traceSequentialLens(
    const Ray& incidentWorldRay,
    const SequentialLensPrescription& prescription,
    const SurfaceIntersectionOptions& intersectionOptions) {
    validateSequentialLensPrescription(prescription);
    validateSurfaceIntersectionOptions(intersectionOptions);
    Ray current = makeRay(
        incidentWorldRay.originMetres,
        incidentWorldRay.direction,
        incidentWorldRay.wavelengthMetres,
        incidentWorldRay.power);
    SequentialTraceResult result {
        .status = SequentialTraceStatus::Completed,
        .records = {},
        .finalRay = std::nullopt,
        .totalGeometricPathMetres = 0.0,
        .totalOpticalPathMetres = 0.0,
    };
    result.records.reserve(prescription.surfaces.size());

    for (const PrescriptionSurface& surface : prescription.surfaces) {
        const Ray localRay = makeRay(
            math::transformPointWorldToLocal(surface.localToWorld, current.originMetres),
            math::transformDirectionWorldToLocal(surface.localToWorld, current.direction),
            current.wavelengthMetres,
            current.power);
        const SurfaceIntersectionResult hit = intersectRotationalSurfaceForward(
            localRay, surface.geometry, intersectionOptions);
        SequentialSurfaceRecord record {
            .surfaceId = surface.id,
            .intersectionStatus = hit.status,
            .worldPointMetres = {},
            .worldNormal = {},
            .geometricDistanceMetres = hit.distanceMetres,
            .incidentRefractiveIndex = 0.0,
            .transmittedRefractiveIndex = 0.0,
            .segmentOpticalPathMetres = 0.0,
            .cumulativeOpticalPathMetres = 0.0,
            .outgoingRay = std::nullopt,
        };
        if (hit.status != SurfaceIntersectionStatus::Hit) {
            result.records.push_back(record);
            result.status = mapFailure(hit.status);
            result.finalRay = std::nullopt;
            return result;
        }

        record.worldPointMetres = math::transformPointLocalToWorld(surface.localToWorld, hit.pointMetres);
        record.worldNormal = math::normalized(
            math::transformDirectionLocalToWorld(surface.localToWorld, hit.geometricNormal));
        const auto& before = findMaterial(prescription, surface.materialBeforeId);
        const auto& after = findMaterial(prescription, surface.materialAfterId);
        record.incidentRefractiveIndex = material::refractiveIndexAtVacuumWavelength(
            before, current.wavelengthMetres);
        record.transmittedRefractiveIndex = material::refractiveIndexAtVacuumWavelength(
            after, current.wavelengthMetres);
        record.segmentOpticalPathMetres = hit.distanceMetres * record.incidentRefractiveIndex;
        if (!std::isfinite(record.segmentOpticalPathMetres)) {
            throw std::overflow_error("sequential trace optical path is not finite");
        }
        result.totalGeometricPathMetres += hit.distanceMetres;
        result.totalOpticalPathMetres += record.segmentOpticalPathMetres;
        record.cumulativeOpticalPathMetres = result.totalOpticalPathMetres;

        const InterfaceInteractionResult interaction = interactInterface(
            current,
            record.worldPointMetres,
            record.worldNormal,
            record.incidentRefractiveIndex,
            record.transmittedRefractiveIndex);
        record.outgoingRay = interaction.outgoingRay;
        result.records.push_back(record);
        if (interaction.status == InterfaceInteractionStatus::TotalInternalReflection) {
            result.status = SequentialTraceStatus::TotalInternalReflection;
            result.finalRay = interaction.outgoingRay;
            return result;
        }
        current = interaction.outgoingRay;
    }
    result.finalRay = current;
    return result;
}

} // namespace holobench::optics::ray
