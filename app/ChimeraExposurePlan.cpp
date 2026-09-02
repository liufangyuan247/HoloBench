#include "app/ChimeraExposurePlan.hpp"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <iomanip>
#include <initializer_list>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>

#include <nlohmann/json.hpp>

#include "app/BenchRecordingRecipe.hpp"
#include "optics/holography/PlateIncidentFields.hpp"
#include "optics/ray/DynamicBenchTracer.hpp"

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;

std::string_view eventKindName(ExposureEventKind kind) noexcept {
    switch (kind) {
    case ExposureEventKind::StageMove: return "stage_move";
    case ExposureEventKind::SlmLoad: return "slm_load";
    case ExposureEventKind::BeamGate: return "beam_gate";
    case ExposureEventKind::Exposure: return "exposure";
    }
    return "unknown";
}

ExposureEventKind eventKindFromName(std::string_view name) {
    if (name == "stage_move") return ExposureEventKind::StageMove;
    if (name == "slm_load") return ExposureEventKind::SlmLoad;
    if (name == "beam_gate") return ExposureEventKind::BeamGate;
    if (name == "exposure") return ExposureEventKind::Exposure;
    throw std::runtime_error("unsupported exposure event kind");
}

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

std::string requiredString(const Json& value, std::string_view field) {
    if (!value.is_string()) {
        throw std::runtime_error(std::string(field) + " must be a string");
    }
    return value.get<std::string>();
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

std::size_t nonNegativeSize(const Json& value, std::string_view field) {
    if (!value.is_number_integer() && !value.is_number_unsigned()) {
        throw std::runtime_error(std::string(field) + " must be an integer");
    }
    const auto result = value.get<std::int64_t>();
    if (result < 0) {
        throw std::runtime_error(std::string(field) + " must be non-negative");
    }
    return static_cast<std::size_t>(result);
}

std::string fnv1a64(std::string_view bytes) {
    std::uint64_t value = 14695981039346656037ULL;
    for (const char character : bytes) {
        value ^= static_cast<std::uint64_t>(
            static_cast<unsigned char>(character));
        value *= 1099511628211ULL;
    }
    std::ostringstream encoded;
    encoded << std::hex << std::setfill('0') << std::setw(16) << value;
    return encoded.str();
}

bool isLowerHexHash(std::string_view value) {
    return value.size() == 16U
        && std::all_of(value.begin(), value.end(), [](char character) {
            return (character >= '0' && character <= '9')
                || (character >= 'a' && character <= 'f');
        });
}

Json eventToJson(const ExposureEvent& event) {
    return {
        {"channel_id", event.channelId},
        {"duration_s", event.durationSeconds},
        {"event_id", event.eventId},
        {"kind", eventKindName(event.kind)},
        {"object_beam_enabled", event.objectBeamEnabled},
        {"object_source_component_id", event.objectSourceComponentId},
        {"recording_recipe_id", event.recordingRecipeId},
        {"reference_beam_enabled", event.referenceBeamEnabled},
        {"reference_source_component_id", event.referenceSourceComponentId},
        {"slm_command_id", event.slmCommandId},
        {"slm_component_id", event.slmComponentId},
        {"stage_x_m", event.stageXMetres},
        {"stage_y_m", event.stageYMetres},
        {"start_s", event.startSeconds},
        {"wavelength_m", event.wavelengthMetres},
        {"hogel_x", event.hogelX},
        {"hogel_y", event.hogelY},
    };
}

Json payloadToJson(const ExposurePlan& plan) {
    Json events = Json::array();
    for (const auto& event : plan.events) {
        events.push_back(eventToJson(event));
    }
    return {
        {"events", std::move(events)},
        {"plan_id", plan.planId},
        {"source_bench_project_id", plan.sourceBenchProjectId},
        {"source_dataset_hash", plan.sourceDatasetHash},
        {"source_dataset_id", plan.sourceDatasetId},
        {"source_recipe_id", plan.sourceRecipeId},
        {"source_recipe_version", plan.sourceRecipeVersion},
        {"total_duration_s", plan.totalDurationSeconds},
        {"units", {{"length", plan.lengthUnit}, {"time", plan.timeUnit}}},
    };
}

const SlmHogelCommand& commandFor(
    const HogelDataset& dataset,
    std::size_t hogelX,
    std::size_t hogelY,
    std::string_view channelId) {
    const auto found = std::find_if(
        dataset.slmCommands.begin(), dataset.slmCommands.end(),
        [&](const auto& command) {
            return command.hogelX == hogelX && command.hogelY == hogelY
                && command.channelId == channelId;
        });
    if (found == dataset.slmCommands.end()) {
        throw std::invalid_argument("hogel SLM command was not found");
    }
    return *found;
}

const HologramRecordingRecipe& recordingRecipeFor(
    const BenchProject& project,
    std::string_view recipeId) {
    const auto found = std::find_if(
        project.recordingRecipes.begin(), project.recordingRecipes.end(),
        [&](const auto& recipe) { return recipe.recipeId == recipeId; });
    if (found == project.recordingRecipes.end()) {
        throw std::invalid_argument("CHIMERA recording recipe was not found");
    }
    return *found;
}

optics::holography::PlacedSlmSparseCommand makePlacedSparseCommand(
    const SlmHogelCommand& command,
    std::string componentId,
    std::size_t pixelWidth,
    std::size_t pixelHeight,
    std::string calibrationId,
    const optics::slm::CalibratedSlmResponse* calibratedResponse) {
    optics::holography::PlacedSlmSparseCommand result {
        .componentId = std::move(componentId),
        .commandId = command.commandId,
        .pixelWidth = pixelWidth,
        .pixelHeight = pixelHeight,
        .defaultNormalizedCommand = 0.0,
        .calibrationId = std::move(calibrationId),
        .calibratedResponse = calibratedResponse,
        .pixels = {},
    };
    result.pixels.reserve(command.pixels.size());
    for (const auto& pixel : command.pixels) {
        result.pixels.push_back({
            .column = pixel.column,
            .row = pixel.row,
            .normalizedCommand = pixel.normalizedAmplitude,
        });
    }
    std::sort(result.pixels.begin(), result.pixels.end(),
        [](const auto& first, const auto& second) {
            return std::pair {first.row, first.column}
                < std::pair {second.row, second.column};
        });
    const auto duplicate = std::adjacent_find(
        result.pixels.begin(), result.pixels.end(),
        [](const auto& first, const auto& second) {
            return first.row == second.row && first.column == second.column;
        });
    if (duplicate != result.pixels.end()) {
        throw std::invalid_argument(
            "collided angular samples cannot form an unambiguous SLM raster");
    }
    return result;
}

} // namespace

