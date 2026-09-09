#include <doctest/doctest.h>

#include <algorithm>
#include <array>
#include <cmath>
#include <chrono>
#include <numbers>
#include <stdexcept>
#include <thread>
#include <utility>

#include "app/BenchWaveObservation.hpp"
#include "app/BenchWaveObservationWorker.hpp"
#include "app/BenchWavePresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace field = holobench::field;
namespace math = holobench::math;
namespace scene = holobench::optics::scene;
namespace ray = holobench::optics::ray;
namespace fft = holobench::compute::fft;

namespace {

double peakIntensity(const field::ComplexField2D& value) {
    double result = 0.0;
    for (const auto& sample : value.samples()) {
        result = std::max(result, std::norm(sample));
    }
    return result;
}

double independentEquivalentBeamRadius(const field::ComplexField2D& value) {
    long double weightedRadiusSquared = 0.0L;
    long double totalIntensity = 0.0L;
    for (std::size_t y = 0U; y < value.height(); ++y) {
        const long double yMetres = value.yCoordinateMetres(y);
        for (std::size_t x = 0U; x < value.width(); ++x) {
            const long double xMetres = value.xCoordinateMetres(x);
            const long double intensity = std::norm(value.at(x, y));
            weightedRadiusSquared += intensity
                * (xMetres * xMetres + yMetres * yMetres);
            totalIntensity += intensity;
        }
    }
    if (!(totalIntensity > 0.0L)) {
        throw std::runtime_error("beam-radius measurement has no power");
    }
    // For a uniform circular beam, <r^2> = R^2 / 2; for a fundamental
    // Gaussian it returns the 1/e field-amplitude radius.
    return std::sqrt(static_cast<double>(
        2.0L * weightedRadiusSquared / totalIntensity));
}

scene::BenchComponent placedComponent(
    scene::BenchComponentKind kind,
    const char* id,
    math::RigidTransform3d transform) {
    auto result = scene::makeDefaultBenchComponent(kind, id);
    result.transform = transform;
    return result;
}

double firstPositiveFringeOffset(const field::ComplexField2D& value) {
    const std::size_t row = value.height() / 2U;
    const std::size_t centre = value.width() / 2U;
    const double peak = peakIntensity(value);
    for (std::size_t x = centre + 4U; x + 1U < value.width(); ++x) {
        const double previous = std::norm(value.at(x - 1U, row));
        const double current = std::norm(value.at(x, row));
        const double next = std::norm(value.at(x + 1U, row));
        if (current >= previous && current > next && current > 0.15 * peak) {
            return value.xCoordinateMetres(x);
        }
    }
    throw std::runtime_error("positive double-slit fringe was not resolved");
}

} // namespace

TEST_CASE("placed double slit produces the expected movable-screen fringe scale") {
    auto project = app::makeDoubleSlitExperimentPreset();
    fft::CpuFftBackend backend;
    auto graph = ray::traceDynamicBench(project.scene);
    const auto nearResult = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 512U, false, backend);

    CHECK(nearResult.fieldAtObservation.width() == 512U);
    CHECK(nearResult.fieldAtObservation.height() == 512U);
    CHECK(peakIntensity(nearResult.fieldAtObservation) > 0.0);
    const double expectedNear = 532e-9 * 0.50 / 0.50e-3;
    const double measuredNear
        = firstPositiveFringeOffset(nearResult.fieldAtObservation);
    CHECK(measuredNear == doctest::Approx(expectedNear).epsilon(0.18));

    auto movedScene = project.scene;
    auto movedScreen = *movedScene.find("wave-screen");
    auto desiredTransform = movedScreen.transform;
    desiredTransform.translationMetres.z = 0.80;
    scene::rebaseMechanicalAssembly(movedScreen, desiredTransform);
    movedScene.replace(movedScreen.id, movedScreen);
    CHECK(nearResult.isStaleFor(movedScene));
    graph = ray::traceDynamicBench(movedScene);
    const auto farResult = app::observeBenchWavePattern(
        movedScene, graph, "wave-screen", 512U, false, backend);
    const double measuredFar
        = firstPositiveFringeOffset(farResult.fieldAtObservation);
    CHECK(measuredFar > measuredNear * 1.45);
    CHECK(measuredFar < measuredNear * 1.75);
}

TEST_CASE("single-slit circular and bounded drag preview use the same placed screen") {
    fft::CpuFftBackend backend;
    for (auto project : {
             app::makeSingleSlitDiffractionPreset(),
             app::makeCircularDiffractionPreset()}) {
        const auto graph = ray::traceDynamicBench(project.scene);
        const auto result = app::observeBenchWavePattern(
            project.scene, graph, "wave-screen", 256U, true, backend);
        CHECK(result.interactivePreview);
        CHECK(result.fieldAtObservation.width() == 256U);
        CHECK(result.fieldAtObservation.height() == 256U);
        REQUIRE(result.contributions.size() == 1U);
        CHECK_FALSE(result.contributions.front()
            .pathSampling.usedTargetTangentProjection);
        CHECK_FALSE(result.contributions.front().pathComponentIds.empty());
        CHECK(peakIntensity(result.fieldAtObservation) > 0.0);
    }
}

