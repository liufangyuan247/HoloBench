#include "core/field/FieldVisualization.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <stdexcept>
#include <string>

namespace holobench::field {
namespace {

void requirePositiveFinite(double value, const char* name) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(std::string(name) + " must be positive and finite");
    }
}

void requireFinite(double value, const char* name) {
    if (!std::isfinite(value)) {
        throw std::invalid_argument(std::string(name) + " must be finite");
    }
}

[[nodiscard]] inline std::uint8_t toByte(double normalized) noexcept {
    if (!std::isfinite(normalized)) {
        return 0;
    }
    const double clamped = std::clamp(normalized, 0.0, 1.0);
    return static_cast<std::uint8_t>(std::round(clamped * 255.0));
}

[[nodiscard]] std::size_t checkedRgbaByteCount(std::size_t pixelCount) {
    if (pixelCount > std::numeric_limits<std::size_t>::max() / 4U) {
        throw std::overflow_error("RGBA image byte count overflows size_t");
    }
    return pixelCount * 4U;
}

void validateLinearOptions(const FieldVisualizationOptions& options) {
    if (!std::isfinite(options.maxIntensityReference) || options.maxIntensityReference < 0.0) {
        throw std::invalid_argument(
            "maxIntensityReference must be zero (automatic) or positive and finite");
    }
}

[[nodiscard]] RgbaColor evaluateGrayscale(double v) noexcept {
    const auto byteVal = toByte(v);
    return RgbaColor{byteVal, byteVal, byteVal, 255};
}

[[nodiscard]] RgbaColor evaluateTurbo(double v) noexcept {
    const double x = std::clamp(v, 0.0, 1.0);
    const double r = 0.13572138 + x * (4.61539260 + x * (-42.66032258 + x * (132.13108234 + x * (-152.94239396 + x * 59.28637943))));
    const double g = 0.09140261 + x * (2.19418839 + x * (4.84296658 + x * (-14.18503333 + x * (4.27729857 + x * 2.82956604))));
    const double b = 0.10667330 + x * (12.59254356 + x * (-60.01802309 + x * (109.07409214 + x * (-88.50849206 + x * 26.86134844))));
    return RgbaColor{toByte(r), toByte(g), toByte(b), 255};
}

[[nodiscard]] RgbaColor evaluateInferno(double v) noexcept {
    const double x = std::clamp(v, 0.0, 1.0);
    const double r = -0.0024 + x * (0.978 + x * (2.138 - x * 2.115));
    const double g = 0.0016 + x * (-0.347 + x * (3.048 - x * 1.705));
    const double b = 0.0130 + x * (3.593 + x * (-8.948 + x * 5.342));
    return RgbaColor{toByte(r), toByte(g), toByte(b), 255};
}

} // namespace

RgbaImage::RgbaImage(std::size_t width, std::size_t height, std::vector<std::uint8_t> rgbaBytes)
    : width_(width)
    , height_(height)
    , rgbaBytes_(std::move(rgbaBytes)) {
    if (width_ == 0 || height_ == 0) {
        throw std::invalid_argument("image dimensions must be nonzero");
    }
    if (width_ > std::numeric_limits<std::size_t>::max() / height_) {
        throw std::overflow_error("image pixel count overflows size_t");
    }
    const std::size_t expectedBytes = checkedRgbaByteCount(width_ * height_);
    if (rgbaBytes_.size() != expectedBytes) {
        throw std::invalid_argument("RGBA byte buffer size does not match width * height * 4");
    }
}

RgbaColor RgbaImage::pixel(std::size_t x, std::size_t y) const {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("image coordinate out of bounds");
    }
    const std::size_t index = (y * width_ + x) * 4;
    return RgbaColor{
        rgbaBytes_[index + 0],
        rgbaBytes_[index + 1],
        rgbaBytes_[index + 2],
        rgbaBytes_[index + 3]
    };
}

void RgbaImage::setPixel(std::size_t x, std::size_t y, RgbaColor color) {
    if (x >= width_ || y >= height_) {
        throw std::out_of_range("image coordinate out of bounds");
    }
    const std::size_t index = (y * width_ + x) * 4;
    rgbaBytes_[index + 0] = color.r;
    rgbaBytes_[index + 1] = color.g;
    rgbaBytes_[index + 2] = color.b;
    rgbaBytes_[index + 3] = color.a;
}

