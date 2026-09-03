#include <doctest/doctest.h>

#include <algorithm>
#include <cmath>
#include <set>
#include <string>

#include <nlohmann/json.hpp>

#include "app/BenchRecordingRecipe.hpp"
#include "app/ChimeraRecipe.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "optics/holography/BenchVolumeHologramReplay.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace app = holobench::app;
namespace chimera = holobench::app::chimera;
namespace holography = holobench::optics::holography;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;

TEST_CASE("canonical CHIMERA recipe round trips byte-stably") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const std::string encoded = chimera::serializeChimeraRecipe(recipe);
    const auto restored = chimera::parseChimeraRecipe(encoded);
    CHECK(restored == recipe);
    CHECK(chimera::serializeChimeraRecipe(restored) == encoded);
}

TEST_CASE("canonical CHIMERA recipe compiles to a feasible ordinary editable bench") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto first = chimera::compileChimeraRecipe(recipe);
    const auto second = chimera::compileChimeraRecipe(recipe);

    CHECK(first.feasible());
    CHECK(first.project.projectId == "chimera-canonical-chimera");
    CHECK(first.project.recordingRecipes.size() == 3U);
    CHECK(first.generatedComponents.size() == 13U);
    CHECK(app::serializeBenchProject(first.project)
        == app::serializeBenchProject(second.project));
    CHECK(first.generatedComponents == second.generatedComponents);
    CHECK(first.constraints == second.constraints);

    std::set<std::string> componentIds;
    for (const auto& provenance : first.generatedComponents) {
        CHECK(provenance.recipeId == recipe.recipeId);
        CHECK(provenance.recipeVersion == recipe.formatVersion);
        CHECK(provenance.compilerVersion
            == chimera::kChimeraRecipeCompilerVersion);
        CHECK(componentIds.insert(provenance.componentId).second);
        CHECK(first.project.scene.find(provenance.componentId) != nullptr);
    }
    CHECK(first.project.scene.find("chimera-slm") != nullptr);
    CHECK(first.project.scene.find("chimera-reference-mirror") != nullptr);
    CHECK(first.project.scene.find("chimera-reference-splitter") != nullptr);
    CHECK(first.project.scene.find("chimera-xcube-combiner") != nullptr);
    CHECK(first.project.scene.find("chimera-plate") != nullptr);
    CHECK(first.project.scene.find("chimera-reconstruction-probe") != nullptr);
    const auto* cameraLens
        = first.project.scene.find("chimera-camera-lens");
    REQUIRE(cameraLens != nullptr);
    CHECK(cameraLens->kind == scene::BenchComponentKind::RealLensAssembly);
    CHECK(std::get<scene::RealLensAssemblyParameters>(cameraLens->parameters)
            .prescriptionId
        == "default_n_bk7_biconvex");

    const auto* slm = first.project.scene.find("chimera-slm");
    REQUIRE(slm != nullptr);
    const auto& slmParameters
        = std::get<scene::SpatialLightModulatorParameters>(slm->parameters);
    CHECK(slmParameters.commandOrigin == scene::SlmCommandOrigin::Automation);
    CHECK(slmParameters.commandId
        == "chimera-canonical-chimera-hogel-pending");
    const auto* relayLens = first.project.scene.find("chimera-relay-lens");
    REQUIRE(relayLens != nullptr);
    CHECK(holobench::math::length(
        slm->transform.translationMetres
            - relayLens->transform.translationMetres)
        == doctest::Approx(recipe.relay.focalLengthMetres).epsilon(1e-13));

    const auto* laserSource = first.project.scene.find("chimera-laser-source-red");
    REQUIRE(laserSource != nullptr);
    CHECK(laserSource->kind == scene::BenchComponentKind::LaserSource);
    const auto* foldMirror = first.project.scene.find("chimera-reference-fold-mirror");
    REQUIRE(foldMirror != nullptr);
    CHECK(foldMirror->kind == scene::BenchComponentKind::PlanarMirror);
    CHECK(first.project.scene.find("chimera-object-source-red") == nullptr);
}

TEST_CASE("compiled CHIMERA branches resolve three independent reflection recipes") {
    const auto compiled
        = chimera::compileChimeraRecipe(chimera::makeCanonicalChimeraRecipe());
    const auto trace = ray::traceDynamicBench(compiled.project.scene);
    const auto fields = holography::collectPlateIncidentFields(
        compiled.project.scene, trace, "chimera-plate");
    REQUIRE(fields.branches.size() == 6U);
    holobench::compute::fft::CpuFftBackend fft;
    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    auto previewSampling = compiled.project.recordingRecipes.front().sampling;
    previewSampling.sampleWidth = 128U;
    previewSampling.sampleHeight = 128U;
    previewSampling.extentWidthMetres = 0.1e-3;
    previewSampling.extentHeightMetres = 0.1e-3;

    std::set<double> wavelengths;
    for (const auto& recipe : compiled.project.recordingRecipes) {
        const auto resolved = app::resolveRecordingRecipe(fields, recipe);
        REQUIRE(resolved.channels.size() == 1U);
        const auto pair = holography::makePlateRecordingPair(
            fields,
            resolved.channels.front().objectBranchId,
            resolved.channels.front().referenceBranchId);
        CHECK(pair.geometry == holography::PlateRecordingGeometry::Reflection);
        wavelengths.insert(pair.wavelengthMetres);
        const auto recording = holography::recordVolumePlate(
            compiled.project.scene,
            fields,
            resolved.channels.front().objectBranchId,
            resolved.channels.front().referenceBranchId,
            recipe.volumeMaterial);
        const auto replay = holography::replayVolumeReflectionToObservation(
            compiled.project.scene,
            fields,
            recording,
            resolved.channels.front().referenceBranchId,
            "chimera-reconstruction-probe",
            previewSampling,
            fft,
            &prescriptions);
        CHECK(replay.braggReplay.volume.kogelnik.diffractionEfficiency > 0.0);
        CHECK(replay.reconstructedPowerOnSampledWindowWatts > 0.0);
        CHECK_FALSE(replay.isStaleFor(compiled.project.scene));
    }
    CHECK(wavelengths == std::set<double> {450e-9, 532e-9, 638e-9});
}