TEST_CASE("live wave screen follows decentered and rotated free placement") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();

    auto screen = *project.scene.find("wave-screen");
    auto desiredTransform = screen.transform;
    desiredTransform.translationMetres.x = 0.50e-3;
    desiredTransform.translationMetres.y -= 0.25e-3;
    scene::rebaseMechanicalAssembly(screen, desiredTransform);
    project.scene.replace(screen.id, screen);
    auto graph = ray::traceDynamicBench(project.scene);
    const auto shifted = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 128U, true, backend);
    REQUIRE(shifted.contributions.size() == 1U);
    const auto& shiftedContribution = shifted.contributions.front();
    CHECK_FALSE(
        shiftedContribution.pathSampling.usedTargetTangentProjection);
    CHECK(shiftedContribution.pathComponentIds.back() == "wave-screen");
    CHECK(peakIntensity(shifted.fieldAtObservation) > 0.0);

    screen = *project.scene.find("wave-screen");
    constexpr double angle = 0.005;
    desiredTransform = screen.transform;
    desiredTransform.localXAxisInWorld = {
        std::cos(angle), 0.0, -std::sin(angle)};
    desiredTransform.localYAxisInWorld = {0.0, 1.0, 0.0};
    desiredTransform.localZAxisInWorld = {
        std::sin(angle), 0.0, std::cos(angle)};
    scene::rebaseMechanicalAssembly(screen, desiredTransform);
    project.scene.replace(screen.id, screen);
    graph = ray::traceDynamicBench(project.scene);
    const auto tilted = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 256U, true, backend);
    REQUIRE(tilted.contributions.size() == 1U);
    const auto& tiltedContribution = tilted.contributions.front();
    CHECK(tiltedContribution.pathSampling.usedTargetTangentProjection);
    CHECK_FALSE(tiltedContribution.pathSampling.warnings.empty());
    CHECK(peakIntensity(tilted.fieldAtObservation) > 0.0);
}

TEST_CASE("virtual field probe observes the wave without requiring a screen") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();
    const auto screen = *project.scene.find("wave-screen");
    REQUIRE(project.scene.remove("wave-screen"));

    auto probe = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::FieldProbe, "wave-field-probe");
    probe.transform = screen.transform;
    probe.parameters = scene::FieldProbeParameters {
        .widthMetres = 0.012,
        .heightMetres = 0.008,
        .sampleWidth = 256U,
        .sampleHeight = 256U,
    };
    project.scene.add(probe);
    auto graph = ray::traceDynamicBench(project.scene);
    const auto result = app::observeBenchWavePattern(
        project.scene, graph, probe.id, 256U, false, backend);

    CHECK(result.observationComponentId == probe.id);
    CHECK(result.fieldAtObservation.width() == 256U);
    CHECK(result.fieldAtObservation.height() == 256U);
    CHECK(peakIntensity(result.fieldAtObservation) > 0.0);
    CHECK_FALSE(result.isStaleFor(project.scene));

    probe.transform.translationMetres.x += 0.25e-3;
    project.scene.replace(probe.id, probe);
    CHECK(result.isStaleFor(project.scene));
}

