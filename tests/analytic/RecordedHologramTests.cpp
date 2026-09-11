#include <doctest/doctest.h>

#include <cmath>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <numbers>

#include "app/BenchHolographyPresets.hpp"
#include "app/HologramObservationWorker.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/field/FieldObservables.hpp"
#include "optics/holography/BenchRgbHologram.hpp"
#include "optics/holography/RecordedHologram.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"
#include "optics/wave/DiffuseObjectWavefront.hpp"

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
    CHECK(object->beam.accumulatedOpticalPathMetres - reference->beam.accumulatedOpticalPathMetres
        == doctest::Approx(0.002 / std::cos(15.0 * std::numbers::pi / 180.0) + 0.002));
    holobench::compute::fft::CpuFftBackend fft;
    h::PlateFieldSamplingOptions requestedSampling;
    requestedSampling.sampleWidth = requestedSampling.sampleHeight = 512;
    requestedSampling.extentWidthMetres = requestedSampling.extentHeightMetres = 0.0015;
    const auto sampling = h::reconstructionSampling(project.scene, fields, object->beam.provenance.branchId,
        reference->beam.provenance.branchId, requestedSampling);
    const auto record = h::recordVolumePlate(project.scene, fields, object->beam.provenance.branchId,
        reference->beam.provenance.branchId, {}, sampling, fft);
    const auto asset = h::freezeReflectionRecording(project.scene, record);
    CHECK(asset.suggestedFocusDepthMetres == doctest::Approx(0.002));
    CHECK(h::defaultHologramView(asset).focusDistanceMetres == doctest::Approx(0.102));
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
    const double illuminationArmMetres = 0.15 + 0.002 / std::cos(15.0 * std::numbers::pi / 180.0);
    const auto incomingPhase = std::polar(1.0, 2.0 * std::numbers::pi * illuminationArmMetres / 532e-9);
    double phaseError = 0.0, phaseReference = 0.0;
    for (std::size_t i = 0; i < independentWave.field.sampleCount(); ++i) {
        phaseError += std::norm(record.objectIncident->field.samples()[i]
            - independentWave.field.samples()[i] * incomingPhase);
        phaseReference += std::norm(independentWave.field.samples()[i]);
    }
    CHECK(phaseError / phaseReference < 1e-16);
    auto blocker = scene::makeDefaultBenchComponent(scene::BenchComponentKind::ScreenDetector, "illumination-blocker");
    blocker.transform.translationMetres.z = 0.001;
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
    const auto view = h::defaultHologramView(frozen);
    CHECK(h::observeRecordedHologram(frozen, view, fft).pupilPowerWatts > 0);
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

