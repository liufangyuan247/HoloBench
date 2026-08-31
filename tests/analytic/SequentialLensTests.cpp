#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

#include "optics/ray/SequentialLens.hpp"

namespace material = holobench::optics::material;
namespace math = holobench::math;
namespace ray = holobench::optics::ray;

namespace {

ray::SequentialLensPrescription makeParallelPlate() {
    const auto vacuum = material::makeVacuumMaterial();
    const material::OpticalMaterial glass {
        .id = "glass",
        .displayName = "Glass",
        .wavelengthDomain = {.minimumMetres = 400e-9, .maximumMetres = 700e-9},
        .dispersion = material::ConstantIndexModel {.refractiveIndex = 1.5},
    };
    const ray::RotationalSurface plane {
        .curvaturePerMetre = 0.0,
        .conicConstant = 0.0,
        .evenAsphereTerms = {},
        .clearSemiDiameterMetres = 0.02,
    };
    return {
        .id = "parallel_plate",
        .materials = {vacuum, glass},
        .surfaces = {
            {.id = "front", .geometry = plane, .localToWorld = {.translationMetres = {0, 0, 0}}, .materialBeforeId = "vacuum", .materialAfterId = "glass"},
            {.id = "back", .geometry = plane, .localToWorld = {.translationMetres = {0, 0, 0.01}}, .materialBeforeId = "glass", .materialAfterId = "vacuum"},
        },
    };
}

ray::SurfaceIntersectionOptions options() {
    auto value = ray::SurfaceIntersectionOptions {};
    value.maximumDistanceMetres = 0.2;
    return value;
}

} // namespace

TEST_CASE("sequential parallel plate preserves normal ray and records optical path") {
    const auto prescription = makeParallelPlate();
    const auto result = ray::traceSequentialLens(
        ray::makeRay({0, 0, -0.02}, {0, 0, 1}, 532e-9), prescription, options());
    REQUIRE(result.status == ray::SequentialTraceStatus::Completed);
    REQUIRE(result.finalRay.has_value());
    REQUIRE(result.records.size() == 2);
    CHECK(result.finalRay->direction.z == doctest::Approx(1.0).epsilon(1e-14));
    CHECK(result.totalGeometricPathMetres == doctest::Approx(0.03).epsilon(1e-13));
    CHECK(result.totalOpticalPathMetres == doctest::Approx(0.035).epsilon(1e-13));
    CHECK(result.records[0].incidentRefractiveIndex == 1.0);
    CHECK(result.records[1].incidentRefractiveIndex == 1.5);
}

TEST_CASE("sequential trace uses wavelength-dependent catalog index") {
    auto prescription = makeParallelPlate();
    prescription.materials[1] = material::makeSchottNBk7Material();
    prescription.surfaces[0].materialAfterId = "schott_n_bk7";
    prescription.surfaces[1].materialBeforeId = "schott_n_bk7";
    const auto blue = ray::traceSequentialLens(
        ray::makeRay({0, 0, -0.02}, {0.3, 0, 1}, 486.1327e-9), prescription, options());
    const auto red = ray::traceSequentialLens(
        ray::makeRay({0, 0, -0.02}, {0.3, 0, 1}, 656.2725e-9), prescription, options());
    REQUIRE(blue.records.size() == 2);
    REQUIRE(red.records.size() == 2);
    CHECK(blue.records[0].transmittedRefractiveIndex > red.records[0].transmittedRefractiveIndex);
    CHECK(blue.status == ray::SequentialTraceStatus::Completed);
    CHECK(red.status == ray::SequentialTraceStatus::Completed);
}

TEST_CASE("sequential trace reports clipping and total internal reflection") {
    auto clippedPrescription = makeParallelPlate();
    const auto clipped = ray::traceSequentialLens(
        ray::makeRay({0.03, 0, -0.02}, {0, 0, 1}), clippedPrescription, options());
    CHECK(clipped.status == ray::SequentialTraceStatus::Clipped);
    CHECK(clipped.records.size() == 1);

    auto tirPrescription = makeParallelPlate();
    tirPrescription.surfaces.erase(tirPrescription.surfaces.begin());
    tirPrescription.surfaces[0].materialBeforeId = "glass";
    tirPrescription.surfaces[0].geometry.clearSemiDiameterMetres = 0.1;
    const auto tir = ray::traceSequentialLens(
        ray::makeRay({0, 0, 0}, {0.9, 0, 0.4358898943540673}), tirPrescription, options());
    CHECK(tir.status == ray::SequentialTraceStatus::TotalInternalReflection);
    REQUIRE(tir.finalRay.has_value());
    CHECK(tir.finalRay->direction.z < 0.0);
}

TEST_CASE("sequential prescription rejects discontinuous media") {
    auto prescription = makeParallelPlate();
    prescription.surfaces[1].materialBeforeId = "vacuum";
    CHECK_THROWS_AS(ray::validateSequentialLensPrescription(prescription), std::invalid_argument);
}
