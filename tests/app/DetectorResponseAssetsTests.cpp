#include <doctest/doctest.h>

#include <array>
#include <filesystem>
#include <stdexcept>
#include <string>
#include <utility>

#include "app/ChimeraCameraImage.hpp"
#include "app/DetectorResponseAssets.hpp"

namespace app = holobench::app;
namespace chimera = holobench::app::chimera;
namespace scene = holobench::optics::scene;
namespace sensor = holobench::optics::sensor;

namespace {

sensor::CalibratedCameraSpectralResponse makeMeasuredResponse(
    std::string id = "measured_detector_rgb") {
    return {std::move(id), {
        {450e-9, {0.02, 0.08, 0.90}},
        {532e-9, {0.04, 0.82, 0.06}},
        {638e-9, {0.88, 0.07, 0.01}},
    }};
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

TEST_CASE("hashed detector response binds restores and drives placed selection") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_detector_response_asset_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "detector.json";
    const auto projectPath = directory / "bench.json";
    sensor::saveCameraSpectralResponseJson(
        assetPath, makeMeasuredResponse());

    auto asset = app::loadDetectorResponseAsset(assetPath);
    asset.provenance.source = "detector.json";
    CHECK(asset.response.calibrationId() == "measured_detector_rgb");
    CHECK(asset.provenance.contentSha256.size() == 64U);

    auto detector = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector,
        "measured-detector");
    app::bindDetectorResponseAsset(
        detector, asset, measuredValidity());
    CHECK(detector.instrument.calibrationMode
        == scene::InstrumentCalibrationMode::Calibrated);
    REQUIRE(detector.instrument.calibrationAssets.size() == 1U);
    CHECK(detector.instrument.calibrationAssets.front().kind
        == scene::CalibrationAssetKind::DetectorResponse);

    scene::BenchScene bench;
    bench.add(detector);
    app::DetectorResponseCatalog catalog;
    app::restoreDetectorResponseAssets(bench, projectPath, catalog);
    REQUIRE(catalog.resolve("measured_detector_rgb") != nullptr);
    REQUIRE(catalog.provenance("measured_detector_rgb") != nullptr);
    CHECK_NOTHROW(app::validateDetectorResponseAssetBindings(
        bench, catalog));

    const sensor::CalibratedCameraSpectralResponse nominal {
        "nominal_preview", {
            {450e-9, {0.1, 0.1, 1.0}},
            {532e-9, {0.1, 1.0, 0.1}},
            {638e-9, {1.0, 0.1, 0.1}},
        }};
    const std::array wavelengths {450e-9, 532e-9, 638e-9};
    const auto selected = app::selectPlacedDetectorResponse(
        bench,
        detector.id,
        catalog,
        nominal,
        wavelengths,
        293.15);
    REQUIRE(selected.response != nullptr);
    CHECK(selected.usedCalibratedAsset);
    CHECK(selected.calibrationId == "measured_detector_rgb");
    CHECK(selected.contentSha256 == asset.provenance.contentSha256);
    CHECK(selected.response->evaluate(532e-9)
            .relativeSensorResponse.green
        == doctest::Approx(0.82));

    chimera::CameraImageResult image;
    image.usedPlacedSequentialLens = true;
    image.cameraCalibrationId = selected.calibrationId;
    app::applyPlacedDetectorResponseEvidence(image, selected);
    CHECK(image.usedPlacedDetectorCalibration);
    CHECK(image.detectorResponseContentSha256
        == asset.provenance.contentSha256);
    CHECK(image.detectorResponseTemperatureKelvin
        == doctest::Approx(293.15));

    sensor::saveCameraSpectralResponseJson(
        assetPath, makeMeasuredResponse("tampered_detector"));
    app::DetectorResponseCatalog tampered;
    CHECK_THROWS_WITH_AS(
        app::restoreDetectorResponseAssets(
            bench, projectPath, tampered),
        doctest::Contains("SHA-256 does not match"),
        std::invalid_argument);
    CHECK(tampered.resolve("measured_detector_rgb") == nullptr);

