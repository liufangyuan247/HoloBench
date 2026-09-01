#include "optics/scene/BenchPathEvidence.hpp"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <stdexcept>
#include <utility>

namespace holobench::optics::scene {
namespace {

bool hasExactPrefix(
    const std::vector<std::string>& candidate,
    const std::vector<std::string>& path,
    std::size_t prefixSize) {
    return candidate.size() == prefixSize
        && path.size() >= prefixSize
        && std::equal(candidate.begin(), candidate.end(), path.begin());
}

bool isTerminalMatch(
    const OpticalInteraction& candidate,
    const OpticalInteraction& terminal) {
    return candidate.componentId == terminal.componentId
        && candidate.incidentBeam.provenance.branchId
            == terminal.incidentBeam.provenance.branchId
        && candidate.incidentBeam.provenance.componentPath
            == terminal.incidentBeam.provenance.componentPath;
}

} // namespace

std::vector<BenchPathInteraction> collectBenchPathInteractions(
    const BenchTraceGraph& traceGraph,
    const OpticalInteraction& terminalInteraction) {
    validateBeamState(terminalInteraction.incidentBeam);
    const auto& componentPath
        = terminalInteraction.incidentBeam.provenance.componentPath;
    if (componentPath.size() < 2U
        || componentPath.back() != terminalInteraction.componentId) {
        throw std::invalid_argument(
            "terminal branch has no complete traced source-to-terminal path");
    }

    const OpticalInteraction* tracedTerminal = nullptr;
    for (const auto& candidate : traceGraph.interactions) {
        if (!isTerminalMatch(candidate, terminalInteraction)) {
            continue;
        }
        if (tracedTerminal != nullptr) {
            throw std::invalid_argument(
                "terminal branch does not identify exactly one trace-graph interaction");
        }
        tracedTerminal = &candidate;
    }
    if (tracedTerminal == nullptr) {
        throw std::invalid_argument(
            "terminal branch does not identify exactly one trace-graph interaction");
    }
    if (tracedTerminal->incidentBeam != terminalInteraction.incidentBeam
        || tracedTerminal->hitPointMetres
            != terminalInteraction.hitPointMetres) {
        throw std::invalid_argument(
            "terminal branch selector differs from authoritative trace evidence");
    }

    std::vector<const OpticalInteraction*> ordered(componentPath.size() - 1U);
    ordered.back() = tracedTerminal;
    for (std::size_t pathIndex = componentPath.size() - 1U;
         pathIndex > 1U; --pathIndex) {
        const auto* next = ordered[pathIndex - 1U];
        const std::uint64_t nextBranchId
            = next->incidentBeam.provenance.branchId;
        const OpticalInteraction* connected = nullptr;
        for (const auto& candidate : traceGraph.interactions) {
            if (candidate.componentId != componentPath[pathIndex - 1U]
                || !hasExactPrefix(
                    candidate.incidentBeam.provenance.componentPath,
                    componentPath,
                    pathIndex)) {
                continue;
            }
            const auto outgoing = std::find_if(
                candidate.outgoing.begin(),
                candidate.outgoing.end(),
                [nextBranchId](const OutgoingBeam& beam) {
                    return beam.beam.provenance.branchId == nextBranchId;
                });
            if (outgoing == candidate.outgoing.end()) {
                continue;
            }
            if (connected != nullptr) {
                throw std::invalid_argument(
                    "terminal branch path has ambiguous connected interaction evidence");
            }
            connected = &candidate;
        }
        if (connected == nullptr) {
            throw std::invalid_argument(
                "terminal branch path is missing connected interaction evidence");
        }
        ordered[pathIndex - 2U] = connected;
    }

    std::vector<BenchPathInteraction> result;
    result.reserve(ordered.size());
    for (std::size_t index = 0; index < ordered.size(); ++index) {
        const auto& interaction = *ordered[index];
        validateBeamState(interaction.incidentBeam);
        BenchPathInteraction evidence {
            .componentId = interaction.componentId,
            .hitPointMetres = interaction.hitPointMetres,
            .incidentBeam = interaction.incidentBeam,
            .hasOutgoingBeam = false,
            .outgoingBeam = {},
        };
        if (index + 1U < ordered.size()) {
            const std::uint64_t nextBranchId
                = ordered[index + 1U]->incidentBeam.provenance.branchId;
            const auto outgoing = std::find_if(
                interaction.outgoing.begin(),
                interaction.outgoing.end(),
                [nextBranchId](const OutgoingBeam& candidate) {
                    return candidate.beam.provenance.branchId == nextBranchId;
                });
            if (outgoing == interaction.outgoing.end()) {
                throw std::logic_error(
                    "ordered Bench path lost its connected outgoing beam");
            }
            validateBeamState(outgoing->beam);
            evidence.hasOutgoingBeam = true;
            evidence.outgoingBeam = outgoing->beam;
        }
        result.push_back(std::move(evidence));
    }
    return result;
}

} // namespace holobench::optics::scene
