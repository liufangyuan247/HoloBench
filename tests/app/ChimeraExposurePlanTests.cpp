#include <doctest/doctest.h>

#include <algorithm>
#include <set>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include <nlohmann/json.hpp>

#include "app/ChimeraExposurePlan.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace chimera = holobench::app::chimera;
namespace holography = holobench::optics::holography;
namespace slm = holobench::optics::slm;
namespace scene = holobench::optics::scene;

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

slm::CalibratedSlmResponse makeMeasuredSlmResponse() {
    return slm::CalibratedSlmResponse(std::vector<slm::SlmWavelengthResponse> {{
        .vacuumWavelengthMetres = 400e-9,
        .commandResponse = {{0.0, 0.0, 0.0}, {1.0, 0.5, 0.0}},
    }, {
        .vacuumWavelengthMetres = 700e-9,
        .commandResponse = {{0.0, 0.0, 0.0}, {1.0, 0.5, 0.0}},
    }});
}

class SingleSlmResponseResolver final : public slm::ISlmResponseResolver {
public:
    SingleSlmResponseResolver(
        std::string calibrationId,
        const slm::CalibratedSlmResponse& response)
        : calibrationId_(std::move(calibrationId)), response_(&response) {}

    const slm::CalibratedSlmResponse* resolveSlmResponse(
        std::string_view calibrationId) const noexcept override {
        return calibrationId == calibrationId_ ? response_ : nullptr;
    }

private:
    std::string calibrationId_;
    const slm::CalibratedSlmResponse* response_ = nullptr;
};

holography::CalibratedMaterialDoseResponse makeMeasuredMaterialResponse() {
    return {"measured-photopolymer-lot-9", {{
        .vacuumWavelengthMetres = 400e-9,
        .doseResponse = {
            {0.0, 0.001, 0.001},
            {1e12, 0.02, 0.01},
        },
    }, {
        .vacuumWavelengthMetres = 700e-9,
        .doseResponse = {
            {0.0, 0.001, 0.001},
            {1e12, 0.02, 0.01},
        },
    }}};
}

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
        CHECK(channel.referenceFieldDiagnostics.integratedPowerWatts > 0.0);
        CHECK(channel.objectFieldDiagnostics.sampledCentreXMetres
            == doctest::Approx(channel.stageXMetres));
        CHECK(channel.objectFieldDiagnostics.sampledCentreYMetres
            == doctest::Approx(channel.stageYMetres));
        CHECK(channel.recording.pair.geometry
            == holography::PlateRecordingGeometry::Reflection);
        REQUIRE(channel.recording.objectIncident.has_value());
        REQUIRE(channel.recording.referenceIncident.has_value());
        CHECK(channel.recording.objectIncident->diagnostics
            .appliedSlmCommandIds
            == channel.objectFieldDiagnostics.appliedSlmCommandIds);
        CHECK(channel.recording.referenceIncident->diagnostics
            .integratedPowerWatts
            == channel.referenceFieldDiagnostics.integratedPowerWatts);
        CHECK(channel.recording.nominalReplay.kogelnik.diffractionEfficiency
            > 0.0);
        channels.insert(channel.channelId);
        wavelengths.insert(channel.recording.pair.wavelengthMetres);
    }
    CHECK(channels == std::set<std::string> {"blue", "green", "red"});
    CHECK(wavelengths == std::set<double> {450e-9, 532e-9, 638e-9});
}

