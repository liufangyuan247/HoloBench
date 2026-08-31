#include "app/lessons/LessonTemplates.hpp"

#include <algorithm>
#include <cmath>
#include <numbers>
#include <stdexcept>

#include "core/math/Vec3.hpp"
#include "optics/ray/Ray.hpp"
#include "optics/scene/GeometricComponents.hpp"

namespace holobench::app::lessons {
namespace {

[[nodiscard]] double angleToNormal(
    math::Vec3d direction,
    math::Vec3d normal) {
    const auto unitDirection = math::normalized(direction);
    const auto unitNormal = math::normalized(normal);
    const double cosine = std::clamp(
        std::abs(math::dot(unitDirection, unitNormal)), 0.0, 1.0);
    return std::acos(cosine);
}

} // namespace

void validateReflectionRefractionLessonConfig(
    const ReflectionRefractionLessonConfig& config) {
    constexpr double kMaximumIncidenceAngle = 85.0 * std::numbers::pi_v<double> / 180.0;
    if (!std::isfinite(config.incidenceAngleRadians)
        || config.incidenceAngleRadians < 0.0
        || config.incidenceAngleRadians > kMaximumIncidenceAngle) {
        throw std::invalid_argument("lesson incidence angle must be between 0 and 85 degrees");
    }
    if (!std::isfinite(config.incidentRefractiveIndex)
        || config.incidentRefractiveIndex <= 0.0
        || !std::isfinite(config.transmittedRefractiveIndex)
        || config.transmittedRefractiveIndex <= 0.0) {
        throw std::invalid_argument("lesson refractive indices must be finite and positive");
    }
}

ReflectionRefractionLessonResult evaluateReflectionRefractionLesson(
    const ReflectionRefractionLessonConfig& config) {
    validateReflectionRefractionLessonConfig(config);
    const math::Vec3d surfaceNormal {0.0, 0.0, -1.0};
    const math::Vec3d direction {
        std::sin(config.incidenceAngleRadians),
        0.0,
        std::cos(config.incidenceAngleRadians),
    };
    const auto incident = optics::ray::makeRay(
        {0.0, 0.0, -0.1}, direction, 532e-9, 1.0);

    auto mirror = optics::scene::createDefaultPlanarMirror();
    mirror.widthMetres = 2.0;
    mirror.heightMetres = 2.0;
    const auto reflection = optics::ray::tracePlanarMirror(incident, mirror);
    if (reflection.status != optics::ray::GeometricInteractionStatus::Reflected
        || !reflection.outgoingRay.has_value()) {
        throw std::runtime_error("reflection lesson ray did not reach the mirror");
    }

    auto interfaceComponent = optics::scene::createDefaultPlaneInterface();
    interfaceComponent.widthMetres = 2.0;
    interfaceComponent.heightMetres = 2.0;
    interfaceComponent.nIncident = config.incidentRefractiveIndex;
    interfaceComponent.nTransmitted = config.transmittedRefractiveIndex;
    const auto refraction = optics::ray::tracePlaneInterface(
        incident, interfaceComponent);
    const bool totalInternalReflection = refraction.status
        == optics::ray::GeometricInteractionStatus::TotalInternalReflection;
    if ((refraction.status != optics::ray::GeometricInteractionStatus::Refracted
            && !totalInternalReflection)
        || !refraction.outgoingRay.has_value()) {
        throw std::runtime_error("reflection lesson ray did not reach the interface");
    }

    const double incidenceAngle = angleToNormal(incident.direction, surfaceNormal);
    const double reflectionAngle = angleToNormal(
        reflection.outgoingRay->direction, surfaceNormal);
    const double transmissionAngle = angleToNormal(
        refraction.outgoingRay->direction, surfaceNormal);
    const double snellResidual = totalInternalReflection
        ? 0.0
        : config.incidentRefractiveIndex * std::sin(incidenceAngle)
            - config.transmittedRefractiveIndex * std::sin(transmissionAngle);
    return {
        .incidenceAngleRadians = incidenceAngle,
        .reflectionAngleRadians = reflectionAngle,
        .transmissionAngleRadians = transmissionAngle,
        .reflectionAngleErrorRadians = reflectionAngle - incidenceAngle,
        .snellResidual = snellResidual,
        .totalInternalReflection = totalInternalReflection,
    };
}

optics::scene::OpticalBenchScene makeThinLensLessonTemplate() {
    auto scene = optics::scene::createDefaultRealImageScene();
    scene.name = "Lesson Template: Thin Lens";
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        throw std::runtime_error("thin-lens lesson template must form a real image");
    }
    scene.screen.planeZMetres = prediction.imagePlaneZMetres + 0.03;
    optics::scene::validateScene(scene);
    return scene;
}

ThinLensLessonObservation evaluateThinLensLessonObservation(
    const optics::scene::OpticalBenchScene& scene,
    double templateScreenZMetres) {
    if (!std::isfinite(templateScreenZMetres)) {
        throw std::invalid_argument("thin-lens lesson template screen position must be finite");
    }
    optics::scene::validateScene(scene);
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        return {
            .prediction = prediction,
            .screenFocusErrorMetres = 0.0,
            .screenMoved = std::abs(scene.screen.planeZMetres - templateScreenZMetres) >= 0.005,
            .screenAtFocus = false,
        };
    }
    const double focusError = scene.screen.planeZMetres - prediction.imagePlaneZMetres;
    return {
        .prediction = prediction,
        .screenFocusErrorMetres = focusError,
        .screenMoved = std::abs(scene.screen.planeZMetres - templateScreenZMetres) >= 0.005,
        .screenAtFocus = std::abs(focusError) <= 0.001,
    };
}

