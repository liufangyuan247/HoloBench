#include <doctest/doctest.h>

#include <algorithm>
#include <numbers>
#include <stdexcept>
#include <utility>

#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holography = holobench::optics::holography;
namespace scene = holobench::optics::scene;
namespace ray = holobench::optics::ray;

namespace {

scene::BenchComponent source(
    scene::BenchComponentKind kind,
    const char* id,
    double zMetres,
    bool towardsPositiveZ,
    double wavelengthMetres = 532e-9,
    const char* coherenceId = "recording") {
    auto result = scene::makeDefaultBenchComponent(kind, id);
    result.transform.translationMetres = {0.0, 0.0, zMetres};
    if (!towardsPositiveZ) {
        result.transform.localXAxisInWorld = {-1.0, 0.0, 0.0};
        result.transform.localZAxisInWorld = {0.0, 0.0, -1.0};
    }
    if (kind == scene::BenchComponentKind::LaserSource) {
        auto parameters = std::get<scene::LaserSourceParameters>(result.parameters);
        parameters.channels = {{
            .wavelengthMetres = wavelengthMetres,
            .powerWatts = 0.5,
            .coherenceId = coherenceId,
        }};
        result.parameters = parameters;
    } else {
        auto parameters
            = std::get<scene::ObjectWavefrontSourceParameters>(result.parameters);
        parameters.channel = {
            .wavelengthMetres = wavelengthMetres,
            .powerWatts = 0.25,
            .coherenceId = coherenceId,
        };
        result.parameters = parameters;
    }
    return result;
}

scene::BenchScene recordingBench(bool oppositeSides) {
    scene::BenchScene bench;
    bench.add(source(
        scene::BenchComponentKind::LaserSource,
        "reference", -1.0, true));
    bench.add(source(
        scene::BenchComponentKind::ObjectWavefrontSource,
        "object", oppositeSides ? 0.5 : -0.5, !oppositeSides));
    bench.add(scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::HolographicPlate, "plate"));
    return bench;
}

const holography::PlateIncidentBranch& branchWithRole(
    const holography::PlateIncidentFieldSet& fields,
    holography::RecordingBranchRole role) {
    const auto found = std::find_if(
        fields.branches.begin(), fields.branches.end(),
        [role](const auto& branch) { return branch.role == role; });
    if (found == fields.branches.end()) {
        throw std::logic_error("expected recording role was not found");
    }
    return *found;
}

} // namespace

TEST_CASE("same-side plate branches form a coherent transmission recording pair") {
    const auto bench = recordingBench(false);
    const auto fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    REQUIRE(fields.branches.size() == 2);
    CHECK_FALSE(fields.isStaleFor(bench));
    const auto& object = branchWithRole(
        fields, holography::RecordingBranchRole::Object);
    const auto& reference = branchWithRole(
        fields, holography::RecordingBranchRole::Reference);
    CHECK(object.side == holography::PlateIncidenceSide::NegativeLocalZ);
    CHECK(reference.side == holography::PlateIncidenceSide::NegativeLocalZ);
    CHECK(object.incidenceAngleRadians == doctest::Approx(0.0));
    CHECK(reference.incidenceAngleRadians == doctest::Approx(0.0));
    REQUIRE(object.pathInteractions.size() == 1U);
    CHECK(object.pathInteractions.front().componentId == "plate");
    CHECK_FALSE(object.pathInteractions.front().hasOutgoingBeam);

    const auto pair = holography::makePlateRecordingPair(
        fields,
        object.beam.provenance.branchId,
        reference.beam.provenance.branchId);
    CHECK(pair.geometry == holography::PlateRecordingGeometry::Transmission);
    CHECK(pair.wavelengthMetres == 532e-9);
    CHECK(pair.signedOpticalPathDifferenceMetres
        == doctest::Approx(-0.5).epsilon(1e-14));
    CHECK(pair.crossingAngleRadians == doctest::Approx(0.0));
}

TEST_CASE("opposite-side branches form a reflection recording pair") {
    const auto bench = recordingBench(true);
    const auto fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    const auto& object = branchWithRole(
        fields, holography::RecordingBranchRole::Object);
    const auto& reference = branchWithRole(
        fields, holography::RecordingBranchRole::Reference);
    CHECK(object.side == holography::PlateIncidenceSide::PositiveLocalZ);
    CHECK(reference.side == holography::PlateIncidenceSide::NegativeLocalZ);

    const auto pair = holography::makePlateRecordingPair(
        fields,
        object.beam.provenance.branchId,
        reference.beam.provenance.branchId);
    CHECK(pair.geometry == holography::PlateRecordingGeometry::Reflection);
    CHECK(pair.crossingAngleRadians
        == doctest::Approx(std::numbers::pi_v<double>).epsilon(1e-14));
}

