#pragma once

#include "core/math/Vec3.hpp"

namespace holobench::math {

/**
 * Right-handed rigid local-to-world transform stored as orthonormal world-space
 * basis columns. Scale, shear, and reflection are invalid.
 */
struct RigidTransform3d final {
    Vec3d translationMetres {};
    Vec3d localXAxisInWorld {1.0, 0.0, 0.0};
    Vec3d localYAxisInWorld {0.0, 1.0, 0.0};
    Vec3d localZAxisInWorld {0.0, 0.0, 1.0};

    bool operator==(const RigidTransform3d&) const = default;
};

void validateRigidTransform(const RigidTransform3d& transform);

[[nodiscard]] Vec3d transformPointLocalToWorld(const RigidTransform3d& transform, Vec3d localPoint);
[[nodiscard]] Vec3d transformPointWorldToLocal(const RigidTransform3d& transform, Vec3d worldPoint);
[[nodiscard]] Vec3d transformDirectionLocalToWorld(const RigidTransform3d& transform, Vec3d localDirection);
[[nodiscard]] Vec3d transformDirectionWorldToLocal(const RigidTransform3d& transform, Vec3d worldDirection);

} // namespace holobench::math
