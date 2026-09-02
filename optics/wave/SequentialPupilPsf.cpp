#include "optics/wave/SequentialPupilPsf.hpp"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace holobench::optics::wave {
namespace {

constexpr double kDirectionEpsilon
    = 64.0 * std::numeric_limits<double>::epsilon();

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

double opticalPathToPoint(
    const SequentialPupilRaySample& sample,
    math::Vec3d point) {
    const double distance = math::length(point - sample.exitRay.originMetres);
    if (!std::isfinite(distance) || distance <= 0.0) {
        throw std::invalid_argument(
            "coherent pupil sensor point must be separated from every exit ray");
    }
    const double result = sample.opticalPathToExitMetres + distance;
    if (!std::isfinite(result)) {
        throw std::overflow_error(
            "coherent pupil optical path is not representable");
    }
    return result;
}

} // namespace

SequentialPupilWavefront traceSequentialPupilWavefront(
    std::span<const ray::Ray> incidentWorldRays,
    const ray::SequentialLensPrescription& prescription,
    const math::RigidTransform3d& imagePlaneLocalToWorld,
    double referenceImageXMetres,
    double referenceImageYMetres,
    const ray::SurfaceIntersectionOptions& intersectionOptions,
    std::span<const double> incidentOpticalPathOffsetsMetres) {
    if (incidentWorldRays.empty()) {
        throw std::invalid_argument(
            "sequential pupil wavefront requires at least one incident ray");
    }
    ray::validateSequentialLensPrescription(prescription);
    math::validateRigidTransform(imagePlaneLocalToWorld);
    ray::validateSurfaceIntersectionOptions(intersectionOptions);
    requireFinite(referenceImageXMetres, "pupil reference image x");
    requireFinite(referenceImageYMetres, "pupil reference image y");
    if (!incidentOpticalPathOffsetsMetres.empty()
        && incidentOpticalPathOffsetsMetres.size()
            != incidentWorldRays.size()) {
        throw std::invalid_argument(
            "incident pupil optical-path offsets must be empty or match the ray count");
    }

    const double wavelength = incidentWorldRays.front().wavelengthMetres;
    SequentialPupilWavefront result;
    result.inputRayCount = incidentWorldRays.size();
    result.samples.reserve(incidentWorldRays.size());
    for (std::size_t index = 0; index < incidentWorldRays.size(); ++index) {
        const auto& incident = incidentWorldRays[index];
        if (incident.wavelengthMetres != wavelength) {
            throw std::invalid_argument(
                "one coherent pupil wavefront requires one exact wavelength");
        }
        const double incidentOpticalPathOffset
            = incidentOpticalPathOffsetsMetres.empty()
            ? 0.0
            : incidentOpticalPathOffsetsMetres[index];
        requireFinite(
            incidentOpticalPathOffset,
            "incident pupil optical-path offset");
        const auto trace = ray::traceSequentialLens(
            incident, prescription, intersectionOptions);
        if (trace.status != ray::SequentialTraceStatus::Completed
            || !trace.finalRay.has_value()) {
            ++result.rejectedRayCount;
            continue;
        }
        const auto localOrigin = math::transformPointWorldToLocal(
            imagePlaneLocalToWorld, trace.finalRay->originMetres);
        const auto localDirection = math::transformDirectionWorldToLocal(
            imagePlaneLocalToWorld, trace.finalRay->direction);
        if (std::abs(localDirection.z) <= kDirectionEpsilon) {
            ++result.rejectedRayCount;
            continue;
        }
        const double distance = -localOrigin.z / localDirection.z;
        if (!std::isfinite(distance)
            || distance <= intersectionOptions.intersectionEpsilonMetres) {
            ++result.rejectedRayCount;
            continue;
        }
        const auto imagePoint = localOrigin + localDirection * distance;
        const double opticalPathToExit
            = trace.totalOpticalPathMetres + incidentOpticalPathOffset;
        if (!std::isfinite(opticalPathToExit) || opticalPathToExit < 0.0) {
            throw std::invalid_argument(
                "incident pupil phase offset produces an invalid optical path");
        }
        result.samples.push_back({
            .sourceRayIndex = index,
            .exitRay = *trace.finalRay,
            .opticalPathToExitMetres = opticalPathToExit,
            .imageXMetres = imagePoint.x,
            .imageYMetres = imagePoint.y,
        });
        result.acceptedPower += trace.finalRay->power;
        if (!std::isfinite(result.acceptedPower)) {
            throw std::overflow_error(
                "sequential pupil accepted power is not representable");
        }
    }
    if (result.samples.empty() || result.acceptedPower <= 0.0) {
        return result;
    }

    for (const auto& sample : result.samples) {
        result.geometricCentroidXMetres += sample.imageXMetres;
        result.geometricCentroidYMetres += sample.imageYMetres;
    }
    const double count = static_cast<double>(result.samples.size());
    result.geometricCentroidXMetres /= count;
    result.geometricCentroidYMetres /= count;
    double squaredRadiusSum = 0.0;
    for (const auto& sample : result.samples) {
        const double radius = std::hypot(
            sample.imageXMetres - result.geometricCentroidXMetres,
            sample.imageYMetres - result.geometricCentroidYMetres);
        squaredRadiusSum = std::fma(radius, radius, squaredRadiusSum);
        result.geometricRadiusMetres = std::max(
            result.geometricRadiusMetres, radius);
    }
    result.geometricRmsRadiusMetres = std::sqrt(squaredRadiusSum / count);

    const auto referencePoint = math::transformPointLocalToWorld(
        imagePlaneLocalToWorld,
        {referenceImageXMetres, referenceImageYMetres, 0.0});
    std::vector<double> referencePaths;
    referencePaths.reserve(result.samples.size());
    double weightedPathSum = 0.0;
    for (const auto& sample : result.samples) {
        const double path = opticalPathToPoint(sample, referencePoint);
        referencePaths.push_back(path);
        weightedPathSum = std::fma(sample.exitRay.power, path, weightedPathSum);
    }
    result.referenceOpticalPathMetres
        = weightedPathSum / result.acceptedPower;
    double weightedSquaredDifference = 0.0;
    double minimumDifference = std::numeric_limits<double>::infinity();
    double maximumDifference = -std::numeric_limits<double>::infinity();
    for (std::size_t index = 0; index < result.samples.size(); ++index) {
        const double difference
            = referencePaths[index] - result.referenceOpticalPathMetres;
        weightedSquaredDifference = std::fma(
            result.samples[index].exitRay.power,
            difference * difference,
            weightedSquaredDifference);
        minimumDifference = std::min(minimumDifference, difference);
        maximumDifference = std::max(maximumDifference, difference);
    }
    result.rmsOpticalPathDifferenceMetres = std::sqrt(
        weightedSquaredDifference / result.acceptedPower);
    result.peakToValleyOpticalPathDifferenceMetres
        = maximumDifference - minimumDifference;
    return result;
}

