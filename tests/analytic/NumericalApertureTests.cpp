#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numbers>
#include <sstream>
#include <string>

#include "optics/scene/NumericalAperture.hpp"
#include "optics/scene/OpticalBenchScene.hpp"

namespace scene = holobench::optics::scene;
namespace math = holobench::math;

TEST_CASE("default scene analytic object-side numerical aperture") {
    const auto bench = scene::createDefaultRealImageScene();
    const auto result = scene::computeObjectSideNumericalAperture(bench, 1.0);

    // Default: source at (0, 0, -0.15), lens at (0, 0, 0) with R = 0.025.
    // Axial distance d = 0.15 m, radius R = 0.025 m.
    constexpr double expectedAxialDistance = 0.15;
    constexpr double expectedRadius = 0.025;
    const double expectedHalfAngle = std::atan2(expectedRadius, expectedAxialDistance);
    const double expectedSinTheta = expectedRadius / std::hypot(expectedAxialDistance, expectedRadius);
    const double expectedNa = 1.0 * expectedSinTheta;

    CHECK(result.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(result.limitingStopId == bench.lens.id);
    CHECK(result.axialDistanceMetres == doctest::Approx(expectedAxialDistance).epsilon(1e-12));
    CHECK(result.halfAngleRadians == doctest::Approx(expectedHalfAngle).epsilon(1e-12));
    CHECK(result.numericalAperture == doctest::Approx(expectedNa).epsilon(1e-12));
    CHECK(result.rimRadiusMetres == doctest::Approx(expectedRadius).epsilon(1e-12));
    CHECK(result.rimCenterMetres.x == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(result.rimCenterMetres.y == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(result.rimCenterMetres.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK_FALSE(result.downstreamStopNotModeled);
    CHECK_FALSE(result.approximate);
    CHECK(result.warningMessage.empty());
}

TEST_CASE("object distance and lens clear aperture radius variations") {
    auto bench = scene::createDefaultRealImageScene();

    const double testDistances[] = {0.01, 0.05, 0.10, 0.15, 0.25, 0.50, 2.0};
    const double testRadii[] = {0.001, 0.005, 0.010, 0.025, 0.040, 0.10};

    for (const double d : testDistances) {
        for (const double r : testRadii) {
            bench.source.positionMetres = math::Vec3d {0.0, 0.0, -d};
            bench.lens.planeZMetres = 0.0;
            bench.lens.clearApertureRadiusMetres = r;
            // Move standalone aperture strictly behind source so lens clear aperture is strictly tested
            bench.aperture.planeZMetres = -d - 1.0;

            const auto result = scene::computeObjectSideNumericalAperture(bench, 1.0);
            const double expectedTheta = std::atan2(r, d);
            const double expectedNa = r / std::hypot(d, r);

            CHECK(result.limitingStop == scene::LimitingStopKind::Lens);
            CHECK(result.axialDistanceMetres == doctest::Approx(d).epsilon(1e-12));
            CHECK(result.halfAngleRadians == doctest::Approx(expectedTheta).epsilon(1e-12));
            CHECK(result.numericalAperture == doctest::Approx(expectedNa).epsilon(1e-12));
            CHECK(result.rimRadiusMetres == doctest::Approx(r).epsilon(1e-12));
            CHECK_FALSE(result.downstreamStopNotModeled);
            CHECK_FALSE(result.approximate);
            CHECK(result.warningMessage.empty());
        }
    }
}

TEST_CASE("focal length variation does not affect object-side geometric NA") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.030;
    // Aperture coplanar with large radius
    bench.aperture.planeZMetres = 0.0;
    bench.aperture.radiusMetres = 0.050;

    const auto baseResult = scene::computeObjectSideNumericalAperture(bench, 1.0);

    const double testFocals[] = {-0.10, -0.05, 0.02, 0.05, 0.10, 0.50, 10.0};
    for (const double f : testFocals) {
        bench.lens.focalLengthMetres = f;
        const auto result = scene::computeObjectSideNumericalAperture(bench, 1.0);
        CHECK(result.numericalAperture == doctest::Approx(baseResult.numericalAperture).epsilon(1e-12));
        CHECK(result.halfAngleRadians == doctest::Approx(baseResult.halfAngleRadians).epsilon(1e-12));
        CHECK(result.limitingStop == scene::LimitingStopKind::Lens);
    }
}

TEST_CASE("pre-lens standalone aperture limits angular acceptance") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.030; // tan(theta_lens) = 0.030 / 0.20 = 0.15

    // Aperture between source and lens: z = -0.10, axial distance d_a = 0.10 m
    bench.aperture.planeZMetres = -0.10;
    bench.aperture.centreXMetres = 0.0;
    bench.aperture.centreYMetres = 0.0;
    bench.aperture.radiusMetres = 0.010; // tan(theta_aperture) = 0.010 / 0.10 = 0.10 < 0.15

    const auto result = scene::computeObjectSideNumericalAperture(bench, 1.0);

    const double expectedTheta = std::atan2(0.010, 0.10);
    const double expectedNa = 0.010 / std::hypot(0.10, 0.010);

    CHECK(result.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(result.limitingStopId == bench.aperture.id);
    CHECK(result.axialDistanceMetres == doctest::Approx(0.10).epsilon(1e-12));
    CHECK(result.halfAngleRadians == doctest::Approx(expectedTheta).epsilon(1e-12));
    CHECK(result.numericalAperture == doctest::Approx(expectedNa).epsilon(1e-12));
    CHECK(result.rimRadiusMetres == doctest::Approx(0.010).epsilon(1e-12));
    CHECK(result.rimCenterMetres.z == doctest::Approx(-0.10).epsilon(1e-12));
    CHECK_FALSE(result.downstreamStopNotModeled);
    CHECK_FALSE(result.approximate);
    CHECK(result.warningMessage.empty());

    // Increase aperture radius so lens becomes limiting: tan(theta_aperture) = 0.020 / 0.10 = 0.20 > 0.15
    bench.aperture.radiusMetres = 0.020;
    const auto result2 = scene::computeObjectSideNumericalAperture(bench, 1.0);

    const double expectedThetaLens = std::atan2(0.030, 0.20);
    const double expectedNaLens = 0.030 / std::hypot(0.20, 0.030);

    CHECK(result2.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(result2.limitingStopId == bench.lens.id);
    CHECK(result2.axialDistanceMetres == doctest::Approx(0.20).epsilon(1e-12));
    CHECK(result2.halfAngleRadians == doctest::Approx(expectedThetaLens).epsilon(1e-12));
    CHECK(result2.numericalAperture == doctest::Approx(expectedNaLens).epsilon(1e-12));
    CHECK(result2.rimRadiusMetres == doctest::Approx(0.030).epsilon(1e-12));
    CHECK(result2.rimCenterMetres.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK_FALSE(result2.downstreamStopNotModeled);
    CHECK_FALSE(result2.approximate);
    CHECK(result2.warningMessage.empty());
}

TEST_CASE("coplanar aperture and lens limiting stop behavior") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.025;
    bench.aperture.planeZMetres = 0.0; // exactly coplanar

    // Smaller aperture limits
    bench.aperture.radiusMetres = 0.015;
    const auto resSmall = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resSmall.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(resSmall.limitingStopId == bench.aperture.id);
    CHECK(resSmall.rimRadiusMetres == doctest::Approx(0.015).epsilon(1e-12));
    CHECK_FALSE(resSmall.approximate);
    CHECK(resSmall.warningMessage.empty());

    // Larger aperture lets lens limit
    bench.aperture.radiusMetres = 0.035;
    const auto resLarge = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resLarge.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resLarge.limitingStopId == bench.lens.id);
    CHECK(resLarge.rimRadiusMetres == doctest::Approx(0.025).epsilon(1e-12));
    CHECK_FALSE(resLarge.approximate);
    CHECK(resLarge.warningMessage.empty());

    // Equal radius selects lens deterministically and consistently
    bench.aperture.radiusMetres = 0.025;
    const auto resEqual = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resEqual.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resEqual.rimRadiusMetres == doctest::Approx(0.025).epsilon(1e-12));
    CHECK_FALSE(resEqual.approximate);
    CHECK(resEqual.warningMessage.empty());
}

TEST_CASE("downstream aperture flags downstreamStopNotModeled and approximate") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.15};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.025;

    // Aperture placed behind lens (z_a > z_lens)
    bench.aperture.planeZMetres = 0.05;
    bench.aperture.radiusMetres = 0.005; // small aperture behind lens

    const auto result = scene::computeObjectSideNumericalAperture(bench, 1.0);

    // Must use lens entrance aperture and flag downstream limitation
    CHECK(result.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(result.limitingStopId == bench.lens.id);
    CHECK(result.axialDistanceMetres == doctest::Approx(0.15).epsilon(1e-12));
    CHECK(result.rimRadiusMetres == doctest::Approx(0.025).epsilon(1e-12));
    CHECK(result.rimCenterMetres.z == doctest::Approx(0.0).epsilon(1e-12));
    CHECK(result.downstreamStopNotModeled);
    CHECK(result.approximate);
    CHECK_FALSE(result.warningMessage.empty());
}

TEST_CASE("refractive index n scales NA linearly without changing half-angle") {
    const auto bench = scene::createDefaultRealImageScene();
    const auto refResult = scene::computeObjectSideNumericalAperture(bench, 1.0);

    const double indices[] = {0.5, 1.0, 1.333, 1.414, 1.517, 1.70, 2.417, 4.0};
    for (const double n : indices) {
        const auto res = scene::computeObjectSideNumericalAperture(bench, n);
        CHECK(res.halfAngleRadians == doctest::Approx(refResult.halfAngleRadians).epsilon(1e-12));
        CHECK(res.numericalAperture == doctest::Approx(n * std::sin(refResult.halfAngleRadians)).epsilon(1e-12));
        CHECK(res.limitingStop == scene::LimitingStopKind::Lens);
    }
}

TEST_CASE("aperture behind or at source does not limit forward acceptance") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.10};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.020;

    // Aperture strictly behind source
    bench.aperture.planeZMetres = -0.20;
    const auto resultBehind = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resultBehind.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resultBehind.axialDistanceMetres == doctest::Approx(0.10).epsilon(1e-12));
    CHECK(resultBehind.rimRadiusMetres == doctest::Approx(0.020).epsilon(1e-12));
    CHECK_FALSE(resultBehind.downstreamStopNotModeled);
    CHECK_FALSE(resultBehind.approximate);
    CHECK(resultBehind.warningMessage.empty());

    // Aperture exactly at source plane (z_a == z_s)
    bench.aperture.planeZMetres = -0.10;
    const auto resultAtSource = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resultAtSource.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resultAtSource.axialDistanceMetres == doctest::Approx(0.10).epsilon(1e-12));
    CHECK(resultAtSource.rimRadiusMetres == doctest::Approx(0.020).epsilon(1e-12));
    CHECK_FALSE(resultAtSource.downstreamStopNotModeled);
    CHECK_FALSE(resultAtSource.approximate);
    CHECK(resultAtSource.warningMessage.empty());
}