TEST_CASE("CHIMERA compiler reports impossible FOV and stop constraints") {
    auto recipe = chimera::makeCanonicalChimeraRecipe();
    recipe.targetHorizontalFieldOfViewRadians = 2.0;
    recipe.relay.stopDiameterMetres = 1e-3;
    const auto compiled = chimera::compileChimeraRecipe(recipe);
    CHECK_FALSE(compiled.feasible());
    CHECK(std::any_of(
        compiled.constraints.begin(),
        compiled.constraints.end(),
        [](const auto& entry) {
            return entry.severity == chimera::ConstraintSeverity::Unsupported
                && entry.code == "horizontal_fov";
        }));
    CHECK(std::any_of(
        compiled.constraints.begin(),
        compiled.constraints.end(),
        [](const auto& entry) {
            return entry.severity == chimera::ConstraintSeverity::Unsupported
                && entry.code == "relay_stop_na";
        }));
}

TEST_CASE("CHIMERA parser rejects unknown schema and invalid RGB identity") {
    auto json = nlohmann::json::parse(chimera::serializeChimeraRecipe(
        chimera::makeCanonicalChimeraRecipe()));
    auto unknown = json;
    unknown["hidden_solver_graph"] = true;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseChimeraRecipe(unknown.dump())),
        std::runtime_error);

    auto reordered = json;
    reordered["rgb"][0]["channel_id"] = "blue";
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseChimeraRecipe(reordered.dump())),
        std::runtime_error);

    auto invalidPixels = json;
    invalidPixels["slm"]["pixel_width"] = 0;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::parseChimeraRecipe(invalidPixels.dump())),
        std::runtime_error);
}

TEST_CASE("CHIMERA supports XCube, CascadedDichroic, and IntegratedMultiLine beam combining") {
    const std::vector<chimera::BeamCombinationMethod> methods = {
        chimera::BeamCombinationMethod::XCube,
        chimera::BeamCombinationMethod::CascadedDichroic,
        chimera::BeamCombinationMethod::IntegratedMultiLine,
    };

    for (const auto method : methods) {
        auto recipe = chimera::makeCanonicalChimeraRecipe();
        recipe.beamCombinationMethod = method;

        // Verify JSON round-trip preserves method
        const auto serialized = chimera::serializeChimeraRecipe(recipe);
        const auto parsed = chimera::parseChimeraRecipe(serialized);
        CHECK(parsed.beamCombinationMethod == method);

        const auto compiled = chimera::compileChimeraRecipe(recipe);
        CHECK(compiled.feasible());
        CHECK(compiled.project.recordingRecipes.size() == 3U);

        // Verify specific combiner components exist
        if (method == chimera::BeamCombinationMethod::XCube) {
            CHECK(compiled.project.scene.find("chimera-xcube-combiner") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-red") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-green") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-blue") != nullptr);
        } else if (method == chimera::BeamCombinationMethod::CascadedDichroic) {
            CHECK(compiled.project.scene.find("chimera-dichroic-combiner-red") != nullptr);
            CHECK(compiled.project.scene.find("chimera-dichroic-combiner-blue") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-red") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-green") != nullptr);
            CHECK(compiled.project.scene.find("chimera-laser-source-blue") != nullptr);
        } else {
            CHECK(compiled.project.scene.find("chimera-laser-source") != nullptr);
        }

        // Trace bench and verify plate fields
        const auto trace = ray::traceDynamicBench(compiled.project.scene);
        const auto fields = holography::collectPlateIncidentFields(
            compiled.project.scene, trace, "chimera-plate");
        REQUIRE(fields.branches.size() == 6U);

        // Verify that all 3 channels have matching object and reference paths
        for (const auto& recRecipe : compiled.project.recordingRecipes) {
            const auto resolved = app::resolveRecordingRecipe(fields, recRecipe);
            REQUIRE(resolved.channels.size() == 1U);
            const auto pair = holography::makePlateRecordingPair(
                fields,
                resolved.channels.front().objectBranchId,
                resolved.channels.front().referenceBranchId);
            CHECK(pair.geometry == holography::PlateRecordingGeometry::Reflection);
            CHECK(std::isfinite(pair.signedOpticalPathDifferenceMetres));
        }

        // Verify collinearity: all object branches hitting the plate must share the same horizontal Y plane (Y = 0)
        for (const auto& branch : fields.branches) {
            CHECK(branch.beam.originMetres.y == doctest::Approx(0.0).epsilon(1e-12));
            CHECK(branch.beam.direction.y == doctest::Approx(0.0).epsilon(1e-12));
        }
    }
}

