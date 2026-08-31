#include <doctest/doctest.h>

#include <numbers>
#include <stdexcept>
#include <utility>

#include "app/lessons/LearnSession.hpp"
#include "compute/fft/CpuFftBackend.hpp"

namespace lessons = holobench::app::lessons;

namespace {

void completeReflectionPrerequisite(
    const lessons::LessonCatalog& catalog,
    lessons::LessonProgress& progress) {
    for (const auto& step : catalog.lesson("reflection_refraction").steps) {
        lessons::completeLessonStep(
            catalog, progress, "reflection_refraction", step.id);
    }
}

void completeLesson(
    const lessons::LessonCatalog& catalog,
    lessons::LessonProgress& progress,
    std::string_view lessonId) {
    for (const auto& step : catalog.lesson(lessonId).steps) {
        lessons::completeLessonStep(catalog, progress, lessonId, step.id);
    }
}

} // namespace

TEST_SUITE("LearnSession") {

TEST_CASE("the first eight catalog lessons expose guided workflows") {
    for (const auto lessonId : {
             "reflection_refraction",
             "thin_lens",
             "real_virtual_images",
             "diffraction",
             "fourier_plane",
             "spatial_filtering",
             "na_psf",
             "coherence_interference"}) {
        CHECK(lessons::hasInteractiveLessonWorkflow(lessonId));
    }
    CHECK_FALSE(lessons::hasInteractiveLessonWorkflow("holography"));
    CHECK_FALSE(lessons::hasInteractiveLessonWorkflow("h1_h2_advanced"));
}

TEST_CASE("reflection workflow requires template angle change and valid observation") {
    lessons::LearnSession session;
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 1U);
    CHECK_FALSE(session.confirmReflectionObservation());

    auto config = session.reflectionConfig();
    config.incidenceAngleRadians = 42.0 * std::numbers::pi_v<double> / 180.0;
    session.setReflectionConfig(config);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 2U);
    CHECK(session.confirmReflectionObservation());
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "reflection_refraction")
        == lessons::LessonStatus::Completed);
}

TEST_CASE("restoring reflection workbench input does not mutate lesson progress") {
    lessons::LearnSession session;
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    auto config = session.reflectionConfig();
    config.incidenceAngleRadians
        = 42.0 * std::numbers::pi_v<double> / 180.0;

    session.replaceReflectionConfig(config);

    CHECK(session.reflectionConfig() == config);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 1U);
    CHECK_FALSE(session.confirmReflectionObservation());
    session.setReflectionConfig(config);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 2U);
}

TEST_CASE("thin lens workflow advances only after template move and shared focus") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeReflectionPrerequisite(session.catalog(), progress);
    session.replaceProgress(progress);
    session.beginLesson("thin_lens");
    auto scene = lessons::makeThinLensLessonTemplate();
    session.confirmTemplateLoaded();
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "thin_lens") == 1U);

    const auto prediction = holobench::optics::scene::predictThinLensImage(scene);
    scene.screen.planeZMetres = prediction.imagePlaneZMetres;
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "thin_lens")
        == lessons::LessonStatus::Completed);
    REQUIRE(session.thinLensObservation().has_value());
    CHECK(session.thinLensObservation()->screenAtFocus);
}

TEST_CASE("session rejects locked or unsupported lessons and reset cascades") {
    lessons::LearnSession session;
    CHECK_THROWS_AS(session.beginLesson("thin_lens"), std::invalid_argument);
    CHECK_THROWS_AS(session.beginLesson("diffraction"), std::invalid_argument);
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    session.resetActiveLesson();
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "reflection_refraction") == 0U);
    CHECK(session.hasActiveLesson());
    session.endLesson();
    CHECK_FALSE(session.hasActiveLesson());
}