RgbaColor evaluateCyclicPhaseColormap(double phaseRadians) noexcept {
    if (!std::isfinite(phaseRadians)) {
        return RgbaColor{0, 0, 0, 255};
    }
    constexpr double pi = std::numbers::pi;
    constexpr double twoPi = 2.0 * std::numbers::pi;

    // Normalize [-pi, +pi) to [0, 1)
    double t = (phaseRadians + pi) / twoPi;
    t = t - std::floor(t);
    if (t < 0.0) {
        t += 1.0;
    }
    if (t >= 1.0) {
        t = 0.0;
    }

    const double hueDeg = t * 360.0;
    const double hh = hueDeg / 60.0;
    const int sector = static_cast<int>(hh) % 6;
    const double frac = hh - static_cast<double>(static_cast<int>(hh));
    const double q = 1.0 - frac;

    double r = 0.0;
    double g = 0.0;
    double b = 0.0;

    switch (sector) {
    case 0:
        r = 1.0;
        g = frac;
        b = 0.0;
        break;
    case 1:
        r = q;
        g = 1.0;
        b = 0.0;
        break;
    case 2:
        r = 0.0;
        g = 1.0;
        b = frac;
        break;
    case 3:
        r = 0.0;
        g = q;
        b = 1.0;
        break;
    case 4:
        r = frac;
        g = 0.0;
        b = 1.0;
        break;
    default:
        r = 1.0;
        g = 0.0;
        b = q;
        break;
    }

    return RgbaColor{toByte(r), toByte(g), toByte(b), 255};
}

RgbaColor evaluateColormap(double normalizedValue, ColormapKind colormap) noexcept {
    switch (colormap) {
    case ColormapKind::Grayscale:
        return evaluateGrayscale(normalizedValue);
    case ColormapKind::Inferno:
        return evaluateInferno(normalizedValue);
    case ColormapKind::Turbo:
        return evaluateTurbo(normalizedValue);
    case ColormapKind::CyclicPhase: {
        constexpr double pi = std::numbers::pi;
        const double phase = (std::clamp(normalizedValue, 0.0, 1.0) * 2.0 - 1.0) * pi;
        return evaluateCyclicPhaseColormap(phase);
    }
    }
    return evaluateTurbo(normalizedValue);
}

RgbaImage renderLinearIntensity(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options) {
    const auto intensityField = computeIntensity(field);
    return renderLinearIntensity(intensityField, options, nullptr);
}

RgbaImage renderLinearIntensity(
    const ScalarField2D& intensityField,
    const FieldVisualizationOptions& options,
    const std::vector<std::uint8_t>* validityMask) {
    validateLinearOptions(options);
    const std::size_t width = intensityField.width();
    const std::size_t height = intensityField.height();
    const auto samples = intensityField.samples();
    const std::size_t count = samples.size();

    if (validityMask != nullptr && validityMask->size() != count) {
        throw std::invalid_argument("validity mask size must match scalar field sample count");
    }

    double maxVal = 0.0;
    for (std::size_t i = 0; i < count; ++i) {
        const double v = samples[i];
        requireFinite(v, "intensity sample");
        if (v < 0.0) {
            throw std::invalid_argument("intensity sample must be non-negative");
        }
        if (validityMask == nullptr || (*validityMask)[i] != 0) {
            maxVal = std::max(maxVal, v);
        }
    }

    const double refMax = options.maxIntensityReference > 0.0
        ? options.maxIntensityReference
        : maxVal;

    std::vector<std::uint8_t> buffer(checkedRgbaByteCount(count));

    for (std::size_t i = 0; i < count; ++i) {
        if (validityMask != nullptr && (*validityMask)[i] == 0) {
            const auto c = options.invalidColor;
            buffer[i * 4 + 0] = c.r;
            buffer[i * 4 + 1] = c.g;
            buffer[i * 4 + 2] = c.b;
            buffer[i * 4 + 3] = c.a;
            continue;
        }

        const double norm = (refMax > 0.0) ? std::clamp(samples[i] / refMax, 0.0, 1.0) : 0.0;
        const auto c = evaluateColormap(norm, options.colormap);
        buffer[i * 4 + 0] = c.r;
        buffer[i * 4 + 1] = c.g;
        buffer[i * 4 + 2] = c.b;
        buffer[i * 4 + 3] = c.a;
    }

    return RgbaImage(width, height, std::move(buffer));
}