    const auto changedAsset = app::loadDetectorResponseAsset(assetPath);
    auto wrongIdBench = bench;
    auto wrongIdDetector = *wrongIdBench.find(detector.id);
    wrongIdDetector.instrument.calibrationAssets.front().contentSha256
        = changedAsset.provenance.contentSha256;
    wrongIdBench.replace(wrongIdDetector.id, wrongIdDetector);
    app::DetectorResponseCatalog wrongIdCatalog;
    CHECK_THROWS_WITH_AS(
        app::restoreDetectorResponseAssets(
            wrongIdBench, projectPath, wrongIdCatalog),
        doctest::Contains("content ID does not match"),
        std::invalid_argument);
    CHECK(wrongIdCatalog.resolve("tampered_detector") == nullptr);

    sensor::saveCameraSpectralResponseJson(
        assetPath, makeMeasuredResponse());
    auto wrongFormatBench = bench;
    auto wrongFormatDetector = *wrongFormatBench.find(detector.id);
    ++wrongFormatDetector.instrument.calibrationAssets.front().formatVersion;
    wrongFormatBench.replace(
        wrongFormatDetector.id, wrongFormatDetector);
    app::DetectorResponseCatalog wrongFormatCatalog;
    CHECK_THROWS_WITH_AS(
        app::restoreDetectorResponseAssets(
            wrongFormatBench, projectPath, wrongFormatCatalog),
        doctest::Contains("format version does not match"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("detector response assets reject invalid components domains and drift") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_detector_response_semantic_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "detector.json";
    sensor::saveCameraSpectralResponseJson(
        assetPath, makeMeasuredResponse());
    auto asset = app::loadDetectorResponseAsset(assetPath);

    app::DetectorResponseCatalog catalog;
    catalog.registerResponse(asset.response, asset.provenance);
    CHECK_NOTHROW(catalog.registerResponse(
        asset.response, asset.provenance));
    CHECK_THROWS_WITH_AS(
        catalog.registerResponse(
            makeMeasuredResponse("measured_detector_rgb"),
            {.formatVersion = asset.provenance.formatVersion,
             .source = "different.json",
             .contentSha256 = asset.provenance.contentSha256}),
        doctest::Contains("different immutable content or provenance"),
        std::invalid_argument);

    auto probe = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::FieldProbe, "virtual-probe");
    CHECK_THROWS_WITH_AS(
        app::bindDetectorResponseAsset(
            probe, asset, measuredValidity()),
        doctest::Contains("only bind physical"),
        std::invalid_argument);

    auto detector = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "bounded-detector");
    const auto original = detector;
    auto invalidValidity = measuredValidity();
    invalidValidity.minimumVacuumWavelengthMetres = 400e-9;
    CHECK_THROWS_WITH_AS(
        app::bindDetectorResponseAsset(
            detector, asset, invalidValidity),
        doctest::Contains("exceeds its sampled wavelength domain"),
        std::invalid_argument);
    CHECK(detector == original);

    auto narrowValidity = measuredValidity();
    narrowValidity.minimumVacuumWavelengthMetres = 500e-9;
    narrowValidity.maximumVacuumWavelengthMetres = 600e-9;
    app::bindDetectorResponseAsset(
        detector, asset, narrowValidity);
    scene::BenchScene bench;
    bench.add(detector);
    const sensor::CalibratedCameraSpectralResponse nominal {
        "nominal_preview", {
            {450e-9, {0.1, 0.1, 1.0}},
            {532e-9, {0.1, 1.0, 0.1}},
            {638e-9, {1.0, 0.1, 0.1}},
        }};
    const std::array outsideWavelengths {450e-9, 532e-9};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::selectPlacedDetectorResponse(
            bench,
            detector.id,
            catalog,
            nominal,
            outsideWavelengths,
            293.15)),
        doctest::Contains("outside its wavelength or temperature"),
        std::invalid_argument);

    auto drifted = bench;
    auto edited = *drifted.find(detector.id);
    ++edited.instrument.specificationVersion;
    drifted.replace(edited.id, edited);
    CHECK_THROWS_WITH_AS(
        app::validateDetectorResponseAssetBindings(drifted, catalog),
        doctest::Contains("provenance does not match"),
        std::invalid_argument);

    scene::BenchScene virtualBench;
    virtualBench.add(probe);
    const std::array nominalWavelengths {450e-9, 532e-9, 638e-9};
    const auto nominalSelection = app::selectPlacedDetectorResponse(
        virtualBench,
        probe.id,
        catalog,
        nominal,
        nominalWavelengths,
        293.15);
    REQUIRE(nominalSelection.response != nullptr);
    CHECK_FALSE(nominalSelection.usedCalibratedAsset);
    CHECK(nominalSelection.contentSha256.empty());

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}
