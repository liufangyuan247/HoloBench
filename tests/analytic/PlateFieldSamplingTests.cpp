#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>
#include <stdexcept>

#include "compute/fft/CpuFftBackend.hpp"
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

scene::BenchScene coaxialElementBench(
    scene::BenchComponent element,
    double wavelengthMetres = 532e-9,
    double beamRadiusMetres = 0.003) {
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "reference");
    source.transform.translationMetres.z = -0.1;
    auto sourceParameters = std::get<scene::LaserSourceParameters>(
        source.parameters);
    sourceParameters.beamRadiusMetres = beamRadiusMetres;
    sourceParameters.channels = {{
        .wavelengthMetres = wavelengthMetres,
        .powerWatts = 0.4,
        .coherenceId = "recording",
    }};
    source.parameters = sourceParameters;
    bench.add(std::move(source));

    element.transform.translationMetres.z = -0.05;
    bench.add(std::move(element));

    auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    auto plateParameters = std::get<scene::HolographicPlateParameters>(
        plate.parameters);
    plateParameters.widthMetres = 0.012;
    plateParameters.heightMetres = 0.012;
    plate.parameters = plateParameters;
    bench.add(std::move(plate));
    return bench;
}

scene::BenchScene foldedApertureBench() {
    constexpr double inverseSqrtTwo = 0.7071067811865475244;
    scene::BenchScene bench;
    auto source = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "folded-reference");
    source.transform.translationMetres.z = -0.1;
    auto sourceParameters = std::get<scene::LaserSourceParameters>(
        source.parameters);
    sourceParameters.beamRadiusMetres = 0.003;
    sourceParameters.channels = {{
        .wavelengthMetres = 532e-9,
        .powerWatts = 0.4,
        .coherenceId = "folded-recording",
    }};
    source.parameters = sourceParameters;
    bench.add(std::move(source));

    auto mirror = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::PlanarMirror, "fold-mirror");
    mirror.transform = {
        .translationMetres = {0.0, 0.0, 0.0},
        .localXAxisInWorld = {inverseSqrtTwo, 0.0, inverseSqrtTwo},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {-inverseSqrtTwo, 0.0, inverseSqrtTwo},
    };
    bench.add(std::move(mirror));

    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "folded-aperture");
    aperture.transform = {
        .translationMetres = {0.05, 0.0, 0.0},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    auto apertureParameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    apertureParameters.widthMetres = 0.002;
    apertureParameters.heightMetres = 0.002;
    aperture.parameters = apertureParameters;
    bench.add(std::move(aperture));

    auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    plate.transform = {
        .translationMetres = {0.1, 0.0, 0.0},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    auto plateParameters = std::get<scene::HolographicPlateParameters>(
        plate.parameters);
    plateParameters.widthMetres = 0.012;
    plateParameters.heightMetres = 0.012;
    plate.parameters = plateParameters;
    bench.add(std::move(plate));
    return bench;
}

holography::PlateFieldSamplingOptions coaxialSampling(
    std::size_t samples = 128U) {
    return {
        .sampleWidth = samples,
        .sampleHeight = samples,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 0.012,
        .extentHeightMetres = 0.012,
    };
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

TEST_CASE("coaxial placed aperture clips the propagated plate field with analytic power") {
    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "aperture");
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::Circular;
    parameters.widthMetres = 0.002;
    parameters.heightMetres = 0.002;
    aperture.parameters = parameters;
    const auto bench = coaxialElementBench(std::move(aperture));
    const auto fields = incidentFields(bench);
    const auto branchId = fields.branches.front().beam.provenance.branchId;
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench, fields, branchId, coaxialSampling(), fft);

    CHECK(sampled.diagnostics.appliedLocalWavePath);
    CHECK(sampled.diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"aperture"});
    std::size_t transmitted = 0U;
    for (std::size_t y = 0; y < sampled.field.height(); ++y) {
        for (std::size_t x = 0; x < sampled.field.width(); ++x) {
            if (std::hypot(
                    sampled.field.xCoordinateMetres(x),
                    sampled.field.yCoordinateMetres(y))
                <= 0.001) {
                ++transmitted;
            }
        }
    }
    const double expectedPower = 0.4
        / (std::numbers::pi * 0.003 * 0.003)
        * static_cast<double>(transmitted)
        * sampled.field.pitchXMetres() * sampled.field.pitchYMetres();
    CHECK(sampled.diagnostics.integratedPowerWatts
        == doctest::Approx(expectedPower).epsilon(1e-4));
}

