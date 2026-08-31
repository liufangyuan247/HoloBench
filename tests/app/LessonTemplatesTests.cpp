#include <doctest/doctest.h>

#include <cmath>
#include <numbers>
#include <stdexcept>

#include "app/lessons/LessonTemplates.hpp"
#include "compute/fft/CpuFftBackend.hpp"
#include "core/project/ProjectDocument.hpp"
#include "optics/scene/SceneProjectAdapter.hpp"

namespace lessons = holobench::app::lessons;

TEST_SUITE("LessonTemplates") {

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

} // TEST_SUITE("LessonTemplates")
