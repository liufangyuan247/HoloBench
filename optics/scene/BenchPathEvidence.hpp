#pragma once

#include <string>
#include <vector>

#include "optics/scene/BenchInteraction.hpp"

namespace holobench::optics::scene {

// Ordered, branch-connected evidence for one source-to-terminal route. This is
// solver truth recovered from the trace graph, not a UI-authored connection.
struct BenchPathInteraction final {
    std::string componentId;
    math::Vec3d hitPointMetres {};
    BeamState incidentBeam;
    bool hasOutgoingBeam = false;
    BeamState outgoingBeam;

    bool operator==(const BenchPathInteraction&) const = default;
};

// Reconstructs the exact source-to-terminal interaction chain for one terminal
// interaction. At splitters the selected outgoing branch, rather than only the
// component-ID prefix, determines the parent interaction. The terminal itself
// is the final result entry and has no outgoing beam.
[[nodiscard]] std::vector<BenchPathInteraction> collectBenchPathInteractions(
    const BenchTraceGraph& traceGraph,
    const OpticalInteraction& terminalInteraction);

} // namespace holobench::optics::scene
