#include <doctest/doctest.h>

#include <array>
#include <stdexcept>

#include <nlohmann/json.hpp>

#include "app/BenchHolographyPresets.hpp"
#include "app/BenchEditHistory.hpp"
#include "app/BenchRecordingRecipe.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;

namespace {

holography::PlateIncidentFieldSet fieldsFor(const app::BenchProject& project) {
    return holography::collectPlateIncidentFields(
        project.scene,
        ray::traceDynamicBench(project.scene),
        "plate-h1");
}

holography::PlateBranchPairSelection singlePair(
    const holography::PlateIncidentFieldSet& fields) {
    std::uint64_t objectId = 0U;
    std::uint64_t referenceId = 0U;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            objectId = branch.beam.provenance.branchId;
        } else {
            referenceId = branch.beam.provenance.branchId;
        }
    }
    if (objectId == 0U || referenceId == 0U) {
        throw std::logic_error("recording test pair was not found");
    }
    return {objectId, referenceId};
}

holography::ThinPlateRecordingOptions thinOptions() {
    holography::ThinPlateRecordingOptions options;
    options.sampling = {
        .sampleWidth = 64U,
        .sampleHeight = 32U,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 0.5e-3,
        .centreXMetres = 0.2e-3,
        .centreYMetres = -0.1e-3,
    };
    options.relativeIntensityReferenceWattsPerSquareMetre = 20e3;
    options.response = {
        .amplitudeBias = 0.15,
        .intensityToAmplitudeGain = 0.3,
        .minimumAmplitudeTransmission = 0.05,
        .maximumAmplitudeTransmission = 0.95,
    };
    return options;
}

} // namespace

TEST_CASE("thin recording recipe survives canonical project persistence and resolves current branches") {
    auto project = app::makeTransmissionHolographyPreset();
    const auto fields = fieldsFor(project);
    const auto selection = singlePair(fields);
    const std::array channels {selection};
    app::upsertRecordingRecipe(
        project,
        app::makeThinRecordingRecipe(
            "thin-green",
            fields,
            channels,
            thinOptions()));

    const std::string bytes = app::serializeBenchProject(project);
    const auto restored = app::parseBenchProject(bytes);
    CHECK(app::serializeBenchProject(restored) == bytes);
    REQUIRE(restored.recordingRecipes.size() == 1U);
    CHECK(restored.recordingRecipes.front().sampling
        == thinOptions().sampling);
    CHECK(restored.recordingRecipes.front().thinResponse
        == thinOptions().response);
    const auto resolved = app::resolveRecordingRecipe(
        fieldsFor(restored), restored.recordingRecipes.front());
    REQUIRE(resolved.channels.size() == 1U);
    CHECK(resolved.channels.front().objectBranchId == selection.objectBranchId);
    CHECK(resolved.channels.front().referenceBranchId
        == selection.referenceBranchId);
}

TEST_CASE("RGB recipe persists three ordered independent selectors without field caches") {
    auto project = app::makeRgbHolographyPreset();
    const auto fields = fieldsFor(project);
    const auto selections
        = holography::selectRgbThinTransmissionPairs(fields);
    app::upsertRecordingRecipe(
        project,
        app::makeThinRecordingRecipe(
            "rgb-full-colour",
            fields,
            selections,
            thinOptions()));

    const auto encoded = nlohmann::json::parse(
        app::serializeBenchProject(project));
    const auto& recipe = encoded.at("recording_recipes").at(0);
    REQUIRE(recipe.at("channels").size() == 3U);
    CHECK_FALSE(recipe.contains("field"));
    CHECK_FALSE(recipe.contains("recorded_intensity"));
    CHECK(recipe.at("channels").at(0).at("object_branch")
        .at("wavelength_m").get<double>()
        > recipe.at("channels").at(1).at("object_branch")
            .at("wavelength_m").get<double>());
    const auto restored = app::parseBenchProject(encoded.dump());
    const auto resolved = app::resolveRecordingRecipe(
        fieldsFor(restored), restored.recordingRecipes.front());
    CHECK(resolved.channels.size() == 3U);
}

TEST_CASE("volume recipe persists material and resolves reflection geometry") {
    auto project = app::makeReflectionHolographyPreset();
    const auto fields = fieldsFor(project);
    const auto selection = singlePair(fields);
    const holography::VolumePlateMaterial material {
        .averageRefractiveIndex = 1.62,
        .refractiveIndexModulation = 0.008,
        .isotropicLinearShrinkageFraction = 0.03,
    };
    app::upsertRecordingRecipe(
        project,
        app::makeVolumeRecordingRecipe(
            "reflection-green",
            fields,
            selection,
            thinOptions().sampling,
            material));

    const auto restored = app::parseBenchProject(
        app::serializeBenchProject(project));
    REQUIRE(restored.recordingRecipes.size() == 1U);
    CHECK(restored.recordingRecipes.front().model
        == app::HologramRecordingModel::VolumeGrating);
    CHECK(restored.recordingRecipes.front().volumeMaterial == material);
    const auto restoredFields = fieldsFor(restored);
    const auto resolved = app::resolveRecordingRecipe(
        restoredFields, restored.recordingRecipes.front());
    REQUIRE(resolved.channels.size() == 1U);
    const auto pair = holography::makePlateRecordingPair(
        restoredFields,
        resolved.channels.front().objectBranchId,
        resolved.channels.front().referenceBranchId);
    CHECK(pair.geometry == holography::PlateRecordingGeometry::Reflection);
}