TEST_CASE("off-axis point source and decentered components detect asymmetry with approximate flag and warning") {
    // 1. Off-axis source with on-axis lens and aperture
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.010, -0.008, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.centreXMetres = 0.0;
    bench.lens.centreYMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.030;
    bench.aperture.planeZMetres = -0.50; // behind source so only off-axis lens limits NA

    const auto resOffAxisSource = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resOffAxisSource.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resOffAxisSource.approximate);
    CHECK_FALSE(resOffAxisSource.downstreamStopNotModeled);
    CHECK_FALSE(resOffAxisSource.warningMessage.empty());

    // 2. On-axis source with decentered limiting pre-lens aperture
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.centreXMetres = 0.0;
    bench.lens.centreYMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.040;

    bench.aperture.planeZMetres = -0.10;
    bench.aperture.centreXMetres = 0.005;
    bench.aperture.centreYMetres = -0.003;
    bench.aperture.radiusMetres = 0.010; // limits cone: tan(0.010/0.10) = 0.10 < tan(0.040/0.20) = 0.20

    const auto resDecenteredAp = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resDecenteredAp.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(resDecenteredAp.rimCenterMetres.x == doctest::Approx(0.005).epsilon(1e-12));
    CHECK(resDecenteredAp.rimCenterMetres.y == doctest::Approx(-0.003).epsilon(1e-12));
    CHECK(resDecenteredAp.rimCenterMetres.z == doctest::Approx(-0.10).epsilon(1e-12));
    CHECK(resDecenteredAp.rimRadiusMetres == doctest::Approx(0.010).epsilon(1e-12));
    CHECK(resDecenteredAp.approximate);
    CHECK_FALSE(resDecenteredAp.warningMessage.empty());

    // 3. Co-axially translated source and stop (transverse offset is zero)
    bench.source.positionMetres = math::Vec3d {0.005, -0.003, -0.20};
    bench.aperture.centreXMetres = 0.005;
    bench.aperture.centreYMetres = -0.003;
    bench.lens.centreXMetres = 0.005;
    bench.lens.centreYMetres = -0.003;

    const auto resCoaxial = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resCoaxial.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK_FALSE(resCoaxial.approximate);
    CHECK(resCoaxial.warningMessage.empty());

    // 4. Decentered lens with limiting lens stop
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.aperture.planeZMetres = -0.50; // behind source so decentered lens limits NA
    bench.lens.centreXMetres = 0.004;
    bench.lens.centreYMetres = 0.002;
    bench.lens.clearApertureRadiusMetres = 0.025;

    const auto resDecenteredLens = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(resDecenteredLens.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resDecenteredLens.rimCenterMetres.x == doctest::Approx(0.004).epsilon(1e-12));
    CHECK(resDecenteredLens.rimCenterMetres.y == doctest::Approx(0.002).epsilon(1e-12));
    CHECK(resDecenteredLens.approximate);
    CHECK_FALSE(resDecenteredLens.downstreamStopNotModeled);
    CHECK_FALSE(resDecenteredLens.warningMessage.empty());
}

