#pragma once

#include <cstddef>
#include <string>

#include "compute/propagation/AngularSpectrumPropagator.hpp"
#include "compute/propagation/TiltedPlanePropagator.hpp"
#include "core/field/ComplexField2D.hpp"
#include "optics/scene/BenchInteraction.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app {

struct BenchWaveObservationResult final {
    std::string sourceComponentId;
    std::string apertureComponentId;
    std::string observationComponentId;
    optics::scene::SceneRevision sourceRevision = 0;
    bool interactivePreview = false;
    double signedPropagationDistanceMetres = 0.0;
    double observationOffsetXMetres = 0.0;
    double observationOffsetYMetres = 0.0;
    bool usedShiftedPaddedPropagation = false;
    bool usedTiltedPlanePropagation = false;
    field::ComplexField2D fieldAtObservation;
    compute::propagation::AngularSpectrumDiagnostics propagation;
    compute::propagation::TiltedPlaneDiagnostics tiltedPropagation;

    [[nodiscard]] bool isStaleFor(
        const optics::scene::BenchScene& bench) const noexcept;
};

// Observes a current Laser -> Aperture route on an ordinary freely placed
// Screen / Detector or non-destructive Field Probe. Rays establish the global
// route; the aperture and free-space propagation are evaluated as a bounded
// local 2-D complex field.
[[nodiscard]] BenchWaveObservationResult observeBenchWavePattern(
    const optics::scene::BenchScene& bench,
    const optics::scene::BenchTraceGraph& traceGraph,
    std::string observationComponentId,
    std::size_t maximumSamplesPerAxis,
    bool interactivePreview,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app
