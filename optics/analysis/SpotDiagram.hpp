#pragma once

#include <cstddef>
#include <optional>
#include <string>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/ray/SequentialLens.hpp"

namespace holobench::optics::analysis {

enum class SpotRayRejectionReason {
    PrescriptionTraceFailed,
    ImagePlaneMiss,
};

struct SpotSample final {
    std::size_t sourceRayIndex = 0;
    std::string fieldId;
    double imageXMetres = 0.0;
    double imageYMetres = 0.0;
    std::optional<double> chiefRelativeXMetres;
    std::optional<double> chiefRelativeYMetres;
    double vacuumWavelengthMetres = 0.0;
    double power = 0.0;
};

struct RejectedSpotRay final {
    std::size_t sourceRayIndex = 0;
    SpotRayRejectionReason reason = SpotRayRejectionReason::PrescriptionTraceFailed;
    std::optional<ray::SequentialTraceStatus> traceStatus;
    std::string fieldId;
    double vacuumWavelengthMetres = 0.0;
};

struct SpotStatistics final {
    double centroidXMetres = 0.0;
    double centroidYMetres = 0.0;
    double rmsRadiusMetres = 0.0;
    double geometricRadiusMetres = 0.0;
    double totalPower = 0.0;
};

struct WavelengthSpotGroup final {
    double vacuumWavelengthMetres = 0.0;
    std::vector<std::size_t> sampleIndices;
    SpotStatistics statistics;
};

struct FieldSpotGroup final {
    std::string fieldId;
    std::vector<std::size_t> sampleIndices;
    SpotStatistics statistics;
};

struct FieldWavelengthSpotGroup final {
    std::string fieldId;
    double vacuumWavelengthMetres = 0.0;
    std::vector<std::size_t> sampleIndices;
    SpotStatistics statistics;
};

struct FieldTaggedRay final {
    ray::Ray ray;
    std::string fieldId;
};

struct SpotDiagramResult final {
    std::vector<SpotSample> samples;
    std::vector<RejectedSpotRay> rejectedRays;
    SpotStatistics statistics;
    std::vector<WavelengthSpotGroup> wavelengthGroups;
    std::vector<FieldSpotGroup> fieldGroups;
    std::vector<FieldWavelengthSpotGroup> fieldWavelengthGroups;
    std::optional<double> chiefImageXMetres;
    std::optional<double> chiefImageYMetres;
};

[[nodiscard]] SpotDiagramResult computeSpotDiagram(
    const std::vector<FieldTaggedRay>& incidentWorldRays,
    const ray::SequentialLensPrescription& prescription,
    const math::RigidTransform3d& imagePlaneLocalToWorld,
    const ray::SurfaceIntersectionOptions& intersectionOptions,
    std::optional<std::size_t> chiefRayIndex = std::nullopt);

[[nodiscard]] SpotDiagramResult computeSpotDiagram(
    const std::vector<ray::Ray>& incidentWorldRays,
    const ray::SequentialLensPrescription& prescription,
    const math::RigidTransform3d& imagePlaneLocalToWorld,
    const ray::SurfaceIntersectionOptions& intersectionOptions,
    std::optional<std::size_t> chiefRayIndex = std::nullopt);

} // namespace holobench::optics::analysis
