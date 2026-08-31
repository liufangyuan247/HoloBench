#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <stdexcept>

#include "optics/holography/BenchHologramRecording.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holography = holobench::optics::holography;
namespace scene = holobench::optics::scene;
namespace ray = holobench::optics::ray;

namespace {

scene::BenchComponent makeSource(
    scene::BenchComponentKind kind,
    const char* id,
    double directionX,
    bool positiveZ,
    double powerWatts) {
    const double directionZMagnitude = std::sqrt(1.0 - directionX * directionX);
    const double directionZ = positiveZ
        ? directionZMagnitude
        : -directionZMagnitude;
    auto source = scene::makeDefaultBenchComponent(kind, id);
    source.transform.translationMetres = {
        -0.2 * directionX,
        0.0,
        -0.2 * directionZ,
    };
    source.transform.localXAxisInWorld = positiveZ
        ? holobench::math::Vec3d {directionZMagnitude, 0.0, -directionX}
        : holobench::math::Vec3d {-directionZMagnitude, 0.0, directionX};
    source.transform.localYAxisInWorld = {0.0, 1.0, 0.0};
    source.transform.localZAxisInWorld = {directionX, 0.0, directionZ};
    if (kind == scene::BenchComponentKind::LaserSource) {
        auto parameters = std::get<scene::LaserSourceParameters>(source.parameters);
        parameters.beamRadiusMetres = 0.003;
        parameters.channels = {{
            .wavelengthMetres = 532e-9,
            .powerWatts = powerWatts,
            .coherenceId = "recording",
        }};
        source.parameters = parameters;
    } else {
        auto parameters = std::get<scene::ObjectWavefrontSourceParameters>(
            source.parameters);
        parameters.channel = {
            .wavelengthMetres = 532e-9,
            .powerWatts = powerWatts,
            .coherenceId = "recording",
        };
        source.parameters = parameters;
    }
    return source;
}

scene::BenchScene recordingBench(bool reflection) {
    scene::BenchScene bench;
    bench.add(makeSource(
        scene::BenchComponentKind::ObjectWavefrontSource,
        "object",
        0.0,
        !reflection,
        0.25));
    bench.add(makeSource(
        scene::BenchComponentKind::LaserSource,
        "reference",
        0.02,
        true,
        0.4));
    auto plate = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate");
    auto parameters = std::get<scene::HolographicPlateParameters>(plate.parameters);
    parameters.widthMetres = 0.02;
    parameters.heightMetres = 0.02;
    plate.parameters = parameters;
    bench.add(std::move(plate));
    return bench;
}

struct PairIds final {
    std::uint64_t object = 0;
    std::uint64_t reference = 0;
};

PairIds pairIds(const holography::PlateIncidentFieldSet& fields) {
    PairIds result;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            result.object = branch.beam.provenance.branchId;
        } else {
            result.reference = branch.beam.provenance.branchId;
        }
    }
    return result;
}

holography::ThinPlateRecordingOptions resolvedOptions() {
    holography::ThinPlateRecordingOptions options;
    options.sampling = {
        .sampleWidth = 128,
        .sampleHeight = 128,
        .refractiveIndex = 1.0,
        .extentWidthMetres = 1e-3,
        .extentHeightMetres = 1e-3,
    };
    options.relativeIntensityReferenceWattsPerSquareMetre = 1e5;
    return options;
}

} // namespace

TEST_CASE("placed same-side branches record a resolved thin transmission hologram") {
    const auto bench = recordingBench(false);
    const auto fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    const auto ids = pairIds(fields);
    const auto result = holography::recordThinTransmissionPlate(
        bench, fields, ids.object, ids.reference, resolvedOptions());

    CHECK(result.plateComponentId == "plate");
    CHECK(result.sourceRevision == bench.revision());
    CHECK(result.pair.geometry == holography::PlateRecordingGeometry::Transmission);
    CHECK(result.pair.wavelengthMetres == 532e-9);
    CHECK(result.diagnostics.fringeCarrierSampled);
    CHECK(result.diagnostics.fringeFrequencyXCyclesPerMetre
        == doctest::Approx(-0.02 / 532e-9).epsilon(2e-14));
    CHECK(result.diagnostics.fringeFrequencyYCyclesPerMetre == 0.0);
    CHECK(result.diagnostics.fringePeriodMetres
        == doctest::Approx(532e-9 / 0.02).epsilon(2e-14));
    CHECK(result.diagnostics.objectPowerOnSampledWindowWatts > 0.0);
    CHECK(result.diagnostics.referencePowerOnSampledWindowWatts > 0.0);
    CHECK(result.hologram.diagnostics.maximumRecordedRelativeIntensity
        > result.hologram.diagnostics.minimumRecordedRelativeIntensity);
    CHECK_FALSE(result.isStaleFor(bench));
}

TEST_CASE("thin transmission recording rejects unresolved fringes and reflection geometry") {
    const auto transmission = recordingBench(false);
    const auto transmissionFields = holography::collectPlateIncidentFields(
        transmission, ray::traceDynamicBench(transmission), "plate");
    const auto transmissionIds = pairIds(transmissionFields);
    auto aliased = resolvedOptions();
    aliased.sampling.sampleWidth = 32;
    aliased.sampling.sampleHeight = 32;
    aliased.sampling.extentWidthMetres = 0.0;
    aliased.sampling.extentHeightMetres = 0.0;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::recordThinTransmissionPlate(
            transmission,
            transmissionFields,
            transmissionIds.object,
            transmissionIds.reference,
            aliased)),
        "thin-plate sampling does not resolve the object/reference fringe carrier",
        std::invalid_argument);

    const auto reflection = recordingBench(true);
    const auto reflectionFields = holography::collectPlateIncidentFields(
        reflection, ray::traceDynamicBench(reflection), "plate");
    const auto reflectionIds = pairIds(reflectionFields);
    CHECK_THROWS_WITH_AS(
        static_cast<void>(holography::recordThinTransmissionPlate(
            reflection,
            reflectionFields,
            reflectionIds.object,
            reflectionIds.reference,
            resolvedOptions())),
        "thin-amplitude plate recording requires same-side transmission geometry",
        std::invalid_argument);
}

TEST_CASE("thin plate recording is deterministic and becomes stale after a bench edit") {
    auto bench = recordingBench(false);
    const auto fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    const auto ids = pairIds(fields);
    const auto first = holography::recordThinTransmissionPlate(
        bench, fields, ids.object, ids.reference, resolvedOptions());
    const auto second = holography::recordThinTransmissionPlate(
        bench, fields, ids.object, ids.reference, resolvedOptions());
    CHECK(std::equal(
        first.hologram.recordedRelativeIntensity.samples().begin(),
        first.hologram.recordedRelativeIntensity.samples().end(),
        second.hologram.recordedRelativeIntensity.samples().begin(),
        second.hologram.recordedRelativeIntensity.samples().end()));
    CHECK(std::equal(
        first.hologram.amplitudeTransmission.samples().begin(),
        first.hologram.amplitudeTransmission.samples().end(),
        second.hologram.amplitudeTransmission.samples().begin(),
        second.hologram.amplitudeTransmission.samples().end()));

    auto plate = *bench.find("plate");
    plate.transform.translationMetres.y = 1e-4;
    bench.replace(plate.id, plate);
    CHECK(first.isStaleFor(bench));
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordThinTransmissionPlate(
            bench, fields, ids.object, ids.reference, resolvedOptions())),
        std::invalid_argument);
}
