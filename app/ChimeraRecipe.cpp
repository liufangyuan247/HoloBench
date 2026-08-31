#include "app/ChimeraRecipe.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <limits>
#include <numbers>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;
namespace scene = optics::scene;

void requireKeys(
    const Json& object,
    std::initializer_list<std::string_view> expected,
    std::string_view context) {
    if (!object.is_object() || object.size() != expected.size()) {
        throw std::runtime_error(
            std::string(context) + " has missing or unknown keys");
    }
    for (const auto key : expected) {
        if (!object.contains(key)) {
            throw std::runtime_error(
                std::string(context) + " has missing or unknown keys");
        }
    }
}

double finiteNumber(const Json& value, std::string_view field) {
    if (!value.is_number()) {
        throw std::runtime_error(std::string(field) + " must be numeric");
    }
    const double result = value.get<double>();
    if (!std::isfinite(result)) {
        throw std::runtime_error(std::string(field) + " must be finite");
    }
    return result;
}

std::size_t positiveSize(const Json& value, std::string_view field) {
    if (!value.is_number_unsigned() && !value.is_number_integer()) {
        throw std::runtime_error(std::string(field) + " must be an integer");
    }
    const auto result = value.get<std::int64_t>();
    if (result <= 0) {
        throw std::runtime_error(std::string(field) + " must be positive");
    }
    return static_cast<std::size_t>(result);
}

std::string requiredString(const Json& value, std::string_view field) {
    if (!value.is_string()) {
        throw std::runtime_error(std::string(field) + " must be a string");
    }
    return value.get<std::string>();
}

void requirePositive(double value, std::string_view field) {
    if (!std::isfinite(value) || value <= 0.0) {
        throw std::invalid_argument(
            std::string(field) + " must be finite and positive");
    }
}

math::RigidTransform3d frameWithZAxis(
    math::Vec3d position,
    math::Vec3d zAxis) {
    zAxis = math::normalized(zAxis);
    const math::Vec3d referenceAxis
        = std::abs(math::dot(zAxis, {0.0, 1.0, 0.0})) < 0.999
        ? math::Vec3d {0.0, 1.0, 0.0}
        : math::Vec3d {1.0, 0.0, 0.0};
    const math::Vec3d xAxis
        = math::normalized(math::cross(referenceAxis, zAxis));
    return {
        .translationMetres = position,
        .localXAxisInWorld = xAxis,
        .localYAxisInWorld = math::cross(zAxis, xAxis),
        .localZAxisInWorld = zAxis,
    };
}

math::RigidTransform3d aimedTransform(
    math::Vec3d position,
    math::Vec3d target) {
    return frameWithZAxis(position, target - position);
}

scene::BenchComponent component(
    scene::BenchComponentKind kind,
    std::string id,
    const math::RigidTransform3d& transform) {
    auto result = scene::makeDefaultBenchComponent(kind, std::move(id));
    result.transform = transform;
    return result;
}

std::string componentId(std::string_view role, std::string_view channel = {}) {
    std::string result = "chimera-";
    result += role;
    if (!channel.empty()) {
        result += "-";
        result += channel;
    }
    return result;
}

void addGenerated(
    CompileResult& result,
    scene::BenchComponent value,
    std::string role,
    std::string channel,
    const ChimeraRecipe& recipe) {
    result.generatedComponents.push_back({
        .componentId = value.id,
        .generatedRole = std::move(role),
        .channelId = std::move(channel),
        .recipeId = recipe.recipeId,
        .recipeVersion = recipe.formatVersion,
        .compilerVersion = kChimeraRecipeCompilerVersion,
    });
    result.project.scene.add(std::move(value));
}

Json armToJson(const SpectralArm& value) {
    return {
        {"channel_id", value.channelId},
        {"object_power_w", value.objectPowerWatts},
        {"reference_power_w", value.referencePowerWatts},
        {"wavelength_m", value.wavelengthMetres},
    };
}

SpectralArm armFromJson(const Json& value) {
    requireKeys(value,
        {"channel_id", "object_power_w", "reference_power_w", "wavelength_m"},
        "CHIMERA spectral arm");
    return {
        .channelId = requiredString(value.at("channel_id"), "channel_id"),
        .wavelengthMetres = finiteNumber(value.at("wavelength_m"), "wavelength_m"),
        .objectPowerWatts = finiteNumber(value.at("object_power_w"), "object_power_w"),
        .referencePowerWatts = finiteNumber(
            value.at("reference_power_w"), "reference_power_w"),
    };
}

} // namespace

