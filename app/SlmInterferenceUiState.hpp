#pragma once

#include <cstddef>
#include <string>

#include "app/SlmInterferencePipeline.hpp"

namespace holobench::app::slmui {

enum class DisplayPlane {
    Interference,
    AngularIntensity,
    SelectedPixelPsf,
};

[[nodiscard]] bool sameExperimentPhysicsConfig(
    const slmexperiment::SlmInterferenceExperimentConfig& lhs,
    const slmexperiment::SlmInterferenceExperimentConfig& rhs) noexcept;

class SlmInterferenceUiState final {
public:
    SlmInterferenceUiState();
    explicit SlmInterferenceUiState(
        slmexperiment::SlmInterferenceExperimentConfig initialConfig);

    [[nodiscard]] const slmexperiment::SlmInterferenceExperimentConfig& draftConfig() const noexcept {
        return draftConfig_;
    }
    [[nodiscard]] const slmexperiment::SlmInterferenceExperimentConfig& appliedConfig() const noexcept {
        return appliedConfig_;
    }
    [[nodiscard]] bool isDirty() const noexcept;
    [[nodiscard]] DisplayPlane displayPlane() const noexcept { return displayPlane_; }
    [[nodiscard]] std::size_t displayedWavelengthIndex() const noexcept {
        return displayedWavelengthIndex_;
    }
    [[nodiscard]] const std::string& draftCalibrationSource() const noexcept {
        return draftCalibrationSource_;
    }
    [[nodiscard]] const std::string& appliedCalibrationSource() const noexcept {
        return appliedCalibrationSource_;
    }

    void setDraftConfig(const slmexperiment::SlmInterferenceExperimentConfig& config);
    void replaceDraftProject(
        slmexperiment::SlmInterferenceExperimentConfig config,
        std::string calibrationSource);
    void setCalibration(
        optics::slm::CalibratedSlmResponse response,
        std::string source);
    void clearCalibration();
    void apply();
    void requestSimulation() noexcept;
    void simulationSucceeded() noexcept;
    void setDisplayPlane(DisplayPlane plane) noexcept;
    void setDisplayedWavelengthIndex(std::size_t index) noexcept;

    [[nodiscard]] bool consumeSimulationRequest() noexcept;
    [[nodiscard]] bool consumeVisualizationRequest() noexcept;

private:
    slmexperiment::SlmInterferenceExperimentConfig draftConfig_;
    slmexperiment::SlmInterferenceExperimentConfig appliedConfig_;
    std::string draftCalibrationSource_ = "No measured LUT loaded";
    std::string appliedCalibrationSource_ = "No measured LUT loaded";
    DisplayPlane displayPlane_ = DisplayPlane::Interference;
    std::size_t displayedWavelengthIndex_ = 0;
    bool simulationRequested_ = true;
    bool visualizationRequested_ = false;
};

} // namespace holobench::app::slmui
