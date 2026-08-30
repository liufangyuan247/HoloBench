#pragma once

#include <optional>

#include "optics/ray/Ray.hpp"

namespace holobench::optics::ray {

inline constexpr double kReferenceIntersectionEpsilon = 1e-12;

struct IdealThinLens final {
    double planeZMetres = 0.0;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
    double focalLengthMetres = 0.05;
    double clearApertureRadiusMetres = 0.025;
};

enum class ThinLensTraceStatus {
    Transmitted,
    NoForwardIntersection,
    ClippedByAperture,
};

struct ThinLensTraceResult final {
    ThinLensTraceStatus status = ThinLensTraceStatus::NoForwardIntersection;
    math::Vec3d intersectionMetres;
    std::optional<Ray> transmittedRay;
};

[[nodiscard]] std::optional<math::Vec3d> intersectForwardPlaneZ(
    const Ray& ray,
    double planeZMetres,
    double epsilon = kReferenceIntersectionEpsilon);

[[nodiscard]] ThinLensTraceResult traceParaxialThinLens(const Ray& incident, const IdealThinLens& lens);

} // namespace holobench::optics::ray