bool CompileResult::feasible() const noexcept {
    return std::none_of(
        constraints.begin(),
        constraints.end(),
        [](const auto& entry) {
            return entry.severity == ConstraintSeverity::Unsupported;
        });
}

void validateChimeraRecipe(const ChimeraRecipe& recipe) {
    if (recipe.formatVersion != kChimeraRecipeFormatVersion) {
        throw std::invalid_argument("unsupported CHIMERA recipe format version");
    }
    if (!scene::isStableBenchId(recipe.recipeId)) {
        throw std::invalid_argument("CHIMERA recipe ID is not stable");
    }
    if (recipe.name.empty()) {
        throw std::invalid_argument("CHIMERA recipe name must not be empty");
    }
    requirePositive(recipe.hogels.pitchMetres, "hogel pitch_m");
    if (recipe.hogels.countX == 0U || recipe.hogels.countY == 0U
        || recipe.hogels.countX > 1'000'000U
        || recipe.hogels.countY > 1'000'000U) {
        throw std::invalid_argument("hogel counts must be in [1, 1000000]");
    }
    requirePositive(
        recipe.targetHorizontalFieldOfViewRadians,
        "target horizontal FOV_rad");
    requirePositive(
        recipe.targetVerticalFieldOfViewRadians,
        "target vertical FOV_rad");
    if (recipe.targetHorizontalFieldOfViewRadians >= std::numbers::pi_v<double>
        || recipe.targetVerticalFieldOfViewRadians
            >= std::numbers::pi_v<double>) {
        throw std::invalid_argument("target FOV_rad must be less than pi");
    }

    const std::array<std::string_view, 3> expectedIds {"red", "green", "blue"};
    std::set<double> wavelengths;
    for (std::size_t index = 0; index < recipe.rgb.size(); ++index) {
        const auto& arm = recipe.rgb[index];
        if (arm.channelId != expectedIds[index]) {
            throw std::invalid_argument(
                "RGB arms must be ordered red, green, blue");
        }
        requirePositive(arm.wavelengthMetres, "RGB wavelength_m");
        requirePositive(arm.objectPowerWatts, "RGB object power_w");
        requirePositive(arm.referencePowerWatts, "RGB reference power_w");
        if (!wavelengths.insert(arm.wavelengthMetres).second) {
            throw std::invalid_argument("RGB wavelengths must be distinct");
        }
    }
    if (!(recipe.rgb[0].wavelengthMetres > recipe.rgb[1].wavelengthMetres
            && recipe.rgb[1].wavelengthMetres
                > recipe.rgb[2].wavelengthMetres)) {
        throw std::invalid_argument(
            "RGB wavelengths must descend from red to blue");
    }

    requirePositive(recipe.slm.widthMetres, "SLM width_m");
    requirePositive(recipe.slm.heightMetres, "SLM height_m");
    if (recipe.slm.pixelWidth == 0U || recipe.slm.pixelHeight == 0U
        || recipe.slm.pixelWidth > 65'536U || recipe.slm.pixelHeight > 65'536U) {
        throw std::invalid_argument("SLM pixel dimensions must be in [1, 65536]");
    }
    if (!std::isfinite(recipe.slm.fillFactor)
        || recipe.slm.fillFactor <= 0.0 || recipe.slm.fillFactor > 1.0) {
        throw std::invalid_argument("SLM fill factor must be in (0, 1]");
    }
    if (recipe.slm.bitDepth == 0U || recipe.slm.bitDepth > 24U) {
        throw std::invalid_argument("SLM bit depth must be in [1, 24]");
    }
    requirePositive(recipe.slm.phaseRangeRadians, "SLM phase range_rad");

    requirePositive(recipe.relay.focalLengthMetres, "relay focal length_m");
    requirePositive(
        recipe.relay.clearApertureDiameterMetres,
        "relay clear aperture_m");
    requirePositive(recipe.relay.stopDiameterMetres, "relay stop diameter_m");
    if (recipe.relay.stopDiameterMetres
        > recipe.relay.clearApertureDiameterMetres) {
        throw std::invalid_argument(
            "relay stop diameter_m cannot exceed the lens clear aperture_m");
    }

    requirePositive(
        recipe.reference.sourceXMetres,
        "reference source x_m");
    requirePositive(
        recipe.reference.splitterDistanceAfterMirrorMetres,
        "reference splitter distance_m");
    requirePositive(
        recipe.reference.armSeparationMetres,
        "reference arm separation_m");
    if (!std::isfinite(recipe.reference.mirrorXMetres)
        || !std::isfinite(recipe.reference.mirrorZMetres)) {
        throw std::invalid_argument("reference mirror position_m must be finite");
    }
    if (!std::isfinite(recipe.reference.splitterPowerTransmission)
        || recipe.reference.splitterPowerTransmission <= 0.0
        || recipe.reference.splitterPowerTransmission > 1.0) {
        throw std::invalid_argument(
            "reference splitter transmission must be in (0, 1]");
    }

    requirePositive(recipe.plate.thicknessMetres, "plate thickness_m");
    requirePositive(
        recipe.plate.averageRefractiveIndex,
        "plate average refractive index");
    requirePositive(
        recipe.plate.refractiveIndexModulation,
        "plate refractive-index modulation");
    if (!std::isfinite(recipe.plate.isotropicLinearShrinkageFraction)
        || recipe.plate.isotropicLinearShrinkageFraction < 0.0
        || recipe.plate.isotropicLinearShrinkageFraction >= 1.0) {
        throw std::invalid_argument("plate shrinkage fraction must be in [0, 1)");
    }
    requirePositive(
        recipe.exposure.exposureSecondsPerChannel,
        "exposure seconds per channel");
    if (recipe.exposure.sampleWidth < 32U
        || recipe.exposure.sampleHeight < 32U
        || recipe.exposure.sampleWidth > 4096U
        || recipe.exposure.sampleHeight > 4096U) {
        throw std::invalid_argument(
            "exposure sample dimensions must be in [32, 4096]");
    }
}

