#include "optics/analysis/SpotDiagram.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holobench::optics::analysis {

namespace {

[[nodiscard]] SpotStatistics computeStatistics(
    const std::vector<SpotSample>& samples,
    const std::vector<std::size_t>& indices) {
    if (indices.empty()) {
        return {};
    }
    long double sumX = 0.0L;
    long double sumY = 0.0L;
    long double totalPower = 0.0L;
    for (const std::size_t index : indices) {
        sumX += samples[index].imageXMetres;
        sumY += samples[index].imageYMetres;
        totalPower += samples[index].power;
    }
    const long double count = static_cast<long double>(indices.size());
    const double centroidX = static_cast<double>(sumX / count);
    const double centroidY = static_cast<double>(sumY / count);
    long double squaredRadiusSum = 0.0L;
    double geometricRadius = 0.0;
    for (const std::size_t index : indices) {
        const double dx = samples[index].imageXMetres - centroidX;
        const double dy = samples[index].imageYMetres - centroidY;
        const double radius = std::hypot(dx, dy);
        squaredRadiusSum += static_cast<long double>(radius) * radius;
        geometricRadius = std::max(geometricRadius, radius);
    }
    const double rms = std::sqrt(static_cast<double>(squaredRadiusSum / count));
    if (!std::isfinite(centroidX) || !std::isfinite(centroidY)
        || !std::isfinite(rms) || !std::isfinite(geometricRadius)
        || !std::isfinite(totalPower)) {
        throw std::overflow_error("spot statistics are not finite");
    }
    return {
        .centroidXMetres = centroidX,
        .centroidYMetres = centroidY,
        .rmsRadiusMetres = rms,
        .geometricRadiusMetres = geometricRadius,
        .totalPower = static_cast<double>(totalPower),
    };
}

} // namespace

SpotDiagramResult computeSpotDiagram(
    const std::vector<ray::Ray>& incidentWorldRays,
    const ray::SequentialLensPrescription& prescription,
    const math::RigidTransform3d& imagePlaneLocalToWorld,
    const ray::SurfaceIntersectionOptions& intersectionOptions,
    std::optional<std::size_t> chiefRayIndex) {
    if (incidentWorldRays.empty()) {
        throw std::invalid_argument("spot diagram requires at least one incident ray");
    }
    if (chiefRayIndex.has_value() && *chiefRayIndex >= incidentWorldRays.size()) {
        throw std::out_of_range("chief ray index is outside the incident bundle");
    }
    ray::validateSequentialLensPrescription(prescription);
    math::validateRigidTransform(imagePlaneLocalToWorld);
    ray::validateSurfaceIntersectionOptions(intersectionOptions);

    SpotDiagramResult result;
    result.samples.reserve(incidentWorldRays.size());
    for (std::size_t index = 0; index < incidentWorldRays.size(); ++index) {
        const ray::SequentialTraceResult trace = ray::traceSequentialLens(
            incidentWorldRays[index], prescription, intersectionOptions);
        if (trace.status != ray::SequentialTraceStatus::Completed || !trace.finalRay.has_value()) {
            result.rejectedRays.push_back({
                .sourceRayIndex = index,
                .reason = SpotRayRejectionReason::PrescriptionTraceFailed,
                .traceStatus = trace.status,
            });
            continue;
        }
        const math::Vec3d localOrigin = math::transformPointWorldToLocal(
            imagePlaneLocalToWorld, trace.finalRay->originMetres);
        const math::Vec3d localDirection = math::transformDirectionWorldToLocal(
            imagePlaneLocalToWorld, trace.finalRay->direction);
        if (std::abs(localDirection.z) <= 64.0 * std::numeric_limits<double>::epsilon()) {
            result.rejectedRays.push_back({index, SpotRayRejectionReason::ImagePlaneMiss, std::nullopt});
            continue;
        }
        const double distance = -localOrigin.z / localDirection.z;
        if (!std::isfinite(distance) || distance <= intersectionOptions.intersectionEpsilonMetres) {
            result.rejectedRays.push_back({index, SpotRayRejectionReason::ImagePlaneMiss, std::nullopt});
            continue;
        }
        const math::Vec3d imagePoint = localOrigin + localDirection * distance;
        result.samples.push_back({
            .sourceRayIndex = index,
            .imageXMetres = imagePoint.x,
            .imageYMetres = imagePoint.y,
            .chiefRelativeXMetres = std::nullopt,
            .chiefRelativeYMetres = std::nullopt,
            .vacuumWavelengthMetres = trace.finalRay->wavelengthMetres,
            .power = trace.finalRay->power,
        });
    }

    std::vector<std::size_t> allIndices(result.samples.size());
    for (std::size_t index = 0; index < allIndices.size(); ++index) {
        allIndices[index] = index;
    }
    result.statistics = computeStatistics(result.samples, allIndices);

    if (chiefRayIndex.has_value()) {
        const auto chief = std::find_if(result.samples.begin(), result.samples.end(), [&](const SpotSample& sample) {
            return sample.sourceRayIndex == *chiefRayIndex;
        });
        if (chief != result.samples.end()) {
            result.chiefImageXMetres = chief->imageXMetres;
            result.chiefImageYMetres = chief->imageYMetres;
            for (SpotSample& sample : result.samples) {
                sample.chiefRelativeXMetres = sample.imageXMetres - *result.chiefImageXMetres;
                sample.chiefRelativeYMetres = sample.imageYMetres - *result.chiefImageYMetres;
            }
        }
    }

    for (std::size_t sampleIndex = 0; sampleIndex < result.samples.size(); ++sampleIndex) {
        const double wavelength = result.samples[sampleIndex].vacuumWavelengthMetres;
        auto group = std::find_if(result.wavelengthGroups.begin(), result.wavelengthGroups.end(), [&](const WavelengthSpotGroup& value) {
            return value.vacuumWavelengthMetres == wavelength;
        });
        if (group == result.wavelengthGroups.end()) {
            result.wavelengthGroups.push_back({.vacuumWavelengthMetres = wavelength, .sampleIndices = {}, .statistics = {}});
            group = std::prev(result.wavelengthGroups.end());
        }
        group->sampleIndices.push_back(sampleIndex);
    }
    for (WavelengthSpotGroup& group : result.wavelengthGroups) {
        group.statistics = computeStatistics(result.samples, group.sampleIndices);
    }
    return result;
}

} // namespace holobench::optics::analysis
