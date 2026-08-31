#include <doctest/doctest.h>

#include <stdexcept>

#include "app/HolographyUiState.hpp"

namespace lab = holobench::app::holographylab;
namespace ui = holobench::app::holographyui;

TEST_SUITE("HolographyUiState") {

TEST_CASE("draft edits remain dirty and do not simulate until Apply") {
    ui::HolographyUiState state;
    CHECK(state.consumeSimulationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());

    auto draft = state.draftConfig();
    draft.transfer.h2AxialPositionMetres += 0.001;
    state.setDraftConfig(draft);
    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());

    state.apply();
    CHECK_FALSE(state.isDirty());
    CHECK(state.consumeSimulationRequest());
    CHECK(state.appliedConfig().transfer.h2AxialPositionMetres
        == draft.transfer.h2AxialPositionMetres);

    draft = state.draftConfig();
    draft.volume.replayVacuumWavelengthMetres = 633e-9;
    state.setDraftConfig(draft);
    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());
}

TEST_CASE("display-only changes request visualization without physics recompute") {
    ui::HolographyUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    state.simulationSucceeded();
    CHECK(state.consumeVisualizationRequest());

    state.setDisplayPlane(ui::DisplayPlane::H2ReplayImage);
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.consumeSimulationRequest());
    state.setDisplayedChannel(9U);
    CHECK(state.displayedChannel() == 2U);
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.isDirty());
}

TEST_CASE("project replacement validates and stays apply gated") {
    ui::HolographyUiState state;
    static_cast<void>(state.consumeSimulationRequest());
    auto loaded = lab::makeDefaultHolographyLabConfig();
    loaded.fieldPitchXMetres = 7e-6;
    state.replaceDraftProject(loaded);
    CHECK(state.isDirty());
    CHECK_FALSE(state.consumeSimulationRequest());
    state.apply();
    CHECK(state.consumeSimulationRequest());

    loaded.fieldWidth = 0U;
    CHECK_THROWS_AS(state.replaceDraftProject(loaded), std::invalid_argument);
}

} // TEST_SUITE("HolographyUiState")