ChimeraRecipe makeCanonicalChimeraRecipe() {
    ChimeraRecipe result;
    validateChimeraRecipe(result);
    return result;
}

CompileResult compileChimeraRecipe(const ChimeraRecipe& recipe) {
    validateChimeraRecipe(recipe);
    CompileResult result;
    result.project.projectId = "chimera-" + recipe.recipeId;
    result.project.name = recipe.name + " (editable generated bench)";
    constexpr std::array<double, 3> armMultipliers {-1.0, 0.0, 1.0};

    const double achievedHorizontalFov = 2.0 * std::atan(
        recipe.slm.widthMetres / (2.0 * recipe.relay.focalLengthMetres));
    const double achievedVerticalFov = 2.0 * std::atan(
        recipe.slm.heightMetres / (2.0 * recipe.relay.focalLengthMetres));
    const double requiredDiameter = 2.0 * recipe.relay.focalLengthMetres
        * std::tan(0.5 * std::max(
            recipe.targetHorizontalFieldOfViewRadians,
            recipe.targetVerticalFieldOfViewRadians));
    result.constraints.push_back({
        .severity = recipe.targetHorizontalFieldOfViewRadians
                <= achievedHorizontalFov
            ? ConstraintSeverity::Feasible
            : ConstraintSeverity::Unsupported,
        .code = "horizontal_fov",
        .message = "requested horizontal FOV_rad="
            + std::to_string(recipe.targetHorizontalFieldOfViewRadians)
            + ", ideal SLM/relay support_rad="
            + std::to_string(achievedHorizontalFov),
    });
    result.constraints.push_back({
        .severity = recipe.targetVerticalFieldOfViewRadians
                <= achievedVerticalFov
            ? ConstraintSeverity::Feasible
            : ConstraintSeverity::Unsupported,
        .code = "vertical_fov",
        .message = "requested vertical FOV_rad="
            + std::to_string(recipe.targetVerticalFieldOfViewRadians)
            + ", ideal SLM/relay support_rad="
            + std::to_string(achievedVerticalFov),
    });
    result.constraints.push_back({
        .severity = requiredDiameter <= recipe.relay.stopDiameterMetres
            ? ConstraintSeverity::Feasible
            : ConstraintSeverity::Unsupported,
        .code = "relay_stop_na",
        .message = "required relay stop diameter_m="
            + std::to_string(requiredDiameter) + ", configured_m="
            + std::to_string(recipe.relay.stopDiameterMetres),
    });
    const double requestedNa = std::sin(0.5 * std::max(
        recipe.targetHorizontalFieldOfViewRadians,
        recipe.targetVerticalFieldOfViewRadians));
    result.constraints.push_back({
        .severity = requestedNa <= 0.5
            ? ConstraintSeverity::Feasible
            : ConstraintSeverity::Warning,
        .code = "scalar_paraxial_na",
        .message = "requested external NA=" + std::to_string(requestedNa)
            + "; high-NA vector effects are not modelled",
    });
    result.constraints.push_back({
        .severity = ConstraintSeverity::Warning,
        .code = "material_model",
        .message = "reflection efficiency uses the scalar equivalent-symmetric "
            "Kogelnik model; arbitrary slant and polarization remain limited",
    });

    double maximumCarrierX = 0.0;
    double maximumCarrierY = 0.0;
    for (std::size_t index = 0; index < recipe.rgb.size(); ++index) {
        const math::Vec3d source {
            -0.03,
            armMultipliers[index] * recipe.reference.armSeparationMetres,
            0.30,
        };
        const math::Vec3d direction = math::normalized(math::Vec3d {} - source);
        maximumCarrierX = std::max(
            maximumCarrierX,
            std::abs(direction.x) / recipe.rgb[index].wavelengthMetres);
        maximumCarrierY = std::max(
            maximumCarrierY,
            std::abs(direction.y) / recipe.rgb[index].wavelengthMetres);
    }
    const double nyquistX = static_cast<double>(recipe.exposure.sampleWidth)
        / (2.0 * recipe.hogels.pitchMetres);
    const double nyquistY = static_cast<double>(recipe.exposure.sampleHeight)
        / (2.0 * recipe.hogels.pitchMetres);
    result.constraints.push_back({
        .severity = maximumCarrierX <= nyquistX
                && maximumCarrierY <= nyquistY
            ? ConstraintSeverity::Feasible
            : ConstraintSeverity::Unsupported,
        .code = "hogel_field_sampling",
        .message = "required object carrier_x/y_cpm="
            + std::to_string(maximumCarrierX) + "/"
            + std::to_string(maximumCarrierY) + ", Nyquist_x/y_cpm="
            + std::to_string(nyquistX) + "/" + std::to_string(nyquistY),
    });

    const double plateWidth = recipe.hogels.pitchMetres
        * static_cast<double>(recipe.hogels.countX);
    const double plateHeight = recipe.hogels.pitchMetres
        * static_cast<double>(recipe.hogels.countY);
    if (!std::isfinite(plateWidth) || !std::isfinite(plateHeight)) {
        throw std::overflow_error("compiled CHIMERA plate dimensions overflow");
    }

    auto plate = component(
        scene::BenchComponentKind::HolographicPlate,
        componentId("plate"),
        math::RigidTransform3d {});
    auto plateParameters
        = std::get<scene::HolographicPlateParameters>(plate.parameters);
    plateParameters.widthMetres = plateWidth;
    plateParameters.heightMetres = plateHeight;
    plateParameters.thicknessMetres = recipe.plate.thicknessMetres;
    plate.parameters = plateParameters;
    addGenerated(result, std::move(plate), "recording_plate", "", recipe);

    auto probe = component(
        scene::BenchComponentKind::FieldProbe,
        componentId("reconstruction-probe"),
        aimedTransform({0.0, 0.0, -0.03}, {0.0, 0.0, 0.0}));
    auto probeParameters = std::get<scene::FieldProbeParameters>(probe.parameters);
    probeParameters.widthMetres = std::max(0.05, 2.0 * plateWidth);
    probeParameters.heightMetres = std::max(0.05, 2.0 * plateHeight);
    probeParameters.sampleWidth = recipe.exposure.sampleWidth;
    probeParameters.sampleHeight = recipe.exposure.sampleHeight;
    probe.parameters = probeParameters;
    addGenerated(result, std::move(probe), "reconstruction_probe", "", recipe);

    for (std::size_t index = 0; index < recipe.rgb.size(); ++index) {
        const auto& arm = recipe.rgb[index];
        const double armY = armMultipliers[index]
            * recipe.reference.armSeparationMetres;
        const std::string coherence = "chimera-" + arm.channelId + "-recording";

        const math::Vec3d objectPosition {-0.03, armY, 0.30};
        auto object = component(
            scene::BenchComponentKind::ObjectWavefrontSource,
            componentId("object-source", arm.channelId),
            aimedTransform(objectPosition, {0.0, 0.0, 0.0}));
        auto objectParameters
            = std::get<scene::ObjectWavefrontSourceParameters>(object.parameters);
        objectParameters.channel = {
            .wavelengthMetres = arm.wavelengthMetres,
            .powerWatts = arm.objectPowerWatts,
            .coherenceId = coherence,
        };
        objectParameters.widthMetres = recipe.hogels.pitchMetres;
        objectParameters.heightMetres = recipe.hogels.pitchMetres;
        object.parameters = objectParameters;
        addGenerated(result, std::move(object), "object_source", arm.channelId, recipe);

        auto slm = component(
            scene::BenchComponentKind::SpatialLightModulator,
            componentId("slm", arm.channelId),
            aimedTransform(objectPosition * 0.80, {0.0, 0.0, 0.0}));
        auto slmParameters
            = std::get<scene::SpatialLightModulatorParameters>(slm.parameters);
        slmParameters.widthMetres = recipe.slm.widthMetres;
        slmParameters.heightMetres = recipe.slm.heightMetres;
        slmParameters.pixelWidth = recipe.slm.pixelWidth;
        slmParameters.pixelHeight = recipe.slm.pixelHeight;
        slmParameters.fillFactor = recipe.slm.fillFactor;
        slmParameters.bitDepth = recipe.slm.bitDepth;
        slmParameters.phaseRangeRadians = recipe.slm.phaseRangeRadians;
        slmParameters.commandOrigin = scene::SlmCommandOrigin::Automation;
        slmParameters.commandId = "chimera-" + recipe.recipeId + "-"
            + arm.channelId + "-hogel-pending";
        slm.parameters = slmParameters;
        addGenerated(result, std::move(slm), "object_slm", arm.channelId, recipe);

        auto lens = component(
            scene::BenchComponentKind::IdealThinLens,
            componentId("relay-lens", arm.channelId),
            aimedTransform(objectPosition * 0.60, {0.0, 0.0, 0.0}));
        auto lensParameters
            = std::get<scene::IdealThinLensParameters>(lens.parameters);
        lensParameters.focalLengthMetres = recipe.relay.focalLengthMetres;
        lensParameters.clearApertureDiameterMetres
            = recipe.relay.clearApertureDiameterMetres;
        lens.parameters = lensParameters;
        addGenerated(result, std::move(lens), "relay_lens", arm.channelId, recipe);

        auto aperture = component(
            scene::BenchComponentKind::Aperture,
            componentId("relay-stop", arm.channelId),
            aimedTransform(objectPosition * 0.40, {0.0, 0.0, 0.0}));
        auto apertureParameters
            = std::get<scene::ApertureParameters>(aperture.parameters);
        apertureParameters.widthMetres = recipe.relay.stopDiameterMetres;
        apertureParameters.heightMetres = recipe.relay.stopDiameterMetres;
        aperture.parameters = apertureParameters;
        addGenerated(result, std::move(aperture), "relay_stop", arm.channelId, recipe);

        const math::Vec3d mirrorPosition {
            recipe.reference.mirrorXMetres,
            armY,
            recipe.reference.mirrorZMetres,
        };
        const math::Vec3d referencePosition {
            recipe.reference.sourceXMetres,
            armY,
            recipe.reference.mirrorZMetres,
        };
        const math::Vec3d incoming = math::normalized(
            mirrorPosition - referencePosition);
        const math::Vec3d outgoing
            = math::normalized(math::Vec3d {} - mirrorPosition);
        const math::Vec3d mirrorNormal = math::normalized(incoming - outgoing);

        auto reference = component(
            scene::BenchComponentKind::LaserSource,
            componentId("reference-source", arm.channelId),
            aimedTransform(referencePosition, mirrorPosition));
        auto referenceParameters
            = std::get<scene::LaserSourceParameters>(reference.parameters);
        referenceParameters.beamRadiusMetres
            = std::max(0.002, 0.5 * recipe.hogels.pitchMetres);
        referenceParameters.channels = {{
            .wavelengthMetres = arm.wavelengthMetres,
            .powerWatts = arm.referencePowerWatts,
            .coherenceId = coherence,
        }};
        reference.parameters = referenceParameters;
        addGenerated(result, std::move(reference), "reference_source", arm.channelId, recipe);

        auto mirror = component(
            scene::BenchComponentKind::PlanarMirror,
            componentId("reference-mirror", arm.channelId),
            frameWithZAxis(mirrorPosition, mirrorNormal));
        auto mirrorParameters
            = std::get<scene::PlanarMirrorParameters>(mirror.parameters);
        mirrorParameters.widthMetres = 0.03;
        mirrorParameters.heightMetres = 0.03;
        mirror.parameters = mirrorParameters;
        addGenerated(result, std::move(mirror), "reference_fold_mirror", arm.channelId, recipe);

        const math::Vec3d splitterPosition = mirrorPosition + outgoing
            * recipe.reference.splitterDistanceAfterMirrorMetres;
        auto splitter = component(
            scene::BenchComponentKind::BeamSplitterCombiner,
            componentId("reference-splitter", arm.channelId),
            frameWithZAxis(splitterPosition, outgoing));
        auto splitterParameters
            = std::get<scene::BeamSplitterParameters>(splitter.parameters);
        splitterParameters.widthMetres = 0.025;
        splitterParameters.heightMetres = 0.025;
        splitterParameters.powerReflectivity = 0.0;
        splitterParameters.powerTransmissivity
            = recipe.reference.splitterPowerTransmission;
        splitter.parameters = splitterParameters;
        addGenerated(result, std::move(splitter), "reference_splitter", arm.channelId, recipe);

        HologramRecordingRecipe recordingRecipe;
        recordingRecipe.recipeId = "chimera-" + arm.channelId + "-volume";
        recordingRecipe.plateComponentId = componentId("plate");
        recordingRecipe.model = HologramRecordingModel::VolumeGrating;
        recordingRecipe.channels = {{
            .objectBranch = {
                .componentPath = {
                    componentId("object-source", arm.channelId),
                    componentId("slm", arm.channelId),
                    componentId("relay-lens", arm.channelId),
                    componentId("relay-stop", arm.channelId),
                    componentId("plate"),
                },
                .wavelengthMetres = arm.wavelengthMetres,
                .coherenceId = coherence,
            },
            .referenceBranch = {
                .componentPath = {
                    componentId("reference-source", arm.channelId),
                    componentId("reference-mirror", arm.channelId),
                    componentId("reference-splitter", arm.channelId),
                    componentId("reconstruction-probe"),
                    componentId("plate"),
                },
                .wavelengthMetres = arm.wavelengthMetres,
                .coherenceId = coherence,
            },
        }};
        recordingRecipe.sampling = {
            .sampleWidth = recipe.exposure.sampleWidth,
            .sampleHeight = recipe.exposure.sampleHeight,
            .refractiveIndex = 1.0,
            .extentWidthMetres = recipe.hogels.pitchMetres,
            .extentHeightMetres = recipe.hogels.pitchMetres,
        };
        recordingRecipe.volumeMaterial = {
            .averageRefractiveIndex = recipe.plate.averageRefractiveIndex,
            .refractiveIndexModulation = recipe.plate.refractiveIndexModulation,
            .isotropicLinearShrinkageFraction
                = recipe.plate.isotropicLinearShrinkageFraction,
        };
        result.project.recordingRecipes.push_back(std::move(recordingRecipe));
    }

    validateBenchProject(result.project);
    return result;
}

