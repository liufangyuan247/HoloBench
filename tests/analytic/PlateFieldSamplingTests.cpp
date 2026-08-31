#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

#include "optics/holography/PlateFieldSampling.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holography = holobench::optics::holography;
namespace scene = holobench::optics::scene;
namespace ray = holobench::optics::ray;

namespace {

scene::BenchScene singleBranchBench(
    double wavelengthMetres = 532e-9,
    double powerWatts = 0.4,
    double directionX = 0.0) {
    const double directionZ = std::sqrt(1.0 - directionX * directionX);
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "reference");
    source.transform.translationMetres = {
        -0.2 * directionX,
        0.0,
        -0.2 * directionZ,
    };
    source.transform.localXAxisInWorld = {directionZ, 0.0, -directionX};
    source.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    source.transform.localZAxisInWorld = {directionX, 0.0, directionZ};
    auto sourceParameters = std::get<scene::LaserSourceParameters>(
        source.parameters);
    sourceParameters.beamRadiusMetres = 0.04;
    sourceParameters.channels = {{
        .wavelengthMetres = wavelengthMetres,
        .powerWatts = powerWatts,
        .coherenceId = "recording",
    }};
    source.parameters = sourceParameters;
    bench.add(std::move(source));

    auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    auto plateParameters = std::get<scene::HolographicPlateParameters>(
        plate.parameters);
    plateParameters.widthMetres = 0.02;
    plateParameters.heightMetres = 0.02;
    plate.parameters = plateParameters;
    bench.add(std::move(plate));
    return bench;
}

holography::PlateIncidentFieldSet incidentFields(
    const scene::BenchScene& bench) {
    return holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
}

double independentlyIntegratedPower(
    const holography::SampledPlateIncidentField& sampled,
    const holography::PlateIncidentBranch& branch) {
    double result = 0.0;
    for (const auto value : sampled.field.samples()) {
        result += std::norm(value)
            * sampled.field.pitchXMetres()
            * sampled.field.pitchYMetres()
            * std::abs(branch.localDirection.z);
    }
    return result;
}

} // namespace

TEST_CASE("plate-local collimated field preserves branch identity phase and power") {
    const auto bench = singleBranchBench();
    const auto fields = incidentFields(bench);
    REQUIRE(fields.branches.size() == 1U);
    const auto& branch = fields.branches.front();
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {.sampleWidth = 64, .sampleHeight = 32, .refractiveIndex = 1.0});

    CHECK(sampled.plateComponentId == "plate");
    CHECK(sampled.sourceRevision == bench.revision());
    CHECK(sampled.branchId == branch.beam.provenance.branchId);
    CHECK(sampled.role == holography::RecordingBranchRole::Reference);
    CHECK(sampled.side == holography::PlateIncidenceSide::NegativeLocalZ);
    CHECK(sampled.field.width() == 64U);
    CHECK(sampled.field.height() == 32U);
    CHECK(sampled.field.vacuumWavelengthMetres() == branch.beam.wavelengthMetres);
    CHECK(independentlyIntegratedPower(sampled, branch)
        == doctest::Approx(sampled.diagnostics.integratedPowerWatts).epsilon(2e-14));
    CHECK(sampled.diagnostics.integratedPowerWatts
        == doctest::Approx(
            branch.beam.powerWatts * (0.02 * 0.02)
            / (std::numbers::pi * 0.04 * 0.04)).epsilon(2e-14));
    CHECK(sampled.diagnostics.illuminatedSampleCount == sampled.field.sampleCount());
    CHECK(sampled.diagnostics.carrierSampled);
    CHECK(sampled.diagnostics.supportTouchesPlateBoundary);
    CHECK_FALSE(sampled.isStaleFor(bench));

    const auto center = sampled.field.at(
        sampled.field.width() / 2U,
        sampled.field.height() / 2U);
    const double expectedPhase = std::remainder(
        branch.beam.phaseRadians
            + 2.0 * std::numbers::pi
                * branch.beam.accumulatedOpticalPathMetres
                / branch.beam.wavelengthMetres,
        2.0 * std::numbers::pi);
    CHECK(std::abs(center / std::abs(center) - std::polar(1.0, expectedPhase))
        < 2e-10);
}

TEST_CASE("oblique plate-local field has the analytic transverse phase slope") {
    constexpr double wavelength = 1e-3;
    constexpr double directionX = 0.2;
    constexpr double refractiveIndex = 1.3;
    const auto bench = singleBranchBench(wavelength, 0.25, directionX);
    const auto fields = incidentFields(bench);
    const auto& branch = fields.branches.front();
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {.sampleWidth = 64, .sampleHeight = 64, .refractiveIndex = refractiveIndex});
    const std::size_t y = sampled.field.height() / 2U;
    const std::size_t x = sampled.field.width() / 2U;
    const auto measuredStep = sampled.field.at(x + 1U, y)
        / sampled.field.at(x, y);
    const double expectedStep = 2.0 * std::numbers::pi
        * refractiveIndex / wavelength
        * branch.localDirection.x
        * sampled.field.pitchXMetres();

    CHECK(std::abs(measuredStep - std::polar(1.0, expectedStep)) < 2e-13);
    CHECK(sampled.diagnostics.transverseFrequencyXCyclesPerMetre
        == doctest::Approx(refractiveIndex * directionX / wavelength)
            .epsilon(2e-15));
    CHECK(sampled.diagnostics.transverseFrequencyYCyclesPerMetre == 0.0);
    CHECK(sampled.diagnostics.carrierSampled);
}

