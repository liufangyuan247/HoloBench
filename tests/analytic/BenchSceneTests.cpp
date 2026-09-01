#include <doctest/doctest.h>

#include <array>
#include <cmath>
#include <limits>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <variant>

#include "optics/scene/BenchScene.hpp"

namespace scene = holobench::optics::scene;

TEST_CASE("dynamic bench exposes twelve stable typed component kinds") {
    const auto& kinds = scene::requiredBenchComponentKinds();
    REQUIRE(kinds.size() == 12);

    std::set<std::string> stableNames;
    std::set<std::string> displayNames;
    for (std::size_t index = 0; index < kinds.size(); ++index) {
        const auto kind = kinds[index];
        const std::string stableName(scene::benchComponentKindName(kind));
        const std::string displayName(scene::benchComponentDisplayName(kind));
        CHECK(stableName != "unknown");
        CHECK(displayName != "Unknown");
        CHECK(stableNames.insert(stableName).second);
        CHECK(displayNames.insert(displayName).second);
        CHECK(scene::benchComponentKindFromName(stableName) == kind);

        const auto component = scene::makeDefaultBenchComponent(
            kind, "component-" + std::to_string(index));
        CHECK(component.kind == kind);
        CHECK(component.parameters.index() == index);
        CHECK_NOTHROW(scene::validateBenchComponent(component));
    }
}

TEST_CASE("dynamic bench rejects invalid IDs transforms parameter mismatches and energy creation") {
    auto component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.id = "bad id";
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.transform.localXAxisInWorld = {2.0, 0.0, 0.0};
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    component = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1");
    component.parameters = scene::PlanarMirrorParameters {};
    CHECK_THROWS_AS(scene::validateBenchComponent(component), std::invalid_argument);

    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner, "splitter-1");
    auto splitterParameters = std::get<scene::BeamSplitterParameters>(splitter.parameters);
    splitterParameters.powerReflectivity = 0.7;
    splitterParameters.powerTransmissivity = 0.4;
    splitter.parameters = splitterParameters;
    CHECK_THROWS_AS(scene::validateBenchComponent(splitter), std::invalid_argument);

    auto slm = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator, "slm-1");
    auto slmParameters = std::get<scene::SpatialLightModulatorParameters>(slm.parameters);
    slmParameters.fillFactor = std::numeric_limits<double>::quiet_NaN();
    slm.parameters = slmParameters;
    CHECK_THROWS_AS(scene::validateBenchComponent(slm), std::invalid_argument);

    slmParameters = {};
    slmParameters.primaryCommand = 1.1;
    slm.parameters = slmParameters;
    CHECK_THROWS_AS(scene::validateBenchComponent(slm), std::invalid_argument);
}