std::string serializeChimeraRecipe(const ChimeraRecipe& recipe) {
    validateChimeraRecipe(recipe);
    Json rgb = Json::array();
    for (const auto& arm : recipe.rgb) {
        rgb.push_back(armToJson(arm));
    }
    const Json root {
        {"document_type", "chimera_recipe"},
        {"exposure", {
            {"exposure_s_per_channel", recipe.exposure.exposureSecondsPerChannel},
            {"sample_height", recipe.exposure.sampleHeight},
            {"sample_width", recipe.exposure.sampleWidth},
        }},
        {"format_version", recipe.formatVersion},
        {"hogel_geometry", {
            {"count_x", recipe.hogels.countX},
            {"count_y", recipe.hogels.countY},
            {"pitch_m", recipe.hogels.pitchMetres},
        }},
        {"name", recipe.name},
        {"plate", {
            {"average_refractive_index", recipe.plate.averageRefractiveIndex},
            {"isotropic_linear_shrinkage_fraction",
                recipe.plate.isotropicLinearShrinkageFraction},
            {"refractive_index_modulation",
                recipe.plate.refractiveIndexModulation},
            {"thickness_m", recipe.plate.thicknessMetres},
        }},
        {"recipe_id", recipe.recipeId},
        {"reference", {
            {"arm_separation_m", recipe.reference.armSeparationMetres},
            {"mirror_x_m", recipe.reference.mirrorXMetres},
            {"mirror_z_m", recipe.reference.mirrorZMetres},
            {"source_x_m", recipe.reference.sourceXMetres},
            {"splitter_distance_after_mirror_m",
                recipe.reference.splitterDistanceAfterMirrorMetres},
            {"splitter_power_transmission",
                recipe.reference.splitterPowerTransmission},
        }},
        {"relay", {
            {"clear_aperture_diameter_m",
                recipe.relay.clearApertureDiameterMetres},
            {"focal_length_m", recipe.relay.focalLengthMetres},
            {"stop_diameter_m", recipe.relay.stopDiameterMetres},
        }},
        {"rgb", std::move(rgb)},
        {"slm", {
            {"bit_depth", recipe.slm.bitDepth},
            {"fill_factor", recipe.slm.fillFactor},
            {"height_m", recipe.slm.heightMetres},
            {"phase_range_rad", recipe.slm.phaseRangeRadians},
            {"pixel_height", recipe.slm.pixelHeight},
            {"pixel_width", recipe.slm.pixelWidth},
            {"width_m", recipe.slm.widthMetres},
        }},
        {"target_fov", {
            {"horizontal_rad", recipe.targetHorizontalFieldOfViewRadians},
            {"vertical_rad", recipe.targetVerticalFieldOfViewRadians},
        }},
    };
    return root.dump(2) + "\n";
}

