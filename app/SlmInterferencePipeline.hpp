#pragma once

#include <complex>
#include <cstddef>
#include <optional>
#include <vector>

#include "compute/fourier/FourierOptics.hpp"
#include "core/field/ScalarField2D.hpp"
#include "optics/slm/SpatialLightModulator.hpp"
#include "optics/slm/SlmResponse.hpp"
#include "optics/wave/CoherentInterference.hpp"
#include "optics/wave/FieldSources.hpp"

namespace holobench::compute::fft {
class IFftBackend;
}

namespace holobench::app::slmexperiment {

enum class SlmDeviceResponseModel {
    Ideal,
    CalibratedLut,
    LcdTeaching,
};

struct SlmInterferenceExperimentConfig final {
    std::size_t fieldWidth = 256;
    std::size_t fieldHeight = 256;
    double fieldPitchXMetres = 2e-6;
    double fieldPitchYMetres = 2e-6;
    double refractiveIndex = 1.0;
    std::vector<double> vacuumWavelengthsMetres{450e-9, 532e-9, 638e-9};
    std::complex<double> laserAmplitude{1.0, 0.0};
    double lensFocalLengthMetres = 0.050;

    optics::slm::PixelatedSlmParameters slm;
    SlmDeviceResponseModel deviceResponseModel = SlmDeviceResponseModel::Ideal;
    std::optional<optics::slm::CalibratedSlmResponse> calibratedResponse;
    optics::slm::LcdTeachingParameters lcdTeaching;
    // Empty means all-one commands. Otherwise the size must match the SLM.
    std::vector<double> normalizedPixelCommands;
    std::size_t selectedPixelColumn = 0;
    std::size_t selectedPixelRow = 0;

    optics::wave::PlaneWaveParameters referenceBeam;
    optics::wave::MutualCoherenceParameters mutualCoherence;
};

struct AngularAxes final {
    // Scalar Fourier mapping sin(theta) = x/f (and y/f). Values outside the
    // propagating interval have NaN angles and remain visible as sampling data.
    std::vector<double> directionCosinesX;
    std::vector<double> directionCosinesY;
    std::vector<double> anglesXRadians;
    std::vector<double> anglesYRadians;
};

struct SelectedPixelAngleMapping final {
    double geometricCenterXMetres = 0.0;
    double geometricCenterYMetres = 0.0;
    double sampledActiveCentroidXMetres = 0.0;
    double sampledActiveCentroidYMetres = 0.0;
    double predictedDirectionCosineX = 0.0;
    double predictedDirectionCosineY = 0.0;
    double sampledPredictedDirectionCosineX = 0.0;
    double sampledPredictedDirectionCosineY = 0.0;
    double measuredDirectionCosineX = 0.0;
    double measuredDirectionCosineY = 0.0;
    double measuredPropagatingSpectralEnergyFraction = 0.0;
};

struct SlmWavelengthExperimentResult final {
    double vacuumWavelengthMetres = 0.0;
    field::ComplexField2D modulatedSlmPlane;
    optics::slm::SlmApplicationDiagnostics modulationDiagnostics;
    compute::fourier::FourierPlaneResult angularDistribution;
    field::ScalarField2D normalizedAngularIntensity;
    compute::fourier::FourierPlaneResult selectedPixelAngularField;
    field::ScalarField2D normalizedAngularPsf;
    AngularAxes angularAxes;
    SelectedPixelAngleMapping selectedPixelMapping;
    optics::wave::InterferenceResult interference;
};

struct SlmInterferenceExperimentResult final {
    std::vector<SlmWavelengthExperimentResult> wavelengths;
};

[[nodiscard]] SlmInterferenceExperimentConfig makeDefaultSlmInterferenceExperimentConfig();

[[nodiscard]] SlmInterferenceExperimentResult runSlmInterferenceExperiment(
    const SlmInterferenceExperimentConfig& config,
    compute::fft::IFftBackend& fftBackend);

} // namespace holobench::app::slmexperiment
