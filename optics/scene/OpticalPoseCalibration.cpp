#include "optics/scene/OpticalPoseCalibration.hpp"

#include <stdexcept>

namespace holobench::optics::scene {
namespace {

constexpr double kMaximumTranslationMetres = 0.1;
constexpr double kMinimumAllowedFrameTrace
    = 1.0 + 2.0 * 0.86602540378443864676;

} // namespace

void validateOpticalPoseCalibration(
    const OpticalPoseCalibration& calibration) {
    math::validateRigidTransform(calibration.nominalToMeasuredOptical);
    if (math::length(calibration.nominalToMeasuredOptical.translationMetres)
        > kMaximumTranslationMetres) {
        throw std::invalid_argument(
            "optical-pose translation exceeds the 100 mm correction limit");
    }
    const auto& offset = calibration.nominalToMeasuredOptical;
    const double trace = offset.localXAxisInWorld.x
        + offset.localYAxisInWorld.y + offset.localZAxisInWorld.z;
    if (trace < kMinimumAllowedFrameTrace) {
        throw std::invalid_argument(
            "optical-pose rotation exceeds the 30 degree correction limit");
    }
}

math::RigidTransform3d applyOpticalPoseCalibration(
    const math::RigidTransform3d& nominalOpticalFrame,
    const OpticalPoseCalibration& calibration) {
    math::validateRigidTransform(nominalOpticalFrame);
    validateOpticalPoseCalibration(calibration);
    const auto& offset = calibration.nominalToMeasuredOptical;
    math::RigidTransform3d result {
        .translationMetres = math::transformPointLocalToWorld(
            nominalOpticalFrame, offset.translationMetres),
        .localXAxisInWorld = math::transformDirectionLocalToWorld(
            nominalOpticalFrame, offset.localXAxisInWorld),
        .localYAxisInWorld = math::transformDirectionLocalToWorld(
            nominalOpticalFrame, offset.localYAxisInWorld),
        .localZAxisInWorld = math::transformDirectionLocalToWorld(
            nominalOpticalFrame, offset.localZAxisInWorld),
    };
    math::validateRigidTransform(result);
    return result;
}

} // namespace holobench::optics::scene
