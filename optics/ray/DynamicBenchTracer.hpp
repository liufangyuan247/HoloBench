#pragma once

#include "optics/material/CoatingResponse.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/scene/BenchInteraction.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::ray {

struct DynamicBenchCalibrationContext final {
    const material::ICoatingResponseResolver* coatingResponses = nullptr;
    double temperatureKelvin = 293.15;
};

/**
 * Traces one centre ray per laser spectral channel through the dynamic bench.
 * This is the deterministic layout graph; local sampled wave fields are a
 * separate M7.3 adapter and are not approximated here.
 */
[[nodiscard]] scene::BenchTraceGraph traceDynamicBench(
    const scene::BenchScene& bench,
    const scene::TraceBudget& budget = {},
    const ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const DynamicBenchCalibrationContext& calibration = {});

// Traces one solver-derived beam emitted from an ordinary placed component.
// This is used for fields created by an interaction such as hologram replay;
// the seed's provenance must contain exactly that source component. The same
// component intersections, prescription resolver, branch budgets, and visible
// path evidence used by source tracing remain authoritative.
[[nodiscard]] scene::BenchTraceGraph traceDerivedBenchBeam(
    const scene::BenchScene& bench,
    const scene::BeamState& seed,
    const scene::TraceBudget& budget = {},
    const ILensPrescriptionResolver* lensPrescriptions = nullptr,
    const DynamicBenchCalibrationContext& calibration = {});

} // namespace holobench::optics::ray
