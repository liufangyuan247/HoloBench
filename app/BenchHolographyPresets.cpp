#include "app/BenchHolographyPresets.hpp"

#include <array>
#include <cmath>
#include <string>
#include <utility>
#include <variant>

namespace holobench::app {
namespace {

namespace bench = optics::scene;

math::RigidTransform3d aimedTransform(
    math::Vec3d positionMetres,
    math::Vec3d targetMetres) {
    const math::Vec3d zAxis = math::normalized(targetMetres - positionMetres);
    const math::Vec3d referenceAxis
        = std::abs(math::dot(zAxis, {0.0, 1.0, 0.0})) < 0.999
        ? math::Vec3d {0.0, 1.0, 0.0}
        : math::Vec3d {1.0, 0.0, 0.0};
    const math::Vec3d xAxis = math::normalized(math::cross(referenceAxis, zAxis));
    return {
        .translationMetres = positionMetres,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = math::cross(zAxis, xAxis),
        .localZAxisInWorld = zAxis,
    };
}

bench::BenchComponent objectSource(
    std::string id,
    math::Vec3d position,
    double wavelengthMetres,
    double powerWatts,
    std::string coherenceId) {
    auto result = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::ObjectWavefrontSource, std::move(id));
    result.transform = aimedTransform(position, {0.0, 0.0, 0.0});
    auto parameters = std::get<bench::ObjectWavefrontSourceParameters>(
        result.parameters);
    parameters.widthMetres = 0.01;
    parameters.heightMetres = 0.01;
    parameters.channel = {
        .wavelengthMetres = wavelengthMetres,
        .powerWatts = powerWatts,
        .coherenceId = std::move(coherenceId),
    };
    result.parameters = std::move(parameters);
    return result;
}

bench::BenchComponent referenceSource(
    std::string id,
    math::Vec3d position,
    double wavelengthMetres,
    double powerWatts,
    std::string coherenceId) {
    auto result = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::LaserSource, std::move(id));
    result.transform = aimedTransform(position, {0.0, 0.0, 0.0});
    auto parameters = std::get<bench::LaserSourceParameters>(result.parameters);
    parameters.beamRadiusMetres = 0.004;
    parameters.channels = {{
        .wavelengthMetres = wavelengthMetres,
        .powerWatts = powerWatts,
        .coherenceId = std::move(coherenceId),
    }};
    result.parameters = std::move(parameters);
    return result;
}

bench::BenchComponent rgbReferenceSource(
    std::string id,
    math::Vec3d position) {
    auto result = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::LaserSource, std::move(id));
    result.transform = aimedTransform(position, {0.0, 0.0, 0.0});
    auto parameters = std::get<bench::LaserSourceParameters>(result.parameters);
    parameters.beamRadiusMetres = 0.006;
    parameters.channels = {
        {.wavelengthMetres = 638e-9, .powerWatts = 0.20,
            .coherenceId = "red-recording"},
        {.wavelengthMetres = 532e-9, .powerWatts = 0.20,
            .coherenceId = "green-recording"},
        {.wavelengthMetres = 450e-9, .powerWatts = 0.20,
            .coherenceId = "blue-recording"},
    };
    result.parameters = std::move(parameters);
    return result;
}

bench::BenchComponent plate() {
    auto result = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::HolographicPlate, "plate-h1");
    auto parameters = std::get<bench::HolographicPlateParameters>(
        result.parameters);
    parameters.widthMetres = 0.05;
    parameters.heightMetres = 0.05;
    parameters.thicknessMetres = 20e-6;
    result.parameters = parameters;
    return result;
}

bench::BenchComponent transmittedScreen() {
    auto result = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::ScreenDetector, "reconstruction-screen");
    result.transform.translationMetres = {0.0, 0.0, 0.03};
    auto parameters = std::get<bench::ScreenDetectorParameters>(result.parameters);
    parameters.widthMetres = 0.05;
    parameters.heightMetres = 0.05;
    parameters.sampleWidth = 512;
    parameters.sampleHeight = 512;
    result.parameters = parameters;
    return result;
}

BenchProject baseProject(std::string id, std::string name) {
    BenchProject result;
    result.projectId = std::move(id);
    result.name = std::move(name);
    return result;
}

} // namespace

BenchProject makeTransmissionHolographyPreset() {
    auto result = baseProject(
        "preset-transmission-holography",
        "Transmission Holography Bench");
    result.scene.add(objectSource(
        "object-green", {-0.002, 0.0, -0.2}, 532e-9, 0.20, "green-recording"));
    result.scene.add(referenceSource(
        "reference-green", {0.004, 0.0, -0.2}, 532e-9, 0.30, "green-recording"));
    result.scene.add(plate());
    result.scene.add(transmittedScreen());
    return result;
}

