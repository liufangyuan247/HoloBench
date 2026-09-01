#include <doctest/doctest.h>

#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>
#include <fstream>
#include <string>

#include <nlohmann/json.hpp>

#include "app/BenchProject.hpp"

namespace app = holobench::app;
namespace scene = holobench::optics::scene;

namespace {

class TemporaryBenchFile final {
public:
    TemporaryBenchFile() {
        static std::atomic<unsigned long long> sequence {0U};
        const auto nonce = std::chrono::steady_clock::now()
            .time_since_epoch().count();
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-bench-recovery-" + std::to_string(nonce) + "-"
                + std::to_string(sequence.fetch_add(1U)) + ".json");
    }

    ~TemporaryBenchFile() {
        std::error_code ignored;
        static_cast<void>(std::filesystem::remove(path_, ignored));
        const auto autosave = app::benchProjectAutosavePath(path_);
        static_cast<void>(std::filesystem::remove(autosave, ignored));
        auto primaryTemporary = path_;
        primaryTemporary += ".write.tmp";
        static_cast<void>(std::filesystem::remove(primaryTemporary, ignored));
        auto autosaveTemporary = autosave;
        autosaveTemporary += ".write.tmp";
        static_cast<void>(std::filesystem::remove(autosaveTemporary, ignored));
    }

    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

void writeInvalidJson(const std::filesystem::path& path) {
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    REQUIRE(output.good());
    output << "{truncated";
    REQUIRE(output.good());
}

} // namespace

TEST_CASE("unified bench project round trips every kind arbitrary transforms and RGB channels byte-stably") {
    app::BenchProject project;
    project.projectId = "rgb-holography-bench";
    project.name = "RGB Holography Bench";

    const double angle = 0.37;
    std::size_t index = 0;
    for (const auto kind : scene::requiredBenchComponentKinds()) {
        auto component = scene::makeDefaultBenchComponent(
            kind, "component-" + std::to_string(20 - index));
        component.transform = {
            .translationMetres = {0.01 * static_cast<double>(index), -0.02, 0.03},
            .localXAxisInWorld = {std::cos(angle), 0.0, -std::sin(angle)},
            .localYAxisInWorld = {0.0, 1.0, 0.0},
            .localZAxisInWorld = {std::sin(angle), 0.0, std::cos(angle)},
        };
        if (kind == scene::BenchComponentKind::LaserSource) {
            auto parameters = std::get<scene::LaserSourceParameters>(component.parameters);
            parameters.channels = {
                {.wavelengthMetres = 638e-9, .powerWatts = 0.8, .coherenceId = "rgb-red"},
                {.wavelengthMetres = 532e-9, .powerWatts = 0.7, .coherenceId = "rgb-green"},
                {.wavelengthMetres = 450e-9, .powerWatts = 0.6, .coherenceId = "rgb-blue"},
            };
            component.parameters = parameters;
        } else if (kind
            == scene::BenchComponentKind::SpatialLightModulator) {
            auto parameters
                = std::get<scene::SpatialLightModulatorParameters>(
                    component.parameters);
            parameters.commandPattern = scene::SlmCommandPattern::LinearRamp;
            parameters.commandOrigin = scene::SlmCommandOrigin::Automation;
            parameters.commandId = "rgb-hogel-42";
            parameters.primaryCommand = 0.25;
            parameters.horizontalCycles = 7.0;
            parameters.verticalCycles = -3.0;
            parameters.bitDepth = 10U;
            component.parameters = parameters;
        }
        project.scene.add(std::move(component));
        ++index;
    }

    const std::string firstBytes = app::serializeBenchProject(project);
    const auto loaded = app::parseBenchProject(firstBytes);
    const std::string secondBytes = app::serializeBenchProject(loaded);
    CHECK(firstBytes == secondBytes);
    CHECK(loaded.scene.revision() == 12);
    REQUIRE(loaded.scene.components().size() == 12);

    const auto* laser = loaded.scene.find("component-20");
    REQUIRE(laser != nullptr);
    const auto& channels = std::get<scene::LaserSourceParameters>(laser->parameters).channels;
    REQUIRE(channels.size() == 3);
    CHECK(channels[0].coherenceId == "rgb-red");
    CHECK(laser->transform.localZAxisInWorld.x == doctest::Approx(std::sin(angle)));

    const auto* slm = loaded.scene.find("component-12");
    REQUIRE(slm != nullptr);
    const auto& slmParameters
        = std::get<scene::SpatialLightModulatorParameters>(slm->parameters);
    CHECK(slmParameters.commandId == "rgb-hogel-42");
    CHECK(slmParameters.commandOrigin == scene::SlmCommandOrigin::Automation);
    CHECK(slmParameters.horizontalCycles == doctest::Approx(7.0));
}

