#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <numbers>

#include "app/BenchHolographyPresets.hpp"
#include "app/HologramObservationWorker.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/RecordedHologram.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace h = holobench::optics::holography;

TEST_CASE("single beam Denisyuk inherits illumination and records a detached reconstructable plate") {
    namespace scene = holobench::optics::scene;
    auto project = holobench::app::makeSingleBeamDenisyukPreset();
    const auto restored = holobench::app::parseBenchProject(holobench::app::serializeBenchProject(project));
    CHECK(std::get<scene::ObjectWavefrontSourceParameters>(restored.scene.find("object-green")->parameters).requiresIllumination);
    const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
    const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
    REQUIRE(fields.branches.size() == 2);
    const auto object = std::find_if(fields.branches.begin(), fields.branches.end(),
        [](const auto& b) { return b.role == h::RecordingBranchRole::Object; });
    const auto reference = std::find_if(fields.branches.begin(), fields.branches.end(),
        [](const auto& b) { return b.role == h::RecordingBranchRole::Reference; });
    REQUIRE(object != fields.branches.end()); REQUIRE(reference != fields.branches.end());
    CHECK(object->beam.coherenceId == reference->beam.coherenceId);
    CHECK(object->beam.powerWatts == doctest::Approx(reference->beam.powerWatts * 0.9 * 0.5));
    CHECK(object->beam.provenance.parentBranchId == reference->beam.provenance.branchId);
    CHECK(object->beam.accumulatedOpticalPathMetres - reference->beam.accumulatedOpticalPathMetres == doctest::Approx(0.06));
    holobench::compute::fft::CpuFftBackend fft;
    const auto sampling = h::reconstructionSampling(project.scene, fields, object->beam.provenance.branchId,
        reference->beam.provenance.branchId, {.sampleWidth = 256, .sampleHeight = 256});
    const auto record = h::recordVolumePlate(project.scene, fields, object->beam.provenance.branchId,
        reference->beam.provenance.branchId, {}, sampling, fft);
    const auto asset = h::freezeReflectionRecording(project.scene, record);
    CHECK(asset.suggestedFocusDepthMetres == doctest::Approx(0.03));
    CHECK(h::defaultHologramView(asset).focusDistanceMetres == doctest::Approx(0.13));
    CHECK(h::observeRecordedHologram(asset, h::defaultHologramView(asset), fft).pupilPowerWatts > 0);
    auto independent = project;
    auto prescribed = *independent.scene.find("object-green");
    auto pp = std::get<scene::ObjectWavefrontSourceParameters>(prescribed.parameters);
    pp.requiresIllumination = false;
    pp.channel.powerWatts = object->beam.powerWatts;
    prescribed.parameters = pp;
    independent.scene.replace(prescribed.id, prescribed);
    const auto independentFields = h::collectPlateIncidentFields(independent.scene,
        holobench::optics::ray::traceDynamicBench(independent.scene), "plate-h1");
    const auto independentObject = std::find_if(independentFields.branches.begin(), independentFields.branches.end(),
        [](const auto& b) { return b.role == h::RecordingBranchRole::Object; });
    REQUIRE(independentObject != independentFields.branches.end());
    const auto independentWave = h::samplePlateIncidentField(independent.scene, independentFields,
        independentObject->beam.provenance.branchId, sampling, fft);
    // The illumination arm is 180 mm in air. The child wave inherits this
    // phase once, in addition to the identical 30 mm return propagation.
    const auto incomingPhase = std::polar(1.0, 2.0 * std::numbers::pi * 0.18 / 532e-9);
    double phaseError = 0.0, phaseReference = 0.0;
    for (std::size_t i = 0; i < independentWave.field.sampleCount(); ++i) {
        phaseError += std::norm(record.objectIncident->field.samples()[i]
            - independentWave.field.samples()[i] * incomingPhase);
        phaseReference += std::norm(independentWave.field.samples()[i]);
    }
    CHECK(phaseError / phaseReference < 1e-16);
    auto blocker = scene::makeDefaultBenchComponent(scene::BenchComponentKind::ScreenDetector, "illumination-blocker");
    blocker.transform.translationMetres.z = 0.015;
    project.scene.add(blocker);
    const auto blocked = h::collectPlateIncidentFields(project.scene,
        holobench::optics::ray::traceDynamicBench(project.scene), "plate-h1");
    REQUIRE(blocked.branches.size() == 1);
    CHECK(blocked.branches.front().role == h::RecordingBranchRole::Reference);
}
namespace {
h::RecordedHologram sphericalRecording(double depth = 0.04, double pointX = 0.0) {
    constexpr double wavelength = 532e-9;
    const double k = 2.0 * std::numbers::pi / wavelength;
    holobench::field::ComplexField2D f(128, 128, 10e-6, 10e-6, wavelength);
    for (std::size_t y = 0; y < f.height(); ++y)
        for (std::size_t x = 0; x < f.width(); ++x) {
            const double r2 = f.xCoordinateMetres(x) * f.xCoordinateMetres(x) +
                              f.yCoordinateMetres(y) * f.yCoordinateMetres(y);
            const double phaseRadius = r2 - 2.0 * f.xCoordinateMetres(x) * pointX + pointX * pointX;
            f.at(x, y) =
                std::polar(std::exp(-r2 / (0.00025 * 0.00025)), k * phaseRadius / (2.0 * depth));
        }
    const double norm = std::sqrt(holobench::field::computeIntegratedIntensity(f));
    for (auto &s : f.samples())
        s /= norm;
    h::VolumeHologramParameters material;
    material.recordingVacuumWavelengthMetres = material.replayVacuumWavelengthMetres = wavelength;
    return {"analytic-spherical-wave",
            material,
            {0, 0, 2 * k * material.averageRefractiveIndex},
            {0, 0, -1},
            {0, 0, 1},
            0,
            0,
            std::move(f)};
}
double peak(const holobench::field::ComplexField2D &f) {
    double result = 0;
    for (const auto &s : f.samples())
        result = std::max(result, std::norm(s));
    return result;
}
} // namespace

