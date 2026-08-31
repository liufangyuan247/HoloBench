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

} // namespace holobench::app::lessons
