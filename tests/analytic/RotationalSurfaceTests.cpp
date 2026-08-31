#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <stdexcept>

#include "optics/ray/RotationalSurface.hpp"

namespace math = holobench::math;
namespace ray = holobench::optics::ray;

namespace {

constexpr double kSagToleranceMetres = 2e-14;

ray::SurfaceIntersectionOptions shortTraceOptions() {
    return ray::SurfaceIntersectionOptions {
        .intersectionEpsilonMetres = 1e-12,
        .maximumDistanceMetres = 0.2,
        .residualToleranceMetres = 2e-13,
        .apertureToleranceMetres = 1e-12,
        .maximumIterations = 96,
        .bracketSubdivisions = 2048,
    };
}

double independentAsphereSag(
    double radiusMetres,
    double curvaturePerMetre,
    double conicConstant,
    double a4PerMetreCubed,
    double a6PerMetreFifth) {
    const double cr = curvaturePerMetre * radiusMetres;
    const double base = curvaturePerMetre * radiusMetres * radiusMetres
        / (1.0 + std::sqrt(1.0 - (1.0 + conicConstant) * cr * cr));
    const double r2 = radiusMetres * radiusMetres;
    return base + a4PerMetreCubed * r2 * r2 + a6PerMetreFifth * r2 * r2 * r2;
}

} // namespace

TEST_CASE("rotational surface sag follows signed sphere and conic conventions") {
    SUBCASE("positive and negative spherical curvature") {
        const double radiusOfCurvature = 0.050;
        for (const double sign : {1.0, -1.0}) {
            const ray::RotationalSurface surface {
                .curvaturePerMetre = sign / radiusOfCurvature,
                .conicConstant = 0.0,
                .evenAsphereTerms = {},
                .clearSemiDiameterMetres = 0.010,
            };
            const double radial = 0.008;
            const auto evaluation = ray::evaluateSurfaceSag(surface, radial);
            const double expected = sign
                * (radiusOfCurvature - std::sqrt(radiusOfCurvature * radiusOfCurvature - radial * radial));
            CHECK(evaluation.sagMetres == doctest::Approx(expected).epsilon(1e-13));
            CHECK(evaluation.radialDerivative
                == doctest::Approx(sign * radial / std::sqrt(radiusOfCurvature * radiusOfCurvature - radial * radial))
                    .epsilon(1e-13));
        }
    }

    SUBCASE("paraboloid and even-asphere terms") {
        const ray::RotationalSurface surface {
            .curvaturePerMetre = 25.0,
            .conicConstant = -1.0,
            .evenAsphereTerms = {
                {.radialOrder = 4, .coefficientSi = 2.0e4},
                {.radialOrder = 6, .coefficientSi = -3.0e7},
            },
            .clearSemiDiameterMetres = 0.006,
        };
        const double radial = 0.004;
        const auto evaluation = ray::evaluateSurfaceSag(surface, radial);
        const double expected = independentAsphereSag(radial, 25.0, -1.0, 2.0e4, -3.0e7);
        const double expectedDerivative = 25.0 * radial
            + 4.0 * 2.0e4 * radial * radial * radial
            + 6.0 * -3.0e7 * std::pow(radial, 5.0);
        CHECK(evaluation.sagMetres == doctest::Approx(expected).epsilon(1e-13));
        CHECK(evaluation.radialDerivative == doctest::Approx(expectedDerivative).epsilon(1e-13));
    }
}

TEST_CASE("rotational surface validation rejects ambiguous or impossible prescriptions") {
    ray::RotationalSurface surface {};
    CHECK_NOTHROW(ray::validateRotationalSurface(surface));

    surface.clearSemiDiameterMetres = 0.0;
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface = ray::RotationalSurface {
        .curvaturePerMetre = 100.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.011,
    };
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface.clearSemiDiameterMetres = 0.010;
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface = ray::RotationalSurface {
        .curvaturePerMetre = 10.0,
        .conicConstant = -1.0,
        .evenAsphereTerms = {{.radialOrder = 5, .coefficientSi = 1.0}},
        .clearSemiDiameterMetres = 0.01,
    };
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface.evenAsphereTerms = {
        {.radialOrder = 6, .coefficientSi = 1.0},
        {.radialOrder = 4, .coefficientSi = 1.0},
    };
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface.evenAsphereTerms = {{
        .radialOrder = 4,
        .coefficientSi = std::numeric_limits<double>::infinity(),
    }};
    CHECK_THROWS_AS(ray::validateRotationalSurface(surface), std::invalid_argument);

    surface.evenAsphereTerms = {{.radialOrder = 5, .coefficientSi = 0.0}};
    const auto evaluateInvalidSurface = [&] {
        const auto evaluation = ray::evaluateSurfaceSag(surface, 0.001);
        static_cast<void>(evaluation);
    };
    CHECK_THROWS_AS(evaluateInvalidSurface(), std::invalid_argument);
}

