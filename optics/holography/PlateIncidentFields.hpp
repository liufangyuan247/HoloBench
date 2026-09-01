#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "optics/scene/BenchPathEvidence.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::holography {

enum class RecordingBranchRole { Object, Reference };
enum class PlateIncidenceSide { NegativeLocalZ, PositiveLocalZ };
enum class PlateRecordingGeometry { Transmission, Reflection };

using PlatePathInteraction = scene::BenchPathInteraction;

struct PlateIncidentBranch final {
    RecordingBranchRole role = RecordingBranchRole::Reference;
    PlateIncidenceSide side = PlateIncidenceSide::NegativeLocalZ;
    math::Vec3d localHitPointMetres {};
    math::Vec3d localDirection {};
    double incidenceAngleRadians = 0.0;
    scene::BeamState beam;
    // Ordered source-to-plate interaction evidence. The plate itself is the
    // final entry and has no outgoing beam.
    std::vector<PlatePathInteraction> pathInteractions;

    bool operator==(const PlateIncidentBranch&) const = default;
};

struct PlateIncidentFieldSet final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::vector<PlateIncidentBranch> branches;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

struct PlateRecordingPair final {
    std::uint64_t objectBranchId = 0;
    std::uint64_t referenceBranchId = 0;
    PlateRecordingGeometry geometry = PlateRecordingGeometry::Transmission;
    double wavelengthMetres = 532e-9;
    double signedOpticalPathDifferenceMetres = 0.0;
    double crossingAngleRadians = 0.0;

    bool operator==(const PlateRecordingPair&) const = default;
};

[[nodiscard]] PlateIncidentFieldSet collectPlateIncidentFields(
    const scene::BenchScene& bench,
    const scene::BenchTraceGraph& traceGraph,
    std::string plateComponentId);

[[nodiscard]] PlateRecordingPair makePlateRecordingPair(
    const PlateIncidentFieldSet& fields,
    std::uint64_t objectBranchId,
    std::uint64_t referenceBranchId);

} // namespace holobench::optics::holography
