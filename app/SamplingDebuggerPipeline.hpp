#pragma once

#include <cstddef>
#include <optional>
#include <vector>

#include "compute/fourier/PsfMtf.hpp"
#include "compute/sampling/AngularSpectrumAnalysis.hpp"
#include "compute/sampling/PlaneProbe.hpp"
#include "compute/sampling/SamplingDiagnostics.hpp"
#include "core/field/FieldVisualization.hpp"

namespace holobench::field {
class ComplexField2D;
}

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::samplingdebug {

struct SamplingDebuggerConfig final {
    double propagationDistanceMetres = 0.0;
    double requestedHalfAngleXRadians = 0.0;
    double requestedHalfAngleYRadians = 0.0;
    std::optional<double> illuminatedExtentXMetres;
    std::optional<double> illuminatedExtentYMetres;
    std::size_t minimumBoundaryGuardSamples = 4U;
    std::size_t probeXIndex = 0U;
    std::size_t probeYIndex = 0U;
    std::vector<double> probeDistancesMetres {0.0};
    double psfFocalLengthMetres = 0.050;
    double psfPupilRadiusMetres = 0.5e-3;
    std::size_t psfGridResolution = 65U;
    double psfSamplesPerFirstDarkRadius = 8.0;
    std::size_t mtfSampleCount = 129U;
    double mtfMaximumCutoffMultiple = 1.2;
    double spectrumFloorDecibels = -60.0;
};

struct SamplingDebuggerResult final {
    compute::sampling::SamplingDiagnostics sampling;
    compute::sampling::AngularSpectrumAnalysis angularSpectrum;
    compute::sampling::PlaneProbeResult planeProbe;
    compute::fourier::CircularPupilTransferDiagnostics pupilDiagnostics;
    field::ScalarField2D normalizedPsf;
    std::vector<compute::fourier::RadialMtfSample> incoherentMtf;
    field::RgbaImage angularSpectrumImage;
};

[[nodiscard]] field::RgbaImage renderAngularSpectrumClassification(
    const compute::sampling::AngularSpectrumAnalysis& analysis,
    double floorDecibels = -60.0);

[[nodiscard]] SamplingDebuggerResult analyzeSamplingDebugger(
    const field::ComplexField2D& selectedPlane,
    const SamplingDebuggerConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::samplingdebug
