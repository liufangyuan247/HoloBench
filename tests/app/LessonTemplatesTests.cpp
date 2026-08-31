#include <doctest/doctest.h>

#include <atomic>
#include <cmath>
#include <complex>
#include <fstream>
#include <iterator>
#include <numbers>
#include <stdexcept>

#include "app/lessons/LessonTemplates.hpp"
#include "app/lessons/LessonTemplateRepository.hpp"
#include "app/HolographyUiState.hpp"
#include "app/ReflectionRefractionWorkbench.hpp"
#include "app/WaveWorkbenchProject.hpp"
#include "app/SlmInterferenceProject.hpp"
#include "app/SlmInterferenceUiState.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/project/ProjectDocument.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"

namespace lessons = holobench::app::lessons;

namespace {

class TemporaryDirectory final {
public:
    TemporaryDirectory() {
        static std::atomic<unsigned long long> counter {0};
        path_ = std::filesystem::temp_directory_path()
            / ("holobench-lesson-template-"
                + std::to_string(counter.fetch_add(1)));
        std::filesystem::create_directories(path_);
    }
    ~TemporaryDirectory() {
        std::error_code ignored;
        std::filesystem::remove_all(path_, ignored);
    }
    [[nodiscard]] const std::filesystem::path& path() const noexcept {
        return path_;
    }

private:
    std::filesystem::path path_;
};

[[nodiscard]] std::string readBytes(const std::filesystem::path& path) {
    std::ifstream stream(path, std::ios::binary);
    return {
        std::istreambuf_iterator<char>(stream),
        std::istreambuf_iterator<char>(),
    };
}

} // namespace

