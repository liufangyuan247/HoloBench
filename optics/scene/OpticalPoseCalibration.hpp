#pragma once

#include "core/math/RigidTransform.hpp"

namespace holobench::optics::scene {

// A measured rigid offset from a nominal instrument frame to its actual
// optical frame. Asset I/O and catalog lifetime belong to app/; this physical
// transform model belongs to optics/.
struct OpticalPoseCalibration final {
    math::RigidTransform3d nominalToMeasuredOptical {};

    bool operator==(const OpticalPoseCalibration&) const = default;
};

void validateOpticalPoseCalibration(
    const OpticalPoseCalibration& calibration);

[[nodiscard]] math::RigidTransform3d applyOpticalPoseCalibration(
    const math::RigidTransform3d& nominalOpticalFrame,
    const OpticalPoseCalibration& calibration);

} // namespace holobench::optics::scene
