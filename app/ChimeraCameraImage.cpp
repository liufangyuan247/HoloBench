#include "app/ChimeraCameraImage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fourier/PsfMtf.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

struct PsfChannel final {
    double wavelengthMetres = 0.0;
    compute::fourier::CircularPupilPsfMtf airy;
    optics::sensor::CameraRgbResponse cameraResponse;
    double supportRadiusMetres = 0.0;
    std::size_t supportRadiusColumns = 0;
    std::size_t supportRadiusRows = 0;
};

void requireFinite(double value, std::string_view name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

void requirePositive(double value, std::string_view name) {
    requireFinite(value, name);
    if (value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive");
    }
}

void validateLinearRgb(const LinearRgb& value, std::string_view name) {
    requireFinite(value.red, name);
    requireFinite(value.green, name);
    requireFinite(value.blue, name);
    if (value.red < 0.0 || value.green < 0.0 || value.blue < 0.0) {
        throw std::invalid_argument(
            std::string(name) + " must be non-negative");
    }
}

void addSignal(LinearRgb& destination, const LinearRgb& value) {
    destination.red += value.red;
    destination.green += value.green;
    destination.blue += value.blue;
    if (!std::isfinite(destination.red)
        || !std::isfinite(destination.green)
        || !std::isfinite(destination.blue)) {
        throw std::overflow_error("camera sensor signal is not representable");
    }
}

LinearRgb scaledResponse(
    double incidentIntensity,
    const optics::sensor::CameraRgbResponse& response) {
    const LinearRgb result {
        .red = incidentIntensity * response.red,
        .green = incidentIntensity * response.green,
        .blue = incidentIntensity * response.blue,
    };
    validateLinearRgb(result, "camera response signal");
    return result;
}

void validateRequest(const CameraImageRequest& request) {
    if (request.formatVersion != kCameraImageRequestFormatVersion
        || !optics::scene::isStableBenchId(request.jobId)) {
        throw std::invalid_argument("camera image request identity is invalid");
    }
    requireFinite(request.pupilCentreXMetres, "camera pupil centre x");
    requireFinite(request.pupilCentreYMetres, "camera pupil centre y");
    requirePositive(
        request.pupilPlaneDistanceMetres, "camera pupil-plane distance");
    requirePositive(request.pupilDiameterMetres, "camera pupil diameter");
    requirePositive(request.focalLengthMetres, "camera focal length");
    requirePositive(request.pixelPitchXMetres, "camera pixel pitch x");
    requirePositive(request.pixelPitchYMetres, "camera pixel pitch y");
    requirePositive(
        request.psfSupportFirstDarkRings, "camera PSF support rings");
    if (request.psfSupportFirstDarkRings > 16.0
        || request.pixelWidth == 0U || request.pixelHeight == 0U
        || request.pixelWidth > kMaximumCameraImagePixels
            / request.pixelHeight) {
        throw std::invalid_argument("camera image dimensions or PSF support are invalid");
    }
}

std::array<PsfChannel, 3> makePsfChannels(
    const ChimeraRecipe& recipe,
    const CameraImageRequest& request,
    const optics::sensor::CalibratedCameraSpectralResponse& cameraResponse,
    CameraImageMetrics& metrics) {
    std::array<PsfChannel, 3> result {{
        {
            .wavelengthMetres = recipe.rgb[0].wavelengthMetres,
            .airy = compute::fourier::CircularPupilPsfMtf(
                recipe.rgb[0].wavelengthMetres, 1.0,
                request.focalLengthMetres,
                0.5 * request.pupilDiameterMetres),
            .cameraResponse = {},
            .supportRadiusMetres = 0.0,
            .supportRadiusColumns = 0U,
            .supportRadiusRows = 0U,
        },
        {
            .wavelengthMetres = recipe.rgb[1].wavelengthMetres,
            .airy = compute::fourier::CircularPupilPsfMtf(
                recipe.rgb[1].wavelengthMetres, 1.0,
                request.focalLengthMetres,
                0.5 * request.pupilDiameterMetres),
            .cameraResponse = {},
            .supportRadiusMetres = 0.0,
            .supportRadiusColumns = 0U,
            .supportRadiusRows = 0U,
        },
        {
            .wavelengthMetres = recipe.rgb[2].wavelengthMetres,
            .airy = compute::fourier::CircularPupilPsfMtf(
                recipe.rgb[2].wavelengthMetres, 1.0,
                request.focalLengthMetres,
                0.5 * request.pupilDiameterMetres),
            .cameraResponse = {},
            .supportRadiusMetres = 0.0,
            .supportRadiusColumns = 0U,
            .supportRadiusRows = 0U,
        },
    }};
    for (std::size_t index = 0; index < result.size(); ++index) {
        auto& channel = result[index];
        channel.cameraResponse = cameraResponse.evaluate(
            channel.wavelengthMetres).relativeSensorResponse;
        const double firstDark = channel.airy.diagnostics().firstDarkRadiusMetres;
        channel.supportRadiusMetres
            = firstDark * request.psfSupportFirstDarkRings;
        channel.supportRadiusColumns = static_cast<std::size_t>(std::ceil(
            channel.supportRadiusMetres / request.pixelPitchXMetres + 0.5));
        channel.supportRadiusRows = static_cast<std::size_t>(std::ceil(
            channel.supportRadiusMetres / request.pixelPitchYMetres + 0.5));
        if (channel.supportRadiusColumns
                > kMaximumCameraPsfSupportRadiusPixels
            || channel.supportRadiusRows
                > kMaximumCameraPsfSupportRadiusPixels) {
            throw std::invalid_argument(
                "camera Airy support exceeds the bounded pixel radius");
        }
        metrics.maximumFirstDarkRadiusMetres = std::max(
            metrics.maximumFirstDarkRadiusMetres, firstDark);
        metrics.maximumPsfSupportRadiusPixels = std::max(
            metrics.maximumPsfSupportRadiusPixels,
            std::max(channel.supportRadiusMetres / request.pixelPitchXMetres,
                channel.supportRadiusMetres / request.pixelPitchYMetres));
        metrics.rgbFirstDarkRadiusMetres[index] = firstDark;
        metrics.rgbPsfSupportRadiusPixelsX[index]
            = channel.supportRadiusMetres / request.pixelPitchXMetres;
        metrics.rgbPsfSupportRadiusPixelsY[index]
            = channel.supportRadiusMetres / request.pixelPitchYMetres;
    }
    return result;
}

double opticalChannelIntensity(const LinearRgb& value, std::size_t index) {
    switch (index) {
    case 0U: return value.red;
    case 1U: return value.green;
    case 2U: return value.blue;
    default: throw std::logic_error("invalid optical RGB channel index");
    }
}

struct WeightedPixel final {
    std::size_t index = 0;
    double weight = 0.0;
};

LinearRgb depositPsf(
    CameraImageResult& result,
    const CameraImageRequest& request,
    const PsfChannel& channel,
    double sensorX,
    double sensorY,
    const LinearRgb& signal) {
    const double centreColumn = sensorX / request.pixelPitchXMetres
        + 0.5 * static_cast<double>(request.pixelWidth) - 0.5;
    const double centreRow = 0.5 * static_cast<double>(request.pixelHeight)
        - sensorY / request.pixelPitchYMetres - 0.5;
    const long long columnCentre = static_cast<long long>(
        std::llround(centreColumn));
    const long long rowCentre = static_cast<long long>(std::llround(centreRow));
    const long long columnRadius = static_cast<long long>(
        channel.supportRadiusColumns);
    const long long rowRadius = static_cast<long long>(
        channel.supportRadiusRows);
    std::vector<WeightedPixel> inside;
    inside.reserve(static_cast<std::size_t>(
        (2LL * columnRadius + 1LL) * (2LL * rowRadius + 1LL)));
    double normalization = 0.0;
    for (long long row = rowCentre - rowRadius;
         row <= rowCentre + rowRadius;
         ++row) {
        for (long long column = columnCentre - columnRadius;
             column <= columnCentre + columnRadius;
             ++column) {
            ++result.metrics.kernelEvaluationCount;
            const double pixelX = (static_cast<double>(column) + 0.5
                    - 0.5 * static_cast<double>(request.pixelWidth))
                * request.pixelPitchXMetres;
            const double pixelY = (0.5 * static_cast<double>(request.pixelHeight)
                    - static_cast<double>(row) - 0.5)
                * request.pixelPitchYMetres;
            const double radius = std::hypot(pixelX - sensorX, pixelY - sensorY);
            if (radius > channel.supportRadiusMetres) continue;
            const double weight = channel.airy.normalizedIntensityPsf(radius);
            normalization += weight;
            if (row >= 0 && column >= 0
                && row < static_cast<long long>(request.pixelHeight)
                && column < static_cast<long long>(request.pixelWidth)) {
                inside.push_back({
                    .index = static_cast<std::size_t>(row)
                            * request.pixelWidth
                        + static_cast<std::size_t>(column),
                    .weight = weight,
                });
            }
        }
    }
    if (!std::isfinite(normalization) || normalization <= 0.0) {
        throw std::runtime_error("camera Airy kernel has no sampled support");
    }
    LinearRgb deposited;
    for (const auto& pixel : inside) {
        const double normalized = pixel.weight / normalization;
        const LinearRgb share {
            .red = signal.red * normalized,
            .green = signal.green * normalized,
            .blue = signal.blue * normalized,
        };
        addSignal(result.rowMajorLinearSensorSignal[pixel.index], share);
        addSignal(deposited, share);
    }
    return deposited;
}

bool psfMayReachSensor(
    const CameraImageRequest& request,
    const PsfChannel& channel,
    double sensorX,
    double sensorY) {
    const double halfWidth = 0.5 * static_cast<double>(request.pixelWidth)
        * request.pixelPitchXMetres;
    const double halfHeight = 0.5 * static_cast<double>(request.pixelHeight)
        * request.pixelPitchYMetres;
    return sensorX + channel.supportRadiusMetres >= -halfWidth
        && sensorX - channel.supportRadiusMetres <= halfWidth
        && sensorY + channel.supportRadiusMetres >= -halfHeight
        && sensorY - channel.supportRadiusMetres <= halfHeight;
}

} // namespace

