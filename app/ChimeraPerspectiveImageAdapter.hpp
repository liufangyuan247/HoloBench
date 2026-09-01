#pragma once

#include <cstddef>
#include <filesystem>
#include <string>
#include <vector>

#include "app/ChimeraHogelDataset.hpp"

namespace holobench::app::chimera {

enum class RgbTransferFunction {
    Linear,
    Srgb,
};

struct NormalizedRgbSample final {
    double red = 0.0;
    double green = 0.0;
    double blue = 0.0;

    bool operator==(const NormalizedRgbSample&) const = default;
};

// Decoder-neutral row-major RGB input. Row zero maps to hogel row zero; file
// decoders must make any orientation transform explicit before adaptation.
struct RgbRasterImage final {
    std::size_t width = 0;
    std::size_t height = 0;
    RgbTransferFunction transferFunction = RgbTransferFunction::Srgb;
    std::vector<NormalizedRgbSample> pixels;

    bool operator==(const RgbRasterImage&) const = default;
};

struct PerspectiveRasterView final {
    std::string viewId;
    double horizontalAngleRadians = 0.0;
    double verticalAngleRadians = 0.0;
    RgbRasterImage image;

    bool operator==(const PerspectiveRasterView&) const = default;
};

struct PerspectiveImageAdapterOptions final {
    std::size_t maximumInputPixelsPerView = 16'777'216U;

    bool operator==(const PerspectiveImageAdapterOptions&) const = default;
};

// Loads a strict Netpbm P3 or P6 RGB file. P6 supports both 8-bit and
// big-endian 16-bit samples. The caller declares whether normalized samples
// are linear or sRGB; PPM does not carry a reliable transfer-function tag.
[[nodiscard]] RgbRasterImage loadPortablePixmap(
    const std::filesystem::path& path,
    RgbTransferFunction transferFunction = RgbTransferFunction::Srgb);

// Converts real decoded rasters into the explicit one-linear-RGB-sample-per-
// hogel perspective contract. It uses exact source-pixel area weighting for
// deterministic anti-aliased down/up-sampling and IEC 61966-2-1 sRGB decoding.
[[nodiscard]] std::vector<PerspectiveViewImage> adaptPerspectiveRasterViews(
    const ChimeraRecipe& recipe,
    std::vector<PerspectiveRasterView> inputs,
    const PerspectiveImageAdapterOptions& options = {});

} // namespace holobench::app::chimera
