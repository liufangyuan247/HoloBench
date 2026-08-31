#include "optics/ray/RotationalSurface.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <optional>
#include <stdexcept>

namespace holobench::optics::ray {

namespace {

struct RootEvaluation final {
    double valueMetres = 0.0;
    double derivative = 0.0;
    math::Vec3d pointMetres {};
};

[[nodiscard]] SurfaceSagEvaluation evaluateSurfaceSagUnchecked(
    const RotationalSurface& surface,
    double radialCoordinateMetres);

[[nodiscard]] bool hasAsphereTerms(const RotationalSurface& surface) noexcept {
    return !surface.evenAsphereTerms.empty();
}

[[nodiscard]] double integerPower(double base, unsigned exponent) {
    double result = 1.0;
    double factor = base;
    unsigned remaining = exponent;
    while (remaining > 0U) {
        if ((remaining & 1U) != 0U) {
            result *= factor;
        }
        remaining >>= 1U;
        if (remaining > 0U) {
            factor *= factor;
        }
    }
    if (!std::isfinite(result)) {
        throw std::overflow_error("surface polynomial power is not representable");
    }
    return result;
}

[[nodiscard]] RootEvaluation evaluateRoot(
    const Ray& ray,
    const RotationalSurface& surface,
    double distanceMetres) {
    const math::Vec3d point = ray.originMetres + ray.direction * distanceMetres;
    if (!math::isFinite(point)) {
        throw std::overflow_error("surface intersection point is not finite");
    }
    const double radius = std::hypot(point.x, point.y);
    const SurfaceSagEvaluation sag = evaluateSurfaceSagUnchecked(surface, radius);
    const double radialRate = (radius > 0.0)
        ? ((point.x * ray.direction.x + point.y * ray.direction.y) / radius)
        : 0.0;
    return RootEvaluation {
        .valueMetres = point.z - sag.sagMetres,
        .derivative = ray.direction.z - sag.radialDerivative * radialRate,
        .pointMetres = point,
    };
}

[[nodiscard]] math::Vec3d normalFromSurfacePoint(
    const RotationalSurface& surface,
    math::Vec3d localSurfacePointMetres) {
    const double radius = std::hypot(localSurfacePointMetres.x, localSurfacePointMetres.y);
    const SurfaceSagEvaluation evaluation = evaluateSurfaceSagUnchecked(surface, radius);
    const double xSlope = (radius > 0.0)
        ? evaluation.radialDerivative * localSurfacePointMetres.x / radius
        : 0.0;
    const double ySlope = (radius > 0.0)
        ? evaluation.radialDerivative * localSurfacePointMetres.y / radius
        : 0.0;
    return math::normalized({-xSlope, -ySlope, 1.0});
}

[[nodiscard]] double scaledResidualTolerance(
    const RootEvaluation& evaluation,
    const SurfaceIntersectionOptions& options) noexcept {
    const double coordinateScale = std::max({
        std::abs(evaluation.pointMetres.x),
        std::abs(evaluation.pointMetres.y),
        std::abs(evaluation.pointMetres.z),
        1e-6,
    });
    return std::max(
        options.residualToleranceMetres,
        128.0 * std::numeric_limits<double>::epsilon() * coordinateScale);
}

[[nodiscard]] SurfaceIntersectionResult makeAcceptedResult(
    const Ray& ray,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options,
    double distanceMetres,
    std::size_t iterations) {
    const RootEvaluation evaluation = evaluateRoot(ray, surface, distanceMetres);
    const double residual = std::abs(evaluation.valueMetres);
    if (residual > scaledResidualTolerance(evaluation, options)) {
        return SurfaceIntersectionResult {
            .status = SurfaceIntersectionStatus::DidNotConverge,
            .distanceMetres = distanceMetres,
            .pointMetres = evaluation.pointMetres,
            .geometricNormal = {},
            .spatialResidualMetres = residual,
            .iterations = iterations,
        };
    }

    const double radius = std::hypot(evaluation.pointMetres.x, evaluation.pointMetres.y);
    const bool clipped = radius > surface.clearSemiDiameterMetres + options.apertureToleranceMetres;
    return SurfaceIntersectionResult {
        .status = clipped ? SurfaceIntersectionStatus::Clipped : SurfaceIntersectionStatus::Hit,
        .distanceMetres = distanceMetres,
        .pointMetres = evaluation.pointMetres,
        .geometricNormal = normalFromSurfacePoint(surface, evaluation.pointMetres),
        .spatialResidualMetres = residual,
        .iterations = iterations,
    };
}

[[nodiscard]] std::vector<double> solveQuadratic(double a, double b, double c) {
    const double scale = std::max({std::abs(a), std::abs(b), std::abs(c)});
    if (scale == 0.0) {
        return {};
    }
    a /= scale;
    b /= scale;
    c /= scale;

    constexpr double kCoefficientTolerance = 64.0 * std::numeric_limits<double>::epsilon();
    if (std::abs(a) <= kCoefficientTolerance) {
        if (std::abs(b) <= kCoefficientTolerance) {
            return {};
        }
        return {-c / b};
    }

    double discriminant = b * b - 4.0 * a * c;
    const double discriminantTolerance = 128.0 * std::numeric_limits<double>::epsilon()
        * (b * b + std::abs(4.0 * a * c));
    if (discriminant < -discriminantTolerance) {
        return {};
    }
    discriminant = std::max(0.0, discriminant);
    const double rootDiscriminant = std::sqrt(discriminant);
    if (rootDiscriminant == 0.0) {
        return {-b / (2.0 * a)};
    }

    const double q = -0.5 * (b + std::copysign(rootDiscriminant, b));
    if (q == 0.0) {
        return {-b / (2.0 * a)};
    }
    std::vector<double> roots {q / a, c / q};
    std::sort(roots.begin(), roots.end());
    return roots;
}

[[nodiscard]] SurfaceIntersectionResult intersectBaseConic(
    const Ray& ray,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options) {
    const double c = surface.curvaturePerMetre;
    const double onePlusK = 1.0 + surface.conicConstant;
    const auto& o = ray.originMetres;
    const auto& d = ray.direction;

    const double quadratic = c * (d.x * d.x + d.y * d.y + onePlusK * d.z * d.z);
    const double linear = 2.0 * c * (o.x * d.x + o.y * d.y + onePlusK * o.z * d.z) - 2.0 * d.z;
    const double constant = c * (o.x * o.x + o.y * o.y + onePlusK * o.z * o.z) - 2.0 * o.z;

    const std::vector<double> roots = solveQuadratic(quadratic, linear, constant);
    bool encounteredDomainFailure = false;
    for (const double root : roots) {
        if (!std::isfinite(root)
            || root <= options.intersectionEpsilonMetres
            || root > options.maximumDistanceMetres) {
            continue;
        }
        try {
            const RootEvaluation evaluation = evaluateRoot(ray, surface, root);
            if (std::abs(evaluation.valueMetres) <= scaledResidualTolerance(evaluation, options)) {
                return makeAcceptedResult(ray, surface, options, root, 0);
            }
        } catch (const std::domain_error&) {
            encounteredDomainFailure = true;
        }
    }
    return SurfaceIntersectionResult {
        .status = encounteredDomainFailure ? SurfaceIntersectionStatus::OutsideDomain : SurfaceIntersectionStatus::Miss,
    };
}

[[nodiscard]] std::optional<double> tryNewton(
    const Ray& ray,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options,
    double seed,
    std::size_t& iterations) {
    double current = std::clamp(
        seed,
        std::nextafter(options.intersectionEpsilonMetres, options.maximumDistanceMetres),
        options.maximumDistanceMetres);
    for (std::size_t iteration = 0; iteration < options.maximumIterations; ++iteration) {
        RootEvaluation evaluation {};
        try {
            evaluation = evaluateRoot(ray, surface, current);
        } catch (const std::domain_error&) {
            return std::nullopt;
        }
        iterations = iteration + 1;
        if (std::abs(evaluation.valueMetres) <= scaledResidualTolerance(evaluation, options)) {
            return current;
        }
        const double derivativeFloor = 64.0 * std::numeric_limits<double>::epsilon();
        if (!std::isfinite(evaluation.derivative) || std::abs(evaluation.derivative) <= derivativeFloor) {
            return std::nullopt;
        }
        const double next = current - evaluation.valueMetres / evaluation.derivative;
        if (!std::isfinite(next)
            || next <= options.intersectionEpsilonMetres
            || next > options.maximumDistanceMetres) {
            return std::nullopt;
        }
        if (next == current) {
            return std::nullopt;
        }
        current = next;
    }
    return std::nullopt;
}

[[nodiscard]] std::optional<double> refineBracket(
    const Ray& ray,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options,
    double lower,
    double upper,
    double lowerValue,
    std::size_t& iterations) {
    double lo = lower;
    double hi = upper;
    double fLo = lowerValue;
    for (std::size_t iteration = 0; iteration < options.maximumIterations; ++iteration) {
        const double mid = lo + (hi - lo) * 0.5;
        const RootEvaluation evaluation = evaluateRoot(ray, surface, mid);
        iterations = iteration + 1;
        if (std::abs(evaluation.valueMetres) <= scaledResidualTolerance(evaluation, options)
            || (hi - lo) <= options.residualToleranceMetres) {
            return mid;
        }
        if (std::signbit(evaluation.valueMetres) == std::signbit(fLo)) {
            lo = mid;
            fLo = evaluation.valueMetres;
        } else {
            hi = mid;
        }
    }
    return std::nullopt;
}

[[nodiscard]] SurfaceIntersectionResult intersectEvenAsphere(
    const Ray& ray,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options) {
    double seed = options.intersectionEpsilonMetres * 2.0;
    if (std::abs(ray.direction.z) > 64.0 * std::numeric_limits<double>::epsilon()) {
        const double planeDistance = -ray.originMetres.z / ray.direction.z;
        if (std::isfinite(planeDistance) && planeDistance > options.intersectionEpsilonMetres) {
            seed = planeDistance;
        }
    }

    std::size_t newtonIterations = 0;
    const std::optional<double> newtonRoot = tryNewton(ray, surface, options, seed, newtonIterations);
    const double scanLimit = newtonRoot.value_or(options.maximumDistanceMetres);
    const double start = std::nextafter(options.intersectionEpsilonMetres, options.maximumDistanceMetres);
    bool hadValidEvaluation = false;
    bool havePrevious = false;
    double previousDistance = 0.0;
    double previousValue = 0.0;

    for (std::size_t index = 0; index <= options.bracketSubdivisions; ++index) {
        const double fraction = static_cast<double>(index) / static_cast<double>(options.bracketSubdivisions);
        const double distance = start + (scanLimit - start) * fraction;
        RootEvaluation evaluation {};
        try {
            evaluation = evaluateRoot(ray, surface, distance);
            hadValidEvaluation = true;
        } catch (const std::domain_error&) {
            havePrevious = false;
            continue;
        }

        if (std::abs(evaluation.valueMetres) <= scaledResidualTolerance(evaluation, options)) {
            return makeAcceptedResult(ray, surface, options, distance, index + newtonIterations);
        }
        if (havePrevious && std::signbit(evaluation.valueMetres) != std::signbit(previousValue)) {
            std::size_t bracketIterations = 0;
            const auto root = refineBracket(
                ray,
                surface,
                options,
                previousDistance,
                distance,
                previousValue,
                bracketIterations);
            if (!root.has_value()) {
                return SurfaceIntersectionResult {
                    .status = SurfaceIntersectionStatus::DidNotConverge,
                    .iterations = index + bracketIterations + newtonIterations,
                };
            }
            return makeAcceptedResult(
                ray,
                surface,
                options,
                *root,
                index + bracketIterations + newtonIterations);
        }
        havePrevious = true;
        previousDistance = distance;
        previousValue = evaluation.valueMetres;
    }

    if (newtonRoot.has_value()) {
        return makeAcceptedResult(ray, surface, options, *newtonRoot, newtonIterations);
    }
    return SurfaceIntersectionResult {
        .status = hadValidEvaluation ? SurfaceIntersectionStatus::Miss : SurfaceIntersectionStatus::OutsideDomain,
        .iterations = newtonIterations + options.bracketSubdivisions + 1,
    };
}

SurfaceSagEvaluation evaluateSurfaceSagUnchecked(
    const RotationalSurface& surface,
    double radialCoordinateMetres) {
    const double c = surface.curvaturePerMetre;
    const double cRadius = c * radialCoordinateMetres;
    const double radicand = 1.0 - (1.0 + surface.conicConstant) * cRadius * cRadius;
    if (!std::isfinite(radicand) || radicand < 0.0) {
        throw std::domain_error("radial coordinate is outside the conic surface domain");
    }
    const double root = std::sqrt(radicand);
    const double radiusSquared = radialCoordinateMetres * radialCoordinateMetres;
    double sag = (c == 0.0) ? 0.0 : (c * radiusSquared / (1.0 + root));
    double derivative = 0.0;
    if (c != 0.0 && radialCoordinateMetres != 0.0) {
        if (root == 0.0) {
            throw std::domain_error("surface derivative is singular at the conic domain boundary");
        }
        derivative = c * radialCoordinateMetres / root;
    }

    for (const EvenAsphereTerm& term : surface.evenAsphereTerms) {
        if (term.coefficientSi == 0.0) {
            continue;
        }
        const double radialPower = integerPower(radialCoordinateMetres, term.radialOrder);
        const double derivativePower = integerPower(radialCoordinateMetres, term.radialOrder - 1U);
        sag += term.coefficientSi * radialPower;
        derivative += static_cast<double>(term.radialOrder) * term.coefficientSi * derivativePower;
    }
    if (!std::isfinite(sag) || !std::isfinite(derivative)) {
        throw std::overflow_error("surface sag or derivative is not representable");
    }
    return {.sagMetres = sag, .radialDerivative = derivative};
}

} // namespace

void validateRotationalSurface(const RotationalSurface& surface) {
    if (!std::isfinite(surface.curvaturePerMetre)) {
        throw std::invalid_argument("surface curvature must be finite");
    }
    if (!std::isfinite(surface.conicConstant)) {
        throw std::invalid_argument("surface conic constant must be finite");
    }
    if (!std::isfinite(surface.clearSemiDiameterMetres) || surface.clearSemiDiameterMetres <= 0.0) {
        throw std::invalid_argument("surface clear semi-diameter must be finite and positive");
    }

    unsigned previousOrder = 0;
    for (const EvenAsphereTerm& term : surface.evenAsphereTerms) {
        if (term.radialOrder < 4U || (term.radialOrder % 2U) != 0U) {
            throw std::invalid_argument("asphere radial orders must be even and at least four");
        }
        if (term.radialOrder <= previousOrder) {
            throw std::invalid_argument("asphere radial orders must be strictly increasing and unique");
        }
        if (!std::isfinite(term.coefficientSi)) {
            throw std::invalid_argument("asphere coefficients must be finite");
        }
        previousOrder = term.radialOrder;
    }

    const double cRadius = surface.curvaturePerMetre * surface.clearSemiDiameterMetres;
    const double radicand = 1.0 - (1.0 + surface.conicConstant) * cRadius * cRadius;
    if (!std::isfinite(radicand) || radicand <= 0.0) {
        throw std::invalid_argument("clear aperture must remain inside the differentiable conic surface domain");
    }

    try {
        static_cast<void>(evaluateSurfaceSagUnchecked(surface, surface.clearSemiDiameterMetres));
    } catch (const std::overflow_error&) {
        throw std::invalid_argument("surface sag is not representable across the clear aperture");
    }
}

void validateSurfaceIntersectionOptions(const SurfaceIntersectionOptions& options) {
    if (!std::isfinite(options.intersectionEpsilonMetres) || options.intersectionEpsilonMetres < 0.0) {
        throw std::invalid_argument("intersection epsilon must be finite and non-negative");
    }
    if (!std::isfinite(options.maximumDistanceMetres)
        || options.maximumDistanceMetres <= options.intersectionEpsilonMetres) {
        throw std::invalid_argument("maximum distance must be finite and greater than the intersection epsilon");
    }
    if (!std::isfinite(options.residualToleranceMetres) || options.residualToleranceMetres <= 0.0) {
        throw std::invalid_argument("residual tolerance must be finite and positive");
    }
    if (!std::isfinite(options.apertureToleranceMetres) || options.apertureToleranceMetres < 0.0) {
        throw std::invalid_argument("aperture tolerance must be finite and non-negative");
    }
    if (options.maximumIterations == 0 || options.bracketSubdivisions == 0) {
        throw std::invalid_argument("surface solver iteration limits must be non-zero");
    }
}

SurfaceSagEvaluation evaluateSurfaceSag(
    const RotationalSurface& surface,
    double radialCoordinateMetres) {
    validateRotationalSurface(surface);
    if (!std::isfinite(radialCoordinateMetres) || radialCoordinateMetres < 0.0) {
        throw std::invalid_argument("surface radial coordinate must be finite and non-negative");
    }
    return evaluateSurfaceSagUnchecked(surface, radialCoordinateMetres);
}

math::Vec3d surfaceNormalAt(
    const RotationalSurface& surface,
    math::Vec3d localSurfacePointMetres) {
    validateRotationalSurface(surface);
    if (!math::isFinite(localSurfacePointMetres)) {
        throw std::invalid_argument("surface point must be finite");
    }
    const double radius = std::hypot(localSurfacePointMetres.x, localSurfacePointMetres.y);
    const SurfaceSagEvaluation evaluation = evaluateSurfaceSagUnchecked(surface, radius);
    const double residual = std::abs(localSurfacePointMetres.z - evaluation.sagMetres);
    const double tolerance = std::max(
        1e-12,
        128.0 * std::numeric_limits<double>::epsilon() * std::max(std::abs(localSurfacePointMetres.z), 1e-6));
    if (residual > tolerance) {
        throw std::invalid_argument("normal requested at a point that is not on the surface");
    }
    return normalFromSurfacePoint(surface, localSurfacePointMetres);
}

SurfaceIntersectionResult intersectRotationalSurfaceForward(
    const Ray& localRay,
    const RotationalSurface& surface,
    const SurfaceIntersectionOptions& options) {
    validateRotationalSurface(surface);
    validateSurfaceIntersectionOptions(options);
    if (!math::isFinite(localRay.originMetres)) {
        throw std::invalid_argument("ray origin must be finite");
    }
    if (!math::isFinite(localRay.direction) || math::lengthSquared(localRay.direction) <= 0.0) {
        throw std::invalid_argument("ray direction must be finite and non-zero");
    }
    if (!std::isfinite(localRay.wavelengthMetres) || localRay.wavelengthMetres <= 0.0) {
        throw std::invalid_argument("ray wavelength must be finite and positive");
    }
    if (!std::isfinite(localRay.power) || localRay.power < 0.0) {
        throw std::invalid_argument("ray power must be finite and non-negative");
    }

    const Ray normalizedRay = makeRay(
        localRay.originMetres,
        localRay.direction,
        localRay.wavelengthMetres,
        localRay.power);
    if (!hasAsphereTerms(surface)) {
        return intersectBaseConic(normalizedRay, surface, options);
    }
    return intersectEvenAsphere(normalizedRay, surface, options);
}

} // namespace holobench::optics::ray
