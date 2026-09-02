#include <doctest/doctest.h>

#include <filesystem>
#include <stdexcept>
#include <string>

#include "app/CoatingResponseAssets.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace material = holobench::optics::material;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

material::CalibratedCoatingResponse makeCoating(
    std::string id = "measured-splitter-coating") {
    return {
        std::move(id),
        {450e-9, 650e-9},
        {0.0, 1.0},
        std::vector<material::CoatingPowerResponse>(
            4U,
            {.powerReflectivity = 0.30,
                .powerTransmissivity = 0.55}),
    };
}

scene::CalibrationValidityDomain measuredValidity() {
    return {
        .minimumVacuumWavelengthMetres = 450e-9,
        .maximumVacuumWavelengthMetres = 650e-9,
        .minimumTemperatureKelvin = 285.0,
        .maximumTemperatureKelvin = 305.0,
    };
}

} // namespace

TEST_CASE("hashed coating response binds restores and drives placed power") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_coating_response_asset_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "coating.json";
    const auto projectPath = directory / "bench.json";
    material::saveCoatingResponseJson(assetPath, makeCoating());

    auto asset = app::loadCoatingResponseAsset(assetPath);
    asset.provenance.source = "coating.json";
    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner,
        "measured-splitter");
    splitter.transform.translationMetres = {0.0, 0.0, 1.0};
    app::bindCoatingResponseAsset(
        splitter, asset, measuredValidity());
    REQUIRE(splitter.instrument.calibrationAssets.size() == 1U);
    CHECK(splitter.instrument.calibrationAssets.front().kind
        == scene::CalibrationAssetKind::CoatingResponse);

    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource,
        "coating-test-laser"));
    bench.add(splitter);
    app::CoatingResponseCatalog catalog;
    app::restoreCoatingResponseAssets(bench, projectPath, catalog);
    CHECK_NOTHROW(app::validateCoatingResponseAssetBindings(
        bench, catalog));
    REQUIRE(catalog.resolveCoatingResponse(
        "measured-splitter-coating") != nullptr);

    const ray::DynamicBenchCalibrationContext calibration {
        .coatingResponses = &catalog,
        .temperatureKelvin = 293.15,
    };
    const auto graph = ray::traceDynamicBench(
        bench, {}, nullptr, calibration);
    REQUIRE(graph.interactions.size() == 1U);
    REQUIRE(graph.interactions.front().outgoing.size() == 2U);
    CHECK(graph.interactions.front().outgoing[0].beam.powerWatts
        == doctest::Approx(0.30));
    CHECK(graph.interactions.front().outgoing[1].beam.powerWatts
        == doctest::Approx(0.55));

    material::saveCoatingResponseJson(
        assetPath, makeCoating("tampered-coating"));
    app::CoatingResponseCatalog tampered;
    CHECK_THROWS_WITH_AS(
        app::restoreCoatingResponseAssets(bench, projectPath, tampered),
        doctest::Contains("SHA-256 does not match"),
        std::invalid_argument);
    CHECK(tampered.resolveCoatingResponse(
        "measured-splitter-coating") == nullptr);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("coating assets reject unsupported components domains and drift") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_coating_response_semantic_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "coating.json";
    material::saveCoatingResponseJson(assetPath, makeCoating());
    auto asset = app::loadCoatingResponseAsset(assetPath);

    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "unsupported-lens");
    CHECK_THROWS_WITH_AS(
        app::bindCoatingResponseAsset(lens, asset, measuredValidity()),
        doctest::Contains("only bind mirrors or beam splitters"),
        std::invalid_argument);

    auto mirror = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror, "bounded-mirror");
    const auto original = mirror;
    auto invalidValidity = measuredValidity();
    invalidValidity.minimumVacuumWavelengthMetres = 400e-9;
    CHECK_THROWS_WITH_AS(
        app::bindCoatingResponseAsset(
            mirror, asset, invalidValidity),
        doctest::Contains("exceeds its sampled wavelength domain"),
        std::invalid_argument);
    CHECK(mirror == original);

    app::CoatingResponseCatalog catalog;
    catalog.registerResponse(asset);
    CHECK_NOTHROW(catalog.registerResponse(asset));
    auto drift = asset;
    drift.provenance.source = "different.json";
    CHECK_THROWS_WITH_AS(
        catalog.registerResponse(std::move(drift)),
        doctest::Contains("different immutable content or provenance"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}