TEST_SUITE("LessonTemplates") {

TEST_CASE("packaged reflection workbench matches factory and canonical bytes") {
    const std::filesystem::path root(HOLOBENCH_LESSON_TEMPLATE_DIR);
    const auto loaded = lessons::loadReflectionRefractionLessonTemplate(
        root, "lesson_reflection_refraction");

    CHECK(loaded.config == lessons::makeReflectionRefractionLessonTemplate());
    CHECK(loaded.provenance
        == holobench::project::makeLessonTemplateProvenance(
            "lesson_reflection_refraction",
            lessons::kLessonTemplateVersion));
    CHECK(holobench::app::reflection::
              serializeReflectionRefractionWorkbenchJson(loaded)
        == readBytes(
            root / "lesson_reflection_refraction.reflection.json"));
    CHECK_THROWS_AS(
        static_cast<void>(
            lessons::loadReflectionRefractionLessonTemplate(
                root, "lesson_thin_lens")),
        std::invalid_argument);
}

TEST_CASE("packaged optical-bench templates match factories and provenance") {
    const std::filesystem::path root(HOLOBENCH_LESSON_TEMPLATE_DIR);
    const auto thin = lessons::loadOpticalBenchLessonTemplate(
        root, "lesson_thin_lens");
    const auto realVirtual = lessons::loadOpticalBenchLessonTemplate(
        root, "lesson_real_virtual_images");
    CHECK(thin.scene == lessons::makeThinLensLessonTemplate());
    CHECK(realVirtual.scene == lessons::makeRealVirtualLessonTemplate());
    CHECK(thin.provenance == holobench::project::makeLessonTemplateProvenance(
        "lesson_thin_lens", lessons::kLessonTemplateVersion));
    CHECK(realVirtual.provenance
        == holobench::project::makeLessonTemplateProvenance(
            "lesson_real_virtual_images", lessons::kLessonTemplateVersion));
    CHECK_THROWS_AS(
        static_cast<void>(lessons::loadOpticalBenchLessonTemplate(
            root, "lesson_diffraction")),
        std::invalid_argument);
}

TEST_CASE("packaged wave-workbench templates match factories and canonical bytes") {
    const std::filesystem::path root(HOLOBENCH_LESSON_TEMPLATE_DIR);
    const auto diffraction = lessons::loadWaveWorkbenchLessonTemplate(
        root, "lesson_diffraction");
    CHECK(diffraction.waveDetector == lessons::makeDiffractionLessonTemplate());
    CHECK(diffraction.samplingDebugger
        == holobench::app::samplingdebug::SamplingDebuggerConfig {});
    CHECK(diffraction.provenance
        == holobench::project::makeLessonTemplateProvenance(
            "lesson_diffraction", lessons::kLessonTemplateVersion));

    const auto expectedFourier = lessons::makeFourierLessonTemplate();
    for (const std::string id : {
             "lesson_fourier_plane",
             "lesson_spatial_filtering",
             "lesson_na_psf",
         }) {
        const auto loaded = lessons::loadWaveWorkbenchLessonTemplate(root, id);
        CHECK(loaded.waveDetector == expectedFourier.waveDetector);
        CHECK(loaded.samplingDebugger == expectedFourier.samplingDebugger);
        CHECK(loaded.provenance
            == holobench::project::makeLessonTemplateProvenance(
                id, lessons::kLessonTemplateVersion));
        CHECK(holobench::app::waveproject::serializeWaveWorkbenchProjectJson(
                  loaded)
            == readBytes(root / (id + ".wave.json")));
    }
    CHECK(holobench::app::waveproject::serializeWaveWorkbenchProjectJson(
              diffraction)
        == readBytes(root / "lesson_diffraction.wave.json"));
    CHECK_THROWS_AS(
        static_cast<void>(lessons::loadWaveWorkbenchLessonTemplate(
            root, "lesson_coherence_interference")),
        std::invalid_argument);
}

TEST_CASE("packaged SLM template matches the coherence factory and canonical bytes") {
    const std::filesystem::path root(HOLOBENCH_LESSON_TEMPLATE_DIR);
    const auto loaded = lessons::loadSlmLessonTemplate(
        root, "lesson_coherence_interference");

    CHECK(holobench::app::slmui::sameExperimentPhysicsConfig(
        loaded.config, lessons::makeCoherenceLessonTemplate()));
    CHECK(loaded.provenance
        == holobench::project::makeLessonTemplateProvenance(
            "lesson_coherence_interference",
            lessons::kLessonTemplateVersion));
    CHECK(loaded.calibrationProvenance == "No measured LUT loaded");
    CHECK(holobench::app::slmproject::serializeSlmInterferenceProjectJson(
              loaded)
        == readBytes(root / "lesson_coherence_interference.slm.json"));
    CHECK_THROWS_AS(
        static_cast<void>(lessons::loadSlmLessonTemplate(
            root, "lesson_volume_holograms")),
        std::invalid_argument);
}

TEST_CASE("packaged holography templates match factories provenance and canonical bytes") {
    const std::filesystem::path root(HOLOBENCH_LESSON_TEMPLATE_DIR);
    const auto basic = lessons::loadHolographyLessonTemplate(
        root, "lesson_holography");
    const auto advanced = lessons::loadHolographyLessonTemplate(
        root, "lesson_h1_h2_advanced");

    CHECK(holobench::app::holographyui::sameHolographyLabConfig(
        basic.config, lessons::makeHolographyLessonTemplate()));
    CHECK(holobench::app::holographyui::sameHolographyLabConfig(
        advanced.config, lessons::makeH1H2AdvancedLessonTemplate()));
    for (const auto* document : {&basic, &advanced}) {
        CHECK(document->provenance
            == holobench::project::makeLessonTemplateProvenance(
                document->provenance.sourceId,
                lessons::kLessonTemplateVersion));
        CHECK(holobench::app::holographyproject::
                  serializeHolographyProjectJson(*document)
            == readBytes(root / (document->provenance.sourceId
                + ".holography.json")));
    }
    CHECK_THROWS_AS(
        static_cast<void>(lessons::loadHolographyLessonTemplate(
            root, "lesson_coherence_interference")),
        std::invalid_argument);

    TemporaryDirectory mismatchedRoot;
    {
        std::ofstream stream(
            mismatchedRoot.path()
                / "lesson_h1_h2_advanced.holography.json",
            std::ios::binary);
        stream << holobench::app::holographyproject::
            serializeHolographyProjectJson(basic);
    }
    CHECK_THROWS_AS(
        static_cast<void>(lessons::loadHolographyLessonTemplate(
            mismatchedRoot.path(), "lesson_h1_h2_advanced")),
        std::invalid_argument);
}

TEST_CASE("reflection lesson reuses ray solvers and matches independent angle laws") {
    const lessons::ReflectionRefractionLessonConfig config {
        .incidenceAngleRadians = 30.0 * std::numbers::pi_v<double> / 180.0,
        .incidentRefractiveIndex = 1.0,
        .transmittedRefractiveIndex = 1.5,
    };
    const auto result = lessons::evaluateReflectionRefractionLesson(config);
    const double expectedTransmission = std::asin(1.0 / 3.0);
    CHECK(result.incidenceAngleRadians == doctest::Approx(config.incidenceAngleRadians));
    CHECK(result.reflectionAngleRadians == doctest::Approx(config.incidenceAngleRadians));
    CHECK(result.transmissionAngleRadians == doctest::Approx(expectedTransmission));
    CHECK(std::abs(result.reflectionAngleErrorRadians) < 1e-14);
    CHECK(std::abs(result.snellResidual) < 1e-14);
    CHECK_FALSE(result.totalInternalReflection);
}

TEST_CASE("reflection lesson exposes total internal reflection and validates inputs") {
    const auto result = lessons::evaluateReflectionRefractionLesson({
        .incidenceAngleRadians = 50.0 * std::numbers::pi_v<double> / 180.0,
        .incidentRefractiveIndex = 1.5,
        .transmittedRefractiveIndex = 1.0,
    });
    CHECK(result.totalInternalReflection);
    CHECK(result.reflectionAngleRadians
        == doctest::Approx(result.incidenceAngleRadians));
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateReflectionRefractionLesson({
            .incidenceAngleRadians = -0.1,
        })),
        std::invalid_argument);
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateReflectionRefractionLesson({
            .transmittedRefractiveIndex = 0.0,
        })),
        std::invalid_argument);
}

