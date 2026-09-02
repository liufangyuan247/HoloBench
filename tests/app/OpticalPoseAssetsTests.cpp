#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <filesystem>
#include <fstream>
#include <limits>
#include <stdexcept>
#include <string>

#include "app/OpticalPoseAssets.hpp"
#include "app/BenchWaveObservation.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace math = holobench::math;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

namespace {

app::OpticalPoseCalibration translatedPose(double zMetres = 0.02) {
    return {
        .nominalToMeasuredOptical = {
            .translationMetres = {0.0, 0.0, zMetres},
        },
    };
}

scene::CalibrationValidityDomain visibleValidity() {
    return {
        .minimumVacuumWavelengthMetres = 450e-9,
        .maximumVacuumWavelengthMetres = 638e-9,
        .minimumTemperatureKelvin = 285.0,
        .maximumTemperatureKelvin = 305.0,
    };
}

bool hasInteraction(
    const scene::BenchTraceGraph& trace,
    std::string_view componentId) {
    return std::any_of(
        trace.interactions.begin(), trace.interactions.end(),
        [&](const auto& interaction) {
            return interaction.componentId == componentId;
        });
}

} // namespace

TEST_CASE("hashed optical pose restores and moves only the solver scene") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_optical_pose_asset_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "pose.json";
    const auto projectPath = directory / "bench.json";
    app::saveOpticalPoseCalibrationJson(assetPath, translatedPose());

    auto asset = app::loadOpticalPoseAsset(assetPath);
    asset.provenance.source = "pose.json";
    CHECK(asset.calibrationId
        == "optical-pose-sha256-" + asset.provenance.contentSha256);

    auto screen = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "measured-screen");
    screen.transform.translationMetres = {0.0, 0.0, 0.10};
    const auto assembly = scene::makeDefaultMechanicalAssembly(screen);
    scene::applyMechanicalAssembly(screen, assembly);
    const auto nominalFrame = screen.transform;
    app::bindOpticalPoseAsset(screen, asset, visibleValidity());

    scene::BenchScene nominalScene;
    auto laser = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "pose-laser");
    nominalScene.add(laser);
    nominalScene.add(screen);
    const auto nominalTrace = ray::traceDynamicBench(nominalScene);
    REQUIRE(hasInteraction(nominalTrace, screen.id));

    app::OpticalPoseCatalog catalog;
    app::restoreOpticalPoseAssets(nominalScene, projectPath, catalog);
    const std::array wavelengths {532e-9};
    const auto calibrated = app::makeCalibratedOpticalScene(
        nominalScene, catalog, wavelengths, 293.15);
    REQUIRE(calibrated.appliedPoses.size() == 1U);
    CHECK(calibrated.appliedPoses.front().componentId == screen.id);
    CHECK(calibrated.appliedPoses.front().calibrationId
        == asset.calibrationId);
    const auto* calibratedScreen = calibrated.scene.find(screen.id);
    REQUIRE(calibratedScreen != nullptr);
    CHECK(calibratedScreen->transform.translationMetres.z
        == doctest::Approx(nominalFrame.translationMetres.z + 0.02));
    CHECK_FALSE(calibratedScreen->mechanicalAssembly.has_value());

    const auto* retainedNominalScreen = nominalScene.find(screen.id);
    REQUIRE(retainedNominalScreen != nullptr);
    CHECK(retainedNominalScreen->transform == nominalFrame);
    CHECK(retainedNominalScreen->mechanicalAssembly.has_value());
    const auto calibratedTrace = ray::traceDynamicBench(calibrated.scene);
    REQUIRE(hasInteraction(calibratedTrace, screen.id));
    CHECK(calibratedTrace.segments.back().endMetres.z
        == doctest::Approx(0.12));
    holobench::compute::fft::CpuFftBackend fft;
    const auto nominalWave = app::observeBenchWavePattern(
        nominalScene, nominalTrace, screen.id, 64U, false, fft);
    const auto calibratedWave = app::observeBenchWavePattern(
        calibrated.scene,
        calibratedTrace,
        screen.id,
        64U,
        false,
        fft);
    REQUIRE(nominalWave.contributions.size() == 1U);
    REQUIRE(calibratedWave.contributions.size() == 1U);
    CHECK(calibratedWave.contributions.front().accumulatedOpticalPathMetres
        == doctest::Approx(
            nominalWave.contributions.front().accumulatedOpticalPathMetres
            + 0.02));
    constexpr std::size_t centreSample = 32U * 64U + 32U;
    CHECK(calibratedWave.fieldAtObservation.samples()[centreSample]
        != nominalWave.fieldAtObservation.samples()[centreSample]);

    app::saveOpticalPoseCalibrationJson(
        assetPath, translatedPose(0.03));
    app::OpticalPoseCatalog tampered;
    CHECK_THROWS_WITH_AS(
        app::restoreOpticalPoseAssets(
            nominalScene, projectPath, tampered),
        doctest::Contains("SHA-256 does not match"),
        std::invalid_argument);
    CHECK(tampered.resolve(asset.calibrationId) == nullptr);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}