TEST_CASE("format two bench SLM migrates to explicit manual zero-phase command") {
    app::BenchProject project;
    project.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator, "legacy-slm"));
    auto json = nlohmann::json::parse(app::serializeBenchProject(project));
    json["format_version"] = app::kRecipeBenchProjectFormatVersion;
    json["components"][0].erase("instrument");
    json["components"][0].erase("mechanical_assembly");
    auto& parameters = json["components"][0]["parameters"];
    const nlohmann::json legacy {
        {"fill_factor", parameters.at("fill_factor")},
        {"height_m", parameters.at("height_m")},
        {"pixel_height", parameters.at("pixel_height")},
        {"pixel_width", parameters.at("pixel_width")},
        {"width_m", parameters.at("width_m")},
    };
    parameters = legacy;

    const auto migrated = app::parseBenchProject(json.dump());
    CHECK(migrated.formatVersion == app::kBenchProjectFormatVersion);
    const auto& restored = std::get<scene::SpatialLightModulatorParameters>(
        migrated.scene.find("legacy-slm")->parameters);
    CHECK(restored.commandOrigin == scene::SlmCommandOrigin::Manual);
    CHECK(restored.commandPattern == scene::SlmCommandPattern::Uniform);
    CHECK(restored.primaryCommand == doctest::Approx(0.0));
}