TEST_CASE("showroom spherical wave focuses at the independent Gaussian lens conjugate") {
    auto asset = sphericalRecording();
    auto view = h::defaultHologramView(asset);
    view.focusDistanceMetres = view.eyePosition.z + 0.04;
    view.paddingFactor = 2;
    holobench::compute::fft::CpuFftBackend fft;
    const auto focused = h::observeRecordedHologram(asset, view, fft);
    CHECK(focused.pupilPowerWatts > 0.0);
    CHECK(focused.pupilPowerWatts <= focused.exitPowerWatts * 1.01);
    CHECK(holobench::field::computeIntegratedIntensity(focused.sensorField) ==
          doctest::Approx(focused.pupilPowerWatts).epsilon(1e-10));
    // Analytic spherical wave from z=-40 mm is in focus at eye distance+40 mm.
    view.focusDistanceMetres = 0.025;
    const auto defocused = h::observeRecordedHologram(asset, view, fft);
    CHECK(peak(focused.sensorField) > peak(defocused.sensorField) * 2.0);
    const auto &f = focused.sensorField;
    CHECK(std::norm(f.at(f.width() / 2, f.height() / 2)) > peak(f) * 0.9);
}

TEST_CASE("showroom light power and Bragg detuning survive locked intensity scaling") {
    const auto asset = sphericalRecording();
    auto view = h::defaultHologramView(asset);
    holobench::compute::fft::CpuFftBackend fft;
    const auto nominal = h::observeRecordedHologram(asset, view, fft);
    view.irradianceWattsPerSquareMetre = 0.25;
    const auto dim = h::observeRecordedHologram(asset, view, fft);
    CHECK(dim.pupilPowerWatts == doctest::Approx(nominal.pupilPowerWatts * 0.25).epsilon(1e-10));
    view.irradianceWattsPerSquareMetre = 0;
    const auto dark = h::observeRecordedHologram(asset, view, fft);
    CHECK(peak(dark.sensorField) == 0.0);
    view.irradianceWattsPerSquareMetre = 1;
    view.wavelengthMetres = 560e-9;
    const auto detuned = h::observeRecordedHologram(asset, view, fft);
    CHECK(detuned.efficiency < nominal.efficiency * 0.1);
}

TEST_CASE("showroom recording round trip needs no scene or source images and rejects corruption") {
    const auto asset = sphericalRecording();
    const auto path = std::filesystem::temp_directory_path() / "holobench-showroom-test.holo.json";
    h::saveRecordedHologram(asset, path);
    const auto restored = h::loadRecordedHologram(path);
    REQUIRE(restored.coupling.sampleCount() == asset.coupling.sampleCount());
    for (std::size_t i = 0; i < asset.coupling.sampleCount(); ++i)
        CHECK(restored.coupling.samples()[i] == asset.coupling.samples()[i]);
    holobench::compute::fft::CpuFftBackend fft;
    CHECK(h::observeRecordedHologram(restored, h::defaultHologramView(restored), fft)
              .pupilPowerWatts > 0);
    nlohmann::json json;
    {
        std::ifstream file(path);
        file >> json;
    }
    json["payload"]["samples"][0][0] = 42.0;
    {
        std::ofstream file(path);
        file << json;
    }
    CHECK_THROWS(static_cast<void>(h::loadRecordedHologram(path)));
    std::filesystem::remove(path);
}

