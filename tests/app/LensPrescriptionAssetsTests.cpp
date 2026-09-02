#include <doctest/doctest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "app/BenchProject.hpp"
#include "app/LensPrescriptionAssets.hpp"
#include "optics/io/LensPrescriptionIO.hpp"

namespace app = holobench::app;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

TEST_CASE("hashed lens assets restore deterministically and reject tampering") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_lens_asset_test";
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "prescription.json";
    auto prescription = ray::makeDefaultNBk7BiconvexPrescription();
    prescription.id = "asset_biconvex";
    holobench::optics::io::saveLensPrescriptionJson(
        prescription, assetPath);

    const auto loaded = app::loadLensPrescriptionAsset(assetPath);
    CHECK(loaded.prescription == prescription);
    CHECK(loaded.provenance.contentSha256.size() == 64U);

    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "asset-lens");
    auto parameters
        = std::get<scene::RealLensAssemblyParameters>(lens.parameters);
    parameters.prescriptionId = prescription.id;
    lens.parameters = parameters;
    lens.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    lens.instrument.calibrationAssets.push_back(
        app::makeLensPrescriptionAssetReference(
            loaded, lens.instrument));
    scene::BenchScene bench;
    bench.add(lens);

    ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    app::restoreLensPrescriptionAssets(
        bench, directory / "bench.json", catalog);
    REQUIRE(catalog.resolve(prescription.id) != nullptr);
    REQUIRE(catalog.provenance(prescription.id) != nullptr);
    CHECK(catalog.provenance(prescription.id)->contentSha256
        == loaded.provenance.contentSha256);
    CHECK_NOTHROW(app::validateLensPrescriptionAssetBindings(
        bench, catalog));

    {
        std::ofstream output(assetPath, std::ios::binary | std::ios::trunc);
        REQUIRE(output.good());
        output << "{broken";
        REQUIRE(output.good());
    }
    ray::LensPrescriptionCatalog tamperedCatalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    CHECK_THROWS_WITH_AS(
        app::restoreLensPrescriptionAssets(
            bench, directory / "bench.json", tamperedCatalog),
        doctest::Contains("SHA-256 does not match"),
        std::invalid_argument);

    std::error_code cleanupError;
    std::filesystem::remove_all(directory, cleanupError);
    CHECK_FALSE(cleanupError);
}

