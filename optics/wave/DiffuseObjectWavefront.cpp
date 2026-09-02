#include "optics/wave/DiffuseObjectWavefront.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <complex>
#include <cstdint>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <vector>

#include "compute/fft/IFftBackend.hpp"
#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "core/field/ComplexField2D.hpp"

namespace holobench::optics::wave {
namespace {

// Six bounded slices retain visible axial structure while keeping RGB plate
// recording interactive on the deterministic CPU reference path.
constexpr std::size_t kMaximumDepthLayers = 6U;
constexpr double kRoughnessCorrelationMetres = 25e-6;

void validatePrimitiveParameters(
    const scene::ObjectWavefrontSourceParameters& parameters) {
    if (!std::isfinite(parameters.widthMetres)
        || !std::isfinite(parameters.heightMetres)
        || !std::isfinite(parameters.depthMetres)
        || parameters.widthMetres <= 0.0
        || parameters.heightMetres <= 0.0
        || parameters.depthMetres <= 0.0
        || !std::isfinite(parameters.primitiveYawRadians)
        || !std::isfinite(parameters.primitivePitchRadians)) {
        throw std::invalid_argument(
            "diffuse object geometry must be positive and finite");
    }
    switch (parameters.geometry) {
    case scene::ObjectSourceGeometry::UniformPlane:
    case scene::ObjectSourceGeometry::Cube:
    case scene::ObjectSourceGeometry::Sphere:
    case scene::ObjectSourceGeometry::Tetrahedron:
        return;
    }
    throw std::invalid_argument("diffuse object geometry is invalid");
}

math::Vec3d primitiveToSourceDirection(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d value) noexcept {
    return frame.xAxisInSource * value.x
        + frame.yAxisInSource * value.y
        + frame.zAxisInSource * value.z;
}

math::Vec3d sourceToPrimitiveDirection(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d value) noexcept {
    return {
        math::dot(value, frame.xAxisInSource),
        math::dot(value, frame.yAxisInSource),
        math::dot(value, frame.zAxisInSource),
    };
}

math::Vec3d primitiveToSourcePoint(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d value) noexcept {
    return frame.centreInSourceMetres
        + primitiveToSourceDirection(frame, value);
}

math::Vec3d sourceToPrimitivePoint(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d value) noexcept {
    return sourceToPrimitiveDirection(
        frame, value - frame.centreInSourceMetres);
}

std::array<math::Vec3d, 4U> tetrahedronVertices(
    const scene::ObjectWavefrontSourceParameters& parameters) noexcept {
    const double hx = 0.5 * parameters.widthMetres;
    const double hy = 0.5 * parameters.heightMetres;
    const double hz = 0.5 * parameters.depthMetres;
    return {{
        {-hx, -hy, -hz},
        {hx, -hy, -hz},
        {0.0, hy, -hz},
        {0.0, 0.0, hz},
    }};
}

DiffuseObjectPrimitiveFrame makePrimitiveFrame(
    const scene::ObjectWavefrontSourceParameters& parameters) {
    const double cy = std::cos(parameters.primitiveYawRadians);
    const double sy = std::sin(parameters.primitiveYawRadians);
    const double cp = std::cos(parameters.primitivePitchRadians);
    const double sp = std::sin(parameters.primitivePitchRadians);
    DiffuseObjectPrimitiveFrame result {
        .centreInSourceMetres = {},
        .xAxisInSource = {cy, 0.0, -sy},
        .yAxisInSource = {sy * sp, cp, cy * sp},
        .zAxisInSource = {sy * cp, -sp, cy * cp},
    };

    const double hx = 0.5 * parameters.widthMetres;
    const double hy = 0.5 * parameters.heightMetres;
    const double hz = 0.5 * parameters.depthMetres;
    double frontExtent = 0.0;
    switch (parameters.geometry) {
    case scene::ObjectSourceGeometry::UniformPlane:
        return result;
    case scene::ObjectSourceGeometry::Cube:
        frontExtent = std::abs(result.xAxisInSource.z) * hx
            + std::abs(result.yAxisInSource.z) * hy
            + std::abs(result.zAxisInSource.z) * hz;
        break;
    case scene::ObjectSourceGeometry::Sphere:
        frontExtent = std::sqrt(
            std::pow(result.xAxisInSource.z * hx, 2.0)
            + std::pow(result.yAxisInSource.z * hy, 2.0)
            + std::pow(result.zAxisInSource.z * hz, 2.0));
        break;
    case scene::ObjectSourceGeometry::Tetrahedron:
        for (const auto& vertex : tetrahedronVertices(parameters)) {
            frontExtent = std::max(
                frontExtent,
                primitiveToSourceDirection(result, vertex).z);
        }
        break;
    }
    result.centreInSourceMetres.z = -frontExtent;
    return result;
}

struct PrimitiveRayHit final {
    double distanceMetres = 0.0;
    math::Vec3d position {};
    math::Vec3d outwardNormal {};
};

std::optional<PrimitiveRayHit> intersectBox(
    math::Vec3d origin,
    math::Vec3d direction,
    math::Vec3d halfExtent) {
    double minimumDistance = -std::numeric_limits<double>::infinity();
    double maximumDistance = std::numeric_limits<double>::infinity();
    math::Vec3d entryNormal {};
    const std::array origins {origin.x, origin.y, origin.z};
    const std::array directions {direction.x, direction.y, direction.z};
    const std::array extents {halfExtent.x, halfExtent.y, halfExtent.z};
    for (std::size_t axis = 0U; axis < 3U; ++axis) {
        if (std::abs(directions[axis]) <= 1e-15) {
            if (origins[axis] < -extents[axis]
                || origins[axis] > extents[axis]) {
                return std::nullopt;
            }
            continue;
        }
        double nearDistance = (-extents[axis] - origins[axis])
            / directions[axis];
        double farDistance = (extents[axis] - origins[axis])
            / directions[axis];
        double nearNormalSign = -1.0;
        if (nearDistance > farDistance) {
            std::swap(nearDistance, farDistance);
            nearNormalSign = 1.0;
        }
        if (nearDistance > minimumDistance) {
            minimumDistance = nearDistance;
            entryNormal = {};
            if (axis == 0U) entryNormal.x = nearNormalSign;
            if (axis == 1U) entryNormal.y = nearNormalSign;
            if (axis == 2U) entryNormal.z = nearNormalSign;
        }
        maximumDistance = std::min(maximumDistance, farDistance);
        if (minimumDistance > maximumDistance) return std::nullopt;
    }
    if (!std::isfinite(minimumDistance) || minimumDistance < 0.0) {
        return std::nullopt;
    }
    return PrimitiveRayHit {
        .distanceMetres = minimumDistance,
        .position = origin + direction * minimumDistance,
        .outwardNormal = entryNormal,
    };
}

std::optional<PrimitiveRayHit> intersectEllipsoid(
    math::Vec3d origin,
    math::Vec3d direction,
    math::Vec3d radius) {
    const math::Vec3d scaledOrigin {
        origin.x / radius.x, origin.y / radius.y, origin.z / radius.z};
    const math::Vec3d scaledDirection {
        direction.x / radius.x,
        direction.y / radius.y,
        direction.z / radius.z};
    const double a = math::dot(scaledDirection, scaledDirection);
    const double b = 2.0 * math::dot(scaledOrigin, scaledDirection);
    const double c = math::dot(scaledOrigin, scaledOrigin) - 1.0;
    const double discriminant = std::fma(b, b, -4.0 * a * c);
    if (!std::isfinite(discriminant) || discriminant < 0.0) {
        return std::nullopt;
    }
    const double root = std::sqrt(std::max(0.0, discriminant));
    const double first = (-b - root) / (2.0 * a);
    const double second = (-b + root) / (2.0 * a);
    const double distance = first >= 0.0 ? first : second;
    if (!std::isfinite(distance) || distance < 0.0) return std::nullopt;
    const math::Vec3d point = origin + direction * distance;
    const math::Vec3d normal = math::normalized({
        point.x / (radius.x * radius.x),
        point.y / (radius.y * radius.y),
        point.z / (radius.z * radius.z),
    });
    return PrimitiveRayHit {
        .distanceMetres = distance,
        .position = point,
        .outwardNormal = normal,
    };
}

std::optional<PrimitiveRayHit> intersectTriangle(
    math::Vec3d origin,
    math::Vec3d direction,
    math::Vec3d a,
    math::Vec3d b,
    math::Vec3d c,
    math::Vec3d primitiveCentroid) {
    const math::Vec3d edge1 = b - a;
    const math::Vec3d edge2 = c - a;
    const math::Vec3d p = math::cross(direction, edge2);
    const double determinant = math::dot(edge1, p);
    if (std::abs(determinant) <= 1e-15) return std::nullopt;
    const double inverse = 1.0 / determinant;
    const math::Vec3d fromA = origin - a;
    const double u = inverse * math::dot(fromA, p);
    if (u < 0.0 || u > 1.0) return std::nullopt;
    const math::Vec3d q = math::cross(fromA, edge1);
    const double v = inverse * math::dot(direction, q);
    if (v < 0.0 || u + v > 1.0) return std::nullopt;
    const double distance = inverse * math::dot(edge2, q);
    if (!std::isfinite(distance) || distance < 0.0) return std::nullopt;
    math::Vec3d normal = math::normalized(math::cross(edge1, edge2));
    if (math::dot(normal, primitiveCentroid - a) > 0.0) normal = -normal;
    return PrimitiveRayHit {
        .distanceMetres = distance,
        .position = origin + direction * distance,
        .outwardNormal = normal,
    };
}

std::optional<PrimitiveRayHit> intersectTetrahedron(
    const scene::ObjectWavefrontSourceParameters& parameters,
    math::Vec3d origin,
    math::Vec3d direction) {
    const auto vertices = tetrahedronVertices(parameters);
    math::Vec3d centroid {};
    for (const auto& vertex : vertices) centroid = centroid + vertex * 0.25;
    constexpr std::array<std::array<std::size_t, 3U>, 4U> faces {{
        {{0U, 2U, 1U}},
        {{0U, 1U, 3U}},
        {{1U, 2U, 3U}},
        {{2U, 0U, 3U}},
    }};
    std::optional<PrimitiveRayHit> nearest;
    for (const auto& face : faces) {
        auto hit = intersectTriangle(
            origin,
            direction,
            vertices[face[0]],
            vertices[face[1]],
            vertices[face[2]],
            centroid);
        if (hit.has_value()
            && (!nearest.has_value()
                || hit->distanceMetres < nearest->distanceMetres)) {
            nearest = hit;
        }
    }
    return nearest;
}

std::uint64_t mix64(std::uint64_t value) noexcept {
    value += 0x9e3779b97f4a7c15ULL;
    value = (value ^ (value >> 30U)) * 0xbf58476d1ce4e5b9ULL;
    value = (value ^ (value >> 27U)) * 0x94d049bb133111ebULL;
    return value ^ (value >> 31U);
}

std::int64_t roughnessCell(double coordinateMetres) noexcept {
    const double cell = std::floor(
        coordinateMetres / kRoughnessCorrelationMetres);
    if (cell <= static_cast<double>(std::numeric_limits<std::int64_t>::min())) {
        return std::numeric_limits<std::int64_t>::min();
    }
    if (cell >= static_cast<double>(std::numeric_limits<std::int64_t>::max())) {
        return std::numeric_limits<std::int64_t>::max();
    }
    return static_cast<std::int64_t>(cell);
}

} // namespace

DiffuseObjectPrimitiveFrame diffuseObjectPrimitiveFrame(
    const scene::ObjectWavefrontSourceParameters& parameters) {
    validatePrimitiveParameters(parameters);
    return makePrimitiveFrame(parameters);
}

math::Vec3d diffuseObjectPrimitiveToSourcePoint(
    const DiffuseObjectPrimitiveFrame& frame,
    math::Vec3d pointInPrimitiveMetres) noexcept {
    return primitiveToSourcePoint(frame, pointInPrimitiveMetres);
}

static std::optional<DiffuseObjectSurfaceSample>
sampleDiffuseObjectSurfaceUnchecked(
    const scene::ObjectWavefrontSourceParameters& parameters,
    double sourceXMetres,
    double sourceYMetres) {
    if (parameters.geometry == scene::ObjectSourceGeometry::UniformPlane) {
        if (std::abs(sourceXMetres) > 0.5 * parameters.widthMetres
            || std::abs(sourceYMetres) > 0.5 * parameters.heightMetres) {
            return std::nullopt;
        }
        return DiffuseObjectSurfaceSample {
            .positionInSourceMetres = {sourceXMetres, sourceYMetres, 0.0},
            .outwardNormalInSource = {0.0, 0.0, 1.0},
            .positionInPrimitiveMetres = {sourceXMetres, sourceYMetres, 0.0},
            .lambertianAmplitude = 1.0,
        };
    }

    const DiffuseObjectPrimitiveFrame frame = makePrimitiveFrame(parameters);
    const double rayOriginZ = 2.0 * (
        parameters.widthMetres + parameters.heightMetres
        + parameters.depthMetres);
    const math::Vec3d sourceOrigin {
        sourceXMetres, sourceYMetres, rayOriginZ};
    const math::Vec3d sourceDirection {0.0, 0.0, -1.0};
    const math::Vec3d primitiveOrigin = sourceToPrimitivePoint(
        frame, sourceOrigin);
    const math::Vec3d primitiveDirection = sourceToPrimitiveDirection(
        frame, sourceDirection);
    std::optional<PrimitiveRayHit> hit;
    switch (parameters.geometry) {
    case scene::ObjectSourceGeometry::UniformPlane:
        break;
    case scene::ObjectSourceGeometry::Cube:
        hit = intersectBox(
            primitiveOrigin,
            primitiveDirection,
            {0.5 * parameters.widthMetres,
                0.5 * parameters.heightMetres,
                0.5 * parameters.depthMetres});
        break;
    case scene::ObjectSourceGeometry::Sphere:
        hit = intersectEllipsoid(
            primitiveOrigin,
            primitiveDirection,
            {0.5 * parameters.widthMetres,
                0.5 * parameters.heightMetres,
                0.5 * parameters.depthMetres});
        break;
    case scene::ObjectSourceGeometry::Tetrahedron:
        hit = intersectTetrahedron(
            parameters, primitiveOrigin, primitiveDirection);
        break;
    }
    if (!hit.has_value()) return std::nullopt;

    const math::Vec3d positionInSource = primitiveToSourcePoint(
        frame, hit->position);
    math::Vec3d normalInSource = math::normalized(
        primitiveToSourceDirection(frame, hit->outwardNormal));
    if (normalInSource.z <= 0.0 || positionInSource.z > 1e-10) {
        return std::nullopt;
    }
    return DiffuseObjectSurfaceSample {
        .positionInSourceMetres = positionInSource,
        .outwardNormalInSource = normalInSource,
        .positionInPrimitiveMetres = hit->position,
        .lambertianAmplitude = std::sqrt(normalInSource.z),
    };
}

std::optional<DiffuseObjectSurfaceSample> sampleDiffuseObjectSurface(
    const scene::ObjectWavefrontSourceParameters& parameters,
    double sourceXMetres,
    double sourceYMetres) {
    validatePrimitiveParameters(parameters);
    if (!std::isfinite(sourceXMetres) || !std::isfinite(sourceYMetres)) {
        throw std::invalid_argument(
            "diffuse object surface coordinates must be finite");
    }
    return sampleDiffuseObjectSurfaceUnchecked(
        parameters, sourceXMetres, sourceYMetres);
}

double diffuseObjectRoughPhaseRadians(
    std::uint64_t seed,
    math::Vec3d positionInPrimitiveMetres) {
    if (!math::isFinite(positionInPrimitiveMetres)) {
        throw std::invalid_argument(
            "diffuse object rough-phase position must be finite");
    }
    std::uint64_t hash = mix64(seed);
    hash ^= mix64(static_cast<std::uint64_t>(
        roughnessCell(positionInPrimitiveMetres.x)));
    hash ^= mix64(static_cast<std::uint64_t>(
        roughnessCell(positionInPrimitiveMetres.y)) + 0x632be59bd9b4e019ULL);
    hash ^= mix64(static_cast<std::uint64_t>(
        roughnessCell(positionInPrimitiveMetres.z)) + 0x8cb92baa3f3d8dd7ULL);
    hash = mix64(hash);
    constexpr double kInverse53Bits = 1.0 / 9007199254740992.0;
    const double unit = static_cast<double>(hash >> 11U) * kInverse53Bits;
    return 2.0 * std::numbers::pi * unit;
}

DiffuseObjectWavefrontDiagnostics
synthesizeDiffuseObjectWavefrontAtReferencePlane(
    field::ComplexField2D& referenceField,
    const scene::ObjectWavefrontSourceParameters& parameters,
    double totalScatteredPowerWatts,
    std::complex<double> sourcePhase,
    compute::fft::IFftBackend& fftBackend) {
    validatePrimitiveParameters(parameters);
    const double sourcePhaseMagnitude = std::abs(sourcePhase);
    if (!std::isfinite(totalScatteredPowerWatts)
        || totalScatteredPowerWatts < 0.0
        || !std::isfinite(sourcePhase.real())
        || !std::isfinite(sourcePhase.imag())
        || !std::isfinite(sourcePhaseMagnitude)
        || std::abs(sourcePhaseMagnitude - 1.0) > 1e-12) {
        throw std::invalid_argument(
            "diffuse object wave power must be non-negative and source phase must be a finite unit phasor");
    }
    referenceField.fill({0.0, 0.0});
    DiffuseObjectWavefrontDiagnostics diagnostics;
    if (totalScatteredPowerWatts == 0.0) return diagnostics;

    struct PendingSample final {
        std::size_t x = 0U;
        std::size_t y = 0U;
        DiffuseObjectSurfaceSample surface;
    };
    std::vector<PendingSample> pending;
    pending.reserve(referenceField.sampleCount());
    double nearestDepth = -std::numeric_limits<double>::infinity();
    double farthestDepth = std::numeric_limits<double>::infinity();
    for (std::size_t y = 0U; y < referenceField.height(); ++y) {
        for (std::size_t x = 0U; x < referenceField.width(); ++x) {
            auto surface = sampleDiffuseObjectSurfaceUnchecked(
                parameters,
                referenceField.xCoordinateMetres(x),
                referenceField.yCoordinateMetres(y));
            if (!surface.has_value() || surface->lambertianAmplitude <= 0.0) {
                continue;
            }
            nearestDepth = std::max(
                nearestDepth, surface->positionInSourceMetres.z);
            farthestDepth = std::min(
                farthestDepth, surface->positionInSourceMetres.z);
            pending.push_back({x, y, *surface});
        }
    }
    if (pending.empty()) {
        throw std::invalid_argument(
            "diffuse object is outside the represented source field window");
    }

    diagnostics.visibleSurfaceSampleCount = pending.size();
    diagnostics.nearestSurfaceDepthMetres = nearestDepth;
    diagnostics.farthestSurfaceDepthMetres = farthestDepth;
    const double depthSpan = nearestDepth - farthestDepth;
    const std::size_t layerCount = depthSpan <= 1e-12
        ? 1U
        : kMaximumDepthLayers;
    std::vector<field::ComplexField2D> layers;
    layers.reserve(layerCount);
    for (std::size_t index = 0U; index < layerCount; ++index) {
        layers.emplace_back(
            referenceField.width(),
            referenceField.height(),
            referenceField.pitchXMetres(),
            referenceField.pitchYMetres(),
            referenceField.vacuumWavelengthMetres(),
            referenceField.refractiveIndex());
    }
    std::vector<double> depthSums(layerCount, 0.0);
    std::vector<std::size_t> sampleCounts(layerCount, 0U);
    const auto layerIndexForDepth = [&](double depth) {
        if (layerCount == 1U) return std::size_t {0U};
        const double normalized = std::clamp(
            (nearestDepth - depth) / depthSpan, 0.0, 1.0);
        return std::min(
            layerCount - 1U,
            static_cast<std::size_t>(normalized
                * static_cast<double>(layerCount)));
    };
    for (const auto& sample : pending) {
        const std::size_t layer = layerIndexForDepth(
            sample.surface.positionInSourceMetres.z);
        depthSums[layer] += sample.surface.positionInSourceMetres.z;
        ++sampleCounts[layer];
    }

    const double wavenumber = referenceField.mediumWavenumberRadiansPerMetre();
    for (const auto& sample : pending) {
        const std::size_t layer = layerIndexForDepth(
            sample.surface.positionInSourceMetres.z);
        const double layerDepth = depthSums[layer]
            / static_cast<double>(sampleCounts[layer]);
        const double residualDistance = layerDepth
            - sample.surface.positionInSourceMetres.z;
        const double phase = diffuseObjectRoughPhaseRadians(
                parameters.roughnessSeed,
                sample.surface.positionInPrimitiveMetres)
            + wavenumber * residualDistance;
        layers[layer].at(sample.x, sample.y)
            = sample.surface.lambertianAmplitude
            * std::polar(1.0, std::remainder(
                phase, 2.0 * std::numbers::pi));
    }

    compute::propagation::AngularSpectrumPropagator propagator(fftBackend);
    for (std::size_t layer = 0U; layer < layerCount; ++layer) {
        if (sampleCounts[layer] == 0U) continue;
        const double layerDepth = depthSums[layer]
            / static_cast<double>(sampleCounts[layer]);
        if (layerDepth < 0.0) {
            static_cast<void>(propagator.propagateInPlace(
                layers[layer], -layerDepth));
        }
        for (std::size_t index = 0U;
             index < referenceField.sampleCount(); ++index) {
            referenceField.samples()[index] += layers[layer].samples()[index];
        }
        ++diagnostics.populatedDepthLayerCount;
    }

    double representedPower = 0.0;
    for (const auto& value : referenceField.samples()) {
        representedPower += std::norm(value);
    }
    representedPower *= referenceField.pitchXMetres()
        * referenceField.pitchYMetres();
    if (!std::isfinite(representedPower) || representedPower <= 0.0) {
        throw std::overflow_error(
            "diffuse object wave has no finite represented power");
    }
    const double amplitudeScale = std::sqrt(
        totalScatteredPowerWatts / representedPower);
    for (auto& value : referenceField.samples()) {
        value *= amplitudeScale * sourcePhase;
    }
    diagnostics.normalizedPowerWatts = totalScatteredPowerWatts;
    return diagnostics;
}

} // namespace holobench::optics::wave