RgbaImage renderDecibelIntensity(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options) {
    requireFinite(options.floorDecibels, "floorDecibels");
    requireFinite(options.maxDecibels, "maxDecibels");
    if (options.floorDecibels >= options.maxDecibels) {
        throw std::invalid_argument("floorDecibels must be strictly less than maxDecibels");
    }
    requirePositiveFinite(options.decibelReferenceIntensity, "decibelReferenceIntensity");

    const auto dbField = computeDecibelIntensity(
        field,
        options.floorDecibels,
        options.decibelReferenceIntensity);

    return renderDecibelIntensity(dbField, options, nullptr);
}

RgbaImage renderDecibelIntensity(
    const ScalarField2D& decibelField,
    const FieldVisualizationOptions& options,
    const std::vector<std::uint8_t>* validityMask) {
    requireFinite(options.floorDecibels, "floorDecibels");
    requireFinite(options.maxDecibels, "maxDecibels");
    if (options.floorDecibels >= options.maxDecibels) {
        throw std::invalid_argument("floorDecibels must be strictly less than maxDecibels");
    }

    const std::size_t width = decibelField.width();
    const std::size_t height = decibelField.height();
    const auto samples = decibelField.samples();
    const std::size_t count = samples.size();

    if (validityMask != nullptr && validityMask->size() != count) {
        throw std::invalid_argument("validity mask size must match scalar field sample count");
    }

    const double range = options.maxDecibels - options.floorDecibels;
    std::vector<std::uint8_t> buffer(checkedRgbaByteCount(count));

    for (std::size_t i = 0; i < count; ++i) {
        if (validityMask != nullptr && (*validityMask)[i] == 0) {
            const auto c = options.invalidColor;
            buffer[i * 4 + 0] = c.r;
            buffer[i * 4 + 1] = c.g;
            buffer[i * 4 + 2] = c.b;
            buffer[i * 4 + 3] = c.a;
            continue;
        }

        const double val = samples[i];
        requireFinite(val, "decibel sample");
        const double norm = std::clamp((val - options.floorDecibels) / range, 0.0, 1.0);
        const auto c = evaluateColormap(norm, options.colormap);
        buffer[i * 4 + 0] = c.r;
        buffer[i * 4 + 1] = c.g;
        buffer[i * 4 + 2] = c.b;
        buffer[i * 4 + 3] = c.a;
    }

    return RgbaImage(width, height, std::move(buffer));
}

RgbaImage renderWrappedPhase(
    const PhaseResult& phaseResult,
    const FieldVisualizationOptions& options) {
    const auto& phaseField = phaseResult.wrappedPhaseRadians();
    const auto& validity = phaseResult.validityMask();
    const std::size_t width = phaseField.width();
    const std::size_t height = phaseField.height();
    const auto samples = phaseField.samples();
    const std::size_t count = samples.size();

    if (validity.size() != count) {
        throw std::invalid_argument("validity mask size must match phase sample count");
    }

    std::vector<std::uint8_t> buffer(checkedRgbaByteCount(count));

    for (std::size_t i = 0; i < count; ++i) {
        if (validity[i] == 0) {
            const auto c = options.invalidColor;
            buffer[i * 4 + 0] = c.r;
            buffer[i * 4 + 1] = c.g;
            buffer[i * 4 + 2] = c.b;
            buffer[i * 4 + 3] = c.a;
            continue;
        }

        const double phase = samples[i];
        requireFinite(phase, "phase sample");
        const auto c = evaluateCyclicPhaseColormap(phase);
        buffer[i * 4 + 0] = c.r;
        buffer[i * 4 + 1] = c.g;
        buffer[i * 4 + 2] = c.b;
        buffer[i * 4 + 3] = c.a;
    }

    return RgbaImage(width, height, std::move(buffer));
}

RgbaImage renderWrappedPhase(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options) {
    if (!std::isfinite(options.phaseMinimumIntensity) || options.phaseMinimumIntensity < 0.0) {
        throw std::invalid_argument("phaseMinimumIntensity must be non-negative and finite");
    }
    const auto phaseResult = computeWrappedPhase(field, options.phaseMinimumIntensity);
    return renderWrappedPhase(phaseResult, options);
}

RgbaImage renderFieldView(
    const ComplexField2D& field,
    FieldViewMode mode,
    const FieldVisualizationOptions& options) {
    switch (mode) {
    case FieldViewMode::Intensity:
        return renderLinearIntensity(field, options);
    case FieldViewMode::DecibelIntensity:
        return renderDecibelIntensity(field, options);
    case FieldViewMode::WrappedPhase:
        return renderWrappedPhase(field, options);
    }
    return renderLinearIntensity(field, options);
}

} // namespace holobench::field
