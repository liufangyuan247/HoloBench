#pragma once

#include <array>
#include <cmath>
#include <stdexcept>

#include "optics/scene/BenchScene.hpp"

namespace holobench::app::observationoverlay {

struct ObservationPlaneQuad final {
    std::array<math::Vec3d, 4> worldCorners;
};

// Corner order is local bottom-left, bottom-right, top-right, top-left. The
// caller may map the vertically flipped OpenGL texture coordinates explicitly.
[[nodiscard]] inline ObservationPlaneQuad makeObservationPlaneQuad(
    const optics::scene::BenchComponent& component) {
    namespace bench = optics::scene;
    double widthMetres = 0.0;
    double heightMetres = 0.0;
    if (component.kind == bench::BenchComponentKind::ScreenDetector) {
        const auto& value = std::get<bench::ScreenDetectorParameters>(
            component.parameters);
        widthMetres = value.widthMetres;
        heightMetres = value.heightMetres;
    } else if (component.kind == bench::BenchComponentKind::FieldProbe) {
        const auto& value = std::get<bench::FieldProbeParameters>(
            component.parameters);
        widthMetres = value.widthMetres;
        heightMetres = value.heightMetres;
    } else if (component.kind == bench::BenchComponentKind::HolographicPlate) {
        const auto& value = std::get<bench::HolographicPlateParameters>(
            component.parameters);
        widthMetres = value.widthMetres;
        heightMetres = value.heightMetres;
    } else {
        throw std::invalid_argument(
            "reconstruction overlay target must be a Screen / Detector, Field Probe, or Holographic Plate");
    }
    math::validateRigidTransform(component.transform);
    if (!std::isfinite(widthMetres) || !std::isfinite(heightMetres)
        || widthMetres <= 0.0 || heightMetres <= 0.0) {
        throw std::invalid_argument(
            "reconstruction overlay target extent must be positive and finite");
    }
    const double halfWidth = 0.5 * widthMetres;
    const double halfHeight = 0.5 * heightMetres;
    return {{
        math::transformPointLocalToWorld(
            component.transform, {-halfWidth, -halfHeight, 0.0}),
        math::transformPointLocalToWorld(
            component.transform, {halfWidth, -halfHeight, 0.0}),
        math::transformPointLocalToWorld(
            component.transform, {halfWidth, halfHeight, 0.0}),
        math::transformPointLocalToWorld(
            component.transform, {-halfWidth, halfHeight, 0.0}),
    }};
}

} // namespace holobench::app::observationoverlay
