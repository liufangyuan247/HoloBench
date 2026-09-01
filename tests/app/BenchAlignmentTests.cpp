#include <array>
#include <limits>

#include <doctest/doctest.h>

#include "app/BenchAlignment.hpp"

namespace alignment = holobench::app::alignment;
namespace math = holobench::math;
namespace scene = holobench::optics::scene;

TEST_CASE("bench alignment aims local Z at a target without moving") {
    math::RigidTransform3d selected;
    selected.translationMetres = {1.0, 2.0, 3.0};
    const auto aimed = alignment::aimAt(selected, {1.0, 2.0, 5.0});
    CHECK(aimed.translationMetres == selected.translationMetres);
    CHECK(aimed.localZAxisInWorld.x == doctest::Approx(0.0));
    CHECK(aimed.localZAxisInWorld.y == doctest::Approx(0.0));
    CHECK(aimed.localZAxisInWorld.z == doctest::Approx(1.0));
    math::validateRigidTransform(aimed);
    CHECK_THROWS_AS(
        static_cast<void>(
            alignment::aimAt(selected, selected.translationMetres)),
        std::invalid_argument);
}

TEST_CASE("coaxial height and spacing actions are ordinary rigid transforms") {
    math::RigidTransform3d target;
    target.translationMetres = {0.1, 0.2, 0.3};
    target.localXAxisInWorld = {0.0, 1.0, 0.0};
    target.localYAxisInWorld = {0.0, 0.0, 1.0};
    target.localZAxisInWorld = {1.0, 0.0, 0.0};
    math::RigidTransform3d selected;
    selected.translationMetres = {0.4, 0.8, -0.2};

    const auto coaxial = alignment::makeCoaxialWith(selected, target);
    CHECK(coaxial.translationMetres.x == doctest::Approx(0.4));
    CHECK(coaxial.translationMetres.y == doctest::Approx(0.2));
    CHECK(coaxial.translationMetres.z == doctest::Approx(0.3));
    CHECK(coaxial.localZAxisInWorld == target.localZAxisInWorld);

    const auto height = alignment::matchHeight(selected, target);
    CHECK(height.translationMetres.x == doctest::Approx(0.4));
    CHECK(height.translationMetres.y == doctest::Approx(0.2));
    CHECK(height.translationMetres.z == doctest::Approx(-0.2));

    const auto spaced
        = alignment::placeAlongTargetAxis(selected, target, -0.05);
    CHECK(spaced.translationMetres.x == doctest::Approx(0.05));
    CHECK(spaced.translationMetres.y == doctest::Approx(0.2));
    CHECK(spaced.translationMetres.z == doctest::Approx(0.3));
    CHECK(spaced.localZAxisInWorld == target.localZAxisInWorld);
}

TEST_CASE("beam snapping chooses the nearest finite segment and its direction") {
    math::RigidTransform3d selected;
    selected.translationMetres = {0.25, 0.02, 0.0};
    const std::array<scene::BenchTraceSegment, 3> segments {{
        {
            .branchId = 7U,
            .startMetres = {0.0, 0.0, 0.0},
            .endMetres = {1.0, 0.0, 0.0},
        },
        {
            .branchId = 8U,
            .startMetres = {0.0, 0.5, 0.0},
            .endMetres = {1.0, 0.5, 0.0},
        },
        {
            .branchId = 9U,
            .startMetres = {0.0, 0.0, 0.0},
            .endMetres = {0.0, 0.0, 0.0},
        },
    }};
    const auto snapped = alignment::snapToNearestBeam(
        selected, segments, 0.05);
    CHECK(snapped.branchId == 7U);
    CHECK(snapped.distanceToBeamMetres == doctest::Approx(0.02));
    CHECK(snapped.transform.translationMetres.x == doctest::Approx(0.25));
    CHECK(snapped.transform.translationMetres.y == doctest::Approx(0.0));
    CHECK(snapped.transform.localZAxisInWorld.x == doctest::Approx(1.0));
    math::validateRigidTransform(snapped.transform);

    CHECK_THROWS_AS(
        static_cast<void>(
            alignment::snapToNearestBeam(selected, segments, 0.01)),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(alignment::snapToNearestBeam(
            selected, segments, std::numeric_limits<double>::infinity())),
        std::invalid_argument);

    const std::array<scene::BenchTraceSegment, 2> crossing {{
        {
            .branchId = 10U,
            .startMetres = {0.0, 0.0, 0.0},
            .endMetres = {1.0, 0.0, 0.0},
        },
        {
            .branchId = 11U,
            .startMetres = {0.25, -1.0, 0.0},
            .endMetres = {0.25, 1.0, 0.0},
        },
    }};
    selected.translationMetres = {0.25, 0.0, 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(
            alignment::snapToNearestBeam(selected, crossing, 0.01)),
        std::invalid_argument);
}