TEST_CASE("mechanical assembly resolves constrained stage and mount state into optical truth") {
    auto mirror = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror, "mounted-mirror");
    mirror.transform = {
        .translationMetres = {0.2, 0.12, -0.1},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    auto assembly = scene::makeDefaultMechanicalAssembly(mirror);
    const auto nominal = scene::resolveMechanicalOpticalTransform(assembly);
    CHECK(nominal == mirror.transform);

    assembly.postHeightMetres = 0.10;
    assembly.stageTranslationMetres = {0.005, -0.002, 0.003};
    assembly.mountYawRadians = 0.2;
    assembly.mountPitchRadians = -0.1;
    scene::applyMechanicalAssembly(mirror, assembly);
    REQUIRE(mirror.mechanicalAssembly.has_value());
    CHECK_NOTHROW(scene::validateBenchComponent(mirror));
    CHECK(mirror.transform.translationMetres.x
        == doctest::Approx(0.203));
    CHECK(mirror.transform.translationMetres.y
        == doctest::Approx(0.138));
    CHECK(mirror.transform.translationMetres.z
        == doctest::Approx(-0.105));
    CHECK(mirror.transform.localZAxisInWorld
        != nominal.localZAxisInWorld);

    const holobench::math::RigidTransform3d rebasedOptical {
        .translationMetres = {-0.4, 0.3, 0.2},
        .localXAxisInWorld = {1.0, 0.0, 0.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {0.0, 0.0, 1.0},
    };
    scene::rebaseMechanicalAssembly(mirror, rebasedOptical);
    CHECK(mirror.transform.translationMetres.x
        == doctest::Approx(rebasedOptical.translationMetres.x));
    CHECK(mirror.transform.translationMetres.y
        == doctest::Approx(rebasedOptical.translationMetres.y));
    CHECK(mirror.transform.translationMetres.z
        == doctest::Approx(rebasedOptical.translationMetres.z));
    CHECK(mirror.transform.localZAxisInWorld.x
        == doctest::Approx(rebasedOptical.localZAxisInWorld.x).epsilon(1e-12));
    CHECK(mirror.transform.localZAxisInWorld.y
        == doctest::Approx(rebasedOptical.localZAxisInWorld.y).epsilon(1e-12));
    CHECK(mirror.transform.localZAxisInWorld.z
        == doctest::Approx(rebasedOptical.localZAxisInWorld.z).epsilon(1e-12));

    auto invalid = assembly;
    invalid.stageTranslationMetres.x
        = invalid.maximumStageTranslationMetres.x + 1e-3;
    CHECK_THROWS_AS(
        scene::applyMechanicalAssembly(mirror, invalid),
        std::invalid_argument);

    auto inconsistent = mirror;
    inconsistent.transform.translationMetres.x += 1e-3;
    CHECK_THROWS_AS(
        scene::validateBenchComponent(inconsistent),
        std::invalid_argument);

    const auto resolvedBeforeRemoval = mirror.transform;
    scene::removeMechanicalAssembly(mirror);
    CHECK_FALSE(mirror.mechanicalAssembly.has_value());
    CHECK(mirror.transform == resolvedBeforeRemoval);
    CHECK_NOTHROW(scene::validateBenchComponent(mirror));
}

TEST_CASE("instrument calibration identity is explicit contextual and becomes stale") {
    auto detector = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "calibrated-detector");
    CHECK(detector.instrument.instrumentClass == "screen_detector");
    CHECK(detector.instrument.specificationId
        == "holobench.generic.screen_detector");
    CHECK(scene::instrumentCalibrationState(detector.instrument)
        == scene::InstrumentCalibrationState::Nominal);

    detector.instrument.manufacturer = "Example Metrology Lab";
    detector.instrument.model = "DT-1";
    detector.instrument.serialNumber = "SN-0001";
    detector.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    detector.instrument.calibrationAssets.push_back({
        .kind = scene::CalibrationAssetKind::DetectorResponse,
        .calibrationId = "detector-rgb-2026",
        .formatVersion = 1,
        .source = "calibration/detector-rgb-2026.json",
        .contentSha256
            = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
        .specificationId = detector.instrument.specificationId,
        .specificationVersion = detector.instrument.specificationVersion,
        .validity = {
            .minimumVacuumWavelengthMetres = 400e-9,
            .maximumVacuumWavelengthMetres = 700e-9,
            .minimumTemperatureKelvin = 290.0,
            .maximumTemperatureKelvin = 310.0,
        },
    });
    CHECK_NOTHROW(scene::validateBenchComponent(detector));
    CHECK(scene::instrumentCalibrationState(detector.instrument)
        == scene::InstrumentCalibrationState::Calibrated);
    constexpr std::array visibleWavelengths {450e-9, 532e-9, 638e-9};
    CHECK(scene::instrumentCalibrationStateForContext(
        detector.instrument, visibleWavelengths, 298.15)
        == scene::InstrumentCalibrationState::Calibrated);
    constexpr std::array outsideWavelengths {365e-9};
    CHECK(scene::instrumentCalibrationStateForContext(
        detector.instrument, outsideWavelengths, 298.15)
        == scene::InstrumentCalibrationState::Stale);

    auto changedSpecification = detector.instrument;
    ++changedSpecification.specificationVersion;
    CHECK(scene::instrumentCalibrationState(changedSpecification)
        == scene::InstrumentCalibrationState::Stale);

    auto invalidHash = detector.instrument;
    invalidHash.calibrationAssets.front().contentSha256 = "not-a-hash";
    CHECK_THROWS_AS(
        scene::validateInstrumentIdentity(invalidHash),
        std::invalid_argument);
    auto duplicateAssetId = detector.instrument;
    duplicateAssetId.calibrationAssets.push_back(
        duplicateAssetId.calibrationAssets.front());
    CHECK_THROWS_AS(
        scene::validateInstrumentIdentity(duplicateAssetId),
        std::invalid_argument);
    auto invalidSource = detector.instrument;
    invalidSource.calibrationAssets.front().source.clear();
    CHECK_THROWS_AS(
        scene::validateInstrumentIdentity(invalidSource),
        std::invalid_argument);
    auto invalidDomain = detector.instrument;
    invalidDomain.calibrationAssets.front()
        .validity.minimumVacuumWavelengthMetres = 800e-9;
    CHECK_THROWS_AS(
        scene::validateInstrumentIdentity(invalidDomain),
        std::invalid_argument);
    auto wrongClass = detector;
    wrongClass.instrument.instrumentClass = "laser_source";
    CHECK_THROWS_AS(
        scene::validateBenchComponent(wrongClass),
        std::invalid_argument);
}

