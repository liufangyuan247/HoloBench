#pragma once

#include <cmath>
#include <cstddef>
#include <optional>
#include <vector>

#include "core/math/Vec3.hpp"
#include "optics/ray/Interface.hpp"
#include "optics/ray/Ray.hpp"
#include "optics/scene/GeometricComponents.hpp"

namespace holobench::optics::ray {

inline constexpr double kGeometricIntersectionEpsilon = 1e-12;
inline constexpr std::size_t kMaxCollimatedRayCount = 10'000'000;

/**
 * @brief Classification of ray interaction outcomes with geometric optical components.
 */
enum class GeometricInteractionStatus {
    Miss,                     ///< Ray does not intersect the infinite plane in the forward direction (parallel or behind).
    Clipped,                  ///< Ray intersects the infinite plane forward, but the hit point lies outside the finite aperture.
    Reflected,                ///< Ray hits the aperture of a mirror and specularly reflects.
    Refracted,                ///< Ray hits the aperture of a dielectric interface and refracts according to Snell's law.
    TotalInternalReflection,  ///< Ray hits the aperture of a dielectric interface and undergoes TIR.
};

/**
 * @brief Result of forward plane intersection.
 */
struct PlaneIntersectionResult final {
    bool hit = false;
    double signedDistanceMetres = 0.0;
    math::Vec3d pointMetres {};

    bool operator==(const PlaneIntersectionResult&) const = default;
};

/**
 * @brief Orthonormal in-plane local coordinate basis for aperture clipping.
 */
struct PlanarApertureBasis final {
    math::Vec3d uAxis {1.0, 0.0, 0.0}; ///< In-plane width axis (normalized)
    math::Vec3d vAxis {0.0, 1.0, 0.0}; ///< In-plane height axis (normalized)

    bool operator==(const PlanarApertureBasis&) const = default;
};

/**
 * @brief Complete geometric trace result.
 */
struct GeometricTraceResult final {
    GeometricInteractionStatus status = GeometricInteractionStatus::Miss;
    double distanceMetres = 0.0;
    math::Vec3d intersectionMetres {};
    std::optional<Ray> outgoingRay;
};

/**
 * @brief Sampling patterns for collimated ray bundle emission.
 */
enum class CollimatedRayPattern {
    FibonacciDisk = 0,
    ConcentricRings = 1,
    UniformGrid = 2,
    SingleRay = 3,
};

/**
 * @brief Computes deterministic orthonormal in-plane basis vectors (u, v) given a plane normal.
 */
[[nodiscard]] PlanarApertureBasis computePlanarApertureBasis(math::Vec3d planeNormal);

/**
 * @brief Validates that a planar aperture basis has finite, normalized, non-zero, and orthogonal axes.
 * @throws std::invalid_argument if basis vectors are non-finite, zero, not normalized, or not orthogonal.
 */
void validatePlanarApertureBasis(const PlanarApertureBasis& basis);

/**
 * @brief Checks whether a planar aperture basis is finite, non-zero, normalized, and orthogonal.
 */
[[nodiscard]] bool isPlanarApertureBasisValid(const PlanarApertureBasis& basis) noexcept;

/**
 * @brief Computes forward ray intersection with an arbitrary 3D plane.
 *
 * Rejection conditions:
 * - Near-parallel rays: |dot(direction, normal)| <= epsilon.
 * - Behind or at ray origin: signed distance t < epsilon.
 *
 * @param ray Incident ray.
 * @param planePointMetres Point lying on the plane.
 * @param planeNormal Plane normal vector.
 * @param epsilon Tolerance for near-parallel and forward distance check.
 * @return PlaneIntersectionResult with hit status, signed distance, and 3D intersection coordinates.
 */
[[nodiscard]] PlaneIntersectionResult intersectPlaneForward(
    const Ray& ray,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double epsilon = kGeometricIntersectionEpsilon);

/**
 * @brief Convenience overload returning optional 3D intersection coordinates.
 */
[[nodiscard]] std::optional<math::Vec3d> intersectForwardPlane(
    const Ray& ray,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double epsilon = kGeometricIntersectionEpsilon);

/**
 * @brief Checks if a 3D point on the plane is inside the rectangular aperture centered at planePointMetres.
 */
[[nodiscard]] bool isInsideRectangularAperture(
    math::Vec3d pointMetres,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double widthMetres,
    double heightMetres,
    double tolerance = kGeometricIntersectionEpsilon);

[[nodiscard]] bool isInsideRectangularAperture(
    math::Vec3d pointMetres,
    math::Vec3d planePointMetres,
    const PlanarApertureBasis& basis,
    double widthMetres,
    double heightMetres,
    double tolerance = kGeometricIntersectionEpsilon);

/**
 * @brief Traces a ray against an ideal planar mirror with a finite rectangular aperture.
 */
[[nodiscard]] GeometricTraceResult tracePlanarMirror(
    const Ray& incident,
    const scene::PlanarMirror& mirror,
    double epsilon = kGeometricIntersectionEpsilon);

/**
 * @brief Traces a ray against a planar dielectric interface with a finite rectangular aperture.
 * Reuses interactInterface for Snell refraction and TIR physics.
 */
[[nodiscard]] GeometricTraceResult tracePlaneInterface(
    const Ray& incident,
    const scene::PlaneInterfaceComponent& interfaceComp,
    double epsilon = kGeometricIntersectionEpsilon);

/**
 * @brief Emits a deterministic collimated ray bundle from a CollimatedSource into caller-provided buffer.
 *
 * Guarantees:
 * - All rays are parallel to source direction and strictly normalized.
 * - Total power across all emitted rays strictly equals source.powerWatts (power conservation).
 * - Vector memory capacity is reused when callerBuffer capacity is sufficient (zero reallocations).
 * - Deterministic output.
 * - Throws std::invalid_argument on illegal inputs or overflow.
 */
void emitCollimatedRays(
    const scene::CollimatedSource& source,
    std::size_t rayCount,
    std::vector<Ray>& outRays,
    CollimatedRayPattern pattern = CollimatedRayPattern::FibonacciDisk);

[[nodiscard]] std::vector<Ray> emitCollimatedRays(
    const scene::CollimatedSource& source,
    std::size_t rayCount,
    CollimatedRayPattern pattern = CollimatedRayPattern::FibonacciDisk);

} // namespace holobench::optics::ray
