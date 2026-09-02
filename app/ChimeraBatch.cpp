#include "app/ChimeraBatch.hpp"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <fstream>
#include <initializer_list>
#include <iomanip>
#include <iterator>
#include <limits>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <utility>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <io.h>
#include <windows.h>
#else
#include <fcntl.h>
#include <unistd.h>
#endif

#include <nlohmann/json.hpp>

#include "compute/fft/IFftBackend.hpp"
#include "optics/scene/BenchScene.hpp"

namespace holobench::app::chimera {
namespace {

using Json = nlohmann::json;

void requireKeys(const Json &value,
                 std::initializer_list<std::string_view> keys,
                 std::string_view context) {
  if (!value.is_object() || value.size() != keys.size()) {
    throw std::runtime_error(std::string(context) +
                             " has missing or unknown keys");
  }
  for (const auto key : keys) {
    if (!value.contains(key)) {
      throw std::runtime_error(std::string(context) +
                               " has missing or unknown keys");
    }
  }
}

std::string fnv1a64(std::string_view bytes) {
  std::uint64_t hash = 14695981039346656037ULL;
  for (const char character : bytes) {
    hash ^= static_cast<std::uint64_t>(static_cast<unsigned char>(character));
    hash *= 1099511628211ULL;
  }
  std::ostringstream stream;
  stream << std::hex << std::setfill('0') << std::setw(16) << hash;
  return stream.str();
}

bool isLowerHexHash(std::string_view value) {
  return value.size() == 16U &&
         std::all_of(value.begin(), value.end(), [](char character) {
           return (character >= '0' && character <= '9') ||
                  (character >= 'a' && character <= 'f');
         });
}

std::string stopReasonName(ChimeraBatchStopReason reason) {
  switch (reason) {
  case ChimeraBatchStopReason::Ready:
    return "ready";
  case ChimeraBatchStopReason::SliceLimit:
    return "slice_limit";
  case ChimeraBatchStopReason::Cancelled:
    return "cancelled";
  case ChimeraBatchStopReason::Complete:
    return "complete";
  }
  throw std::logic_error("unknown CHIMERA batch stop reason");
}

ChimeraBatchStopReason parseStopReason(std::string_view value) {
  if (value == "ready")
    return ChimeraBatchStopReason::Ready;
  if (value == "slice_limit")
    return ChimeraBatchStopReason::SliceLimit;
  if (value == "cancelled")
    return ChimeraBatchStopReason::Cancelled;
  if (value == "complete")
    return ChimeraBatchStopReason::Complete;
  throw std::runtime_error("unknown CHIMERA batch stop reason");
}

Json payloadToJson(const ChimeraBatchArtifact &artifact) {
  Json completed = Json::array();
  for (const auto &hogel : artifact.completedHogels) {
    Json channels = Json::array();
    for (const auto &channel : hogel.channels) {
      channels.push_back({
          {"channel_id", channel.channelId},
          {"diffraction_efficiency", channel.diffractionEfficiency},
          {"wavelength_m", channel.wavelengthMetres},
      });
    }
    completed.push_back({
        {"channels", std::move(channels)},
        {"hogel_x", hogel.hogelX},
        {"hogel_y", hogel.hogelY},
        {"stage_x_m", hogel.stageXMetres},
        {"stage_y_m", hogel.stageYMetres},
    });
  }
  return {
      {"batch_id", artifact.batchId},
      {"completed_hogels", std::move(completed)},
      {"hogel_count_x", artifact.hogelCountX},
      {"hogel_count_y", artifact.hogelCountY},
      {"next_linear_hogel_index", artifact.nextLinearHogelIndex},
      {"source_bench_project_id", artifact.sourceBenchProjectId},
      {"source_dataset_hash", artifact.sourceDatasetHash},
      {"source_dataset_id", artifact.sourceDatasetId},
      {"source_exposure_plan_hash", artifact.sourceExposurePlanHash},
      {"source_exposure_plan_id", artifact.sourceExposurePlanId},
      {"source_recipe_id", artifact.sourceRecipeId},
      {"source_scene_revision", artifact.sourceSceneRevision},
      {"stop_reason", stopReasonName(artifact.stopReason)},
  };
}

std::size_t checkedTotal(const ChimeraBatchArtifact &artifact) {
  if (artifact.hogelCountX == 0U || artifact.hogelCountY == 0U ||
      artifact.hogelCountX >
          kMaximumChimeraBatchHogels / artifact.hogelCountY) {
    throw std::invalid_argument(
        "CHIMERA batch hogel dimensions are invalid or overflow");
  }
  const std::size_t total = artifact.hogelCountX * artifact.hogelCountY;
  if (total > kMaximumChimeraBatchHogels) {
    throw std::invalid_argument("CHIMERA batch exceeds the hogel limit");
  }
  return total;
}

ChimeraBatchHogelEvidence
summarizeExposure(const ExecutedHogelExposure &exposure) {
  if (exposure.channels.size() != 3U) {
    throw std::runtime_error(
        "CHIMERA batch exposure did not return three channels");
  }
  ChimeraBatchHogelEvidence result;
  result.hogelX = exposure.hogelX;
  result.hogelY = exposure.hogelY;
  result.stageXMetres = exposure.channels.front().stageXMetres;
  result.stageYMetres = exposure.channels.front().stageYMetres;
  for (std::size_t index = 0; index < result.channels.size(); ++index) {
    const auto &channel = exposure.channels[index];
    if (!channel.m8VolumeRecordingInvoked ||
        !channel.sparseSlmRasterTransferredToPlacedWavePath ||
        channel.recording.pair.geometry !=
            optics::holography::PlateRecordingGeometry::Reflection ||
        !channel.recording.nominalReplay.kogelnikEfficiencyEvaluated) {
      throw std::runtime_error(
          "CHIMERA batch channel lacks M8 reflection recording evidence");
    }
    result.channels[index] = {
        .channelId = channel.channelId,
        .wavelengthMetres = channel.recording.pair.wavelengthMetres,
        .diffractionEfficiency =
            channel.recording.nominalReplay.kogelnik.diffractionEfficiency,
    };
  }
  std::sort(result.channels.begin(), result.channels.end(),
            [](const auto &first, const auto &second) {
              return first.channelId < second.channelId;
            });
  return result;
}

void discardBatchSampledWavefronts(ExecutedHogelExposure &exposure) {
  for (auto &channel : exposure.channels) {
    channel.recording.objectIncident.reset();
    channel.recording.referenceIncident.reset();
  }
}

void validateProvenance(const ChimeraBatchArtifact &artifact,
                        const ChimeraRecipe &recipe,
                        const HogelDataset &dataset, const ExposurePlan &plan,
                        const BenchProject &bench) {
  validateChimeraBatchArtifact(artifact);
  if (computeChimeraBatchContentHash(artifact) != artifact.contentHash ||
      artifact.sourceRecipeId != recipe.recipeId ||
      artifact.sourceDatasetId != dataset.datasetId ||
      artifact.sourceDatasetHash != dataset.contentHash ||
      artifact.sourceExposurePlanId != plan.planId ||
      artifact.sourceExposurePlanHash != plan.contentHash ||
      artifact.sourceBenchProjectId != bench.projectId ||
      artifact.sourceSceneRevision != bench.scene.revision() ||
      artifact.hogelCountX != recipe.hogels.countX ||
      artifact.hogelCountY != recipe.hogels.countY) {
    throw std::invalid_argument(
        "CHIMERA batch provenance does not match the current Bench workflow");
  }
}

std::filesystem::path temporaryPath(const std::filesystem::path &path) {
  auto result = path;
  result += ".write.tmp";
  return result;
}

void removeTemporary(const std::filesystem::path &path) noexcept {
  std::error_code ignored;
  static_cast<void>(std::filesystem::remove(path, ignored));
}

void writeFlushed(const std::filesystem::path &path,
                  std::string_view contents) {
  std::FILE *output = nullptr;
#ifdef _WIN32
  if (_wfopen_s(&output, path.c_str(), L"wb") != 0 || output == nullptr) {
#else
  output = std::fopen(path.c_str(), "wb");
  if (output == nullptr) {
#endif
    throw std::runtime_error(
        "unable to open temporary CHIMERA batch artifact: " + path.string());
  }
  const auto close = [&output]() noexcept {
    if (output != nullptr) {
      static_cast<void>(std::fclose(output));
      output = nullptr;
    }
  };
  try {
    if (std::fwrite(contents.data(), 1U, contents.size(), output) !=
            contents.size() ||
        std::fflush(output) != 0) {
      throw std::runtime_error("failed to write CHIMERA batch artifact: " +
                               path.string());
    }
#ifdef _WIN32
    if (_commit(_fileno(output)) != 0) {
#else
    if (::fsync(fileno(output)) != 0) {
#endif
      throw std::runtime_error("failed to flush CHIMERA batch artifact: " +
                               path.string());
    }
    if (std::fclose(output) != 0) {
      output = nullptr;
      throw std::runtime_error("failed to close CHIMERA batch artifact: " +
                               path.string());
    }
    output = nullptr;
  } catch (...) {
    close();
    throw;
  }
}

void atomicReplace(const std::filesystem::path &temporary,
                   const std::filesystem::path &destination) {
#ifdef _WIN32
  if (!MoveFileExW(temporary.c_str(), destination.c_str(),
                   MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    const auto error = static_cast<int>(GetLastError());
    throw std::runtime_error(
        "unable to replace CHIMERA batch artifact atomically: " +
        destination.string() + ": " + std::system_category().message(error));
  }
#else
  std::error_code error;
  std::filesystem::rename(temporary, destination, error);
  if (error) {
    throw std::runtime_error(
        "unable to replace CHIMERA batch artifact atomically: " +
        destination.string() + ": " + error.message());
  }
  const auto parent = destination.has_parent_path()
                          ? destination.parent_path()
                          : std::filesystem::path(".");
  const int directory = ::open(parent.c_str(), O_RDONLY | O_DIRECTORY);
  if (directory >= 0) {
    static_cast<void>(::fsync(directory));
    static_cast<void>(::close(directory));
  }
#endif
}

} // namespace

std::size_t ChimeraBatchArtifact::totalHogelCount() const noexcept {
  if (hogelCountX == 0U || hogelCountY == 0U ||
      hogelCountX > std::numeric_limits<std::size_t>::max() / hogelCountY) {
    return 0U;
  }
  return hogelCountX * hogelCountY;
}

double ChimeraBatchArtifact::progressFraction() const noexcept {
  const std::size_t total = totalHogelCount();
  return total == 0U ? 0.0
                     : static_cast<double>(nextLinearHogelIndex) /
                           static_cast<double>(total);
}

bool ChimeraBatchArtifact::complete() const noexcept {
  return totalHogelCount() != 0U && nextLinearHogelIndex == totalHogelCount() &&
         stopReason == ChimeraBatchStopReason::Complete;
}

ChimeraBatchArtifact createChimeraBatchArtifact(std::string batchId,
                                                const ChimeraRecipe &recipe,
                                                const HogelDataset &dataset,
                                                const ExposurePlan &plan,
                                                const BenchProject &bench) {
  validateChimeraRecipe(recipe);
  validateHogelDataset(dataset);
  validateExposurePlan(plan);
  validateBenchProject(bench);
  if (!optics::scene::isStableBenchId(batchId) ||
      computeHogelDatasetContentHash(dataset) != dataset.contentHash ||
      computeExposurePlanContentHash(plan) != plan.contentHash ||
      plan.sourceRecipeId != recipe.recipeId ||
      plan.sourceDatasetId != dataset.datasetId ||
      plan.sourceBenchProjectId != bench.projectId) {
    throw std::invalid_argument(
        "cannot create CHIMERA batch from mismatched workflow artifacts");
  }
  ChimeraBatchArtifact result{
      .formatVersion = kChimeraBatchFormatVersion,
      .batchId = std::move(batchId),
      .sourceRecipeId = recipe.recipeId,
      .sourceDatasetId = dataset.datasetId,
      .sourceDatasetHash = dataset.contentHash,
      .sourceExposurePlanId = plan.planId,
      .sourceExposurePlanHash = plan.contentHash,
      .sourceBenchProjectId = bench.projectId,
      .sourceSceneRevision = bench.scene.revision(),
      .hogelCountX = recipe.hogels.countX,
      .hogelCountY = recipe.hogels.countY,
      .nextLinearHogelIndex = 0U,
      .completedHogels = {},
      .stopReason = ChimeraBatchStopReason::Ready,
      .hashAlgorithm = std::string(kHogelDatasetHashAlgorithm),
      .contentHash = {},
  };
  validateChimeraBatchArtifact(result);
  result.contentHash = computeChimeraBatchContentHash(result);
  return result;
}

void validateChimeraBatchArtifact(const ChimeraBatchArtifact &artifact) {
  if (artifact.formatVersion != kChimeraBatchFormatVersion ||
      !optics::scene::isStableBenchId(artifact.batchId) ||
      !optics::scene::isStableBenchId(artifact.sourceRecipeId) ||
      !optics::scene::isStableBenchId(artifact.sourceDatasetId) ||
      !optics::scene::isStableBenchId(artifact.sourceExposurePlanId) ||
      !optics::scene::isStableBenchId(artifact.sourceBenchProjectId) ||
      !isLowerHexHash(artifact.sourceDatasetHash) ||
      !isLowerHexHash(artifact.sourceExposurePlanHash) ||
      artifact.hashAlgorithm != kHogelDatasetHashAlgorithm ||
      (!artifact.contentHash.empty() &&
       !isLowerHexHash(artifact.contentHash))) {
    throw std::invalid_argument("CHIMERA batch identity is invalid");
  }
  const std::size_t total = checkedTotal(artifact);
  if (artifact.nextLinearHogelIndex > total ||
      artifact.completedHogels.size() != artifact.nextLinearHogelIndex ||
      (artifact.stopReason == ChimeraBatchStopReason::Complete &&
       artifact.nextLinearHogelIndex != total) ||
      (artifact.stopReason != ChimeraBatchStopReason::Complete &&
       artifact.nextLinearHogelIndex == total)) {
    throw std::invalid_argument("CHIMERA batch progress is inconsistent");
  }
  const std::set<std::string> expectedChannels{"blue", "green", "red"};
  for (std::size_t index = 0; index < artifact.completedHogels.size();
       ++index) {
    const auto &hogel = artifact.completedHogels[index];
    if (hogel.hogelX != index % artifact.hogelCountX ||
        hogel.hogelY != index / artifact.hogelCountX ||
        !std::isfinite(hogel.stageXMetres) ||
        !std::isfinite(hogel.stageYMetres)) {
      throw std::invalid_argument(
          "CHIMERA batch hogels are not canonical row-major evidence");
    }
    std::set<std::string> channels;
    for (const auto &channel : hogel.channels) {
      if (!channels.insert(channel.channelId).second ||
          !std::isfinite(channel.wavelengthMetres) ||
          channel.wavelengthMetres <= 0.0 ||
          !std::isfinite(channel.diffractionEfficiency) ||
          channel.diffractionEfficiency < 0.0 ||
          channel.diffractionEfficiency > 1.0) {
        throw std::invalid_argument(
            "CHIMERA batch RGB channel evidence is invalid");
      }
    }
    if (channels != expectedChannels) {
      throw std::invalid_argument(
          "CHIMERA batch must retain one red, green, and blue channel");
    }
  }
}

void validateChimeraBatchForWorkflow(
    const ChimeraBatchArtifact &artifact, const ChimeraRecipe &recipe,
    const HogelDataset &dataset, const ExposurePlan &plan,
    const BenchProject &bench) {
  validateProvenance(artifact, recipe, dataset, plan, bench);
}

std::string
computeChimeraBatchContentHash(const ChimeraBatchArtifact &artifact) {
  validateChimeraBatchArtifact(artifact);
  return fnv1a64(payloadToJson(artifact).dump());
}

std::string
serializeChimeraBatchArtifact(const ChimeraBatchArtifact &artifact) {
  validateChimeraBatchArtifact(artifact);
  if (computeChimeraBatchContentHash(artifact) != artifact.contentHash) {
    throw std::invalid_argument("CHIMERA batch content hash is stale");
  }
  return Json{
             {"content_hash", artifact.contentHash},
             {"format", "holobench_chimera_batch"},
             {"format_version", artifact.formatVersion},
             {"hash_algorithm", artifact.hashAlgorithm},
             {"payload", payloadToJson(artifact)},
         }
             .dump(2) +
         "\n";
}

ChimeraBatchArtifact parseChimeraBatchArtifact(std::string_view jsonText) {
  try {
    const Json document = Json::parse(jsonText);
    requireKeys(document,
                {"content_hash", "format", "format_version", "hash_algorithm",
                 "payload"},
                "CHIMERA batch document");
    if (document.at("format").get<std::string>() != "holobench_chimera_batch" ||
        document.at("format_version").get<int>() !=
            kChimeraBatchFormatVersion) {
      throw std::runtime_error("unsupported CHIMERA batch format or version");
    }
    const Json &payload = document.at("payload");
    requireKeys(payload,
                {"batch_id", "completed_hogels", "hogel_count_x",
                 "hogel_count_y", "next_linear_hogel_index",
                 "source_bench_project_id", "source_dataset_hash",
                 "source_dataset_id", "source_exposure_plan_hash",
                 "source_exposure_plan_id", "source_recipe_id",
                 "source_scene_revision", "stop_reason"},
                "CHIMERA batch payload");
    ChimeraBatchArtifact result;
    result.batchId = payload.at("batch_id").get<std::string>();
    result.sourceRecipeId = payload.at("source_recipe_id").get<std::string>();
    result.sourceDatasetId = payload.at("source_dataset_id").get<std::string>();
    result.sourceDatasetHash =
        payload.at("source_dataset_hash").get<std::string>();
    result.sourceExposurePlanId =
        payload.at("source_exposure_plan_id").get<std::string>();
    result.sourceExposurePlanHash =
        payload.at("source_exposure_plan_hash").get<std::string>();
    result.sourceBenchProjectId =
        payload.at("source_bench_project_id").get<std::string>();
    result.sourceSceneRevision =
        payload.at("source_scene_revision").get<std::uint64_t>();
    result.hogelCountX = payload.at("hogel_count_x").get<std::size_t>();
    result.hogelCountY = payload.at("hogel_count_y").get<std::size_t>();
    result.nextLinearHogelIndex =
        payload.at("next_linear_hogel_index").get<std::size_t>();
    result.stopReason =
        parseStopReason(payload.at("stop_reason").get<std::string>());
    result.hashAlgorithm = document.at("hash_algorithm").get<std::string>();
    result.contentHash = document.at("content_hash").get<std::string>();
    const Json &completed = payload.at("completed_hogels");
    if (!completed.is_array()) {
      throw std::runtime_error(
          "CHIMERA batch completed_hogels must be an array");
    }
    result.completedHogels.reserve(completed.size());
    for (const auto &encodedHogel : completed) {
      requireKeys(encodedHogel,
                  {"channels", "hogel_x", "hogel_y", "stage_x_m", "stage_y_m"},
                  "CHIMERA batch hogel");
      ChimeraBatchHogelEvidence hogel;
      hogel.hogelX = encodedHogel.at("hogel_x").get<std::size_t>();
      hogel.hogelY = encodedHogel.at("hogel_y").get<std::size_t>();
      hogel.stageXMetres = encodedHogel.at("stage_x_m").get<double>();
      hogel.stageYMetres = encodedHogel.at("stage_y_m").get<double>();
      const Json &channels = encodedHogel.at("channels");
      if (!channels.is_array() || channels.size() != 3U) {
        throw std::runtime_error(
            "CHIMERA batch hogel must contain three channels");
      }
      for (std::size_t index = 0; index < hogel.channels.size(); ++index) {
        const auto &channel = channels.at(index);
        requireKeys(channel,
                    {"channel_id", "diffraction_efficiency", "wavelength_m"},
                    "CHIMERA batch channel");
        hogel.channels[index] = {
            .channelId = channel.at("channel_id").get<std::string>(),
            .wavelengthMetres = channel.at("wavelength_m").get<double>(),
            .diffractionEfficiency =
                channel.at("diffraction_efficiency").get<double>(),
        };
      }
      result.completedHogels.push_back(std::move(hogel));
    }
    validateChimeraBatchArtifact(result);
    if (computeChimeraBatchContentHash(result) != result.contentHash) {
      throw std::runtime_error("CHIMERA batch content hash mismatch");
    }
    return result;
  } catch (const Json::exception &error) {
    throw std::runtime_error(std::string("invalid CHIMERA batch JSON: ") +
                             error.what());
  } catch (const std::invalid_argument &error) {
    throw std::runtime_error(std::string("invalid CHIMERA batch: ") +
                             error.what());
  }
}

void saveChimeraBatchArtifact(const ChimeraBatchArtifact &artifact,
                              const std::filesystem::path &path) {
  if (path.empty()) {
    throw std::invalid_argument(
        "CHIMERA batch artifact path must not be empty");
  }
  const std::string contents = serializeChimeraBatchArtifact(artifact);
  const auto temporary = temporaryPath(path);
  removeTemporary(temporary);
  try {
    writeFlushed(temporary, contents);
    atomicReplace(temporary, path);
  } catch (...) {
    removeTemporary(temporary);
    throw;
  }
}

ChimeraBatchArtifact
loadChimeraBatchArtifact(const std::filesystem::path &path) {
  std::ifstream input(path, std::ios::binary);
  if (!input) {
    throw std::runtime_error("unable to open CHIMERA batch artifact: " +
                             path.string());
  }
  const std::string contents((std::istreambuf_iterator<char>(input)),
                             std::istreambuf_iterator<char>());
  return parseChimeraBatchArtifact(contents);
}

ChimeraBatchSliceResult runChimeraBatchSlice(
    ChimeraBatchArtifact &artifact, const ChimeraRecipe &recipe,
    const HogelDataset &dataset, const ExposurePlan &plan,
    const BenchProject &bench, compute::fft::IFftBackend &fftBackend,
    std::size_t maximumHogels, const HogelExposureExecutionOptions &options,
    const std::atomic_bool *cancellationRequested) {
  if (maximumHogels == 0U) {
    throw std::invalid_argument("CHIMERA batch slice limit must be positive");
  }
  validateProvenance(artifact, recipe, dataset, plan, bench);
  ChimeraBatchSliceResult result;
  result.totalHogelCount = artifact.totalHogelCount();
  result.executedHogels.reserve(std::min(
      maximumHogels, result.totalHogelCount - artifact.nextLinearHogelIndex));
  bool cancelled = false;
  while (artifact.nextLinearHogelIndex < result.totalHogelCount &&
         result.executedHogels.size() < maximumHogels) {
    if (cancellationRequested != nullptr &&
        cancellationRequested->load(std::memory_order_relaxed)) {
      cancelled = true;
      break;
    }
    const std::size_t index = artifact.nextLinearHogelIndex;
    auto exposure = executeHogelExposure(
        recipe, dataset, plan, bench, fftBackend, index % artifact.hogelCountX,
        index / artifact.hogelCountX, options);
    artifact.completedHogels.push_back(summarizeExposure(exposure));
    ++artifact.nextLinearHogelIndex;
    // A direct exposure owns its sampled recording wavefronts. A batch result
    // crosses an explicit compact checkpoint boundary: retain the detached
    // path diagnostics and reconstruction summary, but do not accumulate six
    // preview complex fields for every completed RGB hogel in the slice.
    discardBatchSampledWavefronts(exposure);
    result.executedHogels.push_back(std::move(exposure));
  }
  if (artifact.nextLinearHogelIndex == result.totalHogelCount) {
    artifact.stopReason = ChimeraBatchStopReason::Complete;
  } else if (cancelled) {
    artifact.stopReason = ChimeraBatchStopReason::Cancelled;
  } else {
    artifact.stopReason = ChimeraBatchStopReason::SliceLimit;
  }
  artifact.contentHash = computeChimeraBatchContentHash(artifact);
  result.completedHogelCount = artifact.nextLinearHogelIndex;
  result.stopReason = artifact.stopReason;
  return result;
}

std::vector<ExecutedHogelExposure> restoreChimeraBatchReconstructionEvidence(
    const ChimeraBatchArtifact &artifact) {
  validateChimeraBatchArtifact(artifact);
  if (computeChimeraBatchContentHash(artifact) != artifact.contentHash) {
    throw std::invalid_argument("CHIMERA batch content hash is stale");
  }
  std::vector<ExecutedHogelExposure> result;
  result.reserve(artifact.completedHogels.size());
  for (const auto &hogel : artifact.completedHogels) {
    ExecutedHogelExposure exposure{
        .planId = artifact.sourceExposurePlanId,
        .planHash = artifact.sourceExposurePlanHash,
        .hogelX = hogel.hogelX,
        .hogelY = hogel.hogelY,
        .channels = {},
    };
    exposure.channels.reserve(hogel.channels.size());
    for (const auto &summary : hogel.channels) {
      ExecutedHogelChannelExposure channel;
      channel.hogelX = hogel.hogelX;
      channel.hogelY = hogel.hogelY;
      channel.stageXMetres = hogel.stageXMetres;
      channel.stageYMetres = hogel.stageYMetres;
      channel.channelId = summary.channelId;
      channel.m8VolumeRecordingInvoked = true;
      channel.sparseSlmRasterTransferredToPlacedWavePath = true;
      channel.recording.pair.geometry =
          optics::holography::PlateRecordingGeometry::Reflection;
      channel.recording.pair.wavelengthMetres = summary.wavelengthMetres;
      channel.recording.nominalReplay.kogelnikEfficiencyEvaluated = true;
      channel.recording.nominalReplay.kogelnik.diffractionEfficiency =
          summary.diffractionEfficiency;
      exposure.channels.push_back(std::move(channel));
    }
    result.push_back(std::move(exposure));
  }
  return result;
}

} // namespace holobench::app::chimera
