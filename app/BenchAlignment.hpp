#pragma once

#include <cstdint>
#include <span>

#include "core/math/RigidTransform.hpp"
#include "optics/scene/BenchInteraction.hpp"

namespace holobench::app::alignment {

struct BeamSnapResult final {
    math::RigidTransform3d transform;
    std::uint64_t branchId = 0;
    double distanceToBeamMetres = 0.0;
};

// Preserves position and rolls the local frame as little as possible while
// aiming local +Z at the target point.
[[nodiscard]] math::RigidTransform3d aimAt(
    const math::RigidTransform3d& selected,
    math::Vec3d targetPointMetres);

// Projects the selected origin onto the target's infinite local-Z axis and
// copies the target frame. This is an ordinary transform, not a scene link.
[[nodiscard]] math::RigidTransform3d makeCoaxialWith(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target);

[[nodiscard]] math::RigidTransform3d matchHeight(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target);

// Places the selected component at a signed distance along the target's local
// +Z axis and copies its orientation.
[[nodiscard]] math::RigidTransform3d placeAlongTargetAxis(
    const math::RigidTransform3d& selected,
    const math::RigidTransform3d& target,
    double signedDistanceMetres);

// Moves the component to the closest point on a finite visible trace segment
// and aims local +Z along that segment. Invalid/degenerate segments are ignored.
[[nodiscard]] BeamSnapResult snapToNearestBeam(
    const math::RigidTransform3d& selected,
    std::span<const optics::scene::BenchTraceSegment> segments,
    double maximumDistanceMetres);

} // namespace holobench::app::alignment