TEST_CASE("calibration context accepts contiguous evidence split across assets") {
    auto detector = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "multi-range-detector");
    detector.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    const auto makeRange = [&](std::string id, double minimum, double maximum) {
        return scene::CalibrationAssetReference {
            .kind = scene::CalibrationAssetKind::DetectorResponse,
            .calibrationId = std::move(id),
            .formatVersion = 1,
            .source = "calibration/detector-visible.json",
            .contentSha256
                = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef",
            .specificationId = detector.instrument.specificationId,
            .specificationVersion = detector.instrument.specificationVersion,
            .validity = {
                .minimumVacuumWavelengthMetres = minimum,
                .maximumVacuumWavelengthMetres = maximum,
                .minimumTemperatureKelvin = 290.0,
                .maximumTemperatureKelvin = 310.0,
            },
        };
    };
    detector.instrument.calibrationAssets = {
        makeRange("detector-blue-green", 400e-9, 550e-9),
        makeRange("detector-green-red", 550e-9, 700e-9),
    };
    constexpr std::array rgb {450e-9, 532e-9, 638e-9};
    CHECK(scene::instrumentCalibrationStateForContext(
        detector.instrument, rgb, 298.15)
        == scene::InstrumentCalibrationState::Calibrated);
    constexpr std::array ultraviolet {365e-9};
    CHECK(scene::instrumentCalibrationStateForContext(
        detector.instrument, ultraviolet, 298.15)
        == scene::InstrumentCalibrationState::Stale);
}

TEST_CASE("placed SLM procedural commands are deterministic quantized and bounded") {
    scene::SpatialLightModulatorParameters parameters;
    parameters.pixelWidth = 4U;
    parameters.pixelHeight = 4U;
    parameters.bitDepth = 0U;
    parameters.commandPattern = scene::SlmCommandPattern::LinearRamp;
    parameters.primaryCommand = 0.125;
    parameters.horizontalCycles = 1.0;
    parameters.verticalCycles = -0.5;

    CHECK(scene::evaluateSlmNormalizedCommand(parameters, 0U, 0U)
        == doctest::Approx(0.1875));
    CHECK(scene::evaluateSlmNormalizedCommand(parameters, 3U, 3U)
        == doctest::Approx(0.5625));

    parameters.horizontalCycles = std::numeric_limits<double>::max();
    parameters.verticalCycles = std::numeric_limits<double>::max();
    CHECK_THROWS_AS(
        static_cast<void>(
            scene::evaluateSlmNormalizedCommand(parameters, 3U, 3U)),
        std::overflow_error);

    parameters.commandPattern = scene::SlmCommandPattern::Checkerboard;
    parameters.primaryCommand = 0.2;
    parameters.secondaryCommand = 0.8;
    parameters.checkerboardCellWidthPixels = 2U;
    parameters.checkerboardCellHeightPixels = 1U;
    parameters.bitDepth = 2U;
    CHECK(scene::evaluateSlmNormalizedCommand(parameters, 0U, 0U)
        == doctest::Approx(1.0 / 3.0));
    CHECK(scene::evaluateSlmNormalizedCommand(parameters, 0U, 1U)
        == doctest::Approx(2.0 / 3.0));
    CHECK_THROWS_AS(
        static_cast<void>(
            scene::evaluateSlmNormalizedCommand(parameters, 4U, 0U)),
        std::out_of_range);
}

TEST_CASE("scene commands preserve stable IDs advance revision and invalidate observations") {
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "laser-1"));
    CHECK(bench.revision() == 1);

    scene::BenchObservation observation {
        .observerComponentId = "laser-1",
        .sourceRevision = bench.revision(),
    };
    CHECK_FALSE(observation.isStaleFor(bench));

    CHECK_THROWS_AS(
        bench.add(scene::makeDefaultBenchComponent(
            scene::BenchComponentKind::PlanarMirror, "laser-1")),
        std::invalid_argument);
    CHECK(bench.revision() == 1);

    bench.duplicate("laser-1", "laser-copy");
    CHECK(bench.revision() == 2);
    CHECK(bench.find("laser-copy") != nullptr);
    CHECK(observation.isStaleFor(bench));

    auto moved = *bench.find("laser-1");
    moved.transform.translationMetres = {0.1, 0.2, 0.3};
    bench.replace("laser-1", moved);
    CHECK(bench.revision() == 3);
    CHECK(bench.find("laser-1")->transform.translationMetres.x == doctest::Approx(0.1));

    moved.id = "renamed";
    CHECK_THROWS_AS(bench.replace("laser-1", moved), std::invalid_argument);
    CHECK(bench.revision() == 3);

    CHECK(bench.remove("laser-copy"));
    CHECK(bench.revision() == 4);
    CHECK_FALSE(bench.remove("missing"));
    CHECK(bench.revision() == 4);
}
