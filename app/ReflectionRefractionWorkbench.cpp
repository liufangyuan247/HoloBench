#include "app/ReflectionRefractionWorkbench.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string>

#include <nlohmann/json.hpp>

#include "core/math/Vec3.hpp"
#include "optics/ray/GeometricElements.hpp"
#include "optics/ray/Ray.hpp"
#include "optics/scene/GeometricComponents.hpp"

namespace holobench::app::reflection {
namespace {

using Json = nlohmann::json;

void requireKeys(
    const Json& object,
    const std::set<std::string>& expected,
    const char* context) {
    if (!object.is_object()) {
        throw std::invalid_argument(std::string(context) + " must be an object");
    }
    std::set<std::string> actual;
    for (auto iterator = object.begin(); iterator != object.end(); ++iterator) {
        actual.insert(iterator.key());
    }
    if (actual != expected) {
        throw std::invalid_argument(
            std::string(context) + " has missing or unknown keys");
    }
}

[[nodiscard]] double finiteNumber(const Json& value, const char* context) {
    if (!value.is_number()) {
        throw std::invalid_argument(std::string(context) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::invalid_argument(std::string(context) + " must be finite");
    }
    return result;
}

[[nodiscard]] Json provenanceJson(const project::ProjectProvenance& provenance) {
    project::validateProjectProvenance(provenance);
    return {
        {"origin", project::projectOriginKindName(provenance.originKind)},
        {"source_id", provenance.sourceId},
        {"source_version", provenance.sourceVersion},
    };
}

[[nodiscard]] project::ProjectProvenance parseProvenance(const Json& json) {
    requireKeys(
        json, {"origin", "source_id", "source_version"}, "project provenance");
    if (!json.at("origin").is_string()
        || !json.at("source_id").is_string()
        || !json.at("source_version").is_number_integer()) {
        throw std::invalid_argument("project provenance fields have invalid types");
    }
    const auto version = json.at("source_version").get<std::int64_t>();
    if (version < std::numeric_limits<int>::min()
        || version > std::numeric_limits<int>::max()) {
        throw std::invalid_argument(
            "project provenance version is outside int range");
    }
    project::ProjectProvenance result {
        .originKind = project::projectOriginKindFromName(
            json.at("origin").get<std::string>()),
        .sourceId = json.at("source_id").get<std::string>(),
        .sourceVersion = static_cast<int>(version),
    };
    project::validateProjectProvenance(result);
    return result;
}

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

void validateReflectionRefractionConfig(
    const ReflectionRefractionConfig& config) {
    constexpr double kMaximumIncidenceAngle
        = 85.0 * std::numbers::pi_v<double> / 180.0;
    if (!std::isfinite(config.incidenceAngleRadians)
        || config.incidenceAngleRadians < 0.0
        || config.incidenceAngleRadians > kMaximumIncidenceAngle) {
        throw std::invalid_argument(
            "incidence angle must be between 0 and 85 degrees");
    }
    if (!std::isfinite(config.incidentRefractiveIndex)
        || config.incidentRefractiveIndex <= 0.0
        || !std::isfinite(config.transmittedRefractiveIndex)
        || config.transmittedRefractiveIndex <= 0.0) {
        throw std::invalid_argument(
            "refractive indices must be finite and positive");
    }
}

ReflectionRefractionResult evaluateReflectionRefraction(
    const ReflectionRefractionConfig& config) {
    validateReflectionRefractionConfig(config);
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
        throw std::runtime_error("workbench ray did not reach the mirror");
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
        throw std::runtime_error("workbench ray did not reach the interface");
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

void validateReflectionRefractionWorkbench(
    const ReflectionRefractionWorkbenchDocument& document) {
    if (document.formatVersion
        != kReflectionRefractionWorkbenchFormatVersion) {
        throw std::invalid_argument(
            "unsupported reflection/refraction workbench format version");
    }
    if (document.name.empty()) {
        throw std::invalid_argument(
            "reflection/refraction workbench name cannot be empty");
    }
    project::validateProjectProvenance(document.provenance);
    validateReflectionRefractionConfig(document.config);
}

std::string serializeReflectionRefractionWorkbenchJson(
    const ReflectionRefractionWorkbenchDocument& document) {
    validateReflectionRefractionWorkbench(document);
    const Json json = {
        {"format_version", document.formatVersion},
        {"model", "reflection_refraction_workbench"},
        {"name", document.name},
        {"provenance", provenanceJson(document.provenance)},
        {"workbench", {
            {"incidence_angle_rad", document.config.incidenceAngleRadians},
            {"incident_refractive_index",
                document.config.incidentRefractiveIndex},
            {"transmitted_refractive_index",
                document.config.transmittedRefractiveIndex},
        }},
    };
    return json.dump(2) + "\n";
}

ReflectionRefractionWorkbenchDocument
deserializeReflectionRefractionWorkbenchJson(std::string_view jsonText) {
    try {
        const Json json = Json::parse(jsonText);
        requireKeys(json, {
            "format_version", "model", "name", "provenance", "workbench",
        }, "reflection/refraction workbench");
        if (!json.at("format_version").is_number_integer()
            || json.at("format_version").get<int>()
                != kReflectionRefractionWorkbenchFormatVersion) {
            throw std::invalid_argument(
                "unsupported reflection/refraction workbench format version");
        }
        if (!json.at("model").is_string()
            || json.at("model").get<std::string>()
                != "reflection_refraction_workbench") {
            throw std::invalid_argument(
                "unsupported reflection/refraction workbench model");
        }
        if (!json.at("name").is_string()
            || json.at("name").get<std::string>().empty()) {
            throw std::invalid_argument(
                "reflection/refraction workbench name must be non-empty");
        }
        const auto& workbench = json.at("workbench");
        requireKeys(workbench, {
            "incidence_angle_rad", "incident_refractive_index",
            "transmitted_refractive_index",
        }, "reflection/refraction workbench physics");
        ReflectionRefractionWorkbenchDocument result {
            .formatVersion = kReflectionRefractionWorkbenchFormatVersion,
            .name = json.at("name").get<std::string>(),
            .provenance = parseProvenance(json.at("provenance")),
            .config = {
                .incidenceAngleRadians = finiteNumber(
                    workbench.at("incidence_angle_rad"), "incidence angle"),
                .incidentRefractiveIndex = finiteNumber(
                    workbench.at("incident_refractive_index"),
                    "incident refractive index"),
                .transmittedRefractiveIndex = finiteNumber(
                    workbench.at("transmitted_refractive_index"),
                    "transmitted refractive index"),
            },
        };
        validateReflectionRefractionWorkbench(result);
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::invalid_argument(
            std::string("invalid reflection/refraction workbench JSON: ")
            + error.what());
    }
}

void saveReflectionRefractionWorkbench(
    const std::filesystem::path& path,
    const ReflectionRefractionWorkbenchDocument& document) {
    const auto text = serializeReflectionRefractionWorkbenchJson(document);
    std::ofstream stream(path, std::ios::binary | std::ios::trunc);
    if (!stream) {
        throw std::runtime_error(
            "cannot open reflection/refraction workbench for writing");
    }
    stream.write(text.data(), static_cast<std::streamsize>(text.size()));
    if (!stream) {
        throw std::runtime_error(
            "failed to write reflection/refraction workbench");
    }
}

ReflectionRefractionWorkbenchDocument loadReflectionRefractionWorkbench(
    const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    if (!stream) {
        throw std::runtime_error(
            "cannot open reflection/refraction workbench for reading");
    }
    const std::string text {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
    if (!stream.eof() && stream.fail()) {
        throw std::runtime_error(
            "failed to read reflection/refraction workbench");
    }
    return deserializeReflectionRefractionWorkbenchJson(text);
}

} // namespace holobench::app::reflection
