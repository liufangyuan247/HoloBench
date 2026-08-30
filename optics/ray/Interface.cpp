#include "optics/ray/Interface.hpp"

#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace holobench::optics::ray {

namespace {

constexpr double kCriticalAngleTolerance = 1e-12;

} // namespace

InterfaceInteractionResult interactInterface(
    const Ray& incident,
    math::Vec3d intersectionMetres,
    math::Vec3d surfaceNormal,
    double nIncident,
    double nTransmitted) {
    // 1. Validate refractive indices (must be finite and strictly positive)
    if (!std::isfinite(nIncident) || nIncident <= 0.0) {
        throw std::invalid_argument("incident refractive index must be finite and positive");
    }
    if (!std::isfinite(nTransmitted) || nTransmitted <= 0.0) {
        throw std::invalid_argument("transmitted refractive index must be finite and positive");
    }

    // 2. Validate intersection point
    if (!math::isFinite(intersectionMetres)) {
        throw std::invalid_argument("intersection point must be finite");
    }

    // 3. Validate and normalize surface normal
    if (!math::isFinite(surfaceNormal) || math::lengthSquared(surfaceNormal) <= 0.0) {
        throw std::invalid_argument("surface normal must be finite and non-zero");
    }
    const math::Vec3d unitNormal = math::normalized(surfaceNormal);

    // 4. Validate and normalize incident ray direction
    if (!math::isFinite(incident.direction) || math::lengthSquared(incident.direction) <= 0.0) {
        throw std::invalid_argument("incident ray direction must be finite and non-zero");
    }
    const math::Vec3d d = math::normalized(incident.direction);

    // 5. Automatically orient surface normal towards the incident side:
    // If d . N > 0, N points along the ray propagation direction (into the transmitted medium),
    // so we flip N so it opposes the incident ray.
    // If d . N <= 0, N already points towards the incident side.
    const double dDotN = math::dot(d, unitNormal);
    const math::Vec3d normalOriented = (dDotN <= 0.0) ? unitNormal : -unitNormal;

    // cos(theta_i) = -(d . normalOriented) >= 0
    const double cosThetaI = std::clamp(-math::dot(d, normalOriented), 0.0, 1.0);

    // 6. Vector Snell's law formulation:
    // eta = n_1 / n_2
    const double eta = nIncident / nTransmitted;
    const double sin2ThetaI = std::max(0.0, 1.0 - cosThetaI * cosThetaI);
    const double sin2ThetaT = (eta * eta) * sin2ThetaI;

    // Total Internal Reflection (TIR):
    // TIR can only physically occur when travelling from a denser to a rarer medium (eta > 1.0)
    // and sin^2(theta_t) strictly exceeds 1.0 + tolerance.
    // For eta <= 1.0, sin2ThetaT <= sin2ThetaI <= 1.0 always, preventing spurious TIR at grazing angles.
    if (eta > 1.0 && sin2ThetaT > 1.0 + kCriticalAngleTolerance) {
        // Outgoing direction for TIR strictly follows the law of reflection:
        // r = d - 2 * (d . normalOriented) * normalOriented = d + 2 * cosThetaI * normalOriented
        const math::Vec3d r = d + normalOriented * (2.0 * cosThetaI);

        // Power preservation note:
        // Fresnel reflection/transmission power loss is not applied at this stage (power = incident.power).
        const Ray outgoing = makeRay(
            intersectionMetres,
            math::normalized(r),
            incident.wavelengthMetres,
            incident.power);

        return {InterfaceInteractionStatus::TotalInternalReflection, outgoing};
    }

    // Refraction (Transmission):
    // Clamp sin^2(theta_t) to [0.0, 1.0]. Any slight excess in [1.0, 1.0 + tolerance] near the critical
    // angle is safely clamped to 1.0, producing exact tangential refraction (cos(theta_t) = 0.0).
    const double clampedSin2ThetaT = std::clamp(sin2ThetaT, 0.0, 1.0);
    const double cosThetaT = std::sqrt(std::max(0.0, 1.0 - clampedSin2ThetaT));

    // Vector form of refracted ray direction:
    // t = eta * d + (eta * cos(theta_i) - cos(theta_t)) * normalOriented
    const math::Vec3d t = d * eta + normalOriented * (eta * cosThetaI - cosThetaT);

    // Power preservation note:
    // Fresnel reflection/transmission power loss is not applied at this stage (power = incident.power).
    const Ray outgoing = makeRay(
        intersectionMetres,
        math::normalized(t),
        incident.wavelengthMetres,
        incident.power);

    return {InterfaceInteractionStatus::Refracted, outgoing};
}

} // namespace holobench::optics::ray