TEST_CASE("recording pair rejects cross-wavelength and cross-coherence branches") {
    auto bench = recordingBench(false);
    auto object = *bench.find("object");
    auto parameters
        = std::get<scene::ObjectWavefrontSourceParameters>(object.parameters);
    parameters.channel.wavelengthMetres = 633e-9;
    object.parameters = parameters;
    bench.replace(object.id, object);
    auto fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    const auto& objectBranch = branchWithRole(
        fields, holography::RecordingBranchRole::Object);
    const auto& referenceBranch = branchWithRole(
        fields, holography::RecordingBranchRole::Reference);
    CHECK_THROWS_AS(
        static_cast<void>(holography::makePlateRecordingPair(
            fields,
            objectBranch.beam.provenance.branchId,
            referenceBranch.beam.provenance.branchId)),
        std::invalid_argument);

    parameters.channel.wavelengthMetres = 532e-9;
    parameters.channel.coherenceId = "independent-object";
    object.parameters = parameters;
    bench.replace(object.id, object);
    fields = holography::collectPlateIncidentFields(
        bench, ray::traceDynamicBench(bench), "plate");
    const auto& incoherentObject = branchWithRole(
        fields, holography::RecordingBranchRole::Object);
    const auto& incoherentReference = branchWithRole(
        fields, holography::RecordingBranchRole::Reference);
    CHECK_THROWS_AS(
        static_cast<void>(holography::makePlateRecordingPair(
            fields,
            incoherentObject.beam.provenance.branchId,
            incoherentReference.beam.provenance.branchId)),
        std::invalid_argument);
}

TEST_CASE("plate incident field collection rejects stale and wrong observers") {
    auto bench = recordingBench(false);
    const auto staleTrace = ray::traceDynamicBench(bench);
    auto plate = *bench.find("plate");
    plate.transform.translationMetres.x = 0.001;
    bench.replace(plate.id, plate);
    CHECK_THROWS_AS(
        static_cast<void>(holography::collectPlateIncidentFields(
            bench, staleTrace, "plate")),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(holography::collectPlateIncidentFields(
            bench, ray::traceDynamicBench(bench), "reference")),
        std::invalid_argument);

    const auto originalBench = recordingBench(false);
    const auto fields = holography::collectPlateIncidentFields(
        originalBench, ray::traceDynamicBench(originalBench), "plate");
    CHECK(fields.isStaleFor(bench));
}

TEST_CASE("plate-local recording geometry is invariant under a common rigid transform") {
    const auto original = recordingBench(false);
    const holobench::math::RigidTransform3d commonTransform {
        .translationMetres = {0.4, -0.2, 0.7},
        .localXAxisInWorld = {0.0, 0.0, -1.0},
        .localYAxisInWorld = {0.0, 1.0, 0.0},
        .localZAxisInWorld = {1.0, 0.0, 0.0},
    };
    scene::BenchScene transformed;
    for (auto component : original.components()) {
        component.transform.translationMetres
            = holobench::math::transformPointLocalToWorld(
                commonTransform, component.transform.translationMetres);
        component.transform.localXAxisInWorld
            = holobench::math::transformDirectionLocalToWorld(
                commonTransform, component.transform.localXAxisInWorld);
        component.transform.localYAxisInWorld
            = holobench::math::transformDirectionLocalToWorld(
                commonTransform, component.transform.localYAxisInWorld);
        component.transform.localZAxisInWorld
            = holobench::math::transformDirectionLocalToWorld(
                commonTransform, component.transform.localZAxisInWorld);
        transformed.add(std::move(component));
    }

    const auto first = holography::collectPlateIncidentFields(
        original, ray::traceDynamicBench(original), "plate");
    const auto second = holography::collectPlateIncidentFields(
        transformed, ray::traceDynamicBench(transformed), "plate");
    REQUIRE(first.branches.size() == second.branches.size());
    for (std::size_t index = 0; index < first.branches.size(); ++index) {
        CHECK(first.branches[index].role == second.branches[index].role);
        CHECK(first.branches[index].side == second.branches[index].side);
        CHECK(first.branches[index].localHitPointMetres
            == second.branches[index].localHitPointMetres);
        CHECK(first.branches[index].localDirection
            == second.branches[index].localDirection);
        CHECK(first.branches[index].incidenceAngleRadians
            == second.branches[index].incidenceAngleRadians);
        CHECK(first.branches[index].beam.accumulatedOpticalPathMetres
            == doctest::Approx(
                second.branches[index].beam.accumulatedOpticalPathMetres)
                   .epsilon(1e-14));
    }
}
