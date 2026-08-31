#include "core/math/RigidTransform.hpp"

#include <cmath>
#include <stdexcept>

namespace holobench::math {

namespace {

constexpr double kOrthonormalTolerance = 2e-12;

void validateVector(Vec3d value, const char* message) {
    if (!isFinite(value)) {
        throw std::invalid_argument(message);
    }
}

} // namespace

void validateRigidTransform(const RigidTransform3d& transform) {
    validateVector(transform.translationMetres, "rigid-transform translation must be finite");
    validateVector(transform.localXAxisInWorld, "rigid-transform X axis must be finite");
    validateVector(transform.localYAxisInWorld, "rigid-transform Y axis must be finite");
    validateVector(transform.localZAxisInWorld, "rigid-transform Z axis must be finite");

    const double xLengthSquared = lengthSquared(transform.localXAxisInWorld);
    const double yLengthSquared = lengthSquared(transform.localYAxisInWorld);
    const double zLengthSquared = lengthSquared(transform.localZAxisInWorld);
    if (std::abs(xLengthSquared - 1.0) > kOrthonormalTolerance
        || std::abs(yLengthSquared - 1.0) > kOrthonormalTolerance
        || std::abs(zLengthSquared - 1.0) > kOrthonormalTolerance) {
        throw std::invalid_argument("rigid-transform axes must have unit length");
    }
    if (std::abs(dot(transform.localXAxisInWorld, transform.localYAxisInWorld)) > kOrthonormalTolerance
        || std::abs(dot(transform.localXAxisInWorld, transform.localZAxisInWorld)) > kOrthonormalTolerance
        || std::abs(dot(transform.localYAxisInWorld, transform.localZAxisInWorld)) > kOrthonormalTolerance) {
        throw std::invalid_argument("rigid-transform axes must be mutually orthogonal");
    }
    const double handedness = dot(
        cross(transform.localXAxisInWorld, transform.localYAxisInWorld),
        transform.localZAxisInWorld);
    if (std::abs(handedness - 1.0) > kOrthonormalTolerance) {
        throw std::invalid_argument("rigid transform must be right-handed without reflection");
    }
}

Vec3d transformPointLocalToWorld(const RigidTransform3d& transform, Vec3d localPoint) {
    validateRigidTransform(transform);
    validateVector(localPoint, "local point must be finite");
    return transform.translationMetres
        + transform.localXAxisInWorld * localPoint.x
        + transform.localYAxisInWorld * localPoint.y
        + transform.localZAxisInWorld * localPoint.z;
}

Vec3d transformPointWorldToLocal(const RigidTransform3d& transform, Vec3d worldPoint) {
    validateRigidTransform(transform);
    validateVector(worldPoint, "world point must be finite");
    const Vec3d delta = worldPoint - transform.translationMetres;
    return {
        dot(delta, transform.localXAxisInWorld),
        dot(delta, transform.localYAxisInWorld),
        dot(delta, transform.localZAxisInWorld),
    };
}

Vec3d transformDirectionLocalToWorld(const RigidTransform3d& transform, Vec3d localDirection) {
    validateRigidTransform(transform);
    validateVector(localDirection, "local direction must be finite");
    return transform.localXAxisInWorld * localDirection.x
        + transform.localYAxisInWorld * localDirection.y
        + transform.localZAxisInWorld * localDirection.z;
}

Vec3d transformDirectionWorldToLocal(const RigidTransform3d& transform, Vec3d worldDirection) {
    validateRigidTransform(transform);
    validateVector(worldDirection, "world direction must be finite");
    return {
        dot(worldDirection, transform.localXAxisInWorld),
        dot(worldDirection, transform.localYAxisInWorld),
        dot(worldDirection, transform.localZAxisInWorld),
    };
}

} // namespace holobench::math
