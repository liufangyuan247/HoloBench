#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numeric>

#include "app/ChimeraCameraImage.hpp"

namespace chimera = holobench::app::chimera;
namespace sensor = holobench::optics::sensor;

namespace {

sensor::CalibratedCameraSpectralResponse makeRgbCameraResponse() {
    return {"measured-rgb-camera-2", {
        {450e-9, {0.1, 0.2, 0.8}},
        {532e-9, {0.1, 0.9, 0.1}},
        {638e-9, {0.8, 0.1, 0.05}},
    }};
}

chimera::ReconstructionResult makeDirectionalEvidence(
    const chimera::ChimeraRecipe& recipe) {
    chimera::ReconstructionResult result;
    result.jobId = "bounded-directional-camera-source";
    result.sourceRecipeId = recipe.recipeId;
    result.samples = {{
        .hogelX = 3U,
        .hogelY = 2U,
        .stageXMetres = 0.0,
        .stageYMetres = 0.0,
        .viewId = "accepted-centre-view",
        .requestedHorizontalAngleRadians = 0.0,
        .requestedVerticalAngleRadians = 0.0,
        .reconstructedHorizontalAngleRadians = 0.0,
        .reconstructedVerticalAngleRadians = 0.0,
        .horizontalAngleErrorRadians = 0.0,
        .verticalAngleErrorRadians = 0.0,
        .sourceLinearIntensity = {1.0, 2.0, 3.0},
        .reconstructedLinearIntensity = {1.0, 2.0, 3.0},
        .nearestViewSeparationRadians = 0.1,
        .worstDiffractionLimitedAngularResolutionRadians = 0.0,
        .worstNearestViewCrosstalkFraction = 0.0,
        .nearestViewIsResolvable = true,
    }, {
        .hogelX = 3U,
        .hogelY = 2U,
        .stageXMetres = 0.0,
        .stageYMetres = 0.0,
        .viewId = "rejected-side-view",
        .requestedHorizontalAngleRadians = 0.10,
        .requestedVerticalAngleRadians = 0.0,
        .reconstructedHorizontalAngleRadians = 0.10,
        .reconstructedVerticalAngleRadians = 0.0,
        .horizontalAngleErrorRadians = 0.0,
        .verticalAngleErrorRadians = 0.0,
        .sourceLinearIntensity = {4.0, 5.0, 6.0},
        .reconstructedLinearIntensity = {4.0, 5.0, 6.0},
        .nearestViewSeparationRadians = 0.1,
        .worstDiffractionLimitedAngularResolutionRadians = 0.0,
        .worstNearestViewCrosstalkFraction = 0.0,
        .nearestViewIsResolvable = true,
    }};
    return result;
}

chimera::LinearRgb sumImage(const chimera::CameraImageResult& image) {
    return std::accumulate(
        image.rowMajorLinearSensorSignal.begin(),
        image.rowMajorLinearSensorSignal.end(),
        chimera::LinearRgb {},
        [](chimera::LinearRgb sum, const chimera::LinearRgb& pixel) {
            sum.red += pixel.red;
            sum.green += pixel.green;
            sum.blue += pixel.blue;
            return sum;
        });
}

} // namespace

