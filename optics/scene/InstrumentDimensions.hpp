#pragma once

#include "optics/scene/BenchScene.hpp"
#include <algorithm>
#include <array>
#include <cmath>
#include <stdexcept>

namespace holobench::optics::scene {
// Shared analytic face dimensions and nominal disposable-body envelopes.
// Lens surfaces and clear-aperture clipping remain defined by component
// parameters and the optical interaction models.
inline math::Vec3d instrumentFaceDimensions(const BenchComponent &component) {
    return std::visit(
        [](const auto &p) -> math::Vec3d {
            if constexpr (requires {
                              p.widthMetres;
                              p.heightMetres;
                          })
                return {p.widthMetres, p.heightMetres, 0};
            else if constexpr (requires { p.clearApertureDiameterMetres; })
                return {p.clearApertureDiameterMetres, p.clearApertureDiameterMetres, 0};
            else if constexpr (requires { p.sizeMetres; })
                return {p.sizeMetres, p.sizeMetres, 0};
            else
                return {std::max(2.0 * p.beamRadiusMetres, 0.018),
                        std::max(2.0 * p.beamRadiusMetres, 0.018), 0};
        },
        component.parameters);
}

// Protected body/frame dimensions around the analytic optical face. Mounting
// solids may touch this envelope but must remain outside the optical face.
// Values are derived from the same component parameters used by the solver;
// the procedural mesh is never queried for mechanical truth.
inline math::Vec3d instrumentProtectedFrameDimensions(
    const BenchComponent& component) {
    const auto face = instrumentFaceDimensions(component);
    switch (component.kind) {
    case BenchComponentKind::LaserSource:
        return {std::max(0.020, face.x), std::max(0.016, face.y), 0.100};
    case BenchComponentKind::ObjectWavefrontSource: {
        const auto& object = std::get<ObjectWavefrontSourceParameters>(
            component.parameters);
        const double border = object.geometry == ObjectSourceGeometry::UniformPlane
            ? 0.003 : 0.0;
        return {
            face.x + 2.0 * border,
            face.y + 2.0 * border,
            object.geometry == ObjectSourceGeometry::UniformPlane
                ? std::max(0.018, object.depthMetres)
                : object.depthMetres,
        };
    }
    case BenchComponentKind::PlanarMirror:
        return {face.x + 0.005, face.y + 0.005, 0.011};
    case BenchComponentKind::BeamSplitterCombiner:
        return {face.x, face.y,
            2.0 * std::max(0.004, 0.18 * std::min(face.x, face.y))};
    case BenchComponentKind::XCubeCombiner:
        return {face.x + 0.006, face.y + 0.008, face.x + 0.006};
    case BenchComponentKind::IdealThinLens:
        return {face.x + 0.006, face.y + 0.006, 0.006};
    case BenchComponentKind::RealLensAssembly:
        return {face.x + 0.008, face.y + 0.008, 0.023};
    case BenchComponentKind::Aperture:
    case BenchComponentKind::SpatialFilter:
    case BenchComponentKind::SpatialLightModulator:
    case BenchComponentKind::ScreenDetector:
        return {face.x + 0.008, face.y + 0.008, 0.006};
    case BenchComponentKind::FieldProbe:
        return {face.x + 0.003, face.y + 0.003, 0.0014};
    case BenchComponentKind::HolographicPlate:
        return {face.x + 0.006, face.y + 0.006, 0.004};
    }
    return face;
}

struct InstrumentMountLayout final {
    math::Vec3d opticalFaceDimensions {};
    math::Vec3d protectedFrameDimensions {};
    double opticalCentreHeightMetres = 0.0;
    double protectedVerticalHalfSpanMetres = 0.0;
    double clearanceMetres = 0.0015;
    double supportTopHeightMetres = 0.0;
    double postHalfWidthMetres = 0.0;
    double stageWidthMetres = 0.0;
    double stageDepthMetres = 0.0;
    double stageThicknessMetres = 0.005;
    double stageCentreHeightMetres = 0.0;
    double stageControlCentreHeightMetres = 0.0;
    double knobHalfSizeMetres = 0.0;
    // Component-local control centres outside the protected frame.
    double horizontalMountKnobCentreXMetres = 0.0;
    double verticalMountKnobCentreYMetres = 0.0;
};

inline InstrumentMountLayout instrumentMountLayout(
    const BenchComponent& component) {
    if (!component.mechanicalAssembly.has_value()) {
        throw std::invalid_argument(
            "instrument mount layout requires a mechanical assembly");
    }
    const auto& assembly = *component.mechanicalAssembly;
    const auto face = instrumentFaceDimensions(component);
    const auto frame = instrumentProtectedFrameDimensions(component);
    const double pitch = std::abs(assembly.mountPitchRadians);
    const double frameVerticalHalfSpan = 0.5 * (
        frame.y * std::abs(std::cos(pitch))
        + frame.z * std::abs(std::sin(pitch)));
    constexpr double clearance = 0.0015;
    constexpr double stageThickness = 0.005;
    const double opticalCentreHeight = assembly.postHeightMetres
        + assembly.stageTranslationMetres.y;
    // The top of every base-frame support solid ends below the protected
    // frame. A negative available height collapses the post to the bench
    // plane and is reported by instrumentMountClearsProtectedFrame().
    const double supportTop = std::max(
        0.0,
        opticalCentreHeight - frameVerticalHalfSpan - clearance);
    const double postHalfWidth = std::clamp(
        0.08 * frame.x, 0.0015, 0.004);
    const double stageWidth = std::max(0.018, 0.62 * frame.x);
    const double stageDepth = std::max(0.016, 0.55 * frame.x);
    const double knobHalf = std::clamp(
        0.10 * frame.x, 0.0018, 0.0035);
    const double stageCentre = supportTop - 0.5 * stageThickness;
    const double stageControlCentre = supportTop
        - std::max(0.5 * stageThickness, 1.5 * knobHalf);
    return {
        .opticalFaceDimensions = face,
        .protectedFrameDimensions = frame,
        .opticalCentreHeightMetres = opticalCentreHeight,
        .protectedVerticalHalfSpanMetres = frameVerticalHalfSpan,
        .clearanceMetres = clearance,
        .supportTopHeightMetres = supportTop,
        .postHalfWidthMetres = postHalfWidth,
        .stageWidthMetres = stageWidth,
        .stageDepthMetres = stageDepth,
        .stageThicknessMetres = stageThickness,
        .stageCentreHeightMetres = stageCentre,
        .stageControlCentreHeightMetres = stageControlCentre,
        .knobHalfSizeMetres = knobHalf,
        .horizontalMountKnobCentreXMetres = -(
            0.5 * frame.x + clearance + 1.5 * knobHalf),
        .verticalMountKnobCentreYMetres = -(
            0.5 * frame.y + clearance + 1.5 * knobHalf),
    };
}

inline bool instrumentMountClearsProtectedFrame(
    const InstrumentMountLayout& layout) noexcept {
    constexpr double tolerance = 1e-12;
    const double protectedBottom = layout.opticalCentreHeightMetres
        - layout.protectedVerticalHalfSpanMetres;
    const bool baseSolidsClear = layout.supportTopHeightMetres
        + layout.clearanceMetres <= protectedBottom + tolerance;
    const bool horizontalControlClear = -layout.horizontalMountKnobCentreXMetres
        - 1.5 * layout.knobHalfSizeMetres
        >= 0.5 * layout.protectedFrameDimensions.x
            + layout.clearanceMetres - tolerance;
    const bool verticalControlClear = layout.verticalMountKnobCentreYMetres
        + 1.5 * layout.knobHalfSizeMetres
        <= -0.5 * layout.protectedFrameDimensions.y
            - layout.clearanceMetres + tolerance;
    const bool stageControlsClear = layout.stageControlCentreHeightMetres
        + 1.5 * layout.knobHalfSizeMetres
        <= layout.supportTopHeightMetres + tolerance;
    return baseSolidsClear && horizontalControlClear
        && verticalControlClear && stageControlsClear;
}

inline math::Vec3d instrumentBaseDimensions(const BenchComponent &component) {
    const double width = std::max(0.024, 0.75 * instrumentFaceDimensions(component).x);
    return {width, 0.005, std::max(0.020, 0.55 * width)};
}
struct BaseInterference {
    std::string first, second;
};
inline bool instrumentBasesOverlap(const BenchComponent &a, const BenchComponent &b) {
    if (!a.mechanicalAssembly || !b.mechanicalAssembly)
        return false;
    const auto &fa = a.mechanicalAssembly->benchFrame;
    const auto &fb = b.mechanicalAssembly->benchFrame;
    const std::array axesA{fa.localXAxisInWorld, fa.localYAxisInWorld, fa.localZAxisInWorld};
    const std::array axesB{fb.localXAxisInWorld, fb.localYAxisInWorld, fb.localZAxisInWorld};
    const auto ea = instrumentBaseDimensions(a) * 0.5, eb = instrumentBaseDimensions(b) * 0.5;
    const std::array halfA{ea.x, ea.y, ea.z}, halfB{eb.x, eb.y, eb.z};
    const auto displacement = math::transformPointLocalToWorld(fb, {0, -0.0025, 0}) -
                              math::transformPointLocalToWorld(fa, {0, -0.0025, 0});
    const auto separated = [&](math::Vec3d axis) {
        if (math::lengthSquared(axis) < 1e-20)
            return false;
        axis = math::normalized(axis);
        double span = 0;
        for (std::size_t i = 0; i < 3U; ++i)
            span += halfA[i] * std::abs(math::dot(axesA[i], axis)) +
                    halfB[i] * std::abs(math::dot(axesB[i], axis));
        return std::abs(math::dot(displacement, axis)) >= span - 1e-9;
    };
    for (const auto &axis : axesA)
        if (separated(axis))
            return false;
    for (const auto &axis : axesB)
        if (separated(axis))
            return false;
    for (const auto &x : axesA)
        for (const auto &y : axesB)
            if (separated(math::cross(x, y)))
                return false;
    return true;
}
inline std::vector<BaseInterference> findBaseInterferences(const BenchScene &scene) {
    std::vector<BaseInterference> result;
    const auto &c = scene.components();
    for (std::size_t i = 0; i < c.size(); ++i)
        for (std::size_t j = i + 1U; j < c.size(); ++j)
            if (instrumentBasesOverlap(c[i], c[j]))
                result.push_back({c[i].id, c[j].id});
    return result;
}

inline std::vector<std::string> findMountClearanceFailures(
    const BenchScene& scene) {
    std::vector<std::string> result;
    for (const auto& component : scene.components()) {
        if (component.mechanicalAssembly.has_value()
            && !instrumentMountClearsProtectedFrame(
                instrumentMountLayout(component))) {
            result.push_back(component.id);
        }
    }
    return result;
}
} // namespace holobench::optics::scene