TEST_CASE("format five persists instrument calibration and migrates format four") {
    app::BenchProject project;
    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "mounted-lens");
    lens.transform.translationMetres = {0.1, 0.12, 0.3};
    auto assembly = scene::makeDefaultMechanicalAssembly(lens);
    assembly.stageTranslationMetres = {0.004, 0.0, -0.003};
    assembly.mountYawRadians = 0.15;
    assembly.mountPitchRadians = -0.05;
    scene::applyMechanicalAssembly(lens, assembly);
    lens.instrument.manufacturer = "HoloBench Reference";
    lens.instrument.model = "Nominal Lens Mount";
    lens.instrument.serialNumber = "HB-LENS-0001";
    lens.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    lens.instrument.calibrationAssets.push_back({
        .kind = scene::CalibrationAssetKind::OpticalPose,
        .calibrationId = "lens-pose-2026",
        .formatVersion = 1,
        .source = "calibration/lens-pose-2026.json",
        .contentSha256
            = "abcdef0123456789abcdef0123456789abcdef0123456789abcdef0123456789",
        .specificationId = lens.instrument.specificationId,
        .specificationVersion = lens.instrument.specificationVersion,
        .validity = {},
    });
    project.scene.add(lens);

    const auto bytes = app::serializeBenchProject(project);
    const auto json = nlohmann::json::parse(bytes);
    CHECK(json.at("format_version").get<int>()
        == app::kBenchProjectFormatVersion);
    CHECK(json["components"][0]["mechanical_assembly"].is_object());
    CHECK(json["components"][0]["instrument"].is_object());
    const auto restored = app::parseBenchProject(bytes);
    CHECK(app::serializeBenchProject(restored) == bytes);
    const auto* restoredLens = restored.scene.find("mounted-lens");
    REQUIRE(restoredLens != nullptr);
    REQUIRE(restoredLens->mechanicalAssembly.has_value());
    CHECK(restoredLens->mechanicalAssembly->stageTranslationMetres.x
        == doctest::Approx(0.004));
    CHECK(restoredLens->transform == lens.transform);
    CHECK(restoredLens->instrument == lens.instrument);
    CHECK(scene::instrumentCalibrationState(restoredLens->instrument)
        == scene::InstrumentCalibrationState::Calibrated);

    auto inconsistent = json;
    inconsistent["components"][0]["mechanical_assembly"]
        ["stage_translation_m"][0] = 0.010;
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(inconsistent.dump())),
        std::runtime_error);

    auto legacy = json;
    legacy["format_version"]
        = app::kMechanicalAssemblyBenchProjectFormatVersion;
    legacy["components"][0].erase("instrument");
    const auto migrated = app::parseBenchProject(legacy.dump());
    CHECK(migrated.formatVersion == app::kBenchProjectFormatVersion);
    const auto* migratedLens = migrated.scene.find("mounted-lens");
    REQUIRE(migratedLens != nullptr);
    CHECK(migratedLens->mechanicalAssembly.has_value());
    CHECK(migratedLens->instrument
        == scene::makeDefaultInstrumentIdentity(
            scene::BenchComponentKind::IdealThinLens));

    auto corruptHash = json;
    corruptHash["components"][0]["instrument"]["calibration_assets"][0]
        ["content_sha256"] = "bad";
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(corruptHash.dump())),
        std::runtime_error);

    auto missingInstrumentField = json;
    missingInstrumentField["components"][0]["instrument"].erase(
        "specification_id");
    CHECK_THROWS_AS(
        static_cast<void>(
            app::parseBenchProject(missingInstrumentField.dump())),
        std::runtime_error);

    auto unknownInstrumentField = json;
    unknownInstrumentField["components"][0]["instrument"]["vendor_hint"]
        = "must-not-select-a-backend";
    CHECK_THROWS_AS(
        static_cast<void>(
            app::parseBenchProject(unknownInstrumentField.dump())),
        std::runtime_error);
}

TEST_CASE("bench project parser rejects unknown keys duplicate IDs and invalid physical parameters") {
    app::BenchProject project;
    project.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner, "splitter-1"));
    auto json = nlohmann::json::parse(app::serializeBenchProject(project));

    auto unknownKey = json;
    unknownKey["unexpected"] = true;
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(unknownKey.dump())), std::runtime_error);

    auto duplicate = json;
    duplicate["components"].push_back(duplicate["components"][0]);
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(duplicate.dump())), std::runtime_error);

    auto invalidPower = json;
    invalidPower["components"][0]["parameters"]["power_reflectivity"] = 0.8;
    invalidPower["components"][0]["parameters"]["power_transmissivity"] = 0.8;
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(invalidPower.dump())), std::runtime_error);

    auto invalidTransform = json;
    invalidTransform["components"][0]["transform"]["x_axis_world"] = {2.0, 0.0, 0.0};
    CHECK_THROWS_AS(static_cast<void>(app::parseBenchProject(invalidTransform.dump())), std::runtime_error);
}

TEST_CASE("double-slit aperture round trips canonically without changing legacy aperture bytes") {
    app::BenchProject project;
    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "double-slit");
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::DoubleSlit;
    parameters.widthMetres = 0.006;
    parameters.heightMetres = 0.006;
    parameters.slitWidthMetres = 80e-6;
    parameters.slitHeightMetres = 3e-3;
    parameters.slitSeparationMetres = 400e-6;
    aperture.parameters = parameters;
    project.scene.add(aperture);

    const auto bytes = app::serializeBenchProject(project);
    const auto restored = app::parseBenchProject(bytes);
    CHECK(app::serializeBenchProject(restored) == bytes);
    const auto& restoredParameters = std::get<scene::ApertureParameters>(
        restored.scene.find("double-slit")->parameters);
    CHECK(restoredParameters.shape == scene::ApertureShape::DoubleSlit);
    CHECK(restoredParameters.slitSeparationMetres
        == doctest::Approx(400e-6));

    auto json = nlohmann::json::parse(bytes);
    json["components"][0]["parameters"]["slit_separation_m"] = 40e-6;
    CHECK_THROWS_AS(
        static_cast<void>(app::parseBenchProject(json.dump())),
        std::runtime_error);

    app::BenchProject legacyShape;
    auto circular = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "circle");
    legacyShape.scene.add(circular);
    const auto legacyJson = nlohmann::json::parse(
        app::serializeBenchProject(legacyShape));
    CHECK(legacyJson["components"][0]["parameters"].size() == 3U);
    CHECK(legacyJson["components"][0]["parameters"].contains("shape"));
}

