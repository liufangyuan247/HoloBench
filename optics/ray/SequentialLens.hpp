#pragma once

#include <optional>
#include <string>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/material/OpticalMaterial.hpp"
#include "optics/ray/RotationalSurface.hpp"

namespace holobench::optics::ray {

struct PrescriptionSurface final {
    std::string id;
    RotationalSurface geometry;
    math::RigidTransform3d localToWorld;
    std::string materialBeforeId;
    std::string materialAfterId;

    bool operator==(const PrescriptionSurface&) const = default;
};

struct SequentialLensPrescription final {
    std::string id;
    std::vector<material::OpticalMaterial> materials;
    std::vector<PrescriptionSurface> surfaces;

    bool operator==(const SequentialLensPrescription&) const = default;
};

enum class SequentialTraceStatus {
    Completed,
    Miss,
    Clipped,
    OutsideSurfaceDomain,
    SurfaceNonConvergence,
    TotalInternalReflection,
};

struct SequentialSurfaceRecord final {
    std::string surfaceId;
    SurfaceIntersectionStatus intersectionStatus = SurfaceIntersectionStatus::Miss;
    math::Vec3d worldPointMetres {};
    math::Vec3d worldNormal {};
    double geometricDistanceMetres = 0.0;
    double incidentRefractiveIndex = 0.0;
    double transmittedRefractiveIndex = 0.0;
    double segmentOpticalPathMetres = 0.0;
    double cumulativeOpticalPathMetres = 0.0;
    std::optional<Ray> outgoingRay;
};

struct SequentialTraceResult final {
    SequentialTraceStatus status = SequentialTraceStatus::Miss;
    std::vector<SequentialSurfaceRecord> records;
    std::optional<Ray> finalRay;
    double totalGeometricPathMetres = 0.0;
    double totalOpticalPathMetres = 0.0;
};

void validateSequentialLensPrescription(const SequentialLensPrescription& prescription);

[[nodiscard]] SequentialTraceResult traceSequentialLens(
    const Ray& incidentWorldRay,
    const SequentialLensPrescription& prescription,
    const SurfaceIntersectionOptions& intersectionOptions);

} // namespace holobench::optics::ray