TEST_CASE("placed probe exposes complex cursor and physical cross-section measurements") {
    fft::CpuFftBackend backend;
    const auto project = app::makeDoubleSlitExperimentPreset();
    const auto graph = ray::traceDynamicBench(project.scene);
    const auto result = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 256U, false, backend);

    CHECK(result.coherenceId == "wave-green");
    CHECK(result.peakIntensityWattsPerSquareMetre
        == doctest::Approx(peakIntensity(result.fieldAtObservation)));
    CHECK(result.integratedPowerWatts > 0.0);
    CHECK(result.integratedPowerWatts <= 1.0);

    const auto maximum = std::max_element(
        result.fieldAtObservation.samples().begin(),
        result.fieldAtObservation.samples().end(),
        [](const auto& lhs, const auto& rhs) {
            return std::norm(lhs) < std::norm(rhs);
        });
    REQUIRE(maximum != result.fieldAtObservation.samples().end());
    const auto flatIndex = static_cast<std::size_t>(
        std::distance(result.fieldAtObservation.samples().begin(), maximum));
    const std::size_t peakX = flatIndex % result.fieldAtObservation.width();
    const std::size_t peakY = flatIndex / result.fieldAtObservation.width();
    const auto sample = app::measureBenchWaveSample(
        result, peakX, peakY, 0.0, -80.0);
    CHECK(sample.xIndex == peakX);
    CHECK(sample.yIndex == peakY);
    CHECK(sample.complexAmplitude == *maximum);
    CHECK(sample.amplitudeMagnitude
        == doctest::Approx(std::abs(*maximum)));
    CHECK(sample.intensityWattsPerSquareMetre
        == doctest::Approx(result.peakIntensityWattsPerSquareMetre));
    CHECK(sample.decibelsRelativeToPeak == doctest::Approx(0.0));
    CHECK(sample.phaseValid);
    CHECK(sample.wrappedPhaseRadians >= -std::numbers::pi_v<double>);
    CHECK(sample.wrappedPhaseRadians < std::numbers::pi_v<double>);
    CHECK(sample.wavelengthMetres == doctest::Approx(532e-9));

    const auto masked = app::measureBenchWaveSample(
        result,
        peakX,
        peakY,
        result.peakIntensityWattsPerSquareMetre * 2.0,
        -80.0);
    CHECK_FALSE(masked.phaseValid);
    CHECK(masked.wrappedPhaseRadians == doctest::Approx(0.0));

    const auto horizontal = app::measureBenchWaveCrossSection(
        result, app::BenchFieldCrossSectionAxis::HorizontalX, peakY);
    CHECK(horizontal.coordinatesMetres.size()
        == result.fieldAtObservation.width());
    CHECK(horizontal.intensitiesWattsPerSquareMetre.size()
        == result.fieldAtObservation.width());
    CHECK(horizontal.intensitiesWattsPerSquareMetre[peakX]
        == doctest::Approx(result.peakIntensityWattsPerSquareMetre));
    CHECK(horizontal.coordinatesMetres[peakX]
        == doctest::Approx(result.fieldAtObservation.xCoordinateMetres(peakX)));

    const auto vertical = app::measureBenchWaveCrossSection(
        result, app::BenchFieldCrossSectionAxis::VerticalY, peakX);
    CHECK(vertical.coordinatesMetres.size()
        == result.fieldAtObservation.height());
    CHECK(vertical.intensitiesWattsPerSquareMetre[peakY]
        == doctest::Approx(result.peakIntensityWattsPerSquareMetre));
    CHECK_THROWS_AS(
        static_cast<void>(app::measureBenchWaveSample(
            result, result.fieldAtObservation.width(), 0U)),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(app::measureBenchWaveSample(
            result, 0U, 0U, -1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(app::measureBenchWaveSample(
            result, 0U, 0U, 0.0, 1.0)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(app::measureBenchWaveCrossSection(
            result,
            app::BenchFieldCrossSectionAxis::HorizontalX,
            result.fieldAtObservation.height())),
        std::out_of_range);
    CHECK_THROWS_AS(
        static_cast<void>(app::measureBenchWaveCrossSection(
            result,
            static_cast<app::BenchFieldCrossSectionAxis>(-1),
            0U)),
        std::invalid_argument);

    auto dark = result;
    dark.fieldAtObservation.fill({0.0, 0.0});
    dark.peakIntensityWattsPerSquareMetre = 0.0;
    dark.integratedPowerWatts = 0.0;
    const auto darkSample = app::measureBenchWaveSample(
        dark, 0U, 0U, 0.0, -90.0);
    CHECK(darkSample.intensityWattsPerSquareMetre == 0.0);
    CHECK(darkSample.decibelsRelativeToPeak == -90.0);
    CHECK_FALSE(darkSample.phaseValid);
}

TEST_CASE("live wave screen rejects stale incomplete and upstream observations") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();
    auto graph = ray::traceDynamicBench(project.scene);

    auto staleScene = project.scene;
    auto screen = *staleScene.find("wave-screen");
    auto desiredTransform = screen.transform;
    desiredTransform.translationMetres.x = 0.001;
    scene::rebaseMechanicalAssembly(screen, desiredTransform);
    staleScene.replace(screen.id, screen);
    CHECK_THROWS_AS(
        static_cast<void>(app::observeBenchWavePattern(
            staleScene, graph, "wave-screen", 128U, true, backend)),
        std::invalid_argument);

    auto upstreamScene = project.scene;
    screen = *upstreamScene.find("wave-screen");
    desiredTransform = screen.transform;
    desiredTransform.translationMetres.z = -0.20;
    scene::rebaseMechanicalAssembly(screen, desiredTransform);
    upstreamScene.replace(screen.id, screen);
    graph = ray::traceDynamicBench(upstreamScene);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            upstreamScene, graph, "wave-screen", 128U, true, backend)),
        doctest::Contains("traced source branch"),
        std::invalid_argument);

    auto unsupportedScene = project.scene;
    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens,
        "wave-intermediate-lens");
    const auto* unsupportedAperture
        = unsupportedScene.find("wave-aperture");
    REQUIRE(unsupportedAperture != nullptr);
    lens.transform = unsupportedAperture->transform;
    lens.transform.translationMetres.z = 0.25;
    constexpr double lensTiltRadians = 5.0
        * std::numbers::pi_v<double> / 180.0;
    lens.transform.localXAxisInWorld = {
        std::cos(lensTiltRadians), 0.0, -std::sin(lensTiltRadians)};
    lens.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    lens.transform.localZAxisInWorld = {
        std::sin(lensTiltRadians), 0.0, std::cos(lensTiltRadians)};
    unsupportedScene.add(lens);
    graph = ray::traceDynamicBench(unsupportedScene);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWaveChannels(
            unsupportedScene,
            graph,
            "wave-screen",
            128U,
            true,
            backend)),
        doctest::Contains("tilted relative"),
        std::invalid_argument);
}