TEST_CASE("thin lens template starts defocused and shared prediction finds focus") {
    const auto lessonTemplate = lessons::makeThinLensLessonTemplate();
    const auto initial = lessons::evaluateThinLensLessonObservation(
        lessonTemplate, lessonTemplate.screen.planeZMetres);
    CHECK(initial.prediction.nature == holobench::optics::scene::ImageNature::Real);
    CHECK_FALSE(initial.screenMoved);
    CHECK_FALSE(initial.screenAtFocus);
    CHECK(initial.screenFocusErrorMetres == doctest::Approx(0.03));

    auto moved = lessonTemplate;
    moved.screen.planeZMetres = initial.prediction.imagePlaneZMetres + 0.0005;
    const auto focused = lessons::evaluateThinLensLessonObservation(
        moved, lessonTemplate.screen.planeZMetres);
    CHECK(focused.screenMoved);
    CHECK(focused.screenAtFocus);
    CHECK(focused.screenFocusErrorMetres == doctest::Approx(0.0005));
}

TEST_CASE("thin lens setup is an ordinary versioned Lab scene") {
    const auto lessonTemplate = lessons::makeThinLensLessonTemplate();
    const auto document = holobench::optics::scene::sceneToProjectDocument(
        lessonTemplate);
    CHECK(document.formatVersion == holobench::project::kCurrentFormatVersion);
    CHECK(holobench::optics::scene::projectDocumentToScene(document)
        == lessonTemplate);
}

TEST_CASE("real virtual template crosses the focal plane through the shared solver") {
    auto scene = lessons::makeRealVirtualLessonTemplate();
    const auto document = holobench::optics::scene::sceneToProjectDocument(scene);
    CHECK(document.formatVersion == holobench::project::kCurrentFormatVersion);
    CHECK(holobench::optics::scene::projectDocumentToScene(document) == scene);
    const auto initial = lessons::evaluateRealVirtualLessonObservation(scene);
    CHECK(initial.prediction.nature
        == holobench::optics::scene::ImageNature::Real);
    CHECK_FALSE(initial.crossedFocalPlane);

    scene.source.positionMetres.z = scene.lens.planeZMetres
        - 0.75 * scene.lens.focalLengthMetres;
    const auto crossed = lessons::evaluateRealVirtualLessonObservation(scene);
    CHECK(crossed.prediction.nature
        == holobench::optics::scene::ImageNature::Virtual);
    CHECK(crossed.crossedFocalPlane);
}

