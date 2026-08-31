#pragma once

#include <array>
#include <cstddef>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

#include "app/BenchProject.hpp"

namespace holobench::app::chimera {

inline constexpr int kChimeraRecipeFormatVersion = 1;
inline constexpr int kChimeraRecipeCompilerVersion = 1;

struct HogelGeometry final {
    double pitchMetres = 1e-3;
    std::size_t countX = 8;
    std::size_t countY = 6;

    bool operator==(const HogelGeometry&) const = default;
};

struct SpectralArm final {
    std::string channelId;
    double wavelengthMetres = 532e-9;
    double objectPowerWatts = 0.15;
    double referencePowerWatts = 0.20;

    bool operator==(const SpectralArm&) const = default;
};

struct SlmSpecification final {
    double widthMetres = 15.36e-3;
    double heightMetres = 8.64e-3;
    std::size_t pixelWidth = 1920;
    std::size_t pixelHeight = 1080;
    double fillFactor = 0.93;
    unsigned int bitDepth = 8;
    double phaseRangeRadians = 6.283185307179586476925286766559;

    bool operator==(const SlmSpecification&) const = default;
};

struct RelaySpecification final {
    double focalLengthMetres = 50e-3;
    double clearApertureDiameterMetres = 25e-3;
    double stopDiameterMetres = 16e-3;

    bool operator==(const RelaySpecification&) const = default;
};

struct ReferenceSpecification final {
    double sourceXMetres = 0.18;
    double mirrorXMetres = 0.03;
    double mirrorZMetres = -0.30;
    double splitterDistanceAfterMirrorMetres = 0.08;
    double armSeparationMetres = 0.03;
    double splitterPowerTransmission = 0.90;

    bool operator==(const ReferenceSpecification&) const = default;
};

struct PlateSpecification final {
    double thicknessMetres = 30e-6;
    double averageRefractiveIndex = 1.52;
    double refractiveIndexModulation = 0.008;
    double isotropicLinearShrinkageFraction = 0.0;

    bool operator==(const PlateSpecification&) const = default;
};

struct ExposurePolicy final {
    double exposureSecondsPerChannel = 0.04;
    std::size_t sampleWidth = 1024;
    std::size_t sampleHeight = 1024;

    bool operator==(const ExposurePolicy&) const = default;
};

struct ChimeraRecipe final {
    int formatVersion = kChimeraRecipeFormatVersion;
    std::string recipeId = "canonical-chimera";
    std::string name = "Canonical CHIMERA-like Virtual Printer";
    HogelGeometry hogels;
    double targetHorizontalFieldOfViewRadians = 0.2617993877991494;
    double targetVerticalFieldOfViewRadians = 0.1396263401595464;
    std::array<SpectralArm, 3> rgb {{
        {.channelId = "red", .wavelengthMetres = 638e-9},
        {.channelId = "green", .wavelengthMetres = 532e-9},
        {.channelId = "blue", .wavelengthMetres = 450e-9},
    }};
    SlmSpecification slm;
    RelaySpecification relay;
    ReferenceSpecification reference;
    PlateSpecification plate;
    ExposurePolicy exposure;

    bool operator==(const ChimeraRecipe&) const = default;
};

enum class ConstraintSeverity {
    Feasible,
    Warning,
    Unsupported,
};

struct ConstraintReportEntry final {
    ConstraintSeverity severity = ConstraintSeverity::Feasible;
    std::string code;
    std::string message;

    bool operator==(const ConstraintReportEntry&) const = default;
};

struct GeneratedComponentProvenance final {
    std::string componentId;
    std::string generatedRole;
    std::string channelId;
    std::string recipeId;
    int recipeVersion = kChimeraRecipeFormatVersion;
    int compilerVersion = kChimeraRecipeCompilerVersion;

    bool operator==(const GeneratedComponentProvenance&) const = default;
};

struct CompileResult final {
    BenchProject project;
    std::vector<GeneratedComponentProvenance> generatedComponents;
    std::vector<ConstraintReportEntry> constraints;

    [[nodiscard]] bool feasible() const noexcept;
};

void validateChimeraRecipe(const ChimeraRecipe& recipe);
[[nodiscard]] ChimeraRecipe makeCanonicalChimeraRecipe();
[[nodiscard]] CompileResult compileChimeraRecipe(const ChimeraRecipe& recipe);

[[nodiscard]] std::string serializeChimeraRecipe(const ChimeraRecipe& recipe);
[[nodiscard]] ChimeraRecipe parseChimeraRecipe(std::string_view jsonText);
void saveChimeraRecipe(
    const ChimeraRecipe& recipe,
    const std::filesystem::path& path);
[[nodiscard]] ChimeraRecipe loadChimeraRecipe(
    const std::filesystem::path& path);

} // namespace holobench::app::chimera
