#include "app/RealLensWorkbenchPipeline.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <unordered_set>
#include <utility>

#include "optics/ray/LensPrescriptionCatalog.hpp"

namespace holobench::app::reallens {
namespace {

constexpr std::size_t kMaximumPupilSampleCount = 1'000'000;
constexpr std::size_t kMaximumWorkbenchRayCount = 2'000'000;

[[nodiscard]] std::size_t pupilSampleCount(const RealLensWorkbenchConfig& config) {
    if (config.pupilRingCount > 128 || config.pupilSamplesPerFirstRing > 4096) {
        throw std::invalid_argument("real-lens pupil sampling exceeds the supported safety limit");
    }
    const std::size_t triangular = config.pupilRingCount * (config.pupilRingCount + 1) / 2;
    if (triangular > (std::numeric_limits<std::size_t>::max() - 1)
            / config.pupilSamplesPerFirstRing) {
        throw std::overflow_error("real-lens pupil sample count overflows size_t");
    }
    const std::size_t result = 1 + triangular * config.pupilSamplesPerFirstRing;
    if (result > kMaximumPupilSampleCount) {
        throw std::invalid_argument("real-lens pupil sample count exceeds the supported safety limit");
    }
    return result;
}

[[nodiscard]] std::vector<optics::ray::Ray> makeFieldBaseRays(
    const RealLensWorkbenchConfig& config,
    const FieldDefinition& field) {
    const std::size_t sampleCount = pupilSampleCount(config);
    std::vector<optics::ray::Ray> rays;
    rays.reserve(sampleCount);
    const math::Vec3d localDirection = math::normalized({
        std::tan(field.angleXRadians),
        std::tan(field.angleYRadians),
        1.0,
    });
    const auto& firstSurfaceFrame = config.prescription.surfaces.front().localToWorld;
    const double rayPower = field.powerFraction / static_cast<double>(sampleCount);
    const auto appendRay = [&](double pupilX, double pupilY) {
        const math::Vec3d localOrigin {
            pupilX - localDirection.x / localDirection.z * config.objectSpaceDistanceMetres,
            pupilY - localDirection.y / localDirection.z * config.objectSpaceDistanceMetres,
            -config.objectSpaceDistanceMetres,
        };
        rays.push_back(optics::ray::makeRay(
            math::transformPointLocalToWorld(firstSurfaceFrame, localOrigin),
            math::transformDirectionLocalToWorld(firstSurfaceFrame, localDirection),
            532e-9,
            rayPower));
    };
    appendRay(0.0, 0.0);
    for (std::size_t ring = 1; ring <= config.pupilRingCount; ++ring) {
        const std::size_t ringSamples = config.pupilSamplesPerFirstRing * ring;
        const double radius = config.entrancePupilSemiDiameterMetres
            * static_cast<double>(ring) / static_cast<double>(config.pupilRingCount);
        for (std::size_t index = 0; index < ringSamples; ++index) {
            const double angle = 2.0 * std::numbers::pi
                * static_cast<double>(index) / static_cast<double>(ringSamples);
            appendRay(radius * std::cos(angle), radius * std::sin(angle));
        }
    }
    return rays;
}

void appendImagePlanePoint(
    const optics::ray::Ray& ray,
    const math::RigidTransform3d& imagePlane,
    double epsilonMetres,
    std::vector<math::Vec3d>& points) {
    const math::Vec3d localOrigin = math::transformPointWorldToLocal(
        imagePlane, ray.originMetres);
    const math::Vec3d localDirection = math::transformDirectionWorldToLocal(
        imagePlane, ray.direction);
    if (std::abs(localDirection.z) <= 64.0 * std::numeric_limits<double>::epsilon()) {
        return;
    }
    const double distance = -localOrigin.z / localDirection.z;
    if (!std::isfinite(distance) || distance <= epsilonMetres) {
        return;
    }
    points.push_back(math::transformPointLocalToWorld(
        imagePlane, localOrigin + localDirection * distance));
}

} // namespace

RealLensWorkbenchConfig makeDefaultRealLensWorkbenchConfig() {
    auto options = optics::ray::SurfaceIntersectionOptions {};
    options.maximumDistanceMetres = 0.30;
    return {
        .prescription = optics::ray::makeDefaultNBk7BiconvexPrescription(),
        .fields = {
            {.id = "on_axis", .angleXRadians = 0.0, .angleYRadians = 0.0, .powerFraction = 1.0 / 3.0},
            {.id = "field_x_3deg", .angleXRadians = std::numbers::pi / 60.0, .angleYRadians = 0.0, .powerFraction = 1.0 / 3.0},
            {.id = "field_y_3deg", .angleXRadians = 0.0, .angleYRadians = std::numbers::pi / 60.0, .powerFraction = 1.0 / 3.0},
        },
        .chromaticReferenceFieldId = "on_axis",
        .spectrum = optics::analysis::makeFraunhoferFdcSpectrum(),
        .entrancePupilSemiDiameterMetres = 0.008,
        .objectSpaceDistanceMetres = 0.02,
        .pupilRingCount = 4,
        .pupilSamplesPerFirstRing = 8,
        .imagePlaneLocalToWorld = {.translationMetres = {0.0, 0.0, 0.055}},
        .minimumFocusPlaneZMetres = -0.03,
        .maximumFocusPlaneZMetres = 0.10,
        .intersectionOptions = options,
    };
}

void validateRealLensWorkbenchConfig(const RealLensWorkbenchConfig& config) {
    optics::ray::validateSequentialLensPrescription(config.prescription);
    math::validateRigidTransform(config.imagePlaneLocalToWorld);
    optics::ray::validateSurfaceIntersectionOptions(config.intersectionOptions);
    if (config.fields.empty() || config.spectrum.empty()) {
        throw std::invalid_argument("real-lens workbench requires fields and spectral lines");
    }
    if (!std::isfinite(config.entrancePupilSemiDiameterMetres)
        || config.entrancePupilSemiDiameterMetres <= 0.0
        || config.entrancePupilSemiDiameterMetres
            > config.prescription.surfaces.front().geometry.clearSemiDiameterMetres) {
        throw std::invalid_argument("entrance pupil must be finite, positive, and no larger than the first surface aperture");
    }
    if (!std::isfinite(config.objectSpaceDistanceMetres)
        || config.objectSpaceDistanceMetres <= config.intersectionOptions.intersectionEpsilonMetres) {
        throw std::invalid_argument("object-space launch distance must be finite and forward of the first surface");
    }
    if (config.pupilRingCount == 0 || config.pupilSamplesPerFirstRing < 3) {
        throw std::invalid_argument("pupil sampling requires at least one ring and three samples per first ring");
    }
    static_cast<void>(pupilSampleCount(config));
    if (!std::isfinite(config.minimumFocusPlaneZMetres)
        || !std::isfinite(config.maximumFocusPlaneZMetres)
        || config.maximumFocusPlaneZMetres < config.minimumFocusPlaneZMetres) {
        throw std::invalid_argument("focus bounds must be finite and ordered in the image-plane frame");
    }

    long double fieldPower = 0.0L;
    std::unordered_set<std::string> fieldIds;
    bool haveChromaticReference = false;
    constexpr double kMaximumFieldAngle = 80.0 * std::numbers::pi / 180.0;
    for (const FieldDefinition& field : config.fields) {
        if (field.id.empty() || !fieldIds.insert(field.id).second) {
            throw std::invalid_argument("real-lens field ids must be non-empty and unique");
        }
        if (!std::isfinite(field.angleXRadians) || !std::isfinite(field.angleYRadians)
            || std::abs(field.angleXRadians) >= kMaximumFieldAngle
            || std::abs(field.angleYRadians) >= kMaximumFieldAngle) {
            throw std::invalid_argument("real-lens field angles must be finite and have magnitude below 80 degrees");
        }
        if (!std::isfinite(field.powerFraction) || field.powerFraction < 0.0) {
            throw std::invalid_argument("real-lens field powers must be finite and non-negative");
        }
        fieldPower += field.powerFraction;
        haveChromaticReference = haveChromaticReference
            || field.id == config.chromaticReferenceFieldId;
    }
    if (std::abs(fieldPower - 1.0L) > 2e-15L) {
        throw std::invalid_argument("real-lens field power fractions must sum to one");
    }
    if (!haveChromaticReference) {
        throw std::invalid_argument("chromatic reference field id must identify a configured field");
    }

    const std::vector<optics::ray::Ray> probe {
        optics::ray::makeRay({0, 0, 0}, {0, 0, 1}),
    };
    static_cast<void>(optics::analysis::expandRayBundleSpectrum(probe, config.spectrum));
}

RealLensWorkbenchResult runRealLensWorkbench(const RealLensWorkbenchConfig& config) {
    validateRealLensWorkbenchConfig(config);
    RealLensWorkbenchResult result;
    const std::size_t pupilSamples = pupilSampleCount(config);
    if (config.spectrum.size() > std::numeric_limits<std::size_t>::max() / pupilSamples) {
        throw std::overflow_error("real-lens spectral ray count overflows size_t");
    }
    const std::size_t raysPerField = pupilSamples * config.spectrum.size();
    if (config.fields.size() > std::numeric_limits<std::size_t>::max() / raysPerField) {
        throw std::overflow_error("real-lens spectral field ray count overflows size_t");
    }
    const std::size_t totalRayCount = config.fields.size() * raysPerField;
    if (totalRayCount > kMaximumWorkbenchRayCount) {
        throw std::invalid_argument("real-lens workbench ray count exceeds the supported safety limit");
    }
    result.incidentRays.reserve(totalRayCount);
    std::vector<optics::ray::Ray> chromaticReferenceRays;
    chromaticReferenceRays.reserve(raysPerField);
    for (const FieldDefinition& field : config.fields) {
        const auto spectralRays = optics::analysis::expandRayBundleSpectrum(
            makeFieldBaseRays(config, field), config.spectrum);
        for (const optics::ray::Ray& ray : spectralRays) {
            result.incidentRays.push_back({.ray = ray, .fieldId = field.id});
            if (field.id == config.chromaticReferenceFieldId) {
                chromaticReferenceRays.push_back(ray);
            }
        }
    }

    result.spotDiagram = optics::analysis::computeSpotDiagram(
        result.incidentRays,
        config.prescription,
        config.imagePlaneLocalToWorld,
        config.intersectionOptions);
    result.chromaticFocus = optics::analysis::analyzeLongitudinalChromaticFocus(
        chromaticReferenceRays,
        config.prescription,
        config.imagePlaneLocalToWorld,
        config.intersectionOptions,
        config.minimumFocusPlaneZMetres,
        config.maximumFocusPlaneZMetres);

    result.tracePolylines.reserve(result.incidentRays.size());
    for (const optics::analysis::FieldTaggedRay& incident : result.incidentRays) {
        const auto trace = optics::ray::traceSequentialLens(
            incident.ray, config.prescription, config.intersectionOptions);
        TracePolyline polyline {
            .fieldId = incident.fieldId,
            .vacuumWavelengthMetres = incident.ray.wavelengthMetres,
            .status = trace.status,
            .worldPointsMetres = {incident.ray.originMetres},
        };
        for (const optics::ray::SequentialSurfaceRecord& record : trace.records) {
            if (record.intersectionStatus == optics::ray::SurfaceIntersectionStatus::Hit) {
                polyline.worldPointsMetres.push_back(record.worldPointMetres);
            }
        }
        if (trace.status == optics::ray::SequentialTraceStatus::Completed
            && trace.finalRay.has_value()) {
            appendImagePlanePoint(
                *trace.finalRay,
                config.imagePlaneLocalToWorld,
                config.intersectionOptions.intersectionEpsilonMetres,
                polyline.worldPointsMetres);
        }
        result.tracePolylines.push_back(std::move(polyline));
    }
    return result;
}

} // namespace holobench::app::reallens
