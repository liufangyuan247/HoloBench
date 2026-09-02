#include <doctest/doctest.h>

#include <cmath>
#include <limits>
#include <numeric>

#include "app/ChimeraCameraImage.hpp"
#include "app/DetectorResponseAssets.hpp"

namespace app = holobench::app;
namespace chimera = holobench::app::chimera;
namespace material = holobench::optics::material;
namespace ray = holobench::optics::ray;
namespace scene = holobench::optics::scene;
namespace sensor = holobench::optics::sensor;

namespace {

sensor::CalibratedCameraSpectralResponse makeRgbCameraResponse() {
    return {"measured-rgb-camera-2", {
        {450e-9, {0.1, 0.2, 0.8}},
        {532e-9, {0.1, 0.9, 0.1}},
        {638e-9, {0.8, 0.1, 0.05}},
    }};
}

class CameraCoatingResolver final
    : public material::ICoatingResponseResolver {
public:
    CameraCoatingResolver()
        : response_(
            "camera-route-coating",
            {450e-9, 650e-9},
            {0.0, 0.25},
            std::vector<material::CoatingPowerResponse>(
                4U,
                {.powerReflectivity = 0.10,
                    .powerTransmissivity = 0.80})) {}

    const material::CalibratedCoatingResponse* resolveCoatingResponse(
        std::string_view calibrationId) const noexcept override {
        return calibrationId == response_.calibrationId()
            ? &response_ : nullptr;
    }

private:
    material::CalibratedCoatingResponse response_;
};

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

TEST_CASE("placed CHIMERA camera traces RGB through the editable prescription") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    const auto bench = chimera::compileChimeraRecipe(recipe).project;
    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::CameraSensorRequest request;
    request.jobId = "placed-prescription-camera";
    request.pixelWidth = 65U;
    request.pixelHeight = 65U;

    const auto image = chimera::synthesizePlacedCameraImage(
        recipe,
        reconstruction,
        request,
        makeRgbCameraResponse(),
        bench.scene,
        "chimera-plate",
        "chimera-camera-lens",
        "chimera-reconstruction-probe",
        prescriptions);
    CHECK(image.usedPlacedSequentialLens);
    CHECK(image.usedCoherentPrescriptionPsf);
    CHECK(image.sourceSceneRevision == bench.scene.revision());
    CHECK_FALSE(image.isStaleFor(bench.scene));
    CHECK(image.lensComponentId == "chimera-camera-lens");
    CHECK(image.lensPrescriptionId == "default_n_bk7_biconvex");
    CHECK(image.placedClearApertureDiameterMetres
        == doctest::Approx(0.01));
    CHECK(image.rgbEffectiveFocalLengthMetres[0]
        > image.rgbEffectiveFocalLengthMetres[1]);
    CHECK(image.rgbEffectiveFocalLengthMetres[1]
        > image.rgbEffectiveFocalLengthMetres[2]);
    CHECK(image.metrics.prescriptionTraceCompletedCount == 3U);
    CHECK(image.metrics.prescriptionTraceRejectedCount == 0U);
    CHECK(image.metrics.pupilRayTraceCount
        == 3U * chimera::kPlacedCameraPupilRayCount);
    CHECK(image.metrics.pupilRayTraceCompletedCount > 3U);
    CHECK(image.metrics.pupilRayTraceCompletedCount
            + image.metrics.pupilRayTraceRejectedCount
        == image.metrics.pupilRayTraceCount);
    CHECK(image.metrics.pupilRaySensorHitCount > 3U);
    CHECK(image.metrics.maximumGeometricRmsRadiusMetres > 0.0);
    REQUIRE(image.contributions.size() == 1U);
    REQUIRE(image.contributions.front().spectralRays.size() == 3U);
    for (const auto& spectral : image.contributions.front().spectralRays) {
        CHECK(spectral.prescriptionTraceStatus == "completed");
        CHECK(spectral.prescriptionSurfaceCount == 2U);
        CHECK(spectral.enteredPupil);
        CHECK(spectral.intersectedSensorPlane);
        CHECK(spectral.depositedOnSensor);
        CHECK(spectral.pupilRayCount
            == chimera::kPlacedCameraPupilRayCount);
        CHECK(spectral.pupilRayCompletedCount > 1U);
        CHECK(spectral.pupilRayCompletedCount
                + spectral.pupilRayRejectedCount
            == spectral.pupilRayCount);
        CHECK(spectral.pupilRaySensorHitCount > 1U);
        CHECK(spectral.geometricRmsRadiusMetres > 0.0);
        CHECK(spectral.usedCoherentPupilPsf);
        CHECK(spectral.wavefrontRmsOpticalPathDifferenceMetres >= 0.0);
        CHECK(spectral.wavefrontPeakToValleyOpticalPathDifferenceMetres
            >= spectral.wavefrontRmsOpticalPathDifferenceMetres);
    }
    CHECK(image.metrics.maximumWavefrontPeakToValleyOpticalPathDifferenceMetres
        > 0.0);
    CHECK(image.metrics.sensorDepositedSampleCount == 1U);
    auto edited = bench.scene;
    auto sensorPlane = *edited.find("chimera-reconstruction-probe");
    sensorPlane.transform.translationMetres.z -= 1e-3;
    edited.replace("chimera-reconstruction-probe", std::move(sensorPlane));
    CHECK(image.isStaleFor(edited));
}

