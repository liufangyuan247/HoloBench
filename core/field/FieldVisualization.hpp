#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "core/field/ComplexField2D.hpp"
#include "core/field/FieldObservables.hpp"
#include "core/field/ScalarField2D.hpp"

namespace holobench::field {

/**
 * @brief Supported false-color colormaps for scalar and phase field visualization.
 */
enum class ColormapKind {
    Grayscale,
    Inferno,
    Turbo,
    CyclicPhase
};

/**
 * @brief Display mode for optical wave detector views.
 */
enum class FieldViewMode {
    Intensity,
    DecibelIntensity,
    WrappedPhase
};

/**
 * @brief 8-bit per channel RGBA color representation.
 */
struct RgbaColor final {
    std::uint8_t r = 0;
    std::uint8_t g = 0;
    std::uint8_t b = 0;
    std::uint8_t a = 255;

    constexpr bool operator==(const RgbaColor&) const noexcept = default;
};

/**
 * @brief Deterministic CPU RGBA pixel image buffer with row-major layout.
 */
class RgbaImage final {
public:
    RgbaImage(std::size_t width, std::size_t height, std::vector<std::uint8_t> rgbaBytes);

    [[nodiscard]] std::size_t width() const noexcept { return width_; }
    [[nodiscard]] std::size_t height() const noexcept { return height_; }
    [[nodiscard]] std::size_t pixelCount() const noexcept { return width_ * height_; }
    [[nodiscard]] std::size_t byteCount() const noexcept { return rgbaBytes_.size(); }
    [[nodiscard]] std::span<const std::uint8_t> rgbaBytes() const noexcept { return rgbaBytes_; }
    [[nodiscard]] std::span<std::uint8_t> rgbaBytes() noexcept { return rgbaBytes_; }

    [[nodiscard]] RgbaColor pixel(std::size_t x, std::size_t y) const;
    void setPixel(std::size_t x, std::size_t y, RgbaColor color);

private:
    std::size_t width_;
    std::size_t height_;
    std::vector<std::uint8_t> rgbaBytes_;
};

/**
 * @brief Maps normalized value [0.0, 1.0] to an RGBA color according to chosen colormap.
 */
[[nodiscard]] RgbaColor evaluateColormap(double normalizedValue, ColormapKind colormap) noexcept;

/**
 * @brief Maps wrapped phase angle in [-pi, +pi) to a continuous cyclic RGBA color wheel.
 */
[[nodiscard]] RgbaColor evaluateCyclicPhaseColormap(double phaseRadians) noexcept;

/**
 * @brief Options for field visualization rendering.
 */
struct FieldVisualizationOptions final {
    ColormapKind colormap = ColormapKind::Turbo;
    double maxIntensityReference = 0.0; // <= 0 means auto-normalize to peak intensity
    double floorDecibels = -60.0;       // Lower bound for dB view (must be < maxDecibels)
    double maxDecibels = 0.0;           // Upper bound for dB view (must be > floorDecibels)
    double decibelReferenceIntensity = 1.0; // Strictly positive reference for dB conversion
    double phaseMinimumIntensity = 0.0; // Min intensity threshold for valid phase
    RgbaColor invalidColor{20, 20, 26, 255}; // Distinct color for masked / sub-threshold samples
};

/**
 * @brief Renders linear intensity field to RGBA image.
 *
 * @param field Complex optical field.
 * @param options Visualization options.
 * @return RgbaImage Deterministic RGBA pixel buffer.
 * @throws std::invalid_argument On NaN/Inf samples or invalid parameters.
 */
[[nodiscard]] RgbaImage renderLinearIntensity(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options = {});

/**
 * @brief Renders scalar intensity field to RGBA image.
 */
[[nodiscard]] RgbaImage renderLinearIntensity(
    const ScalarField2D& intensityField,
    const FieldVisualizationOptions& options = {},
    const std::vector<std::uint8_t>* validityMask = nullptr);

/**
 * @brief Renders decibel log intensity field to RGBA image.
 *
 * @param field Complex optical field.
 * @param options Visualization options (uses floorDecibels, maxDecibels, decibelReferenceIntensity).
 * @return RgbaImage Deterministic RGBA pixel buffer.
 * @throws std::invalid_argument On NaN/Inf samples or invalid parameters (floorDecibels >= maxDecibels, ref <= 0).
 */
[[nodiscard]] RgbaImage renderDecibelIntensity(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options = {});

/**
 * @brief Renders precomputed decibel intensity field to RGBA image.
 */
[[nodiscard]] RgbaImage renderDecibelIntensity(
    const ScalarField2D& decibelField,
    const FieldVisualizationOptions& options = {},
    const std::vector<std::uint8_t>* validityMask = nullptr);

/**
 * @brief Renders principal wrapped phase [-pi, +pi) to RGBA image using cyclic color wheel.
 *
 * @param phaseResult PhaseResult with wrapped phase in radians and pointwise validityMask.
 * @param options Visualization options (uses invalidColor, colormap).
 * @return RgbaImage Deterministic RGBA pixel buffer.
 * @throws std::invalid_argument On NaN/Inf samples or invalid parameters.
 */
[[nodiscard]] RgbaImage renderWrappedPhase(
    const PhaseResult& phaseResult,
    const FieldVisualizationOptions& options = {});

/**
 * @brief Renders principal wrapped phase from complex field to RGBA image.
 */
[[nodiscard]] RgbaImage renderWrappedPhase(
    const ComplexField2D& field,
    const FieldVisualizationOptions& options = {});

/**
 * @brief Unified render dispatcher based on FieldViewMode.
 */
[[nodiscard]] RgbaImage renderFieldView(
    const ComplexField2D& field,
    FieldViewMode mode,
    const FieldVisualizationOptions& options = {});

} // namespace holobench::field