TEST_CASE("plate sampling reports an unresolved optical carrier instead of aliasing silently") {
    const auto bench = singleBranchBench(532e-9, 0.4, 0.2);
    const auto fields = incidentFields(bench);
    const auto& branch = fields.branches.front();
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {.sampleWidth = 32, .sampleHeight = 32, .refractiveIndex = 1.0});

    CHECK_FALSE(sampled.diagnostics.carrierSampled);
    CHECK(std::any_of(
        sampled.diagnostics.warnings.begin(),
        sampled.diagnostics.warnings.end(),
        [](const std::string& warning) {
            return warning.find("does not resolve") != std::string::npos;
        }));
}

TEST_CASE("explicit local plate window resolves a carrier without changing the physical plate") {
    const auto bench = singleBranchBench(532e-9, 0.4, 0.02);
    const auto fields = incidentFields(bench);
    const auto& branch = fields.branches.front();
    const auto fullPlate = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {.sampleWidth = 128, .sampleHeight = 128, .refractiveIndex = 1.0});
    const auto localWindow = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {
            .sampleWidth = 128,
            .sampleHeight = 128,
            .refractiveIndex = 1.0,
            .extentWidthMetres = 1e-3,
            .extentHeightMetres = 1e-3,
            .centreXMetres = 2e-3,
            .centreYMetres = -1e-3,
        });

    CHECK_FALSE(fullPlate.diagnostics.carrierSampled);
    CHECK(localWindow.diagnostics.carrierSampled);
    CHECK(localWindow.diagnostics.usesLocalAnalysisWindow);
    CHECK(localWindow.diagnostics.sampledExtentWidthMetres == 1e-3);
    CHECK(localWindow.diagnostics.sampledExtentHeightMetres == 1e-3);
    CHECK(localWindow.diagnostics.sampledCentreXMetres == 2e-3);
    CHECK(localWindow.diagnostics.sampledCentreYMetres == -1e-3);
    CHECK(independentlyIntegratedPower(localWindow, branch)
        == doctest::Approx(localWindow.diagnostics.integratedPowerWatts)
            .epsilon(2e-12));
    CHECK(localWindow.diagnostics.integratedPowerWatts
        < fullPlate.diagnostics.integratedPowerWatts);
    CHECK(localWindow.diagnostics.integratedPowerWatts
            / fullPlate.diagnostics.integratedPowerWatts
        == doctest::Approx((1e-3 * 1e-3) / (0.02 * 0.02))
            .epsilon(2e-13));
}

TEST_CASE("Gaussian source sampling is power normalized and labels its approximation") {
    auto bench = singleBranchBench();
    auto source = *bench.find("reference");
    auto parameters = std::get<scene::LaserSourceParameters>(source.parameters);
    parameters.profile = scene::LaserBeamProfile::Gaussian;
    parameters.beamRadiusMetres = 0.003;
    source.parameters = parameters;
    bench.replace(source.id, source);
    const auto fields = incidentFields(bench);
    const auto& branch = fields.branches.front();
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        branch.beam.provenance.branchId,
        {.sampleWidth = 96, .sampleHeight = 80, .refractiveIndex = 1.0});

    CHECK(sampled.diagnostics.usesApproximateSourceEnvelope);
    CHECK(independentlyIntegratedPower(sampled, branch)
        == doctest::Approx(sampled.diagnostics.integratedPowerWatts)
            .epsilon(2e-13));
    CHECK(std::abs(sampled.field.at(48U, 40U))
        > std::abs(sampled.field.at(60U, 40U)));
}

TEST_CASE("plate sampling rejects stale evidence invalid grids and unknown branches") {
    auto bench = singleBranchBench();
    const auto fields = incidentFields(bench);
    const auto branchId = fields.branches.front().beam.provenance.branchId;
    auto plate = *bench.find("plate");
    plate.transform.translationMetres.x = 1e-4;
    bench.replace(plate.id, plate);

    CHECK_THROWS_AS(
        static_cast<void>(holography::samplePlateIncidentField(
            bench, fields, branchId)),
        std::invalid_argument);
    const auto currentFields = incidentFields(bench);
    CHECK_THROWS_AS(
        static_cast<void>(holography::samplePlateIncidentField(
            bench,
            currentFields,
            branchId,
            {.sampleWidth = 1, .sampleHeight = 32, .refractiveIndex = 1.0})),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::samplePlateIncidentField(
            bench,
            currentFields,
            branchId,
            {
                .sampleWidth = 32,
                .sampleHeight = 32,
                .refractiveIndex = 1.0,
                .extentWidthMetres = 0.1,
            })),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::samplePlateIncidentField(
            bench, currentFields, 999999U)),
        std::invalid_argument);
}