TEST_CASE("relative lens assets survive Bench persistence and stale bindings fail closed") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_relative_lens_asset_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "prescription.json";
    const auto projectPath = directory / "bench.json";
    auto prescription = ray::makeDefaultNBk7BiconvexPrescription();
    prescription.id = "portable_biconvex";
    holobench::optics::io::saveLensPrescriptionJson(
        prescription, assetPath);

    auto asset = app::loadLensPrescriptionAsset(assetPath);
    asset.provenance.source = "prescription.json";
    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "portable-lens");
    app::bindLensPrescriptionAsset(lens, asset);
    CHECK(std::get<scene::RealLensAssemblyParameters>(lens.parameters)
            .prescriptionId
        == prescription.id);
    CHECK(lens.instrument.calibrationMode
        == scene::InstrumentCalibrationMode::Calibrated);
    REQUIRE(lens.instrument.calibrationAssets.size() == 1U);
    CHECK(lens.instrument.calibrationAssets.front().source
        == "prescription.json");

    app::BenchProject project;
    project.projectId = "portable-real-lens";
    project.name = "Portable Real Lens";
    project.scene.add(lens);
    app::saveBenchProject(project, projectPath);
    const auto restoredProject = app::loadBenchProject(projectPath);
    ray::LensPrescriptionCatalog restoredCatalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    app::restoreLensPrescriptionAssets(
        restoredProject.scene, projectPath, restoredCatalog);
    REQUIRE(restoredCatalog.resolve(prescription.id) != nullptr);
    CHECK(*restoredCatalog.resolve(prescription.id) == prescription);

    auto missingReferenceScene = restoredProject.scene;
    auto missingReference = *missingReferenceScene.find("portable-lens");
    missingReference.instrument.calibrationAssets.clear();
    missingReferenceScene.replace(missingReference.id, missingReference);
    CHECK_THROWS_WITH_AS(
        app::validateLensPrescriptionAssetBindings(
            missingReferenceScene, restoredCatalog),
        doctest::Contains("requires one calibrated asset reference"),
        std::invalid_argument);

    auto mismatchedSpecificationScene = restoredProject.scene;
    auto mismatchedSpecification
        = *mismatchedSpecificationScene.find("portable-lens");
    ++mismatchedSpecification.instrument.specificationVersion;
    mismatchedSpecificationScene.replace(
        mismatchedSpecification.id, mismatchedSpecification);
    CHECK_THROWS_WITH_AS(
        app::validateLensPrescriptionAssetBindings(
            mismatchedSpecificationScene, restoredCatalog),
        doctest::Contains("provenance does not match"),
        std::invalid_argument);

    auto nominalScene = restoredProject.scene;
    auto nominal = *nominalScene.find("portable-lens");
    auto nominalParameters
        = std::get<scene::RealLensAssemblyParameters>(nominal.parameters);
    nominalParameters.prescriptionId
        = ray::makeDefaultNBk7BiconvexPrescription().id;
    nominal.parameters = nominalParameters;
    nominal.instrument.calibrationAssets.clear();
    nominal.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Nominal;
    nominalScene.replace(nominal.id, nominal);
    CHECK_NOTHROW(app::validateLensPrescriptionAssetBindings(
        nominalScene, restoredCatalog));

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("lens asset restoration rejects semantic drift and catalog IDs stay immutable") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_lens_asset_semantic_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "prescription.csv";
    auto prescription = ray::makeDefaultNBk7BiconvexPrescription();
    prescription.id = "immutable_lens";
    holobench::optics::io::saveLensPrescriptionCsv(
        prescription, assetPath);
    auto asset = app::loadLensPrescriptionAsset(assetPath);
    asset.provenance.source = "prescription.csv";

    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "semantic-lens");
    app::bindLensPrescriptionAsset(lens, asset);
    scene::BenchScene bench;
    bench.add(lens);

    ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    CHECK_NOTHROW(catalog.registerPrescription(
        asset.prescription, asset.provenance));
    CHECK_NOTHROW(catalog.registerPrescription(
        asset.prescription, asset.provenance));
    auto changedPrescription = asset.prescription;
    changedPrescription.surfaces.front().geometry.curvaturePerMetre += 1.0;
    CHECK_THROWS_WITH_AS(
        catalog.registerPrescription(
            changedPrescription, asset.provenance),
        doctest::Contains("different immutable content or provenance"),
        std::invalid_argument);
    auto changedProvenance = asset.provenance;
    changedProvenance.source = "renamed.csv";
    CHECK_THROWS_WITH_AS(
        catalog.registerPrescription(
            asset.prescription, changedProvenance),
        doctest::Contains("different immutable content or provenance"),
        std::invalid_argument);

    auto wrongIdScene = bench;
    auto wrongIdLens = *wrongIdScene.find("semantic-lens");
    auto wrongIdParameters
        = std::get<scene::RealLensAssemblyParameters>(
            wrongIdLens.parameters);
    wrongIdParameters.prescriptionId = "different_lens_id";
    wrongIdLens.parameters = wrongIdParameters;
    wrongIdLens.instrument.calibrationAssets.front().calibrationId
        = wrongIdParameters.prescriptionId;
    wrongIdScene.replace(wrongIdLens.id, wrongIdLens);
    ray::LensPrescriptionCatalog wrongIdCatalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    CHECK_THROWS_WITH_AS(
        app::restoreLensPrescriptionAssets(
            wrongIdScene, directory / "bench.json", wrongIdCatalog),
        doctest::Contains("content ID does not match"),
        std::invalid_argument);
    CHECK(wrongIdCatalog.resolve(prescription.id) == nullptr);

    auto wrongFormatScene = bench;
    auto wrongFormatLens = *wrongFormatScene.find("semantic-lens");
    ++wrongFormatLens.instrument.calibrationAssets.front().formatVersion;
    wrongFormatScene.replace(wrongFormatLens.id, wrongFormatLens);
    ray::LensPrescriptionCatalog wrongFormatCatalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    CHECK_THROWS_WITH_AS(
        app::restoreLensPrescriptionAssets(
            wrongFormatScene, directory / "bench.json", wrongFormatCatalog),
        doctest::Contains("format version does not match"),
        std::invalid_argument);

    auto nonLens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror,
        "not-a-lens");
    CHECK_THROWS_WITH_AS(
        app::bindLensPrescriptionAsset(nonLens, asset),
        doctest::Contains("only bind Real Lens Assemblies"),
        std::invalid_argument);

    auto unchangedLens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "unchanged-lens");
    const auto originalLens = unchangedLens;
    auto invalidAsset = asset;
    invalidAsset.provenance.source.clear();
    CHECK_THROWS_AS(
        app::bindLensPrescriptionAsset(unchangedLens, invalidAsset),
        std::invalid_argument);
    CHECK(unchangedLens == originalLens);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}
