#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "app/ChimeraExposurePlan.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace chimera = holobench::app::chimera;
namespace holography = holobench::optics::holography;

namespace {

struct CanonicalAutomation final {
    chimera::ChimeraRecipe recipe = chimera::makeCanonicalChimeraRecipe();
    chimera::HogelDataset dataset = chimera::generateHogelDataset(
        recipe, chimera::makeCanonicalPerspectiveViews(recipe));
    holobench::app::BenchProject bench
        = chimera::compileChimeraRecipe(recipe).project;
    chimera::ExposurePlan plan
        = chimera::generateExposurePlan(recipe, dataset, bench);
};

} // namespace

TEST_CASE("canonical CHIMERA exposure plan has deterministic RGB event order") {
    const CanonicalAutomation automation;
    const auto& plan = automation.plan;

    CHECK(plan.events.size() == 624U);
    CHECK(plan.totalDurationSeconds == doctest::Approx(5.76));
    CHECK(plan.sourceDatasetHash == automation.dataset.contentHash);
    CHECK(plan.events[0].kind == chimera::ExposureEventKind::StageMove);
    CHECK(plan.events[0].hogelX == 0U);
    CHECK(plan.events[0].hogelY == 0U);
    CHECK(plan.events[0].stageXMetres == doctest::Approx(-3.5e-3));
    CHECK(plan.events[0].stageYMetres == doctest::Approx(2.5e-3));

    const auto exposureCount = std::count_if(
        plan.events.begin(), plan.events.end(), [](const auto& event) {
            return event.kind == chimera::ExposureEventKind::Exposure;
        });
    CHECK(exposureCount == 144U);
    CHECK(plan.events[1].kind == chimera::ExposureEventKind::SlmLoad);
    CHECK(plan.events[2].kind == chimera::ExposureEventKind::BeamGate);
    CHECK(plan.events[2].objectBeamEnabled);
    CHECK(plan.events[3].kind == chimera::ExposureEventKind::Exposure);
    CHECK(plan.events[3].channelId == "red");
    CHECK(plan.events[3].durationSeconds == doctest::Approx(0.04));
    CHECK(plan.events[4].kind == chimera::ExposureEventKind::BeamGate);
    CHECK_FALSE(plan.events[4].objectBeamEnabled);
    CHECK(plan.events[7].channelId == "green");
    CHECK(plan.events[11].channelId == "blue");
}

TEST_CASE("CHIMERA exposure plan JSON is byte stable and hash guarded") {
    const CanonicalAutomation automation;
    const std::string encoded
        = chimera::serializeExposurePlan(automation.plan);
    const auto restored = chimera::parseExposurePlan(encoded);
    CHECK(restored == automation.plan);
    CHECK(chimera::serializeExposurePlan(restored) == encoded);

    auto corrupt = nlohmann::json::parse(encoded);
    corrupt["payload"]["events"][3]["duration_s"] = 0.05;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseExposurePlan(corrupt.dump())),
        std::runtime_error);

    auto unknown = nlohmann::json::parse(encoded);
    unknown["payload"]["direct_hardware_control"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseExposurePlan(unknown.dump())),
        std::runtime_error);
}

TEST_CASE("single hogel exposure invokes three independent M8 volume recordings") {
    const CanonicalAutomation automation;
    holobench::compute::fft::CpuFftBackend fft;
    const auto executed = chimera::executeHogelExposure(
        automation.recipe,
        automation.dataset,
        automation.plan,
        automation.bench,
        fft,
        2U,
        3U);

    REQUIRE(executed.channels.size() == 3U);
    std::set<std::string> channels;
    std::set<double> wavelengths;
    for (const auto& channel : executed.channels) {
        CHECK(channel.hogelX == 2U);
        CHECK(channel.hogelY == 3U);
        CHECK(channel.m8VolumeRecordingInvoked);
        CHECK(channel.sparseSlmRasterTransferredToPlacedWavePath);
        CHECK(channel.sampleWidth == 256U);
        CHECK(channel.sampleHeight == 256U);
        CHECK(channel.usedBoundedPreviewSampling);
        CHECK(channel.objectFieldDiagnostics.appliedLocalWavePath);
        CHECK(channel.objectFieldDiagnostics.integratedPowerWatts > 0.0);
        CHECK(channel.objectFieldDiagnostics.sampledCentreXMetres
            == doctest::Approx(channel.stageXMetres));
        CHECK(channel.objectFieldDiagnostics.sampledCentreYMetres
            == doctest::Approx(channel.stageYMetres));
        CHECK(channel.recording.pair.geometry
            == holography::PlateRecordingGeometry::Reflection);
        CHECK(channel.recording.nominalReplay.kogelnik.diffractionEfficiency
            > 0.0);
        channels.insert(channel.channelId);
        wavelengths.insert(channel.recording.pair.wavelengthMetres);
    }
    CHECK(channels == std::set<std::string> {"blue", "green", "red"});
    CHECK(wavelengths == std::set<double> {450e-9, 532e-9, 638e-9});
}

TEST_CASE("exposure planning rejects mismatched and unsupported inputs") {
    CanonicalAutomation automation;
    holobench::compute::fft::CpuFftBackend fft;

    SUBCASE("dataset hash does not match") {
        automation.dataset.contentHash = "0000000000000000";
        CHECK_THROWS_AS(
            static_cast<void>(chimera::generateExposurePlan(
                automation.recipe, automation.dataset, automation.bench)),
            std::invalid_argument);
    }
    SUBCASE("plan hash does not match") {
        automation.plan.contentHash = "0000000000000000";
        CHECK_THROWS_AS(
            static_cast<void>(chimera::executeHogelExposure(
                automation.recipe,
                automation.dataset,
                automation.plan,
                automation.bench,
                fft,
                0U,
                0U)),
            std::invalid_argument);
    }
    SUBCASE("hogel lies outside the requested grid") {
        CHECK_THROWS_AS(
            static_cast<void>(chimera::executeHogelExposure(
                automation.recipe,
                automation.dataset,
                automation.plan,
                automation.bench,
                fft,
                automation.recipe.hogels.countX,
                0U)),
            std::invalid_argument);
    }
}
