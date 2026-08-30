#include "optics/ray/BenchTracer.hpp"

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "optics/ray/Ray.hpp"

namespace holobench::optics::ray {
namespace {

constexpr double kTwoPi = 2.0 * std::numbers::pi_v<double>;
constexpr double kGoldenAngle = std::numbers::pi_v<double> * (3.0 - 2.2360679774997896964); // pi * (3 - sqrt(5))

void sampleAperturePoint(
    RaySamplingPattern pattern,
    std::size_t index,
    std::size_t totalCount,
    double centreX,
    double centreY,
    double radius,
    double& outX,
    double& outY) noexcept {
    if (totalCount <= 1) {
        outX = centreX;
        outY = centreY;
        return;
    }

    switch (pattern) {
    case RaySamplingPattern::FibonacciDisk: {
        const double r = radius * std::sqrt((static_cast<double>(index) + 0.5) / static_cast<double>(totalCount));
        const double theta = static_cast<double>(index) * kGoldenAngle;
        outX = centreX + r * std::cos(theta);
        outY = centreY + r * std::sin(theta);
        break;
    }
    case RaySamplingPattern::MeridionalFan: {
        const double t = 2.0 * static_cast<double>(index) / static_cast<double>(totalCount - 1) - 1.0;
        outX = centreX;
        outY = centreY + t * radius;
        break;
    }
    case RaySamplingPattern::SagittalFan: {
        const double t = 2.0 * static_cast<double>(index) / static_cast<double>(totalCount - 1) - 1.0;
        outX = centreX + t * radius;
        outY = centreY;
        break;
    }
    case RaySamplingPattern::CrossFans: {
        const std::size_t half = totalCount / 2;
        const std::size_t otherHalf = totalCount - half;
        if (index < half) {
            const double t = (half > 1) ? (2.0 * static_cast<double>(index) / static_cast<double>(half - 1) - 1.0) : 0.0;
            outX = centreX;
            outY = centreY + t * radius;
        } else {
            const std::size_t j = index - half;
            const double t = (otherHalf > 1) ? (2.0 * static_cast<double>(j) / static_cast<double>(otherHalf - 1) - 1.0) : 0.0;
            outX = centreX + t * radius;
            outY = centreY;
        }
        break;
    }
    case RaySamplingPattern::ApertureBoundary: {
        const double theta = kTwoPi * static_cast<double>(index) / static_cast<double>(totalCount);
        outX = centreX + radius * std::cos(theta);
        outY = centreY + radius * std::sin(theta);
        break;
    }
    case RaySamplingPattern::ConcentricRings: {
        if (index == 0) {
            outX = centreX;
            outY = centreY;
        } else {
            constexpr std::size_t kNumRings = 3;
            const std::size_t ringIdx = ((index - 1) * kNumRings) / (totalCount - 1);
            const double r = radius * std::sqrt(static_cast<double>(ringIdx + 1) / static_cast<double>(kNumRings));
            const double theta = kTwoPi * static_cast<double>(index) / static_cast<double>(totalCount - 1);
            outX = centreX + r * std::cos(theta);
            outY = centreY + r * std::sin(theta);
        }
        break;
    }
    }
}

constexpr double kCoplanarEpsilonRel = 1e-9;
constexpr double kCoplanarEpsilonAbs = 1e-12;

[[nodiscard]] bool isCoplanarZ(double z1, double z2) noexcept {
    return std::abs(z1 - z2) <= (kCoplanarEpsilonAbs + kCoplanarEpsilonRel * std::max(std::abs(z1), std::abs(z2)));
}

[[nodiscard]] constexpr bool isValidSamplingPattern(RaySamplingPattern pattern) noexcept {
    switch (pattern) {
    case RaySamplingPattern::FibonacciDisk:
    case RaySamplingPattern::ConcentricRings:
    case RaySamplingPattern::MeridionalFan:
    case RaySamplingPattern::SagittalFan:
    case RaySamplingPattern::CrossFans:
    case RaySamplingPattern::ApertureBoundary:
        return true;
    default:
        return false;
    }
}

} // namespace

void traceBench(
    const scene::OpticalBenchScene& scene,
    const BenchTracerOptions& options,
    std::vector<RaySegment>& outSegments) {
    scene::validateScene(scene);

    if (!isValidSamplingPattern(options.pattern)) {
        throw std::invalid_argument("invalid ray sampling pattern");
    }
    if (options.rayCount == 0) {
        throw std::invalid_argument("bench tracer ray count must be positive");
    }
    if (!std::isfinite(options.maxPropagationDistanceMetres) || options.maxPropagationDistanceMetres <= 0.0) {
        throw std::invalid_argument("max propagation distance must be finite and positive");
    }
    if (!std::isfinite(options.virtualExtensionDistanceMetres) || options.virtualExtensionDistanceMetres <= 0.0) {
        throw std::invalid_argument("virtual extension distance must be finite and positive");
    }

    const std::size_t maxSegmentsPerRay = options.includeVirtualExtensions ? 3 : 2;
    if (options.rayCount > std::numeric_limits<std::size_t>::max() / maxSegmentsPerRay) {
        throw std::invalid_argument("bench tracer ray count causes capacity overflow");
    }
    const std::size_t requiredCapacity = options.rayCount * maxSegmentsPerRay;

    outSegments.clear();
    if (outSegments.capacity() < requiredCapacity) {
        outSegments.reserve(requiredCapacity);
    }

    const auto prediction = scene::predictThinLensImage(scene);
    const double lensZ = scene.lens.planeZMetres;
    const double lensCx = scene.lens.centreXMetres;
    const double lensCy = scene.lens.centreYMetres;
    const double clearRadius = scene.lens.clearApertureRadiusMetres;
    const double rayPower = scene.source.powerWatts / static_cast<double>(options.rayCount);
    const auto lensModel = scene.lens.toIdealThinLens();

    const double apZ = scene.aperture.planeZMetres;
    const double apCx = scene.aperture.centreXMetres;
    const double apCy = scene.aperture.centreYMetres;
    const double apRadiusSq = scene.aperture.radiusMetres * scene.aperture.radiusMetres;

    const double screenZ = scene.screen.planeZMetres;
    const math::Vec3d sourcePos = scene.source.positionMetres;

    for (std::size_t i = 0; i < options.rayCount; ++i) {
        double targetX = 0.0;
        double targetY = 0.0;
        sampleAperturePoint(options.pattern, i, options.rayCount, lensCx, lensCy, clearRadius, targetX, targetY);

        const math::Vec3d targetOnLens {targetX, targetY, lensZ};
        const math::Vec3d incidentDir = math::normalized(targetOnLens - sourcePos);
        const Ray incidentRay = makeRay(sourcePos, incidentDir, scene.source.wavelengthMetres, rayPower);

        // 1. Aperture before lens (z_source < z_aperture < z_lens)
        if (apZ > sourcePos.z && apZ < lensZ && !isCoplanarZ(apZ, lensZ)) {
            if (incidentDir.z > kReferenceIntersectionEpsilon) {
                const double tAp = (apZ - sourcePos.z) / incidentDir.z;
                const math::Vec3d pointOnAperture = sourcePos + incidentDir * tAp;
                const double dx = pointOnAperture.x - apCx;
                const double dy = pointOnAperture.y - apCy;
                if (dx * dx + dy * dy > apRadiusSq) {
                    outSegments.push_back(RaySegment {
                        .startMetres = sourcePos,
                        .endMetres = pointOnAperture,
                        .wavelengthMetres = incidentRay.wavelengthMetres,
                        .power = incidentRay.power,
                        .kind = RaySegmentKind::Clipped,
                        .rayIndex = i,
                    });
                    continue;
                }
            }
        }

        // 2. Aperture coplanar with lens plane
        if (isCoplanarZ(apZ, lensZ)) {
            const double dx = targetX - apCx;
            const double dy = targetY - apCy;
            if (dx * dx + dy * dy > apRadiusSq) {
                outSegments.push_back(RaySegment {
                    .startMetres = sourcePos,
                    .endMetres = targetOnLens,
                    .wavelengthMetres = incidentRay.wavelengthMetres,
                    .power = incidentRay.power,
                    .kind = RaySegmentKind::Clipped,
                    .rayIndex = i,
                });
                continue;
            }
        }

        // 3. Thin lens interaction
        auto lensResult = traceParaxialThinLens(incidentRay, lensModel);
        if (lensResult.status == ThinLensTraceStatus::ClippedByAperture) {
            outSegments.push_back(RaySegment {
                .startMetres = sourcePos,
                .endMetres = targetOnLens,
                .wavelengthMetres = incidentRay.wavelengthMetres,
                .power = incidentRay.power,
                .kind = RaySegmentKind::Clipped,
                .rayIndex = i,
            });
            continue;
        }

        if (lensResult.status != ThinLensTraceStatus::Transmitted || !lensResult.transmittedRay.has_value()) {
            continue;
        }

        // Incident physical segment from source to lens
        outSegments.push_back(RaySegment {
            .startMetres = sourcePos,
            .endMetres = targetOnLens,
            .wavelengthMetres = incidentRay.wavelengthMetres,
            .power = incidentRay.power,
            .kind = RaySegmentKind::Incident,
            .rayIndex = i,
        });

        const Ray& transmitted = *lensResult.transmittedRay;

        // 4. Downstream propagation along +Z after lens
        const bool apertureAfterLens = (apZ > lensZ && !isCoplanarZ(apZ, lensZ));
        const bool screenAfterLens = (screenZ > lensZ && !isCoplanarZ(screenZ, lensZ));
        const bool apertureBeforeScreen = apertureAfterLens && (!screenAfterLens || (apZ < screenZ && !isCoplanarZ(apZ, screenZ)));

        if (apertureBeforeScreen) {
            if (transmitted.direction.z > kReferenceIntersectionEpsilon) {
                const double tAp = (apZ - lensZ) / transmitted.direction.z;
                const math::Vec3d pointOnAperture = targetOnLens + transmitted.direction * tAp;
                const double dx = pointOnAperture.x - apCx;
                const double dy = pointOnAperture.y - apCy;
                if (dx * dx + dy * dy > apRadiusSq) {
                    // Clipped by downstream aperture
                    outSegments.push_back(RaySegment {
                        .startMetres = targetOnLens,
                        .endMetres = pointOnAperture,
                        .wavelengthMetres = transmitted.wavelengthMetres,
                        .power = transmitted.power,
                        .kind = RaySegmentKind::Clipped,
                        .rayIndex = i,
                    });
                    continue;
                }
            }
        }

        // Forward physical segment towards screen or max propagation boundary
        math::Vec3d forwardEnd {};
        if (screenAfterLens && transmitted.direction.z > kReferenceIntersectionEpsilon) {
            const double tScreen = (screenZ - lensZ) / transmitted.direction.z;
            forwardEnd = targetOnLens + transmitted.direction * tScreen;
        } else {
            forwardEnd = targetOnLens + transmitted.direction * options.maxPropagationDistanceMetres;
        }

        outSegments.push_back(RaySegment {
            .startMetres = targetOnLens,
            .endMetres = forwardEnd,
            .wavelengthMetres = transmitted.wavelengthMetres,
            .power = transmitted.power,
            .kind = RaySegmentKind::Transmitted,
            .rayIndex = i,
        });

        // 5. Virtual apparent extension backwards to virtual image plane (for virtual images)
        if (options.includeVirtualExtensions && prediction.nature == scene::ImageNature::Virtual) {
            if (std::abs(transmitted.direction.z) > kReferenceIntersectionEpsilon) {
                const double virtualPlaneZ = prediction.imagePlaneZMetres;
                const double signedDistToPlane = (virtualPlaneZ - lensZ) / transmitted.direction.z;
                const double distToPlane = std::abs(signedDistToPlane);
                const double extensionDist = std::min(distToPlane, options.virtualExtensionDistanceMetres);
                const math::Vec3d virtualPoint = (distToPlane > 0.0)
                    ? (targetOnLens + transmitted.direction * (signedDistToPlane * (extensionDist / distToPlane)))
                    : targetOnLens;
                outSegments.push_back(RaySegment {
                    .startMetres = targetOnLens,
                    .endMetres = virtualPoint,
                    .wavelengthMetres = transmitted.wavelengthMetres,
                    .power = transmitted.power,
                    .kind = RaySegmentKind::VirtualExtension,
                    .rayIndex = i,
                });
            }
        }
    }
}

std::vector<RaySegment> traceBench(
    const scene::OpticalBenchScene& scene,
    const BenchTracerOptions& options) {
    std::vector<RaySegment> segments;
    traceBench(scene, options, segments);
    return segments;
}

} // namespace holobench::optics::ray