BenchProject makeReflectionHolographyPreset() {
    auto result = baseProject(
        "preset-reflection-denisyuk-holography",
        "Reflection / Denisyuk Holography Bench");
    result.scene.add(objectSource(
        "object-green", {0.0, 0.0, 0.15}, 532e-9, 0.20, "green-recording"));
    result.scene.add(referenceSource(
        "reference-green", {0.003, 0.0, -0.15}, 532e-9, 0.30, "green-recording"));
    auto reflectionPlate = plate();
    auto plateParameters = std::get<bench::HolographicPlateParameters>(
        reflectionPlate.parameters);
    plateParameters.thicknessMetres = 30e-6;
    reflectionPlate.parameters = plateParameters;
    result.scene.add(std::move(reflectionPlate));
    auto probe = bench::makeDefaultBenchComponent(
        bench::BenchComponentKind::FieldProbe, "reflection-reconstruction-probe");
    probe.transform.translationMetres = {0.0, 0.0, -0.03};
    auto probeParameters = std::get<bench::FieldProbeParameters>(probe.parameters);
    probeParameters.widthMetres = 0.05;
    probeParameters.heightMetres = 0.05;
    probe.parameters = probeParameters;
    result.scene.add(std::move(probe));
    return result;
}

BenchProject makeRgbHolographyPreset() {
    auto result = baseProject(
        "preset-rgb-full-colour-holography",
        "RGB Full-colour Holography Bench");
    struct Channel final {
        const char* name;
        double wavelengthMetres;
        double yMetres;
    };
    constexpr std::array<Channel, 3> channels {{
        {"red", 638e-9, -0.004},
        {"green", 532e-9, 0.0},
        {"blue", 450e-9, 0.004},
    }};
    for (const auto& channel : channels) {
        const std::string coherence = std::string(channel.name) + "-recording";
        result.scene.add(objectSource(
            std::string("object-") + channel.name,
            {-0.002, channel.yMetres, -0.2},
            channel.wavelengthMetres,
            0.15,
            coherence));
        result.scene.add(referenceSource(
            std::string("reference-") + channel.name,
            {0.002, channel.yMetres, -0.2},
            channel.wavelengthMetres,
            0.20,
            coherence));
    }
    result.scene.add(plate());
    result.scene.add(transmittedScreen());
    return result;
}

BenchProject makeRgbDenisyukHolographyPreset() {
    auto result = baseProject(
        "preset-rgb-denisyuk-holography",
        "RGB Reflection / Denisyuk Holography Bench");
    struct Channel final {
        const char* name;
        double wavelengthMetres;
        double yMetres;
    };
    constexpr std::array<Channel, 3> channels {{
        {"red", 638e-9, -0.004},
        {"green", 532e-9, 0.0},
        {"blue", 450e-9, 0.004},
    }};
    for (const auto& channel : channels) {
        const std::string coherence
            = std::string(channel.name) + "-recording";
        const math::Vec3d objectPosition {
            0.003, channel.yMetres, 0.16};
        result.scene.add(objectSource(
            std::string("object-") + channel.name,
            objectPosition,
            channel.wavelengthMetres,
            0.16,
            coherence));

        auto slm = bench::makeDefaultBenchComponent(
            bench::BenchComponentKind::SpatialLightModulator,
            std::string("object-pattern-") + channel.name);
        slm.transform = aimedTransform(
            objectPosition * 0.55, {0.0, 0.0, 0.0});
        auto slmParameters
            = std::get<bench::SpatialLightModulatorParameters>(
                slm.parameters);
        slmParameters.widthMetres = 0.012;
        slmParameters.heightMetres = 0.012;
        slmParameters.modulationMode = bench::SlmModulationMode::Amplitude;
        slmParameters.commandPattern = bench::SlmCommandPattern::Checkerboard;
        slmParameters.primaryCommand = 0.25;
        slmParameters.secondaryCommand = 1.0;
        slmParameters.checkerboardCellWidthPixels = 96U;
        slmParameters.checkerboardCellHeightPixels = 96U;
        slm.parameters = slmParameters;
        result.scene.add(std::move(slm));
    }
    result.scene.add(rgbReferenceSource(
        "rgb-replay-reference", {-0.003, 0.0, -0.16}));
    auto rgbPlate = plate();
    auto plateParameters = std::get<bench::HolographicPlateParameters>(
        rgbPlate.parameters);
    plateParameters.thicknessMetres = 30e-6;
    rgbPlate.parameters = plateParameters;
    result.scene.add(std::move(rgbPlate));
    return result;
}

} // namespace holobench::app