TEST_CASE("bench project file persistence uses the same canonical representation") {
    const TemporaryBenchFile file;
    app::BenchProject project;
    project.projectId = "file-round-trip";
    project.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate-h1"));

    app::saveBenchProject(project, file.path());
    const auto loaded = app::loadBenchProject(file.path());
    CHECK(app::serializeBenchProject(loaded) == app::serializeBenchProject(project));
    auto temporary = file.path();
    temporary += ".write.tmp";
    CHECK_FALSE(std::filesystem::exists(temporary));
}

TEST_CASE("valid autosave is preferred and can recover a corrupt primary") {
    const TemporaryBenchFile file;
    app::BenchProject primary;
    primary.projectId = "primary";
    primary.name = "Explicit save";
    app::saveBenchProject(primary, file.path());

    auto edited = primary;
    edited.name = "Recovered unsaved edit";
    edited.scene.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "recovered-plate"));
    app::saveBenchProjectAutosave(edited, file.path());

    auto recovery = app::loadBenchProjectWithRecovery(file.path());
    CHECK(recovery.source == app::BenchProjectRecoverySource::Autosave);
    CHECK_FALSE(recovery.ignoredInvalidAutosave);
    CHECK(app::serializeBenchProject(recovery.project)
        == app::serializeBenchProject(edited));

    writeInvalidJson(file.path());
    recovery = app::loadBenchProjectWithRecovery(file.path());
    CHECK(recovery.source == app::BenchProjectRecoverySource::Autosave);
    CHECK(app::serializeBenchProject(recovery.project)
        == app::serializeBenchProject(edited));
}

TEST_CASE("invalid autosave falls back to a valid primary and is reported") {
    const TemporaryBenchFile file;
    app::BenchProject primary;
    primary.projectId = "valid-primary";
    primary.name = "Valid primary";
    app::saveBenchProject(primary, file.path());
    writeInvalidJson(app::benchProjectAutosavePath(file.path()));

    const auto recovery = app::loadBenchProjectWithRecovery(file.path());
    CHECK(recovery.source == app::BenchProjectRecoverySource::Primary);
    CHECK(recovery.ignoredInvalidAutosave);
    CHECK(app::serializeBenchProject(recovery.project)
        == app::serializeBenchProject(primary));

    app::discardBenchProjectAutosave(file.path());
    CHECK_FALSE(std::filesystem::exists(
        app::benchProjectAutosavePath(file.path())));
}

TEST_CASE("recovery rejects corrupt primary and autosave without deleting evidence") {
    const TemporaryBenchFile file;
    writeInvalidJson(file.path());
    const auto autosave = app::benchProjectAutosavePath(file.path());
    writeInvalidJson(autosave);

    try {
        static_cast<void>(app::loadBenchProjectWithRecovery(file.path()));
        FAIL("corrupt primary and autosave should not be recoverable");
    } catch (const std::runtime_error& error) {
        const std::string message = error.what();
        CHECK(message.find("autosave is invalid") != std::string::npos);
        CHECK(message.find("primary is invalid") != std::string::npos);
    }
    CHECK(std::filesystem::exists(file.path()));
    CHECK(std::filesystem::exists(autosave));
}
