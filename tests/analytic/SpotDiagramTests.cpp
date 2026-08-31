#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>
#include <vector>

#include "optics/analysis/SpotDiagram.hpp"

namespace analysis = holobench::optics::analysis;
namespace material = holobench::optics::material;
namespace math = holobench::math;
namespace ray = holobench::optics::ray;

namespace {

ray::SequentialLensPrescription makeWindow() {
    const auto vacuum = material::makeVacuumMaterial();
    return {
        .id = "window",
        .materials = {vacuum},
        .surfaces = {{
            .id = "surface",
            .geometry = {.curvaturePerMetre = 0.0, .conicConstant = 0.0, .evenAsphereTerms = {}, .clearSemiDiameterMetres = 0.01},
            .localToWorld = {},
            .materialBeforeId = "vacuum",
            .materialAfterId = "vacuum",
        }},
    };
}

ray::SurfaceIntersectionOptions options() {
    auto value = ray::SurfaceIntersectionOptions {};
    value.maximumDistanceMetres = 0.2;
    return value;
}

} // namespace

TEST_CASE("spot diagram reports centroid RMS geometric radius and chief coordinates") {
    const std::vector<ray::Ray> rays {
        ray::makeRay({-0.001, 0.0, -0.01}, {0, 0, 1}, 532e-9, 0.2),
        ray::makeRay({0.0, 0.0, -0.01}, {0, 0, 1}, 532e-9, 0.3),
        ray::makeRay({0.001, 0.0, -0.01}, {0, 0, 1}, 532e-9, 0.5),
    };
    const math::RigidTransform3d imagePlane {.translationMetres = {0, 0, 0.05}};
    const auto result = analysis::computeSpotDiagram(rays, makeWindow(), imagePlane, options(), 1);
    REQUIRE(result.samples.size() == 3);
    CHECK(result.rejectedRays.empty());
    CHECK(result.statistics.centroidXMetres == doctest::Approx(0.0).scale(1e-15));
    CHECK(result.statistics.centroidYMetres == doctest::Approx(0.0).scale(1e-15));
    CHECK(result.statistics.rmsRadiusMetres == doctest::Approx(std::sqrt(2.0 / 3.0) * 0.001).epsilon(2e-14));
    CHECK(result.statistics.geometricRadiusMetres == doctest::Approx(0.001).epsilon(2e-14));
    CHECK(result.statistics.totalPower == doctest::Approx(1.0).epsilon(2e-14));
    REQUIRE(result.chiefImageXMetres.has_value());
    CHECK(*result.chiefImageXMetres == doctest::Approx(0.0).scale(1e-15));
    REQUIRE(result.samples[2].chiefRelativeXMetres.has_value());
    CHECK(*result.samples[2].chiefRelativeXMetres == doctest::Approx(0.001).epsilon(2e-14));
}

TEST_CASE("spot diagram groups wavelengths and retains rejected ray evidence") {
    const std::vector<ray::Ray> rays {
        ray::makeRay({0.0, 0.0, -0.01}, {0, 0, 1}, 486.1327e-9),
        ray::makeRay({0.001, 0.0, -0.01}, {0, 0, 1}, 656.2725e-9),
        ray::makeRay({0.02, 0.0, -0.01}, {0, 0, 1}, 656.2725e-9),
    };
    const auto result = analysis::computeSpotDiagram(
        rays, makeWindow(), {.translationMetres = {0, 0, 0.05}}, options());
    REQUIRE(result.samples.size() == 2);
    REQUIRE(result.rejectedRays.size() == 1);
    CHECK(result.rejectedRays[0].reason == analysis::SpotRayRejectionReason::PrescriptionTraceFailed);
    REQUIRE(result.rejectedRays[0].traceStatus.has_value());
    CHECK(*result.rejectedRays[0].traceStatus == ray::SequentialTraceStatus::Clipped);
    REQUIRE(result.wavelengthGroups.size() == 2);
    CHECK(result.wavelengthGroups[0].sampleIndices.size() == 1);
    CHECK(result.wavelengthGroups[1].sampleIndices.size() == 1);
}

TEST_CASE("spot diagram validates empty bundles and chief index") {
    const std::vector<ray::Ray> empty;
    const auto computeEmpty = [&] {
        const auto result = analysis::computeSpotDiagram(empty, makeWindow(), {}, options());
        static_cast<void>(result);
    };
    CHECK_THROWS_AS(computeEmpty(), std::invalid_argument);
    const std::vector<ray::Ray> one {ray::makeRay({0, 0, -0.01}, {0, 0, 1})};
    const auto computeBadChief = [&] {
        const auto result = analysis::computeSpotDiagram(
            one, makeWindow(), {.translationMetres = {0, 0, 0.05}}, options(), 2);
        static_cast<void>(result);
    };
    CHECK_THROWS_AS(computeBadChief(), std::out_of_range);
}

TEST_CASE("spot diagram groups explicit fields and field wavelength pairs") {
    const std::vector<analysis::FieldTaggedRay> rays {
        {.ray = ray::makeRay({-0.001, 0, -0.01}, {0, 0, 1}, 486.1327e-9), .fieldId = "on_axis"},
        {.ray = ray::makeRay({0.0, 0, -0.01}, {0, 0, 1}, 486.1327e-9), .fieldId = "on_axis"},
        {.ray = ray::makeRay({0.001, 0, -0.01}, {0, 0, 1}, 656.2725e-9), .fieldId = "off_axis"},
        {.ray = ray::makeRay({0.002, 0, -0.01}, {0, 0, 1}, 486.1327e-9), .fieldId = "off_axis"},
        {.ray = ray::makeRay({0.02, 0, -0.01}, {0, 0, 1}, 656.2725e-9), .fieldId = "off_axis"},
    };
    const auto result = analysis::computeSpotDiagram(
        rays, makeWindow(), {.translationMetres = {0, 0, 0.05}}, options());

    REQUIRE(result.samples.size() == 4);
    REQUIRE(result.rejectedRays.size() == 1);
    CHECK(result.rejectedRays[0].fieldId == "off_axis");
    CHECK(result.rejectedRays[0].vacuumWavelengthMetres == 656.2725e-9);
    REQUIRE(result.fieldGroups.size() == 2);
    CHECK(result.fieldGroups[0].fieldId == "on_axis");
    CHECK(result.fieldGroups[0].sampleIndices.size() == 2);
    CHECK(result.fieldGroups[0].statistics.centroidXMetres == doctest::Approx(-0.0005));
    CHECK(result.fieldGroups[1].fieldId == "off_axis");
    CHECK(result.fieldGroups[1].sampleIndices.size() == 2);
    CHECK(result.wavelengthGroups.size() == 2);
    REQUIRE(result.fieldWavelengthGroups.size() == 3);
    CHECK(result.fieldWavelengthGroups[0].fieldId == "on_axis");
    CHECK(result.fieldWavelengthGroups[0].sampleIndices.size() == 2);
    CHECK(result.fieldWavelengthGroups[1].fieldId == "off_axis");
    CHECK(result.fieldWavelengthGroups[1].vacuumWavelengthMetres == 656.2725e-9);
    CHECK(result.fieldWavelengthGroups[2].vacuumWavelengthMetres == 486.1327e-9);

    auto invalid = rays;
    invalid[0].fieldId.clear();
    const auto computeInvalid = [&] {
        const auto invalidResult = analysis::computeSpotDiagram(
            invalid, makeWindow(), {.translationMetres = {0, 0, 0.05}}, options());
        static_cast<void>(invalidResult);
    };
    CHECK_THROWS_AS(computeInvalid(), std::invalid_argument);
}