TEST_CASE("placed observation follows lenses SLMs mirrors and splitter folds") {
    fft::CpuFftBackend backend;

    scene::BenchScene straight;
    straight.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "routed-laser"));
    auto lens = placedComponent(
        scene::BenchComponentKind::IdealThinLens,
        "routed-lens",
        {.translationMetres = {0.0, 0.0, 0.20}});
    auto lensParameters = std::get<scene::IdealThinLensParameters>(
        lens.parameters);
    lensParameters.focalLengthMetres = 0.30;
    lens.parameters = lensParameters;
    straight.add(lens);
    straight.add(placedComponent(
        scene::BenchComponentKind::SpatialLightModulator,
        "routed-slm",
        {.translationMetres = {0.0, 0.0, 0.30}}));
    straight.add(placedComponent(
        scene::BenchComponentKind::ScreenDetector,
        "routed-screen",
        {.translationMetres = {0.0, 0.0, 0.50}}));
    auto graph = ray::traceDynamicBench(straight);
    const auto straightResult = app::observeBenchWavePattern(
        straight, graph, "routed-screen", 128U, true, backend);
    REQUIRE(straightResult.contributions.size() == 1U);
    const auto& straightDiagnostics
        = straightResult.contributions.front().pathSampling;
    CHECK(straightDiagnostics.workingSampleWidth == 256U);
    CHECK(straightDiagnostics.workingSampleHeight == 256U);
    CHECK(straightDiagnostics.propagatedSegmentCount == 3U);
    CHECK(std::find(
        straightDiagnostics.appliedWaveComponentIds.begin(),
        straightDiagnostics.appliedWaveComponentIds.end(),
        "routed-lens")
        != straightDiagnostics.appliedWaveComponentIds.end());
    CHECK(std::find(
        straightDiagnostics.appliedWaveComponentIds.begin(),
        straightDiagnostics.appliedWaveComponentIds.end(),
        "routed-slm")
        != straightDiagnostics.appliedWaveComponentIds.end());
    CHECK(straightDiagnostics.appliedSlmCommandIds.size() == 1U);
    CHECK(peakIntensity(straightResult.fieldAtObservation) > 0.0);

    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    const math::RigidTransform3d foldTransform {
        .translationMetres = {0.0, 0.0, 0.50},
        .localXAxisInWorld = {
            inverseSqrtTwo, 0.0, inverseSqrtTwo},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {
            -inverseSqrtTwo, 0.0, inverseSqrtTwo},
    };
    const math::RigidTransform3d xFacingScreen {
        .translationMetres = {0.50, 0.0, 0.50},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    for (const auto& [kind, id] : std::array {
             std::pair {scene::BenchComponentKind::PlanarMirror,
                        "routed-mirror"},
             std::pair {scene::BenchComponentKind::BeamSplitterCombiner,
                        "routed-splitter"}}) {
        scene::BenchScene folded;
        folded.add(scene::makeDefaultBenchComponent(
            scene::BenchComponentKind::LaserSource, "folded-laser"));
        folded.add(placedComponent(kind, id, foldTransform));
        folded.add(placedComponent(
            scene::BenchComponentKind::ScreenDetector,
            "folded-screen",
            xFacingScreen));
        graph = ray::traceDynamicBench(folded);
        const auto foldedResult = app::observeBenchWavePattern(
            folded, graph, "folded-screen", 128U, true, backend);
        REQUIRE(foldedResult.contributions.size() == 1U);
        const auto& contribution = foldedResult.contributions.front();
        CHECK(contribution.pathSampling.workingSampleWidth == 256U);
        CHECK(contribution.pathSampling.workingSampleHeight == 256U);
        CHECK(contribution.pathSampling.propagatedSegmentCount == 2U);
        CHECK(contribution.pathSampling.usedFoldedPath);
        CHECK(contribution.pathSampling.foldedWaveComponentIds
            == std::vector<std::string> {id});
        CHECK(peakIntensity(foldedResult.fieldAtObservation) > 0.0);
    }
}

