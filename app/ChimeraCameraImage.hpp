#pragma once

#include <array>
#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/ChimeraReconstruction.hpp"
#include "optics/ray/LensPrescriptionCatalog.hpp"
#include "optics/scene/BenchScene.hpp"
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

// Raster/readout controls for a camera whose optical truth comes from placed
// Bench components. Unlike CameraImageRequest, this request contains no pupil,
// focal-length, or pose knobs: those are resolved from the RealLensAssembly,
// its immutable prescription, and the placed sensor plane.
struct CameraSensorRequest final {
    int formatVersion = kCameraImageRequestFormatVersion;
    std::string jobId = "chimera-placed-camera-image";
    std::size_t pixelWidth = 256;
    std::size_t pixelHeight = 256;
    double pixelPitchXMetres = 10e-6;
    double pixelPitchYMetres = 10e-6;
    double psfSupportFirstDarkRings = 4.0;

    bool operator==(const CameraSensorRequest&) const = default;
};

struct CameraSpectralRayEvidence final {
    double wavelengthMetres = 0.0;
    std::string prescriptionTraceStatus;
    std::size_t prescriptionSurfaceCount = 0;
    double firstSurfaceXMetres = 0.0;
    double firstSurfaceYMetres = 0.0;
    bool enteredPupil = false;
    bool intersectedSensorPlane = false;
    double sensorCentreXMetres = 0.0;
    double sensorCentreYMetres = 0.0;
    bool depositedOnSensor = false;
    LinearRgb idealSensorSignal;
    LinearRgb depositedSensorSignal;

    bool operator==(const CameraSpectralRayEvidence&) const = default;
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
    std::vector<CameraSpectralRayEvidence> spectralRays;

    bool operator==(const CameraRayContribution&) const = default;
};

struct CameraImageMetrics final {
    std::size_t directionalSampleCount = 0;
    std::size_t pupilAcceptedSampleCount = 0;
    std::size_t pupilRejectedSampleCount = 0;
    std::size_t sensorDepositedSampleCount = 0;
    std::size_t sensorMissedSampleCount = 0;
    std::size_t kernelEvaluationCount = 0;
    std::size_t prescriptionTraceCompletedCount = 0;
    std::size_t prescriptionTraceRejectedCount = 0;
    std::size_t sensorPlaneMissedChannelCount = 0;
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
    bool usedPlacedSequentialLens = false;
    std::uint64_t sourceSceneRevision = 0;
    std::string sourcePlateComponentId;
    std::string lensComponentId;
    std::string lensPrescriptionId;
    std::string observationComponentId;
    double placedClearApertureDiameterMetres = 0.0;
    std::array<double, 3> rgbEffectiveFocalLengthMetres {};
    std::array<double, 3> rgbSensorAxialDistanceMetres {};
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
    [[nodiscard]] bool isStaleFor(
        const optics::scene::BenchScene& bench) const noexcept;
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

// Traces every RGB directional sample from the placed holographic plate
// through the placed sequential lens prescription and onto the placed sensor
// plane. Physical clipping is retained as per-channel evidence; missing or
// ambiguous optical truth is rejected instead of falling back to the ideal
// camera above.
[[nodiscard]] CameraImageResult synthesizePlacedCameraImage(
    const ChimeraRecipe& recipe,
    const ReconstructionResult& reconstruction,
    const CameraSensorRequest& request,
    const optics::sensor::CalibratedCameraSpectralResponse& cameraResponse,
    const optics::scene::BenchScene& bench,
    std::string_view sourcePlateComponentId,
    std::string_view lensComponentId,
    std::string_view observationComponentId,
    const optics::ray::ILensPrescriptionResolver& lensPrescriptions);

} // namespace holobench::app::chimera