const LinearRgb& CameraImageResult::at(
    std::size_t column,
    std::size_t row) const {
    const bool dimensionsOverflow = pixelWidth != 0U
        && pixelHeight > std::numeric_limits<std::size_t>::max() / pixelWidth;
    if (column >= pixelWidth || row >= pixelHeight || dimensionsOverflow
        || rowMajorLinearSensorSignal.size() != pixelWidth * pixelHeight) {
        throw std::out_of_range("camera image pixel is outside the raster");
    }
    return rowMajorLinearSensorSignal[row * pixelWidth + column];
}

CameraImageResult synthesizeCameraImage(
    const ChimeraRecipe& recipe,
    const ReconstructionResult& reconstruction,
    const CameraImageRequest& request,
    const optics::sensor::CalibratedCameraSpectralResponse& cameraResponse) {
    validateChimeraRecipe(recipe);
    validateRequest(request);
    if (reconstruction.sourceRecipeId != recipe.recipeId
        || reconstruction.samples.empty()
        || reconstruction.samples.size() > kMaximumCameraDirectionalSamples
        || reconstruction.requestFormatVersion
            != kReconstructionRequestFormatVersion
        || !optics::scene::isStableBenchId(reconstruction.jobId)) {
        throw std::invalid_argument(
            "camera image reconstruction provenance is invalid");
    }
    const std::size_t pixelCount = request.pixelWidth * request.pixelHeight;
    CameraImageResult result {
        .requestFormatVersion = request.formatVersion,
        .jobId = request.jobId,
        .sourceReconstructionJobId = reconstruction.jobId,
        .sourceRecipeId = recipe.recipeId,
        .cameraCalibrationId = cameraResponse.calibrationId(),
        .pupilCentreXMetres = request.pupilCentreXMetres,
        .pupilCentreYMetres = request.pupilCentreYMetres,
        .pupilPlaneDistanceMetres = request.pupilPlaneDistanceMetres,
        .pupilDiameterMetres = request.pupilDiameterMetres,
        .focalLengthMetres = request.focalLengthMetres,
        .pixelWidth = request.pixelWidth,
        .pixelHeight = request.pixelHeight,
        .pixelPitchXMetres = request.pixelPitchXMetres,
        .pixelPitchYMetres = request.pixelPitchYMetres,
        .psfSupportFirstDarkRings = request.psfSupportFirstDarkRings,
        .rowMajorLinearSensorSignal = std::vector<LinearRgb>(pixelCount),
        .contributions = {},
        .metrics = {},
        .limitations = {
            "ideal on-axis thin camera; distortion, defocus, aberrations, polarization, noise, saturation, and Bayer sampling are not modelled",
            "each directional sample is a ray bundle tested against one finite circular pupil; full coherent multi-hogel field superposition is not performed",
            "Airy intensity is truncated at the declared first-dark-ring support and normalized before bounded sensor-edge loss",
            "sensor values are linear relative signals from the measured spectral LUT, not display RGB or absolute photoelectrons",
            "sensor row zero maps to positive camera Y",
        },
    };
    result.metrics.directionalSampleCount = reconstruction.samples.size();
    result.contributions.reserve(reconstruction.samples.size());
    const auto channels = makePsfChannels(
        recipe, request, cameraResponse, result.metrics);
    std::size_t evaluationsPerSample = 0U;
    for (const auto& channel : channels) {
        const std::size_t width = 2U * channel.supportRadiusColumns + 1U;
        const std::size_t height = 2U * channel.supportRadiusRows + 1U;
        if (width > kMaximumCameraKernelEvaluations / height
            || evaluationsPerSample
                > kMaximumCameraKernelEvaluations - width * height) {
            throw std::invalid_argument(
                "camera PSF kernel work exceeds its safety limit");
        }
        evaluationsPerSample += width * height;
    }
    if (reconstruction.samples.size()
        > kMaximumCameraKernelEvaluations / evaluationsPerSample) {
        throw std::invalid_argument(
            "camera image composition exceeds its kernel-work limit");
    }

    const double pupilRadius = 0.5 * request.pupilDiameterMetres;
    for (const auto& sample : reconstruction.samples) {
        validateLinearRgb(sample.reconstructedLinearIntensity,
            "directional reconstructed intensity");
        requireFinite(sample.stageXMetres, "hogel stage x");
        requireFinite(sample.stageYMetres, "hogel stage y");
        requireFinite(sample.reconstructedHorizontalAngleRadians,
            "reconstructed horizontal angle");
        requireFinite(sample.reconstructedVerticalAngleRadians,
            "reconstructed vertical angle");
        const double tangentX = std::tan(
            sample.reconstructedHorizontalAngleRadians);
        const double tangentY = std::tan(
            sample.reconstructedVerticalAngleRadians);
        if (!std::isfinite(tangentX) || !std::isfinite(tangentY)) {
            throw std::invalid_argument(
                "reconstructed direction is grazing or unrepresentable");
        }
        const double pupilX = sample.stageXMetres
            + request.pupilPlaneDistanceMetres * tangentX;
        const double pupilY = sample.stageYMetres
            + request.pupilPlaneDistanceMetres * tangentY;
        const double sensorX = request.focalLengthMetres * tangentX;
        const double sensorY = request.focalLengthMetres * tangentY;
        if (!std::isfinite(pupilX) || !std::isfinite(pupilY)
            || !std::isfinite(sensorX) || !std::isfinite(sensorY)) {
            throw std::overflow_error(
                "camera ray intersection is not representable");
        }
        const bool enteredPupil = std::hypot(
            pupilX - request.pupilCentreXMetres,
            pupilY - request.pupilCentreYMetres) <= pupilRadius;
        CameraRayContribution contribution {
            .hogelX = sample.hogelX,
            .hogelY = sample.hogelY,
            .viewId = sample.viewId,
            .pupilIntersectionXMetres = pupilX,
            .pupilIntersectionYMetres = pupilY,
            .enteredPupil = enteredPupil,
            .sensorCentreXMetres = sensorX,
            .sensorCentreYMetres = sensorY,
            .depositedOnSensor = false,
            .idealSensorSignal = {},
            .depositedSensorSignal = {},
        };
        if (!enteredPupil) {
            ++result.metrics.pupilRejectedSampleCount;
            result.contributions.push_back(std::move(contribution));
            continue;
        }
        ++result.metrics.pupilAcceptedSampleCount;
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const double incident = opticalChannelIntensity(
                sample.reconstructedLinearIntensity, index);
            if (incident == 0.0) continue;
            const auto signal = scaledResponse(
                incident, channels[index].cameraResponse);
            addSignal(contribution.idealSensorSignal, signal);
            if (!psfMayReachSensor(
                    request,
                    channels[index],
                    contribution.sensorCentreXMetres,
                    contribution.sensorCentreYMetres)) {
                continue;
            }
            const auto deposited = depositPsf(
                result,
                request,
                channels[index],
                contribution.sensorCentreXMetres,
                contribution.sensorCentreYMetres,
                signal);
            addSignal(contribution.depositedSensorSignal, deposited);
        }
        addSignal(result.metrics.totalIdealSensorSignal,
            contribution.idealSensorSignal);
        addSignal(result.metrics.totalDepositedSensorSignal,
            contribution.depositedSensorSignal);
        contribution.depositedOnSensor
            = contribution.depositedSensorSignal.red > 0.0
            || contribution.depositedSensorSignal.green > 0.0
            || contribution.depositedSensorSignal.blue > 0.0;
        if (contribution.depositedOnSensor) {
            ++result.metrics.sensorDepositedSampleCount;
        } else {
            ++result.metrics.sensorMissedSampleCount;
        }
        result.contributions.push_back(std::move(contribution));
    }
    return result;
}

} // namespace holobench::app::chimera