ChimeraRecipe parseChimeraRecipe(std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        requireKeys(root,
            {"document_type", "exposure", "format_version", "hogel_geometry",
             "name", "plate", "recipe_id", "reference", "relay", "rgb",
             "slm", "target_fov"},
            "CHIMERA recipe");
        if (requiredString(root.at("document_type"), "document_type")
            != "chimera_recipe") {
            throw std::runtime_error("project is not a chimera_recipe document");
        }
        if (!root.at("format_version").is_number_integer()) {
            throw std::runtime_error("format_version must be an integer");
        }
        ChimeraRecipe result;
        result.formatVersion = root.at("format_version").get<int>();
        result.recipeId = requiredString(root.at("recipe_id"), "recipe_id");
        result.name = requiredString(root.at("name"), "name");

        const auto& hogels = root.at("hogel_geometry");
        requireKeys(hogels, {"count_x", "count_y", "pitch_m"}, "hogel geometry");
        result.hogels = {
            .pitchMetres = finiteNumber(hogels.at("pitch_m"), "hogel pitch_m"),
            .countX = positiveSize(hogels.at("count_x"), "hogel count_x"),
            .countY = positiveSize(hogels.at("count_y"), "hogel count_y"),
        };

        const auto& fov = root.at("target_fov");
        requireKeys(fov, {"horizontal_rad", "vertical_rad"}, "target FOV");
        result.targetHorizontalFieldOfViewRadians
            = finiteNumber(fov.at("horizontal_rad"), "horizontal FOV_rad");
        result.targetVerticalFieldOfViewRadians
            = finiteNumber(fov.at("vertical_rad"), "vertical FOV_rad");

        const auto& rgb = root.at("rgb");
        if (!rgb.is_array() || rgb.size() != result.rgb.size()) {
            throw std::runtime_error("rgb must contain exactly three arms");
        }
        for (std::size_t index = 0; index < result.rgb.size(); ++index) {
            result.rgb[index] = armFromJson(rgb.at(index));
        }

        const auto& slm = root.at("slm");
        requireKeys(slm,
            {"bit_depth", "fill_factor", "height_m", "phase_range_rad",
             "pixel_height", "pixel_width", "width_m"},
            "SLM specification");
        const std::size_t bitDepth
            = positiveSize(slm.at("bit_depth"), "SLM bit_depth");
        if (bitDepth > 24U) {
            throw std::runtime_error("SLM bit_depth must be in [1, 24]");
        }
        result.slm = {
            .widthMetres = finiteNumber(slm.at("width_m"), "SLM width_m"),
            .heightMetres = finiteNumber(slm.at("height_m"), "SLM height_m"),
            .pixelWidth = positiveSize(slm.at("pixel_width"), "SLM pixel_width"),
            .pixelHeight = positiveSize(slm.at("pixel_height"), "SLM pixel_height"),
            .fillFactor = finiteNumber(slm.at("fill_factor"), "SLM fill_factor"),
            .bitDepth = static_cast<unsigned int>(bitDepth),
            .phaseRangeRadians = finiteNumber(
                slm.at("phase_range_rad"), "SLM phase_range_rad"),
        };

        const auto& relay = root.at("relay");
        requireKeys(relay,
            {"clear_aperture_diameter_m", "focal_length_m", "stop_diameter_m"},
            "relay specification");
        result.relay = {
            .focalLengthMetres = finiteNumber(
                relay.at("focal_length_m"), "relay focal_length_m"),
            .clearApertureDiameterMetres = finiteNumber(
                relay.at("clear_aperture_diameter_m"),
                "relay clear_aperture_diameter_m"),
            .stopDiameterMetres = finiteNumber(
                relay.at("stop_diameter_m"), "relay stop_diameter_m"),
        };

        const auto& reference = root.at("reference");
        requireKeys(reference,
            {"arm_separation_m", "mirror_x_m", "mirror_z_m",
             "source_x_m", "splitter_distance_after_mirror_m",
             "splitter_power_transmission"},
            "reference specification");
        result.reference = {
            .sourceXMetres = finiteNumber(
                reference.at("source_x_m"), "reference source_x_m"),
            .mirrorXMetres = finiteNumber(
                reference.at("mirror_x_m"), "reference mirror_x_m"),
            .mirrorZMetres = finiteNumber(
                reference.at("mirror_z_m"), "reference mirror_z_m"),
            .splitterDistanceAfterMirrorMetres = finiteNumber(
                reference.at("splitter_distance_after_mirror_m"),
                "reference splitter_distance_after_mirror_m"),
            .armSeparationMetres = finiteNumber(
                reference.at("arm_separation_m"), "reference arm_separation_m"),
            .splitterPowerTransmission = finiteNumber(
                reference.at("splitter_power_transmission"),
                "reference splitter_power_transmission"),
        };

        const auto& plate = root.at("plate");
        requireKeys(plate,
            {"average_refractive_index", "isotropic_linear_shrinkage_fraction",
             "refractive_index_modulation", "thickness_m"},
            "plate specification");
        result.plate = {
            .thicknessMetres = finiteNumber(
                plate.at("thickness_m"), "plate thickness_m"),
            .averageRefractiveIndex = finiteNumber(
                plate.at("average_refractive_index"),
                "plate average_refractive_index"),
            .refractiveIndexModulation = finiteNumber(
                plate.at("refractive_index_modulation"),
                "plate refractive_index_modulation"),
            .isotropicLinearShrinkageFraction = finiteNumber(
                plate.at("isotropic_linear_shrinkage_fraction"),
                "plate isotropic_linear_shrinkage_fraction"),
        };

        const auto& exposure = root.at("exposure");
        requireKeys(exposure,
            {"exposure_s_per_channel", "sample_height", "sample_width"},
            "exposure policy");
        result.exposure = {
            .exposureSecondsPerChannel = finiteNumber(
                exposure.at("exposure_s_per_channel"),
                "exposure_s_per_channel"),
            .sampleWidth = positiveSize(
                exposure.at("sample_width"), "exposure sample_width"),
            .sampleHeight = positiveSize(
                exposure.at("sample_height"), "exposure sample_height"),
        };

        validateChimeraRecipe(result);
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            std::string("invalid CHIMERA recipe JSON: ") + error.what());
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(
            std::string("invalid CHIMERA recipe: ") + error.what());
    }
}

void saveChimeraRecipe(
    const ChimeraRecipe& recipe,
    const std::filesystem::path& path) {
    const std::string contents = serializeChimeraRecipe(recipe);
    std::ofstream output(path, std::ios::binary | std::ios::trunc);
    if (!output) {
        throw std::runtime_error(
            "unable to open CHIMERA recipe for writing: " + path.string());
    }
    output.write(contents.data(), static_cast<std::streamsize>(contents.size()));
    if (!output) {
        throw std::runtime_error(
            "failed while writing CHIMERA recipe: " + path.string());
    }
}

ChimeraRecipe loadChimeraRecipe(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        throw std::runtime_error(
            "unable to open CHIMERA recipe for reading: " + path.string());
    }
    const std::string contents(
        (std::istreambuf_iterator<char>(input)),
        std::istreambuf_iterator<char>());
    return parseChimeraRecipe(contents);
}

} // namespace holobench::app::chimera
