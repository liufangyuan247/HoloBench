#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

#include "app/ChimeraRecipe.hpp"

namespace holobench::app::chimera {

inline constexpr int kHogelDatasetFormatVersion = 1;
inline constexpr std::string_view kHogelDatasetHashAlgorithm
    = "fnv1a64-canonical-json-v1";

struct LinearRgb final {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    bool operator==(const LinearRgb&) const = default;
};

// One perspective image contains one linear-RGB source sample per requested
// hogel. Later image adapters may resample larger source images into this
// explicit grid before dataset generation.
struct PerspectiveViewImage final {
    std::string viewId;
    double horizontalAngleRadians = 0.0;
    double verticalAngleRadians = 0.0;
    std::size_t width = 0;
    std::size_t height = 0;
    std::vector<LinearRgb> pixels;

    bool operator==(const PerspectiveViewImage&) const = default;
};

struct HogelAngularSample final {
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    std::string viewId;
    double horizontalAngleRadians = 0.0;
    double verticalAngleRadians = 0.0;
    double slmPositionXMetres = 0.0;
    double slmPositionYMetres = 0.0;
    std::size_t slmPixelColumn = 0;
    std::size_t slmPixelRow = 0;
    LinearRgb linearRgb;

    bool operator==(const HogelAngularSample&) const = default;
};

struct SlmCommandPixel final {
    std::string viewId;
    std::size_t column = 0;
    std::size_t row = 0;
    double normalizedAmplitude = 0.0;

    bool operator==(const SlmCommandPixel&) const = default;
};

struct SlmHogelCommand final {
    std::string commandId;
    std::size_t hogelX = 0;
    std::size_t hogelY = 0;
    std::string channelId;
    double wavelengthMetres = 532e-9;
    std::vector<SlmCommandPixel> pixels;

    bool operator==(const SlmHogelCommand&) const = default;
};

struct HogelDatasetDiagnostics final {
    std::size_t angularSampleCount = 0;
    std::size_t slmCommandCount = 0;
    std::size_t collidedSlmPixelCount = 0;
    double maximumAbsoluteSlmXMetres = 0.0;
    double maximumAbsoluteSlmYMetres = 0.0;
    bool allSamplesInsideSlm = false;

    bool operator==(const HogelDatasetDiagnostics&) const = default;
};

struct HogelDataset final {
    int formatVersion = kHogelDatasetFormatVersion;
    std::string datasetId;
    std::string sourceRecipeId;
    int sourceRecipeVersion = kChimeraRecipeFormatVersion;
    std::string angleUnit = "rad";
    std::string lengthUnit = "m";
    std::string colourUnit = "normalized_linear_rgb";
    std::string spatialIndexUnit = "zero_based_hogel_and_pixel_index";
    HogelGeometry hogels;
    std::size_t slmPixelWidth = 0;
    std::size_t slmPixelHeight = 0;
    std::vector<PerspectiveViewImage> sourceViews;
    std::vector<HogelAngularSample> angularSamples;
    std::vector<SlmHogelCommand> slmCommands;
    HogelDatasetDiagnostics diagnostics;
    std::string hashAlgorithm = std::string(kHogelDatasetHashAlgorithm);
    std::string contentHash;

    bool operator==(const HogelDataset&) const = default;
};

[[nodiscard]] std::vector<PerspectiveViewImage>
makeCanonicalPerspectiveViews(
    const ChimeraRecipe& recipe,
    std::size_t horizontalViewCount = 5,
    std::size_t verticalViewCount = 3);

[[nodiscard]] HogelDataset generateHogelDataset(
    const ChimeraRecipe& recipe,
    std::vector<PerspectiveViewImage> sourceViews);

void validateHogelDataset(const HogelDataset& dataset);
[[nodiscard]] std::string computeHogelDatasetContentHash(
    const HogelDataset& dataset);
[[nodiscard]] std::string serializeHogelDataset(const HogelDataset& dataset);
[[nodiscard]] HogelDataset parseHogelDataset(std::string_view jsonText);

} // namespace holobench::app::chimera