TEST_CASE("placed camera sensor motion produces prescription defocus") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    const auto nominal = chimera::compileChimeraRecipe(recipe).project;
    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::CameraSensorRequest request;
    request.jobId = "placed-camera-defocus";
    request.pixelWidth = 129U;
    request.pixelHeight = 129U;
    const auto focused = chimera::synthesizePlacedCameraImage(
        recipe,
        reconstruction,
        request,
        makeRgbCameraResponse(),
        nominal.scene,
        "chimera-plate",
        "chimera-camera-lens",
        "chimera-reconstruction-probe",
        prescriptions);

    auto defocusedBench = nominal.scene;
    auto sensor = *defocusedBench.find("chimera-reconstruction-probe");
    sensor.transform.translationMetres.z -= 0.01;
    defocusedBench.replace("chimera-reconstruction-probe", std::move(sensor));
    request.jobId = "placed-camera-defocus-moved";
    const auto defocused = chimera::synthesizePlacedCameraImage(
        recipe,
        reconstruction,
        request,
        makeRgbCameraResponse(),
        defocusedBench,
        "chimera-plate",
        "chimera-camera-lens",
        "chimera-reconstruction-probe",
        prescriptions);

    REQUIRE(focused.contributions.size() == 1U);
    REQUIRE(defocused.contributions.size() == 1U);
    REQUIRE(focused.contributions.front().spectralRays.size() == 3U);
    REQUIRE(defocused.contributions.front().spectralRays.size() == 3U);
    const auto& focusedGreen
        = focused.contributions.front().spectralRays[1U];
    const auto& defocusedGreen
        = defocused.contributions.front().spectralRays[1U];
    CHECK(defocusedGreen.geometricRmsRadiusMetres
        > focusedGreen.geometricRmsRadiusMetres);
    CHECK(defocused.metrics.maximumGeometricRmsRadiusMetres
        > focused.metrics.maximumGeometricRmsRadiusMetres);
    CHECK(std::abs(
        defocused.metrics.maximumWavefrontRmsOpticalPathDifferenceMetres
        - focused.metrics.maximumWavefrontRmsOpticalPathDifferenceMetres)
        > 1e-9);
    CHECK(defocused.rgbSensorAxialDistanceMetres[1]
        == doctest::Approx(focused.rgbSensorAxialDistanceMetres[1] + 0.01));
}

TEST_CASE("placed physical detector applies its verified spectral response") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    auto bench = chimera::compileChimeraRecipe(recipe).project.scene;
    auto detector = *bench.find("chimera-reconstruction-probe");
    const auto probe = std::get<scene::FieldProbeParameters>(
        detector.parameters);
    detector.kind = scene::BenchComponentKind::ScreenDetector;
    detector.instrument = scene::makeDefaultInstrumentIdentity(
        scene::BenchComponentKind::ScreenDetector);
    detector.parameters = scene::ScreenDetectorParameters {
        .widthMetres = probe.widthMetres,
        .heightMetres = probe.heightMetres,
        .sampleWidth = probe.sampleWidth,
        .sampleHeight = probe.sampleHeight,
    };
    app::LoadedDetectorResponseAsset asset {
        .response = {"placed-measured-detector", {
            {450e-9, {0.02, 0.03, 0.70}},
            {532e-9, {0.04, 0.75, 0.05}},
            {638e-9, {0.80, 0.06, 0.01}},
        }},
        .provenance = {
            .formatVersion
                = sensor::kCameraSpectralResponseFormatVersion,
            .source = "placed-detector.json",
            .contentSha256 = std::string(64U, 'a'),
        },
    };
    app::bindDetectorResponseAsset(
        detector,
        asset,
        {
            .minimumVacuumWavelengthMetres = 450e-9,
            .maximumVacuumWavelengthMetres = 638e-9,
            .minimumTemperatureKelvin = 285.0,
            .maximumTemperatureKelvin = 305.0,
        });
    bench.replace(detector.id, detector);
    app::DetectorResponseCatalog detectorResponses;
    detectorResponses.registerResponse(
        asset.response, asset.provenance);
    const std::array wavelengths {450e-9, 532e-9, 638e-9};
    const auto selected = app::selectPlacedDetectorResponse(
        bench,
        detector.id,
        detectorResponses,
        makeRgbCameraResponse(),
        wavelengths,
        293.15);
    REQUIRE(selected.response != nullptr);

    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::CameraSensorRequest request;
    request.jobId = "placed-calibrated-detector";
    request.pixelWidth = 65U;
    request.pixelHeight = 65U;
    auto image = chimera::synthesizePlacedCameraImage(
        recipe,
        reconstruction,
        request,
        *selected.response,
        bench,
        "chimera-plate",
        "chimera-camera-lens",
        detector.id,
        prescriptions);
    app::applyPlacedDetectorResponseEvidence(image, selected);

    CHECK(image.cameraCalibrationId == "placed-measured-detector");
    CHECK(image.usedPlacedDetectorCalibration);
    CHECK(image.detectorResponseContentSha256
        == asset.provenance.contentSha256);
    CHECK(image.detectorResponseTemperatureKelvin
        == doctest::Approx(293.15));
    CHECK(image.metrics.sensorDepositedSampleCount == 1U);
    CHECK(image.metrics.totalIdealSensorSignal.red > 0.0);
    CHECK(image.metrics.totalIdealSensorSignal.green > 0.0);
    CHECK(image.metrics.totalIdealSensorSignal.blue > 0.0);
}