TEST_CASE("narrower shared wave aperture produces a broader diffraction result") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto templateConfig = lessons::makeDiffractionLessonTemplate();
    const auto baselineResult = holobench::app::wave::simulateDetectorField(
        templateConfig, fft);
    const auto baseline = lessons::evaluateDiffractionLessonObservation(
        templateConfig,
        baselineResult,
        templateConfig.rectangularHalfWidthMetres);
    CHECK_FALSE(baseline.apertureNarrowed);
    CHECK_FALSE(baseline.patternBroadened);
    auto mismatchedConfig = templateConfig;
    mismatchedConfig.rectangularHalfWidthMetres *= 0.5;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateDiffractionLessonObservation(
            mismatchedConfig,
            baselineResult,
            templateConfig.rectangularHalfWidthMetres)),
        std::invalid_argument);

    auto narrowedConfig = templateConfig;
    narrowedConfig.rectangularHalfWidthMetres *= 0.5;
    const auto narrowedResult = holobench::app::wave::simulateDetectorField(
        narrowedConfig, fft);
    const auto narrowed = lessons::evaluateDiffractionLessonObservation(
        narrowedConfig,
        narrowedResult,
        templateConfig.rectangularHalfWidthMetres,
        baseline.horizontalHalfMaximumWidthMetres);
    CAPTURE(baseline.horizontalHalfMaximumWidthMetres);
    CAPTURE(narrowed.horizontalHalfMaximumWidthMetres);
    CHECK(narrowed.apertureNarrowed);
    CHECK(narrowed.patternBroadened);
    CHECK(narrowed.horizontalHalfMaximumWidthMetres
        > baseline.horizontalHalfMaximumWidthMetres);
}

TEST_CASE("Fourier lesson observes the shared 4-f spectrum and moved plane probe") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    auto config = lessonTemplate.samplingDebugger;
    config.propagationDistanceMetres = 5.0e-3;
    config.probeDistancesMetres = {0.0, config.propagationDistanceMetres};
    const auto result = holobench::app::samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, config, fft);
    const auto observation = lessons::evaluateFourierPlaneLessonObservation(
        lessonTemplate, detectorResult, config, result);
    CAPTURE(observation.nonDcSpectralEnergyFraction);
    CHECK(observation.probeMoved);
    CHECK(observation.spectrumResolved);
    CHECK(observation.probePlaneCount == 2U);

    auto mismatchedDetector = detectorResult;
    mismatchedDetector.field.at(0U, 0U) += std::complex<double> {1.0, 0.0};
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateFourierPlaneLessonObservation(
            lessonTemplate, mismatchedDetector, config, result)),
        std::invalid_argument);

    auto mismatchedConfig = config;
    mismatchedConfig.fourFFirstFocalLengthMetres *= 2.0;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateFourierPlaneLessonObservation(
            lessonTemplate, detectorResult, mismatchedConfig, result)),
        std::invalid_argument);
}

TEST_CASE("spatial filtering lesson measures shared low-pass smoothing") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    const auto baselineResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, lessonTemplate.samplingDebugger, fft);
    const auto baseline = lessons::evaluateSpatialFilteringLessonObservation(
        lessonTemplate,
        detectorResult,
        lessonTemplate.samplingDebugger,
        baselineResult);
    auto filteredConfig = lessonTemplate.samplingDebugger;
    filteredConfig.fourFFilterKind
        = holobench::compute::fourier::CircularFilterKind::LowPass;
    const auto filteredResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, filteredConfig, fft);
    const auto filtered = lessons::evaluateSpatialFilteringLessonObservation(
        lessonTemplate,
        detectorResult,
        filteredConfig,
        filteredResult,
        baseline.imageDetailMetric);
    CAPTURE(baseline.imageDetailMetric);
    CAPTURE(filtered.imageDetailMetric);
    CAPTURE(filtered.integratedIntensityTransmission);
    CHECK(filtered.lowPassApplied);
    CHECK(filtered.imageSmoothed);
    CHECK(filtered.imageDetailMetric < baseline.imageDetailMetric);
}

TEST_CASE("NA lesson measures a narrower shared Airy PSF") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    const auto baselineResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, lessonTemplate.samplingDebugger, fft);
    const auto baseline = lessons::evaluateNaPsfLessonObservation(
        lessonTemplate,
        detectorResult,
        lessonTemplate.samplingDebugger,
        baselineResult);
    auto widerPupilConfig = lessonTemplate.samplingDebugger;
    widerPupilConfig.psfPupilRadiusMetres *= 1.5;
    const auto widerPupilResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, widerPupilConfig, fft);
    const auto widerPupil = lessons::evaluateNaPsfLessonObservation(
        lessonTemplate,
        detectorResult,
        widerPupilConfig,
        widerPupilResult,
        baseline.paraxialNumericalAperture,
        baseline.firstDarkRadiusMetres);
    CHECK(widerPupil.numericalApertureIncreased);
    CHECK(widerPupil.psfNarrowed);
    CHECK(widerPupil.firstDarkRadiusMetres
        < baseline.firstDarkRadiusMetres);
}

