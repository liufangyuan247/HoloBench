#include "app/ChimeraPerspectiveImageAdapter.hpp"

#include <algorithm>
#include <charconv>
#include <cmath>
#include <cstdint>
#include <fstream>
#include <iterator>
#include <limits>
#include <set>
#include <stdexcept>
#include <string_view>

#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

constexpr std::size_t kMaximumPortablePixmapPixels = 16'777'216U;
constexpr std::size_t kMaximumAdaptedHogelPixels = 5'000'000U;

std::size_t checkedProduct(
    std::size_t first,
    std::size_t second,
    std::string_view context) {
    if (first != 0U
        && second > std::numeric_limits<std::size_t>::max() / first) {
        throw std::overflow_error(std::string(context) + " size overflows");
    }
    return first * second;
}

void validateNormalizedSample(
    const NormalizedRgbSample& sample,
    std::string_view context) {
    const auto valid = [](double value) {
        return std::isfinite(value) && value >= 0.0 && value <= 1.0;
    };
    if (!valid(sample.red) || !valid(sample.green) || !valid(sample.blue)) {
        throw std::invalid_argument(
            std::string(context) + " must be finite normalized RGB");
    }
}

double decodeSrgbChannel(double value) {
    return value <= 0.04045
        ? value / 12.92
        : std::pow((value + 0.055) / 1.055, 2.4);
}

LinearRgb decodeSample(
    const NormalizedRgbSample& sample,
    RgbTransferFunction transferFunction) {
    validateNormalizedSample(sample, "perspective raster sample");
    switch (transferFunction) {
    case RgbTransferFunction::Linear:
        return {.red = sample.red, .green = sample.green, .blue = sample.blue};
    case RgbTransferFunction::Srgb:
        return {
            .red = decodeSrgbChannel(sample.red),
            .green = decodeSrgbChannel(sample.green),
            .blue = decodeSrgbChannel(sample.blue),
        };
    }
    throw std::invalid_argument("perspective raster transfer function is invalid");
}

class PortablePixmapCursor final {
public:
    explicit PortablePixmapCursor(const std::vector<unsigned char>& bytes)
        : bytes_(bytes) {}

    [[nodiscard]] std::string token(std::string_view context) {
        skipWhitespaceAndComments();
        const std::size_t start = position_;
        while (position_ < bytes_.size()
            && !isWhitespace(bytes_[position_])
            && bytes_[position_] != '#') {
            ++position_;
        }
        if (position_ == start) {
            throw std::runtime_error(
                "portable pixmap is missing " + std::string(context));
        }
        return std::string(
            reinterpret_cast<const char*>(bytes_.data() + start),
            position_ - start);
    }

    [[nodiscard]] std::size_t unsignedToken(std::string_view context) {
        const std::string text = token(context);
        std::size_t value = 0U;
        const auto parsed = std::from_chars(
            text.data(), text.data() + text.size(), value);
        if (parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) {
            throw std::runtime_error(
                "portable pixmap has invalid " + std::string(context));
        }
        return value;
    }

    void consumeBinarySeparator() {
        if (position_ >= bytes_.size() || !isWhitespace(bytes_[position_])) {
            throw std::runtime_error(
                "binary portable pixmap header lacks a raster separator");
        }
        const unsigned char first = bytes_[position_++];
        if (first == '\r' && position_ < bytes_.size()
            && bytes_[position_] == '\n') {
            ++position_;
        }
    }

    [[nodiscard]] std::size_t remaining() const noexcept {
        return bytes_.size() - position_;
    }

    [[nodiscard]] unsigned char byte() {
        if (position_ >= bytes_.size()) {
            throw std::runtime_error("portable pixmap raster is truncated");
        }
        return bytes_[position_++];
    }

    [[nodiscard]] bool hasTextAfterWhitespaceAndComments() {
        skipWhitespaceAndComments();
        return position_ != bytes_.size();
    }

private:
    static bool isWhitespace(unsigned char value) noexcept {
        return value == ' ' || value == '\t' || value == '\r'
            || value == '\n' || value == '\f' || value == '\v';
    }

    void skipWhitespaceAndComments() {
        for (;;) {
            while (position_ < bytes_.size()
                && isWhitespace(bytes_[position_])) {
                ++position_;
            }
            if (position_ >= bytes_.size() || bytes_[position_] != '#') return;
            while (position_ < bytes_.size()
                && bytes_[position_] != '\n'
                && bytes_[position_] != '\r') {
                ++position_;
            }
        }
    }

    const std::vector<unsigned char>& bytes_;
    std::size_t position_ = 0U;
};