ExposurePlan generateExposurePlan(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const BenchProject& bench) {
    validateChimeraRecipe(recipe);
    validateHogelDataset(dataset);
    validateBenchProject(bench);
    if (computeHogelDatasetContentHash(dataset) != dataset.contentHash
        || dataset.sourceRecipeId != recipe.recipeId
        || dataset.sourceRecipeVersion != recipe.formatVersion
        || dataset.hogels != recipe.hogels
        || dataset.slmPixelWidth != recipe.slm.pixelWidth
        || dataset.slmPixelHeight != recipe.slm.pixelHeight) {
        throw std::invalid_argument(
            "hogel dataset does not match the CHIMERA recipe");
    }
    const auto compiled = compileChimeraRecipe(recipe);
    if (!compiled.feasible()) {
        throw std::invalid_argument(
            "cannot plan exposures for an unsupported CHIMERA recipe");
    }
    for (const auto& arm : recipe.rgb) {
        const std::array requiredComponents {
            std::pair {"chimera-slm-" + arm.channelId,
                optics::scene::BenchComponentKind::SpatialLightModulator},
            std::pair {"chimera-object-source-" + arm.channelId,
                optics::scene::BenchComponentKind::ObjectWavefrontSource},
            std::pair {"chimera-reference-source-" + arm.channelId,
                optics::scene::BenchComponentKind::LaserSource},
        };
        for (const auto& [componentId, kind] : requiredComponents) {
            const auto* component = bench.scene.find(componentId);
            if (component == nullptr || component->kind != kind) {
                throw std::invalid_argument(
                    "editable CHIMERA bench is missing a required component");
            }
        }
        static_cast<void>(recordingRecipeFor(
            bench, "chimera-" + arm.channelId + "-volume"));
    }

    ExposurePlan result;
    result.planId = "exposure-" + recipe.recipeId;
    result.sourceRecipeId = recipe.recipeId;
    result.sourceRecipeVersion = recipe.formatVersion;
    result.sourceDatasetId = dataset.datasetId;
    result.sourceDatasetHash = dataset.contentHash;
    result.sourceBenchProjectId = bench.projectId;
    const std::size_t hogelCount
        = recipe.hogels.countX * recipe.hogels.countY;
    result.events.reserve(hogelCount * (1U + 4U * recipe.rgb.size()));

    double currentTime = 0.0;
    for (std::size_t y = 0; y < recipe.hogels.countY; ++y) {
        for (std::size_t x = 0; x < recipe.hogels.countX; ++x) {
            const double stageX = (static_cast<double>(x) + 0.5
                - 0.5 * static_cast<double>(recipe.hogels.countX))
                * recipe.hogels.pitchMetres;
            const double stageY = (0.5 * static_cast<double>(recipe.hogels.countY)
                - static_cast<double>(y) - 0.5)
                * recipe.hogels.pitchMetres;
            const std::string prefix = result.planId + "-h"
                + std::to_string(x) + "-" + std::to_string(y);
            result.events.push_back({
                .eventId = prefix + "-stage",
                .kind = ExposureEventKind::StageMove,
                .startSeconds = currentTime,
                .hogelX = x,
                .hogelY = y,
                .stageXMetres = stageX,
                .stageYMetres = stageY,
                .channelId = {},
                .wavelengthMetres = 0.0,
                .slmComponentId = {},
                .slmCommandId = {},
                .objectSourceComponentId = {},
                .referenceSourceComponentId = {},
                .objectBeamEnabled = false,
                .referenceBeamEnabled = false,
                .recordingRecipeId = {},
            });
            for (const auto& arm : recipe.rgb) {
                const auto& command = commandFor(dataset, x, y, arm.channelId);
                const std::string channelPrefix = prefix + "-" + arm.channelId;
                const std::string slmId = "chimera-slm-" + arm.channelId;
                const std::string objectId
                    = "chimera-object-source-" + arm.channelId;
                const std::string referenceId
                    = "chimera-reference-source-" + arm.channelId;
                const std::string recordingId
                    = "chimera-" + arm.channelId + "-volume";
                const auto common = [&](std::string eventId,
                                        ExposureEventKind kind,
                                        double duration,
                                        bool beamsEnabled) {
                    return ExposureEvent {
                        .eventId = std::move(eventId),
                        .kind = kind,
                        .startSeconds = currentTime,
                        .durationSeconds = duration,
                        .hogelX = x,
                        .hogelY = y,
                        .stageXMetres = stageX,
                        .stageYMetres = stageY,
                        .channelId = arm.channelId,
                        .wavelengthMetres = arm.wavelengthMetres,
                        .slmComponentId = slmId,
                        .slmCommandId = command.commandId,
                        .objectSourceComponentId = objectId,
                        .referenceSourceComponentId = referenceId,
                        .objectBeamEnabled = beamsEnabled,
                        .referenceBeamEnabled = beamsEnabled,
                        .recordingRecipeId = recordingId,
                    };
                };
                result.events.push_back(common(
                    channelPrefix + "-slm", ExposureEventKind::SlmLoad,
                    0.0, false));
                result.events.push_back(common(
                    channelPrefix + "-on", ExposureEventKind::BeamGate,
                    0.0, true));
                result.events.push_back(common(
                    channelPrefix + "-expose", ExposureEventKind::Exposure,
                    recipe.exposure.exposureSecondsPerChannel, true));
                currentTime += recipe.exposure.exposureSecondsPerChannel;
                result.events.push_back(common(
                    channelPrefix + "-off", ExposureEventKind::BeamGate,
                    0.0, false));
            }
        }
    }
    result.totalDurationSeconds = currentTime;
    result.contentHash = computeExposurePlanContentHash(result);
    validateExposurePlan(result);
    return result;
}

