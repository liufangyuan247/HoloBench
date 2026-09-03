#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/math/RigidTransform.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::scene {

struct BranchProvenance final {
    std::uint64_t branchId = 0;
    std::uint64_t parentBranchId = 0;
    std::vector<std::string> componentPath;

    bool operator==(const BranchProvenance&) const = default;
};

struct BeamState final {
    double wavelengthMetres = 532e-9;
    double powerWatts = 1.0;
    double phaseRadians = 0.0;
    std::string coherenceId = "laser-1";
    double accumulatedOpticalPathMetres = 0.0;
    math::Vec3d originMetres {};
    math::Vec3d direction {0.0, 0.0, 1.0};
    math::RigidTransform3d localFrame {};
    BranchProvenance provenance {};

    bool operator==(const BeamState&) const = default;
};

enum class BranchInteractionKind {
    Reflected,
    Transmitted,
};

struct OutgoingBeam final {
    BranchInteractionKind interaction = BranchInteractionKind::Transmitted;
    BeamState beam;

    bool operator==(const OutgoingBeam&) const = default;
};

struct OpticalInteraction final {
    std::string componentId;
    math::Vec3d hitPointMetres {};
    double distanceMetres = 0.0;
    BeamState incidentBeam;
    std::vector<OutgoingBeam> outgoing;
    std::vector<std::string> diagnostics;

    bool operator==(const OpticalInteraction&) const = default;
};

enum class TraceTerminationReason {
    EscapedScene,
    Absorbed,
    MinimumPower,
    HopLimit,
    BranchLimit,
    InvalidInteraction,
};

struct BenchTraceSegment final {
    std::uint64_t branchId = 0;
    math::Vec3d startMetres {};
    math::Vec3d endMetres {};
    double wavelengthMetres = 532e-9;
    double powerWatts = 0.0;

    bool operator==(const BenchTraceSegment&) const = default;
};

struct BenchTraceTermination final {
    std::uint64_t branchId = 0;
    TraceTerminationReason reason = TraceTerminationReason::EscapedScene;
    std::string detail;

    bool operator==(const BenchTraceTermination&) const = default;
};

struct BenchTraceGraph final {
    SceneRevision sourceRevision = 0;
    std::vector<BenchTraceSegment> segments;
    std::vector<OpticalInteraction> interactions;
    std::vector<BenchTraceTermination> terminations;

    bool operator==(const BenchTraceGraph&) const = default;
};

struct TraceBudget final {
    std::size_t maximumHopsPerBranch = 64;
    std::size_t maximumBranches = 1024;
    double minimumPowerWatts = 1e-12;
    double escapeDistanceMetres = 2.0;
};

void validateBeamState(const BeamState& beam);
void validateTraceBudget(const TraceBudget& budget);

[[nodiscard]] OpticalInteraction interactIdealBeamSplitter(
    const BeamState& incoming,
    const BenchComponent& splitter,
    math::Vec3d hitPointMetres,
    std::uint64_t reflectedBranchId,
    std::uint64_t transmittedBranchId);

[[nodiscard]] OpticalInteraction interactIdealXCubeCombiner(
    const BeamState& incoming,
    const BenchComponent& combiner,
    math::Vec3d hitPointMetres,
    std::uint64_t outputBranchId);

[[nodiscard]] bool canInterfere(const BeamState& first, const BeamState& second) noexcept;

} // namespace holobench::optics::scene