NormalizedRgbSample normalizedSample(
    std::size_t red,
    std::size_t green,
    std::size_t blue,
    std::size_t maximum) {
    const double scale = 1.0 / static_cast<double>(maximum);
    return {
        .red = static_cast<double>(red) * scale,
        .green = static_cast<double>(green) * scale,
        .blue = static_cast<double>(blue) * scale,
    };
}

LinearRgb resampleArea(
    const RgbRasterImage& image,
    std::size_t outputX,
    std::size_t outputY,
    std::size_t outputWidth,
    std::size_t outputHeight) {
    const double sourceX0 = static_cast<double>(outputX)
        * static_cast<double>(image.width) / static_cast<double>(outputWidth);
    const double sourceX1 = static_cast<double>(outputX + 1U)
        * static_cast<double>(image.width) / static_cast<double>(outputWidth);
    const double sourceY0 = static_cast<double>(outputY)
        * static_cast<double>(image.height) / static_cast<double>(outputHeight);
    const double sourceY1 = static_cast<double>(outputY + 1U)
        * static_cast<double>(image.height) / static_cast<double>(outputHeight);
    const std::size_t firstX = static_cast<std::size_t>(std::floor(sourceX0));
    const std::size_t lastX = std::min(
        static_cast<std::size_t>(std::ceil(sourceX1)), image.width);
    const std::size_t firstY = static_cast<std::size_t>(std::floor(sourceY0));
    const std::size_t lastY = std::min(
        static_cast<std::size_t>(std::ceil(sourceY1)), image.height);

    LinearRgb accumulated;
    for (std::size_t y = firstY; y < lastY; ++y) {
        const double overlapY = std::max(0.0,
            std::min(sourceY1, static_cast<double>(y + 1U))
                - std::max(sourceY0, static_cast<double>(y)));
        for (std::size_t x = firstX; x < lastX; ++x) {
            const double overlapX = std::max(0.0,
                std::min(sourceX1, static_cast<double>(x + 1U))
                    - std::max(sourceX0, static_cast<double>(x)));
            const double weight = overlapX * overlapY;
            const auto sample = decodeSample(
                image.pixels[y * image.width + x], image.transferFunction);
            accumulated.red += sample.red * weight;
            accumulated.green += sample.green * weight;
            accumulated.blue += sample.blue * weight;
        }
    }
    const double area = (sourceX1 - sourceX0) * (sourceY1 - sourceY0);
    if (!std::isfinite(area) || area <= 0.0) {
        throw std::runtime_error("perspective raster resampling area is invalid");
    }
    accumulated.red = std::clamp(accumulated.red / area, 0.0, 1.0);
    accumulated.green = std::clamp(accumulated.green / area, 0.0, 1.0);
    accumulated.blue = std::clamp(accumulated.blue / area, 0.0, 1.0);
    return accumulated;
}

} // namespace

RgbRasterImage loadPortablePixmap(
    const std::filesystem::path& path,
    RgbTransferFunction transferFunction) {
    if (transferFunction != RgbTransferFunction::Linear
        && transferFunction != RgbTransferFunction::Srgb) {
        throw std::invalid_argument(
            "portable pixmap transfer function is invalid");
    }
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error("failed to open portable pixmap: " + path.string());
    }
    const std::vector<unsigned char> bytes {
        std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
    if (input.bad()) {
        throw std::runtime_error("failed to read portable pixmap: " + path.string());
    }
    PortablePixmapCursor cursor(bytes);
    const std::string magic = cursor.token("magic");
    if (magic != "P3" && magic != "P6") {
        throw std::runtime_error("portable pixmap must use P3 or P6 RGB format");
    }
    const std::size_t width = cursor.unsignedToken("width");
    const std::size_t height = cursor.unsignedToken("height");
    const std::size_t maximum = cursor.unsignedToken("maximum sample");
    if (width == 0U || height == 0U || maximum == 0U || maximum > 65'535U) {
        throw std::runtime_error("portable pixmap dimensions or maximum are invalid");
    }
    const std::size_t pixelCount = checkedProduct(
        width, height, "portable pixmap");
    if (pixelCount > kMaximumPortablePixmapPixels) {
        throw std::runtime_error("portable pixmap exceeds the bounded pixel limit");
    }

    RgbRasterImage result {
        .width = width,
        .height = height,
        .transferFunction = transferFunction,
        .pixels = {},
    };
    result.pixels.reserve(pixelCount);
    if (magic == "P3") {
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const std::size_t red = cursor.unsignedToken("red sample");
            const std::size_t green = cursor.unsignedToken("green sample");
            const std::size_t blue = cursor.unsignedToken("blue sample");
            if (red > maximum || green > maximum || blue > maximum) {
                throw std::runtime_error(
                    "portable pixmap sample exceeds the declared maximum");
            }
            result.pixels.push_back(
                normalizedSample(red, green, blue, maximum));
        }
        if (cursor.hasTextAfterWhitespaceAndComments()) {
            throw std::runtime_error("portable pixmap has trailing sample data");
        }
    } else {
        cursor.consumeBinarySeparator();
        const std::size_t bytesPerSample = maximum < 256U ? 1U : 2U;
        const std::size_t expectedBytes = checkedProduct(
            checkedProduct(pixelCount, 3U, "portable pixmap raster"),
            bytesPerSample,
            "portable pixmap raster");
        if (cursor.remaining() != expectedBytes) {
            throw std::runtime_error(
                "binary portable pixmap raster size does not match its header");
        }
        const auto sample = [&]() -> std::size_t {
            if (bytesPerSample == 1U) return cursor.byte();
            const std::size_t high = cursor.byte();
            const std::size_t low = cursor.byte();
            return (high << 8U) | low;
        };
        for (std::size_t pixel = 0; pixel < pixelCount; ++pixel) {
            const std::size_t red = sample();
            const std::size_t green = sample();
            const std::size_t blue = sample();
            if (red > maximum || green > maximum || blue > maximum) {
                throw std::runtime_error(
                    "portable pixmap sample exceeds the declared maximum");
            }
            result.pixels.push_back(
                normalizedSample(red, green, blue, maximum));
        }
    }
    return result;
}