TEST_CASE("rejected observations and progress replacements preserve session state") {
    lessons::LearnSession session;
    session.beginLesson("reflection_refraction");
    session.confirmTemplateLoaded();
    const auto progressBefore = lessons::serializeLessonProgressJson(
        session.catalog(), session.progress());
    const auto configBefore = session.reflectionConfig();
    auto invalidConfig = configBefore;
    invalidConfig.transmittedRefractiveIndex = 0.0;
    CHECK_THROWS_AS(
        session.setReflectionConfig(invalidConfig),
        std::invalid_argument);
    CHECK(session.reflectionConfig().transmittedRefractiveIndex
        == configBefore.transmittedRefractiveIndex);
    CHECK(lessons::serializeLessonProgressJson(
        session.catalog(), session.progress()) == progressBefore);

    lessons::LessonProgress invalidProgress;
    invalidProgress.formatVersion = 99;
    CHECK_THROWS_AS(
        session.replaceProgress(std::move(invalidProgress)),
        std::invalid_argument);
    CHECK(session.hasActiveLesson());
    CHECK(lessons::serializeLessonProgressJson(
        session.catalog(), session.progress()) == progressBefore);
}

TEST_CASE("loading progress that locks the active lesson ends only the session") {
    lessons::LearnSession session;
    lessons::LessonProgress unlockedProgress;
    completeReflectionPrerequisite(session.catalog(), unlockedProgress);
    session.replaceProgress(unlockedProgress);
    session.beginLesson("thin_lens");
    CHECK(session.hasActiveLesson());

    session.replaceProgress({});
    CHECK_FALSE(session.hasActiveLesson());
    CHECK(lessons::lessonStatus(
        session.catalog(), session.progress(), "thin_lens")
        == lessons::LessonStatus::Locked);
}

TEST_CASE("real virtual workflow requires a focal crossing and correct classification") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeLesson(session.catalog(), progress, "reflection_refraction");
    completeLesson(session.catalog(), progress, "thin_lens");
    session.replaceProgress(progress);
    session.beginLesson("real_virtual_images");
    session.confirmTemplateLoaded();

    auto scene = lessons::makeRealVirtualLessonTemplate();
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "real_virtual_images") == 1U);
    scene.source.positionMetres.z = scene.lens.planeZMetres
        - 0.75 * scene.lens.focalLengthMetres;
    session.observeOpticalBenchScene(scene);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "real_virtual_images") == 2U);
    CHECK_FALSE(session.confirmRealVirtualClassification(
        holobench::optics::scene::ImageNature::Real));
    CHECK(session.confirmRealVirtualClassification(
        holobench::optics::scene::ImageNature::Virtual));
}

TEST_CASE("diffraction workflow observes applied shared-wave broadening") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeReflectionPrerequisite(session.catalog(), progress);
    session.replaceProgress(progress);
    session.beginLesson("diffraction");
    session.confirmTemplateLoaded();

    holobench::compute::fft::CpuFftBackend fft;
    const auto templateConfig = lessons::makeDiffractionLessonTemplate();
    const auto baselineResult = holobench::app::wave::simulateDetectorField(
        templateConfig, fft);
    session.observeWaveDetector(templateConfig, baselineResult);
    CHECK_FALSE(session.confirmDiffractionObservation());

    auto narrowedConfig = templateConfig;
    narrowedConfig.rectangularHalfWidthMetres *= 0.5;
    const auto narrowedResult = holobench::app::wave::simulateDetectorField(
        narrowedConfig, fft);
    session.observeWaveDetector(narrowedConfig, narrowedResult);
    CHECK(lessons::nextLessonStepIndex(
        session.catalog(), session.progress(), "diffraction") == 2U);
    CHECK(session.confirmDiffractionObservation());
}

TEST_CASE("Fourier plane workflow requires a moved shared probe and correct plane") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeLesson(session.catalog(), progress, "reflection_refraction");
    completeLesson(session.catalog(), progress, "thin_lens");
    completeLesson(session.catalog(), progress, "diffraction");
    session.replaceProgress(progress);
    session.beginLesson("fourier_plane");
    session.confirmTemplateLoaded();

    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    auto config = lessonTemplate.samplingDebugger;
    config.propagationDistanceMetres = 5.0e-3;
    config.probeDistancesMetres = {0.0, config.propagationDistanceMetres};
    const auto result = holobench::app::samplingdebug::analyzeSamplingDebugger(
        detectorResult.field, config, fft);
    session.observeSamplingDebugger(detectorResult, config, result);
    CHECK_FALSE(session.confirmFourierPlaneIdentification(
        lessons::FourierPlaneIdentification::ObjectPlane));
    CHECK(session.confirmFourierPlaneIdentification(
        lessons::FourierPlaneIdentification::FourierPlane));
}

