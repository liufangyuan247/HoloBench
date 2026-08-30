#include <doctest/doctest.h>

#include "optics/ray/ThinLens.hpp"

namespace math = holobench::math;
namespace ray = holobench::optics::ray;

namespace {

double xAtZ(const ray::Ray& value, double planeZMetres) {
    const auto intersection = ray::intersectForwardPlaneZ(value, planeZMetres);
    REQUIRE(intersection.has_value());
    return intersection->x;
}

} // namespace

TEST_CASE("collimated paraxial rays focus at positive focal length") {
    const ray::IdealThinLens lens {.focalLengthMetres = 0.05, .clearApertureRadiusMetres = 0.02};
    for (const double height : {-0.015, -0.0075, 0.0, 0.009, 0.015}) {
        const auto incident = ray::makeRay({height, 0.0, -0.1}, {0.0, 0.0, 1.0});
        const auto result = ray::traceParaxialThinLens(incident, lens);
        REQUIRE(result.status == ray::ThinLensTraceStatus::Transmitted);
        CHECK(xAtZ(*result.transmittedRay, lens.focalLengthMetres) == doctest::Approx(0.0).epsilon(1e-12));
    }
}

TEST_CASE("thin lens reproduces analytic real image position and magnification") {
    constexpr double focalLength = 0.05;
    constexpr double objectDistance = 0.20;
    constexpr double objectHeight = 0.01;
    constexpr double imageDistance = 1.0 / (1.0 / focalLength - 1.0 / objectDistance);
    constexpr double expectedImageHeight = -imageDistance / objectDistance * objectHeight;
    const ray::IdealThinLens lens {.focalLengthMetres = focalLength, .clearApertureRadiusMetres = 0.03};

    for (const double lensHeight : {-0.02, -0.01, 0.0, 0.012, 0.02}) {
        const auto incident = ray::makeRay(
            {objectHeight, 0.0, -objectDistance},
            {lensHeight - objectHeight, 0.0, objectDistance});
        const auto result = ray::traceParaxialThinLens(incident, lens);
        REQUIRE(result.status == ray::ThinLensTraceStatus::Transmitted);
        CHECK(xAtZ(*result.transmittedRay, imageDistance)
            == doctest::Approx(expectedImageHeight).epsilon(1e-12));
    }
}

TEST_CASE("object inside focal length produces an analytic virtual image") {
    constexpr double focalLength = 0.05;
    constexpr double objectDistance = 0.03;
    constexpr double objectHeight = 0.004;
    constexpr double imageDistance = 1.0 / (1.0 / focalLength - 1.0 / objectDistance);
    static_assert(imageDistance < 0.0);
    constexpr double expectedImageHeight = -imageDistance / objectDistance * objectHeight;
    const ray::IdealThinLens lens {.focalLengthMetres = focalLength, .clearApertureRadiusMetres = 0.02};

    const auto incident = ray::makeRay(
        {objectHeight, 0.0, -objectDistance},
        {0.012 - objectHeight, 0.0, objectDistance});
    const auto result = ray::traceParaxialThinLens(incident, lens);
    REQUIRE(result.status == ray::ThinLensTraceStatus::Transmitted);

    const auto& outgoing = *result.transmittedRay;
    const double signedDistance = (imageDistance - outgoing.originMetres.z) / outgoing.direction.z;
    const double reconstructedX = outgoing.originMetres.x + outgoing.direction.x * signedDistance;
    CHECK(reconstructedX == doctest::Approx(expectedImageHeight).epsilon(1e-12));
}

TEST_CASE("thin lens reports aperture clipping and non-forward rays") {
    const ray::IdealThinLens lens {.focalLengthMetres = 0.05, .clearApertureRadiusMetres = 0.01};
    const auto clipped = ray::traceParaxialThinLens(
        ray::makeRay({0.02, 0.0, -0.1}, {0.0, 0.0, 1.0}), lens);
    CHECK(clipped.status == ray::ThinLensTraceStatus::ClippedByAperture);
    CHECK_FALSE(clipped.transmittedRay.has_value());

    const auto backwards = ray::traceParaxialThinLens(
        ray::makeRay({0.0, 0.0, -0.1}, {0.0, 0.0, -1.0}), lens);
    CHECK(backwards.status == ray::ThinLensTraceStatus::NoForwardIntersection);
}

TEST_CASE("plane intersection rejects parallel and behind-origin cases") {
    CHECK_FALSE(ray::intersectForwardPlaneZ(
        ray::makeRay({0.0, 0.0, 0.0}, {1.0, 0.0, 0.0}), 1.0).has_value());
    CHECK_FALSE(ray::intersectForwardPlaneZ(
        ray::makeRay({0.0, 0.0, 1.0}, {0.0, 0.0, 1.0}), 0.0).has_value());
}