TEST_CASE("placed Galilean telescope expands a sampled Gaussian beam by its independent ABCD ratio") {
    fft::CpuFftBackend backend;
    const auto project = app::makeGalileanBeamExpanderPreset();
    const auto* laser = project.scene.find("expander-laser");
    const auto* probe = project.scene.find("expander-probe");
    REQUIRE(laser != nullptr);
    REQUIRE(probe != nullptr);

    const auto expandedGraph = ray::traceDynamicBench(project.scene);
    const auto expanded = app::observeBenchWavePattern(
        project.scene,
        expandedGraph,
        probe->id,
        512U,
        false,
        backend);
    REQUIRE(expanded.contributions.size() == 1U);
    CHECK(expanded.contributions.front().pathComponentIds
        == std::vector<std::string> {
            "expander-negative-lens",
            "expander-positive-lens",
            "expander-probe"});

    scene::BenchScene baselineBench;
    baselineBench.add(*laser);
    baselineBench.add(*probe);
    const auto baselineGraph = ray::traceDynamicBench(baselineBench);
    const auto baseline = app::observeBenchWavePattern(
        baselineBench,
        baselineGraph,
        probe->id,
        512U,
        false,
        backend);
    const double expandedRadius = independentEquivalentBeamRadius(
        expanded.fieldAtObservation);
    const double baselineRadius = independentEquivalentBeamRadius(
        baseline.fieldAtObservation);
    const double measuredExpansion = expandedRadius / baselineRadius;
    CAPTURE(expandedRadius);
    CAPTURE(baselineRadius);
    CAPTURE(measuredExpansion);
    // L2 * P(f2 + f1) * L1 gives A = -f2/f1 = 3 and C = 0.
    // The tolerance includes Gaussian diffraction and finite-grid sampling.
    CHECK(expandedRadius == doctest::Approx(1.50e-3).epsilon(0.05));
    CHECK(measuredExpansion > 2.6);
    CHECK(expanded.equivalentBeamRadiusMetres
        == doctest::Approx(expandedRadius).epsilon(1e-12));
    CHECK_FALSE(expanded.contributions.front()
        .pathSampling.thinLensPhaseUndersampled);
    CHECK(expanded.contributions.front().pathSampling
        .maximumThinLensAdjacentPhaseStepRadians < std::numbers::pi);
}

TEST_CASE("short-focus Galilean setup reports undersampled lens phase instead of credible expansion") {
    fft::CpuFftBackend backend;
    scene::BenchScene bench;
    auto laser = placedComponent(
        scene::BenchComponentKind::LaserSource,
        "undersampled-laser",
        {.translationMetres = {0.0, 0.0, -0.30}});
    auto source = std::get<scene::LaserSourceParameters>(laser.parameters);
    source.profile = scene::LaserBeamProfile::Collimated;
    source.beamRadiusMetres = 2.0e-3;
    laser.parameters = source;
    bench.add(laser);

    auto negative = placedComponent(
        scene::BenchComponentKind::IdealThinLens,
        "undersampled-negative-lens",
        {.translationMetres = {0.0, 0.0, -0.15}});
    auto negativeParameters = std::get<scene::IdealThinLensParameters>(
        negative.parameters);
    negativeParameters.focalLengthMetres = -25.0e-3;
    negativeParameters.clearApertureDiameterMetres = 10.0e-3;
    negative.parameters = negativeParameters;
    bench.add(negative);

    auto positive = placedComponent(
        scene::BenchComponentKind::IdealThinLens,
        "undersampled-positive-lens",
        {.translationMetres = {0.0, 0.0, -0.10}});
    auto positiveParameters = std::get<scene::IdealThinLensParameters>(
        positive.parameters);
    positiveParameters.focalLengthMetres = 75.0e-3;
    positiveParameters.clearApertureDiameterMetres = 20.0e-3;
    positive.parameters = positiveParameters;
    bench.add(positive);

    auto probe = placedComponent(
        scene::BenchComponentKind::FieldProbe,
        "undersampled-probe",
        {.translationMetres = {0.0, 0.0, 0.05}});
    probe.parameters = scene::FieldProbeParameters {
        .widthMetres = 25.0e-3,
        .heightMetres = 25.0e-3,
        .sampleWidth = 512U,
        .sampleHeight = 512U,
    };
    bench.add(probe);

    const auto graph = ray::traceDynamicBench(bench);
    const auto result = app::observeBenchWavePattern(
        bench, graph, probe.id, 512U, false, backend);
    REQUIRE(result.contributions.size() == 1U);
    const auto& diagnostics = result.contributions.front().pathSampling;
    CHECK(diagnostics.thinLensPhaseUndersampled);
    CHECK(diagnostics.maximumThinLensAdjacentPhaseStepRadians
        > std::numbers::pi);
    CHECK(std::any_of(
        diagnostics.warnings.begin(),
        diagnostics.warnings.end(),
        [](const std::string& warning) {
            return warning.find("thin-lens phase sampling is invalid")
                != std::string::npos;
        }));
}