TEST_CASE("calibrated SLM and material LUTs drive physical hogel exposure evidence") {
    const CanonicalAutomation automation;
    const auto slmResponse = makeMeasuredSlmResponse();
    const auto materialResponse = makeMeasuredMaterialResponse();
    holobench::compute::fft::CpuFftBackend fft;
    const chimera::HogelExposureExecutionOptions options {
        .maximumPreviewSampleWidth = 256U,
        .maximumPreviewSampleHeight = 256U,
        .slmCalibrationId = "measured-slm-unit-4",
        .calibratedSlmResponse = &slmResponse,
        .calibratedMaterialDoseResponse = &materialResponse,
    };
    const auto executed = chimera::executeHogelExposure(
        automation.recipe,
        automation.dataset,
        automation.plan,
        automation.bench,
        fft,
        2U,
        3U,
        options);

    REQUIRE(executed.channels.size() == 3U);
    for (const auto& channel : executed.channels) {
        CHECK(channel.calibratedSlmResponseApplied);
        CHECK(channel.slmCalibrationId == "measured-slm-unit-4");
        CHECK(channel.calibratedMaterialDoseResponseApplied);
        CHECK(channel.materialCalibrationId
            == "measured-photopolymer-lot-9");
        CHECK(channel.objectMeanIrradianceWattsPerSquareMetre > 0.0);
        CHECK(channel.referenceMeanIrradianceWattsPerSquareMetre > 0.0);
        CHECK(channel.fringeVisibility > 0.0);
        CHECK(channel.fringeVisibility <= 1.0);
        CHECK(channel.totalDoseJoulesPerSquareMetre
            > channel.fringeModulationDoseJoulesPerSquareMetre);
        CHECK(channel.fringeModulationDoseJoulesPerSquareMetre > 0.0);
        CHECK(channel.referenceFieldDiagnostics.integratedPowerWatts > 0.0);
        CHECK(channel.objectFieldDiagnostics.appliedSlmCalibrationIds
            == std::vector<std::string> {"measured-slm-unit-4"});
        const auto expected = materialResponse.evaluate(
            channel.recording.pair.wavelengthMetres,
            channel.fringeModulationDoseJoulesPerSquareMetre);
        CHECK(channel.recording.material.refractiveIndexModulation
            == doctest::Approx(expected.refractiveIndexModulation));
        CHECK(channel.recording.material.isotropicLinearShrinkageFraction
            == doctest::Approx(
                expected.isotropicLinearShrinkageFraction));
    }
}

TEST_CASE("CHIMERA sparse raster uses the verified response bound to its placed SLM") {
    CanonicalAutomation automation;
    const auto response = makeMeasuredSlmResponse();
    const std::string calibrationId = "placed-slm-response";
    const SingleSlmResponseResolver resolver(calibrationId, response);
    for (const std::string componentId : {
             "chimera-slm-red",
             "chimera-slm-green",
             "chimera-slm-blue"}) {
        const auto* placed = automation.bench.scene.find(componentId);
        REQUIRE(placed != nullptr);
        auto device = *placed;
        device.instrument.calibrationMode
            = scene::InstrumentCalibrationMode::Calibrated;
        device.instrument.calibrationAssets.push_back({
            .kind = scene::CalibrationAssetKind::SlmResponse,
            .calibrationId = calibrationId,
            .formatVersion = 1,
            .source = "placed-slm.json",
            .contentSha256 = std::string(64U, 'a'),
            .specificationId = device.instrument.specificationId,
            .specificationVersion = device.instrument.specificationVersion,
            .validity = {
                .minimumVacuumWavelengthMetres = 400e-9,
                .maximumVacuumWavelengthMetres = 700e-9,
                .minimumTemperatureKelvin = 285.0,
                .maximumTemperatureKelvin = 305.0,
            },
        });
        automation.bench.scene.replace(device.id, device);
    }

    holobench::compute::fft::CpuFftBackend fft;
    chimera::HogelExposureExecutionOptions options;
    options.slmResponses = &resolver;
    options.environmentTemperatureKelvin = 293.15;
    const auto executed = chimera::executeHogelExposure(
        automation.recipe,
        automation.dataset,
        automation.plan,
        automation.bench,
        fft,
        2U,
        3U,
        options);
    REQUIRE(executed.channels.size() == 3U);
    for (const auto& channel : executed.channels) {
        CHECK(channel.calibratedSlmResponseApplied);
        CHECK(channel.slmCalibrationId == calibrationId);
        CHECK(channel.objectFieldDiagnostics.appliedSlmCalibrationIds
            == std::vector<std::string> {calibrationId});
    }
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
    SUBCASE("SLM calibration identity and response must be paired") {
        CHECK_THROWS_AS(
            static_cast<void>(chimera::executeHogelExposure(
                automation.recipe,
                automation.dataset,
                automation.plan,
                automation.bench,
                fft,
                0U,
                0U,
                {.maximumPreviewSampleWidth = 64U,
                    .maximumPreviewSampleHeight = 64U,
                    .slmCalibrationId = "missing-response",
                    .calibratedSlmResponse = nullptr,
                    .calibratedMaterialDoseResponse = nullptr})),
            std::invalid_argument);
    }
}