TEST_CASE("decentered aligned aperture applies at its physical transverse position") {
    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "decentered-aperture");
    aperture.transform.translationMetres.x = 0.00075;
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::Circular;
    parameters.widthMetres = 0.002;
    parameters.heightMetres = 0.002;
    aperture.parameters = parameters;
    const auto bench = coaxialElementBench(std::move(aperture));
    const auto fields = incidentFields(bench);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        fields.branches.front().beam.provenance.branchId,
        coaxialSampling(),
        fft);

    REQUIRE(sampled.diagnostics.appliedLocalWavePath);
    const auto nearestIndex = [&](double coordinateMetres) {
        return static_cast<std::size_t>(std::llround(
            coordinateMetres / sampled.field.pitchXMetres()
            + 0.5 * static_cast<double>(sampled.field.width() - 1U)));
    };
    const std::size_t shiftedX = nearestIndex(0.00075);
    const std::size_t centre = nearestIndex(0.0);
    CHECK(std::norm(sampled.field.at(shiftedX, centre)) > 0.0);
    CHECK(std::norm(sampled.field.at(nearestIndex(0.001), centre))
        > std::norm(sampled.field.at(nearestIndex(-0.001), centre)));

    double weightedCentroidX = 0.0;
    double totalIntensity = 0.0;
    for (std::size_t y = 0; y < sampled.field.height(); ++y) {
        for (std::size_t x = 0; x < sampled.field.width(); ++x) {
            const double intensity = std::norm(sampled.field.at(x, y));
            weightedCentroidX += intensity
                * sampled.field.xCoordinateMetres(x);
            totalIntensity += intensity;
        }
    }
    REQUIRE(totalIntensity > 0.0);
    CHECK(weightedCentroidX / totalIntensity
        == doctest::Approx(0.00075).epsilon(0.04));
}

TEST_CASE("coaxial placed thin lens creates a sampled focal-plane concentration") {
    auto lens = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::IdealThinLens, "lens");
    auto parameters = std::get<scene::IdealThinLensParameters>(lens.parameters);
    parameters.focalLengthMetres = 0.05;
    parameters.clearApertureDiameterMetres = 0.006;
    lens.parameters = parameters;
    const auto bench = coaxialElementBench(std::move(lens), 10e-6);
    const auto fields = incidentFields(bench);
    const auto branchId = fields.branches.front().beam.provenance.branchId;
    const auto baseline = holography::samplePlateIncidentField(
        bench, fields, branchId, coaxialSampling(256U));
    holobench::compute::fft::CpuFftBackend fft;
    const auto focused = holography::samplePlateIncidentField(
        bench, fields, branchId, coaxialSampling(256U), fft);

    REQUIRE(focused.diagnostics.appliedLocalWavePath);
    CHECK(std::find(
        focused.diagnostics.appliedWaveComponentIds.begin(),
        focused.diagnostics.appliedWaveComponentIds.end(),
        "lens") != focused.diagnostics.appliedWaveComponentIds.end());
    const std::size_t center = focused.field.width() / 2U;
    CHECK(std::norm(focused.field.at(center, center))
        > 10.0 * std::norm(baseline.field.at(center, center)));
}

TEST_CASE("coaxial placed SLM applies finite active pixels and dead space") {
    auto device = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::SpatialLightModulator, "slm");
    auto parameters = std::get<scene::SpatialLightModulatorParameters>(
        device.parameters);
    parameters.widthMetres = 0.008;
    parameters.heightMetres = 0.008;
    parameters.pixelWidth = 8U;
    parameters.pixelHeight = 8U;
    parameters.fillFactor = 0.5;
    device.parameters = parameters;
    const auto bench = coaxialElementBench(std::move(device), 532e-9, 0.005);
    const auto fields = incidentFields(bench);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        fields.branches.front().beam.provenance.branchId,
        coaxialSampling(256U),
        fft);

    REQUIRE(sampled.diagnostics.appliedLocalWavePath);
    CHECK(sampled.diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"slm"});
    const double expectedPower = 0.4
        * (0.008 * 0.008 * 0.25)
        / (std::numbers::pi * 0.005 * 0.005);
    CHECK(sampled.diagnostics.integratedPowerWatts
        == doctest::Approx(expectedPower).epsilon(0.08));
}

TEST_CASE("tilted zero-thickness aperture projects into the beam-following field") {
    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "tilted-aperture");
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.widthMetres = 0.002;
    parameters.heightMetres = 0.002;
    aperture.parameters = parameters;
    constexpr double angle = 0.1;
    aperture.transform.localXAxisInWorld = {
        std::cos(angle), 0.0, -std::sin(angle)};
    aperture.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    aperture.transform.localZAxisInWorld = {
        std::sin(angle), 0.0, std::cos(angle)};
    const auto bench = coaxialElementBench(std::move(aperture));
    const auto fields = incidentFields(bench);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        fields.branches.front().beam.provenance.branchId,
        coaxialSampling(),
        fft);

    CHECK(sampled.diagnostics.appliedLocalWavePath);
    CHECK(sampled.diagnostics.usedTiltedElementProjection);
    CHECK(sampled.diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"tilted-aperture"});
    const double expectedProjectedArea = std::numbers::pi
        * 0.001 * std::cos(angle) * 0.001;
    const double expectedPower = 0.4 * expectedProjectedArea
        / (std::numbers::pi * 0.003 * 0.003);
    CHECK(sampled.diagnostics.integratedPowerWatts
        == doctest::Approx(expectedPower).epsilon(0.03));
}

