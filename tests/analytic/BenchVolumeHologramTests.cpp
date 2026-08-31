#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

#include "app/BenchHolographyPresets.hpp"
#include "optics/holography/BenchVolumeHologram.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;

namespace {

struct RecordingInput final {
    holobench::app::BenchProject project;
    holography::PlateIncidentFieldSet fields;
    std::uint64_t objectBranchId = 0;
    std::uint64_t referenceBranchId = 0;
};

RecordingInput inputFor(holobench::app::BenchProject project) {
    const auto trace = ray::traceDynamicBench(project.scene);
    auto fields = holography::collectPlateIncidentFields(
        project.scene, trace, "plate-h1");
    std::uint64_t object = 0;
    std::uint64_t reference = 0;
    for (const auto& branch : fields.branches) {
        if (branch.role == holography::RecordingBranchRole::Object) {
            object = branch.beam.provenance.branchId;
        } else {
            reference = branch.beam.provenance.branchId;
        }
    }
    return {
        .project = std::move(project),
        .fields = std::move(fields),
        .objectBranchId = object,
        .referenceBranchId = reference,
    };
}

} // namespace

TEST_CASE("placed opposite-side branches derive a reflection grating vector and period") {
    const auto input = inputFor(
        holobench::app::makeReflectionHolographyPreset());
    const holography::VolumePlateMaterial material {
        .averageRefractiveIndex = 1.52,
        .refractiveIndexModulation = 0.008,
        .isotropicLinearShrinkageFraction = 0.0,
    };
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId,
        material);

    CHECK(recording.pair.geometry
        == holography::PlateRecordingGeometry::Reflection);
    const auto expectedVector = (
        recording.objectDirectionInMediumLocal
        - recording.referenceDirectionInMediumLocal)
        * (2.0 * std::numbers::pi * material.averageRefractiveIndex
            / recording.pair.wavelengthMetres);
    CHECK(recording.recordedGratingVectorLocalRadiansPerMetre
        == expectedVector);
    CHECK(recording.recordedGratingPeriodMetres
        == doctest::Approx(
            2.0 * std::numbers::pi
            / holobench::math::length(expectedVector)).epsilon(2e-15));
    CHECK(recording.nominalReplay.recordedGratingPeriodMetres
        == doctest::Approx(recording.recordedGratingPeriodMetres)
            .epsilon(2e-15));
    CHECK(recording.gratingSlantFromPlateNormalRadians > 0.0);
    CHECK(recording.nominalReplay.kogelnikEfficiencyEvaluated);
    CHECK(recording.nominalReplay.kogelnik.diffractionEfficiency > 0.0);
    CHECK_FALSE(recording.isStaleFor(input.project.scene));
}

TEST_CASE("volume replay exposes wavelength detuning and material shrinkage") {
    const auto input = inputFor(
        holobench::app::makeReflectionHolographyPreset());
    const holography::VolumePlateMaterial material {
        .averageRefractiveIndex = 1.5,
        .refractiveIndexModulation = 0.01,
        .isotropicLinearShrinkageFraction = 0.03,
    };
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId,
        material);
    const auto shifted = holography::replayVolumePlate(
        input.project.scene,
        recording,
        633e-9,
        recording.equivalentSymmetricBraggAngleInMediumRadians);

    CHECK(recording.nominalReplay.replayThicknessMetres
        == doctest::Approx(
            recording.nominalReplayParameters.recordedThicknessMetres * 0.97));
    CHECK(std::abs(shifted.volume.kogelnik.detuningParameter) > 0.1);
    CHECK(shifted.replayVacuumWavelengthMetres == 633e-9);
    CHECK_FALSE(shifted.isStaleFor(input.project.scene));
}

TEST_CASE("same-side preset derives a transmission volume grating") {
    const auto input = inputFor(
        holobench::app::makeTransmissionHolographyPreset());
    const auto recording = holography::recordVolumePlate(
        input.project.scene,
        input.fields,
        input.objectBranchId,
        input.referenceBranchId);

    CHECK(recording.pair.geometry
        == holography::PlateRecordingGeometry::Transmission);
    CHECK(recording.nominalReplayParameters.geometry
        == holography::VolumeHologramGeometry::Transmission);
    CHECK(recording.equivalentSymmetricBraggAngleInMediumRadians > 0.0);
    CHECK(recording.nominalReplay.diffractedOrderPropagating);
}

TEST_CASE("volume plate recording rejects stale evidence and invalid material") {
    auto input = inputFor(holobench::app::makeReflectionHolographyPreset());
    auto plate = *input.project.scene.find("plate-h1");
    plate.transform.translationMetres.x = 1e-4;
    input.project.scene.replace(plate.id, plate);
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordVolumePlate(
            input.project.scene,
            input.fields,
            input.objectBranchId,
            input.referenceBranchId)),
        std::invalid_argument);

    input = inputFor(holobench::app::makeReflectionHolographyPreset());
    CHECK_THROWS_AS(
        static_cast<void>(holography::recordVolumePlate(
            input.project.scene,
            input.fields,
            input.objectBranchId,
            input.referenceBranchId,
            {.averageRefractiveIndex = 1.5,
             .refractiveIndexModulation = 1.5,
             .isotropicLinearShrinkageFraction = 0.0})),
        std::invalid_argument);
}