TEST_CASE("spatial filtering workflow requires measured shared low-pass smoothing") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeLesson(session.catalog(), progress, "reflection_refraction");
    completeLesson(session.catalog(), progress, "thin_lens");
    completeLesson(session.catalog(), progress, "diffraction");
    completeLesson(session.catalog(), progress, "fourier_plane");
    session.replaceProgress(progress);
    session.beginLesson("spatial_filtering");
    session.confirmTemplateLoaded();

    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    const auto baselineResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, lessonTemplate.samplingDebugger, fft);
    session.observeSamplingDebugger(
        detectorResult, lessonTemplate.samplingDebugger, baselineResult);
    auto filteredConfig = lessonTemplate.samplingDebugger;
    filteredConfig.fourFFilterKind
        = holobench::compute::fourier::CircularFilterKind::LowPass;
    const auto filteredResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, filteredConfig, fft);
    session.observeSamplingDebugger(
        detectorResult, filteredConfig, filteredResult);
    CHECK_FALSE(session.confirmSpatialFilteringEffect(
        lessons::SpatialFilteringEffect::Sharper));
    CHECK(session.confirmSpatialFilteringEffect(
        lessons::SpatialFilteringEffect::SmootherBlurred));
}

TEST_CASE("NA PSF workflow requires a measured narrower shared PSF") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeLesson(session.catalog(), progress, "reflection_refraction");
    completeLesson(session.catalog(), progress, "thin_lens");
    completeLesson(session.catalog(), progress, "diffraction");
    session.replaceProgress(progress);
    session.beginLesson("na_psf");
    session.confirmTemplateLoaded();

    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeFourierLessonTemplate();
    const auto detectorResult = holobench::app::wave::simulateDetectorField(
        lessonTemplate.waveDetector, fft);
    const auto baselineResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, lessonTemplate.samplingDebugger, fft);
    session.observeSamplingDebugger(
        detectorResult, lessonTemplate.samplingDebugger, baselineResult);
    auto changedConfig = lessonTemplate.samplingDebugger;
    changedConfig.psfPupilRadiusMetres *= 1.5;
    const auto changedResult
        = holobench::app::samplingdebug::analyzeSamplingDebugger(
            detectorResult.field, changedConfig, fft);
    session.observeSamplingDebugger(detectorResult, changedConfig, changedResult);
    CHECK_FALSE(session.confirmPsfWidthChange(lessons::PsfWidthChange::Wider));
    CHECK(session.confirmPsfWidthChange(lessons::PsfWidthChange::Narrower));
}

TEST_CASE("coherence workflow requires shared visibility loss and correct classification") {
    lessons::LearnSession session;
    lessons::LessonProgress progress;
    completeLesson(session.catalog(), progress, "reflection_refraction");
    completeLesson(session.catalog(), progress, "diffraction");
    session.replaceProgress(progress);
    session.beginLesson("coherence_interference");
    session.confirmTemplateLoaded();

    holobench::compute::fft::CpuFftBackend fft;
    const auto lessonTemplate = lessons::makeCoherenceLessonTemplate();
    const auto baselineResult
        = holobench::app::slmexperiment::runSlmInterferenceExperiment(
            lessonTemplate, fft);
    session.observeSlmInterference(lessonTemplate, baselineResult);
    auto changedConfig = lessonTemplate;
    changedConfig.mutualCoherence.opticalPathDifferenceMetres
        = changedConfig.mutualCoherence.coherenceLengthMetres;
    const auto changedResult
        = holobench::app::slmexperiment::runSlmInterferenceExperiment(
            changedConfig, fft);
    session.observeSlmInterference(changedConfig, changedResult);
    CHECK_FALSE(session.confirmFringeVisibilityChange(
        lessons::FringeVisibilityChange::Higher));
    CHECK(session.confirmFringeVisibilityChange(
        lessons::FringeVisibilityChange::Lower));
}

} // TEST_SUITE("LearnSession")