TEST_CASE("boundary continuity across aperture positions and radii") {
    auto bench = scene::createDefaultRealImageScene();
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -0.20};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 0.020;

    // Fixed pre-lens position: vary aperture radius smoothly across the threshold
    // Threshold radius at z = -0.10: theta_a = theta_l => r_a = 0.020 * (0.10 / 0.20) = 0.010
    bench.aperture.planeZMetres = -0.10;
    constexpr double rThreshold = 0.010;

    const auto resBelow = scene::computeObjectSideNumericalAperture([&]() {
        auto b = bench;
        b.aperture.radiusMetres = rThreshold - 1e-6;
        return b;
    }());
    const auto resAt = scene::computeObjectSideNumericalAperture([&]() {
        auto b = bench;
        b.aperture.radiusMetres = rThreshold;
        return b;
    }());
    const auto resAbove = scene::computeObjectSideNumericalAperture([&]() {
        auto b = bench;
        b.aperture.radiusMetres = rThreshold + 1e-6;
        return b;
    }());

    CHECK(resBelow.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(resAt.limitingStop == scene::LimitingStopKind::Lens);
    CHECK(resAbove.limitingStop == scene::LimitingStopKind::Lens);

    // Half angle and NA are continuous across the threshold
    CHECK(resBelow.halfAngleRadians == doctest::Approx(resAt.halfAngleRadians).epsilon(1e-4));
    CHECK(resAbove.halfAngleRadians == doctest::Approx(resAt.halfAngleRadians).epsilon(1e-4));
    CHECK(resBelow.numericalAperture == doctest::Approx(resAt.numericalAperture).epsilon(1e-4));
    CHECK(resAbove.numericalAperture == doctest::Approx(resAt.numericalAperture).epsilon(1e-4));

    // Coplanar boundary continuity: as z_a approaches z_l from the left
    bench.aperture.radiusMetres = 0.015; // smaller than lens radius 0.020
    const auto resNearCoplanar = scene::computeObjectSideNumericalAperture([&]() {
        auto b = bench;
        b.aperture.planeZMetres = -1e-7;
        return b;
    }());
    const auto resExactCoplanar = scene::computeObjectSideNumericalAperture([&]() {
        auto b = bench;
        b.aperture.planeZMetres = 0.0;
        return b;
    }());

    CHECK(resNearCoplanar.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(resExactCoplanar.limitingStop == scene::LimitingStopKind::Aperture);
    CHECK(resNearCoplanar.numericalAperture == doctest::Approx(resExactCoplanar.numericalAperture).epsilon(1e-5));
    CHECK(resNearCoplanar.halfAngleRadians == doctest::Approx(resExactCoplanar.halfAngleRadians).epsilon(1e-5));
}

TEST_CASE("numerical stability and bounds on extreme scales and sin(theta) range") {
    auto bench = scene::createDefaultRealImageScene();

    // Extremely large distances (meters to astronomical scale)
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -1e6};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 1e6;
    bench.aperture.planeZMetres = -2e6; // behind source

    const auto resLarge = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(std::isfinite(resLarge.numericalAperture));
    CHECK(resLarge.numericalAperture > 0.0);
    CHECK(resLarge.numericalAperture < 1.0);
    CHECK(resLarge.halfAngleRadians == doctest::Approx(std::numbers::pi / 4.0).epsilon(1e-12));
    CHECK(resLarge.numericalAperture == doctest::Approx(std::sin(std::numbers::pi / 4.0)).epsilon(1e-12));

    // Extremely small distances (micrometers / sub-micron)
    bench.source.positionMetres = math::Vec3d {0.0, 0.0, -1e-6};
    bench.lens.planeZMetres = 0.0;
    bench.lens.clearApertureRadiusMetres = 1e-6;
    bench.aperture.planeZMetres = -2e-6; // behind source

    const auto resSmall = scene::computeObjectSideNumericalAperture(bench, 1.0);
    CHECK(std::isfinite(resSmall.numericalAperture));
    CHECK(resSmall.numericalAperture > 0.0);
    CHECK(resSmall.numericalAperture < 1.0);
    CHECK(resSmall.halfAngleRadians == doctest::Approx(std::numbers::pi / 4.0).epsilon(1e-12));
    CHECK(resSmall.numericalAperture == doctest::Approx(std::sin(std::numbers::pi / 4.0)).epsilon(1e-12));

    // Verify sin(theta) is strictly in (0, 1) and NA in (0, n)
    constexpr double nTest = 1.5;
    const auto resMedium = scene::computeObjectSideNumericalAperture(bench, nTest);
    CHECK(resMedium.numericalAperture > 0.0);
    CHECK(resMedium.numericalAperture < nTest);
}

