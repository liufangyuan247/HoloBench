#include "optics/ray/ThinLens.hpp"

#include <cmath>
#include <stdexcept>

namespace holobench::optics::ray {
namespace {

void validateLens(const IdealThinLens& lens) {
    if (!std::isfinite(lens.planeZMetres)
        || !std::isfinite(lens.centreXMetres)
        || !std::isfinite(lens.centreYMetres)
        || !std::isfinite(lens.focalLengthMetres)
        || !std::isfinite(lens.clearApertureRadiusMetres)) {
        throw std::invalid_argument("thin-lens parameters must be finite");
    }
    if (lens.focalLengthMetres == 0.0) {
        throw std::invalid_argument("thin-lens focal length must be non-zero");
    }
    if (lens.clearApertureRadiusMetres <= 0.0) {
        throw std::invalid_argument("thin-lens clear aperture radius must be positive");
    }
}

} // namespace

std::optional<math::Vec3d> intersectForwardPlaneZ(const Ray& ray, double planeZMetres, double epsilon) {
    if (!std::isfinite(planeZMetres) || !std::isfinite(epsilon) || epsilon < 0.0) {
        throw std::invalid_argument("plane intersection parameters must be finite and epsilon non-negative");
    }
    if (std::abs(ray.direction.z) <= epsilon) {
        return std::nullopt;
    }

    const double distance = (planeZMetres - ray.originMetres.z) / ray.direction.z;
    if (distance < 0.0) {
        return std::nullopt;
    }
    return ray.originMetres + ray.direction * distance;
}

ThinLensTraceResult traceParaxialThinLens(const Ray& incident, const IdealThinLens& lens) {
    validateLens(lens);
    if (incident.direction.z <= kReferenceIntersectionEpsilon) {
        return {ThinLensTraceStatus::NoForwardIntersection, {}, std::nullopt};
    }

    const auto intersection = intersectForwardPlaneZ(incident, lens.planeZMetres);
    if (!intersection.has_value()) {
        return {ThinLensTraceStatus::NoForwardIntersection, {}, std::nullopt};
    }

    const double localX = intersection->x - lens.centreXMetres;
    const double localY = intersection->y - lens.centreYMetres;
    const double radialDistanceSquared = localX * localX + localY * localY;
    const double apertureRadiusSquared = lens.clearApertureRadiusMetres * lens.clearApertureRadiusMetres;
    if (radialDistanceSquared > apertureRadiusSquared) {
        return {ThinLensTraceStatus::ClippedByAperture, *intersection, std::nullopt};
    }

    const double slopeX = incident.direction.x / incident.direction.z;
    const double slopeY = incident.direction.y / incident.direction.z;
    const math::Vec3d outgoingDirection {
        slopeX - localX / lens.focalLengthMetres,
        slopeY - localY / lens.focalLengthMetres,
        1.0,
    };
    Ray transmitted = makeRay(*intersection, outgoingDirection, incident.wavelengthMetres, incident.power);
    return {ThinLensTraceStatus::Transmitted, *intersection, transmitted};
}

} // namespace holobench::optics::ray

