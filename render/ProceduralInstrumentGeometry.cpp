#include "render/ProceduralInstrumentGeometry.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "core/math/RigidTransform.hpp"

namespace holobench::render {

namespace {

namespace bench = optics::scene;

constexpr float kMinimumVisualThickness = 5e-4F;

struct VisualExtent final {
    float width = 0.03F;
    float height = 0.03F;
};

[[nodiscard]] glm::vec3 toGlm(const math::Vec3d& value) {
    const glm::vec3 converted {
        static_cast<float>(value.x),
        static_cast<float>(value.y),
        static_cast<float>(value.z),
    };
    if (!std::isfinite(converted.x) || !std::isfinite(converted.y)
        || !std::isfinite(converted.z)) {
        throw std::invalid_argument("procedural instrument transform is not finite");
    }
    return converted;
}

[[nodiscard]] glm::vec3 transformPoint(
    const bench::BenchComponent& component,
    const glm::vec3& local) {
    return toGlm(math::transformPointLocalToWorld(
        component.transform,
        {local.x, local.y, local.z}));
}

[[nodiscard]] glm::vec3 transformDirection(
    const bench::BenchComponent& component,
    const glm::vec3& local) {
    const auto world = math::transformDirectionLocalToWorld(
        component.transform,
        {local.x, local.y, local.z});
    const glm::vec3 converted = toGlm(world);
    const float length = glm::length(converted);
    if (!std::isfinite(length) || length <= 1e-8F) {
        throw std::invalid_argument("procedural instrument normal is degenerate");
    }
    return converted / length;
}

[[nodiscard]] VisualExtent visualExtent(
    const bench::BenchComponent& component) {
    double width = 0.03;
    double height = 0.03;
    switch (component.kind) {
    case bench::BenchComponentKind::LaserSource: {
        const auto& value = std::get<bench::LaserSourceParameters>(component.parameters);
        width = height = std::max(2.0 * value.beamRadiusMetres, 0.018);
        break;
    }
    case bench::BenchComponentKind::ObjectWavefrontSource: {
        const auto& value = std::get<bench::ObjectWavefrontSourceParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::PlanarMirror: {
        const auto& value = std::get<bench::PlanarMirrorParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::BeamSplitterCombiner: {
        const auto& value = std::get<bench::BeamSplitterParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::IdealThinLens:
        width = height = std::get<bench::IdealThinLensParameters>(component.parameters)
            .clearApertureDiameterMetres;
        break;
    case bench::BenchComponentKind::RealLensAssembly:
        width = height = std::get<bench::RealLensAssemblyParameters>(component.parameters)
            .clearApertureDiameterMetres;
        break;
    case bench::BenchComponentKind::Aperture: {
        const auto& value = std::get<bench::ApertureParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::SpatialFilter:
        width = height = std::get<bench::SpatialFilterParameters>(component.parameters)
            .clearApertureDiameterMetres;
        break;
    case bench::BenchComponentKind::SpatialLightModulator: {
        const auto& value = std::get<bench::SpatialLightModulatorParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::ScreenDetector: {
        const auto& value = std::get<bench::ScreenDetectorParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::FieldProbe: {
        const auto& value = std::get<bench::FieldProbeParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    case bench::BenchComponentKind::HolographicPlate: {
        const auto& value = std::get<bench::HolographicPlateParameters>(component.parameters);
        width = value.widthMetres;
        height = value.heightMetres;
        break;
    }
    }
    if (!std::isfinite(width) || !std::isfinite(height)
        || width <= 0.0 || height <= 0.0
        || width > static_cast<double>(std::numeric_limits<float>::max())
        || height > static_cast<double>(std::numeric_limits<float>::max())) {
        throw std::invalid_argument("procedural instrument extent is invalid");
    }
    return {static_cast<float>(width), static_cast<float>(height)};
}

[[nodiscard]] glm::vec4 selectedTint(glm::vec4 color, bool selected) {
    if (selected) {
        color.r = std::min(1.0F, color.r * 0.78F + 0.22F);
        color.g = std::min(1.0F, color.g * 0.78F + 0.18F);
        color.b = std::min(1.0F, color.b * 0.70F + 0.08F);
    }
    return color;
}

void addTriangle(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec4& color) {
    const glm::vec3 cross = glm::cross(b - a, c - a);
    const float length = glm::length(cross);
    if (!std::isfinite(length) || length <= 1e-10F) {
        throw std::invalid_argument("procedural instrument triangle is degenerate");
    }
    const glm::vec3 normal = transformDirection(component, cross / length);
    mesh.triangles.push_back({transformPoint(component, a), normal, color});
    mesh.triangles.push_back({transformPoint(component, b), normal, color});
    mesh.triangles.push_back({transformPoint(component, c), normal, color});
}

void addQuad(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const glm::vec3& a,
    const glm::vec3& b,
    const glm::vec3& c,
    const glm::vec3& d,
    const glm::vec4& color) {
    addTriangle(mesh, component, a, b, c, color);
    addTriangle(mesh, component, a, c, d, color);
}

void addBox(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const glm::vec3& centre,
    const glm::vec3& half,
    const glm::vec4& color) {
    if (half.x <= 0.0F || half.y <= 0.0F || half.z <= 0.0F) {
        throw std::invalid_argument("procedural instrument box extent is invalid");
    }
    const float x0 = centre.x - half.x;
    const float x1 = centre.x + half.x;
    const float y0 = centre.y - half.y;
    const float y1 = centre.y + half.y;
    const float z0 = centre.z - half.z;
    const float z1 = centre.z + half.z;
    addQuad(mesh, component, {x0, y0, z1}, {x1, y0, z1}, {x1, y1, z1}, {x0, y1, z1}, color);
    addQuad(mesh, component, {x1, y0, z0}, {x0, y0, z0}, {x0, y1, z0}, {x1, y1, z0}, color);
    addQuad(mesh, component, {x1, y0, z1}, {x1, y0, z0}, {x1, y1, z0}, {x1, y1, z1}, color);
    addQuad(mesh, component, {x0, y0, z0}, {x0, y0, z1}, {x0, y1, z1}, {x0, y1, z0}, color);
    addQuad(mesh, component, {x0, y1, z1}, {x1, y1, z1}, {x1, y1, z0}, {x0, y1, z0}, color);
    addQuad(mesh, component, {x0, y0, z0}, {x1, y0, z0}, {x1, y0, z1}, {x0, y0, z1}, color);
}

void addCylinderZ(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const glm::vec3& centre,
    float radius,
    float halfDepth,
    std::size_t segmentCount,
    const glm::vec4& color) {
    if (radius <= 0.0F || halfDepth <= 0.0F || segmentCount < 8U) {
        throw std::invalid_argument("procedural instrument cylinder is invalid");
    }
    const float z0 = centre.z - halfDepth;
    const float z1 = centre.z + halfDepth;
    for (std::size_t index = 0; index < segmentCount; ++index) {
        const double angle0 = 2.0 * std::numbers::pi_v<double>
            * static_cast<double>(index) / static_cast<double>(segmentCount);
        const double angle1 = 2.0 * std::numbers::pi_v<double>
            * static_cast<double>(index + 1U) / static_cast<double>(segmentCount);
        const glm::vec3 back0 {
            centre.x + radius * static_cast<float>(std::cos(angle0)),
            centre.y + radius * static_cast<float>(std::sin(angle0)), z0};
        const glm::vec3 back1 {
            centre.x + radius * static_cast<float>(std::cos(angle1)),
            centre.y + radius * static_cast<float>(std::sin(angle1)), z0};
        const glm::vec3 front0 {back0.x, back0.y, z1};
        const glm::vec3 front1 {back1.x, back1.y, z1};
        addQuad(mesh, component, back0, back1, front1, front0, color);
        addTriangle(mesh, component, {centre.x, centre.y, z1}, front0, front1, color);
        addTriangle(mesh, component, {centre.x, centre.y, z0}, back1, back0, color);
    }
}

void addEllipticalRingZ(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const glm::vec3& centre,
    const glm::vec2& outerRadius,
    const glm::vec2& innerRadius,
    float halfDepth,
    std::size_t segmentCount,
    const glm::vec4& color) {
    if (outerRadius.x <= 0.0F || outerRadius.y <= 0.0F
        || innerRadius.x <= 0.0F || innerRadius.y <= 0.0F
        || innerRadius.x >= outerRadius.x || innerRadius.y >= outerRadius.y
        || halfDepth <= 0.0F || segmentCount < 8U) {
        throw std::invalid_argument("procedural instrument ring is invalid");
    }
    const float z0 = centre.z - halfDepth;
    const float z1 = centre.z + halfDepth;
    for (std::size_t index = 0U; index < segmentCount; ++index) {
        const double angle0 = 2.0 * std::numbers::pi_v<double>
            * static_cast<double>(index) / static_cast<double>(segmentCount);
        const double angle1 = 2.0 * std::numbers::pi_v<double>
            * static_cast<double>(index + 1U) / static_cast<double>(segmentCount);
        const auto at = [&centre](
                            const glm::vec2& radius,
                            double angle,
                            float z) {
            return glm::vec3 {
                centre.x + radius.x * static_cast<float>(std::cos(angle)),
                centre.y + radius.y * static_cast<float>(std::sin(angle)),
                z,
            };
        };
        const glm::vec3 outerBack0 = at(outerRadius, angle0, z0);
        const glm::vec3 outerBack1 = at(outerRadius, angle1, z0);
        const glm::vec3 outerFront0 = at(outerRadius, angle0, z1);
        const glm::vec3 outerFront1 = at(outerRadius, angle1, z1);
        const glm::vec3 innerBack0 = at(innerRadius, angle0, z0);
        const glm::vec3 innerBack1 = at(innerRadius, angle1, z0);
        const glm::vec3 innerFront0 = at(innerRadius, angle0, z1);
        const glm::vec3 innerFront1 = at(innerRadius, angle1, z1);
        addQuad(mesh, component,
            outerBack0, outerBack1, outerFront1, outerFront0, color);
        addQuad(mesh, component,
            innerBack1, innerBack0, innerFront0, innerFront1, color);
        addQuad(mesh, component,
            outerFront0, outerFront1, innerFront1, innerFront0, color);
        addQuad(mesh, component,
            outerBack1, outerBack0, innerBack0, innerBack1, color);
    }
}

void addRectangularFrame(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    float width,
    float height,
    float border,
    float halfDepth,
    const glm::vec4& color) {
    const float clampedBorder = std::clamp(
        border, kMinimumVisualThickness, 0.45F * std::min(width, height));
    addBox(mesh, component, {0.0F, 0.5F * (height - clampedBorder), 0.0F},
        {0.5F * width, 0.5F * clampedBorder, halfDepth}, color);
    addBox(mesh, component, {0.0F, -0.5F * (height - clampedBorder), 0.0F},
        {0.5F * width, 0.5F * clampedBorder, halfDepth}, color);
    const float innerHeight = std::max(height - 2.0F * clampedBorder, kMinimumVisualThickness);
    addBox(mesh, component, {0.5F * (width - clampedBorder), 0.0F, 0.0F},
        {0.5F * clampedBorder, 0.5F * innerHeight, halfDepth}, color);
    addBox(mesh, component, {-0.5F * (width - clampedBorder), 0.0F, 0.0F},
        {0.5F * clampedBorder, 0.5F * innerHeight, halfDepth}, color);
}

void addPostAndBase(
    ProceduralInstrumentMesh& mesh,
    const bench::BenchComponent& component,
    const VisualExtent& extent,
    const glm::vec4& color) {
    if (component.mechanicalAssembly.has_value()) {
        const auto& assembly = *component.mechanicalAssembly;
        bench::BenchComponent baseComponent = component;
        baseComponent.transform = assembly.benchFrame;
        baseComponent.mechanicalAssembly.reset();

        const float postHeight = static_cast<float>(assembly.postHeightMetres);
        const float postHalfWidth = std::clamp(
            0.08F * extent.width, 0.0015F, 0.004F);
        const float baseWidth = std::max(0.024F, 0.75F * extent.width);
        const float baseDepth = std::max(0.020F, 0.55F * baseWidth);
        const float stageWidth = std::max(0.018F, 0.62F * extent.width);
        const float stageDepth = std::max(0.016F, 0.55F * extent.width);
        const glm::vec3 stageTranslation {
            static_cast<float>(assembly.stageTranslationMetres.x),
            static_cast<float>(assembly.stageTranslationMetres.y),
            static_cast<float>(assembly.stageTranslationMetres.z),
        };
        addBox(mesh, baseComponent,
            {0.0F, -0.0025F, 0.0F},
            {0.5F * baseWidth, 0.0025F, 0.5F * baseDepth}, color);
        if (postHeight > kMinimumVisualThickness) {
            addBox(mesh, baseComponent,
                {0.0F, 0.5F * postHeight, 0.0F},
                {postHalfWidth, 0.5F * postHeight, postHalfWidth}, color);
        }
        addBox(mesh, baseComponent,
            stageTranslation + glm::vec3 {0.0F, postHeight, 0.0F},
            {0.5F * stageWidth, 0.0025F, 0.5F * stageDepth}, color);
        const float knobHalf = std::clamp(
            0.10F * extent.width, 0.0018F, 0.0035F);
        addBox(mesh, baseComponent,
            {0.014F, 0.55F * postHeight, 0.0F},
            {knobHalf, knobHalf, knobHalf}, color);
        addBox(mesh, baseComponent,
            stageTranslation + glm::vec3 {
                0.5F * stageWidth + 1.5F * knobHalf,
                postHeight,
                0.0F},
            {1.5F * knobHalf, knobHalf, knobHalf}, color);
        addBox(mesh, baseComponent,
            stageTranslation + glm::vec3 {
                0.0F,
                postHeight + 1.5F * knobHalf,
                0.5F * stageDepth},
            {knobHalf, 1.5F * knobHalf, knobHalf}, color);
        addBox(mesh, baseComponent,
            stageTranslation + glm::vec3 {
                0.0F,
                postHeight,
                0.5F * stageDepth + 1.5F * knobHalf},
            {knobHalf, knobHalf, 1.5F * knobHalf}, color);
        addBox(mesh, component,
            {-0.55F * extent.width, 0.0F, -0.006F},
            {1.5F * knobHalf, knobHalf, knobHalf}, color);
        addBox(mesh, component,
            {0.0F, -0.55F * extent.height, -0.006F},
            {knobHalf, 1.5F * knobHalf, knobHalf}, color);
        return;
    }
    const float postHeight = std::max(0.018F, 0.45F * extent.height);
    const float postWidth = std::clamp(0.08F * extent.width, 0.0015F, 0.004F);
    const float baseWidth = std::max(0.018F, 0.65F * extent.width);
    addBox(mesh, component,
        {0.0F, -0.5F * extent.height - 0.5F * postHeight, -0.004F},
        {postWidth, 0.5F * postHeight, postWidth}, color);
    addBox(mesh, component,
        {0.0F, -0.5F * extent.height - postHeight - 0.0025F, -0.004F},
        {0.5F * baseWidth, 0.0025F, std::max(0.009F, 0.35F * baseWidth)}, color);
}

void updateBounds(ProceduralInstrumentMesh& mesh) {
    if (mesh.triangles.empty()) {
        throw std::invalid_argument("procedural instrument generated no triangles");
    }
    mesh.boundsMinimum = mesh.triangles.front().position;
    mesh.boundsMaximum = mesh.triangles.front().position;
    for (const auto& vertex : mesh.triangles) {
        mesh.boundsMinimum = glm::min(mesh.boundsMinimum, vertex.position);
        mesh.boundsMaximum = glm::max(mesh.boundsMaximum, vertex.position);
    }
}

} // namespace

ProceduralInstrumentMesh generateProceduralInstrumentMesh(
    const bench::BenchComponent& component,
    const InstrumentGenerationOptions& options) {
    bench::validateBenchComponent(component);
    const std::size_t radialSegments = std::clamp(
        options.radialSegments, std::size_t {8U}, std::size_t {64U});
    const VisualExtent extent = visualExtent(component);
    const float halfWidth = 0.5F * extent.width;
    const float halfHeight = 0.5F * extent.height;
    const float shortSide = std::min(extent.width, extent.height);

    ProceduralInstrumentMesh mesh;
    mesh.triangles.reserve(1'024U);
    mesh.opticalProxy = {
        .centre = toGlm(component.transform.translationMetres),
        .normal = toGlm(component.transform.localZAxisInWorld),
        .xAxis = toGlm(component.transform.localXAxisInWorld),
        .yAxis = toGlm(component.transform.localYAxisInWorld),
        .widthMetres = extent.width,
        .heightMetres = extent.height,
    };

    const glm::vec4 dark = selectedTint({0.10F, 0.12F, 0.15F, 1.0F}, options.selected);
    const glm::vec4 metal = selectedTint({0.34F, 0.38F, 0.44F, 1.0F}, options.selected);
    switch (component.kind) {
    case bench::BenchComponentKind::LaserSource: {
        const float bodyRadius = std::max(0.010F, halfWidth);
        addBox(mesh, component, {0.0F, 0.0F, -0.020F},
            {bodyRadius, std::max(0.008F, halfHeight), 0.030F},
            selectedTint({0.42F, 0.08F, 0.06F, 1.0F}, options.selected));
        addCylinderZ(mesh, component, {0.0F, 0.0F, 0.014F},
            std::max(0.004F, 0.45F * bodyRadius), 0.004F, radialSegments, dark);
        addCylinderZ(mesh, component, {0.0F, 0.0F, 0.019F},
            std::max(0.002F, 0.28F * bodyRadius), 0.001F, radialSegments,
            {0.75F, 0.10F, 0.06F, 1.0F});
        break;
    }
    case bench::BenchComponentKind::ObjectWavefrontSource:
        addBox(mesh, component, {0.0F, 0.0F, -0.004F},
            {halfWidth + 0.003F, halfHeight + 0.003F, 0.005F}, dark);
        addBox(mesh, component, {0.0F, 0.0F, 0.0015F},
            {halfWidth, halfHeight, 0.0005F},
            selectedTint({0.58F, 0.18F, 0.78F, 1.0F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::PlanarMirror:
        addBox(mesh, component, {0.0F, 0.0F, -0.0025F},
            {halfWidth + 0.0025F, halfHeight + 0.0025F, 0.003F}, dark);
        addBox(mesh, component, {0.0F, 0.0F, 0.0005F},
            {halfWidth, halfHeight, 0.0005F},
            selectedTint({0.72F, 0.82F, 0.92F, 1.0F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::BeamSplitterCombiner:
        addBox(mesh, component, {0.0F, 0.0F, 0.0F},
            {halfWidth, halfHeight, std::max(0.004F, 0.18F * shortSide)},
            selectedTint({0.20F, 0.62F, 0.76F, 0.82F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::IdealThinLens:
        addEllipticalRingZ(mesh, component, {},
            {halfWidth + 0.003F, halfHeight + 0.003F},
            {halfWidth, halfHeight}, 0.003F, radialSegments, dark);
        addCylinderZ(mesh, component, {0.0F, 0.0F, 0.0008F}, halfWidth,
            0.0012F, radialSegments,
            selectedTint({0.18F, 0.72F, 0.88F, 0.74F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::RealLensAssembly:
        addEllipticalRingZ(mesh, component, {},
            {halfWidth + 0.004F, halfHeight + 0.004F},
            {halfWidth, halfHeight}, 0.009F, radialSegments, dark);
        addCylinderZ(mesh, component, {0.0F, 0.0F, 0.010F}, halfWidth,
            0.0015F, radialSegments,
            selectedTint({0.16F, 0.58F, 0.82F, 0.82F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::Aperture: {
        const auto& value = std::get<bench::ApertureParameters>(component.parameters);
        if (value.shape == bench::ApertureShape::Circular) {
            addEllipticalRingZ(mesh, component, {},
                {halfWidth + 0.004F, halfHeight + 0.004F},
                {halfWidth, halfHeight}, 0.002F, radialSegments, dark);
        } else if (value.shape == bench::ApertureShape::Rectangular) {
            addRectangularFrame(mesh, component, extent.width + 0.008F,
                extent.height + 0.008F, 0.004F, 0.002F, dark);
        } else {
            const float slitHalfWidth = 0.5F * static_cast<float>(value.slitWidthMetres);
            const float slitHalfHeight = 0.5F * static_cast<float>(value.slitHeightMetres);
            const float halfSeparation = 0.5F * static_cast<float>(value.slitSeparationMetres);
            const float plateHalfWidth = halfWidth + 0.004F;
            const float plateHalfHeight = halfHeight + 0.004F;
            const float leftEdge = -halfSeparation - slitHalfWidth;
            const float rightEdge = halfSeparation + slitHalfWidth;
            const float centreGap = halfSeparation - slitHalfWidth;
            addBox(mesh, component,
                {-0.5F * (plateHalfWidth - leftEdge), 0.0F, 0.0F},
                {0.5F * (plateHalfWidth + leftEdge), plateHalfHeight, 0.002F}, dark);
            addBox(mesh, component,
                {0.5F * (plateHalfWidth + rightEdge), 0.0F, 0.0F},
                {0.5F * (plateHalfWidth - rightEdge), plateHalfHeight, 0.002F}, dark);
            addBox(mesh, component, {0.0F, 0.0F, 0.0F},
                {centreGap, plateHalfHeight, 0.002F}, dark);
            const float openingSpan = rightEdge - leftEdge;
            addBox(mesh, component,
                {0.0F, 0.5F * (plateHalfHeight + slitHalfHeight), 0.0F},
                {0.5F * openingSpan, 0.5F * (plateHalfHeight - slitHalfHeight), 0.002F}, dark);
            addBox(mesh, component,
                {0.0F, -0.5F * (plateHalfHeight + slitHalfHeight), 0.0F},
                {0.5F * openingSpan, 0.5F * (plateHalfHeight - slitHalfHeight), 0.002F}, dark);
        }
        addPostAndBase(mesh, component, extent, metal);
        break;
    }
    case bench::BenchComponentKind::SpatialFilter:
        addEllipticalRingZ(mesh, component, {},
            {halfWidth + 0.004F, halfHeight + 0.004F},
            {
                std::max(1e-6F,
                    0.5F * static_cast<float>(std::get<bench::SpatialFilterParameters>(component.parameters).pinholeDiameterMetres)),
                std::max(1e-6F,
                    0.5F * static_cast<float>(std::get<bench::SpatialFilterParameters>(component.parameters).pinholeDiameterMetres)),
            },
            0.003F, radialSegments, dark);
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::SpatialLightModulator:
        addBox(mesh, component, {0.0F, 0.0F, -0.005F},
            {halfWidth + 0.004F, halfHeight + 0.004F, 0.006F}, dark);
        addBox(mesh, component, {0.0F, 0.0F, 0.0015F},
            {halfWidth, halfHeight, 0.0005F},
            selectedTint({0.46F, 0.18F, 0.76F, 1.0F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::ScreenDetector:
        addRectangularFrame(mesh, component, extent.width + 0.008F,
            extent.height + 0.008F, 0.004F, 0.0025F, dark);
        addBox(mesh, component, {0.0F, 0.0F, -0.001F},
            {halfWidth, halfHeight, 0.0007F},
            selectedTint({0.90F, 0.93F, 0.96F, 1.0F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    case bench::BenchComponentKind::FieldProbe:
        addRectangularFrame(mesh, component, extent.width + 0.003F,
            extent.height + 0.003F, 0.0015F, 0.0007F,
            selectedTint({0.12F, 0.82F, 0.48F, 0.88F}, options.selected));
        addBox(mesh, component, {}, {halfWidth, halfHeight, 0.00025F},
            selectedTint({0.12F, 0.92F, 0.58F, 0.25F}, options.selected));
        break;
    case bench::BenchComponentKind::HolographicPlate: {
        const auto& value = std::get<bench::HolographicPlateParameters>(component.parameters);
        const float visualThickness = std::max(
            kMinimumVisualThickness,
            static_cast<float>(std::min(value.thicknessMetres, 0.004)));
        addRectangularFrame(mesh, component, extent.width + 0.006F,
            extent.height + 0.006F, 0.003F, 0.002F, dark);
        addBox(mesh, component, {}, {halfWidth, halfHeight, 0.5F * visualThickness},
            selectedTint({0.86F, 0.18F, 0.58F, 0.72F}, options.selected));
        addPostAndBase(mesh, component, extent, metal);
        break;
    }
    }

    if (mesh.triangles.size() > 50'000U) {
        throw std::length_error("procedural instrument mesh exceeds bounded vertex budget");
    }
    updateBounds(mesh);
    return mesh;
}

} // namespace holobench::render
