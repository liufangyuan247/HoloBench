#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "optics/holography/PlateIncidentFields.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::optics::holography {

struct PlateFieldSamplingOptions final {
    std::size_t sampleWidth = 128;
    std::size_t sampleHeight = 128;
    double refractiveIndex = 1.0;
    // Zero extent selects the complete physical plate axis. Non-zero extents
    // select an explicit plate-local analysis window without changing the
    // physical plate dimensions or pretending the complete plate is sampled.
    double extentWidthMetres = 0.0;
    double extentHeightMetres = 0.0;
    double centreXMetres = 0.0;
    double centreYMetres = 0.0;
};

struct PlateFieldSamplingDiagnostics final {
    std::size_t illuminatedSampleCount = 0;
    double integratedPowerWatts = 0.0;
    double transverseFrequencyXCyclesPerMetre = 0.0;
    double transverseFrequencyYCyclesPerMetre = 0.0;
    double nyquistXCyclesPerMetre = 0.0;
    double nyquistYCyclesPerMetre = 0.0;
    double sampledExtentWidthMetres = 0.0;
    double sampledExtentHeightMetres = 0.0;
    double sampledCentreXMetres = 0.0;
    double sampledCentreYMetres = 0.0;
    bool usesLocalAnalysisWindow = false;
    bool carrierSampled = false;
    bool supportTouchesPlateBoundary = false;
    bool usesApproximateSourceEnvelope = false;
    bool appliedCoaxialWavePath = false;
    std::vector<std::string> appliedWaveComponentIds;
    std::vector<std::string> warnings;
};

struct SampledPlateIncidentField final {
    std::string plateComponentId;
    scene::SceneRevision sourceRevision = 0;
    std::uint64_t branchId = 0;
    RecordingBranchRole role = RecordingBranchRole::Reference;
    PlateIncidenceSide side = PlateIncidenceSide::NegativeLocalZ;
    field::ComplexField2D field;
    PlateFieldSamplingDiagnostics diagnostics;

    [[nodiscard]] bool isStaleFor(const scene::BenchScene& bench) const noexcept;
};

// Converts centreline branch evidence into a finite, plate-local sampled field.
// Field magnitude is normalized in sqrt(W/m^2 normal to propagation). The
// complete analytic source profile carries the branch power; a finite plate or
// local analysis window integrates only the power that it actually intercepts.
// Mirrors and ideal splitters are already represented by direction, phase/path,
// and branch power.
// This overload intentionally returns the direct centreline/source-envelope
// evidence and reports every component that still needs wave refinement.
[[nodiscard]] SampledPlateIncidentField samplePlateIncidentField(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    const PlateFieldSamplingOptions& options = {});

// Refines a supported coaxial, normally incident component path with sampled
// propagation and local thin-element transforms. Unsupported tilted,
// decentered, folded, or prescription-lens paths fall back to the explicit
// centreline-envelope result and retain refinement warnings.
[[nodiscard]] SampledPlateIncidentField samplePlateIncidentField(
    const scene::BenchScene& bench,
    const PlateIncidentFieldSet& fields,
    std::uint64_t branchId,
    const PlateFieldSamplingOptions& options,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::optics::holography
