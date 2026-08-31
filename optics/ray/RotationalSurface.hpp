#pragma once

#include <cstddef>
#include <vector>

#include "core/math/Vec3.hpp"
#include "optics/ray/Ray.hpp"

namespace holobench::optics::ray {

struct EvenAsphereTerm final {
    unsigned radialOrder = 4;
    double coefficientSi = 0.0;

    bool operator==(const EvenAsphereTerm&) const = default;
};

/**
 * Rotationally symmetric surface in its local vertex frame.
 *
 * Local +Z is the nominal propagation direction and curvature is c = 1/R.
 * The sag convention is fixed by ADR 0007. The clear semi-diameter clips the
 * mathematical surface but does not alter its sag equation.
 */
struct RotationalSurface final {
    double curvaturePerMetre = 0.0;
    double conicConstant = 0.0;
    std::vector<EvenAsphereTerm> evenAsphereTerms;
    double clearSemiDiameterMetres = 0.01;

    bool operator==(const RotationalSurface&) const = default;
};

struct SurfaceSagEvaluation final {
    double sagMetres = 0.0;
    double radialDerivative = 0.0;
};

enum class SurfaceIntersectionStatus {
    Hit,
    Miss,
    Clipped,
    OutsideDomain,
    DidNotConverge,
};

struct SurfaceIntersectionOptions final {
    double intersectionEpsilonMetres = 1e-12;
    double maximumDistanceMetres = 1.0;
    double residualToleranceMetres = 1e-12;
    double apertureToleranceMetres = 1e-12;
    std::size_t maximumIterations = 96;
    std::size_t bracketSubdivisions = 1024;
};

struct SurfaceIntersectionResult final {
    SurfaceIntersectionStatus status = SurfaceIntersectionStatus::Miss;
    double distanceMetres = 0.0;
    math::Vec3d pointMetres {};
    math::Vec3d geometricNormal {};
    double spatialResidualMetres = 0.0;
    std::size_t iterations = 0;
};

void validateRotationalSurface(const RotationalSurface& surface);
void validateSurfaceIntersectionOptions(const SurfaceIntersectionOptions& options);

[[nodiscard]] SurfaceSagEvaluation evaluateSurfaceSag(
    const RotationalSurface& surface,
    double radialCoordinateMetres);

[[nodiscard]] math::Vec3d surfaceNormalAt(
    const RotationalSurface& surface,
    math::Vec3d localSurfacePointMetres);

/**
 * Intersects a local-frame ray with a rotational surface.
 *
 * Planes and base conics use analytic roots. Even aspheres use safeguarded
 * Newton iteration plus an ascending bracket scan. A result is accepted only
 * when its explicit sag residual passes the requested tolerance.
 */
[[nodiscard]] SurfaceIntersectionResult intersectRotationalSurfaceForward(
    const Ray& localRay,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options);

} // namespace holobench::optics::ray

