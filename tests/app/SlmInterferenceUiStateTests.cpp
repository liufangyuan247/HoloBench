#include <doctest/doctest.h>

#include <string>
#include <utility>
#include <vector>

#include "app/SlmInterferenceUiState.hpp"

namespace slmexperiment = holobench::app::slmexperiment;
namespace slmui = holobench::app::slmui;
namespace slm = holobench::optics::slm;

TEST_SUITE("SlmInterferenceUiState") {

TEST_CASE("initial request and Apply each schedule the expensive experiment exactly once") {
    slmui::SlmInterferenceUiState state;
    CHECK(state.consumeSimulationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());

    auto draft = state.draftConfig();
    draft.referenceBeam.directionCosineX = 0.01;
    state.setDraftConfig(draft);
    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());

    state.apply();
    CHECK_FALSE(state.isDirty());
    CHECK(state.consumeSimulationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());
}

TEST_CASE("display changes request texture refresh without rerunning physics") {
    slmui::SlmInterferenceUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    state.simulationSucceeded();
    CHECK(state.consumeVisualizationRequest());

    state.setDisplayPlane(slmui::DisplayPlane::AngularIntensity);
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());
    state.setDisplayedWavelengthIndex(2);
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());
}

TEST_CASE("measured calibration remains draft provenance until Apply") {
    slmui::SlmInterferenceUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    slm::CalibratedSlmResponse response({
        {500e-9, {{0.0, 0.2, 0.0}, {1.0, 0.8, 1.0}}},
        {600e-9, {{0.0, 0.3, 0.1}, {1.0, 0.9, 1.1}}},
    });

    state.setCalibration(std::move(response), "lab/slm-42.json");

    CHECK(state.isDirty());
    CHECK(state.draftConfig().deviceResponseModel
        == slmexperiment::SlmDeviceResponseModel::CalibratedLut);
    CHECK(state.draftConfig().vacuumWavelengthsMetres == std::vector<double>{500e-9, 600e-9});
    CHECK(state.draftCalibrationSource() == "lab/slm-42.json");
    CHECK(state.appliedCalibrationSource() == "No measured LUT loaded");
    state.apply();
    CHECK(state.appliedCalibrationSource() == "lab/slm-42.json");
}

TEST_CASE("default teaching experiment is nontrivial and internally consistent") {
    const auto config = slmexperiment::makeDefaultSlmInterferenceExperimentConfig();
    CHECK(config.fieldWidth == 128);
    CHECK(config.vacuumWavelengthsMetres.size() == 3);
    CHECK(config.normalizedPixelCommands.size()
        == config.slm.pixelColumns * config.slm.pixelRows);
    CHECK(config.referenceBeam.directionCosineX != 0.0);
    CHECK(config.lcdTeaching.spectralTransmission.size() == 3);
}

} // TEST_SUITE("SlmInterferenceUiState")