TEST_CASE("analytic plane intersection reports hit miss and clear-aperture clipping") {
    const ray::RotationalSurface plane {
        .curvaturePerMetre = 0.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.010,
    };
    const auto options = shortTraceOptions();

    const auto hit = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.003, -0.004, -0.05}, {0.0, 0.0, 1.0}),
        plane,
        options);
    REQUIRE(hit.status == ray::SurfaceIntersectionStatus::Hit);
    CHECK(hit.distanceMetres == doctest::Approx(0.05).epsilon(1e-14));
    CHECK(hit.pointMetres.z == doctest::Approx(0.0).scale(kSagToleranceMetres));
    CHECK(hit.geometricNormal.z == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(hit.spatialResidualMetres <= options.residualToleranceMetres);

    const auto clipped = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.011, 0.0, -0.05}, {0.0, 0.0, 1.0}),
        plane,
        options);
    CHECK(clipped.status == ray::SurfaceIntersectionStatus::Clipped);
    CHECK(clipped.pointMetres.x == doctest::Approx(0.011).epsilon(1e-14));

    const auto parallel = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.0, 0.0, -0.05}, {1.0, 0.0, 0.0}),
        plane,
        options);
    CHECK(parallel.status == ray::SurfaceIntersectionStatus::Miss);
}

TEST_CASE("analytic sphere roots select the explicit vertex sag sheet") {
    const ray::RotationalSurface sphere {
        .curvaturePerMetre = 20.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.012,
    };
    const auto options = shortTraceOptions();

    const auto axial = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.0, 0.0, -0.04}, {0.0, 0.0, 1.0}),
        sphere,
        options);
    REQUIRE(axial.status == ray::SurfaceIntersectionStatus::Hit);
    CHECK(axial.distanceMetres == doctest::Approx(0.04).epsilon(1e-13));
    CHECK(axial.pointMetres.z == doctest::Approx(0.0).scale(kSagToleranceMetres));

    const double x = 0.007;
    const double expectedSag = 0.05 - std::sqrt(0.05 * 0.05 - x * x);
    const auto offAxis = ray::intersectRotationalSurfaceForward(
        ray::makeRay({x, 0.0, -0.04}, {0.0, 0.0, 1.0}),
        sphere,
        options);
    REQUIRE(offAxis.status == ray::SurfaceIntersectionStatus::Hit);
    CHECK(offAxis.pointMetres.z == doctest::Approx(expectedSag).epsilon(2e-13));
    CHECK(offAxis.distanceMetres == doctest::Approx(0.04 + expectedSag).epsilon(2e-13));
    CHECK(offAxis.spatialResidualMetres <= options.residualToleranceMetres);

    const ray::RotationalSurface negativeSphere {
        .curvaturePerMetre = -20.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.012,
    };
    const auto negative = ray::intersectRotationalSurfaceForward(
        ray::makeRay({x, 0.0, -0.04}, {0.0, 0.0, 1.0}),
        negativeSphere,
        options);
    REQUIRE(negative.status == ray::SurfaceIntersectionStatus::Hit);
    CHECK(negative.pointMetres.z == doctest::Approx(-expectedSag).epsilon(2e-13));
}

TEST_CASE("analytic conic intersection agrees with independent paraboloid sag") {
    const ray::RotationalSurface paraboloid {
        .curvaturePerMetre = 30.0,
        .conicConstant = -1.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.010,
    };
    const double x = 0.006;
    const double expectedSag = 0.5 * 30.0 * x * x;
    const auto result = ray::intersectRotationalSurfaceForward(
        ray::makeRay({x, 0.0, -0.03}, {0.0, 0.0, 1.0}),
        paraboloid,
        shortTraceOptions());
    REQUIRE(result.status == ray::SurfaceIntersectionStatus::Hit);
    CHECK(result.pointMetres.z == doctest::Approx(expectedSag).epsilon(1e-13));
    CHECK(result.spatialResidualMetres <= shortTraceOptions().residualToleranceMetres);
}