TEST_CASE("reproduce denisyuk showroom reconstruction crash scenarios") {
    holobench::compute::fft::CpuFftBackend fft;
    // Scenario A: Single-beam Denisyuk preset with UI parameters (512x512, 2mm)
    {
        auto project = holobench::app::makeSingleBeamDenisyukPreset();
        const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
        const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
        std::uint64_t object = 0, reference = 0;
        for (const auto &b : fields.branches) {
            if (b.role == h::RecordingBranchRole::Object) object = b.beam.provenance.branchId;
            else reference = b.beam.provenance.branchId;
        }
        h::PlateFieldSamplingOptions requestedSampling;
        requestedSampling.sampleWidth = requestedSampling.sampleHeight = 512;
        requestedSampling.extentWidthMetres = requestedSampling.extentHeightMetres = 0.0015;
        const auto sampling = h::reconstructionSampling(project.scene, fields, object, reference, requestedSampling);
        auto record = h::recordVolumePlate(project.scene, fields, object, reference, {}, sampling, fft);
        auto asset = h::freezeReflectionRecording(project.scene, record);
        auto view = h::defaultHologramView(asset);
        auto res = h::observeRecordedHologram(asset, view, fft);
        CHECK(res.pupilPowerWatts > 0);
    }
    // Scenario B: Reflection Denisyuk preset with UI parameters (256x256, 1mm)
    {
        auto project = holobench::app::makeReflectionHolographyPreset();
        const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
        const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
        std::uint64_t object = 0, reference = 0;
        for (const auto &b : fields.branches) {
            if (b.role == h::RecordingBranchRole::Object) object = b.beam.provenance.branchId;
            else reference = b.beam.provenance.branchId;
        }
        h::PlateFieldSamplingOptions sampling;
        sampling.sampleWidth = sampling.sampleHeight = 256;
        sampling.extentWidthMetres = sampling.extentHeightMetres = 0.001;
        auto record = h::recordVolumePlate(project.scene, fields, object, reference, {}, sampling, fft);
        auto asset = h::freezeReflectionRecording(project.scene, record);
        auto view = h::defaultHologramView(asset);
        auto res = h::observeRecordedHologram(asset, view, fft);
        CHECK(res.pupilPowerWatts > 0);
    }
    // Scenario C: Reflection Denisyuk preset with UI parameters (512x512, 2mm)
    {
        auto project = holobench::app::makeReflectionHolographyPreset();
        const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
        const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
        std::uint64_t object = 0, reference = 0;
        for (const auto &b : fields.branches) {
            if (b.role == h::RecordingBranchRole::Object) object = b.beam.provenance.branchId;
            else reference = b.beam.provenance.branchId;
        }
        h::PlateFieldSamplingOptions sampling;
        sampling.sampleWidth = sampling.sampleHeight = 512;
        sampling.extentWidthMetres = sampling.extentHeightMetres = 0.002;
        auto record = h::recordVolumePlate(project.scene, fields, object, reference, {}, sampling, fft);
        auto asset = h::freezeReflectionRecording(project.scene, record);
        auto view = h::defaultHologramView(asset);
        auto res = h::observeRecordedHologram(asset, view, fft);
        CHECK(res.pupilPowerWatts > 0);
        view.paddingFactor = 2;
        auto res2 = h::observeRecordedHologram(asset, view, fft);
        CHECK(res2.pupilPowerWatts > 0);
    }
    // Scenario D: RGB Denisyuk channels with paddingFactor 1 and 2
    {
        auto project = holobench::app::makeRgbDenisyukHolographyPreset();
        const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
        const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
        const auto selections = h::selectRgbReflectionPairs(fields);
        h::PlateFieldSamplingOptions sampling;
        sampling.sampleWidth = sampling.sampleHeight = 256;
        sampling.extentWidthMetres = sampling.extentHeightMetres = 0.002;
        for (const auto &pair : selections) {
            sampling = h::reconstructionSampling(project.scene, fields, pair.objectBranchId, pair.referenceBranchId, sampling);
        }
        auto rgbRecord = h::recordRgbReflectionVolumePlate(project.scene, fields, selections, {}, sampling, fft);
        for (const auto &channel : rgbRecord.channels) {
            auto asset = h::freezeReflectionRecording(project.scene, channel);
            auto view = h::defaultHologramView(asset);
            auto res1 = h::observeRecordedHologram(asset, view, fft);
            CHECK(res1.pupilPowerWatts > 0);
            view.paddingFactor = 2;
            auto res2 = h::observeRecordedHologram(asset, view, fft);
            CHECK(res2.pupilPowerWatts > 0);
        }
    }
    // Scenario E: HologramObservationWorker with rotation
    {
        auto project = holobench::app::makeSingleBeamDenisyukPreset();
        const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
        const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
        std::uint64_t object = 0, reference = 0;
        for (const auto &b : fields.branches) {
            if (b.role == h::RecordingBranchRole::Object) object = b.beam.provenance.branchId;
            else reference = b.beam.provenance.branchId;
        }
        h::PlateFieldSamplingOptions requestedSampling;
        requestedSampling.sampleWidth = requestedSampling.sampleHeight = 256;
        requestedSampling.extentWidthMetres = requestedSampling.extentHeightMetres = 0.0015;
        const auto sampling = h::reconstructionSampling(project.scene, fields, object, reference, requestedSampling);
        auto record = h::recordVolumePlate(project.scene, fields, object, reference, {}, sampling, fft);
        auto asset = std::make_shared<const h::RecordedHologram>(h::freezeReflectionRecording(project.scene, record));
        auto view = h::defaultHologramView(*asset);
        namespace math = holobench::math;
        float yaw = 2.0f * 0.0005f;
        float pitch = 1.0f * 0.0005f;
        const auto p = [](math::Vec3d v, double yaw, double pitch) {
            const math::Vec3d p{v.x, std::cos(pitch) * v.y - std::sin(pitch) * v.z,
                                std::sin(pitch) * v.y + std::cos(pitch) * v.z};
            return math::Vec3d{std::cos(yaw) * p.x + std::sin(yaw) * p.z, p.y,
                    -std::sin(yaw) * p.x + std::cos(yaw) * p.z};
        };
        const auto rawZ = p(view.platePose.localZAxisInWorld, yaw, pitch);
        const auto rawY = p(view.platePose.localYAxisInWorld, yaw, pitch);
        const auto z = math::normalized(rawZ);
        const auto x = math::normalized(math::cross(rawY, z));
        const auto y = math::cross(z, x);
        view.platePose.localXAxisInWorld = x;
        view.platePose.localYAxisInWorld = y;
        view.platePose.localZAxisInWorld = z;
        holobench::app::HologramObservationWorker worker;
        worker.submit(1, asset, view);
        worker.wait();
        auto update = worker.poll();
        REQUIRE(update.has_value());
        if (!update->error.empty()) {
            FAIL(update->error);
        }
        CHECK(worker.progress().fraction == 1.0F);
        CHECK(!worker.progress().busy);

        // Test large angle / backward 180 degree rotation (flipped around)
        yaw = static_cast<float>(std::numbers::pi);
        pitch = 0.5F;
        const auto rawZ_back = p(view.platePose.localZAxisInWorld, yaw, pitch);
        const auto rawY_back = p(view.platePose.localYAxisInWorld, yaw, pitch);
        const auto z_back = math::normalized(rawZ_back);
        const auto x_back = math::normalized(math::cross(rawY_back, z_back));
        const auto y_back = math::cross(z_back, x_back);
        view.platePose.localXAxisInWorld = x_back;
        view.platePose.localYAxisInWorld = y_back;
        view.platePose.localZAxisInWorld = z_back;
        worker.submit(2, asset, view);
        worker.wait();
        auto updateBack = worker.poll();
        REQUIRE(updateBack.has_value());
        CHECK(updateBack->error.empty());
        CHECK(updateBack->result.has_value());
        CHECK(updateBack->result->efficiency == 0.0);
        CHECK(worker.progress().fraction == 1.0F);
        CHECK(!worker.progress().busy);
    }
}

