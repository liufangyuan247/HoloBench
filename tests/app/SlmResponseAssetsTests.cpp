#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/BenchWaveObservation.hpp"
#include "app/SlmResponseAssets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace scene = holobench::optics::scene;
namespace slm = holobench::optics::slm;
namespace ray = holobench::optics::ray;

namespace {

slm::CalibratedSlmResponse makeMeasuredResponse(double fullAmplitude = 0.5) {
    return slm::CalibratedSlmResponse({
        {450e-9, {{0.0, 0.9, 0.0}, {1.0, fullAmplitude, 2.0}}},
        {532e-9, {{0.0, 0.8, 0.1}, {1.0, fullAmplitude, 3.0}}},
        {638e-9, {{0.0, 0.7, 0.2}, {1.0, fullAmplitude, 4.0}}},
    });
}

scene::CalibrationValidityDomain measuredValidity() {
    return {
        .minimumVacuumWavelengthMetres = 450e-9,
        .maximumVacuumWavelengthMetres = 638e-9,
        .minimumTemperatureKelvin = 285.0,
        .maximumTemperatureKelvin = 305.0,
    };
}

} // namespace

TEST_CASE("hashed placed SLM response binds restores and detects tampering") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_slm_response_asset_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "slm.json";
    const auto projectPath = directory / "bench.json";
    slm::saveSlmResponseJson(assetPath, makeMeasuredResponse());

    auto asset = app::loadSlmResponseAsset(assetPath);
    asset.provenance.source = "slm.json";
    CHECK(asset.calibrationId
        == "slm-response-sha256-" + asset.provenance.contentSha256);
    CHECK(asset.provenance.contentSha256.size() == 64U);

    auto device = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator,
        "measured-slm");
    app::bindSlmResponseAsset(device, asset, measuredValidity());
    REQUIRE(device.instrument.calibrationAssets.size() == 1U);
    CHECK(device.instrument.calibrationAssets.front().kind
        == scene::CalibrationAssetKind::SlmResponse);

    scene::BenchScene bench;
    bench.add(device);
    app::SlmResponseCatalog catalog;
    app::restoreSlmResponseAssets(bench, projectPath, catalog);
    REQUIRE(catalog.resolveSlmResponse(asset.calibrationId) != nullptr);
    CHECK_NOTHROW(app::validateSlmResponseAssetBindings(bench, catalog));
    const auto evaluated = catalog.resolveSlmResponse(asset.calibrationId)
        ->evaluate(532e-9, 1.0);
    CHECK(evaluated.amplitudeTransmission == doctest::Approx(0.5));
    CHECK(evaluated.unwrappedPhaseDelayRadians == doctest::Approx(3.0));

    slm::saveSlmResponseJson(assetPath, makeMeasuredResponse(0.25));
    app::SlmResponseCatalog tampered;
    CHECK_THROWS_WITH_AS(
        app::restoreSlmResponseAssets(bench, projectPath, tampered),
        doctest::Contains("SHA-256 does not match"),
        std::invalid_argument);
    CHECK(tampered.resolveSlmResponse(asset.calibrationId) == nullptr);

    const auto changedAsset = app::loadSlmResponseAsset(assetPath);
    auto wrongAddressBench = bench;
    auto wrongAddressDevice = *wrongAddressBench.find(device.id);
    wrongAddressDevice.instrument.calibrationAssets.front().contentSha256
        = changedAsset.provenance.contentSha256;
    wrongAddressBench.replace(device.id, wrongAddressDevice);
    app::SlmResponseCatalog wrongAddressCatalog;
    CHECK_THROWS_WITH_AS(
        app::restoreSlmResponseAssets(
            wrongAddressBench, projectPath, wrongAddressCatalog),
        doctest::Contains("content address does not match"),
        std::invalid_argument);
    CHECK(wrongAddressCatalog.resolveSlmResponse(
        changedAsset.calibrationId) == nullptr);

    slm::saveSlmResponseJson(assetPath, makeMeasuredResponse());
    auto wrongFormatBench = bench;
    auto wrongFormatDevice = *wrongFormatBench.find(device.id);
    ++wrongFormatDevice.instrument.calibrationAssets.front().formatVersion;
    wrongFormatBench.replace(device.id, wrongFormatDevice);
    app::SlmResponseCatalog wrongFormatCatalog;
    CHECK_THROWS_WITH_AS(
        app::restoreSlmResponseAssets(
            wrongFormatBench, projectPath, wrongFormatCatalog),
        doctest::Contains("format version does not match"),
        std::invalid_argument);

    auto drifted = bench;
    auto edited = *drifted.find(device.id);
    ++edited.instrument.specificationVersion;
    drifted.replace(edited.id, edited);
    CHECK_THROWS_WITH_AS(
        app::validateSlmResponseAssetBindings(drifted, catalog),
        doctest::Contains("provenance does not match"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("placed SLM response rejects wrong components domains and oversized assets") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_slm_response_semantic_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "slm.json";
    slm::saveSlmResponseJson(assetPath, makeMeasuredResponse());
    auto asset = app::loadSlmResponseAsset(assetPath);

    app::SlmResponseCatalog catalog;
    catalog.registerResponse(asset);
    CHECK_NOTHROW(catalog.registerResponse(asset));
    auto differentProvenance = asset;
    differentProvenance.provenance.source = "other.json";
    CHECK_THROWS_WITH_AS(
        catalog.registerResponse(std::move(differentProvenance)),
        doctest::Contains("different immutable truth or provenance"),
        std::invalid_argument);

    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "wrong-component");
    CHECK_THROWS_WITH_AS(
        app::bindSlmResponseAsset(aperture, asset, measuredValidity()),
        doctest::Contains("only bind placed SLM"),
        std::invalid_argument);

    auto device = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator, "bounded-slm");
    const auto original = device;
    auto invalidValidity = measuredValidity();
    invalidValidity.minimumVacuumWavelengthMetres = 400e-9;
    CHECK_THROWS_WITH_AS(
        app::bindSlmResponseAsset(device, asset, invalidValidity),
        doctest::Contains("exceeds its sampled wavelength domain"),
        std::invalid_argument);
    CHECK(device == original);

    const auto oversizedPath = directory / "oversized.json";
    {
        std::ofstream output(oversizedPath, std::ios::binary);
        output.seekp(static_cast<std::streamoff>(
            slm::kMaximumSlmResponseJsonBytes));
        output.put('x');
    }
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::loadSlmResponseAsset(oversizedPath)),
        doctest::Contains("exceeds its byte limit"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("verified placed SLM LUT changes ordinary Screen complex field") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_slm_response_wave_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "slm.json";
    slm::saveSlmResponseJson(assetPath, makeMeasuredResponse());
    auto asset = app::loadSlmResponseAsset(assetPath);

    scene::BenchScene nominalBench;
    auto laser = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "slm-wave-laser");
    auto laserParameters = std::get<scene::LaserSourceParameters>(
        laser.parameters);
    laserParameters.beamRadiusMetres = 0.002;
    laser.parameters = laserParameters;
    nominalBench.add(laser);

    auto device = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator,
        "slm-wave-device");
    device.transform.translationMetres = {0.0, 0.0, 0.05};
    auto deviceParameters
        = std::get<scene::SpatialLightModulatorParameters>(device.parameters);
    deviceParameters.widthMetres = 0.008;
    deviceParameters.heightMetres = 0.008;
    deviceParameters.pixelWidth = 8U;
    deviceParameters.pixelHeight = 8U;
    deviceParameters.fillFactor = 1.0;
    deviceParameters.modulationMode = scene::SlmModulationMode::Amplitude;
    deviceParameters.commandPattern = scene::SlmCommandPattern::Uniform;
    deviceParameters.primaryCommand = 1.0;
    device.parameters = deviceParameters;
    nominalBench.add(device);

    auto screen = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "slm-wave-screen");
    screen.transform.translationMetres = {0.0, 0.0, 0.10};
    screen.parameters = scene::ScreenDetectorParameters {
        .widthMetres = 0.008,
        .heightMetres = 0.008,
        .sampleWidth = 64U,
        .sampleHeight = 64U,
    };
    nominalBench.add(screen);

    holobench::compute::fft::CpuFftBackend fft;
    const auto nominalTrace = ray::traceDynamicBench(nominalBench);
    const auto nominal = app::observeBenchWavePattern(
        nominalBench,
        nominalTrace,
        screen.id,
        64U,
        false,
        fft);

    auto calibratedBench = nominalBench;
    auto calibratedDevice = *calibratedBench.find(device.id);
    app::bindSlmResponseAsset(
        calibratedDevice, asset, measuredValidity());
    calibratedBench.replace(device.id, calibratedDevice);
    app::SlmResponseCatalog catalog;
    catalog.registerResponse(asset);
    const auto calibratedTrace = ray::traceDynamicBench(calibratedBench);

    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            calibratedBench,
            calibratedTrace,
            screen.id,
            64U,
            false,
            fft)),
        doctest::Contains("no verified calibrated resolver"),
        std::invalid_argument);
    const auto calibrated = app::observeBenchWavePattern(
        calibratedBench,
        calibratedTrace,
        screen.id,
        64U,
        false,
        fft,
        nullptr,
        &catalog,
        293.15);
    REQUIRE(calibrated.contributions.size() == 1U);
    CHECK(calibrated.contributions.front().pathSampling
        .appliedSlmCalibrationIds
        == std::vector<std::string> {asset.calibrationId});
    CHECK(calibrated.integratedPowerWatts
        == doctest::Approx(0.25 * nominal.integratedPowerWatts)
            .epsilon(1e-10));
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            calibratedBench,
            calibratedTrace,
            screen.id,
            64U,
            false,
            fft,
            nullptr,
            &catalog,
            310.0)),
        doctest::Contains("outside its wavelength or temperature"),
        std::invalid_argument);

    const auto terminal = std::find_if(
        calibratedTrace.interactions.begin(),
        calibratedTrace.interactions.end(),
        [&](const auto& interaction) {
            return interaction.componentId == screen.id;
        });
    REQUIRE(terminal != calibratedTrace.interactions.end());
    const auto path = scene::collectBenchPathInteractions(
        calibratedTrace, *terminal);
    const holobench::optics::wave::PlacedSlmSparseCommand transient {
        .componentId = device.id,
        .commandId = deviceParameters.commandId,
        .pixelWidth = deviceParameters.pixelWidth,
        .pixelHeight = deviceParameters.pixelHeight,
        .defaultNormalizedCommand = 1.0,
        .calibrationId = "transient-response",
        .calibratedResponse = &asset.response,
        .pixels = {},
    };
    const std::array transientCommands {transient};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(
            holobench::optics::wave::sampleBeamFollowingField(
                calibratedBench,
                terminal->incidentBeam,
                path,
                {
                    .sampleWidth = 64U,
                    .sampleHeight = 64U,
                    .extentWidthMetres = 0.008,
                    .extentHeightMetres = 0.008,
                    .slmResponses = &catalog,
                    .environmentTemperatureKelvin = 293.15,
                },
                fft,
                transientCommands)),
        doctest::Contains("calibrations conflict"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}