optics::scene::OpticalBenchScene makeRealVirtualLessonTemplate() {
    auto scene = optics::scene::createDefaultRealImageScene();
    scene.name = "Lesson Template: Real and Virtual Images";
    const auto prediction = optics::scene::predictThinLensImage(scene);
    if (prediction.nature != optics::scene::ImageNature::Real) {
        throw std::runtime_error("real/virtual lesson template must start with a real image");
    }
    return scene;
}

RealVirtualLessonObservation evaluateRealVirtualLessonObservation(
    const optics::scene::OpticalBenchScene& scene) {
    optics::scene::validateScene(scene);
    const auto prediction = optics::scene::predictThinLensImage(scene);
    const double focalLengthMagnitude = std::abs(scene.lens.focalLengthMetres);
    return {
        .prediction = prediction,
        .crossedFocalPlane = scene.lens.focalLengthMetres > 0.0
            && prediction.objectDistanceMetres < focalLengthMagnitude
            && prediction.nature == optics::scene::ImageNature::Virtual,
    };
}

wave::WaveDetectorConfig makeDiffractionLessonTemplate() {
    wave::WaveDetectorConfig config;
    config.sourceKind = wave::WaveSourceKind::PlaneWave;
    config.apertureKind = wave::WaveApertureKind::Rectangular;
    config.rectangularHalfWidthMetres = 0.30e-3;
    config.rectangularHalfHeightMetres = 1.20e-3;
    config.propagator = wave::WavePropagatorKind::AngularSpectrum;
    config.propagationDistanceMetres = 0.20;
    config.gridResolution = 128U;
    config.gridPhysicalSpanMetres = 8.0e-3;
    return config;
}

DiffractionLessonObservation evaluateDiffractionLessonObservation(
    const wave::WaveDetectorConfig& appliedConfig,
    const wave::WaveDetectorResult& result,
    double templateHalfWidthMetres,
    std::optional<double> baselineHalfMaximumWidthMetres) {
    if (appliedConfig.apertureKind != wave::WaveApertureKind::Rectangular
        || result.sourceConfig != appliedConfig
        || !std::isfinite(appliedConfig.rectangularHalfWidthMetres)
        || appliedConfig.rectangularHalfWidthMetres <= 0.0
        || !std::isfinite(templateHalfWidthMetres)
        || templateHalfWidthMetres <= 0.0
        || result.field.width() != appliedConfig.gridResolution
        || result.field.height() != appliedConfig.gridResolution
        || result.field.vacuumWavelengthMetres() != appliedConfig.wavelengthMetres
        || result.field.refractiveIndex() != appliedConfig.refractiveIndex) {
        throw std::invalid_argument(
            "diffraction lesson requires a compatible rectangular-aperture result");
    }

    const std::size_t centerY = result.field.height() / 2U;
    std::size_t peakX = 0U;
    double peakIntensity = 0.0;
    for (std::size_t x = 0; x < result.field.width(); ++x) {
        const double intensity = std::norm(result.field.at(x, centerY));
        if (!std::isfinite(intensity)) {
            throw std::invalid_argument(
                "diffraction lesson result intensity must be finite");
        }
        if (intensity > peakIntensity) {
            peakIntensity = intensity;
            peakX = x;
        }
    }
    if (!(peakIntensity > 0.0)) {
        throw std::invalid_argument(
            "diffraction lesson result must contain finite non-zero intensity");
    }
    const double halfMaximum = 0.5 * peakIntensity;
    std::size_t left = peakX;
    while (left > 0U
        && std::norm(result.field.at(left, centerY)) > halfMaximum) {
        --left;
    }
    std::size_t right = peakX;
    while (right + 1U < result.field.width()
        && std::norm(result.field.at(right, centerY)) > halfMaximum) {
        ++right;
    }
    if (left == 0U || right + 1U == result.field.width()) {
        throw std::invalid_argument(
            "diffraction lesson central lobe is clipped by the field window");
    }
    const double halfMaximumWidth = result.field.xCoordinateMetres(right)
        - result.field.xCoordinateMetres(left);
    if (!std::isfinite(halfMaximumWidth) || halfMaximumWidth <= 0.0) {
        throw std::invalid_argument(
            "diffraction lesson half-maximum width must be finite and positive");
    }

    constexpr double kRequiredWidthRatio = 0.75;
    constexpr double kRequiredBroadeningRatio = 1.10;
    const bool apertureNarrowed = appliedConfig.rectangularHalfWidthMetres
        <= templateHalfWidthMetres * kRequiredWidthRatio;
    return {
        .apertureFullWidthMetres = 2.0 * appliedConfig.rectangularHalfWidthMetres,
        .horizontalHalfMaximumWidthMetres = halfMaximumWidth,
        .apertureNarrowed = apertureNarrowed,
        .patternBroadened = apertureNarrowed
            && baselineHalfMaximumWidthMetres.has_value()
            && halfMaximumWidth >= baselineHalfMaximumWidthMetres.value()
                * kRequiredBroadeningRatio,
    };
}

} // namespace holobench::app::lessons