TEST_CASE("diagnose Cornell Box RGB holography reconstruction") {
    auto project = holobench::app::makeRgbDenisyukHolographyPreset();
    const auto trace = holobench::optics::ray::traceDynamicBench(project.scene);
    const auto fields = h::collectPlateIncidentFields(project.scene, trace, "plate-h1");
    const auto selections = h::selectRgbReflectionPairs(fields);
    std::cout << "Selections count: " << selections.size() << std::endl;
    holobench::compute::fft::CpuFftBackend fft;
    {
        const auto obj = *project.scene.find("object-red");
        const auto& p = std::get<holobench::optics::scene::ObjectWavefrontSourceParameters>(obj.parameters);
        int totalHits = 0;
        double minX = 1e9, maxX = -1e9, minY = 1e9, maxY = -1e9;
        for (int iy = -10; iy <= 10; ++iy) {
            for (int ix = -10; ix <= 10; ++ix) {
                double x = ix * 0.002;
                double y = iy * 0.002;
                auto hit = holobench::optics::wave::sampleDiffuseObjectSurface(p, x, y);
                if (hit.has_value()) {
                    totalHits++;
                    minX = std::min(minX, x);
                    maxX = std::max(maxX, x);
                    minY = std::min(minY, y);
                    maxY = std::max(maxY, y);
                }
            }
        }
        std::cout << "Grid sampling total hits: " << totalHits
                  << ", X: [" << minX << ", " << maxX << "], Y: [" << minY << ", " << maxY << "]" << std::endl;
    }
    for (std::size_t i = 0; i < selections.size(); ++i) {
        const auto &sel = selections[i];
        std::cout << "Selection " << i << ": object=" << sel.objectBranchId
                  << ", ref=" << sel.referenceBranchId << std::endl;
        h::PlateFieldSamplingOptions rgbSampling{
            .sampleWidth = 512U,
            .sampleHeight = 512U,
            .refractiveIndex = 1.0,
            .extentWidthMetres = 0.04,
            .extentHeightMetres = 0.04,
            .demodulateCarrier = true,
        };
        auto rec = h::recordVolumePlate(project.scene, fields, sel.objectBranchId, sel.referenceBranchId, {}, rgbSampling, fft);
        auto asset = h::freezeReflectionRecording(project.scene, rec);
        std::cout << "Channel " << i << ": suggestedFocusDepthMetres = " << asset.suggestedFocusDepthMetres << std::endl;
        auto view = h::defaultHologramView(asset);
        std::cout << "Channel " << i << ": focusDistanceMetres = " << view.focusDistanceMetres
                  << ", pupilRadiusMetres = " << view.pupilRadiusMetres
                  << ", eyeZ = " << view.eyePosition.z << std::endl;
        auto obs = h::observeRecordedHologram(asset, view, fft);
        double minI = 1e30, maxObsI = 0.0;
        for (const auto& s : obs.sensorField.samples()) {
            double iVal = std::norm(s);
            minI = std::min(minI, iVal);
            maxObsI = std::max(maxObsI, iVal);
        }
        std::cout << "Observed sensor field size: " << obs.sensorField.width() << "x" << obs.sensorField.height()
                  << ", pitch: " << obs.sensorField.pitchXMetres() << "x" << obs.sensorField.pitchYMetres()
                  << ", pupilPower: " << obs.pupilPowerWatts
                  << ", minI: " << minI << ", maxI: " << maxObsI << std::endl;

        // Complete observer imaging pipeline test
        {
            auto fld = asset.coupling;
            const double zEye = std::max(0.001, view.eyePosition.z - view.platePose.translationMetres.z);
            const double zProp = zEye - view.focusDistanceMetres;
            holobench::compute::propagation::AngularSpectrumPropagator asProp(fft);
            asProp.propagateInPlace(fld, zProp);

            // Pupil filtering in spatial frequency domain
            fft.forward2D(fld);
            const double lambda = asset.material.recordingVacuumWavelengthMetres;
            const double fc = std::max(100.0, view.pupilRadiusMetres / (lambda * zEye));
            const double dfx = 1.0 / (static_cast<double>(fld.width()) * fld.pitchXMetres());
            const double dfy = 1.0 / (static_cast<double>(fld.height()) * fld.pitchYMetres());
            const double fx0 = (view.eyePosition.x) / (lambda * zEye);
            const double fy0 = (view.eyePosition.y) / (lambda * zEye);

            const auto halfW = fld.width() / 2;
            const auto halfH = fld.height() / 2;
            for (std::size_t y = 0; y < fld.height(); ++y) {
                const double fy = (static_cast<double>(y) - static_cast<double>(halfH)) * dfy;
                for (std::size_t x = 0; x < fld.width(); ++x) {
                    const double fx = (static_cast<double>(x) - static_cast<double>(halfW)) * dfx;
                    const double dist2 = std::pow(fx - fx0, 2.0) + std::pow(fy - fy0, 2.0);
                    if (dist2 > fc * fc) {
                        fld.at(x, y) = 0.0;
                    }
                }
            }
            fft.inverse2D(fld);

            std::ofstream bmp("out/diag-pipeline-" + std::to_string(i) + ".bmp", std::ios::binary);
            const uint32_t w = static_cast<uint32_t>(fld.width());
            const uint32_t h = static_cast<uint32_t>(fld.height());
            double maxI = 0.0;
            for (const auto& s : fld.samples()) maxI = std::max(maxI, std::norm(s));
            uint32_t rowSize = (w * 3 + 3) & ~3U;
            uint32_t fileSize = 54 + rowSize * h;
            uint8_t header[54] = {'B','M', (uint8_t)fileSize, (uint8_t)(fileSize>>8), (uint8_t)(fileSize>>16), (uint8_t)(fileSize>>24),
                                  0,0,0,0, 54,0,0,0, 40,0,0,0, (uint8_t)w, (uint8_t)(w>>8), (uint8_t)(w>>16), (uint8_t)(w>>24),
                                  (uint8_t)h, (uint8_t)(h>>8), (uint8_t)(h>>16), (uint8_t)(h>>24), 1,0, 24,0};
            bmp.write(reinterpret_cast<const char*>(header), 54);
            std::vector<uint8_t> row(rowSize, 0);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    double val = maxI > 0.0 ? std::norm(fld.at(x, y)) / maxI : 0.0;
                    uint8_t byteVal = static_cast<uint8_t>(std::clamp(std::sqrt(val) * 255.0, 0.0, 255.0));
                    row[x * 3 + 0] = byteVal;
                    row[x * 3 + 1] = byteVal;
                    row[x * 3 + 2] = byteVal;
                }
                bmp.write(reinterpret_cast<const char*>(row.data()), rowSize);
            }
        }

        // Test shifted eye viewpoint (parallax test)
        {
            auto fld = asset.coupling;
            const double zEye = std::max(0.001, view.eyePosition.z - view.platePose.translationMetres.z);
            const double zProp = zEye - view.focusDistanceMetres;
            holobench::compute::propagation::AngularSpectrumPropagator asProp(fft);
            asProp.propagateInPlace(fld, zProp);

            fft.forward2D(fld);
            const double lambda = asset.material.recordingVacuumWavelengthMetres;
            const double fc = std::max(100.0, view.pupilRadiusMetres / (lambda * zEye));
            const double dfx = 1.0 / (static_cast<double>(fld.width()) * fld.pitchXMetres());
            const double dfy = 1.0 / (static_cast<double>(fld.height()) * fld.pitchYMetres());
            const double shiftedEyeX = 0.015; // Shift eye by 15 mm to the right
            const double fx0 = shiftedEyeX / (lambda * zEye);
            const double fy0 = 0.0;

            const auto halfW = fld.width() / 2;
            const auto halfH = fld.height() / 2;
            for (std::size_t y = 0; y < fld.height(); ++y) {
                const double fy = (static_cast<double>(y) - static_cast<double>(halfH)) * dfy;
                for (std::size_t x = 0; x < fld.width(); ++x) {
                    const double fx = (static_cast<double>(x) - static_cast<double>(halfW)) * dfx;
                    const double dist2 = std::pow(fx - fx0, 2.0) + std::pow(fy - fy0, 2.0);
                    if (dist2 > fc * fc) {
                        fld.at(x, y) = 0.0;
                    }
                }
            }
            fft.inverse2D(fld);

            std::ofstream bmp("out/diag-shifted-" + std::to_string(i) + ".bmp", std::ios::binary);
            const uint32_t w = static_cast<uint32_t>(fld.width());
            const uint32_t h = static_cast<uint32_t>(fld.height());
            double maxI = 0.0;
            for (const auto& s : fld.samples()) maxI = std::max(maxI, std::norm(s));
            uint32_t rowSize = (w * 3 + 3) & ~3U;
            uint32_t fileSize = 54 + rowSize * h;
            uint8_t header[54] = {'B','M', (uint8_t)fileSize, (uint8_t)(fileSize>>8), (uint8_t)(fileSize>>16), (uint8_t)(fileSize>>24),
                                  0,0,0,0, 54,0,0,0, 40,0,0,0, (uint8_t)w, (uint8_t)(w>>8), (uint8_t)(w>>16), (uint8_t)(w>>24),
                                  (uint8_t)h, (uint8_t)(h>>8), (uint8_t)(h>>16), (uint8_t)(h>>24), 1,0, 24,0};
            bmp.write(reinterpret_cast<const char*>(header), 54);
            std::vector<uint8_t> row(rowSize, 0);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    double val = maxI > 0.0 ? std::norm(fld.at(x, y)) / maxI : 0.0;
                    uint8_t byteVal = static_cast<uint8_t>(std::clamp(std::sqrt(val) * 255.0, 0.0, 255.0));
                    row[x * 3 + 0] = byteVal;
                    row[x * 3 + 1] = byteVal;
                    row[x * 3 + 2] = byteVal;
                }
                bmp.write(reinterpret_cast<const char*>(row.data()), rowSize);
            }
        }
        // Save sensor field intensity
        {
            std::ofstream bmp("out/diag-sensor-" + std::to_string(i) + ".bmp", std::ios::binary);
            const uint32_t w = static_cast<uint32_t>(obs.sensorField.width());
            const uint32_t h = static_cast<uint32_t>(obs.sensorField.height());
            double maxI = 0.0;
            for (const auto& s : obs.sensorField.samples()) maxI = std::max(maxI, std::norm(s));
            uint32_t rowSize = (w * 3 + 3) & ~3U;
            uint32_t fileSize = 54 + rowSize * h;
            uint8_t header[54] = {'B','M', (uint8_t)fileSize, (uint8_t)(fileSize>>8), (uint8_t)(fileSize>>16), (uint8_t)(fileSize>>24),
                                  0,0,0,0, 54,0,0,0, 40,0,0,0, (uint8_t)w, (uint8_t)(w>>8), (uint8_t)(w>>16), (uint8_t)(w>>24),
                                  (uint8_t)h, (uint8_t)(h>>8), (uint8_t)(h>>16), (uint8_t)(h>>24), 1,0, 24,0};
            bmp.write(reinterpret_cast<const char*>(header), 54);
            std::vector<uint8_t> row(rowSize, 0);
            for (uint32_t y = 0; y < h; ++y) {
                for (uint32_t x = 0; x < w; ++x) {
                    double val = maxI > 0.0 ? std::norm(obs.sensorField.at(x, y)) / maxI : 0.0;
                    uint8_t byteVal = static_cast<uint8_t>(std::clamp(std::sqrt(val) * 255.0, 0.0, 255.0));
                    row[x * 3 + 0] = byteVal;
                    row[x * 3 + 1] = byteVal;
                    row[x * 3 + 2] = byteVal;
                }
                bmp.write(reinterpret_cast<const char*>(row.data()), rowSize);
            }
        }

        // Closed-loop quantitative validation of reconstructed scene
        REQUIRE(obs.sensorField.width() == 512U);
        REQUIRE(obs.sensorField.height() == 512U);
        CHECK(obs.pupilPowerWatts > 0.0);
        CHECK(maxObsI > 0.0);

        const auto w = obs.sensorField.width();
        const auto h = obs.sensorField.height();
        const double wD = static_cast<double>(w);
        double bgEnergy = 0.0, leftEnergy = 0.0, centerEnergy = 0.0, rightEnergy = 0.0;
        int bgCount = 0;
        for (std::size_t y = 0; y < h; ++y) {
            for (std::size_t x = 0; x < w; ++x) {
                const double val = std::norm(obs.sensorField.at(x, y));
                if (x < 50U && y < 50U) {
                    bgEnergy += val;
                    bgCount++;
                }
                const double xD = static_cast<double>(x);
                if (xD < wD * 0.38) {
                    leftEnergy += val;
                } else if (xD < wD * 0.62) {
                    centerEnergy += val;
                } else {
                    rightEnergy += val;
                }
            }
        }
        CHECK(bgEnergy / static_cast<double>(std::max(1, bgCount)) < 1e-6 * maxObsI);
        CHECK(leftEnergy > 0.0);
        CHECK(centerEnergy > 0.0);
        CHECK(rightEnergy > 0.0);

        // Closed loop test: observer eye viewpoint translation (parallax)
        auto shiftedView = view;
        shiftedView.eyePosition.x += 0.005; // 5 mm right shift
        auto shiftedObs = h::observeRecordedHologram(asset, shiftedView, fft);
        CHECK(shiftedObs.pupilPowerWatts > 0.0);

        // Closed loop test: plate tilt with proper orthonormal rotation preserving initial orientation
        auto tiltedView = view;
        const double yaw = 0.03;
        const double cosY = std::cos(yaw), sinY = std::sin(yaw);
        const auto origX = view.platePose.localXAxisInWorld;
        const auto origZ = view.platePose.localZAxisInWorld;
        tiltedView.platePose.localXAxisInWorld = {cosY * origX.x + sinY * origX.z, origX.y, -sinY * origX.x + cosY * origX.z};
        tiltedView.platePose.localZAxisInWorld = {cosY * origZ.x + sinY * origZ.z, origZ.y, -sinY * origZ.x + cosY * origZ.z};
        auto tiltedObs = h::observeRecordedHologram(asset, tiltedView, fft);
        CHECK(tiltedObs.efficiency > 0.5);
        CHECK(tiltedObs.pupilPowerWatts > 1e-8);
    }
}
template<typename T, typename = void>
struct HasReferenceDirection : std::false_type {};
template<typename T>
struct HasReferenceDirection<T, std::void_t<decltype(std::declval<T>().referenceDirection)>> : std::true_type {};