TEST_CASE("moving the placed camera lens clips the same directional evidence") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    auto bench = chimera::compileChimeraRecipe(recipe).project;
    auto lens = *bench.scene.find("chimera-camera-lens");
    lens.transform.translationMetres.x += 0.02;
    auto edited = bench.scene;
    edited.replace("chimera-camera-lens", std::move(lens));
    bench.scene = std::move(edited);
    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::CameraSensorRequest request;
    request.jobId = "decentred-prescription-camera";
    request.pixelWidth = 65U;
    request.pixelHeight = 65U;

    const auto image = chimera::synthesizePlacedCameraImage(
        recipe,
        reconstruction,
        request,
        makeRgbCameraResponse(),
        bench.scene,
        "chimera-plate",
        "chimera-camera-lens",
        "chimera-reconstruction-probe",
        prescriptions);
    CHECK(image.sourceSceneRevision == bench.scene.revision());
    CHECK(image.metrics.prescriptionTraceCompletedCount == 0U);
    CHECK(image.metrics.prescriptionTraceRejectedCount == 3U);
    CHECK(image.metrics.pupilAcceptedSampleCount == 0U);
    CHECK(image.metrics.pupilRejectedSampleCount == 1U);
    CHECK(image.metrics.sensorDepositedSampleCount == 0U);
    CHECK(sumImage(image) == chimera::LinearRgb {});
}

TEST_CASE("placed CHIMERA camera rejects an unmodelled intervening Bench optic") {
    const auto recipe = chimera::makeCanonicalChimeraRecipe();
    auto reconstruction = makeDirectionalEvidence(recipe);
    reconstruction.samples.resize(1U);
    auto bench = chimera::compileChimeraRecipe(recipe).project;
    auto splitter = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::BeamSplitterCombiner,
        "camera-intervening-splitter");
    splitter.transform.translationMetres = {0.0, 0.0, -0.025};
    splitter.instrument.calibrationMode
        = scene::InstrumentCalibrationMode::Calibrated;
    splitter.instrument.calibrationAssets.push_back({
        .kind = scene::CalibrationAssetKind::CoatingResponse,
        .calibrationId = "camera-route-coating",
        .formatVersion = material::kCoatingResponseFormatVersion,
        .source = "camera-route-coating.json",
        .contentSha256 = std::string(64U, 'a'),
        .specificationId = splitter.instrument.specificationId,
        .specificationVersion = splitter.instrument.specificationVersion,
        .validity = {
            .minimumVacuumWavelengthMetres = 450e-9,
            .maximumVacuumWavelengthMetres = 650e-9,
            .minimumTemperatureKelvin = 290.0,
            .maximumTemperatureKelvin = 300.0,
        },
    });
    bench.scene.add(std::move(splitter));
    const ray::LensPrescriptionCatalog prescriptions({
        ray::makeDefaultNBk7BiconvexPrescription(),
    });
    chimera::CameraSensorRequest request;
    request.jobId = "intervening-optic-camera";
    request.pixelWidth = 65U;
    request.pixelHeight = 65U;

    CHECK_THROWS_WITH_AS(
        static_cast<void>(chimera::synthesizePlacedCameraImage(
            recipe,
            reconstruction,
            request,
            makeRgbCameraResponse(),
            bench.scene,
            "chimera-plate",
            "chimera-camera-lens",
            "chimera-reconstruction-probe",
            prescriptions)),
        doctest::Contains("unresolved, stale, or inapplicable"),
        std::invalid_argument);
    const CameraCoatingResolver coatingResolver;
    CHECK_THROWS_WITH_AS(
        static_cast<void>(chimera::synthesizePlacedCameraImage(
            recipe,
            reconstruction,
            request,
            makeRgbCameraResponse(),
            bench.scene,
            "chimera-plate",
            "chimera-camera-lens",
            "chimera-reconstruction-probe",
            prescriptions,
            &coatingResolver,
            293.15)),
        doctest::Contains("additional placed optics"),
        std::invalid_argument);
}