TEST_CASE("showroom multi-channel file retains complex recordings and accepts legacy single files") {
    auto red = sphericalRecording();
    // Independent red grid with the same envelope, preserving model metadata.
    constexpr double wavelength = 638e-9;
    holobench::field::ComplexField2D f(red.coupling.width(), red.coupling.height(),
        red.coupling.pitchXMetres(), red.coupling.pitchYMetres(), wavelength);
    std::copy(red.coupling.samples().begin(), red.coupling.samples().end(), f.samples().begin());
    red.coupling = std::move(f);
    red.material.recordingVacuumWavelengthMetres = red.material.replayVacuumWavelengthMetres = wavelength;
    red.gratingVector.z = 4.0 * std::numbers::pi * red.material.averageRefractiveIndex / wavelength;
    const std::vector channels{red, sphericalRecording()};
    const auto path = std::filesystem::temp_directory_path() / "holobench-channel-set-test.holo.json";
    h::saveRecordedHolograms(channels, path);
    const auto loaded = h::loadRecordedHolograms(path);
    REQUIRE(loaded.size() == 2);
    CHECK(loaded[0].material.recordingVacuumWavelengthMetres == wavelength);
    for (std::size_t c = 0; c < loaded.size(); ++c)
        for (std::size_t i = 0; i < loaded[c].coupling.sampleCount(); ++i)
            CHECK(loaded[c].coupling.samples()[i] == channels[c].coupling.samples()[i]);
    h::saveRecordedHologram(channels[1], path);
    REQUIRE(h::loadRecordedHolograms(path).size() == 1);
    std::filesystem::remove(path);
}

TEST_CASE(
    "showroom freezes actual placed reflection evidence and rejects stale or undersampled data") {
    auto project = holobench::app::makeReflectionHolographyPreset();
    const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
    const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
    std::uint64_t object = 0, reference = 0;
    for (const auto &branch : fields.branches) {
        if (branch.role == h::RecordingBranchRole::Object)
            object = branch.beam.provenance.branchId;
        else
            reference = branch.beam.provenance.branchId;
    }
    holobench::compute::fft::CpuFftBackend fft;
    h::PlateFieldSamplingOptions sampling;
    sampling.extentWidthMetres = sampling.extentHeightMetres = 0.0002;
    auto recording =
        h::recordVolumePlate(project.scene, fields, object, reference, {}, sampling, fft);
    const auto frozen = h::freezeReflectionRecording(project.scene, recording);
    CHECK(frozen.sourcePlateId == "plate-h1");
    recording.objectIncident->diagnostics.carrierSampled = false;
    CHECK_THROWS(static_cast<void>(h::freezeReflectionRecording(project.scene, recording)));
    recording.objectIncident->diagnostics.carrierSampled = true;
    auto plate = *project.scene.find("plate-h1");
    plate.transform.translationMetres.x += 0.001;
    project.scene.replace(plate.id, plate);
    CHECK_THROWS(static_cast<void>(h::freezeReflectionRecording(project.scene, recording)));
    CHECK_NOTHROW(h::validateRecordedHologram(frozen));
}

TEST_CASE("showroom latest request wins and cancellation discards completed results") {
    holobench::app::HologramObservationWorker worker;
    const auto asset = std::make_shared<const h::RecordedHologram>(sphericalRecording());
    auto view = h::defaultHologramView(*asset);
    worker.submit(1, asset, view);
    view.irradianceWattsPerSquareMetre = 0;
    worker.submit(2, asset, view);
    worker.wait();
    auto result = worker.poll();
    REQUIRE(result);
    CHECK(result->requestId == 2);
    REQUIRE(result->error.empty());
    REQUIRE(result->result);
    CHECK(result->stage == 2);
    CHECK(peak(result->result->sensorField) == 0);
    worker.submit(3, asset, view);
    worker.cancel();
    worker.wait();
    CHECK_FALSE(worker.poll());
}

TEST_CASE("showroom eye translation produces the independent finite-depth parallax") {
    const auto asset = sphericalRecording(0.04, 0.0001);
    auto view = h::defaultHologramView(asset);
    view.focusDistanceMetres = 0.14;
    view.paddingFactor = 2;
    holobench::compute::fft::CpuFftBackend fft;
    const auto centroid = [](const holobench::field::ComplexField2D &f) {
        double power = 0, moment = 0;
        for (std::size_t y = 0; y < f.height(); ++y)
            for (std::size_t x = 0; x < f.width(); ++x) {
                const double intensity = std::norm(f.at(x, y));
                power += intensity;
                moment += intensity * f.xCoordinateMetres(x);
            }
        return moment / power;
    };
    const auto first = h::observeRecordedHologram(asset, view, fft);
    const double expected = -view.sensorDistanceMetres * 0.0001 / 0.14;
    CHECK(centroid(first.sensorField) == doctest::Approx(expected).epsilon(0.08));
    view.eyePosition.x = 0.00005;
    const auto shifted = h::observeRecordedHologram(asset, view, fft);
    const double parallax = view.sensorDistanceMetres * 0.00005 / 0.14;
    CHECK(centroid(shifted.sensorField) - centroid(first.sensorField) ==
          doctest::Approx(parallax).epsilon(0.08));
    view.eyePosition.x = 0.01;
    CHECK_THROWS(static_cast<void>(h::observeRecordedHologram(asset, view, fft)));
}