TEST_CASE("RGB Denisyuk recipe persists three independent reflection gratings") {
    auto project = app::makeRgbDenisyukHolographyPreset();
    const auto fields = fieldsFor(project);
    const auto selections = holography::selectRgbReflectionPairs(fields);
    app::upsertRecordingRecipe(
        project,
        app::makeVolumeRecordingRecipe(
            "rgb-denisyuk",
            fields,
            selections,
            thinOptions().sampling,
            {}));
    const auto bytes = app::serializeBenchProject(project);
    const auto restored = app::parseBenchProject(bytes);
    CHECK(app::serializeBenchProject(restored) == bytes);
    REQUIRE(restored.recordingRecipes.size() == 1U);
    CHECK(restored.recordingRecipes.front().model
        == app::HologramRecordingModel::VolumeGrating);
    REQUIRE(restored.recordingRecipes.front().channels.size() == 3U);
    const auto restoredFields = fieldsFor(restored);
    const auto resolved = app::resolveRecordingRecipe(
        restoredFields, restored.recordingRecipes.front());
    REQUIRE(resolved.channels.size() == 3U);
    for (const auto& selection : resolved.channels) {
        CHECK(holography::makePlateRecordingPair(
            restoredFields,
            selection.objectBranchId,
            selection.referenceBranchId).geometry
            == holography::PlateRecordingGeometry::Reflection);
    }
}

TEST_CASE("legacy bench migrates empty recipes and corrupt recipes reject strictly") {
    auto project = app::makeTransmissionHolographyPreset();
    auto json = nlohmann::json::parse(app::serializeBenchProject(project));
    json.erase("recording_recipes");
    json["format_version"] = app::kLegacyBenchProjectFormatVersion;
    const auto migrated = app::parseBenchProject(json.dump());
    CHECK(migrated.formatVersion == app::kBenchProjectFormatVersion);
    CHECK(migrated.recordingRecipes.empty());

    const auto fields = fieldsFor(project);
    const std::array channels {singlePair(fields)};
    app::upsertRecordingRecipe(
        project,
        app::makeThinRecordingRecipe(
            "thin-green", fields, channels, thinOptions()));
    const auto valid = nlohmann::json::parse(
        app::serializeBenchProject(project));

    auto unknown = valid;
    unknown["recording_recipes"][0]["cached_field"] = nullptr;
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(unknown.dump())),
        std::runtime_error);

    auto missingPath = valid;
    missingPath["recording_recipes"][0]["channels"][0]
        ["object_branch"]["component_path"][0] = "missing-source";
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(missingPath.dump())),
        std::runtime_error);

    auto invalidWindow = valid;
    invalidWindow["recording_recipes"][0]["sampling"]
        ["extent_width_m"] = 10.0;
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(invalidWindow.dump())),
        std::runtime_error);

    auto invalidVolumeChannels = valid;
    invalidVolumeChannels["recording_recipes"][0]["model"]
        = "volume_grating";
    invalidVolumeChannels["recording_recipes"][0]["channels"].push_back(
        invalidVolumeChannels["recording_recipes"][0]["channels"][0]);
    invalidVolumeChannels["recording_recipes"][0]["channels"].push_back(
        invalidVolumeChannels["recording_recipes"][0]["channels"][0]);
    CHECK_THROWS_AS(
        static_cast<void>(
            app::parseBenchProject(invalidVolumeChannels.dump())),
        std::runtime_error);
}

TEST_CASE("recording recipes participate in bench undo and redo") {
    auto project = app::makeTransmissionHolographyPreset();
    app::BenchEditHistory history;
    history.reset(project);
    const auto fields = fieldsFor(project);
    const std::array channels {singlePair(fields)};
    app::upsertRecordingRecipe(
        project,
        app::makeThinRecordingRecipe(
            "thin-green", fields, channels, thinOptions()));

    CHECK(history.record(project));
    CHECK(history.undo().recordingRecipes.empty());
    REQUIRE(history.redo().recordingRecipes.size() == 1U);
    CHECK(history.current().recordingRecipes.front().recipeId
        == "thin-green");
}

TEST_CASE("routing edits leave a saved recipe explicit and unresolved") {
    auto project = app::makeTransmissionHolographyPreset();
    const auto fields = fieldsFor(project);
    const std::array channels {singlePair(fields)};
    app::upsertRecordingRecipe(
        project,
        app::makeThinRecordingRecipe(
            "thin-green", fields, channels, thinOptions()));

    auto object = *project.scene.find("object-green");
    object.transform.localXAxisInWorld = {0.0, 0.0, -1.0};
    object.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    object.transform.localZAxisInWorld = {1.0, 0.0, 0.0};
    project.scene.replace(object.id, object);
    app::validateBenchProject(project);

    const auto editedFields = fieldsFor(project);
    CHECK_THROWS_AS(
        static_cast<void>(app::resolveRecordingRecipe(
            editedFields, project.recordingRecipes.front())),
        std::invalid_argument);
    CHECK(project.recordingRecipes.front().recipeId == "thin-green");
}