std::vector<PerspectiveViewImage> adaptPerspectiveRasterViews(
    const ChimeraRecipe& recipe,
    std::vector<PerspectiveRasterView> inputs,
    const PerspectiveImageAdapterOptions& options) {
    validateChimeraRecipe(recipe);
    if (inputs.empty()) {
        throw std::invalid_argument(
            "perspective raster adapter requires at least one view");
    }
    if (options.maximumInputPixelsPerView == 0U
        || options.maximumInputPixelsPerView
            > kMaximumPortablePixmapPixels) {
        throw std::invalid_argument(
            "perspective raster input limit must be in [1, 16777216]");
    }
    const std::size_t outputPixelCount = checkedProduct(
        recipe.hogels.countX,
        recipe.hogels.countY,
        "adapted perspective view");
    if (outputPixelCount > kMaximumAdaptedHogelPixels) {
        throw std::invalid_argument(
            "adapted perspective view exceeds the bounded hogel limit");
    }

    std::set<std::string> viewIds;
    std::vector<PerspectiveViewImage> result;
    result.reserve(inputs.size());
    for (const auto& input : inputs) {
        if (!optics::scene::isStableBenchId(input.viewId)
            || !viewIds.insert(input.viewId).second) {
            throw std::invalid_argument(
                "perspective raster view IDs must be unique stable IDs");
        }
        if (!std::isfinite(input.horizontalAngleRadians)
            || !std::isfinite(input.verticalAngleRadians)
            || std::abs(input.horizontalAngleRadians)
                > 0.5 * recipe.targetHorizontalFieldOfViewRadians + 1e-15
            || std::abs(input.verticalAngleRadians)
                > 0.5 * recipe.targetVerticalFieldOfViewRadians + 1e-15) {
            throw std::invalid_argument(
                "perspective raster view angle lies outside the recipe FOV");
        }
        const std::size_t inputPixelCount = checkedProduct(
            input.image.width,
            input.image.height,
            "perspective raster");
        if (input.image.width == 0U || input.image.height == 0U
            || inputPixelCount != input.image.pixels.size()
            || inputPixelCount > options.maximumInputPixelsPerView) {
            throw std::invalid_argument(
                "perspective raster dimensions or bounded pixel count are invalid");
        }
        for (const auto& sample : input.image.pixels) {
            validateNormalizedSample(sample, "perspective raster sample");
        }

        PerspectiveViewImage view {
            .viewId = input.viewId,
            .horizontalAngleRadians = input.horizontalAngleRadians,
            .verticalAngleRadians = input.verticalAngleRadians,
            .width = recipe.hogels.countX,
            .height = recipe.hogels.countY,
            .pixels = {},
        };
        view.pixels.reserve(outputPixelCount);
        for (std::size_t y = 0; y < view.height; ++y) {
            for (std::size_t x = 0; x < view.width; ++x) {
                view.pixels.push_back(resampleArea(
                    input.image, x, y, view.width, view.height));
            }
        }
        result.push_back(std::move(view));
    }
    return result;
}

} // namespace holobench::app::chimera
