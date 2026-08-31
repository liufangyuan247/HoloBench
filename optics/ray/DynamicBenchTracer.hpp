#pragma once

#include "optics/scene/BenchInteraction.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::optics::ray {

/**
 * Traces one centre ray per laser spectral channel through the dynamic bench.
 * This is the deterministic layout graph; local sampled wave fields are a
 * separate M7.3 adapter and are not approximated here.
 */
[[nodiscard]] scene::BenchTraceGraph traceDynamicBench(
    const scene::BenchScene& bench,
    const scene::TraceBudget& budget = {});

} // namespace holobench::optics::ray
