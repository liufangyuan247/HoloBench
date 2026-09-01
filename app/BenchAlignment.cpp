#include "app/BenchAlignment.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <stdexcept>

namespace holobench::app::alignment {
namespace {

[[nodiscard]] math::RigidTransform3d frameAimedAlong(
    const math::RigidTransform3d& selected,
    math::Vec3d direction) {
    math::validateRigidTransform(selected);
    if (!math::isFinite(direction) || math::length(direction) <= 1e-12) {
        throw std::invalid_argument(
            "alignment target must define a finite nonzero direction");
    }
    const math::Vec3d zAxis = math::normalized(direction);
    math::Vec3d xCandidate = selected.localXAxisInWorld
        - zAxis * math::dot(selected.localXAxisInWorld, zAxis);
    if (math::length(xCandidate) <= 1e-10) {
        const math::Vec3d fallback
            = std::abs(zAxis.y) < 0.9
            ? math::Vec3d {0.0, 1.0, 0.0}
            : math::Vec3d {1.0, 0.0, 0.0};
        xCandidate = math::cross(fallback, zAxis);
    }
    const math::Vec3d xAxis = math::normalized(xCandidate);
    const math::Vec3d yAxis = math::cross(zAxis, xAxis);
    math::RigidTransform3d result {
        .translationMetres = selected.translationMetres,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = yAxis,
        .localZAxisInWorld = zAxis,
    };
    math::validateRigidTransform(result);
    return result;
}

} // namespace

math::RigidTransform3d aimAt(
    const math::RigidTransform3d& selected,
    math::Vec3d targetPointMetres) {
    if (!math::isFinite(targetPointMetres)) {
        throw std::invalid_argument(
            "alignment target point must be finite");
    }
    return frameAimedAlong(
        selected, targetPointMetres - selected.translationMetres);
}

math::RigidTransform3d makeCoaxialWith(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target) {
    math::validateRigidTransform(selected);
    math::validateRigidTransform(target);
    const math::Vec3d targetToSelected
        = selected.translationMetres - target.translationMetres;
    const double axialDistance
        = math::dot(targetToSelected, target.localZAxisInWorld);
    math::RigidTransform3d result = target;
    result.translationMetres = target.translationMetres
        + target.localZAxisInWorld * axialDistance;
    math::validateRigidTransform(result);
    return result;
}

math::RigidTransform3d matchHeight(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target) {
    math::validateRigidTransform(selected);
    math::validateRigidTransform(target);
    auto result = selected;
    result.translationMetres.y = target.translationMetres.y;
    math::validateRigidTransform(result);
    return result;
}

math::RigidTransform3d placeAlongTargetAxis(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target,
    double signedDistanceMetres) {
    math::validateRigidTransform(selected);
    math::validateRigidTransform(target);
    if (!std::isfinite(signedDistanceMetres)) {
        throw std::invalid_argument(
            "alignment spacing must be finite");
    }
    math::RigidTransform3d result = target;
    result.translationMetres = target.translationMetres
        + target.localZAxisInWorld * signedDistanceMetres;
    math::validateRigidTransform(result);
    return result;
}

BeamSnapResult snapToNearestBeam(
    const math::RigidTransform3d& selected,
    std::span<const optics::scene::BenchTraceSegment> segments,
    double maximumDistanceMetres) {
    math::validateRigidTransform(selected);
    if (!std::isfinite(maximumDistanceMetres)
        || maximumDistanceMetres <= 0.0) {
        throw std::invalid_argument(
            "beam snap distance must be positive and finite");
    }
    double nearestDistance = std::numeric_limits<double>::infinity();
    math::Vec3d nearestPoint;
    math::Vec3d nearestDirection;
    std::uint64_t nearestBranchId = 0U;
    bool nearestIsAmbiguous = false;
    for (const auto& segment : segments) {
        const math::Vec3d delta = segment.endMetres - segment.startMetres;
        const double lengthSquared = math::dot(delta, delta);
        if (!math::isFinite(segment.startMetres)
            || !math::isFinite(segment.endMetres)
            || !std::isfinite(lengthSquared) || lengthSquared <= 1e-24) {
            continue;
        }
        const double parameter = std::clamp(
            math::dot(
                selected.translationMetres - segment.startMetres, delta)
                / lengthSquared,
            0.0,
            1.0);
        const math::Vec3d point = segment.startMetres + delta * parameter;
        const double distance
            = math::length(selected.translationMetres - point);
        const double comparisonDistance = std::isfinite(nearestDistance)
            ? nearestDistance : distance;
        const double tieTolerance = 1e-12 * std::max({
            1.0, distance, comparisonDistance});
        if (!std::isfinite(nearestDistance)
            || distance + tieTolerance < nearestDistance) {
            nearestDistance = distance;
            nearestPoint = point;
            nearestDirection = delta;
            nearestBranchId = segment.branchId;
            nearestIsAmbiguous = false;
        } else if (std::abs(distance - nearestDistance) <= tieTolerance
            && segment.branchId != nearestBranchId) {
            nearestIsAmbiguous = true;
        }
    }
    if (!std::isfinite(nearestDistance)
        || nearestDistance > maximumDistanceMetres) {
        throw std::invalid_argument(
            "no visible beam segment is within the snap distance");
    }
    if (nearestIsAmbiguous) {
        throw std::invalid_argument(
            "multiple visible beam branches are equally near; move closer to the intended segment");
    }
    auto transform = frameAimedAlong(selected, nearestDirection);
    transform.translationMetres = nearestPoint;
    math::validateRigidTransform(transform);
    return {
        .transform = transform,
        .branchId = nearestBranchId,
        .distanceToBeamMetres = nearestDistance,
    };
}

} // namespace holobench::app::alignment