TEST_CASE("placed real prescription focuses a bounded coaxial scalar field") {
    fft::CpuFftBackend backend;
    scene::BenchScene bench;
    auto laser = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource,
        "real-wave-laser");
    auto laserParameters = std::get<scene::LaserSourceParameters>(
        laser.parameters);
    laserParameters.beamRadiusMetres = 1.0e-3;
    laser.parameters = laserParameters;
    bench.add(laser);

    auto lens = placedComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "real-wave-lens",
        {.translationMetres = {0.0, 0.0, 0.50}});
    auto lensParameters
        = std::get<scene::RealLensAssemblyParameters>(lens.parameters);
    lensParameters.clearApertureDiameterMetres = 2.0e-3;
    lens.parameters = lensParameters;
    bench.add(lens);

    auto screen = placedComponent(
        scene::BenchComponentKind::ScreenDetector,
        "real-wave-screen",
        {.translationMetres = {0.0, 0.0, 0.553}});
    screen.parameters = scene::ScreenDetectorParameters {
        .widthMetres = 0.004,
        .heightMetres = 0.004,
        .sampleWidth = 256U,
        .sampleHeight = 256U,
    };
    bench.add(screen);

    const ray::LensPrescriptionCatalog catalog({
        ray::makeDefaultNBk7BiconvexPrescription()});
    const auto graph = ray::traceDynamicBench(
        bench, {}, &catalog);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            bench,
            graph,
            "real-wave-screen",
            256U,
            false,
            backend)),
        doctest::Contains("prescription resolver"),
        std::invalid_argument);

    const auto result = app::observeBenchWavePattern(
        bench,
        graph,
        "real-wave-screen",
        256U,
        false,
        backend,
        &catalog);
    REQUIRE(result.contributions.size() == 1U);
    const auto& diagnostics
        = result.contributions.front().pathSampling;
    CHECK(diagnostics.propagatedSegmentCount == 3U);
    CHECK(diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"real-wave-lens"});
    CHECK(diagnostics.appliedRealLensPrescriptionIds
        == std::vector<std::string> {
            "default_n_bk7_biconvex"});
    REQUIRE_FALSE(diagnostics.warnings.empty());
    CHECK(diagnostics.warnings.front()
        == doctest::Contains("scalar low-NA split-step"));
    const std::size_t centreX
        = result.fieldAtObservation.width() / 2U;
    const std::size_t centreY
        = result.fieldAtObservation.height() / 2U;
    const double centreIntensity = std::norm(
        result.fieldAtObservation.at(centreX, centreY));
    CHECK(centreIntensity > 10.0 * std::norm(
        result.fieldAtObservation.at(centreX + 20U, centreY)));
    CHECK(result.integratedPowerWatts > 0.0);
    CHECK(result.integratedPowerWatts <= 1.0);
}

TEST_CASE("real prescription wave adapter rejects a high-slope surface") {
    fft::CpuFftBackend backend;
    scene::BenchScene bench;
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource,
        "high-na-laser"));
    auto lens = placedComponent(
        scene::BenchComponentKind::RealLensAssembly,
        "high-na-lens",
        {.translationMetres = {0.0, 0.0, 0.20}});
    auto lensParameters
        = std::get<scene::RealLensAssemblyParameters>(lens.parameters);
    lensParameters.prescriptionId = "high-slope-lens";
    lensParameters.clearApertureDiameterMetres = 0.02;
    lens.parameters = lensParameters;
    bench.add(lens);
    bench.add(placedComponent(
        scene::BenchComponentKind::ScreenDetector,
        "high-na-screen",
        {.translationMetres = {0.0, 0.0, 0.40}}));

    auto prescription = ray::makeDefaultNBk7BiconvexPrescription();
    prescription.id = lensParameters.prescriptionId;
    prescription.surfaces.front().geometry.curvaturePerMetre = 80.0;
    const ray::LensPrescriptionCatalog catalog({prescription});
    const auto graph = ray::traceDynamicBench(
        bench, {}, &catalog);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            bench,
            graph,
            "high-na-screen",
            128U,
            true,
            backend,
            &catalog)),
        doctest::Contains("low-NA scalar surface-slope limit"),
        std::invalid_argument);
}

TEST_CASE("placed Mach-Zehnder instruments recombine fields on one Screen") {
    fft::CpuFftBackend backend;
    auto constructiveProject = app::makeMachZehnderInterferometerPreset();
    CHECK(constructiveProject.projectId == "preset-mach-zehnder");
    REQUIRE(constructiveProject.scene.components().size() == 7U);
    for (const auto& component : constructiveProject.scene.components()) {
        CHECK(component.mechanicalAssembly.has_value());
    }
    auto graph = ray::traceDynamicBench(constructiveProject.scene);
    const auto constructive = app::observeBenchWavePattern(
        constructiveProject.scene, graph, "mz-screen", 128U, true, backend);
    REQUIRE(constructive.contributions.size() == 2U);
    CHECK(constructive.contributions[0].pathSampling.usedFoldedPath);
    CHECK(constructive.contributions[1].pathSampling.usedFoldedPath);
    CHECK(constructive.peakIntensityWattsPerSquareMetre > 0.0);

    auto destructiveProject = constructiveProject;
    auto armSlm = *destructiveProject.scene.find("mz-arm-phase");
    auto slmParameters
        = std::get<scene::SpatialLightModulatorParameters>(
            armSlm.parameters);
    slmParameters.primaryCommand = 0.5;
    armSlm.parameters = slmParameters;
    destructiveProject.scene.replace("mz-arm-phase", std::move(armSlm));
    graph = ray::traceDynamicBench(destructiveProject.scene);
    const auto destructive = app::observeBenchWavePattern(
        destructiveProject.scene, graph, "mz-screen", 128U, true, backend);
    REQUIRE(destructive.contributions.size() == 2U);
    // The finite pixelated SLM clips and diffracts one arm, so its pi command
    // produces a deep physical null rather than pretending to be an ideal
    // global phase scalar with exact cancellation everywhere.
    CHECK(destructive.peakIntensityWattsPerSquareMetre
        < 0.30 * constructive.peakIntensityWattsPerSquareMetre);
}

