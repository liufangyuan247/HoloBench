#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "app/BenchWaveObservation.hpp"
#include "app/BenchWavePresets.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace field = holobench::field;
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
    movedScreen.transform.translationMetres.z = 0.80;
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
        CHECK(result.usedShiftedPaddedPropagation);
        CHECK(peakIntensity(result.fieldAtObservation) > 0.0);
    }
}

TEST_CASE("live wave screen follows decentered and rotated free placement") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();

    auto screen = *project.scene.find("wave-screen");
    screen.transform.translationMetres.x = 0.50e-3;
    screen.transform.translationMetres.y = -0.25e-3;
    project.scene.replace(screen.id, screen);
    auto graph = ray::traceDynamicBench(project.scene);
    const auto shifted = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 128U, true, backend);
    CHECK(shifted.usedShiftedPaddedPropagation);
    CHECK_FALSE(shifted.usedTiltedPlanePropagation);
    CHECK(shifted.observationOffsetXMetres
        == doctest::Approx(0.50e-3));
    CHECK(shifted.observationOffsetYMetres
        == doctest::Approx(-0.25e-3));
    CHECK(peakIntensity(shifted.fieldAtObservation) > 0.0);

    screen = *project.scene.find("wave-screen");
    constexpr double angle = 0.005;
    screen.transform.localXAxisInWorld = {
        std::cos(angle), 0.0, -std::sin(angle)};
    screen.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    screen.transform.localZAxisInWorld = {
        std::sin(angle), 0.0, std::cos(angle)};
    project.scene.replace(screen.id, screen);
    graph = ray::traceDynamicBench(project.scene);
    const auto tilted = app::observeBenchWavePattern(
        project.scene, graph, "wave-screen", 256U, true, backend);
    CHECK_FALSE(tilted.usedShiftedPaddedPropagation);
    CHECK(tilted.usedTiltedPlanePropagation);
    CHECK(tilted.tiltedPropagation.propagatingOutputBinCount > 0U);
    CHECK(tilted.tiltedPropagation.interpolatedOutputBinCount > 0U);
    CHECK(peakIntensity(tilted.fieldAtObservation) > 0.0);
}

TEST_CASE("live wave screen rejects stale ambiguous and upstream observations") {
    fft::CpuFftBackend backend;
    auto project = app::makeDoubleSlitExperimentPreset();
    auto graph = ray::traceDynamicBench(project.scene);

    auto staleScene = project.scene;
    auto screen = *staleScene.find("wave-screen");
    screen.transform.translationMetres.x = 0.001;
    staleScene.replace(screen.id, screen);
    CHECK_THROWS_AS(
        static_cast<void>(app::observeBenchWavePattern(
            staleScene, graph, "wave-screen", 128U, true, backend)),
        std::invalid_argument);

    auto ambiguousScene = project.scene;
    auto secondLaser = *ambiguousScene.find("wave-laser-green");
    secondLaser.id = "wave-laser-second";
    ambiguousScene.add(secondLaser);
    graph = ray::traceDynamicBench(ambiguousScene);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            ambiguousScene, graph, "wave-screen", 128U, true, backend)),
        doctest::Contains("ambiguous"),
        std::invalid_argument);

    auto upstreamScene = project.scene;
    screen = *upstreamScene.find("wave-screen");
    screen.transform.translationMetres.z = -0.20;
    upstreamScene.replace(screen.id, screen);
    graph = ray::traceDynamicBench(upstreamScene);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(app::observeBenchWavePattern(
            upstreamScene, graph, "wave-screen", 128U, true, backend)),
        doctest::Contains("downstream"),
        std::invalid_argument);
}
