#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <numbers>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraHogelDataset.hpp"

namespace chimera = holobench::app::chimera;

namespace {

const chimera::HogelAngularSample& findSample(
    const chimera::HogelDataset& dataset,
    std::string_view viewId,
    std::size_t hogelX,
    std::size_t hogelY) {
    const auto found = std::find_if(
        dataset.angularSamples.begin(),
        dataset.angularSamples.end(),
        [&](const auto& sample) {
            return sample.viewId == viewId
                && sample.hogelX == hogelX
                && sample.hogelY == hogelY;
        });
    REQUIRE(found != dataset.angularSamples.end());
    return *found;
}

} // namespace

TEST_CASE("canonical CHIMERA views generate bounded hashed hogel commands") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto views = chimera::makeCanonicalPerspectiveViews(recipe);
    const auto dataset = chimera::generateHogelDataset(recipe, views);

    CHECK(dataset.sourceViews.size() == 15U);
    CHECK(dataset.angularSamples.size() == 720U);
    CHECK(dataset.slmCommands.size() == 144U);
    CHECK(dataset.diagnostics.angularSampleCount == 720U);
    CHECK(dataset.diagnostics.slmCommandCount == 144U);
    CHECK(dataset.diagnostics.collidedSlmPixelCount == 0U);
    CHECK(dataset.diagnostics.allSamplesInsideSlm);
    CHECK(dataset.hashAlgorithm == chimera::kHogelDatasetHashAlgorithm);
    CHECK(dataset.contentHash.size() == 16U);
    for (const auto& command : dataset.slmCommands) {
        CHECK(command.pixels.size() == 15U);
    }
}

TEST_CASE("hogel angles use the analytic Fourier lens position and SLM pixel oracle") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto dataset = chimera::generateHogelDataset(
        recipe, chimera::makeCanonicalPerspectiveViews(recipe));
    const auto& sample = findSample(dataset, "view-x4-y0", 0U, 0U);

    const double expectedX = -recipe.relay.focalLengthMetres
        * std::tan(0.5 * recipe.targetHorizontalFieldOfViewRadians);
    const double expectedY = -recipe.relay.focalLengthMetres
        * std::tan(-0.5 * recipe.targetVerticalFieldOfViewRadians);
    CHECK(sample.slmPositionXMetres == doctest::Approx(expectedX).epsilon(1e-13));
    CHECK(sample.slmPositionYMetres == doctest::Approx(expectedY).epsilon(1e-13));

    const double normalizedX = expectedX / recipe.slm.widthMetres + 0.5;
    const double normalizedY = expectedY / recipe.slm.heightMetres + 0.5;
    const auto expectedColumn = static_cast<std::size_t>(std::floor(
        normalizedX * static_cast<double>(recipe.slm.pixelWidth)));
    const auto expectedRow = static_cast<std::size_t>(std::floor(
        normalizedY * static_cast<double>(recipe.slm.pixelHeight)));
    CHECK(sample.slmPixelColumn == expectedColumn);
    CHECK(sample.slmPixelRow == expectedRow);
}

TEST_CASE("hogel dataset generation canonicalizes perspective view order") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto views = chimera::makeCanonicalPerspectiveViews(recipe);
    const auto ordered = chimera::generateHogelDataset(recipe, views);
    std::reverse(views.begin(), views.end());
    const auto reversed = chimera::generateHogelDataset(recipe, views);

    CHECK(reversed == ordered);
    CHECK(reversed.contentHash == ordered.contentHash);
    CHECK(chimera::serializeHogelDataset(reversed)
        == chimera::serializeHogelDataset(ordered));
}

TEST_CASE("hogel dataset JSON round trips byte-stably and detects corruption") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto dataset = chimera::generateHogelDataset(
        recipe, chimera::makeCanonicalPerspectiveViews(recipe));
    const std::string encoded = chimera::serializeHogelDataset(dataset);
    const auto restored = chimera::parseHogelDataset(encoded);
    CHECK(restored == dataset);
    CHECK(chimera::serializeHogelDataset(restored) == encoded);

    auto corrupt = nlohmann::json::parse(encoded);
    corrupt["payload"]["source_views"][0]["pixels_linear_rgb"][0][0] = 0.25;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseHogelDataset(corrupt.dump())),
        std::runtime_error);

    auto unknown = nlohmann::json::parse(encoded);
    unknown["payload"]["hidden_raster"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseHogelDataset(unknown.dump())),
        std::runtime_error);

    auto wrongUnit = nlohmann::json::parse(encoded);
    wrongUnit["payload"]["units"]["length"] = "mm";
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseHogelDataset(wrongUnit.dump())),
        std::runtime_error);
}

TEST_CASE("hogel dataset rejects invalid source views and unsupported recipes") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto views = chimera::makeCanonicalPerspectiveViews(recipe);

    SUBCASE("duplicate view identity") {
        views[1].viewId = views[0].viewId;
        CHECK_THROWS_AS(
            static_cast<void>(chimera::generateHogelDataset(recipe, views)),
            std::invalid_argument);
    }
    SUBCASE("angle outside requested field of view") {
        views[0].horizontalAngleRadians
            = recipe.targetHorizontalFieldOfViewRadians;
        CHECK_THROWS_AS(
            static_cast<void>(chimera::generateHogelDataset(recipe, views)),
            std::invalid_argument);
    }
    SUBCASE("image dimensions do not match the hogel grid") {
        views[0].width += 1U;
        CHECK_THROWS_AS(
            static_cast<void>(chimera::generateHogelDataset(recipe, views)),
            std::invalid_argument);
    }
    SUBCASE("unsupported construction recipe") {
        auto impossible = recipe;
        impossible.targetHorizontalFieldOfViewRadians
            = 0.75 * std::numbers::pi;
        CHECK_THROWS_AS(
            static_cast<void>(chimera::generateHogelDataset(
                impossible, chimera::makeCanonicalPerspectiveViews(impossible))),
            std::invalid_argument);
    }
}
