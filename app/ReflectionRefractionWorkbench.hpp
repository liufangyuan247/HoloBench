#pragma once

#include <filesystem>
#include <string>
#include <string_view>

#include "core/project/ProjectProvenance.hpp"

namespace holobench::app::reflection {

inline constexpr int kReflectionRefractionWorkbenchFormatVersion = 1;

struct ReflectionRefractionConfig final {
    double incidenceAngleRadians = 0.5235987755982988;
    double incidentRefractiveIndex = 1.0;
    double transmittedRefractiveIndex = 1.5;

    bool operator==(const ReflectionRefractionConfig&) const = default;
};

struct ReflectionRefractionResult final {
    double incidenceAngleRadians = 0.0;
    double reflectionAngleRadians = 0.0;
    double transmissionAngleRadians = 0.0;
    double reflectionAngleErrorRadians = 0.0;
    double snellResidual = 0.0;
    bool totalInternalReflection = false;

    bool operator==(const ReflectionRefractionResult&) const = default;
};

struct ReflectionRefractionWorkbenchDocument final {
    int formatVersion = kReflectionRefractionWorkbenchFormatVersion;
    std::string name = "Reflection & Refraction Workbench";
    project::ProjectProvenance provenance;
    ReflectionRefractionConfig config;

    bool operator==(const ReflectionRefractionWorkbenchDocument&) const = default;
};

void validateReflectionRefractionConfig(
    const ReflectionRefractionConfig& config);
[[nodiscard]] ReflectionRefractionResult evaluateReflectionRefraction(
    const ReflectionRefractionConfig& config);
void validateReflectionRefractionWorkbench(
    const ReflectionRefractionWorkbenchDocument& document);
[[nodiscard]] std::string serializeReflectionRefractionWorkbenchJson(
    const ReflectionRefractionWorkbenchDocument& document);
[[nodiscard]] ReflectionRefractionWorkbenchDocument
deserializeReflectionRefractionWorkbenchJson(std::string_view jsonText);
void saveReflectionRefractionWorkbench(
    const std::filesystem::path& path,
    const ReflectionRefractionWorkbenchDocument& document);
[[nodiscard]] ReflectionRefractionWorkbenchDocument
loadReflectionRefractionWorkbench(const std::filesystem::path& path);

} // namespace holobench::app::reflection