TEST_CASE("optical pose schema binding and validity fail closed") {
    const auto directory = std::filesystem::temp_directory_path()
        / "holobench_optical_pose_semantic_test";
    std::error_code ignored;
    std::filesystem::remove_all(directory, ignored);
    std::filesystem::create_directories(directory);
    const auto assetPath = directory / "pose.json";
    app::saveOpticalPoseCalibrationJson(assetPath, translatedPose());
    const auto asset = app::loadOpticalPoseAsset(assetPath);

    const auto text = app::serializeOpticalPoseCalibrationJson(
        asset.calibration);
    CHECK(app::serializeOpticalPoseCalibrationJson(
            app::deserializeOpticalPoseCalibrationJson(text))
        == text);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::deserializeOpticalPoseCalibrationJson(
            R"({"format_version":1,"model":"rigid_optical_frame_offset","nominal_to_measured_optical":{"translation_m":[0,0,0],"local_x_axis":[1,0,0],"local_y_axis":[0,1,0],"local_z_axis":[0,0,1]},"unknown":1})")),
        doctest::Contains("missing or unknown keys"),
        std::invalid_argument);

    auto excessiveTranslation = translatedPose(0.101);
    CHECK_THROWS_WITH_AS(
        app::validateOpticalPoseCalibration(excessiveTranslation),
        doctest::Contains("100 mm"),
        std::invalid_argument);
    app::OpticalPoseCalibration excessiveRotation {
        .nominalToMeasuredOptical = {
            .localXAxisInWorld = {0.0, 1.0, 0.0},
            .localYAxisInWorld = {-1.0, 0.0, 0.0},
            .localZAxisInWorld = {0.0, 0.0, 1.0},
        },
    };
    CHECK_THROWS_WITH_AS(
        app::validateOpticalPoseCalibration(excessiveRotation),
        doctest::Contains("30 degree"),
        std::invalid_argument);

    auto probe = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::FieldProbe, "virtual-probe");
    CHECK_THROWS_WITH_AS(
        app::bindOpticalPoseAsset(probe, asset, visibleValidity()),
        doctest::Contains("virtual Field Probe"),
        std::invalid_argument);

    auto mirror = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror, "pose-mirror");
    app::bindOpticalPoseAsset(mirror, asset, visibleValidity());
    scene::BenchScene bench;
    bench.add(mirror);
    app::OpticalPoseCatalog catalog;
    catalog.registerCalibration(asset);
    const std::array outsideWavelengths {405e-9};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::makeCalibratedOpticalScene(
            bench, catalog, outsideWavelengths, 293.15)),
        doctest::Contains("outside its wavelength or temperature"),
        std::invalid_argument);
    const std::array insideWavelengths {532e-9};
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::makeCalibratedOpticalScene(
            bench, catalog, insideWavelengths, 310.0)),
        doctest::Contains("outside its wavelength or temperature"),
        std::invalid_argument);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::makeCalibratedOpticalScene(
            bench,
            catalog,
            insideWavelengths,
            std::numeric_limits<double>::quiet_NaN())),
        doctest::Contains("temperature must be finite and positive"),
        std::invalid_argument);

    auto drifted = bench;
    auto edited = *drifted.find(mirror.id);
    ++edited.instrument.specificationVersion;
    drifted.replace(edited.id, edited);
    CHECK_THROWS_WITH_AS(
        app::validateOpticalPoseAssetBindings(drifted, catalog),
        doctest::Contains("provenance does not match"),
        std::invalid_argument);

    const auto oversizedPath = directory / "oversized.json";
    {
        std::ofstream output(oversizedPath, std::ios::binary);
        output.seekp(static_cast<std::streamoff>(
            app::kMaximumOpticalPoseCalibrationJsonBytes));
        output.put('x');
    }
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::loadOpticalPoseAsset(oversizedPath)),
        doctest::Contains("exceeds its byte limit"),
        std::invalid_argument);

    std::filesystem::remove_all(directory, ignored);
    CHECK_FALSE(ignored);
}