template<typename T, typename = void>
struct HasObjectDirection : std::false_type {};
template<typename T>
struct HasObjectDirection<T, std::void_t<decltype(std::declval<T>().objectDirection)>> : std::true_type {};

static_assert(!HasReferenceDirection<h::RecordedHologram>::value,
              "RecordedHologram must NOT contain referenceDirection");
static_assert(!HasObjectDirection<h::RecordedHologram>::value,
              "RecordedHologram must NOT contain objectDirection");

TEST_CASE("recorded hologram contains solely interference fringe information and supports reflection and transmission volume gratings") {
    constexpr double lambda = 532e-9;
    constexpr double n0 = 1.5;
    const double k = 2.0 * std::numbers::pi / lambda;
    const double km = k * n0;

    // 1. Transmission Volume Grating test: micro-fringes run along Z (Kz ~ 0)
    {
        const double gratingPeriod = 1.0e-6; // 1 um fringe period in X
        const double K0x = 2.0 * std::numbers::pi / gratingPeriod;
        const holobench::math::Vec3d transK0{K0x, 0.0, 0.0};
        const double thetaB = std::asin(std::clamp(K0x / (2.0 * km), 0.0, 0.99));

        h::VolumeHologramParameters transMat;
        transMat.recordingVacuumWavelengthMetres = transMat.replayVacuumWavelengthMetres = lambda;
        transMat.averageRefractiveIndex = n0;
        transMat.refractiveIndexModulation = 0.03;
        transMat.recordedThicknessMetres = 15e-6;
        transMat.geometry = h::VolumeHologramGeometry::Transmission;
        transMat.recordingBraggAngleInMediumRadians = thetaB;
        transMat.replayAngleInMediumRadians = thetaB;

        holobench::field::ComplexField2D field(32, 32, 1e-6, 1e-6, lambda);
        constexpr double phaseSlopeX = 1e4; // rad/m
        for (std::size_t y = 0; y < field.height(); ++y) {
            for (std::size_t x = 0; x < field.width(); ++x) {
                const double xM = field.xCoordinateMetres(x);
                field.at(x, y) = std::polar(1.0, phaseSlopeX * xM);
            }
        }
        const double norm = std::sqrt(holobench::field::computeIntegratedIntensity(field));
        for (auto &s : field.samples()) s /= norm;

        h::RecordedHologram transAsset{
            .sourcePlateId = "transmission-plate",
            .material = transMat,
            .gratingVector = transK0,
            .centreXMetres = 0.0,
            .centreYMetres = 0.0,
            .coupling = std::move(field),
            .suggestedFocusDepthMetres = 0.05,
        };

        h::validateRecordedHologram(transAsset);
        const auto localField = h::computeLocalVolumeGratingField(transAsset);
        REQUIRE(localField.size() == 32U * 32U);

        // Verify that local K = K0 + grad(Phi)
        // At interior pixels: Kx = K0x + phaseSlopeX, Ky = 0, Kz = 0
        const auto centerSample = localField[16 * 32 + 16];
        CHECK(centerSample.amplitude == doctest::Approx(1.0).epsilon(1e-4));
        CHECK(centerSample.Kx == doctest::Approx(K0x + phaseSlopeX).epsilon(1e-2));
        CHECK(centerSample.Ky == doctest::Approx(0.0).epsilon(1e-4));
        CHECK(centerSample.Kz == doctest::Approx(0.0).epsilon(1e-4));

        // Verify JSON serialization contains purely grating and fringe data, without direction tags
        const auto tempTransPath = std::filesystem::temp_directory_path() / "test-transmission.holo.json";
        h::saveRecordedHologram(transAsset, tempTransPath);
        std::string content;
        {
            std::ifstream f(tempTransPath);
            std::stringstream buffer;
            buffer << f.rdbuf();
            content = buffer.str();
        }
        CHECK(content.find("\"reference\"") == std::string::npos);
        CHECK(content.find("\"object\"") == std::string::npos);
        CHECK(content.find("\"grating\"") != std::string::npos);
        CHECK(content.find("geometry") != std::string::npos);

        // Roundtrip check
        const auto decoded = h::loadRecordedHologram(tempTransPath);
        std::filesystem::remove(tempTransPath);
        CHECK(decoded.material.geometry == h::VolumeHologramGeometry::Transmission);
        CHECK(decoded.gratingVector.x == doctest::Approx(transK0.x));
        CHECK(decoded.gratingVector.z == doctest::Approx(0.0));
    }

    // 2. Reflection Volume Grating (Denisyuk) test: micro-fringes stacked in depth (Kz ~ 2 km)
    {
        h::VolumeHologramParameters reflMat;
        reflMat.recordingVacuumWavelengthMetres = reflMat.replayVacuumWavelengthMetres = lambda;
        reflMat.averageRefractiveIndex = n0;
        reflMat.refractiveIndexModulation = 0.04;
        reflMat.recordedThicknessMetres = 12e-6;
        reflMat.geometry = h::VolumeHologramGeometry::Reflection;
        reflMat.recordingBraggAngleInMediumRadians = 0.0;
        reflMat.replayAngleInMediumRadians = 0.0;

        const double K0z = 2.0 * km;
        const holobench::math::Vec3d reflK0{0.0, 0.0, K0z};

        holobench::field::ComplexField2D field(32, 32, 1e-6, 1e-6, lambda);
        constexpr double phaseSlopeY = -2e4; // rad/m
        for (std::size_t y = 0; y < field.height(); ++y) {
            for (std::size_t x = 0; x < field.width(); ++x) {
                const double yM = field.yCoordinateMetres(y);
                field.at(x, y) = std::polar(0.8, phaseSlopeY * yM);
            }
        }
        const double norm = std::sqrt(holobench::field::computeIntegratedIntensity(field));
        for (auto &s : field.samples()) s /= norm;

        h::RecordedHologram reflAsset{
            .sourcePlateId = "reflection-denisyuk-plate",
            .material = reflMat,
            .gratingVector = reflK0,
            .centreXMetres = 0.0,
            .centreYMetres = 0.0,
            .coupling = std::move(field),
            .suggestedFocusDepthMetres = 0.02,
        };

        h::validateRecordedHologram(reflAsset);
        const auto localField = h::computeLocalVolumeGratingField(reflAsset);
        REQUIRE(localField.size() == 32U * 32U);

        const auto centerSample = localField[16 * 32 + 16];
        CHECK(centerSample.amplitude == doctest::Approx(1.0).epsilon(1e-4));
        CHECK(centerSample.Kx == doctest::Approx(0.0).epsilon(1e-4));
        CHECK(centerSample.Ky == doctest::Approx(phaseSlopeY).epsilon(1e-2));
        CHECK(centerSample.Kz == doctest::Approx(K0z).epsilon(1e-4));

        // Verify JSON serialization contains purely grating and fringe data, without direction tags
        const auto tempReflPath = std::filesystem::temp_directory_path() / "test-reflection.holo.json";
        h::saveRecordedHologram(reflAsset, tempReflPath);
        std::string reflContent;
        {
            std::ifstream f(tempReflPath);
            std::stringstream buffer;
            buffer << f.rdbuf();
            reflContent = buffer.str();
        }
        CHECK(reflContent.find("\"reference\"") == std::string::npos);
        CHECK(reflContent.find("\"object\"") == std::string::npos);
        CHECK(reflContent.find("\"grating\"") != std::string::npos);

        const auto decoded = h::loadRecordedHologram(tempReflPath);
        std::filesystem::remove(tempReflPath);
        CHECK(decoded.material.geometry == h::VolumeHologramGeometry::Reflection);
        CHECK(decoded.gratingVector.z == doctest::Approx(K0z));
    }
}