void validateExposurePlan(const ExposurePlan& plan) {
    if (plan.formatVersion != kExposurePlanFormatVersion
        || !optics::scene::isStableBenchId(plan.planId)
        || !optics::scene::isStableBenchId(plan.sourceRecipeId)
        || plan.sourceRecipeVersion != kChimeraRecipeFormatVersion
        || !optics::scene::isStableBenchId(plan.sourceDatasetId)
        || !optics::scene::isStableBenchId(plan.sourceBenchProjectId)
        || !isLowerHexHash(plan.sourceDatasetHash)
        || plan.timeUnit != "s" || plan.lengthUnit != "m"
        || plan.events.empty() || !std::isfinite(plan.totalDurationSeconds)
        || plan.totalDurationSeconds <= 0.0
        || plan.hashAlgorithm != kHogelDatasetHashAlgorithm
        || !isLowerHexHash(plan.contentHash)) {
        throw std::invalid_argument("exposure plan metadata is invalid");
    }
    std::set<std::string> eventIds;
    double previousStart = 0.0;
    double maximumEnd = 0.0;
    for (const auto& event : plan.events) {
        if (!optics::scene::isStableBenchId(event.eventId)
            || !eventIds.insert(event.eventId).second
            || !std::isfinite(event.startSeconds) || event.startSeconds < 0.0
            || !std::isfinite(event.durationSeconds)
            || event.durationSeconds < 0.0
            || event.startSeconds < previousStart
            || !std::isfinite(event.stageXMetres)
            || !std::isfinite(event.stageYMetres)
            || !std::isfinite(event.wavelengthMetres)
            || event.wavelengthMetres < 0.0) {
            throw std::invalid_argument("exposure event is invalid");
        }
        previousStart = event.startSeconds;
        maximumEnd = std::max(
            maximumEnd, event.startSeconds + event.durationSeconds);
        if (event.kind == ExposureEventKind::StageMove) {
            if (!event.channelId.empty() || event.wavelengthMetres != 0.0
                || !event.slmComponentId.empty()
                || !event.slmCommandId.empty()
                || !event.objectSourceComponentId.empty()
                || !event.referenceSourceComponentId.empty()
                || event.objectBeamEnabled || event.referenceBeamEnabled
                || !event.recordingRecipeId.empty()
                || event.durationSeconds != 0.0) {
                throw std::invalid_argument("stage event contains channel state");
            }
        } else if (!optics::scene::isStableBenchId(event.channelId)
            || event.wavelengthMetres <= 0.0
            || !optics::scene::isStableBenchId(event.slmComponentId)
            || !optics::scene::isStableBenchId(event.slmCommandId)
            || !optics::scene::isStableBenchId(event.objectSourceComponentId)
            || !optics::scene::isStableBenchId(event.referenceSourceComponentId)
            || !optics::scene::isStableBenchId(event.recordingRecipeId)) {
            throw std::invalid_argument(
                "channel exposure event identity is invalid");
        }
        if (event.kind == ExposureEventKind::Exposure
            && (event.durationSeconds <= 0.0
                || !event.objectBeamEnabled
                || !event.referenceBeamEnabled)) {
            throw std::invalid_argument(
                "exposure event requires positive time and both beams");
        }
        if (event.kind == ExposureEventKind::SlmLoad
            && (event.durationSeconds != 0.0
                || event.objectBeamEnabled
                || event.referenceBeamEnabled)) {
            throw std::invalid_argument(
                "SLM load event must be instantaneous with closed beams");
        }
        if (event.kind == ExposureEventKind::BeamGate
            && (event.durationSeconds != 0.0
                || event.objectBeamEnabled != event.referenceBeamEnabled)) {
            throw std::invalid_argument(
                "beam gate event must switch both recording beams together");
        }
    }
    if (maximumEnd != plan.totalDurationSeconds) {
        throw std::invalid_argument(
            "exposure plan duration does not match its events");
    }
}