TEST_CASE("same wavelength and coherence branches merge as complex fields") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();
    const auto baselineGraph = ray::traceDynamicBench(project.scene);
    const auto baseline = app::observeBenchWavePattern(
        project.scene, baselineGraph, "wave-screen", 128U, true, backend);

    auto secondLaser = *project.scene.find("wave-laser-green");
    secondLaser.id = "wave-laser-second";
    project.scene.add(secondLaser);
    const auto graph = ray::traceDynamicBench(project.scene);
    const auto channels = app::observeBenchWaveChannels(
        project.scene, graph, "wave-screen", 128U, true, backend);

    REQUIRE(channels.size() == 1U);
    const auto& merged = channels.front();
    CHECK(merged.coherenceId == "wave-green");
    REQUIRE(merged.contributions.size() == 2U);
    CHECK(merged.contributions[0].branchId
        < merged.contributions[1].branchId);
    CHECK(merged.peakIntensityWattsPerSquareMetre
        == doctest::Approx(
            4.0 * baseline.peakIntensityWattsPerSquareMetre)
            .epsilon(1e-11));
    CHECK(merged.integratedPowerWatts
        == doctest::Approx(4.0 * baseline.integratedPowerWatts)
            .epsilon(1e-11));

    auto quadratureProject = app::makeDoubleSlitExperimentPreset();
    auto quadratureLaser
        = *quadratureProject.scene.find("wave-laser-green");
    quadratureLaser.id = "wave-laser-quadrature";
    auto quadratureTransform = quadratureLaser.transform;
    quadratureTransform.translationMetres.z -= 0.25 * 532e-9;
    scene::rebaseMechanicalAssembly(
        quadratureLaser, quadratureTransform);
    quadratureProject.scene.add(quadratureLaser);
    const auto quadratureGraph
        = ray::traceDynamicBench(quadratureProject.scene);
    const auto quadratureChannels = app::observeBenchWaveChannels(
        quadratureProject.scene,
        quadratureGraph,
        "wave-screen",
        128U,
        true,
        backend);
    REQUIRE(quadratureChannels.size() == 1U);
    CHECK(quadratureChannels.front().peakIntensityWattsPerSquareMetre
        == doctest::Approx(
            2.0 * baseline.peakIntensityWattsPerSquareMetre)
            .epsilon(2e-7));

    auto destructiveProject = app::makeDoubleSlitExperimentPreset();
    auto destructiveLaser
        = *destructiveProject.scene.find("wave-laser-green");
    destructiveLaser.id = "wave-laser-destructive";
    auto destructiveTransform = destructiveLaser.transform;
    destructiveTransform.translationMetres.z -= 0.5 * 532e-9;
    scene::rebaseMechanicalAssembly(
        destructiveLaser, destructiveTransform);
    destructiveProject.scene.add(destructiveLaser);
    const auto destructiveGraph
        = ray::traceDynamicBench(destructiveProject.scene);
    const auto destructiveChannels = app::observeBenchWaveChannels(
        destructiveProject.scene,
        destructiveGraph,
        "wave-screen",
        128U,
        true,
        backend);
    REQUIRE(destructiveChannels.size() == 1U);
    CHECK(destructiveChannels.front().peakIntensityWattsPerSquareMetre
        < baseline.peakIntensityWattsPerSquareMetre * 1e-16);
}

