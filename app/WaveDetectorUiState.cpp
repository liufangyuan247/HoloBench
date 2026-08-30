#include "app/WaveDetectorUiState.hpp"

#include <algorithm>
#include <cmath>
#include <limits>
#include <utility>

namespace holobench::app::waveui {

bool samePhysicsConfig(
    const optics::wave::WaveDetectorConfig& lhs,
    const optics::wave::WaveDetectorConfig& rhs) noexcept {
    return lhs.sourceKind == rhs.sourceKind
        && lhs.wavelengthMetres == rhs.wavelengthMetres
        && lhs.sourceAmplitude == rhs.sourceAmplitude
        && lhs.gaussianWaistRadiusMetres == rhs.gaussianWaistRadiusMetres
        && lhs.planeWaveDirectionCosineX == rhs.planeWaveDirectionCosineX
        && lhs.planeWaveDirectionCosineY == rhs.planeWaveDirectionCosineY
        && lhs.sourcePhaseAtOriginRadians == rhs.sourcePhaseAtOriginRadians
        && lhs.apertureKind == rhs.apertureKind
        && lhs.circularApertureRadiusMetres == rhs.circularApertureRadiusMetres
        && lhs.rectangularHalfWidthMetres == rhs.rectangularHalfWidthMetres
        && lhs.rectangularHalfHeightMetres == rhs.rectangularHalfHeightMetres
        && lhs.doubleSlitWidthMetres == rhs.doubleSlitWidthMetres
        && lhs.doubleSlitHeightMetres == rhs.doubleSlitHeightMetres
        && lhs.doubleSlitSeparationMetres == rhs.doubleSlitSeparationMetres
        && lhs.apertureCenterXMetres == rhs.apertureCenterXMetres
        && lhs.apertureCenterYMetres == rhs.apertureCenterYMetres
        && lhs.enableThinLens == rhs.enableThinLens
        && lhs.thinLensFocalLengthMetres == rhs.thinLensFocalLengthMetres
        && lhs.thinLensCenterXMetres == rhs.thinLensCenterXMetres
        && lhs.thinLensCenterYMetres == rhs.thinLensCenterYMetres
        && lhs.propagator == rhs.propagator
        && lhs.propagationDistanceMetres == rhs.propagationDistanceMetres
        && lhs.gridResolution == rhs.gridResolution
        && lhs.gridPhysicalSpanMetres == rhs.gridPhysicalSpanMetres
        && lhs.refractiveIndex == rhs.refractiveIndex;
}

DetectorImageLayout fitDetectorImage(
    float availableWidth,
    float availableHeight,
    std::size_t pixelWidth,
    std::size_t pixelHeight) noexcept {
    if (!(availableWidth > 0.0F) || !(availableHeight > 0.0F)
        || !std::isfinite(availableWidth) || !std::isfinite(availableHeight)
        || pixelWidth == 0U || pixelHeight == 0U) {
        return {};
    }
    const double aspect = static_cast<double>(pixelWidth) / static_cast<double>(pixelHeight);
    double width = static_cast<double>(availableWidth);
    double height = width / aspect;
    if (height > static_cast<double>(availableHeight)) {
        height = static_cast<double>(availableHeight);
        width = height * aspect;
    }
    if (!std::isfinite(width) || !std::isfinite(height)
        || width > static_cast<double>(std::numeric_limits<float>::max())
        || height > static_cast<double>(std::numeric_limits<float>::max())) {
        return {};
    }
    return {static_cast<float>(width), static_cast<float>(height)};
}

bool mapDisplayPointToDetectorPixel(
    float localX,
    float localY,
    float displayedWidth,
    float displayedHeight,
    std::size_t pixelWidth,
    std::size_t pixelHeight,
    DetectorPixel& output) noexcept {
    if (!(displayedWidth > 0.0F) || !(displayedHeight > 0.0F)
        || !std::isfinite(displayedWidth) || !std::isfinite(displayedHeight)
        || !std::isfinite(localX) || !std::isfinite(localY)
        || localX < 0.0F || localY < 0.0F || localX >= displayedWidth || localY >= displayedHeight
        || pixelWidth == 0U || pixelHeight == 0U) {
        return false;
    }
    const double normalizedX = static_cast<double>(localX) / static_cast<double>(displayedWidth);
    const double normalizedY = static_cast<double>(localY) / static_cast<double>(displayedHeight);
    const auto x = static_cast<std::size_t>(normalizedX * static_cast<double>(pixelWidth));
    const auto displayY = static_cast<std::size_t>(normalizedY * static_cast<double>(pixelHeight));
    output.x = std::min(x, pixelWidth - 1U);
    output.y = pixelHeight - 1U - std::min(displayY, pixelHeight - 1U);
    return true;
}

void WaveDetectorUiState::setDraftConfig(const optics::wave::WaveDetectorConfig& config) noexcept {
    draftConfig_ = config;
}

void WaveDetectorUiState::apply() noexcept {
    appliedConfig_ = draftConfig_;
    propagationRequested_ = true;
}

void WaveDetectorUiState::requestRecompute() noexcept {
    propagationRequested_ = true;
}

void WaveDetectorUiState::propagationSucceeded() noexcept {
    visualizationRequested_ = true;
}

void WaveDetectorUiState::setViewMode(field::FieldViewMode mode) noexcept {
    if (mode != viewMode_) {
        viewMode_ = mode;
        visualizationRequested_ = true;
    }
}

bool WaveDetectorUiState::consumePropagationRequest() noexcept {
    return std::exchange(propagationRequested_, false);
}

bool WaveDetectorUiState::consumeVisualizationRequest() noexcept {
    return std::exchange(visualizationRequested_, false);
}

} // namespace holobench::app::waveui