std::string computeExposurePlanContentHash(const ExposurePlan& plan) {
    return fnv1a64(payloadToJson(plan).dump());
}

std::string serializeExposurePlan(const ExposurePlan& plan) {
    validateExposurePlan(plan);
    if (computeExposurePlanContentHash(plan) != plan.contentHash) {
        throw std::invalid_argument(
            "exposure plan content hash does not match its payload");
    }
    const Json root {
        {"content_hash", plan.contentHash},
        {"document_type", "chimera_exposure_plan"},
        {"format_version", plan.formatVersion},
        {"hash_algorithm", plan.hashAlgorithm},
        {"payload", payloadToJson(plan)},
    };
    return root.dump(2) + "\n";
}

ExposurePlan parseExposurePlan(std::string_view jsonText) {
    try {
        const Json root = Json::parse(jsonText);
        requireKeys(root,
            {"content_hash", "document_type", "format_version",
             "hash_algorithm", "payload"},
            "exposure plan");
        if (requiredString(root.at("document_type"), "document_type")
            != "chimera_exposure_plan") {
            throw std::runtime_error(
                "document is not a chimera_exposure_plan");
        }
        const auto& payload = root.at("payload");
        requireKeys(payload,
            {"events", "plan_id", "source_bench_project_id",
             "source_dataset_hash", "source_dataset_id", "source_recipe_id",
             "source_recipe_version", "total_duration_s", "units"},
            "exposure plan payload");
        ExposurePlan result;
        if (!root.at("format_version").is_number_integer()
            || !payload.at("source_recipe_version").is_number_integer()) {
            throw std::runtime_error("exposure plan versions must be integers");
        }
        result.formatVersion = root.at("format_version").get<int>();
        result.hashAlgorithm = requiredString(
            root.at("hash_algorithm"), "hash_algorithm");
        result.contentHash = requiredString(
            root.at("content_hash"), "content_hash");
        result.planId = requiredString(payload.at("plan_id"), "plan_id");
        result.sourceRecipeId = requiredString(
            payload.at("source_recipe_id"), "source_recipe_id");
        result.sourceRecipeVersion
            = payload.at("source_recipe_version").get<int>();
        result.sourceDatasetId = requiredString(
            payload.at("source_dataset_id"), "source_dataset_id");
        result.sourceDatasetHash = requiredString(
            payload.at("source_dataset_hash"), "source_dataset_hash");
        result.sourceBenchProjectId = requiredString(
            payload.at("source_bench_project_id"),
            "source_bench_project_id");
        result.totalDurationSeconds = finiteNumber(
            payload.at("total_duration_s"), "total_duration_s");
        const auto& units = payload.at("units");
        requireKeys(units, {"length", "time"}, "exposure plan units");
        result.lengthUnit = requiredString(units.at("length"), "length unit");
        result.timeUnit = requiredString(units.at("time"), "time unit");

        const auto& events = payload.at("events");
        if (!events.is_array() || events.empty() || events.size() > 5'000'000U) {
            throw std::runtime_error("exposure events are not a bounded array");
        }
        result.events.reserve(events.size());
        for (const auto& encoded : events) {
            requireKeys(encoded,
                {"channel_id", "duration_s", "event_id", "hogel_x",
                 "hogel_y", "kind", "object_beam_enabled",
                 "object_source_component_id", "recording_recipe_id",
                 "reference_beam_enabled", "reference_source_component_id",
                 "slm_command_id", "slm_component_id", "stage_x_m",
                 "stage_y_m", "start_s", "wavelength_m"},
                "exposure event");
            if (!encoded.at("object_beam_enabled").is_boolean()
                || !encoded.at("reference_beam_enabled").is_boolean()) {
                throw std::runtime_error("beam gate states must be boolean");
            }
            result.events.push_back({
                .eventId = requiredString(encoded.at("event_id"), "event_id"),
                .kind = eventKindFromName(requiredString(
                    encoded.at("kind"), "event kind")),
                .startSeconds = finiteNumber(encoded.at("start_s"), "start_s"),
                .durationSeconds = finiteNumber(
                    encoded.at("duration_s"), "duration_s"),
                .hogelX = nonNegativeSize(encoded.at("hogel_x"), "hogel_x"),
                .hogelY = nonNegativeSize(encoded.at("hogel_y"), "hogel_y"),
                .stageXMetres = finiteNumber(
                    encoded.at("stage_x_m"), "stage_x_m"),
                .stageYMetres = finiteNumber(
                    encoded.at("stage_y_m"), "stage_y_m"),
                .channelId = requiredString(
                    encoded.at("channel_id"), "channel_id"),
                .wavelengthMetres = finiteNumber(
                    encoded.at("wavelength_m"), "wavelength_m"),
                .slmComponentId = requiredString(
                    encoded.at("slm_component_id"), "slm_component_id"),
                .slmCommandId = requiredString(
                    encoded.at("slm_command_id"), "slm_command_id"),
                .objectSourceComponentId = requiredString(
                    encoded.at("object_source_component_id"),
                    "object_source_component_id"),
                .referenceSourceComponentId = requiredString(
                    encoded.at("reference_source_component_id"),
                    "reference_source_component_id"),
                .objectBeamEnabled
                    = encoded.at("object_beam_enabled").get<bool>(),
                .referenceBeamEnabled
                    = encoded.at("reference_beam_enabled").get<bool>(),
                .recordingRecipeId = requiredString(
                    encoded.at("recording_recipe_id"), "recording_recipe_id"),
            });
        }
        validateExposurePlan(result);
        if (computeExposurePlanContentHash(result) != result.contentHash) {
            throw std::runtime_error(
                "exposure plan content hash does not match its payload");
        }
        return result;
    } catch (const nlohmann::json::exception& error) {
        throw std::runtime_error(
            std::string("invalid exposure plan JSON: ") + error.what());
    } catch (const std::invalid_argument& error) {
        throw std::runtime_error(
            std::string("invalid exposure plan: ") + error.what());
    }
}