TEST_CASE("wavelength and coherence identities remain independent channels") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();
    auto secondLaser = *project.scene.find("wave-laser-green");
    secondLaser.id = "wave-laser-independent";
    auto& secondParameters = std::get<scene::LaserSourceParameters>(
        secondLaser.parameters);
    secondParameters.channels.front().coherenceId = "independent-green";
    project.scene.add(secondLaser);
    auto graph = ray::traceDynamicBench(project.scene);
    auto channels = app::observeBenchWaveChannels(
        project.scene, graph, "wave-screen", 128U, true, backend);
    REQUIRE(channels.size() == 2U);
    CHECK(channels[0].fieldAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(532e-9));
    CHECK(channels[1].fieldAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(532e-9));
    CHECK(channels[0].coherenceId == "independent-green");
    CHECK(channels[1].coherenceId == "wave-green");
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            project.scene, graph, "wave-screen", 128U, true, backend)),
        doctest::Contains("exactly one wavelength and coherence"),
        std::invalid_argument);

    auto spectralProject = app::makeDoubleSlitExperimentPreset();
    auto spectralLaser = *spectralProject.scene.find("wave-laser-green");
    auto& spectralParameters = std::get<scene::LaserSourceParameters>(
        spectralLaser.parameters);
    spectralParameters.channels = {
        {.wavelengthMetres = 638e-9,
         .powerWatts = 0.20,
         .coherenceId = "spectral-red"},
        {.wavelengthMetres = 450e-9,
         .powerWatts = 0.20,
         .coherenceId = "spectral-blue"},
        {.wavelengthMetres = 532e-9,
         .powerWatts = 0.20,
         .coherenceId = "spectral-green"},
    };
    spectralProject.scene.replace(spectralLaser.id, spectralLaser);
    graph = ray::traceDynamicBench(spectralProject.scene);
    channels = app::observeBenchWaveChannels(
        spectralProject.scene,
        graph,
        "wave-screen",
        128U,
        true,
        backend);
    REQUIRE(channels.size() == 3U);
    CHECK(channels[0].fieldAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(450e-9));
    CHECK(channels[1].fieldAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(532e-9));
    CHECK(channels[2].fieldAtObservation.vacuumWavelengthMetres()
        == doctest::Approx(638e-9));
}

TEST_CASE("progressive sample stages sequence correctly") {
    const auto stages64 = app::progressiveSampleStages(64U, 64U);
    REQUIRE(stages64.size() == 1U);
    CHECK(stages64[0] == 64U);

    const auto stages128 = app::progressiveSampleStages(128U, 64U);
    REQUIRE(stages128.size() == 2U);
    CHECK(stages128[0] == 64U);
    CHECK(stages128[1] == 128U);

    const auto stages512 = app::progressiveSampleStages(512U, 64U);
    REQUIRE(stages512.size() == 4U);
    CHECK(stages512[0] == 64U);
    CHECK(stages512[1] == 128U);
    CHECK(stages512[2] == 256U);
    CHECK(stages512[3] == 512U);
}

TEST_CASE("wave observation supports immediate cancellation") {
    auto project = app::makeDoubleSlitExperimentPreset();
    fft::CpuFftBackend backend;
    auto graph = ray::traceDynamicBench(project.scene);

    std::atomic_bool cancel {true};
    const auto channels = app::observeBenchWaveChannels(
        project.scene,
        graph,
        "wave-screen",
        128U,
        true,
        backend,
        nullptr,
        nullptr,
        293.15,
        &cancel);
    CHECK(channels.empty());

    CHECK_THROWS_AS(
        [&]() {
            auto res = app::observeBenchWavePattern(
                project.scene,
                graph,
                "wave-screen",
                128U,
                true,
                backend,
                nullptr,
                nullptr,
                293.15,
                &cancel);
            (void)res;
        }(),
        holobench::optics::wave::OperationCancelledException);
}

TEST_CASE("progressive observation worker computes stages asynchronously and honors cancellation") {
    auto project = app::makeDoubleSlitExperimentPreset();
    auto graph = ray::traceDynamicBench(project.scene);

    app::BenchWaveObservationWorker worker;
    app::ObservationWorkerRequest req {
        .requestId = 1U,
        .scene = project.scene,
        .traceGraph = graph,
        .observationComponentId = "wave-screen",
        .stages = {64U, 128U},
        .lensPrescriptions = nullptr,
        .slmResponses = nullptr,
        .environmentTemperatureKelvin = 293.15,
    };

    worker.submitRequest(std::move(req));

    std::vector<app::ProgressiveObservationStage> completedStages;
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < deadline) {
        if (auto stage = worker.pollResult()) {
            completedStages.push_back(std::move(*stage));
            if (completedStages.back().isFinalStage) {
                break;
            }
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }

    REQUIRE(!completedStages.empty());
    CHECK(completedStages.front().requestId == 1U);
    CHECK(completedStages.front().sampleLimit == 64U);
    CHECK(!completedStages.front().channels.empty());
    if (completedStages.size() > 1U) {
        CHECK(completedStages.back().sampleLimit == 128U);
        CHECK(completedStages.back().isFinalStage);
    }

    // Now test immediate cancellation
    app::ObservationWorkerRequest cancelReq {
        .requestId = 2U,
        .scene = project.scene,
        .traceGraph = graph,
        .observationComponentId = "wave-screen",
        .stages = {64U, 128U, 256U, 512U},
        .lensPrescriptions = nullptr,
        .slmResponses = nullptr,
        .environmentTemperatureKelvin = 293.15,
    };
    worker.submitRequest(std::move(cancelReq));
    worker.cancel();

    // Verify worker handles clean stop without deadlock or memory leak
    worker.stop();
    CHECK(!worker.isBusy());
}
