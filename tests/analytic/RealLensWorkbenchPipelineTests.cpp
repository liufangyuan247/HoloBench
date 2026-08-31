#include <doctest/doctest.h>

#include <cmath>
#include <stdexcept>

#include "app/RealLensWorkbenchPipeline.hpp"

namespace reallens = holobench::app::reallens;
namespace analysis = holobench::optics::analysis;

TEST_CASE("default real-lens workbench runs multi-field polychromatic engineering analysis") {
    const auto config = reallens::makeDefaultRealLensWorkbenchConfig();
    reallens::validateRealLensWorkbenchConfig(config);
    const auto result = reallens::runRealLensWorkbench(config);

    constexpr std::size_t kPupilSamples = 1 + 8 * (1 + 2 + 3 + 4);
    REQUIRE(result.incidentRays.size() == kPupilSamples * 3 * 3);
    REQUIRE(result.tracePolylines.size() == result.incidentRays.size());
    long double totalPower = 0.0L;
    for (const auto& incident : result.incidentRays) {
        totalPower += incident.ray.power;
    }
    CHECK(static_cast<double>(totalPower) == doctest::Approx(1.0).epsilon(2e-14));
    CHECK(result.spotDiagram.samples.size() > 600);
    CHECK(result.spotDiagram.rejectedRays.size() < 100);
    CHECK(result.spotDiagram.fieldGroups.size() == 3);
    CHECK(result.spotDiagram.wavelengthGroups.size() == 3);
    CHECK(result.spotDiagram.fieldWavelengthGroups.size() == 9);
    CHECK(result.spotDiagram.fieldGroups[0].fieldId == "on_axis");
    CHECK(result.spotDiagram.fieldGroups[1].fieldId == "field_x_3deg");
    CHECK(result.spotDiagram.fieldGroups[2].fieldId == "field_y_3deg");
    CHECK(std::abs(result.spotDiagram.fieldGroups[1].statistics.centroidXMetres) > 1e-4);
    CHECK(std::abs(result.spotDiagram.fieldGroups[2].statistics.centroidYMetres) > 1e-4);

    REQUIRE(result.chromaticFocus.wavelengthResults.size() == 3);
    CHECK(result.chromaticFocus.wavelengthResults[0].focus.status
        == analysis::AxialFocusFitStatus::BestFocus);
    CHECK(result.chromaticFocus.wavelengthResults[2].focus.status
        == analysis::AxialFocusFitStatus::BestFocus);
    CHECK(result.chromaticFocus.wavelengthResults[0].focus.planeZMetres
        < result.chromaticFocus.wavelengthResults[2].focus.planeZMetres);
    CHECK(result.chromaticFocus.focalShiftMetres > 0.0);

    REQUIRE(result.tracePolylines.front().worldPointsMetres.size() == 4);
    CHECK(result.tracePolylines.front().status
        == holobench::optics::ray::SequentialTraceStatus::Completed);
    CHECK(result.tracePolylines.front().fieldId == "on_axis");
}

TEST_CASE("real-lens workbench output is deterministic across repeated refreshes") {
    const auto config = reallens::makeDefaultRealLensWorkbenchConfig();
    const auto first = reallens::runRealLensWorkbench(config);
    const auto second = reallens::runRealLensWorkbench(config);

    REQUIRE(second.incidentRays.size() == first.incidentRays.size());
    REQUIRE(second.spotDiagram.samples.size() == first.spotDiagram.samples.size());
    REQUIRE(second.tracePolylines.size() == first.tracePolylines.size());
    CHECK(second.incidentRays[317].ray.originMetres == first.incidentRays[317].ray.originMetres);
    CHECK(second.incidentRays[317].ray.direction == first.incidentRays[317].ray.direction);
    CHECK(second.spotDiagram.samples[317].imageXMetres
        == first.spotDiagram.samples[317].imageXMetres);
    CHECK(second.spotDiagram.samples[317].imageYMetres
        == first.spotDiagram.samples[317].imageYMetres);
    CHECK(second.chromaticFocus.focalShiftMetres == first.chromaticFocus.focalShiftMetres);
    CHECK(second.tracePolylines[317].worldPointsMetres
        == first.tracePolylines[317].worldPointsMetres);
}

TEST_CASE("real-lens workbench rejects unsafe sampling fields spectra and focus bounds") {
    const auto validateDiscard = [](const reallens::RealLensWorkbenchConfig& config) {
        reallens::validateRealLensWorkbenchConfig(config);
    };

    auto invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.entrancePupilSemiDiameterMetres = 0.02;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.fields[1].id = invalid.fields[0].id;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.fields[0].powerFraction = 0.5;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.chromaticReferenceFieldId = "absent";
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.pupilRingCount = 129;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.pupilRingCount = 128;
    invalid.pupilSamplesPerFirstRing = 4096;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.spectrum[0].powerFraction = 0.5;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);

    invalid = reallens::makeDefaultRealLensWorkbenchConfig();
    invalid.maximumFocusPlaneZMetres = invalid.minimumFocusPlaneZMetres - 1.0;
    CHECK_THROWS_AS(validateDiscard(invalid), std::invalid_argument);
}