ExecutedHogelExposure executeHogelExposure(
    const ChimeraRecipe& recipe,
    const HogelDataset& dataset,
    const ExposurePlan& plan,
    const BenchProject& bench,
    compute::fft::IFftBackend& fftBackend,
    std::size_t hogelX,
    std::size_t hogelY,
    const HogelExposureExecutionOptions& options) {
    validateChimeraRecipe(recipe);
    validateHogelDataset(dataset);
    validateExposurePlan(plan);
    validateBenchProject(bench);
    if (options.maximumPreviewSampleWidth < 32U
        || options.maximumPreviewSampleHeight < 32U
        || options.maximumPreviewSampleWidth > 2048U
        || options.maximumPreviewSampleHeight > 2048U) {
        throw std::invalid_argument(
            "hogel exposure preview sampling must be in [32, 2048]");
    }
    if ((options.calibratedSlmResponse == nullptr)
            != options.slmCalibrationId.empty()
        || (!options.slmCalibrationId.empty()
            && !optics::scene::isStableBenchId(options.slmCalibrationId))) {
        throw std::invalid_argument(
            "hogel exposure SLM calibration identity is invalid");
    }
    if (computeHogelDatasetContentHash(dataset) != dataset.contentHash
        || computeExposurePlanContentHash(plan) != plan.contentHash
        || plan.sourceRecipeId != recipe.recipeId
        || plan.sourceDatasetId != dataset.datasetId
        || plan.sourceDatasetHash != dataset.contentHash
        || plan.sourceBenchProjectId != bench.projectId
        || hogelX >= recipe.hogels.countX || hogelY >= recipe.hogels.countY) {
        throw std::invalid_argument(
            "exposure execution inputs or selected hogel are inconsistent");
    }
    auto workingProject = bench;
    const auto stageEvent = std::find_if(
        plan.events.begin(), plan.events.end(), [&](const auto& event) {
            return event.kind == ExposureEventKind::StageMove
                && event.hogelX == hogelX && event.hogelY == hogelY;
        });
    if (stageEvent == plan.events.end()) {
        throw std::invalid_argument(
            "selected hogel does not contain a stage move event");
    }
    const auto* currentPlate = workingProject.scene.find("chimera-plate");
    if (currentPlate == nullptr
        || currentPlate->kind
            != optics::scene::BenchComponentKind::HolographicPlate) {
        throw std::invalid_argument(
            "editable CHIMERA bench is missing its holographic plate");
    }
    auto stagedPlate = *currentPlate;
    stagedPlate.transform.translationMetres
        = stagedPlate.transform.translationMetres
        - stageEvent->stageXMetres
            * stagedPlate.transform.localXAxisInWorld
        - stageEvent->stageYMetres
            * stagedPlate.transform.localYAxisInWorld;
    workingProject.scene.replace("chimera-plate", std::move(stagedPlate));
    const BenchProject stagedProject = workingProject;
    ExecutedHogelExposure result {
        .planId = plan.planId,
        .planHash = plan.contentHash,
        .hogelX = hogelX,
        .hogelY = hogelY,
        .channels = {},
    };
    result.channels.reserve(recipe.rgb.size());
    for (const auto& event : plan.events) {
        if (event.kind != ExposureEventKind::Exposure
            || event.hogelX != hogelX || event.hogelY != hogelY) {
            continue;
        }
        if (event.stageXMetres != stageEvent->stageXMetres
            || event.stageYMetres != stageEvent->stageYMetres) {
            throw std::invalid_argument(
                "exposure event does not match its hogel stage position");
        }
        const auto& command = commandFor(
            dataset, hogelX, hogelY, event.channelId);
        if (command.commandId != event.slmCommandId
            || command.wavelengthMetres != event.wavelengthMetres) {
            throw std::invalid_argument(
                "exposure event does not match its SLM command");
        }
        auto channelProject = stagedProject;
        const auto* currentSlm = channelProject.scene.find(
            event.slmComponentId);
        if (currentSlm == nullptr
            || currentSlm->kind
                != optics::scene::BenchComponentKind::SpatialLightModulator) {
            throw std::invalid_argument(
                "exposure event SLM is missing from the compiled bench");
        }
        auto slm = *currentSlm;
        auto parameters
            = std::get<optics::scene::SpatialLightModulatorParameters>(
                slm.parameters);
        parameters.commandOrigin = optics::scene::SlmCommandOrigin::Automation;
        parameters.commandId = event.slmCommandId;
        parameters.modulationMode
            = optics::scene::SlmModulationMode::Amplitude;
        slm.parameters = std::move(parameters);
        channelProject.scene.replace(event.slmComponentId, std::move(slm));

        const auto trace = optics::ray::traceDynamicBench(
            channelProject.scene);
        const auto fields = optics::holography::collectPlateIncidentFields(
            channelProject.scene, trace, "chimera-plate");
        const auto& recordingRecipe = recordingRecipeFor(
            channelProject, event.recordingRecipeId);
        const auto resolved = resolveRecordingRecipe(fields, recordingRecipe);
        if (resolved.channels.size() != 1U) {
            throw std::logic_error(
                "CHIMERA volume recipe did not resolve one channel");
        }
        const auto& pair = resolved.channels.front();
        const auto sparseCommand = makePlacedSparseCommand(
            command,
            event.slmComponentId,
            dataset.slmPixelWidth,
            dataset.slmPixelHeight,
            options.slmCalibrationId,
            options.calibratedSlmResponse);
        const std::array sparseCommands {sparseCommand};
        auto sampling = recordingRecipe.sampling;
        sampling.sampleWidth = std::min(
            sampling.sampleWidth, options.maximumPreviewSampleWidth);
        sampling.sampleHeight = std::min(
            sampling.sampleHeight, options.maximumPreviewSampleHeight);
        sampling.centreXMetres = stageEvent->stageXMetres;
        sampling.centreYMetres = stageEvent->stageYMetres;
        auto sampledObject = optics::holography::samplePlateIncidentField(
            channelProject.scene,
            fields,
            pair.objectBranchId,
            sampling,
            fftBackend,
            sparseCommands);
        const bool sparseRasterApplied = std::find(
            sampledObject.diagnostics.appliedSlmCommandIds.begin(),
            sampledObject.diagnostics.appliedSlmCommandIds.end(),
            event.slmCommandId)
            != sampledObject.diagnostics.appliedSlmCommandIds.end();
        if (!sparseRasterApplied
            || sampledObject.diagnostics.integratedPowerWatts <= 0.0) {
            throw std::invalid_argument(
                "placed sparse SLM command produced no sampled object field");
        }
        auto sampledReference
            = optics::holography::samplePlateIncidentField(
                channelProject.scene,
                fields,
                pair.referenceBranchId,
                sampling,
                fftBackend);
        double objectIrradiance = 0.0;
        double referenceIrradiance = 0.0;
        double fringeVisibility = 0.0;
        double totalDose = 0.0;
        double modulationDose = 0.0;
        auto material = recordingRecipe.volumeMaterial;
        std::string materialCalibrationId;
        if (options.calibratedMaterialDoseResponse != nullptr) {
            const double sampledArea
                = sampledObject.diagnostics.sampledExtentWidthMetres
                * sampledObject.diagnostics.sampledExtentHeightMetres;
            if (!std::isfinite(sampledArea) || sampledArea <= 0.0
                || sampledReference.diagnostics.sampledExtentWidthMetres
                        != sampledObject.diagnostics.sampledExtentWidthMetres
                || sampledReference.diagnostics.sampledExtentHeightMetres
                        != sampledObject.diagnostics.sampledExtentHeightMetres) {
                throw std::logic_error(
                    "calibrated exposure fields do not share a physical area");
            }
            objectIrradiance
                = sampledObject.diagnostics.integratedPowerWatts / sampledArea;
            referenceIrradiance
                = sampledReference.diagnostics.integratedPowerWatts / sampledArea;
            if (!std::isfinite(objectIrradiance)
                || !std::isfinite(referenceIrradiance)
                || objectIrradiance <= 0.0 || referenceIrradiance <= 0.0) {
                throw std::invalid_argument(
                    "calibrated exposure requires non-zero finite object and reference irradiance");
            }
            const double sumIrradiance
                = objectIrradiance + referenceIrradiance;
            const double modulationIrradiance = 2.0
                * std::sqrt(objectIrradiance)
                * std::sqrt(referenceIrradiance);
            fringeVisibility = modulationIrradiance / sumIrradiance;
            totalDose = sumIrradiance * event.durationSeconds;
            modulationDose = modulationIrradiance * event.durationSeconds;
            if (!std::isfinite(sumIrradiance)
                || !std::isfinite(modulationIrradiance)
                || !std::isfinite(fringeVisibility)
                || !std::isfinite(totalDose)
                || !std::isfinite(modulationDose)
                || fringeVisibility <= 0.0 || fringeVisibility > 1.0) {
                throw std::overflow_error(
                    "calibrated exposure dose is not representable");
            }
            const auto evaluated
                = options.calibratedMaterialDoseResponse->evaluate(
                    event.wavelengthMetres, modulationDose);
            material.refractiveIndexModulation
                = evaluated.refractiveIndexModulation;
            material.isotropicLinearShrinkageFraction
                = evaluated.isotropicLinearShrinkageFraction;
            materialCalibrationId = evaluated.calibrationId;
        }
        auto objectFieldDiagnostics = sampledObject.diagnostics;
        auto referenceFieldDiagnostics = sampledReference.diagnostics;
        auto recording
            = optics::holography::recordVolumePlateFromSampledFields(
            channelProject.scene,
            fields,
            pair.objectBranchId,
            pair.referenceBranchId,
            material,
            std::move(sampledObject),
            std::move(sampledReference));
        result.channels.push_back({
            .hogelX = hogelX,
            .hogelY = hogelY,
            .stageXMetres = event.stageXMetres,
            .stageYMetres = event.stageYMetres,
            .channelId = event.channelId,
            .exposureEventId = event.eventId,
            .slmCommandId = event.slmCommandId,
            .recordingRecipeId = event.recordingRecipeId,
            .m8VolumeRecordingInvoked = true,
            .sparseSlmRasterTransferredToPlacedWavePath = true,
            .sampleWidth = sampling.sampleWidth,
            .sampleHeight = sampling.sampleHeight,
            .usedBoundedPreviewSampling
                = sampling.sampleWidth != recordingRecipe.sampling.sampleWidth
                || sampling.sampleHeight
                    != recordingRecipe.sampling.sampleHeight,
            .calibratedSlmResponseApplied
                = options.calibratedSlmResponse != nullptr,
            .slmCalibrationId = options.slmCalibrationId,
            .calibratedMaterialDoseResponseApplied
                = options.calibratedMaterialDoseResponse != nullptr,
            .materialCalibrationId = std::move(materialCalibrationId),
            .objectMeanIrradianceWattsPerSquareMetre = objectIrradiance,
            .referenceMeanIrradianceWattsPerSquareMetre
                = referenceIrradiance,
            .fringeVisibility = fringeVisibility,
            .totalDoseJoulesPerSquareMetre = totalDose,
            .fringeModulationDoseJoulesPerSquareMetre = modulationDose,
            .objectFieldDiagnostics = std::move(objectFieldDiagnostics),
            .referenceFieldDiagnostics = std::move(
                referenceFieldDiagnostics),
            .recording = std::move(recording),
        });
    }
    if (result.channels.size() != recipe.rgb.size()) {
        throw std::invalid_argument(
            "selected hogel does not contain three RGB exposure events");
    }
    return result;
}

} // namespace holobench::app::chimera
