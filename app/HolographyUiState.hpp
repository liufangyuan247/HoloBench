#pragma once

#include <cstddef>
#include <utility>

#include "app/HolographyLabPipeline.hpp"

namespace holobench::app::holographyui {

enum class DisplayPlane {
    H1Exposure,
    H1RealImage,
    H2Exposure,
    H2ReplayImage,
};

[[nodiscard]] bool sameHolographyLabConfig(
    const holographylab::HolographyLabConfig& lhs,
    const holographylab::HolographyLabConfig& rhs) noexcept;

class HolographyUiState final {
public:
    HolographyUiState();
    explicit HolographyUiState(holographylab::HolographyLabConfig initialConfig);

    [[nodiscard]] const holographylab::HolographyLabConfig& draftConfig() const noexcept {
        return draftConfig_;
    }
    [[nodiscard]] const holographylab::HolographyLabConfig& appliedConfig() const noexcept {
        return appliedConfig_;
    }
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] DisplayPlane displayPlane() const noexcept { return displayPlane_; }
    [[nodiscard]] std::size_t displayedChannel() const noexcept {
        return displayedChannel_;
    }

    void setDraftConfig(const holographylab::HolographyLabConfig& config);
    void replaceDraftProject(holographylab::HolographyLabConfig config);
    void apply();
    void requestSimulation() noexcept;
    void simulationSucceeded() noexcept;
    void setDisplayPlane(DisplayPlane plane) noexcept;
    void setDisplayedChannel(std::size_t channel) noexcept;

    [[nodiscard]] bool consumeSimulationRequest() noexcept;
    [[nodiscard]] bool consumeVisualizationRequest() noexcept;

private:
    holographylab::HolographyLabConfig draftConfig_;
    holographylab::HolographyLabConfig appliedConfig_;
    DisplayPlane displayPlane_ = DisplayPlane::H1RealImage;
    std::size_t displayedChannel_ = 1U;
    bool simulationRequested_ = true;
    bool visualizationRequested_ = false;
};

} // namespace holobench::app::holographyui
