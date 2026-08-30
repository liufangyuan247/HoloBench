#pragma once

#include <cstddef>

#include "core/field/FieldVisualization.hpp"
#include "optics/wave/WaveDetectorPipeline.hpp"

namespace holobench::app::waveui {

struct DetectorImageLayout final {
    float width = 0.0F;
    float height = 0.0F;
};

struct DetectorPixel final {
    std::size_t x = 0;
    std::size_t y = 0;
};

[[nodiscard]] bool samePhysicsConfig(
    const optics::wave::WaveDetectorConfig& lhs,
    const optics::wave::WaveDetectorConfig& rhs) noexcept;

[[nodiscard]] DetectorImageLayout fitDetectorImage(
    float availableWidth,
    float availableHeight,
    std::size_t pixelWidth,
    std::size_t pixelHeight) noexcept;

// The detector texture is displayed with ImGui UVs (0, 1) -> (1, 0), so
// display-space Y is inverted relative to the field's row-major Y index.
[[nodiscard]] bool mapDisplayPointToDetectorPixel(
    float localX,
    float localY,
    float displayedWidth,
    float displayedHeight,
    std::size_t pixelWidth,
    std::size_t pixelHeight,
    DetectorPixel& output) noexcept;

class WaveDetectorUiState final {
public:
    WaveDetectorUiState() = default;

    [[nodiscard]] const optics::wave::WaveDetectorConfig& draftConfig() const noexcept { return draftConfig_; }
    [[nodiscard]] const optics::wave::WaveDetectorConfig& appliedConfig() const noexcept { return appliedConfig_; }
    [[nodiscard]] field::FieldViewMode viewMode() const noexcept { return viewMode_; }
    [[nodiscard]] bool isDirty() const noexcept { return !samePhysicsConfig(draftConfig_, appliedConfig_); }

    void setDraftConfig(const optics::wave::WaveDetectorConfig& config) noexcept;
    void apply() noexcept;
    void requestRecompute() noexcept;
    void propagationSucceeded() noexcept;
    void setViewMode(field::FieldViewMode mode) noexcept;

    [[nodiscard]] bool consumePropagationRequest() noexcept;
    [[nodiscard]] bool consumeVisualizationRequest() noexcept;

private:
    optics::wave::WaveDetectorConfig draftConfig_;
    optics::wave::WaveDetectorConfig appliedConfig_;
    field::FieldViewMode viewMode_ = field::FieldViewMode::DecibelIntensity;
    bool propagationRequested_ = true;
    bool visualizationRequested_ = false;
};

} // namespace holobench::app::waveui