TEST_CASE("folded mirror path transports a sampled field through a downstream aperture") {
    const auto bench = foldedApertureBench();
    const auto fields = incidentFields(bench);
    REQUIRE(fields.branches.size() == 1U);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        fields.branches.front().beam.provenance.branchId,
        coaxialSampling(),
        fft);

    CHECK(sampled.diagnostics.appliedLocalWavePath);
    CHECK(sampled.diagnostics.usedFoldedPath);
    CHECK(sampled.diagnostics.foldedWaveComponentIds
        == std::vector<std::string> {"fold-mirror"});
    CHECK(sampled.diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"fold-mirror", "folded-aperture"});
    CHECK_FALSE(sampled.diagnostics.usedPlateTangentProjection);
    CHECK(sampled.diagnostics.integratedPowerWatts > 0.0);
    CHECK(sampled.diagnostics.integratedPowerWatts < 0.4);
}

TEST_CASE("mirror fold transports transverse field parity into the outgoing local frame") {
    auto bench = foldedApertureBench();
    auto downstream = *bench.find("folded-aperture");
    auto downstreamParameters = std::get<scene::ApertureParameters>(
        downstream.parameters);
    downstreamParameters.widthMetres = 0.012;
    downstreamParameters.heightMetres = 0.012;
    downstream.parameters = downstreamParameters;
    bench.replace(downstream.id, downstream);

    auto inputAperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "decentered-input-aperture");
    inputAperture.transform.translationMetres = {0.00075, 0.0, -0.05};
    auto inputParameters = std::get<scene::ApertureParameters>(
        inputAperture.parameters);
    inputParameters.widthMetres = 0.002;
    inputParameters.heightMetres = 0.002;
    inputAperture.parameters = inputParameters;
    bench.add(std::move(inputAperture));

    const auto fields = incidentFields(bench);
    REQUIRE(fields.branches.size() == 1U);
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampled = holography::samplePlateIncidentField(
        bench,
        fields,
        fields.branches.front().beam.provenance.branchId,
        coaxialSampling(256U),
        fft);

    double weightedX = 0.0;
    double intensitySum = 0.0;
    for (std::size_t y = 0; y < sampled.field.height(); ++y) {
        for (std::size_t x = 0; x < sampled.field.width(); ++x) {
            const double intensity = std::norm(sampled.field.at(x, y));
            weightedX += intensity * sampled.field.xCoordinateMetres(x);
            intensitySum += intensity;
        }
    }
    REQUIRE(intensitySum > 0.0);
    CHECK(weightedX / intensitySum
        == doctest::Approx(-0.00075).epsilon(0.08));
    CHECK(sampled.diagnostics.usedFoldedPath);
}

TEST_CASE("oblique plate tangent projection restores the analytic plane-wave carrier") {
    constexpr double wavelength = 1e-3;
    constexpr double directionX = 0.2;
    auto bench = singleBranchBench(wavelength, 0.25, directionX);
    auto source = *bench.find("reference");
    auto sourceParameters = std::get<scene::LaserSourceParameters>(
        source.parameters);
    sourceParameters.beamRadiusMetres = 0.1;
    source.parameters = sourceParameters;
    bench.replace(source.id, source);

    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "wide-oblique-path-aperture");
    aperture.transform = source.transform;
    aperture.transform.translationMetres
        = 0.5 * source.transform.translationMetres;
    auto apertureParameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    apertureParameters.widthMetres = 0.08;
    apertureParameters.heightMetres = 0.08;
    aperture.parameters = apertureParameters;
    bench.add(std::move(aperture));

    const auto fields = incidentFields(bench);
    const auto branchId = fields.branches.front().beam.provenance.branchId;
    const holography::PlateFieldSamplingOptions options {
        .sampleWidth = 64,
        .sampleHeight = 64,
        .refractiveIndex = 1.0,
    };
    const auto analytic = holography::samplePlateIncidentField(
        bench, fields, branchId, options);
    holobench::compute::fft::CpuFftBackend fft;
    const auto refined = holography::samplePlateIncidentField(
        bench, fields, branchId, options, fft);

    REQUIRE(refined.diagnostics.appliedLocalWavePath);
    CHECK(refined.diagnostics.usedPlateTangentProjection);
    CHECK(refined.diagnostics.appliedWaveComponentIds
        == std::vector<std::string> {"wide-oblique-path-aperture"});
    for (std::size_t y = 0; y < refined.field.height(); ++y) {
        for (std::size_t x = 0; x < refined.field.width(); ++x) {
            CHECK(std::abs(refined.field.at(x, y) - analytic.field.at(x, y))
                < 2e-11);
        }
    }
}
