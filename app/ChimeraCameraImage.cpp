#include "app/ChimeraCameraImage.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <limits>
#include <stdexcept>
#include <string_view>
#include <vector>

#include "compute/fourier/PsfMtf.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/scene/BenchPathEvidence.hpp"
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
    CameraImageMetrics& metrics,
    const std::array<double, 3>* effectiveFocalLengths = nullptr) {
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
        const double focalLength = effectiveFocalLengths == nullptr
            ? request.focalLengthMetres
            : (*effectiveFocalLengths)[index];
        channel.airy = compute::fourier::CircularPupilPsfMtf(
            channel.wavelengthMetres,
            1.0,
            focalLength,
            0.5 * request.pupilDiameterMetres);
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

void validateSensorRequest(const CameraSensorRequest& request) {
    CameraImageRequest raster;
    raster.formatVersion = request.formatVersion;
    raster.jobId = request.jobId;
    raster.pixelWidth = request.pixelWidth;
    raster.pixelHeight = request.pixelHeight;
    raster.pixelPitchXMetres = request.pixelPitchXMetres;
    raster.pixelPitchYMetres = request.pixelPitchYMetres;
    raster.psfSupportFirstDarkRings = request.psfSupportFirstDarkRings;
    validateRequest(raster);
}

std::string_view sequentialTraceStatusName(
    optics::ray::SequentialTraceStatus status) noexcept {
    using optics::ray::SequentialTraceStatus;
    switch (status) {
    case SequentialTraceStatus::Completed: return "completed";
    case SequentialTraceStatus::Miss: return "miss";
    case SequentialTraceStatus::Clipped: return "clipped";
    case SequentialTraceStatus::OutsideSurfaceDomain:
        return "outside_surface_domain";
    case SequentialTraceStatus::SurfaceNonConvergence:
        return "surface_non_convergence";
    case SequentialTraceStatus::TotalInternalReflection:
        return "total_internal_reflection";
    }
    return "unknown";
}

double observationHalfWidth(
    const optics::scene::BenchComponent& observation) {
    using optics::scene::BenchComponentKind;
    if (observation.kind == BenchComponentKind::ScreenDetector) {
        return 0.5 * std::get<optics::scene::ScreenDetectorParameters>(
            observation.parameters).widthMetres;
    }
    return 0.5 * std::get<optics::scene::FieldProbeParameters>(
        observation.parameters).widthMetres;
}

double observationHalfHeight(
    const optics::scene::BenchComponent& observation) {
    using optics::scene::BenchComponentKind;
    if (observation.kind == BenchComponentKind::ScreenDetector) {
        return 0.5 * std::get<optics::scene::ScreenDetectorParameters>(
            observation.parameters).heightMetres;
    }
    return 0.5 * std::get<optics::scene::FieldProbeParameters>(
        observation.parameters).heightMetres;
}

optics::ray::SequentialLensPrescription makePlacedCameraPrescription(
    const optics::scene::BenchComponent& lens,
    const optics::ray::ILensPrescriptionResolver& resolver,
    std::string& prescriptionId,
    double& effectiveApertureDiameterMetres) {
    const auto& parameters
        = std::get<optics::scene::RealLensAssemblyParameters>(
            lens.parameters);
    const auto* source = resolver.resolve(parameters.prescriptionId);
    if (source == nullptr) {
        throw std::invalid_argument(
            "CHIMERA camera lens prescription '" + parameters.prescriptionId
            + "' is unavailable; ideal-camera fallback is forbidden");
    }
    auto placed = optics::ray::placeLensPrescription(*source, lens.transform);
    double clearRadius = 0.5 * parameters.clearApertureDiameterMetres;
    for (auto& surface : placed.surfaces) {
        clearRadius = std::min(
            clearRadius, surface.geometry.clearSemiDiameterMetres);
    }
    if (!std::isfinite(clearRadius) || clearRadius <= 0.0) {
        throw std::invalid_argument(
            "CHIMERA camera lens has no finite positive shared clear aperture");
    }
    for (auto& surface : placed.surfaces) {
        surface.geometry.clearSemiDiameterMetres = std::min(
            surface.geometry.clearSemiDiameterMetres, clearRadius);
    }
    optics::ray::validateSequentialLensPrescription(placed);
    prescriptionId = parameters.prescriptionId;
    effectiveApertureDiameterMetres = 2.0 * clearRadius;
    return placed;
}

double paraxialEffectiveFocalLength(
    const optics::ray::SequentialLensPrescription& placed,
    double wavelengthMetres,
    double clearApertureDiameterMetres) {
    const auto& frame = placed.surfaces.front().localToWorld;
    const double rayHeight = std::clamp(
        clearApertureDiameterMetres * 5e-4, 1e-7, 1e-5);
    const auto incident = optics::ray::makeRay(
        frame.translationMetres
            - frame.localZAxisInWorld * 0.02
            + frame.localXAxisInWorld * rayHeight,
        frame.localZAxisInWorld,
        wavelengthMetres,
        1.0);
    optics::ray::SurfaceIntersectionOptions options;
    options.maximumDistanceMetres = 1.0;
    const auto trace = optics::ray::traceSequentialLens(
        incident, placed, options);
    if (trace.status != optics::ray::SequentialTraceStatus::Completed
        || !trace.finalRay.has_value()) {
        throw std::invalid_argument(
            "CHIMERA camera prescription paraxial calibration failed with status "
            + std::string(sequentialTraceStatusName(trace.status)));
    }
    const auto localDirection = math::transformDirectionWorldToLocal(
        frame, trace.finalRay->direction);
    if (!std::isfinite(localDirection.x)
        || !std::isfinite(localDirection.z)
        || std::abs(localDirection.z) <= 1e-12) {
        throw std::invalid_argument(
            "CHIMERA camera prescription has an invalid paraxial exit ray");
    }
    const double slope = localDirection.x / localDirection.z;
    const double focalLength = -rayHeight / slope;
    if (!std::isfinite(focalLength) || focalLength <= 0.0) {
        throw std::invalid_argument(
            "CHIMERA camera currently requires a converging prescription");
    }
    return focalLength;
}

bool intersectObservationPlane(
    const optics::ray::Ray& ray,
    const optics::scene::BenchComponent& observation,
    double& sensorX,
    double& sensorY) {
    const auto& frame = observation.transform;
    const double denominator = math::dot(
        ray.direction, frame.localZAxisInWorld);
    if (std::abs(denominator) <= 1e-12) return false;
    const double distance = math::dot(
        frame.translationMetres - ray.originMetres,
        frame.localZAxisInWorld) / denominator;
    if (!std::isfinite(distance) || distance <= 1e-12) return false;
    const auto point = ray.originMetres + ray.direction * distance;
    const auto local = math::transformPointWorldToLocal(frame, point);
    sensorX = local.x;
    sensorY = local.y;
    return std::abs(sensorX) <= observationHalfWidth(observation)
        && std::abs(sensorY) <= observationHalfHeight(observation);
}

math::RigidTransform3d makeCameraRayFrame(
    const math::RigidTransform3d& plateFrame,
    math::Vec3d origin,
    math::Vec3d direction) {
    const auto zAxis = math::normalized(direction);
    auto xAxis = plateFrame.localXAxisInWorld
        - zAxis * math::dot(plateFrame.localXAxisInWorld, zAxis);
    if (math::lengthSquared(xAxis) <= 1e-12) {
        xAxis = plateFrame.localYAxisInWorld
            - zAxis * math::dot(plateFrame.localYAxisInWorld, zAxis);
    }
    xAxis = math::normalized(xAxis);
    math::RigidTransform3d result {
        .translationMetres = origin,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = math::cross(zAxis, xAxis),
        .localZAxisInWorld = zAxis,
    };
    math::validateRigidTransform(result);
    return result;
}

bool cameraBenchRouteReachesSensor(
    const optics::scene::BenchScene& bench,
    const optics::scene::BenchComponent& plate,
    const optics::scene::BenchComponent& lens,
    const optics::scene::BenchComponent& observation,
    math::Vec3d origin,
    math::Vec3d direction,
    double wavelengthMetres,
    const optics::ray::ILensPrescriptionResolver& lensPrescriptions) {
    const optics::scene::BeamState seed {
        .wavelengthMetres = wavelengthMetres,
        .powerWatts = 1.0,
        .phaseRadians = 0.0,
        .coherenceId = "chimera-placed-camera",
        .accumulatedOpticalPathMetres = 0.0,
        .originMetres = origin,
        .direction = direction,
        .localFrame = makeCameraRayFrame(plate.transform, origin, direction),
        .provenance = {
            .branchId = 1U,
            .parentBranchId = 0U,
            .componentPath = {plate.id},
        },
    };
    const auto trace = optics::ray::traceDerivedBenchBeam(
        bench, seed, {}, &lensPrescriptions);
    const optics::scene::OpticalInteraction* terminal = nullptr;
    for (const auto& interaction : trace.interactions) {
        if (interaction.componentId != observation.id
            || interaction.incidentBeam.provenance.componentPath.empty()
            || interaction.incidentBeam.provenance.componentPath.front()
                != plate.id) {
            continue;
        }
        if (terminal != nullptr) {
            throw std::invalid_argument(
                "CHIMERA camera reaches the selected sensor through ambiguous Bench branches");
        }
        terminal = &interaction;
    }
    if (terminal == nullptr) return false;
    const auto path = optics::scene::collectBenchPathInteractions(
        trace, *terminal);
    if (path.size() != 2U || path.front().componentId != lens.id
        || path.back().componentId != observation.id) {
        throw std::invalid_argument(
            "CHIMERA camera path must be the explicit selected Real Lens Assembly followed by the selected sensor; additional placed optics require a broader calibrated camera model");
    }
    return true;
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

bool CameraImageResult::isStaleFor(
    const optics::scene::BenchScene& bench) const noexcept {
    if (!usedPlacedSequentialLens) return false;
    const auto* plate = bench.find(sourcePlateComponentId);
    const auto* lens = bench.find(lensComponentId);
    const auto* observation = bench.find(observationComponentId);
    return sourceSceneRevision != bench.revision()
        || plate == nullptr
        || plate->kind
            != optics::scene::BenchComponentKind::HolographicPlate
        || lens == nullptr
        || lens->kind
            != optics::scene::BenchComponentKind::RealLensAssembly
        || std::get<optics::scene::RealLensAssemblyParameters>(
               lens->parameters).prescriptionId
            != lensPrescriptionId
        || observation == nullptr
        || (observation->kind
                != optics::scene::BenchComponentKind::ScreenDetector
            && observation->kind
                != optics::scene::BenchComponentKind::FieldProbe);
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
        .usedPlacedSequentialLens = false,
        .sourceSceneRevision = 0,
        .sourcePlateComponentId = {},
        .lensComponentId = {},
        .lensPrescriptionId = {},
        .observationComponentId = {},
        .placedClearApertureDiameterMetres = 0.0,
        .rgbEffectiveFocalLengthMetres = {},
        .rgbSensorAxialDistanceMetres = {},
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
            .spectralRays = {},
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

CameraImageResult synthesizePlacedCameraImage(
    const ChimeraRecipe& recipe,
    const ReconstructionResult& reconstruction,
    const CameraSensorRequest& request,
    const optics::sensor::CalibratedCameraSpectralResponse& cameraResponse,
    const optics::scene::BenchScene& bench,
    std::string_view sourcePlateComponentId,
    std::string_view lensComponentId,
    std::string_view observationComponentId,
    const optics::ray::ILensPrescriptionResolver& lensPrescriptions) {
    validateChimeraRecipe(recipe);
    validateSensorRequest(request);
    if (reconstruction.sourceRecipeId != recipe.recipeId
        || reconstruction.samples.empty()
        || reconstruction.samples.size() > kMaximumCameraDirectionalSamples
        || reconstruction.requestFormatVersion
            != kReconstructionRequestFormatVersion
        || !optics::scene::isStableBenchId(reconstruction.jobId)) {
        throw std::invalid_argument(
            "placed camera reconstruction provenance is invalid");
    }
    const auto* plate = bench.find(sourcePlateComponentId);
    const auto* lens = bench.find(lensComponentId);
    const auto* observation = bench.find(observationComponentId);
    if (plate == nullptr
        || plate->kind != optics::scene::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "CHIMERA placed camera requires an explicit holographic source plate");
    }
    if (lens == nullptr
        || lens->kind
            != optics::scene::BenchComponentKind::RealLensAssembly) {
        throw std::invalid_argument(
            "CHIMERA placed camera requires an explicit Real Lens Assembly");
    }
    if (observation == nullptr
        || (observation->kind
                != optics::scene::BenchComponentKind::ScreenDetector
            && observation->kind
                != optics::scene::BenchComponentKind::FieldProbe)) {
        throw std::invalid_argument(
            "CHIMERA placed camera requires an explicit Screen or Field Probe sensor plane");
    }
    if (plate->id == lens->id || plate->id == observation->id
        || lens->id == observation->id) {
        throw std::invalid_argument(
            "CHIMERA placed camera plate, lens, and sensor must be distinct components");
    }

    const double rasterWidth = static_cast<double>(request.pixelWidth)
        * request.pixelPitchXMetres;
    const double rasterHeight = static_cast<double>(request.pixelHeight)
        * request.pixelPitchYMetres;
    if (!std::isfinite(rasterWidth) || !std::isfinite(rasterHeight)
        || rasterWidth > 2.0 * observationHalfWidth(*observation) + 1e-12
        || rasterHeight > 2.0 * observationHalfHeight(*observation) + 1e-12) {
        throw std::invalid_argument(
            "CHIMERA camera raster exceeds the placed sensor active area");
    }

    std::string prescriptionId;
    double clearApertureDiameter = 0.0;
    const auto placedPrescription = makePlacedCameraPrescription(
        *lens,
        lensPrescriptions,
        prescriptionId,
        clearApertureDiameter);
    std::array<double, 3> effectiveFocalLengths {};
    for (std::size_t index = 0; index < recipe.rgb.size(); ++index) {
        effectiveFocalLengths[index] = paraxialEffectiveFocalLength(
            placedPrescription,
            recipe.rgb[index].wavelengthMetres,
            clearApertureDiameter);
    }

    const double plateToLensDistance = math::length(
        lens->transform.translationMetres - plate->transform.translationMetres);
    const double sensorAxialDistance = math::dot(
        observation->transform.translationMetres
            - lens->transform.translationMetres,
        lens->transform.localZAxisInWorld);
    if (!std::isfinite(plateToLensDistance) || plateToLensDistance <= 0.0
        || !std::isfinite(sensorAxialDistance)
        || sensorAxialDistance <= 0.0) {
        throw std::invalid_argument(
            "CHIMERA placed camera sensor must lie after the lens along its optical axis");
    }
    const double propagationSign = math::dot(
            lens->transform.translationMetres
                - plate->transform.translationMetres,
            plate->transform.localZAxisInWorld)
            >= 0.0
        ? 1.0
        : -1.0;
    const math::Vec3d nominalDirection
        = plate->transform.localZAxisInWorld * propagationSign;
    if (math::dot(
            nominalDirection, lens->transform.localZAxisInWorld) <= 1e-6) {
        throw std::invalid_argument(
            "CHIMERA camera lens prescription is reversed relative to plate replay propagation");
    }

    CameraImageRequest raster;
    raster.formatVersion = request.formatVersion;
    raster.jobId = request.jobId;
    raster.pupilPlaneDistanceMetres = plateToLensDistance;
    raster.pupilDiameterMetres = clearApertureDiameter;
    raster.focalLengthMetres = effectiveFocalLengths[1];
    raster.pixelWidth = request.pixelWidth;
    raster.pixelHeight = request.pixelHeight;
    raster.pixelPitchXMetres = request.pixelPitchXMetres;
    raster.pixelPitchYMetres = request.pixelPitchYMetres;
    raster.psfSupportFirstDarkRings = request.psfSupportFirstDarkRings;
    validateRequest(raster);

    const std::size_t pixelCount = raster.pixelWidth * raster.pixelHeight;
    CameraImageResult result {
        .requestFormatVersion = request.formatVersion,
        .jobId = request.jobId,
        .sourceReconstructionJobId = reconstruction.jobId,
        .sourceRecipeId = recipe.recipeId,
        .cameraCalibrationId = cameraResponse.calibrationId(),
        .usedPlacedSequentialLens = true,
        .sourceSceneRevision = bench.revision(),
        .sourcePlateComponentId = plate->id,
        .lensComponentId = lens->id,
        .lensPrescriptionId = prescriptionId,
        .observationComponentId = observation->id,
        .placedClearApertureDiameterMetres = clearApertureDiameter,
        .rgbEffectiveFocalLengthMetres = effectiveFocalLengths,
        .rgbSensorAxialDistanceMetres = {
            sensorAxialDistance,
            sensorAxialDistance,
            sensorAxialDistance,
        },
        .pupilCentreXMetres = 0.0,
        .pupilCentreYMetres = 0.0,
        .pupilPlaneDistanceMetres = plateToLensDistance,
        .pupilDiameterMetres = clearApertureDiameter,
        .focalLengthMetres = effectiveFocalLengths[1],
        .pixelWidth = raster.pixelWidth,
        .pixelHeight = raster.pixelHeight,
        .pixelPitchXMetres = raster.pixelPitchXMetres,
        .pixelPitchYMetres = raster.pixelPitchYMetres,
        .psfSupportFirstDarkRings = raster.psfSupportFirstDarkRings,
        .rowMajorLinearSensorSignal = std::vector<LinearRgb>(pixelCount),
        .contributions = {},
        .metrics = {},
        .limitations = {
            "chief rays are traced wavelength-by-wavelength through the placed sequential prescription; finite-aperture clipping and sensor-plane pose are physical Bench inputs",
            "diffraction blur uses each wavelength's prescription-derived paraxial effective focal length and shared circular clear aperture; prescription aberration wavefronts and defocus blur are not yet integrated",
            "each directional sample remains an incoherent ray bundle; full coherent multi-hogel field superposition is not performed",
            "sensor values are linear relative signals from the measured spectral LUT, not display RGB or absolute photoelectrons",
            "sensor row zero maps to positive placed-sensor local Y",
        },
    };
    result.metrics.directionalSampleCount = reconstruction.samples.size();
    result.contributions.reserve(reconstruction.samples.size());
    const auto channels = makePsfChannels(
        recipe,
        raster,
        cameraResponse,
        result.metrics,
        &effectiveFocalLengths);

    std::size_t evaluationsPerSample = 0U;
    for (const auto& channel : channels) {
        const std::size_t width = 2U * channel.supportRadiusColumns + 1U;
        const std::size_t height = 2U * channel.supportRadiusRows + 1U;
        if (width > kMaximumCameraKernelEvaluations / height
            || evaluationsPerSample
                > kMaximumCameraKernelEvaluations - width * height) {
            throw std::invalid_argument(
                "placed camera PSF kernel work exceeds its safety limit");
        }
        evaluationsPerSample += width * height;
    }
    if (reconstruction.samples.size()
        > kMaximumCameraKernelEvaluations / evaluationsPerSample) {
        throw std::invalid_argument(
            "placed camera composition exceeds its kernel-work limit");
    }

    optics::ray::SurfaceIntersectionOptions traceOptions;
    traceOptions.maximumDistanceMetres = std::max(
        1.0, plateToLensDistance + clearApertureDiameter + 0.1);
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
        const auto origin = math::transformPointLocalToWorld(
            plate->transform,
            {sample.stageXMetres, sample.stageYMetres, 0.0});
        const auto direction = math::normalized(
            nominalDirection
            + plate->transform.localXAxisInWorld * tangentX
            + plate->transform.localYAxisInWorld * tangentY);
        CameraRayContribution contribution {
            .hogelX = sample.hogelX,
            .hogelY = sample.hogelY,
            .viewId = sample.viewId,
            .pupilIntersectionXMetres = 0.0,
            .pupilIntersectionYMetres = 0.0,
            .enteredPupil = false,
            .sensorCentreXMetres = 0.0,
            .sensorCentreYMetres = 0.0,
            .depositedOnSensor = false,
            .idealSensorSignal = {},
            .depositedSensorSignal = {},
            .spectralRays = {},
        };
        contribution.spectralRays.reserve(channels.size());
        bool acceptedAnyChannel = false;
        for (std::size_t index = 0; index < channels.size(); ++index) {
            const auto incident = optics::ray::makeRay(
                origin,
                direction,
                channels[index].wavelengthMetres,
                1.0);
            const auto trace = optics::ray::traceSequentialLens(
                incident, placedPrescription, traceOptions);
            CameraSpectralRayEvidence evidence {
                .wavelengthMetres = channels[index].wavelengthMetres,
                .prescriptionTraceStatus = std::string(
                    sequentialTraceStatusName(trace.status)),
                .prescriptionSurfaceCount = trace.records.size(),
                .firstSurfaceXMetres = 0.0,
                .firstSurfaceYMetres = 0.0,
                .enteredPupil = false,
                .intersectedSensorPlane = false,
                .sensorCentreXMetres = 0.0,
                .sensorCentreYMetres = 0.0,
                .depositedOnSensor = false,
                .idealSensorSignal = {},
                .depositedSensorSignal = {},
            };
            if (trace.status != optics::ray::SequentialTraceStatus::Completed
                || !trace.finalRay.has_value() || trace.records.empty()) {
                ++result.metrics.prescriptionTraceRejectedCount;
                contribution.spectralRays.push_back(std::move(evidence));
                continue;
            }
            if (!cameraBenchRouteReachesSensor(
                    bench,
                    *plate,
                    *lens,
                    *observation,
                    origin,
                    direction,
                    channels[index].wavelengthMetres,
                    lensPrescriptions)) {
                evidence.prescriptionTraceStatus = "bench_path_blocked";
                ++result.metrics.prescriptionTraceRejectedCount;
                contribution.spectralRays.push_back(std::move(evidence));
                continue;
            }
            ++result.metrics.prescriptionTraceCompletedCount;
            acceptedAnyChannel = true;
            evidence.enteredPupil = true;
            const auto firstLocal = math::transformPointWorldToLocal(
                placedPrescription.surfaces.front().localToWorld,
                trace.records.front().worldPointMetres);
            evidence.firstSurfaceXMetres = firstLocal.x;
            evidence.firstSurfaceYMetres = firstLocal.y;
            double sensorX = 0.0;
            double sensorY = 0.0;
            evidence.intersectedSensorPlane = intersectObservationPlane(
                *trace.finalRay, *observation, sensorX, sensorY);
            evidence.sensorCentreXMetres = sensorX;
            evidence.sensorCentreYMetres = sensorY;
            if (index == 1U) {
                contribution.pupilIntersectionXMetres = firstLocal.x;
                contribution.pupilIntersectionYMetres = firstLocal.y;
                contribution.sensorCentreXMetres = sensorX;
                contribution.sensorCentreYMetres = sensorY;
            }
            if (!evidence.intersectedSensorPlane) {
                ++result.metrics.sensorPlaneMissedChannelCount;
                contribution.spectralRays.push_back(std::move(evidence));
                continue;
            }
            const double opticalIntensity = opticalChannelIntensity(
                sample.reconstructedLinearIntensity, index);
            if (opticalIntensity > 0.0) {
                evidence.idealSensorSignal = scaledResponse(
                    opticalIntensity, channels[index].cameraResponse);
                addSignal(contribution.idealSensorSignal,
                    evidence.idealSensorSignal);
                if (psfMayReachSensor(
                        raster, channels[index], sensorX, sensorY)) {
                    evidence.depositedSensorSignal = depositPsf(
                        result,
                        raster,
                        channels[index],
                        sensorX,
                        sensorY,
                        evidence.idealSensorSignal);
                    addSignal(contribution.depositedSensorSignal,
                        evidence.depositedSensorSignal);
                }
            }
            evidence.depositedOnSensor
                = evidence.depositedSensorSignal.red > 0.0
                || evidence.depositedSensorSignal.green > 0.0
                || evidence.depositedSensorSignal.blue > 0.0;
            contribution.spectralRays.push_back(std::move(evidence));
        }
        contribution.enteredPupil = acceptedAnyChannel;
        if (acceptedAnyChannel) {
            ++result.metrics.pupilAcceptedSampleCount;
        } else {
            ++result.metrics.pupilRejectedSampleCount;
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
        } else if (acceptedAnyChannel) {
            ++result.metrics.sensorMissedSampleCount;
        }
        result.contributions.push_back(std::move(contribution));
    }
    return result;
}

} // namespace holobench::app::chimera