CoherentPupilEvaluation evaluateCoherentPupil(
    const SequentialPupilWavefront& wavefront,
    std::span<const math::Vec3d> sensorWorldPoints) {
    if (wavefront.samples.empty() || wavefront.acceptedPower <= 0.0) {
        throw std::invalid_argument(
            "coherent pupil evaluation requires accepted traced rays");
    }
    if (sensorWorldPoints.size()
        > std::numeric_limits<std::size_t>::max()
            / wavefront.samples.size()) {
        throw std::overflow_error(
            "coherent pupil evaluation term count overflows");
    }
    CoherentPupilEvaluation result;
    result.relativeIntensities.reserve(sensorWorldPoints.size());
    result.complexTermCount
        = sensorWorldPoints.size() * wavefront.samples.size();
    const double wavelength
        = wavefront.samples.front().exitRay.wavelengthMetres;
    for (const auto& sample : wavefront.samples) {
        if (sample.exitRay.wavelengthMetres != wavelength
            || !std::isfinite(sample.exitRay.power)
            || sample.exitRay.power <= 0.0
            || !std::isfinite(sample.opticalPathToExitMetres)
            || sample.opticalPathToExitMetres < 0.0) {
            throw std::invalid_argument(
                "coherent pupil sample is invalid or spectrally mixed");
        }
    }
    for (const auto point : sensorWorldPoints) {
        if (!math::isFinite(point)) {
            throw std::invalid_argument(
                "coherent pupil sensor point must be finite");
        }
        std::complex<long double> field;
        for (const auto& sample : wavefront.samples) {
            const double distance = math::length(
                point - sample.exitRay.originMetres);
            if (!std::isfinite(distance) || distance <= 0.0) {
                throw std::invalid_argument(
                    "coherent pupil sensor point must be separated from every exit ray");
            }
            const double path = sample.opticalPathToExitMetres + distance;
            const double pathDifference
                = path - wavefront.referenceOpticalPathMetres;
            const double phase = 2.0 * std::numbers::pi
                * std::remainder(pathDifference, wavelength) / wavelength;
            const long double amplitude
                = std::sqrt(static_cast<long double>(sample.exitRay.power))
                / static_cast<long double>(distance);
            field += amplitude * std::complex<long double>(
                std::cos(static_cast<long double>(phase)),
                std::sin(static_cast<long double>(phase)));
        }
        const long double intensity = std::norm(field);
        if (!std::isfinite(intensity)
            || intensity > std::numeric_limits<double>::max()) {
            throw std::overflow_error(
                "coherent pupil intensity is not representable");
        }
        const double value = static_cast<double>(intensity);
        result.relativeIntensities.push_back(value);
        result.intensitySum += value;
        if (!std::isfinite(result.intensitySum)) {
            throw std::overflow_error(
                "coherent pupil intensity sum is not representable");
        }
    }
    return result;
}

} // namespace holobench::optics::wave
