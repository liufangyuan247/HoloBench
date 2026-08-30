#pragma once

#include "core/math/Vec3.hpp"
#include "optics/ray/Ray.hpp"

namespace holobench::optics::ray {

/**
 * @brief Interaction status at an optical dielectric interface.
 */
enum class InterfaceInteractionStatus {
    Refracted,
    TotalInternalReflection,
};

/**
 * @brief Result of ray interaction with an ideal planar dielectric interface.
 *
 * Note on physical assumptions:
 * At this stage, Fresnel reflection/transmission power loss and amplitude splitting
 * are not applied. Outgoing ray power is preserved from the incident ray (power = incident.power).
 * Full Fresnel coefficients will be implemented in subsequent phases.
 */
struct InterfaceInteractionResult final {
    InterfaceInteractionStatus status = InterfaceInteractionStatus::Refracted;
    Ray outgoingRay;
};

/**
 * @brief Computes the refracted or totally internally reflected outgoing ray at an ideal planar dielectric interface.
 *
 * Physical Model & Conventions:
 * - CPU double-precision reference solver.
 * - Geometric surface normal is automatically oriented towards the incident side.
 * - Refractive indices nIncident and nTransmitted must be finite and strictly positive (> 0).
 * - Total Internal Reflection (TIR):
 *   Occurs when light travels from an optically denser to a rarer medium (nIncident > nTransmitted)
 *   at an angle of incidence strictly greater than the critical angle plus numerical tolerance.
 *   Under TIR, the outgoing ray direction strictly follows the law of reflection (theta_r = theta_i).
 * - Near-critical Numerical Stability:
 *   sin^2(theta_t) > 1.0 + tolerance (1e-12) is required for TIR.
 *   Values in [1.0, 1.0 + tolerance] are clamped to 1.0, yielding exact tangential refraction.
 * - Refraction (Snell's Law):
 *   When TIR does not occur, the transmitted ray direction is solved using the numerically stable vector form:
 *     t = eta * d + (eta * cos(theta_i) - cos(theta_t)) * N_in
 *   where eta = nIncident / nTransmitted, N_in is the incident-facing surface normal, and
 *   cos(theta_t) = sqrt(max(0.0, 1.0 - sin^2(theta_t))).
 * - Outgoing ray origin is set to intersectionMetres, and outgoing direction is guaranteed to be normalized.
 * - Power Loss:
 *   Fresnel power loss is not applied at this stage; outgoing ray power is preserved from the incident ray.
 *
 * @param incident Incident ray.
 * @param intersectionMetres 3D coordinates of the intersection point on the interface.
 * @param surfaceNormal Geometric surface normal (points in either outward direction; auto-oriented internally).
 * @param nIncident Refractive index of the incident medium (must be finite and > 0).
 * @param nTransmitted Refractive index of the transmitted medium (must be finite and > 0).
 * @return InterfaceInteractionResult containing the interaction status and normalized outgoing ray.
 * @throws std::invalid_argument if inputs are non-finite, refractive indices are <= 0, or normal is zero.
 */
[[nodiscard]] InterfaceInteractionResult interactInterface(
    const Ray& incident,
    math::Vec3d intersectionMetres,
    math::Vec3d surfaceNormal,
    double nIncident,
    double nTransmitted);

} // namespace holobench::optics::ray

