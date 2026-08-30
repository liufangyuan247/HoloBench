#include "optics/ray/GeometricElements.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>
#include <utility>

namespace holobench::optics::ray {

PlanarApertureBasis computePlanarApertureBasis(math::Vec3d planeNormal) {
    if (!math::isFinite(planeNormal) || math::lengthSquared(planeNormal) <= 0.0) {
        throw std::invalid_argument("plane normal must be finite and non-zero");
    }
    const math::Vec3d n = math::normalized(planeNormal);
    math::Vec3d u {};
    if (std::abs(n.y) < 0.9999) {
        u = math::cross(math::Vec3d {0.0, 1.0, 0.0}, n);
    } else {
        u = math::cross(math::Vec3d {0.0, 0.0, 1.0}, n);
    }
    u = math::normalized(u);
    const math::Vec3d v = math::normalized(math::cross(n, u));
    return PlanarApertureBasis {.uAxis = u, .vAxis = v};
}

void validatePlanarApertureBasis(const PlanarApertureBasis& basis) {
    if (!math::isFinite(basis.uAxis) || !math::isFinite(basis.vAxis)) {
        throw std::invalid_argument("aperture basis vectors must be finite");
    }
    const double uLenSq = math::lengthSquared(basis.uAxis);
    const double vLenSq = math::lengthSquared(basis.vAxis);
    if (uLenSq <= 0.0 || vLenSq <= 0.0) {
        throw std::invalid_argument("aperture basis vectors must be non-zero");
    }
    constexpr double kOrthoTolerance = 1e-5;
    if (std::abs(uLenSq - 1.0) > kOrthoTolerance || std::abs(vLenSq - 1.0) > kOrthoTolerance) {
        throw std::invalid_argument("aperture basis vectors must be unit length (normalized)");
    }
    if (std::abs(math::dot(basis.uAxis, basis.vAxis)) > kOrthoTolerance) {
        throw std::invalid_argument("aperture basis vectors must be orthogonal");
    }
}

bool isPlanarApertureBasisValid(const PlanarApertureBasis& basis) noexcept {
    if (!math::isFinite(basis.uAxis) || !math::isFinite(basis.vAxis)) {
        return false;
    }
    const double uLenSq = math::lengthSquared(basis.uAxis);
    const double vLenSq = math::lengthSquared(basis.vAxis);
    if (uLenSq <= 0.0 || vLenSq <= 0.0) {
        return false;
    }
    constexpr double kOrthoTolerance = 1e-5;
    if (std::abs(uLenSq - 1.0) > kOrthoTolerance || std::abs(vLenSq - 1.0) > kOrthoTolerance) {
        return false;
    }
    if (std::abs(math::dot(basis.uAxis, basis.vAxis)) > kOrthoTolerance) {
        return false;
    }
    return true;
}

PlaneIntersectionResult intersectPlaneForward(
    const Ray& ray,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double epsilon) {
    if (!math::isFinite(ray.originMetres)) {
        throw std::invalid_argument("ray origin must be finite");
    }
    if (!math::isFinite(ray.direction) || math::lengthSquared(ray.direction) <= 0.0) {
        throw std::invalid_argument("ray direction must be finite and non-zero");
    }
    if (!std::isfinite(ray.wavelengthMetres) || ray.wavelengthMetres <= 0.0) {
        throw std::invalid_argument("ray wavelength must be finite and positive");
    }
    if (!std::isfinite(ray.power) || ray.power < 0.0) {
        throw std::invalid_argument("ray power must be finite and non-negative");
    }
    if (!math::isFinite(planePointMetres)) {
        throw std::invalid_argument("plane point must be finite");
    }
    if (!math::isFinite(planeNormal) || math::lengthSquared(planeNormal) <= 0.0) {
        throw std::invalid_argument("plane normal must be finite and non-zero");
    }
    if (!std::isfinite(epsilon) || epsilon < 0.0) {
        throw std::invalid_argument("epsilon must be finite and non-negative");
    }

    const math::Vec3d d = math::normalized(ray.direction);
    const math::Vec3d n = math::normalized(planeNormal);

    const double denom = math::dot(d, n);
    if (std::abs(denom) <= epsilon) {
        return PlaneIntersectionResult {.hit = false, .signedDistanceMetres = 0.0, .pointMetres = {}};
    }

    const double numer = math::dot(planePointMetres - ray.originMetres, n);
    const double t = numer / denom;

    // Strict forward distance rejection: t must be strictly positive and > epsilon (reject t=0 even if epsilon=0)
    if (t <= epsilon || t <= 0.0) {
        return PlaneIntersectionResult {.hit = false, .signedDistanceMetres = t, .pointMetres = {}};
    }

    const math::Vec3d hitPoint = ray.originMetres + d * t;
    return PlaneIntersectionResult {.hit = true, .signedDistanceMetres = t, .pointMetres = hitPoint};
}

std::optional<math::Vec3d> intersectForwardPlane(
    const Ray& ray,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double epsilon) {
    const auto result = intersectPlaneForward(ray, planePointMetres, planeNormal, epsilon);
    if (!result.hit) {
        return std::nullopt;
    }
    return result.pointMetres;
}

bool isInsideRectangularAperture(
    math::Vec3d pointMetres,
    math::Vec3d planePointMetres,
    const PlanarApertureBasis& basis,
    double widthMetres,
    double heightMetres,
    double tolerance) {
    if (!math::isFinite(pointMetres) || !math::isFinite(planePointMetres)) {
        throw std::invalid_argument("point and plane point coordinates must be finite");
    }
    validatePlanarApertureBasis(basis);
    if (!std::isfinite(widthMetres) || widthMetres <= 0.0 || !std::isfinite(heightMetres) || heightMetres <= 0.0) {
        throw std::invalid_argument("aperture width and height must be finite and positive");
    }
    if (!std::isfinite(tolerance) || tolerance < 0.0) {
        throw std::invalid_argument("tolerance must be finite and non-negative");
    }
    const math::Vec3d delta = pointMetres - planePointMetres;
    const double uProj = math::dot(delta, basis.uAxis);
    const double vProj = math::dot(delta, basis.vAxis);
    const double halfW = widthMetres * 0.5 + tolerance;
    const double halfH = heightMetres * 0.5 + tolerance;
    return (std::abs(uProj) <= halfW) && (std::abs(vProj) <= halfH);
}

bool isInsideRectangularAperture(
    math::Vec3d pointMetres,
    math::Vec3d planePointMetres,
    math::Vec3d planeNormal,
    double widthMetres,
    double heightMetres,
    double tolerance) {
    const auto basis = computePlanarApertureBasis(planeNormal);
    return isInsideRectangularAperture(pointMetres, planePointMetres, basis, widthMetres, heightMetres, tolerance);
}

GeometricTraceResult tracePlanarMirror(
    const Ray& incident,
    const scene::PlanarMirror& mirror,
    double epsilon) {
    scene::validatePlanarMirror(mirror);

    const auto isect = intersectPlaneForward(incident, mirror.planePointMetres, mirror.normal, epsilon);
    if (!isect.hit) {
        return GeometricTraceResult {
            .status = GeometricInteractionStatus::Miss,
            .distanceMetres = isect.signedDistanceMetres,
            .intersectionMetres = {},
            .outgoingRay = std::nullopt,
        };
    }

    if (!isInsideRectangularAperture(
            isect.pointMetres,
            mirror.planePointMetres,
            mirror.normal,
            mirror.widthMetres,
            mirror.heightMetres,
            epsilon)) {
        return GeometricTraceResult {
            .status = GeometricInteractionStatus::Clipped,
            .distanceMetres = isect.signedDistanceMetres,
            .intersectionMetres = isect.pointMetres,
            .outgoingRay = std::nullopt,
        };
    }

    const math::Vec3d d = math::normalized(incident.direction);
    const math::Vec3d n = math::normalized(mirror.normal);
    const double dDotN = math::dot(d, n);
    const math::Vec3d normalOriented = (dDotN <= 0.0) ? n : -n;
    const double cosThetaI = -math::dot(d, normalOriented);
    const math::Vec3d reflectedDir = math::normalized(d + normalOriented * (2.0 * cosThetaI));

    const Ray outgoing = makeRay(
        isect.pointMetres,
        reflectedDir,
        incident.wavelengthMetres,
        incident.power);

    return GeometricTraceResult {
        .status = GeometricInteractionStatus::Reflected,
        .distanceMetres = isect.signedDistanceMetres,
        .intersectionMetres = isect.pointMetres,
        .outgoingRay = outgoing,
    };
}

GeometricTraceResult tracePlaneInterface(
    const Ray& incident,
    const scene::PlaneInterfaceComponent& interfaceComp,
    double epsilon) {
    scene::validatePlaneInterfaceComponent(interfaceComp);

    const auto isect = intersectPlaneForward(incident, interfaceComp.planePointMetres, interfaceComp.normal, epsilon);
    if (!isect.hit) {
        return GeometricTraceResult {
            .status = GeometricInteractionStatus::Miss,
            .distanceMetres = isect.signedDistanceMetres,
            .intersectionMetres = {},
            .outgoingRay = std::nullopt,
        };
    }

    if (!isInsideRectangularAperture(
            isect.pointMetres,
            interfaceComp.planePointMetres,
            interfaceComp.normal,
            interfaceComp.widthMetres,
            interfaceComp.heightMetres,
            epsilon)) {
        return GeometricTraceResult {
            .status = GeometricInteractionStatus::Clipped,
            .distanceMetres = isect.signedDistanceMetres,
            .intersectionMetres = isect.pointMetres,
            .outgoingRay = std::nullopt,
        };
    }

    const auto ifResult = interactInterface(
        incident,
        isect.pointMetres,
        interfaceComp.normal,
        interfaceComp.nIncident,
        interfaceComp.nTransmitted);

    const GeometricInteractionStatus status = (ifResult.status == InterfaceInteractionStatus::Refracted)
        ? GeometricInteractionStatus::Refracted
        : GeometricInteractionStatus::TotalInternalReflection;

    return GeometricTraceResult {
        .status = status,
        .distanceMetres = isect.signedDistanceMetres,
        .intersectionMetres = isect.pointMetres,
        .outgoingRay = ifResult.outgoingRay,
    };
}

void emitCollimatedRays(
    const scene::CollimatedSource& source,
    std::size_t rayCount,
    std::vector<Ray>& outRays,
    CollimatedRayPattern pattern) {
    // 1. Validate inputs thoroughly before mutating caller buffer (strong exception safety)
    scene::validateCollimatedSource(source);

    if (rayCount == 0 || rayCount > kMaxCollimatedRayCount) {
        throw std::invalid_argument("rayCount must be greater than 0 and not exceed maximum allowable limit");
    }

    if (static_cast<int>(pattern) < static_cast<int>(CollimatedRayPattern::FibonacciDisk)
        || static_cast<int>(pattern) > static_cast<int>(CollimatedRayPattern::SingleRay)) {
        throw std::invalid_argument("unrecognized CollimatedRayPattern value");
    }

    const math::Vec3d dir = math::normalized(source.direction);
    const PlanarApertureBasis basis = computePlanarApertureBasis(dir);
    validatePlanarApertureBasis(basis);

    const double perRayPower = source.powerWatts / static_cast<double>(rayCount);
    if (!std::isfinite(perRayPower) || perRayPower < 0.0) {
        throw std::invalid_argument("computed per-ray power must be finite and non-negative");
    }

    // 2. Reserve memory first; if this throws std::bad_alloc, outRays is unmodified
    outRays.reserve(rayCount);
    outRays.clear();

    const double R = source.beamRadiusMetres;

    if (pattern == CollimatedRayPattern::SingleRay || rayCount == 1) {
        for (std::size_t i = 0; i < rayCount; ++i) {
            outRays.push_back(makeRay(source.originMetres, dir, source.wavelengthMetres, perRayPower));
        }
        return;
    }

    if (pattern == CollimatedRayPattern::FibonacciDisk) {
        constexpr double kGoldenAngle = 2.3999632297286533222; // pi * (3 - sqrt(5))
        for (std::size_t i = 0; i < rayCount; ++i) {
            const double r = R * std::sqrt((static_cast<double>(i) + 0.5) / static_cast<double>(rayCount));
            const double theta = static_cast<double>(i) * kGoldenAngle;
            const double xLoc = r * std::cos(theta);
            const double yLoc = r * std::sin(theta);
            const math::Vec3d origin = source.originMetres + basis.uAxis * xLoc + basis.vAxis * yLoc;
            outRays.push_back(makeRay(origin, dir, source.wavelengthMetres, perRayPower));
        }
    } else if (pattern == CollimatedRayPattern::ConcentricRings) {
        outRays.push_back(makeRay(source.originMetres, dir, source.wavelengthMetres, perRayPower));
        const std::size_t remaining = rayCount - 1;
        if (remaining > 0) {
            const std::size_t numRings = static_cast<std::size_t>(std::max(1.0, std::ceil(std::sqrt(static_cast<double>(remaining)))));
            std::size_t raysAssigned = 0;
            for (std::size_t ring = 1; ring <= numRings && raysAssigned < remaining; ++ring) {
                const double r = R * (static_cast<double>(ring) / static_cast<double>(numRings));
                const std::size_t raysInThisRing = std::min(
                    remaining - raysAssigned,
                    static_cast<std::size_t>(std::max(std::size_t{1}, static_cast<std::size_t>(std::round(6.0 * static_cast<double>(ring))))));
                const double dTheta = (2.0 * std::numbers::pi_v<double>) / static_cast<double>(raysInThisRing);
                for (std::size_t k = 0; k < raysInThisRing; ++k) {
                    const double theta = static_cast<double>(k) * dTheta;
                    const double xLoc = r * std::cos(theta);
                    const double yLoc = r * std::sin(theta);
                    const math::Vec3d origin = source.originMetres + basis.uAxis * xLoc + basis.vAxis * yLoc;
                    outRays.push_back(makeRay(origin, dir, source.wavelengthMetres, perRayPower));
                    ++raysAssigned;
                }
            }
            while (outRays.size() < rayCount) {
                outRays.push_back(makeRay(source.originMetres, dir, source.wavelengthMetres, perRayPower));
            }
        }
    } else if (pattern == CollimatedRayPattern::UniformGrid) {
        // Direct population into outRays without creating temporary vector allocations
        const std::size_t side = static_cast<std::size_t>(std::ceil(std::sqrt(static_cast<double>(rayCount) * 1.5)));
        const double step = (2.0 * R) / static_cast<double>(side);
        for (std::size_t iy = 0; iy < side && outRays.size() < rayCount; ++iy) {
            const double y = -R + (static_cast<double>(iy) + 0.5) * step;
            for (std::size_t ix = 0; ix < side && outRays.size() < rayCount; ++ix) {
                const double x = -R + (static_cast<double>(ix) + 0.5) * step;
                if (x * x + y * y <= R * R) {
                    const math::Vec3d origin = source.originMetres + basis.uAxis * x + basis.vAxis * y;
                    outRays.push_back(makeRay(origin, dir, source.wavelengthMetres, perRayPower));
                }
            }
        }
        while (outRays.size() < rayCount) {
            outRays.push_back(makeRay(source.originMetres, dir, source.wavelengthMetres, perRayPower));
        }
    }
}

std::vector<Ray> emitCollimatedRays(
    const scene::CollimatedSource& source,
    std::size_t rayCount,
    CollimatedRayPattern pattern) {
    std::vector<Ray> rays;
    emitCollimatedRays(source, rayCount, rays, pattern);
    return rays;
}

} // namespace holobench::optics::ray
