#include <doctest/doctest.h>

#include "app/BenchObservationOverlay.hpp"

namespace app = holobench::app;
namespace math = holobench::math;
namespace scene = holobench::optics::scene;

TEST_CASE("observation overlay follows the placed screen transform and extent") {
    auto screen = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "screen");
    screen.transform.translationMetres = {0.1, 0.2, 0.3};
    screen.transform.localXAxisInWorld = {0.0, 1.0, 0.0};
    screen.transform.localYAxisInWorld = {0.0, 0.0, 1.0};
    screen.transform.localZAxisInWorld = {1.0, 0.0, 0.0};
    auto parameters = std::get<scene::ScreenDetectorParameters>(
        screen.parameters);
    parameters.widthMetres = 0.04;
    parameters.heightMetres = 0.02;
    screen.parameters = parameters;

    const auto quad
        = app::observationoverlay::makeObservationPlaneQuad(screen);
    CHECK(quad.worldCorners[0].x == doctest::Approx(0.1));
    CHECK(quad.worldCorners[0].y == doctest::Approx(0.18));
    CHECK(quad.worldCorners[0].z == doctest::Approx(0.29));
    CHECK(quad.worldCorners[2].x == doctest::Approx(0.1));
    CHECK(quad.worldCorners[2].y == doctest::Approx(0.22));
    CHECK(quad.worldCorners[2].z == doctest::Approx(0.31));
}

TEST_CASE("observation overlay accepts a field probe and holographic plate") {
    const auto probe = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::FieldProbe, "probe");
    const auto quad
        = app::observationoverlay::makeObservationPlaneQuad(probe);
    CHECK(quad.worldCorners[0].x < quad.worldCorners[1].x);
    CHECK(quad.worldCorners[0].y < quad.worldCorners[3].y);

    const auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    const auto plateQuad
        = app::observationoverlay::makeObservationPlaneQuad(plate);
    CHECK(plateQuad.worldCorners[0].x < plateQuad.worldCorners[1].x);
    CHECK(plateQuad.worldCorners[0].y < plateQuad.worldCorners[3].y);

}

TEST_CASE("observation overlay rejects non-observation optics") {
    const auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "lens");
    CHECK_THROWS_AS(
        static_cast<void>(
            app::observationoverlay::makeObservationPlaneQuad(lens)),
        std::invalid_argument);
}