TEST_CASE("safeguarded even-asphere solve passes independent sag residuals") {
    const ray::RotationalSurface asphere {
        .curvaturePerMetre = 18.0,
        .conicConstant = -0.7,
        .evenAsphereTerms = {
            {.radialOrder = 4, .coefficientSi = 1.5e4},
            {.radialOrder = 6, .coefficientSi = -8.0e7},
        },
        .clearSemiDiameterMetres = 0.008,
    };

    SUBCASE("fixed-radius axial ray") {
        const double x = 0.0045;
        const auto result = ray::intersectRotationalSurfaceForward(
            ray::makeRay({x, 0.0, -0.025}, {0.0, 0.0, 1.0}),
            asphere,
            shortTraceOptions());
        REQUIRE(result.status == ray::SurfaceIntersectionStatus::Hit);
        const double expected = independentAsphereSag(x, 18.0, -0.7, 1.5e4, -8.0e7);
        CHECK(result.pointMetres.z == doctest::Approx(expected).epsilon(2e-12));
        CHECK(result.spatialResidualMetres <= shortTraceOptions().residualToleranceMetres);
    }

    SUBCASE("oblique ray") {
        const auto incident = ray::makeRay({-0.002, 0.001, -0.03}, {0.08, -0.02, 1.0});
        const auto result = ray::intersectRotationalSurfaceForward(incident, asphere, shortTraceOptions());
        REQUIRE(result.status == ray::SurfaceIntersectionStatus::Hit);
        const double radial = std::hypot(result.pointMetres.x, result.pointMetres.y);
        const double independentSag = independentAsphereSag(radial, 18.0, -0.7, 1.5e4, -8.0e7);
        CHECK(std::abs(result.pointMetres.z - independentSag) <= 2e-13);
        CHECK(result.distanceMetres > 0.0);
        CHECK(result.distanceMetres < shortTraceOptions().maximumDistanceMetres);
    }

    SUBCASE("mathematical hit outside clear aperture is clipped") {
        const auto result = ray::intersectRotationalSurfaceForward(
            ray::makeRay({0.0085, 0.0, -0.025}, {0.0, 0.0, 1.0}),
            asphere,
            shortTraceOptions());
        REQUIRE(result.status == ray::SurfaceIntersectionStatus::Clipped);
        CHECK(result.spatialResidualMetres <= shortTraceOptions().residualToleranceMetres);
    }
}

TEST_CASE("surface analytic normal agrees with independent central differences") {
    const ray::RotationalSurface surface {
        .curvaturePerMetre = -22.0,
        .conicConstant = -0.4,
        .evenAsphereTerms = {{.radialOrder = 4, .coefficientSi = -9.0e3}},
        .clearSemiDiameterMetres = 0.009,
    };
    const double x = 0.003;
    const double y = -0.004;
    const double radius = std::hypot(x, y);
    const double z = independentAsphereSag(radius, -22.0, -0.4, -9.0e3, 0.0);
    const math::Vec3d normal = ray::surfaceNormalAt(surface, {x, y, z});

    const double h = 2e-8;
    const auto independentSagAt = [&](double sampleX, double sampleY) {
        return independentAsphereSag(std::hypot(sampleX, sampleY), -22.0, -0.4, -9.0e3, 0.0);
    };
    const double dzdx = (independentSagAt(x + h, y) - independentSagAt(x - h, y)) / (2.0 * h);
    const double dzdy = (independentSagAt(x, y + h) - independentSagAt(x, y - h)) / (2.0 * h);
    const math::Vec3d finiteDifferenceNormal = math::normalized({-dzdx, -dzdy, 1.0});
    CHECK(normal.x == doctest::Approx(finiteDifferenceNormal.x).epsilon(2e-9));
    CHECK(normal.y == doctest::Approx(finiteDifferenceNormal.y).epsilon(2e-9));
    CHECK(normal.z == doctest::Approx(finiteDifferenceNormal.z).epsilon(2e-9));
}

TEST_CASE("surface solver rejects invalid options and non-forward roots") {
    const ray::RotationalSurface plane {};
    auto options = shortTraceOptions();
    options.maximumDistanceMetres = options.intersectionEpsilonMetres;
    const auto invokeWithInvalidOptions = [&] {
        const auto result = ray::intersectRotationalSurfaceForward(
            ray::makeRay({0.0, 0.0, -0.01}, {0.0, 0.0, 1.0}), plane, options);
        static_cast<void>(result);
    };
    CHECK_THROWS_AS(invokeWithInvalidOptions(), std::invalid_argument);

    const auto behind = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.0, 0.0, 0.01}, {0.0, 0.0, 1.0}),
        plane,
        shortTraceOptions());
    CHECK(behind.status == ray::SurfaceIntersectionStatus::Miss);

    options = shortTraceOptions();
    options.maximumDistanceMetres = 0.005;
    const auto beyondTraceLimit = ray::intersectRotationalSurfaceForward(
        ray::makeRay({0.0, 0.0, -0.01}, {0.0, 0.0, 1.0}),
        plane,
        options);
    CHECK(beyondTraceLimit.status == ray::SurfaceIntersectionStatus::Miss);
}
