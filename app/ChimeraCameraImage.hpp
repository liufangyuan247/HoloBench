#pragma once

#include <array>
#include <cstddef>
#include <string>
#include <vector>

#include "app/ChimeraReconstruction.hpp"
#include "optics/sensor/CameraSpectralResponse.hpp"

namespace holobench::app::chimera {

inline constexpr int kCameraImageRequestFormatVersion = 1;
inline constexpr std::size_t kMaximumCameraImagePixels = 4U * 1024U * 1024U;
inline constexpr std::size_t kMaximumCameraPsfSupportRadiusPixels = 128U;
inline constexpr std::size_t kMaximumCameraKernelEvaluations = 100'000'000U;
inline constexpr std::size_t kMaximumCameraDirectionalSamples = 100'000U;

struct CameraImageRequest final {
    int formatVersion = kCameraImageRequestFormatVersion;
    std::string jobId = "chimera-camera-image";
    double pupilCentreXMetres = 0.0;
    double pupilCentreYMetres = 0.0;
    double pupilPlaneDistanceMetres = 0.10;
    double pupilDiameterMetres = 2e-3;
    double focalLengthMetres = 25e-3;
    std::size_t pixelWidth = 256;
    std::size_t pixelHeight = 256;
    double pixelPitchXMetres = 10e-6;
    double pixelPitchYMetres = 10e-6;
    double psfSupportFirstDarkRings = 4.0;

    bool operator==(const CameraImageRequest&) const = default;
};

struct CameraRayContribution final {
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    std::string viewId;
    double pupilIntersectionXMetres = 0.0;
    double pupilIntersectionYMetres = 0.0;
    bool enteredPupil = false;
    double sensorCentreXMetres = 0.0;
    double sensorCentreYMetres = 0.0;
    bool depositedOnSensor = false;
    LinearRgb idealSensorSignal;
    LinearRgb depositedSensorSignal;

    bool operator==(const CameraRayContribution&) const = default;
};

struct CameraImageMetrics final {
    std::size_t directionalSampleCount = 0;
    std::size_t pupilAcceptedSampleCount = 0;
    std::size_t pupilRejectedSampleCount = 0;
    std::size_t sensorDepositedSampleCount = 0;
    std::size_t sensorMissedSampleCount = 0;
    std::size_t kernelEvaluationCount = 0;
    double maximumFirstDarkRadiusMetres = 0.0;
    double maximumPsfSupportRadiusPixels = 0.0;
    std::array<double, 3> rgbFirstDarkRadiusMetres {};
    std::array<double, 3> rgbPsfSupportRadiusPixelsX {};
    std::array<double, 3> rgbPsfSupportRadiusPixelsY {};
    LinearRgb totalIdealSensorSignal;
    LinearRgb totalDepositedSensorSignal;

    bool operator==(const CameraImageMetrics&) const = default;
};

struct CameraImageResult final {
    int requestFormatVersion = kCameraImageRequestFormatVersion;
    std::string jobId;
    std::string sourceReconstructionJobId;
    std::string sourceRecipeId;
    std::string cameraCalibrationId;
    double pupilCentreXMetres = 0.0;
    double pupilCentreYMetres = 0.0;
    double pupilPlaneDistanceMetres = 0.0;
    double pupilDiameterMetres = 0.0;
    double focalLengthMetres = 0.0;
    std::size_t pixelWidth = 0;
    std::size_t pixelHeight = 0;
    double pixelPitchXMetres = 0.0;
    double pixelPitchYMetres = 0.0;
    double psfSupportFirstDarkRings = 0.0;
    std::vector<LinearRgb> rowMajorLinearSensorSignal;
    std::vector<CameraRayContribution> contributions;
    CameraImageMetrics metrics;
    std::vector<std::string> limitations;

    [[nodiscard]] const LinearRgb& at(
        std::size_t column,
        std::size_t row) const;
    bool operator==(const CameraImageResult&) const = default;
};

// Composes the bounded directional reconstruction into an ideal on-axis camera.
// A ray must intersect the finite circular pupil. Its angle selects a sensor
// position, measured spectral response maps each optical wavelength into sensor
// RGB, and a wavelength-specific circular-pupil Airy PSF deposits the signal.
[[nodiscard]] CameraImageResult synthesizeCameraImage(
    const ChimeraRecipe& recipe,
    const ReconstructionResult& reconstruction,
    const CameraImageRequest& request,
    const optics::sensor::CalibratedCameraSpectralResponse& cameraResponse);

} // namespace holobench::app::chimera