TEST_CASE("toString and stream operators for LimitingStopKind") {
    CHECK(std::string(scene::toString(scene::LimitingStopKind::Lens)) == "Thin Lens");
    CHECK(std::string(scene::toString(scene::LimitingStopKind::Aperture)) == "Independent Aperture");

    std::ostringstream oss1;
    oss1 << scene::LimitingStopKind::Lens;
    CHECK(oss1.str() == "Thin Lens");

    std::ostringstream oss2;
    oss2 << scene::LimitingStopKind::Aperture;
    CHECK(oss2.str() == "Independent Aperture");
}

TEST_CASE("illegal inputs and invalid scenes throw invalid_argument") {
    const auto validBench = scene::createDefaultRealImageScene();

    // Invalid refractive indices
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, 0.0), std::invalid_argument);
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, -1.0), std::invalid_argument);
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, -1e-8), std::invalid_argument);
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, std::numeric_limits<double>::infinity()), std::invalid_argument);
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, -std::numeric_limits<double>::infinity()), std::invalid_argument);
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(validBench, std::numeric_limits<double>::quiet_NaN()), std::invalid_argument);

    // Source at or behind lens
    auto invalidBench = validBench;
    invalidBench.source.positionMetres = math::Vec3d {0.0, 0.0, 0.0}; // zs == zl
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.source.positionMetres = math::Vec3d {0.0, 0.0, 0.10}; // zs > zl
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    // Non-finite source position
    invalidBench = validBench;
    invalidBench.source.positionMetres.z = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.source.positionMetres.x = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    // Negative, zero, or non-finite radii
    invalidBench = validBench;
    invalidBench.lens.clearApertureRadiusMetres = 0.0;
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.lens.clearApertureRadiusMetres = -0.01;
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.lens.clearApertureRadiusMetres = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.lens.clearApertureRadiusMetres = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench = validBench;
    invalidBench.aperture.radiusMetres = 0.0;
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.aperture.radiusMetres = -0.01;
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.aperture.radiusMetres = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    // Non-finite component positions
    invalidBench = validBench;
    invalidBench.lens.planeZMetres = std::numeric_limits<double>::infinity();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.lens.planeZMetres = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench.aperture.planeZMetres = std::numeric_limits<double>::quiet_NaN();
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    // Empty IDs
    invalidBench = validBench;
    invalidBench.lens.id = "";
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);

    invalidBench = validBench;
    invalidBench.aperture.id = "";
    CHECK_THROWS_AS((void)scene::computeObjectSideNumericalAperture(invalidBench, 1.0), std::invalid_argument);
}