TEST_CASE("coherence lesson measures visibility loss from the shared SLM result") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeCoherenceLessonTemplate();
    const auto baselineResult
        = holobench::app::slmexperiment::runSlmInterferenceExperiment(
            lessonTemplate, fft);
    const auto baseline = lessons::evaluateCoherenceLessonObservation(
        lessonTemplate, lessonTemplate, baselineResult);
    auto changedConfig = lessonTemplate;
    changedConfig.mutualCoherence.opticalPathDifferenceMetres
        = lessonTemplate.mutualCoherence.coherenceLengthMetres;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateCoherenceLessonObservation(
            lessonTemplate, changedConfig, baselineResult)),
        std::invalid_argument);
    const auto changedResult
        = holobench::app::slmexperiment::runSlmInterferenceExperiment(
            changedConfig, fft);
    const auto changed = lessons::evaluateCoherenceLessonObservation(
        lessonTemplate,
        changedConfig,
        changedResult,
        baseline.fringeVisibility);
    CAPTURE(baseline.fringeVisibility);
    CAPTURE(changed.fringeVisibility);
    CHECK(changed.pathDifferenceChanged);
    CHECK(changed.visibilityReduced);
    CHECK(changed.coherenceMagnitude
        < baseline.coherenceMagnitude);
}

TEST_CASE("holography lesson observes shared recording replay and order diagnostics") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeHolographyLessonTemplate();
    const auto result = holobench::app::holographylab::runHolographyLab(
        lessonTemplate, fft);
    const auto observation = lessons::evaluateHolographyLessonObservation(
        lessonTemplate, lessonTemplate, result);

    CHECK(observation.h1Recorded);
    CHECK(observation.realImageReplayed);
    CHECK(observation.orderDiagnosticsAvailable);
    CHECK(observation.worstRealImageNormalizedError < 1e-8);
    CHECK(observation.minimumZeroOrderSeparationMetres > 0.0);
    CHECK(observation.minimumTwinOrderSeparationMetres > 0.0);

    auto staleConfig = lessonTemplate;
    staleConfig.transfer.h2AxialPositionMetres += 1e-3;
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateHolographyLessonObservation(
            lessonTemplate, staleConfig, result)),
        std::invalid_argument);
}

TEST_CASE("H1 H2 lesson observes only an applied axial move to transplane") {
    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeH1H2AdvancedLessonTemplate();
    const auto baselineResult = holobench::app::holographylab::runHolographyLab(
        lessonTemplate, fft);
    const auto baseline = lessons::evaluateH1H2AdvancedLessonObservation(
        lessonTemplate, lessonTemplate, baselineResult);
    CHECK(baseline.h1Recorded);
    CHECK_FALSE(baseline.h2PositionChanged);
    CHECK_FALSE(baseline.transplaneReached);

    auto moved = lessonTemplate;
    moved.transfer.h2AxialPositionMetres
        = moved.transfer.h1.objectToPlateDistanceMetres;
    const auto movedResult = holobench::app::holographylab::runHolographyLab(
        moved, fft);
    const auto transplane = lessons::evaluateH1H2AdvancedLessonObservation(
        lessonTemplate, moved, movedResult);
    CHECK(transplane.h2PositionChanged);
    CHECK(transplane.transplaneReached);
    CHECK(std::abs(transplane.signedImageDistanceFromH2Metres) <= 0.1e-3);

    auto illegalChange = moved;
    illegalChange.objectFeatures[0].phaseRadians += 0.1;
    const auto illegalResult = holobench::app::holographylab::runHolographyLab(
        illegalChange, fft);
    CHECK_THROWS_AS(
        static_cast<void>(lessons::evaluateH1H2AdvancedLessonObservation(
            lessonTemplate, illegalChange, illegalResult)),
        std::invalid_argument);
}

} // TEST_SUITE("LessonTemplates")