TEST_CASE("finite pupil and measured spectral LUT compose a bounded camera image") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto reconstruction = makeDirectionalEvidence(recipe);
    const auto response = makeRgbCameraResponse();
    const chimera::CameraImageRequest request {
        .formatVersion = chimera::kCameraImageRequestFormatVersion,
        .jobId = "measured-pupil-camera-image",
        .pupilCentreXMetres = 0.0,
        .pupilCentreYMetres = 0.0,
        .pupilPlaneDistanceMetres = 0.10,
        .pupilDiameterMetres = 2e-3,
        .focalLengthMetres = 25e-3,
        .pixelWidth = 65U,
        .pixelHeight = 65U,
        .pixelPitchXMetres = 10e-6,
        .pixelPitchYMetres = 10e-6,
        .psfSupportFirstDarkRings = 4.0,
    };

    const auto image = chimera::synthesizeCameraImage(
        recipe, reconstruction, request, response);
    CHECK(image.cameraCalibrationId == "measured-rgb-camera-2");
    CHECK(image.rowMajorLinearSensorSignal.size() == 65U * 65U);
    REQUIRE(image.contributions.size() == 2U);
    CHECK(image.contributions[0].enteredPupil);
    CHECK(image.contributions[0].depositedOnSensor);
    CHECK_FALSE(image.contributions[1].enteredPupil);
    CHECK_FALSE(image.contributions[1].depositedOnSensor);
    CHECK(image.metrics.directionalSampleCount == 2U);
    CHECK(image.metrics.pupilAcceptedSampleCount == 1U);
    CHECK(image.metrics.pupilRejectedSampleCount == 1U);
    CHECK(image.metrics.sensorDepositedSampleCount == 1U);
    CHECK(image.metrics.sensorMissedSampleCount == 0U);
    CHECK(image.metrics.kernelEvaluationCount > 0U);
    CHECK(image.metrics.maximumFirstDarkRadiusMetres > 0.0);
    CHECK(image.metrics.rgbFirstDarkRadiusMetres[0]
        > image.metrics.rgbFirstDarkRadiusMetres[1]);
    CHECK(image.metrics.rgbFirstDarkRadiusMetres[1]
        > image.metrics.rgbFirstDarkRadiusMetres[2]);
    CHECK(image.pupilDiameterMetres == doctest::Approx(2e-3));
    CHECK(image.focalLengthMetres == doctest::Approx(25e-3));

    const chimera::LinearRgb expected {1.3, 2.5, 2.65};
    CHECK(image.contributions[0].idealSensorSignal.red
        == doctest::Approx(expected.red));
    CHECK(image.contributions[0].idealSensorSignal.green
        == doctest::Approx(expected.green));
    CHECK(image.contributions[0].idealSensorSignal.blue
        == doctest::Approx(expected.blue));
    const auto summed = sumImage(image);
    CHECK(summed.red == doctest::Approx(expected.red).epsilon(2e-12));
    CHECK(summed.green == doctest::Approx(expected.green).epsilon(2e-12));
    CHECK(summed.blue == doctest::Approx(expected.blue).epsilon(2e-12));
    CHECK(image.metrics.totalDepositedSensorSignal.red
        == doctest::Approx(summed.red));
    CHECK(image.at(32U, 32U).red > 0.0);
    CHECK_THROWS_AS(static_cast<void>(image.at(65U, 0U)), std::out_of_range);
}

TEST_CASE("camera image is deterministic and reports bounded sensor-edge loss") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    reconstruction.samples[0].reconstructedHorizontalAngleRadians = 0.0125;
    const auto response = makeRgbCameraResponse();
    chimera::CameraImageRequest request;
    request.jobId = "edge-loss-camera-image";
    request.pupilCentreXMetres = request.pupilPlaneDistanceMetres
        * std::tan(0.0125);
    request.pixelWidth = 65U;
    request.pixelHeight = 65U;
    request.pixelPitchXMetres = 10e-6;
    request.pixelPitchYMetres = 10e-6;

    const auto first = chimera::synthesizeCameraImage(
        recipe, reconstruction, request, response);
    const auto second = chimera::synthesizeCameraImage(
        recipe, reconstruction, request, response);
    CHECK(first == second);
    CHECK(first.metrics.pupilAcceptedSampleCount == 1U);
    CHECK(first.metrics.sensorDepositedSampleCount == 1U);
    CHECK(first.metrics.totalDepositedSensorSignal.red
        < first.metrics.totalIdealSensorSignal.red);
}

TEST_CASE("camera raster access rejects corrupt overflowing dimensions") {
    chimera::CameraImageResult corrupt;
    corrupt.pixelWidth = std::numeric_limits<std::size_t>::max();
    corrupt.pixelHeight = 2U;
    CHECK_THROWS_AS(
        static_cast<void>(corrupt.at(0U, 0U)), std::out_of_range);
}

TEST_CASE("camera composition rejects invalid provenance dimensions and work") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    const auto reconstruction = makeDirectionalEvidence(recipe);
    const auto response = makeRgbCameraResponse();
    chimera::CameraImageRequest request;

    request.pixelWidth = chimera::kMaximumCameraImagePixels;
    request.pixelHeight = 2U;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::synthesizeCameraImage(
            recipe, reconstruction, request, response)),
        std::invalid_argument);

    request = {};
    request.pupilDiameterMetres = 1e-9;
    CHECK_THROWS_AS(
        static_cast<void>(chimera::synthesizeCameraImage(
            recipe, reconstruction, request, response)),
        std::invalid_argument);

    request = {};
    auto wrong = reconstruction;
    wrong.sourceRecipeId = "different-recipe";
    CHECK_THROWS_AS(
        static_cast<void>(chimera::synthesizeCameraImage(
            recipe, wrong, request, response)),
        std::invalid_argument);
}
