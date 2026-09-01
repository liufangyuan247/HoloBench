#include "app/BenchWavePresets.hpp"

#include <string>
#include <utility>

namespace holobench::app {
namespace {

namespace scene = optics::scene;

void mountOnOpticalTable(scene::BenchComponent& component) {
    const scene::MechanicalAssemblyState nominal;
    component.transform.translationMetres
        = component.transform.translationMetres
        + component.transform.localYAxisInWorld * nominal.postHeightMetres;
    scene::applyMechanicalAssembly(
        component, scene::makeDefaultMechanicalAssembly(component));
}

BenchProject makeBaseWaveBench(std::string id, std::string name) {
    BenchProject result;
    result.projectId = std::move(id);
    result.name = std::move(name);

    auto laser = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::LaserSource, "wave-laser-green");
    laser.transform.translationMetres = {0.0, 0.0, -0.10};
    auto laserParameters = std::get<scene::LaserSourceParameters>(
        laser.parameters);
    laserParameters.profile = scene::LaserBeamProfile::Gaussian;
    laserParameters.beamRadiusMetres = 0.004;
    laserParameters.channels = {{
        .wavelengthMetres = 532e-9,
        .powerWatts = 0.050,
        .coherenceId = "wave-green",
    }};
    laser.parameters = laserParameters;
    mountOnOpticalTable(laser);
    result.scene.add(std::move(laser));

    auto screen = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::ScreenDetector, "wave-screen");
    screen.transform.translationMetres = {0.0, 0.0, 0.50};
    auto screenParameters = std::get<scene::ScreenDetectorParameters>(
        screen.parameters);
    screenParameters.widthMetres = 0.012;
    screenParameters.heightMetres = 0.008;
    screenParameters.sampleWidth = 512U;
    screenParameters.sampleHeight = 512U;
    screen.parameters = screenParameters;
    mountOnOpticalTable(screen);
    result.scene.add(std::move(screen));
    return result;
}

scene::BenchComponent makeAperture() {
    auto aperture = scene::makeDefaultBenchComponent(
        scene::BenchComponentKind::Aperture, "wave-aperture");
    aperture.transform.translationMetres = {0.0, 0.0, 0.0};
    mountOnOpticalTable(aperture);
    return aperture;
}

} // namespace

BenchProject makeDoubleSlitExperimentPreset() {
    auto result = makeBaseWaveBench(
        "preset-double-slit", "Double-slit Interference Bench");
    auto aperture = makeAperture();
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::DoubleSlit;
    parameters.widthMetres = 0.006;
    parameters.heightMetres = 0.006;
    parameters.slitWidthMetres = 0.10e-3;
    parameters.slitHeightMetres = 4.0e-3;
    parameters.slitSeparationMetres = 0.50e-3;
    aperture.parameters = parameters;
    result.scene.add(std::move(aperture));
    return result;
}

BenchProject makeSingleSlitDiffractionPreset() {
    auto result = makeBaseWaveBench(
        "preset-single-slit", "Single-slit Diffraction Bench");
    auto aperture = makeAperture();
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::Rectangular;
    parameters.widthMetres = 0.10e-3;
    parameters.heightMetres = 4.0e-3;
    aperture.parameters = parameters;
    result.scene.add(std::move(aperture));
    return result;
}

BenchProject makeCircularDiffractionPreset() {
    auto result = makeBaseWaveBench(
        "preset-circular-diffraction", "Circular-aperture Diffraction Bench");
    auto aperture = makeAperture();
    auto parameters = std::get<scene::ApertureParameters>(
        aperture.parameters);
    parameters.shape = scene::ApertureShape::Circular;
    parameters.widthMetres = 0.25e-3;
    parameters.heightMetres = 0.25e-3;
    aperture.parameters = parameters;
    result.scene.add(std::move(aperture));
    return result;
}

} // namespace holobench::app
