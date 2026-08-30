#include <doctest/doctest.h>

#include "app/WaveDetectorUiState.hpp"

namespace holobench::app::waveui {
namespace {

TEST_SUITE("WaveDetectorUiState") {

TEST_CASE("initial request and Apply each trigger propagation exactly once") {
    WaveDetectorUiState state;
    CHECK(state.consumePropagationRequest());
    CHECK_FALSE(state.consumePropagationRequest());
    CHECK_FALSE(state.consumePropagationRequest());

    state.apply();
    CHECK(state.consumePropagationRequest());
    CHECK_FALSE(state.consumePropagationRequest());
}

TEST_CASE("draft edits remain dirty and do not recompute until Apply") {
    WaveDetectorUiState state;
    static_cast<void>(state.consumePropagationRequest());
    auto edited = state.draftConfig();
    edited.propagationDistanceMetres = 0.075;
    state.setDraftConfig(edited);

    CHECK(state.isDirty());
    CHECK_FALSE(state.consumePropagationRequest());
    state.apply();
    CHECK_FALSE(state.isDirty());
    CHECK(state.appliedConfig().propagationDistanceMetres == doctest::Approx(0.075));
    CHECK(state.consumePropagationRequest());
    CHECK_FALSE(state.consumePropagationRequest());
}

TEST_CASE("display-only changes refresh visualization without propagation") {
    WaveDetectorUiState state;
    static_cast<void>(state.consumePropagationRequest());
    state.propagationSucceeded();
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.consumeVisualizationRequest());

    state.setViewMode(field::FieldViewMode::WrappedPhase);
    CHECK(state.consumeVisualizationRequest());
    CHECK_FALSE(state.consumePropagationRequest());
    state.setViewMode(field::FieldViewMode::WrappedPhase);
    CHECK_FALSE(state.consumeVisualizationRequest());
}

TEST_CASE("detector image fitting preserves aspect ratio") {
    const auto wide = fitDetectorImage(800.0F, 300.0F, 400U, 200U);
    CHECK(wide.width == doctest::Approx(600.0F));
    CHECK(wide.height == doctest::Approx(300.0F));

    const auto tall = fitDetectorImage(200.0F, 500.0F, 100U, 200U);
    CHECK(tall.width == doctest::Approx(200.0F));
    CHECK(tall.height == doctest::Approx(400.0F));
}

TEST_CASE("probe mapping accounts for vertically flipped detector texture") {
    DetectorPixel pixel;
    REQUIRE(mapDisplayPointToDetectorPixel(0.0F, 0.0F, 400.0F, 200.0F, 4U, 2U, pixel));
    CHECK(pixel.x == 0U);
    CHECK(pixel.y == 1U);

    REQUIRE(mapDisplayPointToDetectorPixel(399.0F, 199.0F, 400.0F, 200.0F, 4U, 2U, pixel));
    CHECK(pixel.x == 3U);
    CHECK(pixel.y == 0U);
    CHECK_FALSE(mapDisplayPointToDetectorPixel(400.0F, 20.0F, 400.0F, 200.0F, 4U, 2U, pixel));
}

} // TEST_SUITE("WaveDetectorUiState")

} // namespace
} // namespace holobench::app::waveui
